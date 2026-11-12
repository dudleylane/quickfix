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

package com.dudleylane.quickfix.examples.ordermatch;

import java.util.concurrent.atomic.AtomicLong;

/**
 * ExecIDs: examples/ordermatch/IDGenerator.h. The C++ counts from 1 in every run, but its sessions' stores outlive the
 * run, and a client that drops an ExecID it has already seen on the session -- Banzai does -- would drop every report
 * of the next run. These carry the run's start time.
 */
final class IDGenerator {
    private final String run = Long.toString(System.currentTimeMillis(), 36);
    private final AtomicLong executionID = new AtomicLong();

    String genExecutionID() {
        return run + "-" + executionID.incrementAndGet();
    }
}
