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
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import javax.swing.table.AbstractTableModel;

/** The fills, a row each, each once. Used on the Swing thread only. */
@SuppressWarnings("serial") // Serializable as a Swing model; Banzai never serializes it
public class ExecutionTableModel extends AbstractTableModel {
    private static final int SYMBOL = 0;
    private static final int QUANTITY = 1;
    private static final int SIDE = 2;
    private static final int PRICE = 3;

    private static final String[] HEADERS = {"Symbol", "Quantity", "Side", "Price"};

    private final List<Execution> rows = new ArrayList<>();
    private final Set<String> exchangeIDs = new HashSet<>();

    public void addExecution(Execution execution) {
        if (!exchangeIDs.add(execution.exchangeID())) {
            return;
        }
        int row = rows.size();
        rows.add(execution);
        fireTableRowsInserted(row, row);
    }

    public Execution getExecution(int row) {
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
        Execution execution = rows.get(rowIndex);
        return switch (columnIndex) {
        case SYMBOL -> execution.symbol();
        case QUANTITY -> execution.quantity();
        case SIDE -> execution.side();
        case PRICE -> execution.price();
        default -> "";
        };
    }
}
