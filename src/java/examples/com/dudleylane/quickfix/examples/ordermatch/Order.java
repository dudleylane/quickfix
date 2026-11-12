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

/** An order in the book: examples/ordermatch/Order.h. */
final class Order {
    enum Side { BUY, SELL }

    enum Type { MARKET, LIMIT }

    private final String clientID;
    private final String symbol;
    private final String owner;
    private final String target;
    private final Side side;
    private final Type type;
    private final double price;
    private final long quantity;

    private long openQuantity;
    private long executedQuantity;
    private double avgExecutedPrice;
    private double lastExecutedPrice;
    private long lastExecutedQuantity;

    Order(String clientID, String symbol, String owner, String target, Side side, Type type, double price,
          long quantity) {
        this.clientID = clientID;
        this.symbol = symbol;
        this.owner = owner;
        this.target = target;
        this.side = side;
        this.type = type;
        this.price = price;
        this.quantity = quantity;
        openQuantity = quantity;
    }

    /** The order as it is now, for a report sent after the book has moved on: the C++ queues copies. */
    Order(Order order) {
        this(order.clientID, order.symbol, order.owner, order.target, order.side, order.type, order.price,
             order.quantity);
        openQuantity = order.openQuantity;
        executedQuantity = order.executedQuantity;
        avgExecutedPrice = order.avgExecutedPrice;
        lastExecutedPrice = order.lastExecutedPrice;
        lastExecutedQuantity = order.lastExecutedQuantity;
    }

    String getClientID() { return clientID; }
    String getSymbol() { return symbol; }
    String getOwner() { return owner; }
    String getTarget() { return target; }
    Side getSide() { return side; }
    Type getType() { return type; }
    double getPrice() { return price; }
    long getQuantity() { return quantity; }

    long getOpenQuantity() { return openQuantity; }
    long getExecutedQuantity() { return executedQuantity; }
    double getAvgExecutedPrice() { return avgExecutedPrice; }
    double getLastExecutedPrice() { return lastExecutedPrice; }
    long getLastExecutedQuantity() { return lastExecutedQuantity; }

    boolean isFilled() { return quantity == executedQuantity; }
    boolean isClosed() { return openQuantity == 0; }

    void execute(double price, long quantity) {
        avgExecutedPrice = ((quantity * price) + (avgExecutedPrice * executedQuantity)) / (quantity + executedQuantity);
        openQuantity -= quantity;
        executedQuantity += quantity;
        lastExecutedPrice = price;
        lastExecutedQuantity = quantity;
    }

    void cancel() { openQuantity = 0; }

    @Override
    public String toString() {
        return String.format("ID: %10s%s OWNER: %10s%s PRICE: %10s%s QUANTITY: %10s%s", ",", clientID, ",", owner,
                             ",", price, ",", quantity);
    }
}
