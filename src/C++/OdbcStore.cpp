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

#ifdef HAVE_ODBC

#include "FieldConvertors.h"
#include "OdbcStore.h"
#include "Parser.h"
#include "SessionID.h"
#include "SessionSettings.h"
#include "Utility.h"
#include <fstream>

namespace FIX
{

const std::string OdbcStoreFactory::DEFAULT_USER = "sa";
const std::string OdbcStoreFactory::DEFAULT_PASSWORD = "";
const std::string OdbcStoreFactory::DEFAULT_CONNECTION_STRING = "DATABASE=quickfix;DRIVER={SQL Server};SERVER=(local);";

namespace
{
std::string sessionIs(const SessionID &sessionID)
{
    return "beginstring=" + odbcQuote(sessionID.getBeginString().getValue()) +
           " and sendercompid=" + odbcQuote(sessionID.getSenderCompID().getValue()) +
           " and targetcompid=" + odbcQuote(sessionID.getTargetCompID().getValue()) +
           " and session_qualifier=" + odbcQuote(sessionID.getSessionQualifier());
}

std::string sessionValues(const SessionID &sessionID)
{
    return odbcQuote(sessionID.getBeginString().getValue()) + "," + odbcQuote(sessionID.getSenderCompID().getValue()) +
           "," + odbcQuote(sessionID.getTargetCompID().getValue()) + "," + odbcQuote(sessionID.getSessionQualifier());
}
} // namespace

OdbcStore::OdbcStore(const UtcTimeStamp &now, const SessionID &sessionID, const std::string &user,
                     const std::string &password, const std::string &connectionString)
    : m_cache(now), m_sessionID(sessionID), m_messageType(SQL_LONGVARCHAR)
{
    m_pConnection = new OdbcConnection(user, password, connectionString);
    try
    {
        populateCache();
    }
    catch (...)
    {
        // No destructor runs after a constructor throws.
        delete m_pConnection;
        throw;
    }
}

OdbcStore::~OdbcStore() { delete m_pConnection; }

void OdbcStore::populateCache()
{
    // The column decides how a message goes and comes back, and a wrong guess is not refused: through psqlODBC, a
    // character binding reads a binary column as hex, and a binary one stores hex in a text column. Only a binary
    // column holds every byte, and a text one, as on SQL Server's schema, keeps the binding it always had.
    OdbcQuery column("SELECT message FROM messages WHERE 1=0");
    SQLSMALLINT type, digits, nullable;
    SQLULEN size;
    if (!m_pConnection->execute(column) ||
        !odbcSuccess(SQLDescribeCol(column.statement(), 1, nullptr, 0, nullptr, &type, &size, &digits, &nullable)))
    {
        throw ConfigError("Unable to query messages table");
    }
    m_messageType = odbcIsBinary(type) ? type : SQL_LONGVARCHAR;
    column.close();

    std::stringstream queryString;

    queryString << "SELECT creation_time, incoming_seqnum, outgoing_seqnum FROM sessions WHERE "
                << sessionIs(m_sessionID);

    OdbcQuery query(queryString.str());

    if (!m_pConnection->execute(query))
    {
        throw ConfigError("Unable to query sessions table");
    }

    int rows = 0;
    while (query.fetch())
    {
        rows++;
        if (rows > 1)
        {
            throw ConfigError("Multiple entries found for session in database");
        }

        SQL_TIMESTAMP_STRUCT creationTime;
        SQLLEN creationTimeLength;
        SQLGetData(query.statement(), 1, SQL_C_TYPE_TIMESTAMP, &creationTime, 0, &creationTimeLength);
        SQLUBIGINT incomingSeqNum;
        SQLLEN incomingSeqNumLength;
        SQLGetData(query.statement(), 2, SQL_C_UBIGINT, &incomingSeqNum, 0, &incomingSeqNumLength);

        SQLUBIGINT outgoingSeqNum;
        SQLLEN outgoingSeqNumLength;
        SQLGetData(query.statement(), 3, SQL_C_UBIGINT, &outgoingSeqNum, 0, &outgoingSeqNumLength);

        UtcTimeStamp time = UtcTimeStamp::now();
        time.setYMD(creationTime.year, creationTime.month, creationTime.day);
        time.setHMS(creationTime.hour, creationTime.minute, creationTime.second, creationTime.fraction);
        m_cache.setCreationTime(time);
        m_cache.setNextTargetMsgSeqNum(incomingSeqNum);
        m_cache.setNextSenderMsgSeqNum(outgoingSeqNum);
    }
    query.close();

    if (rows == 0)
    {
        UtcTimeStamp time = m_cache.getCreationTime();
        char sqlTime[100];
        int year, month, day, hour, minute, second, millis;
        time.getYMD(year, month, day);
        time.getHMS(hour, minute, second, millis);
        STRING_SPRINTF(sqlTime, "%d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
        std::stringstream queryString2;
        queryString2 << "INSERT INTO sessions (beginstring, sendercompid, targetcompid, session_qualifier,"
                     << "creation_time, incoming_seqnum, outgoing_seqnum) VALUES(" << sessionValues(m_sessionID) << ","
                     << "{ts '" << sqlTime << "'}," << m_cache.getNextTargetMsgSeqNum() << ","
                     << m_cache.getNextSenderMsgSeqNum() << ")";

        OdbcQuery query2(queryString2.str());
        if (!m_pConnection->execute(query2))
        {
            throw ConfigError("Unable to create session in database");
        }
    }
}

MessageStore *OdbcStoreFactory::create(const UtcTimeStamp &now, const SessionID &sessionID)
{
    if (m_useSettings)
    {
        return create(now, sessionID, m_settings.get(sessionID));
    }
    else if (m_useDictionary)
    {
        return create(now, sessionID, m_dictionary);
    }
    else
    {
        return new OdbcStore(now, sessionID, m_user, m_password, m_connectionString);
    }
}

void OdbcStoreFactory::destroy(MessageStore *pStore) { delete pStore; }

MessageStore *OdbcStoreFactory::create(const UtcTimeStamp &now, const SessionID &sessionID, const Dictionary &settings)
{
    std::string user = DEFAULT_USER;
    std::string password = DEFAULT_PASSWORD;
    std::string connectionString = DEFAULT_CONNECTION_STRING;

    try
    {
        user = settings.getString(ODBC_STORE_USER);
    }
    catch (ConfigError &)
    {
    }

    try
    {
        password = settings.getString(ODBC_STORE_PASSWORD);
    }
    catch (ConfigError &)
    {
    }

    try
    {
        connectionString = settings.getString(ODBC_STORE_CONNECTION_STRING);
    }
    catch (ConfigError &)
    {
    }

    return new OdbcStore(now, sessionID, user, password, connectionString);
}

bool OdbcStore::set(SEQNUM msgSeqNum, const std::string &msg) EXCEPT(IOException)
{
    std::stringstream queryString;
    queryString << "INSERT INTO messages "
                << "(beginstring, sendercompid, targetcompid, session_qualifier, msgseqnum, message) "
                << "VALUES (" << sessionValues(m_sessionID) << "," << msgSeqNum << ",?)";

    OdbcQuery query(queryString.str(), msg, m_messageType);
    if (!m_pConnection->execute(query))
    {
        std::stringstream queryString2;
        queryString2 << "UPDATE messages SET message=? WHERE " << sessionIs(m_sessionID) << " and "
                     << "msgseqnum=" << msgSeqNum;
        OdbcQuery query2(queryString2.str(), msg, m_messageType);
        if (!m_pConnection->execute(query2))
        {
            query2.throwException();
        }

        // ODBC has no upsert its servers share. The INSERT was refusing a duplicate only if the UPDATE found one;
        // otherwise its failure is the message's, and reporting success would turn a resend into a gap fill.
        SQLLEN updated = 0;
        SQLRowCount(query2.statement(), &updated);
        if (updated == 0)
        {
            query.throwException();
        }
    }

    // In a text column a NUL survives on some servers and is cut on others, PostgreSQL's TEXT for one, and what is
    // stored must be what a resend would send. A message holding one, which few do, pays for a read.
    if (!odbcIsBinary(m_messageType) && msg.find('\0') != std::string::npos)
    {
        std::vector<std::string> stored;
        get(msgSeqNum, msgSeqNum, stored);
        if (stored.size() != 1 || stored[0] != msg)
        {
            std::stringstream removal;
            removal << "DELETE FROM messages WHERE " << sessionIs(m_sessionID) << " and msgseqnum=" << msgSeqNum;
            OdbcQuery query3(removal.str());
            m_pConnection->execute(query3);
            throw IOException("Message " + std::to_string(msgSeqNum) + " holds a NUL, which the server did not keep");
        }
    }
    return true;
}

void OdbcStore::get(SEQNUM begin, SEQNUM end, std::vector<std::string> &result) const EXCEPT(IOException)
{
    result.clear();
    std::stringstream queryString;
    queryString << "SELECT message FROM messages WHERE " << sessionIs(m_sessionID) << " and "
                << "msgseqnum>=" << begin << " and " << "msgseqnum<=" << end << " "
                << "ORDER BY msgseqnum";

    OdbcQuery query(queryString.str());

    if (!m_pConnection->execute(query))
    {
        query.throwException();
    }

    const bool binary = odbcIsBinary(m_messageType);
    while (query.fetch())
    {
        std::string message;
        char messageBuffer[4096];
        SQLLEN messageLength;
        // A character read keeps a byte for the terminator, and a binary read has none.
        const SQLLEN room = binary ? sizeof(messageBuffer) : sizeof(messageBuffer) - 1;

        // messageLength is what remained before this read, not what the read returned: it can exceed the room, or be
        // SQL_NO_TOTAL. A read that does not finish the value fills the room.
        while (odbcSuccess(SQLGetData(query.statement(), 1, binary ? SQL_C_BINARY : SQL_C_CHAR, messageBuffer,
                                      sizeof(messageBuffer), &messageLength)))
        {
            if (messageLength == SQL_NULL_DATA)
            {
                break;
            }
            const bool more = messageLength == SQL_NO_TOTAL || messageLength > room;
            message.append(messageBuffer, more ? room : messageLength);
        }

        result.push_back(message);
    }
}

SEQNUM OdbcStore::getNextSenderMsgSeqNum() const EXCEPT(IOException) { return m_cache.getNextSenderMsgSeqNum(); }

SEQNUM OdbcStore::getNextTargetMsgSeqNum() const EXCEPT(IOException) { return m_cache.getNextTargetMsgSeqNum(); }

void OdbcStore::setNextSenderMsgSeqNum(SEQNUM value) EXCEPT(IOException)
{
    std::stringstream queryString;
    queryString << "UPDATE sessions SET outgoing_seqnum=" << value << " WHERE " << sessionIs(m_sessionID);
    OdbcQuery query(queryString.str());
    if (!m_pConnection->execute(query))
    {
        query.throwException();
    }
    m_cache.setNextSenderMsgSeqNum(value);
}

void OdbcStore::setNextTargetMsgSeqNum(SEQNUM value) EXCEPT(IOException)
{
    std::stringstream queryString;
    queryString << "UPDATE sessions SET incoming_seqnum=" << value << " WHERE " << sessionIs(m_sessionID);

    OdbcQuery query(queryString.str());
    if (!m_pConnection->execute(query))
    {
        query.throwException();
    }

    m_cache.setNextTargetMsgSeqNum(value);
}

void OdbcStore::incrNextSenderMsgSeqNum() EXCEPT(IOException)
{
    m_cache.incrNextSenderMsgSeqNum();
    setNextSenderMsgSeqNum(m_cache.getNextSenderMsgSeqNum());
}

void OdbcStore::incrNextTargetMsgSeqNum() EXCEPT(IOException)
{
    m_cache.incrNextTargetMsgSeqNum();
    setNextTargetMsgSeqNum(m_cache.getNextTargetMsgSeqNum());
}

UtcTimeStamp OdbcStore::getCreationTime() const EXCEPT(IOException) { return m_cache.getCreationTime(); }

void OdbcStore::reset(const UtcTimeStamp &now) EXCEPT(IOException)
{
    std::stringstream queryString;
    queryString << "DELETE FROM messages WHERE " << sessionIs(m_sessionID);

    OdbcQuery query(queryString.str());
    if (!m_pConnection->execute(query))
    {
        query.throwException();
    }
    query.close();

    m_cache.reset(now);
    UtcTimeStamp time = m_cache.getCreationTime();

    int year, month, day, hour, minute, second, millis;
    time.getYMD(year, month, day);
    time.getHMS(hour, minute, second, millis);

    char sqlTime[100];
    STRING_SPRINTF(sqlTime, "%d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);

    std::stringstream queryString2;
    queryString2 << "UPDATE sessions SET creation_time={ts '" << sqlTime << "'}, "
                 << "incoming_seqnum=" << m_cache.getNextTargetMsgSeqNum() << ", "
                 << "outgoing_seqnum=" << m_cache.getNextSenderMsgSeqNum() << " WHERE " << sessionIs(m_sessionID);

    OdbcQuery query2(queryString2.str());
    if (!m_pConnection->execute(query2))
    {
        query2.throwException();
    }
}

void OdbcStore::refresh() EXCEPT(IOException)
{
    m_cache.reset(UtcTimeStamp::now());
    populateCache();
}

} // namespace FIX

#endif
