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

import com.dudleylane.quickfix.SessionID;
import com.dudleylane.quickfix.examples.banzai.BanzaiApplication;
import com.dudleylane.quickfix.examples.banzai.DoubleNumberTextField;
import com.dudleylane.quickfix.examples.banzai.IntegerNumberTextField;
import com.dudleylane.quickfix.examples.banzai.LogonEvent;
import com.dudleylane.quickfix.examples.banzai.Order;
import com.dudleylane.quickfix.examples.banzai.OrderSide;
import com.dudleylane.quickfix.examples.banzai.OrderTIF;
import com.dudleylane.quickfix.examples.banzai.OrderTableModel;
import com.dudleylane.quickfix.examples.banzai.OrderType;
import java.awt.Color;
import java.awt.Font;
import java.awt.GridBagConstraints;
import java.awt.GridBagLayout;
import java.awt.Insets;
import java.awt.event.ActionListener;
import javax.swing.BorderFactory;
import javax.swing.JButton;
import javax.swing.JComboBox;
import javax.swing.JComponent;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JTextField;
import javax.swing.SwingConstants;
import javax.swing.event.DocumentEvent;
import javax.swing.event.DocumentListener;

/** Where an order is entered: its symbol, quantity, side, type, prices and time in force, and its session. */
@SuppressWarnings("serial") // Serializable as a Swing component; Banzai never serializes it
public class OrderEntryPanel extends JPanel {
    private final JTextField symbolTextField = new JTextField();
    private final IntegerNumberTextField quantityTextField = new IntegerNumberTextField();

    private final JComboBox<OrderSide> sideComboBox = new JComboBox<>(OrderSide.values());
    private final JComboBox<OrderType> typeComboBox = new JComboBox<>(OrderType.values());
    private final JComboBox<OrderTIF> tifComboBox = new JComboBox<>(OrderTIF.values());

    private final DoubleNumberTextField limitPriceTextField = new DoubleNumberTextField();
    private final DoubleNumberTextField stopPriceTextField = new DoubleNumberTextField();

    // The sessions logged on: Banzai's own copies, shown by their strings.
    private final JComboBox<SessionID> sessionComboBox = new JComboBox<>();

    private final JLabel limitPriceLabel = new JLabel("Limit");
    private final JLabel stopPriceLabel = new JLabel("Stop");

    private final JLabel messageLabel = new JLabel(" ");
    private final JButton submitButton = new JButton("Submit");

    private final OrderTableModel orderTableModel;
    private final BanzaiApplication application;

    private final GridBagConstraints constraints = new GridBagConstraints();

    public OrderEntryPanel(OrderTableModel orderTableModel, BanzaiApplication application) {
        setName("OrderEntryPanel");
        this.orderTableModel = orderTableModel;
        this.application = application;

        application.addLogonListener(this::logonEvent);

        // On the documents, which typing, pasting and setText all change.
        DocumentListener activator = new DocumentListener() {
            @Override public void insertUpdate(DocumentEvent e) { activateSubmit(); }
            @Override public void removeUpdate(DocumentEvent e) { activateSubmit(); }
            @Override public void changedUpdate(DocumentEvent e) { activateSubmit(); }
        };
        symbolTextField.getDocument().addDocumentListener(activator);
        quantityTextField.getDocument().addDocumentListener(activator);
        limitPriceTextField.getDocument().addDocumentListener(activator);
        stopPriceTextField.getDocument().addDocumentListener(activator);
        sessionComboBox.addItemListener(e -> activateSubmit());

        setBorder(BorderFactory.createEmptyBorder(5, 0, 5, 0));
        setLayout(new GridBagLayout());
        createComponents();
    }

    public void addActionListener(ActionListener listener) {
        submitButton.addActionListener(listener);
    }

    public void setMessage(String message) {
        messageLabel.setText(message == null || message.isEmpty() ? " " : message);
    }

    public void clearMessage() {
        setMessage(null);
    }

