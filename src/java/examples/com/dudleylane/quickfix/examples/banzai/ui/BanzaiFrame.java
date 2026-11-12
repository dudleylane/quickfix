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
import com.dudleylane.quickfix.examples.banzai.OrderTableModel;
import java.awt.BorderLayout;
import javax.swing.JFrame;

/** Banzai's window. */
@SuppressWarnings("serial") // Serializable as a Swing component; Banzai never serializes it
public class BanzaiFrame extends JFrame {
    public BanzaiFrame(OrderTableModel orderTableModel, ExecutionTableModel executionTableModel,
                       BanzaiApplication application) {
        setTitle("Banzai!");
        setSize(600, 400);
        getContentPane().add(new BanzaiPanel(orderTableModel, executionTableModel, application), BorderLayout.CENTER);
    }
}
