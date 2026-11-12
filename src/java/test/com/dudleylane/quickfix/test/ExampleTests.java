package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;
import com.dudleylane.quickfix.examples.banzai.BanzaiApplication;
import com.dudleylane.quickfix.examples.banzai.Execution;
import com.dudleylane.quickfix.examples.banzai.ExecutionTableModel;
import com.dudleylane.quickfix.examples.banzai.Order;
import com.dudleylane.quickfix.examples.banzai.OrderSide;
import com.dudleylane.quickfix.examples.banzai.OrderTableModel;
import com.dudleylane.quickfix.examples.banzai.OrderType;
import com.dudleylane.quickfix.examples.banzai.ui.BanzaiPanel;
import com.dudleylane.quickfix.field.*;
import java.awt.Component;
import java.awt.Container;
import java.awt.Rectangle;
import java.awt.event.MouseEvent;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.concurrent.Callable;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;
import java.util.stream.Stream;
import javax.swing.JButton;
import javax.swing.JComboBox;
import javax.swing.JLabel;
import javax.swing.JTable;
import javax.swing.JTextField;
import javax.swing.SwingUtilities;

/**
 * The examples under examples/: the executor and ordermatch run as their documentation runs them, in JVMs of their
 * own, and Banzai's window, which test-java.sh builds headless and never shows, clicked through against ordermatch.
 */
public class ExampleTests {
    static final String EXECUTOR = "com.dudleylane.quickfix.examples.Executor";
    static final String ORDERMATCH = "com.dudleylane.quickfix.examples.ordermatch.OrderMatch";

    /**
     * An example's main in a JVM of its own, run with -Xcheck:jni as the suites are, on a settings file whose ${store}
     * is a directory of its own. close() kills it if finish() did not see it end, printing what it printed.
     */
    static final class Example implements AutoCloseable {
        private final Path directory;
        private final Path log;
        private final Process process;

        Example(String mainClass, String settings) throws IOException {
            directory = Files.createTempDirectory("example");
            Path config = directory.resolve("example.cfg");
            Files.writeString(config, settings.replace("${store}", directory.resolve("store").toString()));
            log = directory.resolve("example.log");
            List<String> command = List.of(ProcessHandle.current().info().command().orElse("java"), "-Xcheck:jni",
                                           "-cp", System.getProperty("java.class.path"),
                                           "-Dcom.dudleylane.quickfix.library="
                                               + System.getProperty("com.dudleylane.quickfix.library"),
                                           mainClass, config.toString());
            process = new ProcessBuilder(command).redirectErrorStream(true).redirectOutput(log.toFile()).start();
        }

        /** Its standard input: ordermatch's console. */
        OutputStream input() {
            return process.getOutputStream();
        }

        /**
         * Waits for it to end -- after a SIGTERM, as an operator stops it, when terminate: the binding's shutdown hook
         * stops its transport -- and checks it ended cleanly within 30 s, having printed banner and no sanitizer or
         * JNI report. Returns what it printed.
         */
        String finish(boolean terminate, String banner) throws Exception {
            if (terminate) {
                process.destroy();
            }
            boolean exited = process.waitFor(30, TimeUnit.SECONDS);
            String output = Files.readString(log);
            Check.isTrue(exited, "it ended" + (terminate ? " on SIGTERM" : "") + " within 30 s:\n" + output);
            Check.isTrue(output.contains(banner), "it started:\n" + output);
            Check.isTrue(!output.contains("Sanitizer"), "a sanitizer report:\n" + output);
            Check.isTrue(!output.contains("WARNING in native method"), "a JNI warning:\n" + output);
            int status = process.exitValue();
            Check.isTrue(status == 0 || terminate && status == 143,
                         "a clean exit, not a crash: " + status + "\n" + output);
            return output;
        }

        @Override
        public void close() throws IOException {
            if (process.isAlive()) {
                process.destroyForcibly();
                System.out.println("the example's output:\n" + Files.readString(log));
            }
            try (Stream<Path> paths = Files.walk(directory)) {
                paths.sorted(Comparator.reverseOrder()).forEach(path -> path.toFile().delete());
            }
        }
    }

    static SessionSettings initiatorSettings(int port, String sessions) {
        return Fixture.settings("[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort="
                                + port + "\nHeartBtInt=30\nReconnectInterval=1\nStartTime=00:00:00\nEndTime=00:00:00\n"
                                + "UseDataDictionary=N\n" + sessions);
    }

