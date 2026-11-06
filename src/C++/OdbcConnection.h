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

// Defines HAVE_ODBC for a program built against an install that has it (#137); within the library's own
// build, and in builds with no build tree, config.h or -DHAVE_ODBC does instead.
#ifndef SWIG
#if __has_include("QuickFIXBuildConfig.h")
#include "QuickFIXBuildConfig.h"
#endif
#endif

#ifndef HAVE_ODBC
#error OdbcConnection.h included, but HAVE_ODBC not defined
#endif

#ifdef HAVE_ODBC
#ifndef FIX_ODBCCONNECTION_H
#define FIX_ODBCCONNECTION_H

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786 4290)
#pragma comment(lib, "Odbc32")
#endif

#include "DatabaseConnectionID.h"
#include "DatabaseConnectionPool.h"
#include "Exceptions.h"
#include "Mutex.h"
#include "Utility.h"
#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>
#include <sstream>

namespace FIX
{

inline std::string odbcError(SQLSMALLINT statementType, SQLHANDLE handle)
{
    SQLCHAR state[6];
    SQLINTEGER error;
    SQLCHAR text[SQL_MAX_MESSAGE_LENGTH];
    SQLSMALLINT textLength;
    RETCODE result = SQLGetDiagRec(statementType, handle, 1, state, &error, text, sizeof(text), &textLength);
    if (result == SQL_SUCCESS || result == SQL_SUCCESS_WITH_INFO)
    {
        return std::string((char *)text);
    }
    return "";
}

inline bool odbcSuccess(RETCODE result) { return result == SQL_SUCCESS || result == SQL_SUCCESS_WITH_INFO; }

class OdbcQuery
{
public:
    OdbcQuery(const std::string &query) : m_statement(0), m_result(0), m_query(query), m_bound(false), m_valueLength(0)
    {
    }

    /// A query whose one ? stands for the value, which the driver carries as data rather than as SQL.
    OdbcQuery(const std::string &query, const std::string &value)
        : m_statement(0), m_result(0), m_query(query), m_value(value), m_bound(true), m_valueLength(0)
    {
    }

    ~OdbcQuery() { close(); }

    void close()
    {
        if (m_statement)
        {
            SQLFreeHandle(SQL_HANDLE_STMT, m_statement);
            m_statement = 0;
            m_result = 0;
        }
    }

    bool execute(HDBC connection)
    {
        if (m_statement)
        {
            SQLFreeHandle(SQL_HANDLE_STMT, m_statement);
        }
        SQLAllocHandle(SQL_HANDLE_STMT, connection, &m_statement);
        m_result = SQL_SUCCESS;
        if (m_bound)
        {
            m_valueLength = (SQLLEN)m_value.size();
            m_result = SQLBindParameter(m_statement, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_LONGVARCHAR,
                                        m_value.empty() ? 1 : m_value.size(), 0, (SQLPOINTER)m_value.data(),
                                        m_valueLength, &m_valueLength);
        }
        if (success())
        {
            m_result = SQLExecDirect(m_statement, (SQLCHAR *)m_query.c_str(), (SQLINTEGER)m_query.size());
        }
        if (success() || m_result == SQL_NO_DATA)
        {
            return true;
        }
        m_reason = odbcError(SQL_HANDLE_STMT, m_statement);
        SQLFreeHandle(SQL_HANDLE_STMT, m_statement);
        m_statement = 0;
        return success();
    }

    bool success() { return odbcSuccess(m_result); }

    /*int rows()
    {
      return (int)mysql_num_rows( m_result );
    }*/

    const std::string &reason() { return m_reason; }

    /// Why a query that failed could not be retried, which is more use than why it failed.
    void reason(const std::string &reason) { m_reason = reason; }

    bool fetch() { return odbcSuccess(SQLFetch(m_statement)); }

    /*char* getValue( int row, int column )
    {
      if( m_rows.empty() )
      {
        MYSQL_ROW row = 0;
        while( row = mysql_fetch_row( m_result ) )
          m_rows.push_back(row);
      }
      return m_rows[row][column];
    }*/

