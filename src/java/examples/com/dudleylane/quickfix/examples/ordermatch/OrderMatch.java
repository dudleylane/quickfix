/****************************************************************************
** Copyright (c) 2001-2014
**
** This file is part of the QuickFIX FIX Engine
**
** This file may be distributed under the terms of the quickfixengine.org
** license as defined by quickfixengine.org and appearing in the file
** LICENSE included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://www.quickfixengine.org/LICENSE for licensing information.
**
** Contact ask@quickfixengine.org if any conditions of this licensing are
** not clear to you.
**
****************************************************************************/

package com.dudleylane.quickfix.examples.ordermatch;

import com.dudleylane.quickfix.*;
import com.dudleylane.quickfix.field.*;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.NoSuchElementException;
import java.util.Scanner;
import java.util.concurrent.CountDownLatch;

/**
 * examples/ordermatch in Java: an acceptor that keeps a book per symbol of the FIX 4.2 limit orders it receives,
 * matches a bid and an ask when they cross, at the ask's price, and answers each order, fill, cancel and reject
 * with an ExecutionReport. Banzai (examples/.../banzai) trades with it. Run it with a session settings file, as the
 * C++ one runs:
 *
 *   java -cp quickfix.jar:examples com.dudleylane.quickfix.examples.ordermatch.OrderMatch ordermatch.cfg
 *
 * Its input takes the C++ one's commands: a symbol prints that symbol's book, #symbols the symbols, #quit stops it.
 * At the end of its input it runs on until SIGINT or SIGTERM, through the shutdown hook the binding registers for
 * running transports.
 */
public final class OrderMatch extends Application {
    private final OrderMatcher orderMatcher = new OrderMatcher();
    private final IDGenerator generator = new IDGenerator();

    @Override public void onCreate(SessionID id) {}
    @Override public void onLogon(SessionID id) {}
    @Override public void onLogout(SessionID id) {}
    @Override public void toAdmin(Message message, SessionID id) {}
    @Override public void toApp(Message message, SessionID id) {}
    @Override public void fromAdmin(Message message, SessionID id) {}

    @Override
    public void fromApp(Message message, SessionID id) {
        // message is the engine's, and valid only until this returns: everything kept is read out of it here.
        // What the C++ one's MessageCracker hands its handlers; anything else, a FIX 5.0 order on its FIXT
        // sessions included, is unsupported there too.
        String beginString = message.getHeader().getField(BeginString.FIELD);
        String msgType = MsgType.get(message.getHeader());
        if (beginString.equals("FIX.4.2") && msgType.equals(MsgType.NewOrderSingle)) {
            onNewOrderSingle(message);
        } else if (beginString.equals("FIX.4.2") && msgType.equals(MsgType.OrderCancelRequest)) {
            onOrderCancelRequest(message);
        } else if (beginString.equals("FIX.4.2") && msgType.equals(MsgType.MarketDataRequest)) {
            // Read and not answered, as in the C++; a subscription, rather than a snapshot, is refused.
            if (SubscriptionRequestType.get(message) != SubscriptionRequestType.SNAPSHOT) {
                throw new IncorrectTagValue(SubscriptionRequestType.FIELD);
            }
        } else if (beginString.equals("FIX.4.3") && msgType.equals(MsgType.MarketDataRequest)) {
            System.out.println(message.toXML());
        } else {
            throw new UnsupportedMessageType();
        }
    }

    private void onNewOrderSingle(Message message) {
        String senderCompID = SenderCompID.get(message.getHeader());
        String targetCompID = TargetCompID.get(message.getHeader());
        String clOrdID = ClOrdID.get(message);
        String symbol = Symbol.get(message);
        char side = Side.get(message);
        char ordType = OrdType.get(message);
        double price = ordType == OrdType.LIMIT ? Price.get(message) : 0;
        double orderQty = OrderQty.get(message);
        char timeInForce = TimeInForce.isSet(message) ? TimeInForce.get(message) : TimeInForce.DAY;

        try {
            if (timeInForce != TimeInForce.DAY) {
                throw new IllegalArgumentException("Unsupported TIF, use Day");
            }
            processOrder(new Order(clOrdID, symbol, senderCompID, targetCompID, convertSide(side),
                                   convertType(ordType), price, (long) orderQty));
        } catch (IllegalArgumentException e) {
            rejectOrder(senderCompID, targetCompID, clOrdID, symbol, side, e.getMessage());
        }
    }

    private void onOrderCancelRequest(Message message) {
        String origClOrdID = OrigClOrdID.get(message);
        String symbol = Symbol.get(message);
        char side = Side.get(message);
        try {
            processCancel(origClOrdID, symbol, convertSide(side));
        } catch (IllegalArgumentException | NoSuchElementException e) {
            // As in the C++: a cancel for an order that is not in the book goes unanswered.
        }
    }

