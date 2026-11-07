/* -*- C++ -*- */

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

#ifndef FIX_POSTGRESQLCONNECTION_H
#define FIX_POSTGRESQLCONNECTION_H

// Defines HAVE_POSTGRESQL for a program built against an install that has it (#137); within the library's own
// build, and in builds with no build tree, config.h or -DHAVE_POSTGRESQL does instead.
#ifndef SWIG
#if __has_include("QuickFIXBuildConfig.h")
#include "QuickFIXBuildConfig.h"
#endif
#endif

#ifndef HAVE_POSTGRESQL
#error PostgreSQLConnection.h included, but HAVE_POSTGRESQL not defined
#endif

#ifdef HAVE_POSTGRESQL

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786 4290)
#pragma comment(lib, "libpq")
#endif

#include "DatabaseConnectionID.h"
#include "DatabaseConnectionPool.h"
#include "Mutex.h"
#include <algorithm>
#include <libpq-fe.h>
#include <limits>

namespace FIX
{
#ifndef SWIG
/// libpq's error text, without the newline it ends with.
inline std::string postgreSQLError(const char *text)
{
    std::string error = text;
    while (!error.empty() && error.back() == '\n')
    {
        error.pop_back();
    }
    return error;
}
#endif

class PostgreSQLQuery
{
public:
    PostgreSQLQuery(const std::string &query) : m_result(0), m_query(query), m_parameters(0), m_resultFormat(0) {}

#ifndef SWIG
    /// A query whose one $1 stands for the value, which the server takes as its bytes rather than as SQL.
    PostgreSQLQuery(const std::string &query, const std::string &value)
        : m_result(0), m_query(query), m_value(value), m_parameters(1), m_resultFormat(0)
    {
    }

    /// Asks for results as the bytes a column holds, where text format would show a BYTEA column in hex. It applies to
    /// every column the query selects.
    void resultsAsBytes() { m_resultFormat = 1; }

    /// The length of a value, which getValue cannot tell for one holding a NUL.
    int getLength(int row, int column) { return PQgetlength(m_result, row, column); }
#endif

    ~PostgreSQLQuery()
    {
        if (m_result)
        {
            PQclear(m_result);
        }
    }

    bool execute(PGconn *pConnection)
    {
        int retry = 0;

        do
        {
            if (m_result)
            {
                PQclear(m_result);
            }
            m_result = send(pConnection);
            m_status = PQresultStatus(m_result);
            if (success())
            {
                return true;
            }
            PQreset(pConnection);
            retry++;
        } while (retry <= 1);
        return success();
    }

    bool success() { return m_status == PGRES_TUPLES_OK || m_status == PGRES_COMMAND_OK; }

    int rows() { return PQntuples(m_result); }

    char *reason() { return PQresultErrorMessage(m_result); }

    char *getValue(int row, int column) { return PQgetvalue(m_result, row, column); }

    void throwException() EXCEPT(IOException)
    {
        if (!success())
        {
            throw IOException("Query failed [" + m_query + "] " + postgreSQLError(reason()));
        }
    }

private:
    PGresult *send(PGconn *pConnection)
    {
        if (m_parameters == 0 && m_resultFormat == 0)
        {
            return PQexec(pConnection, m_query.c_str());
        }
        const char *value = m_value.data();
        // The server refuses a value of a gigabyte or more, so this limit cuts nothing it would have stored.
        const int length = static_cast<int>(std::min<std::size_t>(m_value.size(), std::numeric_limits<int>::max()));
        const int binary = 1;
        return PQexecParams(pConnection, m_query.c_str(), m_parameters, nullptr, &value, &length, &binary,
                            m_resultFormat);
    }

    PGresult *m_result;
    ExecStatusType m_status;
    std::string m_query;
    std::string m_value;
    int m_parameters;
    int m_resultFormat;
};

class PostgreSQLConnection
{
public:
    PostgreSQLConnection(const DatabaseConnectionID &id) : m_connectionID(id) { connect(); }

    PostgreSQLConnection(const std::string &database, const std::string &user, const std::string &password,
                         const std::string &host, unsigned short port)
        : m_connectionID(database, user, password, host, port)
    {
        connect();
    }

    ~PostgreSQLConnection()
    {
        if (m_pConnection)
        {
            PQfinish(m_pConnection);
        }
    }

    const DatabaseConnectionID &connectionID() { return m_connectionID; }

    bool connected()
    {
        Locker locker(m_mutex);
        return PQstatus(m_pConnection) == CONNECTION_OK;
    }

    bool reconnect()
    {
        Locker locker(m_mutex);
        PQreset(m_pConnection);
        return connected();
    }

    bool execute(PostgreSQLQuery &pQuery)
    {
        Locker locker(m_mutex);
        return pQuery.execute(m_pConnection);
    }

#ifndef SWIG
    /// A value escaped for a single-quoted literal on this connection, by its own settings rather than by those of
    /// whichever connection the process made last.
    std::string escape(const std::string &value)
    {
        std::string escaped((value.size() * 2) + 1, '\0');
        Locker locker(m_mutex);
        escaped.resize(PQescapeStringConn(m_pConnection, escaped.data(), value.data(), value.size(), nullptr));
        return escaped;
    }

    /// A value as a single-quoted literal on this connection.
    std::string quote(const std::string &value) { return "'" + escape(value) + "'"; }
#endif

private:
    void connect()
    {
        unsigned short port = m_connectionID.getPort();

        m_pConnection =
            PQsetdbLogin(m_connectionID.getHost().c_str(), port == 0 ? "" : IntConvertor::convert(port).c_str(), "", "",
                         m_connectionID.getDatabase().c_str(), m_connectionID.getUser().c_str(),
                         m_connectionID.getPassword().c_str());

        if (!connected())
        {
            // No destructor runs after a constructor throws.
            const std::string reason = postgreSQLError(PQerrorMessage(m_pConnection));
            PQfinish(m_pConnection);
            throw ConfigError("Unable to connect to postgres database '" + m_connectionID.getDatabase() +
                              "': " + m_connectionID.getUser() + '@' + m_connectionID.getHost() + ":" +
                              std::to_string(port) + " [" + reason + "]");
        }
    }

    PGconn *m_pConnection;
    DatabaseConnectionID m_connectionID;
    Mutex m_mutex;
};

typedef DatabaseConnectionPool<PostgreSQLConnection> PostgreSQLConnectionPool;
typedef std::unique_ptr<PostgreSQLConnectionPool> PostgreSQLConnectionPoolPtr;
} // namespace FIX

#endif // FIX_POSTGRESQLCONNECTION_H
#endif // HAVE_POSTGRESQL
