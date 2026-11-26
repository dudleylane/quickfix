package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.List;

/**
 * A FIX exception a callback raises reaches the engine and is collectible afterward. A reactor acceptor's fromApp
 * raises FieldNotFound on each order it receives; once the engine has rejected the message, the exception object
 * is reclaimed by the garbage collector.
 */
public class CallbackExceptionTests {
    public void testARaisedCallbackExceptionIsCollectible() throws Exception {
        int n = 50;
        List<WeakReference<Throwable>> raised = new ArrayList<>();
        Fixture.Recorder server = new Fixture.Recorder();
        server.onFromApp = (message, id) -> {
            FieldNotFound error = new FieldNotFound(44);
            synchronized (raised) {
                raised.add(new WeakReference<>(error));
            }
            throw error;
        };
        int port = Fixture.freePort();
        MemoryStoreFactory factory = new MemoryStoreFactory();
        try (AutoCloseable acceptor = Fixture.acceptor("Socket", server, factory,
                 Fixture.acceptorSettings(port, "SERVER", "CLIENT", false), null);
             AutoCloseable initiator = Fixture.initiator("Socket", new Fixture.Recorder(), factory,
                 Fixture.initiatorSettings(port, "SERVER", "CLIENT", false), null)) {
            Fixture.start(acceptor);
            Fixture.start(initiator);
            Check.isTrue(Fixture.loggedOn(TransportTests.CLIENT), "session logged on");
            for (int i = 0; i < n; ++i) {
                Session.sendToTarget(Fixture.order("ID" + i), TransportTests.CLIENT);
            }
            Check.isTrue(Check.eventually(20, () -> server.fromApp.size() >= n),
                         "the acceptor's fromApp saw " + n + " orders (saw " + server.fromApp.size() + ")");
            // Each raised exception must become collectible once the engine has it; held, none would clear.
            boolean cleared = false;
            long deadline = System.nanoTime() + 15_000_000_000L;
            while (System.nanoTime() < deadline) {
                System.gc();
                Thread.sleep(50);
                synchronized (raised) {
                    if (raised.stream().allMatch(reference -> reference.get() == null)) {
                        cleared = true;
                        break;
                    }
                }
            }
            long live;
            synchronized (raised) {
                live = raised.stream().filter(reference -> reference.get() != null).count();
            }
            Check.isTrue(cleared, live + " of " + n + " raised exceptions were still reachable after repeated GC");
        }
    }
}