    /** ordermatch's settings: one FIX 4.2 session, to client, checked against FIX42.xml as bin/cfg's is. */
    static String orderMatchSettings(int port, String client) {
        return "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" + port + "\nStartTime=00:00:00\nEndTime=00:00:00"
               + "\nFileStorePath=${store}\nUseDataDictionary=Y\nDataDictionary=" + Fixture.spec("FIX42")
               + "\n[SESSION]\nBeginString=FIX.4.2\nSenderCompID=ORDERMATCH\nTargetCompID=" + client + "\n";
    }

    public void testTheExecutorFillsALimitOrder() throws Exception {
        int port = Fixture.freePort();
        try (Example executor = new Example(EXECUTOR, "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" + port
                                                      + "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=Y"
                                                      + "\nDataDictionary=" + Fixture.spec("FIX44") + "\n[SESSION]"
                                                      + "\nBeginString=FIX.4.4\nSenderCompID=EXECUTOR\nTargetCompID=CLIENT\n")) {
            Fixture.Recorder client = new Fixture.Recorder();
            List<Message> reports = new CopyOnWriteArrayList<>();
            client.onFromApp = (message, id) -> reports.add(new Message(message)); // a copy: kept past the callback
            SessionID id = new SessionID("FIX.4.4", "CLIENT", "EXECUTOR");
            try (SocketInitiator initiator = new SocketInitiator(
                     client, new MemoryStoreFactory(),
                     initiatorSettings(port, "[SESSION]\nBeginString=FIX.4.4\nSenderCompID=CLIENT\nTargetCompID=EXECUTOR\n"))) {
                initiator.start();
                Check.isTrue(Fixture.loggedOn(id), "logged on to the executor");
                var order = new com.dudleylane.quickfix.fix44.NewOrderSingle();
                ClOrdID.set(order, "ORDER1");
                Symbol.set(order, "LNUX");
                Side.set(order, Side.BUY);
                TransactTime.set(order, "20261025-10:00:00.000");
                OrdType.set(order, OrdType.LIMIT);
                Price.set(order, 12.5);
                OrderQty.set(order, 300);
                Check.isTrue(Session.sendToTarget(order, id), "sent");
                Check.isTrue(Check.eventually(10, () -> !reports.isEmpty()), "an ExecutionReport came back");
                Message report = reports.get(0);
                Check.equal(MsgType.ExecutionReport, MsgType.get(report.getHeader()), "an ExecutionReport");
                Check.equal(OrdStatus.FILLED, OrdStatus.get(report), "filled");
                Check.equal(300.0, CumQty.get(report), "in full");
                Check.equal(12.5, AvgPx.get(report), "at its price");
                Check.equal("ORDER1", ClOrdID.get(report), "the order's ClOrdID");
            }
            executor.finish(true, "Executor listening");
        }
    }

    /** A FIX 4.2 NewOrderSingle for LNUX: a limit order at price, or a market order when price is null. */
    static Message order(String clOrdID, char side, double quantity, Double price) {
        var order = new com.dudleylane.quickfix.fix42.NewOrderSingle();
        ClOrdID.set(order, clOrdID);
        HandlInst.set(order, HandlInst.AUTOMATED_EXECUTION_NO_INTERVENTION);
        Symbol.set(order, "LNUX");
        Side.set(order, side);
        TransactTime.set(order, Instant.now());
        OrdType.set(order, price == null ? OrdType.MARKET : OrdType.LIMIT);
        if (price != null) {
            Price.set(order, price);
        }
        OrderQty.set(order, quantity);
        return order;
    }

    static void send(SessionID id, Message message) {
        Check.isTrue(Session.sendToTarget(message, id), "sent a " + MsgType.get(message.getHeader()));
    }

    static Message report(List<Message> reports, int index) throws InterruptedException {
        Check.isTrue(Check.eventually(10, () -> reports.size() > index),
                     "ExecutionReport " + (index + 1) + " came back; " + reports.size() + " did");
        return reports.get(index);
    }

    static void checkReport(Message report, String clOrdID, char ordStatus, double leavesQty, double cumQty) {
        Check.equal(MsgType.ExecutionReport, MsgType.get(report.getHeader()), "an ExecutionReport");
        Check.equal(clOrdID, ClOrdID.get(report), "its ClOrdID");
        Check.equal(ordStatus, OrdStatus.get(report), clOrdID + "'s OrdStatus");
        Check.equal(leavesQty, LeavesQty.get(report), clOrdID + "'s LeavesQty");
        Check.equal(cumQty, CumQty.get(report), clOrdID + "'s CumQty");
    }

