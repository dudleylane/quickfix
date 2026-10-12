#include "config.h"

#include <Log.h>
#include <MessageStore.h>
#include <SessionSettings.h>
#include <SocketInitiator.h>
#include <ThreadedSocketInitiator.h>

#include "TestHelper.h"
#include "catch_amalgamated.hpp"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace FIX;

// Each session waits its own ReconnectInterval between connection attempts. The
// initiators kept one interval for all of their sessions, overwritten by
// whichever session connected last, so the others retried at its pace (#125).

namespace
{
// Counts each session's "Connecting to" events: one per connection attempt.
class AttemptLog : public NullLog
{
public:
    explicit AttemptLog(std::atomic<int> &attempts) : m_attempts(attempts) {}
    void onEvent(const std::string &event) override
    {
        if (event.rfind("Connecting to", 0) == 0)
        {
            ++m_attempts;
        }
    }

private:
    std::atomic<int> &m_attempts;
};

class AttemptLogFactory : public LogFactory
{
public:
    Log *create() override { return new NullLog; }
    Log *create(const SessionID &sessionID) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return new AttemptLog(m_attempts[sessionID.getTargetCompID().getValue()]);
    }
    void destroy(Log *log) override { delete log; }
    int attempts(const std::string &target)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_attempts[target];
    }

private:
    std::mutex m_mutex;
    std::map<std::string, std::atomic<int>> m_attempts;
};

// A port nothing listens on, so every attempt fails at once and the session is
// disconnected again for the next one.
int closedPort()
{
    int probe = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(probe, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    socklen_t length = sizeof(address);
    ::getsockname(probe, reinterpret_cast<sockaddr *>(&address), &length);
    ::close(probe);
    return ntohs(address.sin_port);
}

SessionSettings twoIntervals(int port)
{
    std::stringstream config;
    config << "[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=" << port
           << "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\nHeartBtInt=30\nReconnectInterval=30\n"
           << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=RECONNECT\nTargetCompID=FAST\nReconnectInterval=1\n"
           << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=RECONNECT\nTargetCompID=SLOW\n";
    return SessionSettings(config);
}

template <typename InitiatorType> void checkIntervals()
{
    NullApplication application;
    MemoryStoreFactory storeFactory;
    AttemptLogFactory logFactory;
    InitiatorType initiator(application, storeFactory, twoIntervals(closedPort()), logFactory);

    initiator.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(3500));
    initiator.stop(true);

    // FAST retries about every second, SLOW not again within its 30 s.
    CHECK(logFactory.attempts("FAST") >= 3);
    CHECK(logFactory.attempts("SLOW") == 1);
}
} // namespace

TEST_CASE("InitiatorReconnectTests")
{
    SECTION("eachSessionWaitsItsOwnIntervalOnTheReactor") { checkIntervals<SocketInitiator>(); }

    SECTION("eachSessionWaitsItsOwnIntervalOnTheThreadedTransport") { checkIntervals<ThreadedSocketInitiator>(); }

    SECTION("anIntervalThatIsNotANumberFailsAtConstruction")
    {
        NullApplication application;
        MemoryStoreFactory storeFactory;
        std::stringstream config;
        config << "[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=1\n"
               << "StartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\nHeartBtInt=30\n"
               << "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=RECONNECT\nTargetCompID=BAD\nReconnectInterval=soon\n";
        CHECK_THROWS_AS(SocketInitiator(application, storeFactory, SessionSettings(config)), ConfigError);
    }
}
