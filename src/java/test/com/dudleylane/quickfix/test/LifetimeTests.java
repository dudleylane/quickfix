package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;
import java.util.concurrent.atomic.AtomicInteger;

public class LifetimeTests {
    static final AtomicInteger received = new AtomicInteger();

    /**
     * Everything a transport is made with written inline, with nothing else referring to it, while the GC
     * runs: before the transports kept their arguments, the GC finalized the Application under the running
     * engine and the next callback used freed memory.
     */
    public void testArgumentsWrittenInlineSurviveTheGC() throws Exception {
        for (String transport : Fixture.TRANSPORTS) {
            received.set(0);
            int port = Fixture.freePort();
            boolean ssl = transport.contains("SSL");
            try (AutoCloseable acceptor = Fixture.acceptor(transport, new Fixture.Recorder() {
                     @Override public void fromApp(Message message, SessionID id) { received.incrementAndGet(); }
                 }, new MemoryStoreFactory(), Fixture.acceptorSettings(port, "SERVER", "CLIENT", ssl), new ScreenLogFactory(false, false, false));
                 AutoCloseable initiator = Fixture.initiator(transport, new Fixture.Recorder(), new MemoryStoreFactory(),
                                                             Fixture.initiatorSettings(port, "SERVER", "CLIENT", ssl), null)) {
                Fixture.start(acceptor);
                Fixture.start(initiator);
                Check.isTrue(Fixture.loggedOn(TransportTests.CLIENT), transport + ": logged on");
                for (int i = 0; i < 500; ++i) {
                    if (i % 50 == 0) {
                        System.gc();
                    }
                    Session.sendToTarget(Fixture.order("ID" + i), TransportTests.CLIENT);
                }
                Check.isTrue(Check.eventually(20, () -> received.get() == 500), transport + ": all arrived, got " + received.get());
            }
        }
    }

    /**
     * stop() on one thread and close() on another, as the shutdown hook and an application's own close() race on
     * SIGTERM. The engine's stop() returns at once to a second caller while the first still waits for the
     * transport's thread, so close() freed the acceptor under that thread (SIGSEGV in SocketServer::close).
     */
    public void testStopAndCloseRacing() throws Exception {
        for (String transport : new String[] {"Socket", "SSLSocket"}) {
            for (int round = 0; round < 20; ++round) {
                int port = Fixture.freePort();
                boolean ssl = transport.contains("SSL");
                AutoCloseable acceptor = Fixture.acceptor(transport, new Fixture.Recorder(), new MemoryStoreFactory(),
                                                          Fixture.acceptorSettings(port, "SERVER", "CLIENT", ssl), null);
                Fixture.start(acceptor);
                java.util.concurrent.CountDownLatch go = new java.util.concurrent.CountDownLatch(1);
                Thread stopper = new Thread(() -> {
                    try {
                        go.await();
                    } catch (InterruptedException e) {
                        return;
                    }
                    ((Acceptor) acceptor).stop();
                });
                stopper.start();
                go.countDown();
                acceptor.close();
                stopper.join();
                ((Acceptor) acceptor).stop(); // after close: does nothing
            }
        }
    }

    /**
     * A callback stopping its own transport while another thread stops it. The Java transports used to hold
     * their monitor across stop(), so the callback waited for the monitor while the other stop joined the
     * callback's thread: a deadlock. Since #174 the engine takes concurrent stops, and the callback's returns
     * at once.
     */
    public void testACallbackStopsItsOwnTransport() throws Exception {
        int port = Fixture.freePort();
        java.util.concurrent.atomic.AtomicReference<ThreadedSocketAcceptor> self = new java.util.concurrent.atomic.AtomicReference<>();
        java.util.concurrent.CountDownLatch inCallback = new java.util.concurrent.CountDownLatch(1);
        java.util.concurrent.CountDownLatch callbackReturned = new java.util.concurrent.CountDownLatch(1);
        Fixture.Recorder server = new Fixture.Recorder();
        server.onFromApp = (message, id) -> {
            inCallback.countDown();
            try {
                Thread.sleep(200); // the other stop starts meanwhile
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            self.get().stop();
            callbackReturned.countDown();
        };
        ThreadedSocketAcceptor acceptor = new ThreadedSocketAcceptor(server, new MemoryStoreFactory(),
                                                                     Fixture.acceptorSettings(port, "SERVER", "CLIENT", false));
        self.set(acceptor);
        try (SocketInitiator initiator = new SocketInitiator(new Fixture.Recorder(), new MemoryStoreFactory(),
                                                             Fixture.initiatorSettings(port, "SERVER", "CLIENT", false))) {
            acceptor.start();
            initiator.start();
            Check.isTrue(Fixture.loggedOn(TransportTests.CLIENT), "logged on");
            Session.sendToTarget(Fixture.order("ID"), TransportTests.CLIENT);
            Check.isTrue(inCallback.await(10, java.util.concurrent.TimeUnit.SECONDS), "in the callback");
            Thread stopper = new Thread(acceptor::stop, "stopper");
            stopper.setDaemon(true); // if it deadlocks, it must not hold the JVM open
            stopper.start();
            // stop's two waits, logout then onStop's, are 10 s and 5 s at most
            Check.isTrue(callbackReturned.await(40, java.util.concurrent.TimeUnit.SECONDS), "the callback's stop() returned");
            stopper.join(40000);
            Check.isTrue(!stopper.isAlive(), "the other stop() returned");
        }
        acceptor.close();
    }
}