    public void testOrderMatchMatchesCancelsAndRejects() throws Exception {
        int port = Fixture.freePort();
        try (Example orderMatch = new Example(ORDERMATCH, orderMatchSettings(port, "CLIENT"))) {
            Fixture.Recorder client = new Fixture.Recorder();
            List<Message> reports = new CopyOnWriteArrayList<>();
            client.onFromApp = (message, id) -> reports.add(new Message(message));
            List<Integer> rejectedTags = new CopyOnWriteArrayList<>();
            client.onFromAdmin = (message, id) -> {
                if (MsgType.get(message.getHeader()).equals(MsgType.Reject)) {
                    rejectedTags.add(RefTagID.get(message));
                }
            };
            SessionID id = new SessionID("FIX.4.2", "CLIENT", "ORDERMATCH");
            try (SocketInitiator initiator = new SocketInitiator(
                     client, new MemoryStoreFactory(),
                     initiatorSettings(port, "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=CLIENT\nTargetCompID=ORDERMATCH\n"))) {
                initiator.start();
                Check.isTrue(Fixture.loggedOn(id), "logged on to ordermatch");

                send(id, order("B1", Side.BUY, 100, 12.5));
                checkReport(report(reports, 0), "B1", OrdStatus.NEW, 100, 0);
                send(id, order("S1", Side.SELL, 60, 12.25));
                checkReport(report(reports, 1), "S1", OrdStatus.NEW, 60, 0);
                // They cross: 60 trade at the ask's price, reported to the bid first.
                Message bid = report(reports, 2);
                checkReport(bid, "B1", OrdStatus.PARTIALLY_FILLED, 40, 60);
                Check.equal(60.0, LastShares.get(bid), "the fill's quantity");
                Check.equal(12.25, LastPx.get(bid), "at the ask's price");
                Check.equal(12.25, AvgPx.get(bid), "B1's average price");
                checkReport(report(reports, 3), "S1", OrdStatus.FILLED, 0, 60);

                var cancel = new com.dudleylane.quickfix.fix42.OrderCancelRequest();
                OrigClOrdID.set(cancel, "B1");
                ClOrdID.set(cancel, "C1");
                Symbol.set(cancel, "LNUX");
                Side.set(cancel, Side.BUY);
                TransactTime.set(cancel, Instant.now());
                send(id, cancel);
                checkReport(report(reports, 4), "B1", OrdStatus.CANCELED, 0, 60);

                send(id, order("M1", Side.BUY, 10, null));
                Message rejected = report(reports, 5);
                checkReport(rejected, "M1", OrdStatus.REJECTED, 0, 0);
                Check.equal("Unsupported Order Type, use limit", Text.get(rejected), "why");

                send(id, order("B2", Side.BUY, 10, 11.0));
                checkReport(report(reports, 6), "B2", OrdStatus.NEW, 10, 0);
                Check.equal(7L, reports.stream().map(report -> ExecID.get(report)).distinct().count(),
                            "an ExecID for each report");

                // A subscription to market data is refused for its SubscriptionRequestType.
                var request = new com.dudleylane.quickfix.fix42.MarketDataRequest();
                MDReqID.set(request, "MD1");
                SubscriptionRequestType.set(request, SubscriptionRequestType.SNAPSHOT_AND_UPDATES);
                MarketDepth.set(request, 0);
                var entryType = com.dudleylane.quickfix.fix42.MarketDataRequest.newNoMDEntryTypes();
                MDEntryType.set(entryType, MDEntryType.BID);
                request.addGroup(entryType);
                var symbol = com.dudleylane.quickfix.fix42.MarketDataRequest.newNoRelatedSym();
                Symbol.set(symbol, "LNUX");
                request.addGroup(symbol);
                send(id, request);
                Check.isTrue(Check.eventually(10, () -> !rejectedTags.isEmpty()), "the subscription was rejected");
                Check.equal(SubscriptionRequestType.FIELD, rejectedTags.get(0), "for its SubscriptionRequestType");
            }
            // The console: B2 rests in the book of LNUX, the one symbol traded.
            try (OutputStream console = orderMatch.input()) {
                console.write("LNUX\n#symbols\n#quit\n".getBytes(StandardCharsets.UTF_8));
            }
            String output = orderMatch.finish(false, "OrderMatch listening");
            Check.isTrue(output.contains("BIDS:") && output.contains(",B2 OWNER:"), "LNUX's book holds B2:\n" + output);
            Check.isTrue(output.contains("SYMBOLS:") && output.contains("\nLNUX\n"), "the symbols:\n" + output);
        }
    }