    HSTMT statement() { return m_statement; }

    void throwException() EXCEPT(IOException)
    {
        if (!success())
        {
            throw IOException("Query failed [" + m_query + "] " + reason());
        }
    }

private:
    HSTMT m_statement;
    RETCODE m_result;
    std::string m_query;
    std::string m_reason;
    std::string m_value;
    bool m_bound;
    SQLLEN m_valueLength;
};

class OdbcConnection
{
public:
    OdbcConnection(const DatabaseConnectionID &id) : m_environment(0), m_connection(0), m_connectionID(id)
    {
        connect();
    }

    OdbcConnection(const std::string &user, const std::string &password, const std::string &connectionString)
        : m_environment(0), m_connection(0), m_connectionID("", user, password, connectionString, 0)
    {
        connect();
    }

    ~OdbcConnection()
    {
        if (m_connection)
        {
            SQLDisconnect(m_connection);
        }
        release();
    }

    const DatabaseConnectionID &connectionID() { return m_connectionID; }

    bool connected()
    {
        Locker locker(m_mutex);
        return m_connected;
    }

    bool reconnect()
    {
        Locker locker(m_mutex);
        if (m_connection)
        {
            SQLDisconnect(m_connection);
            SQLFreeHandle(SQL_HANDLE_DBC, m_connection);
            m_connection = 0;
        }
        connect();
        return true;
    }

    bool execute(OdbcQuery &pQuery)
    {
        Locker locker(m_mutex);
        if (!pQuery.execute(m_connection))
        {
            // A connection that cannot be remade is a failed query, as on the other backends, and the next one tries
            // again.
            try
            {
                reconnect();
            }
            catch (const ConfigError &e)
            {
                pQuery.reason(e.what());
                return false;
            }
            return pQuery.execute(m_connection);
        }
        return true;
    }

private:
    void release()
    {
        if (m_connection)
        {
            SQLFreeHandle(SQL_HANDLE_DBC, m_connection);
            m_connection = 0;
        }
        if (m_environment)
        {
            SQLFreeHandle(SQL_HANDLE_ENV, m_environment);
            m_environment = 0;
        }
    }

    // Every throw here releases what was allocated: no destructor runs after a constructor throws.
    void connect()
    {
        m_connected = false;

        RETCODE result;
        if (!m_environment)
        {
            result = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &m_environment);
            if (!odbcSuccess(result))
            {
                throw ConfigError("Unable to allocate ODBC environment");
            }

            result = SQLSetEnvAttr(m_environment, SQL_ATTR_ODBC_VERSION, (void *)SQL_OV_ODBC3, 0);
            if (!odbcSuccess(result))
            {
                release();
                throw ConfigError("Unable to find ODBC version 3.0");
            }
        }

        result = SQLAllocHandle(SQL_HANDLE_DBC, m_environment, &m_connection);
        if (!odbcSuccess(result))
        {
            release();
            throw ConfigError("Unable to allocate ODBC connection");
        }

        std::stringstream connectionStream;
        std::string connectionString = m_connectionID.getHost();
        if (!m_connectionID.getHost().contains("UID="))
        {
            connectionStream << "UID=" << m_connectionID.getUser() << ";";
        }
        if (!m_connectionID.getHost().contains("PWD="))
        {
            connectionStream << "PWD=" << m_connectionID.getPassword() << ";";
        }
        connectionStream << m_connectionID.getHost();
        connectionString = connectionStream.str();

        SQLCHAR connectionStringOut[255];

        result = SQLDriverConnect(m_connection, NULL, (SQLCHAR *)connectionString.c_str(), SQL_NTS, connectionStringOut,
                                  255, 0, SQL_DRIVER_NOPROMPT);

        if (!odbcSuccess(result))
        {
            std::string error = odbcError(SQL_HANDLE_DBC, m_connection);
            release();
            throw ConfigError(error);
        }

        m_connected = true;
    }

    HENV m_environment;
    HDBC m_connection;
    bool m_connected;
    DatabaseConnectionID m_connectionID;
    Mutex m_mutex;
};
} // namespace FIX

#endif
#endif