    private void processOrder(Order order) {
        synchronized (orderMatcher) {
            if (!orderMatcher.insert(order)) {
                updateOrder(order, OrdStatus.REJECTED);
                return;
            }
            updateOrder(order, OrdStatus.NEW);
            List<Order> orders = orderMatcher.match(order.getSymbol());
            for (Order matched : orders) {
                updateOrder(matched, matched.isFilled() ? OrdStatus.FILLED : OrdStatus.PARTIALLY_FILLED);
            }
        }
    }

    private void processCancel(String id, String symbol, Order.Side side) {
        synchronized (orderMatcher) {
            Order order = orderMatcher.find(symbol, side, id);
            order.cancel();
            updateOrder(order, OrdStatus.CANCELED);
            orderMatcher.erase(order);
        }
    }

    private void updateOrder(Order order, char status) {
        var report = new com.dudleylane.quickfix.fix42.ExecutionReport();
        OrderID.set(report, order.getClientID());
        ExecID.set(report, generator.genExecutionID());
        ExecTransType.set(report, ExecTransType.NEW);
        ExecType.set(report, status);
        OrdStatus.set(report, status);
        Symbol.set(report, order.getSymbol());
        Side.set(report, convertSide(order.getSide()));
        LeavesQty.set(report, order.getOpenQuantity());
        CumQty.set(report, order.getExecutedQuantity());
        AvgPx.set(report, order.getAvgExecutedPrice());
        ClOrdID.set(report, order.getClientID());
        OrderQty.set(report, order.getQuantity());
        if (status == OrdStatus.FILLED || status == OrdStatus.PARTIALLY_FILLED) {
            LastShares.set(report, order.getLastExecutedQuantity());
            LastPx.set(report, order.getLastExecutedPrice());
        }
        send(report, order.getTarget(), order.getOwner());
    }

    private void rejectOrder(String senderCompID, String targetCompID, String clOrdID, String symbol, char side,
                             String text) {
        var report = new com.dudleylane.quickfix.fix42.ExecutionReport();
        OrderID.set(report, clOrdID);
        ExecID.set(report, generator.genExecutionID());
        ExecTransType.set(report, ExecTransType.NEW);
        ExecType.set(report, ExecType.REJECTED);
        OrdStatus.set(report, OrdStatus.REJECTED);
        Symbol.set(report, symbol);
        Side.set(report, side);
        LeavesQty.set(report, 0);
        CumQty.set(report, 0);
        AvgPx.set(report, 0);
        ClOrdID.set(report, clOrdID);
        Text.set(report, text);
        send(report, targetCompID, senderCompID);
    }

    // The ExecutionReport class names FIX.4.2, which picks the session among those between the two CompIDs.
    private static void send(Message report, String senderCompID, String targetCompID) {
        try {
            Session.sendToTarget(report, senderCompID, targetCompID);
        } catch (SessionNotFound e) {
            // As in the C++: the client's session is gone, and the report with it.
        } finally {
            report.delete();
        }
    }

    private static Order.Side convertSide(char side) {
        return switch (side) {
        case Side.BUY -> Order.Side.BUY;
        case Side.SELL -> Order.Side.SELL;
        default -> throw new IllegalArgumentException("Unsupported Side, use buy or sell");
        };
    }

    private static Order.Type convertType(char ordType) {
        if (ordType != OrdType.LIMIT) {
            throw new IllegalArgumentException("Unsupported Order Type, use limit");
        }
        return Order.Type.LIMIT;
    }

    private static char convertSide(Order.Side side) {
        return side == Order.Side.BUY ? Side.BUY : Side.SELL;
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("usage: OrderMatch FILE");
            System.exit(2);
        }
        SessionSettings settings = new SessionSettings();
        settings.setFromString(Files.readString(Path.of(args[0])));
        OrderMatch application = new OrderMatch();
        try (SocketAcceptor acceptor = new SocketAcceptor(application, new FileStoreFactory(settings), settings,
                                                          new ScreenLogFactory(settings))) {
            acceptor.start();
            System.out.println("OrderMatch listening");
            Scanner input = new Scanner(System.in);
            while (input.hasNext()) {
                String value = input.next();
                if (value.equals("#quit")) {
                    return;
                } else if (value.equals("#symbols")) {
                    application.orderMatcher.display();
                } else {
                    application.orderMatcher.display(value);
                }
                System.out.println();
            }
            CountDownLatch stopped = new CountDownLatch(1);
            Runtime.getRuntime().addShutdownHook(new Thread(stopped::countDown));
            stopped.await();
        }
    }
}
