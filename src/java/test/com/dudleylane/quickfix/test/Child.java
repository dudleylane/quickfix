package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;

/** The child JVMs of CallbackErrorTests. */
public final class Child {
    private Child() {}

    static class ThrowingLog extends Log {
        @Override public void clear() {}
        @Override public void backup() {}
        @Override public void onIncoming(String message) {}
        @Override public void onOutgoing(String message) {}
        @Override public void onEvent(String text) { throw new IllegalStateException("thrown from onEvent"); }
    }

    public static void main(String[] args) throws Exception {
        String mode = args[0];
        String transport = mode.endsWith("-threaded") ? "ThreadedSocket" : "Socket";
        Fixture.Recorder server = new Fixture.Recorder();
        Fixture.Recorder client = new Fixture.Recorder() {
            @Override
            public void onLogon(SessionID id) {
                if (mode.equals("onLogon")) {
                    throw new IllegalStateException("thrown from onLogon");
                }
                super.onLogon(id);
            }
        };
        server.onFromApp = (message, id) -> {
            if (mode.startsWith("fromApp")) {
                throw new IllegalStateException("thrown from fromApp");
            }
        };
        LogFactory logs = mode.equals("onEvent") ? new LogFactory() {
            @Override public Log create() { return new ThrowingLog(); }
            @Override public Log create(SessionID id) { return new ThrowingLog(); }
            @Override public void destroy(Log log) {}
        } : null;
        int port = Fixture.freePort();
        MemoryStoreFactory factory = new MemoryStoreFactory();
        AutoCloseable acceptor = Fixture.acceptor(transport, server, factory,
                                                  Fixture.acceptorSettings(port, "SERVER", "CLIENT", false), logs);
        AutoCloseable initiator = Fixture.initiator(transport, client, factory,
                                                    Fixture.initiatorSettings(port, "SERVER", "CLIENT", false), null);
        Fixture.start(acceptor);
        Fixture.start(initiator);
        if (mode.startsWith("exit")) {
            Fixture.loggedOn(TransportTests.CLIENT);
            System.out.println("Child: returning from main");
            return; // the shutdown hook stops both
        }
        Fixture.loggedOn(TransportTests.CLIENT);
        Session.sendToTarget(Fixture.order("ID"), TransportTests.CLIENT);
        Thread.sleep(10000);
        System.out.println("Child: survived");
        System.exit(2);
    }
}