    private void createComponents() {
        constraints.fill = GridBagConstraints.BOTH;
        constraints.weightx = 1;

        int x = 0;
        int y = 0;

        add(new JLabel("Symbol"), x, y);
        add(new JLabel("Quantity"), ++x, y);
        add(new JLabel("Side"), ++x, y);
        add(new JLabel("Type"), ++x, y);
        constraints.ipadx = 30;
        add(limitPriceLabel, ++x, y);
        add(stopPriceLabel, ++x, y);
        constraints.ipadx = 0;
        add(new JLabel("TIF"), ++x, y);
        constraints.ipadx = 30;

        symbolTextField.setName("SymbolTextField");
        add(symbolTextField, x = 0, ++y);
        constraints.ipadx = 0;
        quantityTextField.setName("QuantityTextField");
        add(quantityTextField, ++x, y);
        sideComboBox.setName("SideComboBox");
        add(sideComboBox, ++x, y);
        typeComboBox.setName("TypeComboBox");
        add(typeComboBox, ++x, y);
        limitPriceTextField.setName("LimitPriceTextField");
        add(limitPriceTextField, ++x, y);
        stopPriceTextField.setName("StopPriceTextField");
        add(stopPriceTextField, ++x, y);
        tifComboBox.setName("TifComboBox");
        add(tifComboBox, ++x, y);

        constraints.insets = new Insets(3, 0, 0, 0);
        constraints.gridwidth = GridBagConstraints.RELATIVE;
        sessionComboBox.setName("SessionComboBox");
        add(sessionComboBox, 0, ++y);
        constraints.gridwidth = GridBagConstraints.REMAINDER;
        submitButton.setName("SubmitButton");
        add(submitButton, x, y);
        constraints.gridwidth = 0;
        messageLabel.setName("MessageLabel");
        add(messageLabel, 0, ++y);

        typeComboBox.addItemListener(e -> {
            OrderType type = (OrderType) typeComboBox.getSelectedItem();
            enable(limitPriceTextField, limitPriceLabel, type.hasLimit());
            enable(stopPriceTextField, stopPriceLabel, type.hasStop());
            activateSubmit();
        });
        typeComboBox.setSelectedItem(OrderType.STOP);
        typeComboBox.setSelectedItem(OrderType.MARKET);

        messageLabel.setFont(new Font(messageLabel.getFont().getFontName(), Font.BOLD, 12));
        messageLabel.setForeground(Color.red);
        messageLabel.setHorizontalAlignment(SwingConstants.CENTER);
        submitButton.setEnabled(false);
        submitButton.addActionListener(e -> submit());
        activateSubmit();
    }

    private JComponent add(JComponent component, int x, int y) {
        constraints.gridx = x;
        constraints.gridy = y;
        add(component, constraints);
        return component;
    }

    private static void enable(JTextField field, JLabel label, boolean enabled) {
        field.setEnabled(enabled);
        field.setBackground(enabled ? Color.white : Color.gray);
        label.setForeground(enabled ? Color.black : Color.gray);
    }

    // Submit takes a symbol, a quantity and a session, and the prices the order's type has.
    private void activateSubmit() {
        OrderType type = (OrderType) typeComboBox.getSelectedItem();
        boolean activate = entered(symbolTextField) && entered(quantityTextField)
                           && sessionComboBox.getSelectedItem() != null;
        submitButton.setEnabled(activate && (!type.hasLimit() || entered(limitPriceTextField))
                                && (!type.hasStop() || entered(stopPriceTextField)));
    }

    private static boolean entered(JTextField field) {
        return !field.getText().isBlank();
    }

    private void logonEvent(LogonEvent event) {
        if (event.loggedOn()) {
            sessionComboBox.addItem(event.sessionID());
            return;
        }
        // SessionID compares by identity: the session to go is the item with its string.
        for (int i = 0; i < sessionComboBox.getItemCount(); ++i) {
            if (sessionComboBox.getItemAt(i).toString().equals(event.sessionID().toString())) {
                sessionComboBox.removeItemAt(i);
                return;
            }
        }
    }

    private void submit() {
        Order order = new Order();
        order.setSide((OrderSide) sideComboBox.getSelectedItem());
        order.setType((OrderType) typeComboBox.getSelectedItem());
        order.setTIF((OrderTIF) tifComboBox.getSelectedItem());
        order.setSymbol(symbolTextField.getText().trim());
        order.setSessionID((SessionID) sessionComboBox.getSelectedItem());
        try {
            order.setQuantity(Integer.parseInt(quantityTextField.getText().trim()));
            if (order.getType().hasLimit()) {
                order.setLimit(Order.price(limitPriceTextField.getText()));
            }
            if (order.getType().hasStop()) {
                order.setStop(Order.price(stopPriceTextField.getText()));
            }
            application.send(order);
        } catch (IllegalArgumentException e) { // a pasted number that is not one, or a version Banzai cannot send
            setMessage("Order not sent: " + e.getMessage());
            return;
        }
        // Its reports are handled on this thread too, after this returns, so the row is there before them.
        orderTableModel.addOrder(order);
    }
}
