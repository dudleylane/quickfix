/* -*- C++ -*- */

// Several threads sending on one session, which pt cannot show because its rows send from one thread (#184).
//
// Open loop. Each sender follows its own schedule of exponential gaps, drawn before it starts, so a stalled sender's
// next messages are late rather than not yet sent. Waiting out a gap after each send() returns hides the stalls being
// measured. End to end runs from the time a message was due to the peer's fromApp, and send() is the call's duration.
// With no gap the senders run flat out, and end to end is then mostly the length of whatever queue builds up.
//
// The first tenth of each sender's messages is dropped as warm-up. Senders spin between messages, so each needs a CPU,
// as does the receiver with -r and the sending connection's thread with -b: past the machine's physical cores this
// measures the scheduler. Like pt it prints timings and judges nothing; compare runs on a quiet machine. It fails only
// if the session does not log on, a send is refused or a message does not arrive.

#include "config.h"

#include "Application.h"
#include "FileStore.h"
#include "MessageStore.h"
#include "Session.h"
#include "SessionSettings.h"
#include "ThreadedSocketAcceptor.h"
#include "ThreadedSocketInitiator.h"
#include "fix42/NewOrderSingle.h"
#include "getopt-repl.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

// Holds the time a message was due. No dictionary is in use, so nothing refuses the tag.
constexpr int DUE_TAG = 5000;

long nowNanos()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

// The peer. One connection's thread calls fromApp, so only it writes the samples, and they are read once received has
// reached the total.
struct Receiver : FIX::NullApplication
{
    explicit Receiver(long total) : endToEnd(total) {}

    void fromApp(const FIX::Message &message, const FIX::SessionID &)
        EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue,
               FIX::UnsupportedMessageType) override
    {
        const long at = received.load(std::memory_order_relaxed);
        if (at < static_cast<long>(endToEnd.size()))
        {
            endToEnd[at] = nowNanos() - std::atol(message.getField(DUE_TAG).c_str());
        }
        received.store(at + 1, std::memory_order_release);
    }

    std::vector<long> endToEnd;
    std::atomic<long> received{0};
};

struct Options
{
    int port = 0;
    int senders = 2;
    int count = 50000;
    int gapMicros = 100;
    bool queuedWrites = false;
    bool busyPoll = false;
    bool receiverBusyPoll = false;
    std::string fileStore;
};

void report(const char *name, std::vector<long> &samples)
{
    std::sort(samples.begin(), samples.end());
    const auto at = [&](double q)
    { return samples[std::min(samples.size() - 1, static_cast<size_t>(q * samples.size()))] / 1000.0; };
    std::cout << std::fixed << std::setprecision(2) << name << "  p50 " << at(0.50) << "  p99 " << at(0.99)
              << "  p99.9 " << at(0.999) << "  max " << samples.back() / 1000.0 << " us" << std::endl;
}

std::vector<long> drawGaps(int sender, const Options &options)
{
    std::vector<long> gaps(options.gapMicros ? options.count : 0);
    std::mt19937_64 random(sender + 1);
    std::exponential_distribution<double> exponential(1.0 / std::max(options.gapMicros * 1000.0, 1.0));
    for (long &gap : gaps)
    {
        gap = static_cast<long>(exponential(random));
    }
    return gaps;
}

