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

package com.dudleylane.quickfix.examples.banzai;

import com.dudleylane.quickfix.*;
import com.dudleylane.quickfix.field.*;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.function.Consumer;
import javax.swing.SwingUtilities;

/**
 * Banzai's FIX side. It sends the orders, cancels and replaces the window asks for, in its session's version of FIX
 * -- 4.0 to 4.4, and FIX 5.0 over FIXT 1.1 -- and keeps the order and execution tables in step with the
 * ExecutionReports and OrderCancelRejects that come back.
 *
 * The engine calls the Application methods on its own threads, with a Message and a SessionID that are the
 * engine's and valid only for the call. Each is copied there and handed to the Swing thread, which alone reads and
 * changes the orders, the tables and this object's state; send, cancel and replace are called on it too.
 */
public class BanzaiApplication extends Application {
    private final OrderTableModel orderTableModel;
    private final ExecutionTableModel executionTableModel;
    private final List<Consumer<LogonEvent>> logonListeners = new CopyOnWriteArrayList<>();
    private final List<Consumer<Order>> orderListeners = new CopyOnWriteArrayList<>();
    // The ExecIDs each session has reported, by the session's string: SessionID compares by identity.
    private final Map<String, Set<String>> execIDs = new HashMap<>();

    public BanzaiApplication(OrderTableModel orderTableModel, ExecutionTableModel executionTableModel) {
        this.orderTableModel = orderTableModel;
        this.executionTableModel = executionTableModel;
    }

    @Override public void onCreate(SessionID sessionID) {}

    @Override
    public void onLogon(SessionID sessionID) {
        LogonEvent event = new LogonEvent(copy(sessionID), true);
        SwingUtilities.invokeLater(() -> logonListeners.forEach(listener -> listener.accept(event)));
    }

    @Override
    public void onLogout(SessionID sessionID) {
        LogonEvent event = new LogonEvent(copy(sessionID), false);
        SwingUtilities.invokeLater(() -> logonListeners.forEach(listener -> listener.accept(event)));
    }

    @Override public void toAdmin(Message message, SessionID sessionID) {}

    @Override
    public void toApp(Message message, SessionID sessionID) {
        // As examples/tradeclient does: an order the session resends would reach the counterparty as a second
        // order, so it goes as a gap fill instead.
        if (PossDupFlag.isSet(message.getHeader()) && PossDupFlag.get(message.getHeader())) {
            throw new DoNotSend();
        }
    }

    @Override public void fromAdmin(Message message, SessionID sessionID) {}

    @Override
    public void fromApp(Message message, SessionID sessionID) {
        Message copy = new Message(message);
        SessionID session = copy(sessionID);
        SwingUtilities.invokeLater(() -> process(copy, session));
    }

    /** listener hears, on the Swing thread, each session logging on or off. */
    public void addLogonListener(Consumer<LogonEvent> listener) {
        logonListeners.add(listener);
    }

    /** listener hears, on the Swing thread, each order an ExecutionReport changes. */
    public void addOrderListener(Consumer<Order> listener) {
        orderListeners.add(listener);
    }

    private void process(Message message, SessionID sessionID) {
        try {
            String msgType = MsgType.get(message.getHeader());
            if (msgType.equals(MsgType.ExecutionReport)) {
                executionReport(message, sessionID);
            } else if (msgType.equals(MsgType.OrderCancelReject)) {
                cancelReject(message);
            }
        } catch (FIXException e) {
            System.out.println(e); // a field Banzai reads is missing, or does not read as its type
        } finally {
            message.delete();
        }
    }

