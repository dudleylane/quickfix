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
import com.dudleylane.quickfix.examples.banzai.DoubleNumberTextField;
import com.dudleylane.quickfix.examples.banzai.IntegerNumberTextField;
import com.dudleylane.quickfix.examples.banzai.Order;
import java.awt.Color;
import java.awt.GridBagConstraints;
import java.awt.GridBagLayout;
import java.awt.Insets;
import java.awt.event.ActionListener;
import javax.swing.JButton;
import javax.swing.JComponent;
import javax.swing.JLabel;
import javax.swing.JPanel;

/** Cancels the order selected, or replaces it with a new quantity and limit price. */
@SuppressWarnings("serial") // Serializable as a Swing component; Banzai never serializes it
public class CancelReplacePanel extends JPanel {
    private final JLabel quantityLabel = new JLabel("Quantity");
    private final JLabel limitPriceLabel = new JLabel("Limit");
    private final IntegerNumberTextField quantityTextField = new IntegerNumberTextField();
    private final DoubleNumberTextField limitPriceTextField = new DoubleNumberTextField();
    private final JButton cancelButton = new JButton("Cancel");
    private final JButton replaceButton = new JButton("Replace");
    private final GridBagConstraints constraints = new GridBagConstraints();
    private final BanzaiApplication application;
    private Order order;

    public CancelReplacePanel(BanzaiApplication application) {
        this.application = application;
        cancelButton.setName("CancelButton");
        replaceButton.setName("ReplaceButton");
        quantityTextField.setName("ReplaceQuantityTextField");
        limitPriceTextField.setName("ReplaceLimitPriceTextField");
        cancelButton.addActionListener(e -> application.cancel(order));
        replaceButton.addActionListener(e -> replace());
        setLayout(new GridBagLayout());
        createComponents();
    }

    public void addActionListener(ActionListener listener) {
        cancelButton.addActionListener(listener);
        replaceButton.addActionListener(listener);
    }

    private void createComponents() {
        constraints.fill = GridBagConstraints.BOTH;
        constraints.weightx = 1;

        int x = 0;
        int y = 0;

        constraints.insets = new Insets(0, 0, 5, 5);
        add(cancelButton, x, y);
        add(replaceButton, ++x, y);
        constraints.weightx = 0;
        add(quantityLabel, ++x, y);
        constraints.weightx = 5;
        add(quantityTextField, ++x, y);
        constraints.weightx = 0;
        add(limitPriceLabel, ++x, y);
        constraints.weightx = 5;
        add(limitPriceTextField, ++x, y);
    }

    @Override
    public void setEnabled(boolean enabled) {
        super.setEnabled(enabled);
        cancelButton.setEnabled(enabled);
        replaceButton.setEnabled(enabled);
        quantityTextField.setEnabled(enabled);
        limitPriceTextField.setEnabled(enabled);

        Color labelColor = enabled ? Color.black : Color.gray;
        Color bgColor = enabled ? Color.white : Color.gray;
        quantityTextField.setBackground(bgColor);
        limitPriceTextField.setBackground(bgColor);
        quantityLabel.setForeground(labelColor);
        limitPriceLabel.setForeground(labelColor);
    }

    /** Shows the order's change: an order with nothing open can be neither cancelled nor replaced. */
    public void update() {
        setOrder(order);
    }

    public void setOrder(Order order) {
        if (order == null) {
            return;
        }
        this.order = order;
        quantityTextField.setText(Integer.toString(order.getOpen()));
        limitPriceTextField.setText(order.getLimit() == null ? "" : order.getLimit().toString());
        setEnabled(order.getOpen() > 0);
    }

    private JComponent add(JComponent component, int x, int y) {
        constraints.gridx = x;
        constraints.gridy = y;
        add(component, constraints);
        return component;
    }

    private void replace() {
        Order newOrder = order.replacement();
        try {
            newOrder.setQuantity(Integer.parseInt(quantityTextField.getText().trim()));
            newOrder.setLimit(Order.price(limitPriceTextField.getText()));
        } catch (NumberFormatException e) { // pasted: the fields take only numbers from the keyboard
            return;
        }
        newOrder.setRejected(false);
        newOrder.setCanceled(false);
        newOrder.setOpen(0);
        newOrder.setExecuted(0);
        application.replace(order, newOrder);
    }
}
