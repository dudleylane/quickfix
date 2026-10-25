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
}