    /** What call returns, called on the Swing thread, which alone touches Banzai's panel, models and orders. */
    static <T> T fromEdt(Callable<T> call) throws Exception {
        FutureTask<T> task = new FutureTask<>(call);
        SwingUtilities.invokeAndWait(task);
        try {
            return task.get();
        } catch (ExecutionException e) {
            if (e.getCause() instanceof Exception cause) {
                throw cause;
            }
            throw (Error) e.getCause();
        }
    }

    static void onEdt(Check.Action action) throws Exception {
        fromEdt(() -> {
            action.run();
            return null;
        });
    }

    static boolean eventuallyOnEdt(double seconds, Callable<Boolean> condition) throws InterruptedException {
        return Check.eventually(seconds, () -> {
            try {
                return fromEdt(condition);
            } catch (Exception e) {
                throw new IllegalStateException(e);
            }
        });
    }

    /** The component of type named name under root. */
    static <T extends Component> T component(Container root, Class<T> type, String name) {
        T found = find(root, type, name);
        Check.isTrue(found != null, "no " + type.getSimpleName() + " named " + name);
        return found;
    }

    private static <T extends Component> T find(Container root, Class<T> type, String name) {
        for (Component child : root.getComponents()) {
            if (type.isInstance(child) && name.equals(child.getName())) {
                return type.cast(child);
            }
            if (child instanceof Container container) {
                T found = find(container, type, name);
                if (found != null) {
                    return found;
                }
            }
        }
        return null;
    }

    /** Enters an order in Banzai's fields and submits it, as a user would. */
    static void submit(BanzaiPanel panel, String symbol, String quantity, OrderSide side, OrderType type,
                       String limit) throws Exception {
        onEdt(() -> {
            component(panel, JTextField.class, "SymbolTextField").setText(symbol);
            component(panel, JTextField.class, "QuantityTextField").setText(quantity);
            component(panel, JComboBox.class, "SideComboBox").setSelectedItem(side);
            component(panel, JComboBox.class, "TypeComboBox").setSelectedItem(type);
            component(panel, JTextField.class, "LimitPriceTextField").setText(limit);
            JButton submit = component(panel, JButton.class, "SubmitButton");
            Check.isTrue(submit.isEnabled(), "Submit is enabled for " + side + " " + quantity + " " + symbol);
            submit.doClick();
        });
    }

