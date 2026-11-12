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
import com.dudleylane.quickfix.examples.banzai.ExecutionTableModel;
import com.dudleylane.quickfix.examples.banzai.Order;
import com.dudleylane.quickfix.examples.banzai.OrderTableModel;
import java.awt.GridBagConstraints;
import java.awt.GridBagLayout;
import java.awt.Insets;
import java.awt.event.ActionListener;
import javax.swing.BorderFactory;
import javax.swing.JPanel;
import javax.swing.JTabbedPane;
import javax.swing.JTable;

/**
 * Banzai's window content: order entry above, the orders and executions in tabs, and cancel and replace for the
 * order selected below.
 */
@SuppressWarnings("serial") // Serializable as a Swing component; Banzai never serializes it
public class BanzaiPanel extends JPanel {
    private final OrderEntryPanel orderEntryPanel;
    private final OrderPanel orderPanel;
    private final CancelReplacePanel cancelReplacePanel;
    private final OrderTableModel orderTableModel;

    public BanzaiPanel(OrderTableModel orderTableModel, ExecutionTableModel executionTableModel,
                       BanzaiApplication application) {
        setName("BanzaiPanel");
        this.orderTableModel = orderTableModel;

        setBorder(BorderFactory.createEmptyBorder(5, 5, 5, 5));
        setLayout(new GridBagLayout());
        GridBagConstraints constraints = new GridBagConstraints();
        constraints.fill = GridBagConstraints.BOTH;
        constraints.weightx = 1;

        orderEntryPanel = new OrderEntryPanel(orderTableModel, application);
        constraints.insets = new Insets(0, 0, 5, 0);
        add(orderEntryPanel, constraints);

        constraints.gridx++;
        constraints.weighty = 10;

        JTabbedPane tabbedPane = new JTabbedPane();
        orderPanel = new OrderPanel(orderTableModel, application);
        tabbedPane.add("Orders", orderPanel);
        tabbedPane.add("Executions", new ExecutionPanel(executionTableModel));
        add(tabbedPane, constraints);

        cancelReplacePanel = new CancelReplacePanel(application);
        constraints.weighty = 0;
        add(cancelReplacePanel, constraints);
        cancelReplacePanel.setEnabled(false);

        // Submit, Cancel and Replace each clear the selection.
        ActionListener clearSelection = e -> orderPanel.orderTable().getSelectionModel().clearSelection();
        orderEntryPanel.addActionListener(clearSelection);
        cancelReplacePanel.addActionListener(clearSelection);
        orderPanel.orderTable().getSelectionModel().addListSelectionListener(e -> orderSelected());
        application.addOrderListener(order -> cancelReplacePanel.update());
    }

    // One order selected shows its message, and is the one Cancel and Replace act on.
    private void orderSelected() {
        JTable orderTable = orderPanel.orderTable();
        if (orderTable.getSelectedRowCount() != 1) {
            orderEntryPanel.clearMessage();
            return;
        }
        Order order = orderTableModel.getOrder(orderTable.getSelectedRow());
        if (order != null) {
            orderEntryPanel.setMessage(order.getMessage());
            cancelReplacePanel.setOrder(order);
        }
    }
}