    private void executionReport(Message message, SessionID sessionID) {
        String execID = ExecID.get(message);
        if (!execIDs.computeIfAbsent(sessionID.toString(), session -> new HashSet<>()).add(execID)) {
            return;
        }

        String id = ClOrdID.get(message);
        Order order = orderTableModel.getOrder(id);
        if (order == null) {
            return;
        }

        int lastShares = LastShares.isSet(message) ? (int) LastShares.get(message) : 0;
        if (OrderQty.isSet(message)) {
            int orderQty = (int) OrderQty.get(message);
            int diff = order.getQuantity() - orderQty;
            order.setQuantity(orderQty);
            order.setOpen(order.getOpen() - diff);
        }
        if (Price.isSet(message)) {
            order.setLimit(Price.get(message));
        }
        if (lastShares > 0) {
            order.setExecuted((int) CumQty.get(message));
            order.setAvgPx(AvgPx.get(message));
        }

        char ordStatus = OrdStatus.get(message);
        if (LeavesQty.isSet(message)) {
            order.setOpen((int) LeavesQty.get(message));
        } else {
            // FIX 4.0 has no LeavesQty
            if (ordStatus == OrdStatus.REJECTED || ordStatus == OrdStatus.CANCELED) {
                order.setOpen(0);
            } else if (ordStatus == OrdStatus.NEW && order.isNew()) {
                order.setOpen(order.getQuantity());
                order.setNew(false);
            }
            order.setOpen(order.getOpen() - lastShares);
        }

        if (ordStatus == OrdStatus.REJECTED) {
            order.setRejected(true);
        } else if (ordStatus == OrdStatus.CANCELED || ordStatus == OrdStatus.DONE_FOR_DAY) {
            order.setCanceled(true);
        }
        order.setMessage(Text.isSet(message) ? Text.get(message) : null);

        orderTableModel.updateOrder(order, id);
        orderListeners.forEach(listener -> listener.accept(order));

        if (lastShares > 0) {
            executionTableModel.addExecution(new Execution(sessionID.toString() + execID, Symbol.get(message),
                                                           lastShares, OrderSide.of(Side.get(message)),
                                                           LastPx.get(message)));
        }
    }

    private void cancelReject(Message message) {
        Order order = orderTableModel.getOrder(ClOrdID.get(message));
        if (order != null && order.getOriginalID() != null) {
            order = orderTableModel.getOrder(order.getOriginalID());
        }
        if (order == null) {
            return;
        }
        if (Text.isSet(message)) {
            order.setMessage(Text.get(message));
        }
        orderTableModel.updateOrder(order, order.getID());
    }

    /** Sends order as a NewOrderSingle. IllegalArgumentException if its session's version is not one Banzai sends. */
    public void send(Order order) {
        String version = order.getSessionID().getBeginString().getValue();
        Message message = switch (version) {
        case "FIX.4.0" -> new com.dudleylane.quickfix.fix40.NewOrderSingle();
        case "FIX.4.1" -> new com.dudleylane.quickfix.fix41.NewOrderSingle();
        case "FIX.4.2" -> new com.dudleylane.quickfix.fix42.NewOrderSingle();
        case "FIX.4.3" -> new com.dudleylane.quickfix.fix43.NewOrderSingle();
        case "FIX.4.4" -> new com.dudleylane.quickfix.fix44.NewOrderSingle();
        case "FIXT.1.1" -> new com.dudleylane.quickfix.fix50.NewOrderSingle();
        default -> throw unsupported(version);
        };
        ClOrdID.set(message, order.getID());
        HandlInst.set(message, HandlInst.AUTOMATED_EXECUTION_NO_INTERVENTION);
        Symbol.set(message, order.getSymbol());
        Side.set(message, order.getSide().value());
        OrderQty.set(message, order.getQuantity());
        OrdType.set(message, order.getType().value());
        setTransactTime(message, version);
        setPrices(message, order);
        send(message, order.getSessionID());
    }