    public void testBanzaiTradesWithOrderMatch() throws Exception {
        int port = Fixture.freePort();
        try (Example orderMatch = new Example(ORDERMATCH, orderMatchSettings(port, "BANZAI"))) {
            OrderTableModel orders = new OrderTableModel();
            ExecutionTableModel executions = new ExecutionTableModel();
            BanzaiApplication application = new BanzaiApplication(orders, executions);
            BanzaiPanel panel = fromEdt(() -> new BanzaiPanel(orders, executions, application));
            JComboBox<?> sessions = fromEdt(() -> component(panel, JComboBox.class, "SessionComboBox"));
            JTable orderTable = fromEdt(() -> component(panel, JTable.class, "OrderTable"));
            JButton cancel = fromEdt(() -> component(panel, JButton.class, "CancelButton"));
            try (SocketInitiator initiator = new SocketInitiator(
                     application, new MemoryStoreFactory(),
                     initiatorSettings(port, "[SESSION]\nBeginString=FIX.4.2\nSenderCompID=BANZAI\nTargetCompID=ORDERMATCH\n"))) {
                initiator.start();
                Check.isTrue(eventuallyOnEdt(20, () -> sessions.getItemCount() == 1), "the session logged on is offered");
                Check.equal("FIX.4.2:BANZAI->ORDERMATCH", fromEdt(() -> String.valueOf(sessions.getSelectedItem())),
                            "and chosen");

                submit(panel, "LNUX", "100", OrderSide.BUY, OrderType.LIMIT, "12.5");
                Check.isTrue(eventuallyOnEdt(10, () -> orders.getRowCount() == 1 && orders.getOrder(0).getOpen() == 100),
                             "the buy order is acknowledged");
                submit(panel, "LNUX", "60", OrderSide.SELL, OrderType.LIMIT, "12.25");
                Check.isTrue(eventuallyOnEdt(10, () -> executions.getRowCount() == 2), "the match is two executions");
                onEdt(() -> {
                    Order buy = orders.getOrder(0);
                    Check.equal(40, buy.getOpen(), "the buy order's open quantity");
                    Check.equal(60, buy.getExecuted(), "its executed quantity");
                    Check.equal(12.25, buy.getAvgPx(), "its average price, the ask's");
                    Order sell = orders.getOrder(1);
                    Check.equal(0, sell.getOpen(), "the sell order's open quantity");
                    Check.equal(60, sell.getExecuted(), "filled");
                    OrderSide[] sides = {OrderSide.BUY, OrderSide.SELL};
                    for (int row = 0; row < 2; ++row) {
                        Execution execution = executions.getExecution(row);
                        Check.equal(new Execution(execution.exchangeID(), "LNUX", 60, sides[row], 12.25), execution,
                                    "the fill reported to the bid, then to the ask");
                    }
                });

                // Selecting the buy order offers what is open of it to cancel or replace.
                onEdt(() -> {
                    orderTable.setRowSelectionInterval(0, 0);
                    Check.equal("40", component(panel, JTextField.class, "ReplaceQuantityTextField").getText(),
                                "the quantity open");
                    Check.isTrue(cancel.isEnabled(), "Cancel is enabled");
                    cancel.doClick();
                });
                Check.isTrue(eventuallyOnEdt(10, () -> orders.getOrder(0).getCanceled() && !cancel.isEnabled()),
                             "the buy order is cancelled, and Cancel disabled with nothing open");

                // A double click on an order's row cancels it.
                submit(panel, "LNUX", "10", OrderSide.BUY, OrderType.LIMIT, "11");
                Check.isTrue(eventuallyOnEdt(10, () -> orders.getRowCount() == 3 && orders.getOrder(2).getOpen() == 10),
                             "the third order rests");
                onEdt(() -> {
                    Rectangle cell = orderTable.getCellRect(2, 0, false);
                    orderTable.dispatchEvent(new MouseEvent(orderTable, MouseEvent.MOUSE_CLICKED, System.currentTimeMillis(),
                                                            0, cell.x + 1, cell.y + 1, 2, false));
                });
                Check.isTrue(eventuallyOnEdt(10, () -> orders.getOrder(2).getCanceled()), "the double click cancelled it");

                // ordermatch takes limit orders only: a market order comes back rejected, and selecting it says why.
                submit(panel, "LNUX", "5", OrderSide.BUY, OrderType.MARKET, "");
                Check.isTrue(eventuallyOnEdt(10, () -> orders.getRowCount() == 4 && orders.getOrder(3).getRejected()),
                             "the market order is rejected");
                onEdt(() -> {
                    orderTable.setRowSelectionInterval(3, 3);
                    Check.equal("Unsupported Order Type, use limit",
                                component(panel, JLabel.class, "MessageLabel").getText(), "why, under the entry fields");
                    Check.isTrue(!cancel.isEnabled(), "nothing open to cancel");
                });
            }
            Check.isTrue(eventuallyOnEdt(10, () -> sessions.getItemCount() == 0), "the session logged out is withdrawn");
            orderMatch.finish(true, "OrderMatch listening");
        }
    }

    record Received(String version, String msgType, String clOrdID, String origClOrdID, Double price, Double stopPx,
                    double quantity) {}

