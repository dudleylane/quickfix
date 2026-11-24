package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;
import com.dudleylane.quickfix.field.BeginString;
import com.dudleylane.quickfix.field.MsgType;
import com.dudleylane.quickfix.field.SenderCompID;
import com.dudleylane.quickfix.field.TargetCompID;
import java.lang.ref.WeakReference;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * What a transport hands out of itself -- a Session from lookupSession, registerSession or getSession, a session's
 * store and log, and the transport's log -- points into the transport. Each such view holds its transport, so the GC
 * never finalizes one a view can reach; close() or delete() ends its views first; and a constructor that throws ends
 * those of the sessions it had made: a call through one then throws NullPointerException instead of reaching freed
 * memory (#197). What a transport was made with is returned as the very object, and getSessionSettings and a
 * session's getAllowedRemoteAddresses as copies. None of these transports is started, so none listens or connects.
 */
public class TransportViewTests {
    interface Maker {
        Object make(Application application, MessageStoreFactory store, SessionSettings settings, LogFactory logs);
    }

    record Kind(String name, boolean acceptor, Maker maker) {
        Object make(Application application, MessageStoreFactory store, SessionSettings settings) {
            return maker.make(application, store, settings, null);
        }
    }

    static final List<Kind> KINDS = List.of(
        new Kind("SocketAcceptor", true,
                 (a, s, t, l) -> l == null ? new SocketAcceptor(a, s, t) : new SocketAcceptor(a, s, t, l)),
        new Kind("SocketInitiator", false,
                 (a, s, t, l) -> l == null ? new SocketInitiator(a, s, t) : new SocketInitiator(a, s, t, l)),
        new Kind("ThreadedSocketAcceptor", true,
                 (a, s, t, l) -> l == null ? new ThreadedSocketAcceptor(a, s, t) : new ThreadedSocketAcceptor(a, s, t, l)),
        new Kind("ThreadedSocketInitiator", false,
                 (a, s, t, l) -> l == null ? new ThreadedSocketInitiator(a, s, t) : new ThreadedSocketInitiator(a, s, t, l)),
        new Kind("SSLSocketAcceptor", true,
                 (a, s, t, l) -> l == null ? new SSLSocketAcceptor(a, s, t) : new SSLSocketAcceptor(a, s, t, l)),
        new Kind("SSLSocketInitiator", false,
                 (a, s, t, l) -> l == null ? new SSLSocketInitiator(a, s, t) : new SSLSocketInitiator(a, s, t, l)),
        new Kind("ThreadedSSLSocketAcceptor", true,
                 (a, s, t, l) -> l == null ? new ThreadedSSLSocketAcceptor(a, s, t)
                                           : new ThreadedSSLSocketAcceptor(a, s, t, l)),
        new Kind("ThreadedSSLSocketInitiator", false,
                 (a, s, t, l) -> l == null ? new ThreadedSSLSocketInitiator(a, s, t)
                                           : new ThreadedSSLSocketInitiator(a, s, t, l)));

    /** With take, keeps a view of each session it is told of, from onCreate. */
    static class App extends Application {
        final boolean take;
        final List<Session> taken = new ArrayList<>();

        App(boolean take) {
            this.take = take;
        }

        App() {
            this(false);
        }

        @Override public void onCreate(SessionID id) {
            if (take) {
                taken.add(Session.lookupSession(id));
            }
        }
        @Override public void onLogon(SessionID id) {}
        @Override public void onLogout(SessionID id) {}
        @Override public void toAdmin(Message message, SessionID id) {}
        @Override public void toApp(Message message, SessionID id) {}
        @Override public void fromAdmin(Message message, SessionID id) {}
        @Override public void fromApp(Message message, SessionID id) {}
    }

    /** Settings for kind with a session from each sender to TV. */
    static SessionSettings settings(Kind kind, String... senders) {
        boolean ssl = kind.name().contains("SSL");
        StringBuilder text = new StringBuilder("[DEFAULT]\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n");
        if (kind.acceptor()) {
            text.append("ConnectionType=acceptor\nSocketAcceptPort=1\n");
            if (ssl) {
                text.append("ServerCertificateFile=").append(Fixture.cert("127_0_0_1_server.crt"))
                    .append("\nServerCertificateKeyFile=").append(Fixture.cert("127_0_0_1_server.key")).append('\n');
            }
        } else {
            text.append("ConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=1\nHeartBtInt=30\n");
            if (ssl) {
                text.append("CertificationAuthoritiesFile=").append(Fixture.cert("certs/cacert.pem"))
                    .append("\nClientCertificateFile=").append(Fixture.cert("127_0_0_1_client.crt"))
                    .append("\nClientCertificateKeyFile=").append(Fixture.cert("127_0_0_1_client.key")).append('\n');
            }
        }
        for (String sender : senders) {
            text.append("[SESSION]\nBeginString=FIX.4.2\nSenderCompID=").append(sender).append("\nTargetCompID=TV\n");
        }
        return Fixture.settings(text.toString());
    }

    static SessionID id(String sender) {
        return new SessionID("FIX.4.2", sender, "TV");
    }

    /** A message's text from sender to TV, which lookupSession's string overload reads the session from. */
    static String header(String sender) {
        Message message = new Message();
        message.getHeader().setField(BeginString.FIELD, "FIX.4.2");
        message.getHeader().setField(SenderCompID.FIELD, sender);
        message.getHeader().setField(TargetCompID.FIELD, "TV");
        message.getHeader().setField(MsgType.FIELD, MsgType.Heartbeat);
        return message.toString();
    }

    static Session getSession(Object transport, SessionID id) {
        return transport instanceof Initiator initiator ? initiator.getSession(id) : ((Acceptor) transport).getSession(id);
    }

    static Log getLog(Object transport) {
        return transport instanceof Initiator initiator ? initiator.getLog() : ((Acceptor) transport).getLog();
    }

    static void close(Object transport) throws Exception {
        ((AutoCloseable) transport).close();
    }

    static Object owner(Object view) throws Exception {
        Field field = view.getClass().getDeclaredField("swigOwner");
        field.setAccessible(true);
        return field.get(view);
    }

    static void ended(Object view, String what) {
        try {
            Check.raises(NullPointerException.class, () -> {
                if (view instanceof Session session) {
                    session.getSessionID();
                } else if (view instanceof Log log) {
                    log.onEvent("ended");
                } else {
                    ((MessageStore) view).getNextSenderMsgSeqNum();
                }
            });
        } catch (AssertionError e) {
            throw new AssertionError(what + ": " + e.getMessage(), e);
        }
    }

    static int transportViewCount() throws Exception {
        Method count = Class.forName("com.dudleylane.quickfix.Views").getDeclaredMethod("transportViewCount");
        count.setAccessible(true);
        return (Integer) count.invoke(null);
    }

    /** The count once the views earlier tests dropped are collected: the same twice, a GC apart. */
    static int settledTransportViewCount() throws Exception {
        int last = -1;
        int count = transportViewCount();
        while (count != last) {
            last = count;
            System.gc();
            Thread.sleep(20);
            count = transportViewCount();
        }
        return count;
    }

    public void testViewsEndWithTheirTransport() throws Exception {
        for (Kind kind : KINDS) {
            String sender = "V" + kind.name();
            SessionID id = id(sender);
            Object made = kind.make(new App(), new MemoryStoreFactory(), settings(kind, sender));
            Map<String, Object> views = new LinkedHashMap<>();
            views.put("lookupSession", Session.lookupSession(id));
            views.put("lookupSession(string)", Session.lookupSession(header(sender), false));
            views.put("registerSession", Session.registerSession(id));
            Session session = getSession(made, id);
            views.put("getSession", session);
            for (Map.Entry<String, Object> view : views.entrySet()) {
                Session viewed = (Session) view.getValue();
                Check.equal(sender, viewed.getSessionID().getSenderCompID().getValue(), kind.name() + ": " + view.getKey());
            }
            MessageStore store = session.getStore();
            Log sessionLog = session.getLog();
            Log log = getLog(made);
            StringSet addresses = session.getAllowedRemoteAddresses();
            Check.equal(1L, store.getNextSenderMsgSeqNum(), kind.name() + ": getStore");
            sessionLog.onEvent("alive");
            log.onEvent("alive");
            views.put("Session.getStore", store);
            views.put("Session.getLog", sessionLog);
            views.put("getLog", log);
            close(made);
            for (Map.Entry<String, Object> view : views.entrySet()) {
                ended(view.getValue(), kind.name() + ": " + view.getKey());
            }
            Check.isTrue(addresses.isEmpty(), kind.name() + ": getAllowedRemoteAddresses is a copy");
        }
    }

    public void testViewsHoldTheirTransport() throws Exception {
        for (Kind kind : KINDS) {
            String sender = "VH" + kind.name();
            SessionID id = id(sender);
            Object made = kind.make(new App(), new MemoryStoreFactory(), settings(kind, sender));
            Session session = getSession(made, id);
            for (Session view : List.of(Session.lookupSession(id), Session.registerSession(id), session)) {
                Check.isTrue(owner(view) == made, kind.name() + ": a session's view holds its transport");
            }
            Check.isTrue(owner(getLog(made)) == made, kind.name() + ": getLog holds its transport");
            Check.isTrue(owner(session.getStore()) == session && owner(session.getLog()) == session,
                         kind.name() + ": a session's store and log hold its view");
            close(made);
        }
    }

    private static Object[] madeElsewhere() {
        Kind kind = KINDS.get(0);
        Object made = kind.make(new App(), new MemoryStoreFactory(), settings(kind, "VHOLD"));
        return new Object[] {getSession(made, id("VHOLD")), new WeakReference<>(made)};
    }

    public void testAViewHoldsItsTransport() throws Exception {
        Object[] made = madeElsewhere();
        Session view = (Session) made[0];
        @SuppressWarnings("unchecked") WeakReference<Object> transport = (WeakReference<Object>) made[1];
        made = null;
        for (int i = 0; i < 5 && transport.get() != null; ++i) {
            System.gc();
            Thread.sleep(20);
        }
        Check.isTrue(transport.get() != null, "the view holds its transport");
        Check.equal("VHOLD", view.getSessionID().getSenderCompID().getValue(), "the view");
        close(transport.get());
        ended(view, "after close()");
    }

    public void testAViewOnCreateTookGetsItsTransport() throws Exception {
        Kind kind = KINDS.get(1);
        App app = new App(true);
        Object made = kind.make(app, new MemoryStoreFactory(), settings(kind, "VOWN"));
        Check.equal(1, app.taken.size(), "views taken");
        Check.isTrue(owner(app.taken.get(0)) == made, "its transport, once made");
        close(made);
        ended(app.taken.get(0), "after close()");
    }

    public void testAFailedConstructorEndsTheViewsOnCreateTook() throws Exception {
        for (Kind kind : KINDS) {
            // VB exists already, so a transport with VA and VB makes VA, whose onCreate takes a view of it, and then
            // throws at VB, freeing VA.
            String first = "VB" + kind.name();
            String second = "VA" + kind.name();
            Object holder = kind.make(new App(), new MemoryStoreFactory(), settings(kind, first));
            Session live = Session.lookupSession(id(first));
            App app = new App(true);
            Check.raises(ConfigError.class, () -> kind.make(app, new MemoryStoreFactory(), settings(kind, second, first)));
            Check.equal(1, app.taken.size(), kind.name() + ": views taken");
            ended(app.taken.get(0), kind.name());
            Check.isTrue(Session.lookupSession(id(second)) == null, kind.name() + ": freed");
            // A view of a session that still exists is left alone.
            Check.equal(first, live.getSessionID().getSenderCompID().getValue(), kind.name() + ": a live session's view");
            close(holder);
            ended(live, kind.name() + ": after close()");
        }
    }

    public void testAViewDroppedIsNoLongerKept() throws Exception {
        Kind kind = KINDS.get(0);
        Object made = kind.make(new App(), new MemoryStoreFactory(), settings(kind, "VDROP"));
        int before = settledTransportViewCount();
        Session view = getSession(made, id("VDROP"));
        Check.equal(before + 1, transportViewCount(), "kept");
        view = null;
        Check.isTrue(Check.eventually(10, () -> {
            System.gc();
            try {
                return transportViewCount() == before;
            } catch (Exception e) {
                throw new AssertionError(e);
            }
        }), "dropped once the GC collects it");
        close(made);
    }

    public void testSessionSettingsAreACopy() throws Exception {
        Kind kind = KINDS.get(1);
        SocketInitiator made = (SocketInitiator) kind.make(new App(), new MemoryStoreFactory(), settings(kind, "VSET"));
        Dictionary dictionary = made.getSessionSettings(id("VSET"));
        Check.isTrue(made.getSessionSettings(id("VNONE")) == null, "no such session");
        close(made);
        Check.equal("VSET", dictionary.getString("SenderCompID"), "after close()");
    }

    public void testWhatATransportWasMadeWithIsReturnedItself() throws Exception {
        for (Kind kind : KINDS) {
            App app = new App();
            MemoryStoreFactory store = new MemoryStoreFactory();
            Object made = kind.make(app, store, settings(kind, "VW" + kind.name()));
            Application application = made instanceof Initiator initiator ? initiator.getApplication()
                                                                          : ((Acceptor) made).getApplication();
            MessageStoreFactory factory = made instanceof Initiator initiator ? initiator.getMessageStoreFactory()
                                                                              : ((Acceptor) made).getMessageStoreFactory();
            Check.isTrue(application == app, kind.name() + ": getApplication");
            Check.isTrue(factory == store, kind.name() + ": getMessageStoreFactory");
            close(made);
        }
    }

    public void testWhatASessionHoldsOfItsTransportIsNotWrapped() {
        // Each returned an opaque handle to the session's state, freed with the transport.
        for (String name : List.of("getDataDictionaryProvider", "getLogonTime")) {
            Check.raises(NoSuchMethodException.class, () -> Session.class.getMethod(name));
        }
    }
}
