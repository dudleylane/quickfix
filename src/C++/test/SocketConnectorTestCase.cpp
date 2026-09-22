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

#include "config.h"

#include "TestHelper.h"
#include <SocketConnector.h>
#include <SocketServer.h>
#ifdef _MSC_VER
#include <stdlib.h>
#endif

#include "catch_amalgamated.hpp"

using namespace FIX;

TEST_CASE("SocketConnectorTests")
{
    SECTION("accept")
    {
        SocketConnector object;
        SocketServer server(0);
        // Port 0 lets the OS pick a free one; binding a fixed port collides
        // with anything else on the machine using it, including the
        // acceptance suite and a concurrent CI run.
        socket_handle socket = server.add(0, true, true);
        const uint16_t port = socket_hostport(socket);
        CHECK(object.connect("127.0.0.1", port, false, 1024, 1024));
        CHECK(server.accept(socket));
        server.close();
    }
}
