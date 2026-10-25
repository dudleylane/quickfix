package com.dudleylane.quickfix.test;

import com.dudleylane.quickfix.*;
import com.dudleylane.quickfix.field.*;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;

/** examples/.../Executor.java, run as its documentation says, filling an order sent to it. */
public class ExampleTests {
    public void testTheExecutorFillsALimitOrder() throws Exception {
        int port = Fixture.freePort();
        Path config = Files.createTempFile("executor", ".cfg");
        Files.writeString(config, "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" + port
                                  + "\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=Y\nDataDictionary="
                                  + Fixture.spec("FIX44") + "\n[SESSION]\nBeginString=FIX.4.4\nSenderCompID=EXECUTOR"
                                  + "\nTargetCompID=CLIENT\n");
        List<String> command = new ArrayList<>(List.of(ProcessHandle.current().info().command().orElse("java"), "-cp",
                                                       System.getProperty("java.class.path")));
        command.add("-Dcom.dudleylane.quickfix.library=" + System.getProperty("com.dudleylane.quickfix.library"));
        command.add("com.dudleylane.quickfix.examples.Executor");
        command.add(config.toString());
        Path log = Files.createTempFile("executor", ".log");
        Process executor = new ProcessBuilder(command).redirectErrorStream(true).redirectOutput(log.toFile()).start();
        try {
            Fixture.Recorder client = new Fixture.Recorder();
            List<Message> reports = new java.util.concurrent.CopyOnWriteArrayList<>();
            client.onFromApp = (message, id) -> reports.add(new Message(message)); // a copy: kept past the callback
            SessionSettings settings = Fixture.settings(
                "[DEFAULT]\nConnectionType=initiator\nSocketConnectHost=127.0.0.1\nSocketConnectPort=" + port
                + "\nHeartBtInt=30\nReconnectInterval=1\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=N\n"
                + "[SESSION]\nBeginString=FIX.4.4\nSenderCompID=CLIENT\nTargetCompID=EXECUTOR\n");
            SessionID id = new SessionID("FIX.4.4", "CLIENT", "EXECUTOR");
            try (SocketInitiator initiator = new SocketInitiator(client, new MemoryStoreFactory(), settings)) {
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
        } finally {
            executor.destroy(); // SIGTERM: the binding's shutdown hook stops the acceptor
            boolean exited = executor.waitFor(30, TimeUnit.SECONDS);
            String output = Files.readString(log);
            Files.deleteIfExists(config);
            Files.deleteIfExists(log);
            Check.isTrue(exited, "the executor stopped on SIGTERM");
            Check.isTrue(output.contains("Executor listening"), "it started:\n" + output);
            Check.isTrue(!output.contains("Sanitizer"), "a sanitizer report:\n" + output);
            Check.isTrue(executor.exitValue() == 143 || executor.exitValue() == 0,
                         "a clean SIGTERM exit, not a crash: " + executor.exitValue() + "\n" + output);
        }
    }
}
