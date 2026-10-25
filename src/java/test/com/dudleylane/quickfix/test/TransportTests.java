package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

public class TransportTests {
    static final SessionID CLIENT = new SessionID("FIX.4.2", "CLIENT", "SERVER");

    /** Logs on over transport, runs body, and closes both ends. */
    interface Body {
        void run(Fixture.Recorder server, Fixture.Recorder client) throws Exception;
    }

    static void session(String transport, LogFactory logFactory, Fixture.Recorder server, Fixture.Recorder client,
                        Body body) throws Exception {
        boolean ssl = transport.contains("SSL");
        int port = Fixture.freePort();
        MemoryStoreFactory factory = new MemoryStoreFactory();
        try (AutoCloseable acceptor = Fixture.acceptor(transport, server, factory,
                                                       Fixture.acceptorSettings(port, "SERVER", "CLIENT", ssl), logFactory);
             AutoCloseable initiator = Fixture.initiator(transport, client, factory,
                                                         Fixture.initiatorSettings(port, "SERVER", "CLIENT", ssl), logFactory)) {
            Fixture.start(acceptor);
            Fixture.start(initiator);
            body.run(server, client);
        }
    }

    static void logsOnAndExchanges(String transport) throws Exception {
        session(transport, null, new Fixture.Recorder(), new Fixture.Recorder(), (server, client) -> {
            Check.isTrue(Fixture.loggedOn(CLIENT), transport + ": logged on");
            Check.isTrue(Check.eventually(10, () -> server.logons.get() == 1), transport + ": the acceptor saw the logon");
            for (int i = 0; i < 200; ++i) {
                Check.isTrue(Session.sendToTarget(Fixture.order("ID" + i), CLIENT), "sent " + i);
            }
            Check.isTrue(Check.eventually(20, () -> server.fromApp.size() == 200),
                         transport + ": 200 orders arrived, got " + server.fromApp.size());
            Session session = Session.lookupSession(CLIENT);
            Check.isTrue(session.getStore().getNextSenderMsgSeqNum() > 200, transport + ": the session's store, typed");
            session.getLog().onEvent("from the test");
            Check.isTrue(session.getAllowedRemoteAddresses().isEmpty(), transport + ": a StringSet");
        });
    }

    public void testSocket() throws Exception { logsOnAndExchanges("Socket"); }
    public void testThreadedSocket() throws Exception { logsOnAndExchanges("ThreadedSocket"); }
    public void testSSLSocket() throws Exception { logsOnAndExchanges("SSLSocket"); }
    public void testThreadedSSLSocket() throws Exception { logsOnAndExchanges("ThreadedSSLSocket"); }

    /** A Log a Java LogFactory makes, kept by the factory while the GC runs (quickfix.i). */
    static class EventLog extends Log {
        final List<String> events;
        EventLog(List<String> events) { this.events = events; }
        @Override public void clear() {}
        @Override public void backup() {}
        @Override public void onIncoming(String message) { events.add("in " + message.substring(0, 12)); }
        @Override public void onOutgoing(String message) { events.add("out " + message.substring(0, 12)); }
        @Override public void onEvent(String text) { events.add(text); }
    }

    static class EventLogFactory extends LogFactory {
        final List<String> events = new CopyOnWriteArrayList<>();
        final List<Log> destroyed = new CopyOnWriteArrayList<>();
        @Override public Log create() { return new EventLog(events); }
        @Override public Log create(SessionID id) { return new EventLog(events); }
        @Override public void destroy(Log log) { destroyed.add(log); }
    }

    public void testAJavaLogFactory() throws Exception {
        for (String transport : Fixture.TRANSPORTS) {
            EventLogFactory logs = new EventLogFactory();
            session(transport, logs, new Fixture.Recorder(), new Fixture.Recorder(), (server, client) -> {
                Check.isTrue(Fixture.loggedOn(CLIENT), transport + ": logged on");
                for (int i = 0; i < 5; ++i) {
                    System.gc();
                    Session.sendToTarget(Fixture.order("ID" + i), CLIENT);
                }
                Check.isTrue(Check.eventually(10, () -> server.fromApp.size() == 5), transport + ": orders arrived");
            });
            Check.isTrue(logs.events.stream().anyMatch(e -> e.startsWith("Created session")), transport + ": events logged");
            Check.isTrue(logs.events.stream().anyMatch(e -> e.startsWith("out 8=FIX.4.2")), transport + ": messages logged");
            Check.isTrue(!logs.destroyed.isEmpty(), transport + ": the factory's destroy got its logs back");
            Check.isTrue(logs.destroyed.stream().allMatch(l -> l instanceof EventLog),
                         transport + ": destroy got the factory's own objects");
        }
    }

