package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;
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

    /** Every SessionID each callback is handed, kept. */
    static class Keeper extends Application {
        final Map<String, List<SessionID>> handed = new ConcurrentHashMap<>();

        void keep(String callback, SessionID id) {
            handed.computeIfAbsent(callback, name -> new CopyOnWriteArrayList<>()).add(id);
        }

        @Override public void onCreate(SessionID id) { keep("onCreate", id); }
        @Override public void onLogon(SessionID id) { keep("onLogon", id); }
        @Override public void onLogout(SessionID id) { keep("onLogout", id); }
        @Override public void toAdmin(Message message, SessionID id) { keep("toAdmin", id); }
        @Override public void toApp(Message message, SessionID id) { keep("toApp", id); }
        @Override public void fromAdmin(Message message, SessionID id) { keep("fromAdmin", id); }
        @Override public void fromApp(Message message, SessionID id) { keep("fromApp", id); }
    }

    static class KeepingLogFactory extends TransportTests.EventLogFactory {
        final List<SessionID> kept = new CopyOnWriteArrayList<>();

        @Override
        public Log create(SessionID id) {
            kept.add(id);
            return super.create(id);
        }
    }

    /** Whether the proxy frees its native SessionID: SWIG's flag, which nothing public reads. */
    static boolean owned(SessionID id) throws ReflectiveOperationException {
        java.lang.reflect.Field own = SessionID.class.getDeclaredField("swigCMemOwn");
        own.setAccessible(true);
        return own.getBoolean(id);
    }

    /** The native SessionID the proxy wraps: SWIG's pointer, which nothing public reads. */
    static long pointer(SessionID id) throws ReflectiveOperationException {
        java.lang.reflect.Field cPtr = SessionID.class.getDeclaredField("swigCPtr");
        cPtr.setAccessible(true);
        return cPtr.getLong(id);
    }

    /**
     * A SessionID the binding hands Java -- a callback's argument, LogFactory.create's, Session.getSessionID()'s
     * result -- is the binding's one copy of that ID, which every SessionID handed out for it shares and nothing
     * frees (#190). Each was a view of the session's own ID, and one kept past close(), which frees the
     * transport's sessions, read freed memory.
     */
    public void testAHandedSessionIDOutlivesItsTransport() throws Exception {
        List<String> serverCallbacks = List.of("onCreate", "onLogon", "toAdmin", "fromAdmin", "fromApp");
        List<String> clientCallbacks = List.of("onCreate", "onLogon", "toAdmin", "fromAdmin", "toApp");
        for (String transport : new String[] {"Socket", "ThreadedSocket"}) {
            int port = Fixture.freePort();
            Keeper server = new Keeper();
            Keeper client = new Keeper();
            KeepingLogFactory logs = new KeepingLogFactory();
            SessionSettings serverSettings = Fixture.acceptorSettings(port, "SERVER", "CLIENT", false);
            SessionSettings clientSettings = Fixture.initiatorSettings(port, "SERVER", "CLIENT", false);
            List<SessionID> kept = new ArrayList<>();
            try (AutoCloseable acceptor = Fixture.acceptor(transport, server, new MemoryStoreFactory(), serverSettings, logs);
                 AutoCloseable initiator = Fixture.initiator(transport, client, new MemoryStoreFactory(), clientSettings, null)) {
                Fixture.start(acceptor);
                Fixture.start(initiator);
                Check.isTrue(Fixture.loggedOn(TransportTests.CLIENT), transport + ": logged on");
                Session.sendToTarget(Fixture.order("KEPT"), TransportTests.CLIENT);
                Check.isTrue(Check.eventually(10, () -> server.handed.containsKey("fromApp")), transport + ": order arrived");
                kept.add(Session.lookupSession(TransportTests.CLIENT).getSessionID());
            }
            Check.isTrue(Session.lookupSession(TransportTests.CLIENT) == null, transport + ": close() freed the session");
            Check.isTrue(server.handed.keySet().containsAll(serverCallbacks)
                             && client.handed.keySet().containsAll(clientCallbacks) && !logs.kept.isEmpty(),
                         transport + ": every callback ran: " + server.handed.keySet() + ", " + client.handed.keySet());
            kept.addAll(logs.kept);
            for (Keeper keeper : List.of(server, client)) {
                keeper.handed.values().forEach(kept::addAll);
            }
            Map<String, Long> copies = new HashMap<>();
            for (SessionID id : kept) {
                Check.isTrue(!owned(id), transport + ": a handed SessionID is the binding's, not Java's own");
                String text = id.toString(); // freed memory, while these were views
                Check.isTrue(text.equals("FIX.4.2:CLIENT->SERVER") || text.equals("FIX.4.2:SERVER->CLIENT"),
                             transport + ": a kept SessionID reads " + text);
                Long copy = copies.putIfAbsent(text, pointer(id));
                Check.isTrue(copy == null || copy == pointer(id), transport + ": one copy of " + text + " is handed out");
            }
        }
    }

    /**
     * A SessionID the binding hands Java is shared by every one handed out for its session, so it cannot change
     * (#190): fromString throws for it, and every SessionID's getters return copies. It was a view of the session's
     * own ID, so changing it changed the engine's.
     */
    public void testAHandedSessionIDCannotChange() throws Exception {
        List<Throwable> refused = new CopyOnWriteArrayList<>();
        Fixture.Recorder application = new Fixture.Recorder() {
            @Override public void onCreate(SessionID id) {
                try {
                    id.fromString("FIX.4.2:CHANGED->BYAPP");
                } catch (UnsupportedOperationException e) {
                    refused.add(e); // thrown out of a callback, it would end the process
                }
                id.getSenderCompID().setValue("CHANGED");
            }
        };
        SessionSettings settings = Fixture.initiatorSettings(Fixture.freePort(), "SERVER", "CHANGER", false);
        // Never started: making it makes its session, which calls onCreate, and closing it frees the session.
        AutoCloseable initiator = Fixture.initiator("Socket", application, new MemoryStoreFactory(), settings, null);
        try {
            Check.equal(1, refused.size(), "onCreate's fromString threw");
            Session session = Session.lookupSession(new SessionID("FIX.4.2", "CHANGER", "SERVER"));
            Check.isTrue(session != null, "the session is found by its ID");
            SessionID handed = session.getSessionID();
            Check.equal("FIX.4.2:CHANGER->SERVER", handed.toString(), "after onCreate tried to change its argument");
            Check.equal("CHANGER", handed.getSenderCompID().getValue(), "its SenderCompID");
            Check.raises(UnsupportedOperationException.class, () -> handed.fromString("FIX.4.2:CHANGED->BYAPP"));
            Check.equal("FIX.4.2:CHANGER->SERVER", session.getSessionID().toString(), "after fromString was refused");
        } finally {
            initiator.close();
        }
        SessionID own = new SessionID("FIX.4.2", "A", "B");
        own.getSenderCompID().setValue("CHANGED");
        Check.equal("A", own.getSenderCompID().getValue(), "an application's SessionID, after its getter's copy changed");
        own.fromString("FIX.4.2:C->D");
        Check.equal("FIX.4.2:C->D", own.toString(), "an application's own SessionID changes");
    }
}