    /** Asks for the rest of order to be cancelled, under a ClOrdID of its own that finds the order. */
    public void cancel(Order order) {
        String version = order.getSessionID().getBeginString().getValue();
        Message message = switch (version) {
        case "FIX.4.0" -> new com.dudleylane.quickfix.fix40.OrderCancelRequest();
        case "FIX.4.1" -> new com.dudleylane.quickfix.fix41.OrderCancelRequest();
        case "FIX.4.2" -> new com.dudleylane.quickfix.fix42.OrderCancelRequest();
        case "FIX.4.3" -> new com.dudleylane.quickfix.fix43.OrderCancelRequest();
        case "FIX.4.4" -> new com.dudleylane.quickfix.fix44.OrderCancelRequest();
        case "FIXT.1.1" -> new com.dudleylane.quickfix.fix50.OrderCancelRequest();
        default -> throw unsupported(version);
        };
        String id = Order.generateID();
        OrigClOrdID.set(message, order.getID());
        ClOrdID.set(message, id);
        if (version.equals("FIX.4.0")) {
            CxlType.set(message, CxlType.FULL_REMAINING_QUANTITY);
        }
        Symbol.set(message, order.getSymbol());
        Side.set(message, order.getSide().value());
        OrderQty.set(message, order.getQuantity());
        setTransactTime(message, version);
        orderTableModel.addID(order, id);
        send(message, order.getSessionID());
    }

    /**
     * Asks for order to be replaced by newOrder, which order.replacement() made and the caller changed: its
     * quantity, prices and time in force go, as a replace restates the order.
     */
    public void replace(Order order, Order newOrder) {
        String version = order.getSessionID().getBeginString().getValue();
        Message message = switch (version) {
        case "FIX.4.0" -> new com.dudleylane.quickfix.fix40.OrderCancelReplaceRequest();
        case "FIX.4.1" -> new com.dudleylane.quickfix.fix41.OrderCancelReplaceRequest();
        case "FIX.4.2" -> new com.dudleylane.quickfix.fix42.OrderCancelReplaceRequest();
        case "FIX.4.3" -> new com.dudleylane.quickfix.fix43.OrderCancelReplaceRequest();
        case "FIX.4.4" -> new com.dudleylane.quickfix.fix44.OrderCancelReplaceRequest();
        case "FIXT.1.1" -> new com.dudleylane.quickfix.fix50.OrderCancelReplaceRequest();
        default -> throw unsupported(version);
        };
        OrigClOrdID.set(message, order.getID());
        ClOrdID.set(message, newOrder.getID());
        HandlInst.set(message, HandlInst.AUTOMATED_EXECUTION_NO_INTERVENTION);
        Symbol.set(message, order.getSymbol());
        Side.set(message, order.getSide().value());
        OrderQty.set(message, newOrder.getQuantity());
        OrdType.set(message, order.getType().value());
        setTransactTime(message, version);
        setPrices(message, newOrder);
        orderTableModel.addID(order, newOrder.getID());
        send(message, order.getSessionID());
    }

    private static void setPrices(Message message, Order order) {
        if (order.getType().hasLimit() && order.getLimit() != null) {
            Price.set(message, order.getLimit());
        }
        if (order.getType().hasStop() && order.getStop() != null) {
            StopPx.set(message, order.getStop());
        }
        TimeInForce.set(message, order.getTIF().value());
    }

    // FIX 4.2 added TransactTime to orders, cancels and replaces, required; 4.0 and 4.1 do not define it there.
    private static void setTransactTime(Message message, String version) {
        if (!version.equals("FIX.4.0") && !version.equals("FIX.4.1")) {
            TransactTime.set(message, Instant.now().truncatedTo(ChronoUnit.MILLIS));
        }
    }

    private static void send(Message message, SessionID sessionID) {
        try {
            Session.sendToTarget(message, sessionID);
        } catch (SessionNotFound e) {
            System.out.println(e);
        } finally {
            message.delete();
        }
    }

    private static IllegalArgumentException unsupported(String version) {
        return new IllegalArgumentException("Unsupported BeginString '" + version + "' in order");
    }

    // A callback's SessionID is a view of the engine's own; one kept past the callback is built afresh from it.
    private static SessionID copy(SessionID sessionID) {
        return new SessionID(sessionID.getBeginString().getValue(), sessionID.getSenderCompID().getValue(),
                             sessionID.getTargetCompID().getValue(), sessionID.getSessionQualifier());
    }
}
