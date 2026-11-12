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

import com.dudleylane.quickfix.field.Side;

/** An order's side as Banzai shows it, and its Side (54) value. */
public enum OrderSide {
    BUY("Buy", Side.BUY),
    SELL("Sell", Side.SELL),
    SHORT_SELL("Short Sell", Side.SELL_SHORT),
    SHORT_SELL_EXEMPT("Short Sell Exempt", Side.SELL_SHORT_EXEMPT),
    CROSS("Cross", Side.CROSS),
    CROSS_SHORT("Cross Short", Side.CROSS_SHORT),
    CROSS_SHORT_EXEMPT("Cross Short Exempt", Side.CROSS_SHORT_EXEMPT);

    private final String name;
    private final char value;

    OrderSide(String name, char value) {
        this.name = name;
        this.value = value;
    }

    public char value() {
        return value;
    }

    /** The side whose Side value is value, or null for one Banzai does not offer. */
    public static OrderSide of(char value) {
        for (OrderSide side : values()) {
            if (side.value == value) {
                return side;
            }
        }
        return null;
    }

    @Override
    public String toString() {
        return name;
    }
}
