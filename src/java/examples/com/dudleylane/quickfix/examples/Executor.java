package com.dudleylane.quickfix.examples;

import com.dudleylane.quickfix.*;
import com.dudleylane.quickfix.field.*;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicLong;

/**
 * examples/executor in Java: an acceptor that fills every FIX 4.4 limit NewOrderSingle it receives at its
 * price, answering with an ExecutionReport. Run it with a session settings file, as the C++ one runs:
 *
 *   java -cp quickfix.jar:examples com.dudleylane.quickfix.examples.Executor executor.cfg
 *
 * It stops on SIGINT or SIGTERM, through the shutdown hook the binding registers for running transports.
 */
public final class Executor extends Application {
    private final AtomicLong ids = new AtomicLong();

    @Override public void onCreate(SessionID id) {}
    @Override public void onLogon(SessionID id) { System.out.println("Logon - " + id); }
    @Override public void onLogout(SessionID id) { System.out.println("Logout - " + id); }
    @Override public void toAdmin(Message message, SessionID id) {}
    @Override public void fromAdmin(Message message, SessionID id) {}
    @Override public void toApp(Message message, SessionID id) {}

    @Override
    public void fromApp(Message message, SessionID id) {
        // message is the engine's, and valid only until this returns: read it here, or copy it to keep it.
        if (!message.getHeader().getField(BeginString.FIELD).equals("FIX.4.4")
            || !MsgType.get(message.getHeader()).equals(MsgType.NewOrderSingle)) {
            throw new UnsupportedMessageType();
        }
        var order = com.dudleylane.quickfix.fix44.NewOrderSingle.view(message);
        if (OrdType.get(order) != OrdType.LIMIT) {
            throw new IncorrectTagValue(OrdType.FIELD);
        }
        double price = Price.get(order);
        double quantity = OrderQty.get(order);

        var report = new com.dudleylane.quickfix.fix44.ExecutionReport();
        OrderID.set(report, "ORDER" + ids.incrementAndGet());
        ExecID.set(report, "EXEC" + ids.incrementAndGet());
        ExecType.set(report, ExecType.TRADE);
        OrdStatus.set(report, OrdStatus.FILLED);
        Side.set(report, Side.get(order));
        LeavesQty.set(report, 0);
        CumQty.set(report, quantity);
        AvgPx.set(report, price);
        LastQty.set(report, quantity);
        LastPx.set(report, price);
        ClOrdID.set(report, ClOrdID.get(order));
        Symbol.set(report, Symbol.get(order));
        if (Account.isSet(order)) {
            Account.set(report, Account.get(order));
        }
        Session.sendToTarget(report, id);
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("usage: Executor FILE");
            System.exit(2);
        }
        SessionSettings settings = new SessionSettings();
        settings.setFromString(new String(java.nio.file.Files.readAllBytes(java.nio.file.Path.of(args[0]))));
        CountDownLatch stopped = new CountDownLatch(1);
        try (SocketAcceptor acceptor = new SocketAcceptor(new Executor(), new MemoryStoreFactory(), settings,
                                                          new ScreenLogFactory(settings))) {
            acceptor.start();
            System.out.println("Executor listening");
            Runtime.getRuntime().addShutdownHook(new Thread(stopped::countDown));
            stopped.await();
        }
    }
}
