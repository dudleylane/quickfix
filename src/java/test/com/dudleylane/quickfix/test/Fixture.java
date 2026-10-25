package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;
import java.io.IOException;
import java.net.ServerSocket;
import java.nio.file.Path;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.BiConsumer;

/** What the suites share: settings, the transports by name, and an Application that records. */
final class Fixture {
    private Fixture() {}

    /** The repository root, from quickfix.test.root (test-java.sh sets it). */
    static Path root() {
        return Path.of(System.getProperty("quickfix.test.root"));
    }

    static String spec(String version) {
        return root().resolve("spec").resolve(version + ".xml").toString();
    }

    static String cert(String name) {
        return root().resolve("bin/cfg/certs").resolve(name).toString();
    }

    static int freePort() throws IOException {
        try (ServerSocket socket = new ServerSocket(0)) {
            return socket.getLocalPort();
        }
    }

    static SessionSettings settings(String text) {
        SessionSettings settings = new SessionSettings();
        settings.setFromString(text);
        return settings;
    }

    static final String COMMON = "StartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\nBeginString=FIX.4.2\n"
                                 + "PersistMessages=N\n";

    static SessionSettings acceptorSettings(int port, String server, String client, boolean ssl) {
        return settings("[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" + port + "\n" + COMMON
                        + (ssl ? "ServerCertificateFile=" + cert("127_0_0_1_server.crt") + "\nServerCertificateKeyFile="
                                     + cert("127_0_0_1_server.key") + "\n"
                               : "")
                        + "[SESSION]\nSenderCompID=" + server + "\nTargetCompID=" + client + "\n");
    }

    static SessionSettings initiatorSettings(int port, String server, String client, boolean ssl) {
        return settings("[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=" + port
                        + "\nHeartBtInt=30\nReconnectInterval=1\n" + COMMON
                        + (ssl ? "CertificationAuthoritiesFile=" + cert("certs/cacert.pem") + "\nClientCertificateFile="
                                     + cert("127_0_0_1_client.crt") + "\nClientCertificateKeyFile="
                                     + cert("127_0_0_1_client.key") + "\nCertificateVerifyLevel=0\n"
                               : "")
                        + "[SESSION]\nSenderCompID=" + client + "\nTargetCompID=" + server + "\n");
    }

    static final List<String> TRANSPORTS = List.of("Socket", "ThreadedSocket", "SSLSocket", "ThreadedSSLSocket");

    static AutoCloseable acceptor(String transport, Application application, MessageStoreFactory factory,
                                  SessionSettings settings, LogFactory logFactory) {
        return switch (transport) {
        case "Socket" -> logFactory == null ? new SocketAcceptor(application, factory, settings)
                                             : new SocketAcceptor(application, factory, settings, logFactory);
        case "ThreadedSocket" -> logFactory == null ? new ThreadedSocketAcceptor(application, factory, settings)
                                                     : new ThreadedSocketAcceptor(application, factory, settings, logFactory);
        case "SSLSocket" -> logFactory == null ? new SSLSocketAcceptor(application, factory, settings)
                                                : new SSLSocketAcceptor(application, factory, settings, logFactory);
        case "ThreadedSSLSocket" -> logFactory == null ? new ThreadedSSLSocketAcceptor(application, factory, settings)
                                                        : new ThreadedSSLSocketAcceptor(application, factory, settings, logFactory);
        default -> throw new IllegalArgumentException(transport);
        };
    }

    static AutoCloseable initiator(String transport, Application application, MessageStoreFactory factory,
                                   SessionSettings settings, LogFactory logFactory) {
        return switch (transport) {
        case "Socket" -> logFactory == null ? new SocketInitiator(application, factory, settings)
                                             : new SocketInitiator(application, factory, settings, logFactory);
        case "ThreadedSocket" -> logFactory == null ? new ThreadedSocketInitiator(application, factory, settings)
                                                     : new ThreadedSocketInitiator(application, factory, settings, logFactory);
        case "SSLSocket" -> logFactory == null ? new SSLSocketInitiator(application, factory, settings)
                                                : new SSLSocketInitiator(application, factory, settings, logFactory);
        case "ThreadedSSLSocket" -> logFactory == null ? new ThreadedSSLSocketInitiator(application, factory, settings)
                                                        : new ThreadedSSLSocketInitiator(application, factory, settings, logFactory);
        default -> throw new IllegalArgumentException(transport);
        };
    }

    static void start(AutoCloseable transport) {
        if (transport instanceof Initiator initiator) {
            initiator.start();
        } else {
            ((Acceptor) transport).start();
        }
    }

    /** Records what reaches it; hooks let a test throw from a callback. */
    static class Recorder extends Application {
        final AtomicInteger logons = new AtomicInteger();
        final AtomicInteger logouts = new AtomicInteger();
        final List<String> fromApp = new CopyOnWriteArrayList<>();
        final List<String> fromAdmin = new CopyOnWriteArrayList<>();
        volatile BiConsumer<Message, SessionID> onFromApp = (m, id) -> {};
        volatile BiConsumer<Message, SessionID> onFromAdmin = (m, id) -> {};
        volatile BiConsumer<Message, SessionID> onToApp = (m, id) -> {};

        @Override public void onCreate(SessionID id) {}
        @Override public void onLogon(SessionID id) { logons.incrementAndGet(); }
        @Override public void onLogout(SessionID id) { logouts.incrementAndGet(); }
        @Override public void toAdmin(Message message, SessionID id) {}
        @Override public void toApp(Message message, SessionID id) { onToApp.accept(message, id); }

        @Override
        public void fromAdmin(Message message, SessionID id) {
            fromAdmin.add(message.getHeader().getField(35));
            onFromAdmin.accept(message, id);
        }

        @Override
        public void fromApp(Message message, SessionID id) {
            fromApp.add(message.getHeader().getField(35));
            onFromApp.accept(message, id);
        }
    }

    static Message order(String clOrdID) {
        Message message = new Message();
        message.getHeader().setField(35, "D");
        message.setField(11, clOrdID);
        message.setField(21, "1");
        message.setField(55, "LNUX");
        message.setField(54, "1");
        message.setField(60, "20261025-10:00:00.000");
        message.setField(40, "1");
        return message;
    }

    static boolean loggedOn(SessionID id) throws InterruptedException {
        return Check.eventually(20, () -> {
            Session session = Session.lookupSession(id);
            return session != null && session.isLoggedOn();
        });
    }
}
