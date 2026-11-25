package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;
import java.lang.ref.Reference;
import java.util.Map;

/**
 * The child JVM of TransportGcTests: transports made with their store and log factories written inline, then dropped
 * without close(), so that the GC finds each unreachable together with what it was made with and runs their
 * finalizers in any order (#201). Before, a factory's could run first, and the transport's destructor, which destroys
 * its sessions' stores and logs through them, used freed memory. Prints key=value lines.
 * <ul>
 * <li>acceptor: SocketAcceptors, half with a LogFactory written in Java, half without</li>
 * <li>initiator: SocketInitiators, the same</li>
 * <li>base: a subclass of SocketAcceptorBase that keeps nothing it was made with, so only the pins hold them</li>
 * <li>stopped: SocketAcceptors started and stopped first</li>
 * <li>factorycycle: each LogFactory refers to its transport. Pinned, it keeps the transport, which is never
 * finalized, and its session stays registered</li>
 * <li>appfactorycycle: the same through the Application, which the LogFactory refers to</li>
 * </ul>
 */
public final class TransportGcChild {
    static final int COUNT = 40;

    private TransportGcChild() {}

    static final class QuietLog extends Log {
        @Override public void clear() {}
        @Override public void backup() {}
        @Override public void onIncoming(String message) {}
        @Override public void onOutgoing(String message) {}
        @Override public void onEvent(String text) {}
    }

    /** Keeps nothing: the binding keeps each Log a LogFactory creates until destroy (LogFactory.swigKeep). */
    static final class QuietLogFactory extends LogFactory {
        @Override public Log create() { return new QuietLog(); }
        @Override public Log create(SessionID id) { return new QuietLog(); }
        @Override public void destroy(Log log) {}
    }

    /** Refers to the transport it makes logs for, directly or through its application. */
    static final class ReachingLogFactory extends LogFactory {
        Object transport;
        HoldingApp app;
        @Override public Log create() { return new QuietLog(); }
        @Override public Log create(SessionID id) { return new QuietLog(); }
        @Override public void destroy(Log log) {}
    }

    static final class HoldingApp extends Fixture.Recorder {
        Object transport;
    }

    /** Keeps nothing it was made with, unlike the classes in main/. */
    static final class BareAcceptor extends SocketAcceptorBase {
        BareAcceptor(Application application, MessageStoreFactory storeFactory, SessionSettings settings,
                     LogFactory logFactory) {
            super(application, storeFactory, settings, logFactory);
        }
    }

    static SessionSettings settings(boolean acceptor, int index, int port) {
        return Fixture.settings("[DEFAULT]\nNonStopSession=Y\nUseDataDictionary=N\nHeartBtInt=30\n"
                                + (acceptor ? "ConnectionType=acceptor\nSocketAcceptPort=" + port + "\n"
                                            : "ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort="
                                                  + port + "\n")
                                + "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=GC" + index + "\nTargetCompID=TV\n");
    }

    static Object make(String mode, int index) throws Exception {
        LogFactory logs = index % 2 == 0 ? new QuietLogFactory() : null;
        switch (mode) {
        case "acceptor":
            return Fixture.acceptor("Socket", new Fixture.Recorder(), new MemoryStoreFactory(), settings(true, index, 1),
                                    logs);
        case "initiator":
            return Fixture.initiator("Socket", new Fixture.Recorder(), new MemoryStoreFactory(),
                                     settings(false, index, 1), logs);
        case "base":
            return new BareAcceptor(new Fixture.Recorder(), new MemoryStoreFactory(), settings(true, index, 1),
                                    new QuietLogFactory());
        case "stopped":
            AutoCloseable transport = Fixture.acceptor("Socket", new Fixture.Recorder(), new MemoryStoreFactory(),
                                                       settings(true, index, Fixture.freePort()), logs);
            Fixture.start(transport);
            ((Acceptor) transport).stop();
            return transport;
        case "factorycycle":
        case "appfactorycycle":
            HoldingApp app = new HoldingApp();
            ReachingLogFactory reaching = new ReachingLogFactory();
            AutoCloseable made = Fixture.acceptor("Socket", app, new MemoryStoreFactory(), settings(true, index, 1),
                                                  reaching);
            if (mode.equals("factorycycle")) {
                reaching.transport = made;
            } else {
                reaching.app = app;
                app.transport = made;
            }
            return made;
        default:
            throw new IllegalArgumentException(mode);
        }
    }

    /** How many transports' factories are pinned: the binding's count, which nothing public reads. */
    static String pins() throws ReflectiveOperationException {
        Class<?> arguments;
        try {
            arguments = Class.forName("com.dudleylane.quickfix.TransportArguments");
        } catch (ClassNotFoundException e) {
            return "none";
        }
        java.lang.reflect.Field pinned = arguments.getDeclaredField("PINNED");
        pinned.setAccessible(true);
        return Integer.toString(((Map<?, ?>) pinned.get(null)).size());
    }

    static void collect() throws InterruptedException {
        System.gc();
        Thread.sleep(20);
    }

    public static void main(String[] args) throws Exception {
        String mode = args[0];
        Object kept = make(mode, COUNT);
        System.out.println("pinned=" + pins());
        Reference.reachabilityFence(kept);
        kept = null;
        for (int i = 0; i < COUNT; ++i) {
            make(mode, i);
            if (i % 10 == 9) {
                collect();
            }
        }
        // Each transport's finalizer deletes it and lets its factories go, which a later collection finalizes.
        // A factory that reaches its transport keeps it, so its pins never go: a few collections show that.
        int rounds = mode.endsWith("factorycycle") ? 25 : 250;
        for (int i = 0; i < rounds && !"0".equals(pins()); ++i) {
            collect();
        }
        System.out.println("collected=true");
        System.out.println("pins=" + pins());
        int registered = 0;
        for (int i = 0; i <= COUNT; ++i) {
            if (Session.lookupSession(new SessionID("FIX.4.2", "GC" + i, "TV")) != null) {
                ++registered;
            }
        }
        System.out.println("registered=" + registered);
        System.out.println("end=true");
    }
}