    /**
     * Banzai's order, cancel and replace in each version it sends, to an acceptor that checks each against its
     * version's dictionary: one that did not conform would be rejected there, and not reach its fromApp. The cancel
     * and the replace go from the panel's buttons.
     */
    public void testBanzaiSendsEachVersionsMessages() throws Exception {
        List<String> versions = List.of("FIX.4.0", "FIX.4.1", "FIX.4.2", "FIX.4.3", "FIX.4.4", "FIXT.1.1");
        StringBuilder acceptorSessions = new StringBuilder();
        StringBuilder initiatorSessions = new StringBuilder();
        for (String version : versions) {
            boolean fixt = version.equals("FIXT.1.1");
            acceptorSessions.append("[SESSION]\nBeginString=" + version + "\nSenderCompID=EXECUTOR\nTargetCompID=BANZAI\n")
                .append(fixt ? "DefaultApplVerID=FIX.5.0\nTransportDataDictionary=" + Fixture.spec("FIXT11")
                                   + "\nAppDataDictionary=" + Fixture.spec("FIX50") + "\n"
                             : "DataDictionary=" + Fixture.spec(version.replace(".", "")) + "\n");
            initiatorSessions.append("[SESSION]\nBeginString=" + version + "\nSenderCompID=BANZAI\nTargetCompID=EXECUTOR\n")
                .append(fixt ? "DefaultApplVerID=FIX.5.0\n" : "");
        }
        int port = Fixture.freePort();
        Fixture.Recorder executor = new Fixture.Recorder();
        List<Received> received = new CopyOnWriteArrayList<>();
        executor.onFromApp = (message, id) -> received.add(new Received(
            id.getBeginString().getValue(), MsgType.get(message.getHeader()), ClOrdID.get(message),
            OrigClOrdID.isSet(message) ? OrigClOrdID.get(message) : null, Price.isSet(message) ? Price.get(message) : null,
            StopPx.isSet(message) ? StopPx.get(message) : null, OrderQty.get(message)));
        OrderTableModel orders = new OrderTableModel();
        ExecutionTableModel executions = new ExecutionTableModel();
        BanzaiApplication banzai = new BanzaiApplication(orders, executions);
        BanzaiPanel panel = fromEdt(() -> new BanzaiPanel(orders, executions, banzai));
        try (SocketAcceptor acceptor = new SocketAcceptor(
                 executor, new MemoryStoreFactory(),
                 Fixture.settings("[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" + port + "\nStartTime=00:00:00"
                                  + "\nEndTime=00:00:00\nUseDataDictionary=Y\n" + acceptorSessions));
             SocketInitiator initiator = new SocketInitiator(banzai, new MemoryStoreFactory(),
                                                             initiatorSettings(port, initiatorSessions.toString()))) {
            acceptor.start();
            initiator.start();
            List<SessionID> ids = versions.stream().map(version -> new SessionID(version, "BANZAI", "EXECUTOR")).toList();
            for (SessionID id : ids) {
                Check.isTrue(Fixture.loggedOn(id), id + " logged on");
            }
            // Each a stop limit order, which has both prices; then, selected in the table, Cancel; then, selected
            // again, Replace with a new quantity and limit.
            List<Order> sent = fromEdt(() -> {
                JTable table = component(panel, JTable.class, "OrderTable");
                List<Order> result = new ArrayList<>();
                for (int row = 0; row < ids.size(); ++row) {
                    Order order = new Order();
                    order.setSessionID(ids.get(row));
                    order.setSymbol("LNUX");
                    order.setQuantity(100);
                    order.setType(OrderType.STOP_LIMIT);
                    order.setLimit(12.5);
                    order.setStop(12.0);
                    banzai.send(order);
                    orders.addOrder(order);
                    order.setOpen(100); // as its acknowledgement would: Cancel and Replace need something open
                    table.setRowSelectionInterval(row, row);
                    component(panel, JButton.class, "CancelButton").doClick();
                    table.setRowSelectionInterval(row, row);
                    component(panel, JTextField.class, "ReplaceQuantityTextField").setText("50");
                    component(panel, JTextField.class, "ReplaceLimitPriceTextField").setText("12.75");
                    component(panel, JButton.class, "ReplaceButton").doClick();
                    result.add(order);
                }
                return result;
            });
            boolean arrived = Check.eventually(20, () -> received.size() >= 3 * versions.size());
            Check.isTrue(arrived, "each version's order, cancel and replace was accepted; these were: " + received);
            for (int i = 0; i < versions.size(); ++i) {
                String version = versions.get(i);
                Order order = sent.get(i);
                List<Received> messages = received.stream().filter(message -> message.version().equals(version)).toList();
                Check.equal(new Received(version, "D", order.getID(), null, 12.5, 12.0, 100), messages.get(0),
                            version + "'s order");
                Received cancel = messages.get(1);
                Check.equal(new Received(version, "F", cancel.clOrdID(), order.getID(), null, null, 100), cancel,
                            version + "'s cancel");
                Received replace = messages.get(2);
                Check.equal(new Received(version, "G", replace.clOrdID(), order.getID(), 12.75, 12.0, 50), replace,
                            version + "'s replace");
                Check.isTrue(Stream.of(order.getID(), cancel.clOrdID(), replace.clOrdID()).distinct().count() == 3,
                             version + "'s cancel and replace have ClOrdIDs of their own");
                Check.isTrue(fromEdt(() -> orders.getOrder(cancel.clOrdID()) == order
                                           && orders.getOrder(replace.clOrdID()) == order),
                             "which find the order their reports are for");
            }
        }
    }
}
