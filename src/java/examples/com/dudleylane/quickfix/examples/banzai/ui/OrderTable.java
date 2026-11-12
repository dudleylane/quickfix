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

package com.dudleylane.quickfix.examples.banzai.ui;

import com.dudleylane.quickfix.examples.banzai.BanzaiApplication;
import com.dudleylane.quickfix.examples.banzai.Order;
import com.dudleylane.quickfix.examples.banzai.OrderTableModel;
import java.awt.Color;
import java.awt.Component;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import javax.swing.JTable;
import javax.swing.table.DefaultTableCellRenderer;
import javax.swing.table.TableCellRenderer;

/**
 * The orders, coloured by state: red rejected, yellow not yet acknowledged, green open, white done. A double click
 * cancels the order clicked.
 */
@SuppressWarnings("serial") // Serializable as a Swing component; Banzai never serializes it
public class OrderTable extends JTable {
    private final OrderTableModel orderTableModel;

    public OrderTable(OrderTableModel orderTableModel, BanzaiApplication application) {
        super(orderTableModel);
        this.orderTableModel = orderTableModel;
        addMouseListener(new MouseAdapter() {
            @Override
            public void mouseClicked(MouseEvent e) {
                if (e.getClickCount() != 2) {
                    return;
                }
                Order order = orderTableModel.getOrder(rowAtPoint(e.getPoint()));
                if (order != null) {
                    application.cancel(order);
                }
            }
        });
    }

    @Override
    public Component prepareRenderer(TableCellRenderer renderer, int row, int column) {
        Order order = orderTableModel.getOrder(row);
        DefaultTableCellRenderer r = (DefaultTableCellRenderer) renderer;
        r.setForeground(Color.black);
        if (order.getRejected()) {
            r.setBackground(Color.red);
        } else if (order.getCanceled()) {
            r.setBackground(Color.white);
        } else if (order.getOpen() == 0 && order.getExecuted() == 0) {
            r.setBackground(Color.yellow);
        } else if (order.getOpen() > 0) {
            r.setBackground(Color.green);
        } else {
            r.setBackground(Color.white);
        }
        return super.prepareRenderer(renderer, row, column);
    }
}