    public void testRejectLogonRefusesTheLogon() throws Exception {
        Fixture.Recorder server = new Fixture.Recorder();
        server.onFromAdmin = (message, id) -> {
            if (message.getHeader().getField(35).equals("A")) {
                throw new RejectLogon("not today");
            }
        };
        session("ThreadedSocket", null, server, new Fixture.Recorder(), (s, client) -> {
            Check.isTrue(Check.eventually(10, () -> client.fromAdmin.contains("5")), "the initiator was logged out");
            Check.equal(0, s.logons.get(), "the acceptor never logged on");
        });
    }

    public void testDoNotSendKeepsAMessageOffTheWire() throws Exception {
        Fixture.Recorder client = new Fixture.Recorder();
        client.onToApp = (message, id) -> {
            if (message.getField(11).equals("SECRET")) {
                throw new DoNotSend();
            }
        };
        session("Socket", null, new Fixture.Recorder(), client, (server, c) -> {
            Check.isTrue(Fixture.loggedOn(CLIENT), "logged on");
            Check.isTrue(!Session.sendToTarget(Fixture.order("SECRET"), CLIENT), "send reports it was not sent");
            Session.sendToTarget(Fixture.order("PUBLIC"), CLIENT);
            Check.isTrue(Check.eventually(10, () -> server.fromApp.size() == 1), "the second arrived");
            Thread.sleep(200);
            Check.equal(1, server.fromApp.size(), "and only the second");
        });
    }

    static void answersWithABusinessReject(String transport, RuntimeException thrown) throws Exception {
        Fixture.Recorder server = new Fixture.Recorder();
        server.onFromApp = (message, id) -> { throw thrown; };
        session(transport, null, server, new Fixture.Recorder(), (s, client) -> {
            Check.isTrue(Fixture.loggedOn(CLIENT), "logged on");
            Session.sendToTarget(Fixture.order("ID"), CLIENT);
            Check.isTrue(Check.eventually(10, () -> client.fromApp.contains("j")),
                         transport + ": " + thrown.getClass().getSimpleName() + " drew a BusinessMessageReject");
            Check.isTrue(Session.lookupSession(CLIENT).isLoggedOn(), "and the session stayed up");
        });
    }

    public void testUnsupportedMessageTypeIsRejected() throws Exception {
        answersWithABusinessReject("ThreadedSocket", new UnsupportedMessageType());
        answersWithABusinessReject("Socket", new UnsupportedMessageType());
    }

    public void testFieldNotFoundIsRejected() throws Exception {
        answersWithABusinessReject("ThreadedSocket", new FieldNotFound(58));
    }

    public void testCloseFreesTheSessions() throws Exception {
        for (int round = 0; round < 3; ++round) {
            logsOnAndExchanges("ThreadedSocket");
        }
    }

    public void testAConflictingTransportIsConfigError() throws Exception {
        int port = Fixture.freePort();
        MemoryStoreFactory factory = new MemoryStoreFactory();
        try (SocketAcceptor first = new SocketAcceptor(new Fixture.Recorder(), factory,
                                                       Fixture.acceptorSettings(port, "SERVER", "CLIENT", false))) {
            Check.isTrue(first.getSessions().size() == 1, "the first holds the session");
            ConfigError e = Check.raises(ConfigError.class, () -> new SocketAcceptor(
                new Fixture.Recorder(), factory, Fixture.acceptorSettings(port, "SERVER", "CLIENT", false)));
            Check.isTrue(e.getMessage().contains("already exists"), e.getMessage());
        }
    }
}
