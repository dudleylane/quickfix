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

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.TreeMap;

/**
 * One symbol's book: examples/ordermatch/Market.{h,cpp}. Bids best (highest) first and asks best (lowest) first,
 * each price's orders in the order they came, as the C++ multimaps keep them.
 */
final class Market {
    private final TreeMap<Double, ArrayDeque<Order>> bidOrders = new TreeMap<>(Comparator.reverseOrder());
    private final TreeMap<Double, ArrayDeque<Order>> askOrders = new TreeMap<>();

    boolean insert(Order order) {
        book(order.getSide()).computeIfAbsent(order.getPrice(), price -> new ArrayDeque<>()).add(order);
        return true;
    }

    void erase(Order order) {
        for (Iterator<Map.Entry<Double, ArrayDeque<Order>>> level = book(order.getSide()).entrySet().iterator();
             level.hasNext();) {
            ArrayDeque<Order> orders = level.next().getValue();
            for (Iterator<Order> i = orders.iterator(); i.hasNext();) {
                if (i.next().getClientID().equals(order.getClientID())) {
                    i.remove();
                    if (orders.isEmpty()) {
                        level.remove();
                    }
                    return;
                }
            }
        }
    }

    /** The order on side with ClOrdID id; NoSuchElementException if there is none. */
    Order find(Order.Side side, String id) {
        for (ArrayDeque<Order> orders : book(side).values()) {
            for (Order order : orders) {
                if (order.getClientID().equals(id)) {
                    return order;
                }
            }
        }
        throw new NoSuchElementException(id);
    }

    /** Matches while the best bid reaches the best ask, returning both sides of each match as they were then. */
    List<Order> match() {
        List<Order> orders = new ArrayList<>();
        while (!bidOrders.isEmpty() && !askOrders.isEmpty()) {
            Order bid = bidOrders.firstEntry().getValue().getFirst();
            Order ask = askOrders.firstEntry().getValue().getFirst();
            if (bid.getPrice() < ask.getPrice()) {
                break;
            }
            match(bid, ask);
            orders.add(new Order(bid));
            orders.add(new Order(ask));
            if (bid.isClosed()) {
                removeFirst(bidOrders);
            }
            if (ask.isClosed()) {
                removeFirst(askOrders);
            }
        }
        return orders;
    }

    void display() {
        System.out.println("BIDS:");
        System.out.println("-----");
        System.out.println();
        bidOrders.values().forEach(orders -> orders.forEach(System.out::println));
        System.out.println();
        System.out.println();
        System.out.println("ASKS:");
        System.out.println("-----");
        System.out.println();
        askOrders.values().forEach(orders -> orders.forEach(System.out::println));
    }

    private TreeMap<Double, ArrayDeque<Order>> book(Order.Side side) {
        return side == Order.Side.BUY ? bidOrders : askOrders;
    }

    private static void removeFirst(TreeMap<Double, ArrayDeque<Order>> book) {
        ArrayDeque<Order> orders = book.firstEntry().getValue();
        orders.removeFirst();
        if (orders.isEmpty()) {
            book.pollFirstEntry();
        }
    }

    // At the ask's price, for as much as both have open.
    private static void match(Order bid, Order ask) {
        double price = ask.getPrice();
        long quantity = Math.min(bid.getOpenQuantity(), ask.getOpenQuantity());
        bid.execute(price, quantity);
        ask.execute(price, quantity);
    }
}
