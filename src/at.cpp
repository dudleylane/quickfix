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

#ifdef _MSC_VER
#pragma warning(disable : 4503)
#endif
#include "config.h"

#include "FileStore.h"
#include "Log.h"
#include "SessionSettings.h"
#include "SocketAcceptor.h"
#include "ThreadedSocketAcceptor.h"
#include "Utility.h"
#include "at_application.h"
#include "getopt-repl.h"
#include <fstream>
#include <iostream>
#include <memory>
#include <signal.h>

typedef std::unique_ptr<FIX::Acceptor> AcceptorPtr;

int main(int argc, char **argv)
{
    std::string file;
    bool threaded = false;
    bool eventLog = false;
    bool usage = false;

    int option;
    while ((option = getopt(argc, argv, "+f:tl")) != -1)
    {
        switch (option)
        {
        case 'f':
            file = optarg;
            break;
        case 't':
            threaded = true;
            break;
        case 'l':
            eventLog = true;
            break;
        default:
            usage = true;
            break;
        }
    }

    if (usage || file.empty())
    {
        std::cout << "usage: " << argv[0] << " -f FILE [-t] [-l]" << std::endl;
        return 1;
    }

    // runat.sh stops this process with SIGTERM. At its default action it
    // skips every destructor and LeakSanitizer's at-exit check, so an
    // ASan build could never report a leak (#84). Block it here, before any
    // engine thread exists so that all of them inherit the mask, and take it
    // with sigwait() below: no engine thread sees EINTR, and there is no
    // handler to keep async-signal-safe.
    sigset_t stopSignals;
    sigemptyset(&stopSignals);
    sigaddset(&stopSignals, SIGTERM);
    sigaddset(&stopSignals, SIGINT);
    pthread_sigmask(SIG_BLOCK, &stopSignals, nullptr);

    try
    {
        FIX::SessionSettings settings(file);
        Application application;
        FIX::FileStoreFactory factory("store");

        // -l: session events only, on stdout. Without it every session gets a
        // null log, so seeing what the engine did during a run meant editing
        // this file. Declared before the acceptor, which holds a reference.
        std::unique_ptr<FIX::LogFactory> pLogFactory;
        if (eventLog)
        {
            pLogFactory = std::make_unique<FIX::ScreenLogFactory>(false, false, true);
        }

        AcceptorPtr pAcceptor;
        if (threaded)
        {
            pAcceptor.reset(pLogFactory ? new FIX::ThreadedSocketAcceptor(application, factory, settings, *pLogFactory)
                                        : new FIX::ThreadedSocketAcceptor(application, factory, settings));
        }
        else
        {
            pAcceptor.reset(pLogFactory ? new FIX::SocketAcceptor(application, factory, settings, *pLogFactory)
                                        : new FIX::SocketAcceptor(application, factory, settings));
        }

        pAcceptor->start();
        int received = 0;
        sigwait(&stopSignals, &received);
        pAcceptor->stop();
    }
    catch (std::exception &e)
    {
        std::cout << e.what();
        return 2;
    }

    return 0;
}
