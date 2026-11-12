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

import java.util.List;
import java.util.NoSuchElementException;
import java.util.TreeMap;

/**
 * The books by symbol: examples/ordermatch/OrderMatcher.h. Synchronized, unlike the C++, whose console prints a book
 * while the engine's thread changes it: a caller that needs several calls to be one step holds this object's lock
 * across them.
 */
final class OrderMatcher {
    private final TreeMap<String, Market> markets = new TreeMap<>();

    synchronized boolean insert(Order order) {
        return markets.computeIfAbsent(order.getSymbol(), symbol -> new Market()).insert(order);
    }

    synchronized void erase(Order order) {
        Market market = markets.get(order.getSymbol());
        if (market != null) {
            market.erase(order);
        }
    }

    /** The order with ClOrdID id; NoSuchElementException if there is none. */
    synchronized Order find(String symbol, Order.Side side, String id) {
        Market market = markets.get(symbol);
        if (market == null) {
            throw new NoSuchElementException(symbol);
        }
        return market.find(side, id);
    }

    synchronized List<Order> match(String symbol) {
        Market market = markets.get(symbol);
        return market == null ? List.of() : market.match();
    }

    synchronized void display(String symbol) {
        Market market = markets.get(symbol);
        if (market != null) {
            market.display();
        }
    }

    synchronized void display() {
        System.out.println("SYMBOLS:");
        System.out.println("--------");
        markets.keySet().forEach(System.out::println);
    }
}