int measure(const Options &options)
{
    // Separate settings for each transport: an acceptor checks that every session it reads agrees on its socket
    // options. Non-stop sessions: a daily one resets at 00:00 UTC and drops the message in hand (#186).
    std::stringstream common;
    common << "[DEFAULT]\nSocketConnectHost=127.0.0.1\nSocketConnectPort=" << options.port
           << "\nSocketAcceptPort=" << options.port
           << "\nNonStopSession=Y\nUseDataDictionary=N\nBeginString=FIX.4.2\nResetOnLogon=Y\n"
           << "PersistMessages=" << (options.fileStore.empty() ? "N" : "Y") << "\n";
    std::stringstream acceptorConfig, initiatorConfig;
    acceptorConfig << common.str() << "[SESSION]\nConnectionType=acceptor\nSenderCompID=SERVER\nTargetCompID=CLIENT\n"
                   << "SocketBusyPoll=" << (options.receiverBusyPoll ? "Y" : "N") << "\n";
    initiatorConfig << common.str() << "[SESSION]\nConnectionType=initiator\nSenderCompID=CLIENT\nTargetCompID=SERVER\n"
                    << "HeartBtInt=30\nSocketQueuedWrites=" << (options.queuedWrites ? "Y" : "N")
                    << "\nSocketBusyPoll=" << (options.busyPoll ? "Y" : "N") << "\n";
    FIX::SessionSettings acceptorSettings(acceptorConfig), initiatorSettings(initiatorConfig);

    const long total = static_cast<long>(options.senders) * options.count;
    Receiver receiver(total);
    FIX::NullApplication sender;
    FIX::MemoryStoreFactory memoryStore;
    std::unique_ptr<FIX::MessageStoreFactory> fileStore;
    if (!options.fileStore.empty())
    {
        fileStore.reset(new FIX::FileStoreFactory(options.fileStore));
    }

    FIX::ThreadedSocketAcceptor acceptor(receiver, memoryStore, acceptorSettings);
    acceptor.start();
    FIX::ThreadedSocketInitiator initiator(sender, fileStore ? *fileStore : memoryStore, initiatorSettings);
    initiator.start();

    FIX::Session *session = nullptr;
    for (int i = 0; i < 1000 && !(session && session->isLoggedOn()); ++i)
    {
        session = FIX::Session::lookupSession(FIX::SessionID("FIX.4.2", "CLIENT", "SERVER"));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!session || !session->isLoggedOn())
    {
        initiator.stop(true);
        acceptor.stop(true);
        std::cerr << "FAILED: the session did not log on" << std::endl;
        return EXIT_FAILURE;
    }

    std::vector<std::vector<long>> sendTimes(options.senders);
    std::atomic<bool> go{false};
    std::atomic<long> startAt{0};
    std::atomic<long> refused{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < options.senders; ++t)
    {
        threads.emplace_back(
            [&, t]
            {
                FIX42::NewOrderSingle order(FIX::ClOrdID("ORDER-0000000001"), FIX::HandlInst('1'), FIX::Symbol("MSFT"),
                                            FIX::Side(FIX::Side_BUY), FIX::TransactTime(),
                                            FIX::OrdType(FIX::OrdType_MARKET));
                std::vector<long> &times = sendTimes[t];
                times.reserve(options.count);
                const std::vector<long> gaps = drawGaps(t, options);
                while (!go)
                {
                    std::this_thread::yield();
                }
                long due = startAt.load();
                for (int i = 0; i < options.count; ++i)
                {
                    if (options.gapMicros)
                    {
                        due += gaps[i];
                        while (nowNanos() < due)
                        {
                        }
                    }
                    else
                    {
                        due = nowNanos();
                    }
                    order.setField(FIX::StringField(DUE_TAG, std::to_string(due)));
                    const auto before = Clock::now();
                    if (!session->send(order))
                    {
                        ++refused;
                    }
                    times.push_back(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - before).count());
                }
            });
    }
    // Long enough for every sender to have drawn its gaps before the first is due.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto start = Clock::now();
    startAt = nowNanos() + 1000000;
    go = true;
    for (std::thread &thread : threads)
    {
        thread.join();
    }

    // A lost message would otherwise wait for ever: give up after 30 s with none arriving.
    long seen = -1;
    auto lastArrival = Clock::now();
    while (receiver.received.load(std::memory_order_acquire) < total &&
           Clock::now() - lastArrival < std::chrono::seconds(30))
    {
        const long received = receiver.received.load(std::memory_order_relaxed);
        if (received != seen)
        {
            seen = received;
            lastArrival = Clock::now();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const double elapsed = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
    const long received = receiver.received.load(std::memory_order_acquire);

    initiator.stop(true);
    acceptor.stop(true);

    if (refused != 0 || received != total)
    {
        std::cerr << "FAILED: " << refused << " sends refused, " << received << " of " << total << " messages arrived"
                  << std::endl;
        return EXIT_FAILURE;
    }

    std::vector<long> sends;
    for (const std::vector<long> &times : sendTimes)
    {
        sends.insert(sends.end(), times.begin() + times.size() / 10, times.end());
    }
    std::vector<long> arrivals(receiver.endToEnd.begin() + total / 10, receiver.endToEnd.end());

    std::cout << options.senders << " senders, ";
    if (options.gapMicros)
    {
        std::cout << "each every " << options.gapMicros << " us on average";
    }
    else
    {
        std::cout << "flat out";
    }
    std::cout << "; SocketQueuedWrites=" << (options.queuedWrites ? "Y" : "N")
              << " SocketBusyPoll=" << (options.busyPoll ? "Y" : "N") << "; receiver "
              << (options.receiverBusyPoll ? "spinning" : "sleeping") << "; "
              << (options.fileStore.empty() ? "MemoryStore" : "FileStore") << std::endl;
    report("send()    ", sends);
    report("end to end", arrivals);
    std::cout << std::fixed << std::setprecision(2) << elapsed / total << " us a message" << std::endl;
    return EXIT_SUCCESS;
}
} // namespace

int main(int argc, char **argv)
{
    Options options;
    int opt;
    while ((opt = getopt(argc, argv, "+p:s:c:g:qbrf:")) != -1)
    {
        switch (opt)
        {
        case 'p':
            options.port = atoi(optarg);
            break;
        case 's':
            options.senders = atoi(optarg);
            break;
        case 'c':
            options.count = atoi(optarg);
            break;
        case 'g':
            options.gapMicros = atoi(optarg);
            break;
        case 'q':
            options.queuedWrites = true;
            break;
        case 'b':
            options.busyPoll = true;
            break;
        case 'r':
            options.receiverBusyPoll = true;
            break;
        case 'f':
            options.fileStore = optarg;
            break;
        default:
            options.port = 0;
            break;
        }
    }
    if (options.port <= 0 || options.senders < 1 || options.count < 1 || options.gapMicros < 0)
    {
        std::cout << "usage: " << argv[0] << " -p port [-s senders] [-c messages per sender]\n"
                  << "    [-g each sender's mean gap in us, 0 for flat out] [-q SocketQueuedWrites=Y]\n"
                  << "    [-b SocketBusyPoll=Y on the sender's side] [-r SocketBusyPoll=Y on the receiver's side]\n"
                  << "    [-f FileStore directory, on the sender's side]" << std::endl;
        return EXIT_FAILURE;
    }

    try
    {
        return measure(options);
    }
    catch (std::exception const &e)
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
