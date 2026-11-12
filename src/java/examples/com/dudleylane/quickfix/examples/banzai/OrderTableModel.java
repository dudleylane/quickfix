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

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import javax.swing.table.AbstractTableModel;

/**
 * The orders, a row each, found by any ClOrdID they have had: a cancel's and a replace's map to the order they act
 * on. Used on the Swing thread only.
 */
@SuppressWarnings("serial") // Serializable as a Swing model; Banzai never serializes it
public class OrderTableModel extends AbstractTableModel {
    private static final int SYMBOL = 0;
    private static final int QUANTITY = 1;
    private static final int OPEN = 2;
    private static final int EXECUTED = 3;
    private static final int SIDE = 4;
    private static final int TYPE = 5;
    private static final int LIMITPRICE = 6;
    private static final int STOPPRICE = 7;
    private static final int AVGPX = 8;
    private static final int TARGET = 9;

    private static final String[] HEADERS = {"Symbol", "Quantity", "Open", "Executed", "Side", "Type", "Limit",
                                             "Stop", "AvgPx", "Target"};

    private final List<Order> rows = new ArrayList<>();
    private final Map<String, Integer> idToRow = new HashMap<>();
    private final Map<String, Order> idToOrder = new HashMap<>();

    public void addOrder(Order order) {
        int row = rows.size();
        rows.add(order);
        idToRow.put(order.getID(), row);
        idToOrder.put(order.getID(), order);
        fireTableRowsInserted(row, row);
    }

    /** Shows order's change, and when id, the ClOrdID a report named, is not order's, makes it order's. */
    public void updateOrder(Order order, String id) {
        if (!id.equals(order.getID())) {
            String originalID = order.getID();
            order.setID(id);
            replaceOrder(order, originalID);
            return;
        }
        Integer row = idToRow.get(order.getID());
        if (row != null) {
            fireTableRowsUpdated(row, row);
        }
    }

    public void replaceOrder(Order order, String originalID) {
        Integer row = idToRow.get(originalID);
        if (row == null) {
            return;
        }
        rows.set(row, order);
        idToRow.put(order.getID(), row);
        idToOrder.put(order.getID(), order);
        fireTableRowsUpdated(row, row);
    }

    public void addID(Order order, String newID) {
        idToOrder.put(newID, order);
    }

    public Order getOrder(String id) {
        return idToOrder.get(id);
    }

    public Order getOrder(int row) {
        return row >= 0 && row < rows.size() ? rows.get(row) : null;
    }

    @Override
    public boolean isCellEditable(int rowIndex, int columnIndex) {
        return false;
    }

    @Override
    public Class<?> getColumnClass(int columnIndex) {
        return String.class;
    }

    @Override
    public int getRowCount() {
        return rows.size();
    }

    @Override
    public int getColumnCount() {
        return HEADERS.length;
    }

    @Override
    public String getColumnName(int columnIndex) {
        return HEADERS[columnIndex];
    }

    @Override
    public Object getValueAt(int rowIndex, int columnIndex) {
        Order order = rows.get(rowIndex);
        return switch (columnIndex) {
        case SYMBOL -> order.getSymbol();
        case QUANTITY -> order.getQuantity();
        case OPEN -> order.getOpen();
        case EXECUTED -> order.getExecuted();
        case SIDE -> order.getSide();
        case TYPE -> order.getType();
        case LIMITPRICE -> order.getLimit();
        case STOPPRICE -> order.getStop();
        case AVGPX -> order.getAvgPx();
        case TARGET -> order.getSessionID().getTargetCompID().getValue();
        default -> "";
        };
    }
}
