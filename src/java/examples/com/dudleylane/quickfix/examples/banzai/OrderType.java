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

import com.dudleylane.quickfix.field.OrdType;

/** An order's type as Banzai shows it, and its OrdType (40) value. */
public enum OrderType {
    MARKET("Market", OrdType.MARKET),
    LIMIT("Limit", OrdType.LIMIT),
    STOP("Stop", OrdType.STOP),
    STOP_LIMIT("Stop Limit", OrdType.STOP_LIMIT);

    private final String name;
    private final char value;

    OrderType(String name, char value) {
        this.name = name;
        this.value = value;
    }

    public char value() {
        return value;
    }

    /** Whether an order of this type has a limit price. */
    public boolean hasLimit() {
        return this == LIMIT || this == STOP_LIMIT;
    }

    /** Whether an order of this type has a stop price. */
    public boolean hasStop() {
        return this == STOP || this == STOP_LIMIT;
    }

    @Override
    public String toString() {
        return name;
    }
}
