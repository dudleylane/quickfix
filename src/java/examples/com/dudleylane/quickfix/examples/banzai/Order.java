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

import com.dudleylane.quickfix.SessionID;
import java.util.concurrent.atomic.AtomicLong;

/** An order as Banzai shows it. Read and changed on the Swing thread only. */
public class Order implements Cloneable {
    // A ClOrdID must not repeat on its session, and the counterparty's book outlives a Banzai run: IDs counted from 1
    // in every run would cancel, or be filled for, the orders of the run before. So they carry the run's start time.
    private static final String RUN = Long.toString(System.currentTimeMillis(), 36);
    private static final AtomicLong nextID = new AtomicLong();

    private SessionID sessionID;
    private String symbol;
    private int quantity;
    private int open;
    private int executed;
    private OrderSide side = OrderSide.BUY;
    private OrderType type = OrderType.MARKET;
    private OrderTIF tif = OrderTIF.DAY;
    private Double limit;
    private Double stop;
    private double avgPx;
    private boolean rejected;
    private boolean canceled;
    private boolean isNew = true;
    private String message;
    private String id;
    private String originalID;

    public Order() {
        id = generateID();
    }

    public static String generateID() {
        return RUN + "-" + nextID.incrementAndGet();
    }

    /** A copy of this order under a new ClOrdID, naming this one's as its original: what a replace sends. */
    public Order replacement() {
        try {
            Order order = (Order) super.clone();
            order.originalID = id;
            order.id = generateID();
            return order;
        } catch (CloneNotSupportedException e) {
            throw new AssertionError(e);
        }
    }

    /** The session the order goes to: one Banzai owns, never one a callback was given. */
    public SessionID getSessionID() { return sessionID; }
    public void setSessionID(SessionID sessionID) { this.sessionID = sessionID; }
    public String getSymbol() { return symbol; }
    public void setSymbol(String symbol) { this.symbol = symbol; }
    public int getQuantity() { return quantity; }
    public void setQuantity(int quantity) { this.quantity = quantity; }
    public int getOpen() { return open; }
    public void setOpen(int open) { this.open = open; }
    public int getExecuted() { return executed; }
    public void setExecuted(int executed) { this.executed = executed; }
    public OrderSide getSide() { return side; }
    public void setSide(OrderSide side) { this.side = side; }
    public OrderType getType() { return type; }
    public void setType(OrderType type) { this.type = type; }
    public OrderTIF getTIF() { return tif; }
    public void setTIF(OrderTIF tif) { this.tif = tif; }
    public Double getLimit() { return limit; }
    public void setLimit(Double limit) { this.limit = limit; }
    public Double getStop() { return stop; }
    public void setStop(Double stop) { this.stop = stop; }
    public double getAvgPx() { return avgPx; }
    public void setAvgPx(double avgPx) { this.avgPx = avgPx; }
    public boolean getRejected() { return rejected; }
    public void setRejected(boolean rejected) { this.rejected = rejected; }
    public boolean getCanceled() { return canceled; }
    public void setCanceled(boolean canceled) { this.canceled = canceled; }
    public boolean isNew() { return isNew; }
    public void setNew(boolean isNew) { this.isNew = isNew; }
    public String getMessage() { return message; }
    public void setMessage(String message) { this.message = message; }
    public String getID() { return id; }
    public void setID(String id) { this.id = id; }
    public String getOriginalID() { return originalID; }

    /** A price from a text field: none when it is empty. NumberFormatException when it is not a number. */
    public static Double price(String text) {
        return text == null || text.isBlank() ? null : Double.valueOf(text.trim());
    }
}
