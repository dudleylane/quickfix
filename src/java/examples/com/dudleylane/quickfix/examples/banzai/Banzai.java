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

import com.dudleylane.quickfix.FileStoreFactory;
import com.dudleylane.quickfix.ScreenLogFactory;
import com.dudleylane.quickfix.SessionSettings;
import com.dudleylane.quickfix.SocketInitiator;
import com.dudleylane.quickfix.examples.banzai.ui.BanzaiFrame;
import java.awt.event.WindowAdapter;
import java.awt.event.WindowEvent;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.CountDownLatch;
import javax.swing.SwingUtilities;
import javax.swing.WindowConstants;

/**
 * Banzai, a trading client with a window: upstream's examples/tradeclientgui, which upstream removed in 2023
 * (75633c25), on this binding. Enter an order and submit it to any session logged on; the orders and their fills
 * fill the tables as the reports come back, and the order selected can be cancelled or replaced. It trades with
 * ordermatch, in C++ or Java (examples/.../ordermatch), and with the executors. Run it with a session settings file:
 *
 *   java -cp quickfix.jar:examples com.dudleylane.quickfix.examples.banzai.Banzai banzai.cfg
 *
 * bin/run_banzai.sh runs it with bin/cfg/banzai.cfg. Closing the window logs out and ends it.
 */
public final class Banzai {
    private Banzai() {}

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("usage: Banzai FILE");
            System.exit(2);
        }
        SessionSettings settings = new SessionSettings();
        settings.setFromString(Files.readString(Path.of(args[0])));
        OrderTableModel orderTableModel = new OrderTableModel();
        ExecutionTableModel executionTableModel = new ExecutionTableModel();
        BanzaiApplication application = new BanzaiApplication(orderTableModel, executionTableModel);
        CountDownLatch closed = new CountDownLatch(1);
        try (SocketInitiator initiator = new SocketInitiator(application, new FileStoreFactory(settings), settings,
                                                             new ScreenLogFactory(settings))) {
            // The window before the sessions, so that it hears each one log on.
            SwingUtilities.invokeAndWait(() -> {
                BanzaiFrame frame = new BanzaiFrame(orderTableModel, executionTableModel, application);
                frame.setDefaultCloseOperation(WindowConstants.DISPOSE_ON_CLOSE);
                frame.addWindowListener(new WindowAdapter() {
                    @Override
                    public void windowClosed(WindowEvent e) {
                        closed.countDown();
                    }
                });
                frame.setVisible(true);
            });
            initiator.start();
            Runtime.getRuntime().addShutdownHook(new Thread(closed::countDown));
            closed.await();
        }
        System.exit(0);
    }
}
