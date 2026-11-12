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

import java.awt.event.KeyEvent;
import javax.swing.JTextField;

/** A text field that takes only digits and one decimal point from the keyboard. */
@SuppressWarnings("serial") // Serializable as a Swing component; Banzai never serializes it
public class DoubleNumberTextField extends JTextField {
    @Override
    public void processKeyEvent(KeyEvent e) {
        char keyChar = e.getKeyChar();
        if ((keyChar >= '0' && keyChar <= '9') || keyChar == KeyEvent.VK_BACK_SPACE || keyChar == KeyEvent.VK_DELETE) {
            super.processKeyEvent(e);
        } else if (keyChar == '.' && getText().indexOf('.') == -1) {
            super.processKeyEvent(e);
        }
    }
}
