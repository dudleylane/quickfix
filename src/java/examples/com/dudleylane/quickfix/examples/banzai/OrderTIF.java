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

import com.dudleylane.quickfix.field.TimeInForce;

/** An order's time in force as Banzai shows it, and its TimeInForce (59) value. */
public enum OrderTIF {
    DAY("Day", TimeInForce.DAY),
    IOC("IOC", TimeInForce.IMMEDIATE_OR_CANCEL),
    OPG("OPG", TimeInForce.AT_THE_OPENING),
    GTC("GTC", TimeInForce.GOOD_TILL_CANCEL),
    GTX("GTX", TimeInForce.GOOD_TILL_CROSSING);

    private final String name;
    private final char value;

    OrderTIF(String name, char value) {
        this.name = name;
        this.value = value;
    }

    public char value() {
        return value;
    }

    @Override
    public String toString() {
        return name;
    }
}
