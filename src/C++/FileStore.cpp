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

#include "FileStore.h"
#include "Parser.h"
#include "SessionID.h"
#include "Utility.h"
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <fstream>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _MSC_VER
#define FILE_SEEK _fseeki64
#define FILE_TELL _ftelli64
#else
#define FILE_SEEK fseeko
#define FILE_TELL ftello
#endif

namespace
{
auto const seqNumFileFormat = "%" + std::to_string(std::numeric_limits<uint64_t>::digits10 + 1) + "." +
                              std::to_string(std::numeric_limits<uint64_t>::digits10 + 1) + SCNu64;

auto const seqNumPairFileFormat = (seqNumFileFormat + " : " + seqNumFileFormat);

auto constexpr sizeOf64BitSeqNumFile = 43;

void writeAll(int fd, const char *data, std::size_t size, const std::string &fileName)
{
    while (size > 0)
    {
        const ssize_t written = ::write(fd, data, size);
        if (written < 0 && errno == EINTR)
        {
            continue;
        }
        if (written <= 0)
        {
            throw FIX::IOException("Unable to write to file " + fileName);
        }
        data += written;
        size -= static_cast<std::size_t>(written);
    }
}

// Writes to the body and header files go to the end whatever the stream's read position.
void appendOnly(FILE *file)
{
    const int fd = fileno(file);
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_APPEND);
}
} // namespace
namespace FIX
{
FileStore::FileStore(const UtcTimeStamp &now, std::string path, const SessionID &sessionID)
    : m_cache(now), m_msgFile(0), m_headerFile(0), m_seqNumsFile(0), m_sessionFile(0)
{
    file_mkdir(path.c_str());

    if (path.empty())
    {
        path = ".";
    }
    const std::string &begin = sessionID.getBeginString().getString();
    const std::string &sender = sessionID.getSenderCompID().getString();
    const std::string &target = sessionID.getTargetCompID().getString();
    const std::string &qualifier = sessionID.getSessionQualifier();

    std::string sessionid = begin + "-" + sender + "-" + target;
    if (qualifier.size())
    {
        sessionid += "-" + qualifier;
    }

    std::string prefix = file_appendpath(path, sessionid + ".");

    m_msgFileName = prefix + "body";
    m_headerFileName = prefix + "header";
    m_seqNumsFileName = prefix + "seqnums";
    m_sessionFileName = prefix + "session";

    try
    {
        open(false);
    }
    catch (IOException &e)
    {
        throw ConfigError(e.what());
    }
}

FileStore::~FileStore()
{
    if (m_msgFile)
    {
        fclose(m_msgFile);
    }
    if (m_headerFile)
    {
        fclose(m_headerFile);
    }
    if (m_seqNumsFile)
    {
        fclose(m_seqNumsFile);
    }
    if (m_sessionFile)
    {
        fclose(m_sessionFile);
    }
}

void FileStore::open(bool deleteFile)
{
    if (m_msgFile)
    {
        fclose(m_msgFile);
    }
    if (m_headerFile)
    {
        fclose(m_headerFile);
    }
    if (m_seqNumsFile)
    {
        fclose(m_seqNumsFile);
    }
    if (m_sessionFile)
    {
        fclose(m_sessionFile);
    }

    m_msgFile = 0;
    m_headerFile = 0;
    m_seqNumsFile = 0;
    m_sessionFile = 0;

    if (deleteFile)
    {
        file_unlink(m_msgFileName.c_str());
        file_unlink(m_headerFileName.c_str());
        file_unlink(m_seqNumsFileName.c_str());
        file_unlink(m_sessionFileName.c_str());
    }

    populateCache();
    m_msgFile = file_fopen(m_msgFileName.c_str(), "r+");
    if (!m_msgFile)
    {
        m_msgFile = file_fopen(m_msgFileName.c_str(), "w+");
    }
    if (!m_msgFile)
    {
        throw ConfigError("Could not open body file: " + m_msgFileName + " " + error_strerror());
    }

    m_headerFile = file_fopen(m_headerFileName.c_str(), "r+");
    if (!m_headerFile)
    {
        m_headerFile = file_fopen(m_headerFileName.c_str(), "w+");
    }
    if (!m_headerFile)
    {
        throw ConfigError("Could not open header file: " + m_headerFileName + " " + error_strerror());
    }
    appendOnly(m_msgFile);
    appendOnly(m_headerFile);

    m_seqNumsFile = file_fopen(m_seqNumsFileName.c_str(), "r+");
    if (!m_seqNumsFile)
    {
        m_seqNumsFile = file_fopen(m_seqNumsFileName.c_str(), "w+");
    }
    if (!m_seqNumsFile)
    {
        throw ConfigError("Could not open seqnums file: " + m_seqNumsFileName + " " + error_strerror());
    }

    bool setCreationTime = false;
    m_sessionFile = file_fopen(m_sessionFileName.c_str(), "r");
    if (!m_sessionFile)
    {
        setCreationTime = true;
    }
    else
    {
        fclose(m_sessionFile);
    }

    m_sessionFile = file_fopen(m_sessionFileName.c_str(), "r+");
    if (!m_sessionFile)
    {
        m_sessionFile = file_fopen(m_sessionFileName.c_str(), "w+");
    }
    if (!m_sessionFile)
    {
        throw ConfigError("Could not open session file " + error_strerror());
    }
    if (setCreationTime)
    {
        setSession();
    }

    setNextSenderMsgSeqNum(getNextSenderMsgSeqNum());
    setNextTargetMsgSeqNum(getNextTargetMsgSeqNum());
}

void FileStore::populateCache()
{
    FILE *headerFile = file_fopen(m_headerFileName.c_str(), "r+");
    if (headerFile)
    {
        SEQNUM msgSeqNum;
        int64_t offset;
        std::size_t size;

        while (FILE_FSCANF(headerFile, "%" SCNu64 ",%" SCNi64 ",%zu ", &msgSeqNum, &offset, &size) == 3)
        {
            std::pair<NumToOffset::iterator, bool> it =
                m_offsets.insert(NumToOffset::value_type(msgSeqNum, std::make_pair(offset, size)));

            if (it.second == false)
            {
                it.first->second = std::make_pair(offset, size);
            }
        }
        fclose(headerFile);
    }

    struct stat seqNumsFileStat;
    FILE *seqNumsFile = file_fopen(m_seqNumsFileName.c_str(), "r+");

    if (seqNumsFile && stat(m_seqNumsFileName.c_str(), &seqNumsFileStat) == 0)
    {
        if (seqNumsFileStat.st_size == sizeOf64BitSeqNumFile)
        {
            SEQNUM sender, target;
            if (FILE_FSCANF(seqNumsFile, "%" SCNu64 " : %" SCNu64, &sender, &target) == 2)
            {
                m_cache.setNextSenderMsgSeqNum(sender);
                m_cache.setNextTargetMsgSeqNum(target);
            }
        }
        else // try old int seq num file format
        {
            int sender, target;
            if (FILE_FSCANF(seqNumsFile, "%d : %d", &sender, &target) == 2)
            {
                m_cache.setNextSenderMsgSeqNum(sender);
                m_cache.setNextTargetMsgSeqNum(target);
            }
        }
        fclose(seqNumsFile);
    }

    FILE *sessionFile = file_fopen(m_sessionFileName.c_str(), "r+");
    if (sessionFile)
    {
        // setSession writes 17 characters; 31 still fits a nanosecond timestamp, so a
        // longer token is cut short and fails to convert. The read was unbounded into
        // 22 bytes, and a corrupted file overflowed the stack (#126).
        char time[32];
#ifdef HAVE_FSCANF_S
        int result = FILE_FSCANF(sessionFile, "%31s", time, 32);
#else
        int result = FILE_FSCANF(sessionFile, "%31s", time);
#endif
        fclose(sessionFile);
        if (result == 1)
        {
            try
            {
                m_cache.setCreationTime(UtcTimeStampConvertor::convert(time));
            }
            catch (FieldConvertError &)
            {
                // The creation time decides session resets, so fail rather than guess.
                throw IOException("Unreadable creation time in " + m_sessionFileName + ": " + time);
            }
        }
    }
}

MessageStore *FileStoreFactory::create(const UtcTimeStamp &now, const SessionID &sessionID)
{
    if (m_path.size())
    {
        return new FileStore(now, m_path, sessionID);
    }

    std::string path;
    Dictionary settings = m_settings.get(sessionID);
    path = settings.getString(FILE_STORE_PATH);
    if (settings.has(FILE_STORE_SYNC) && settings.getBool(FILE_STORE_SYNC))
    {
        return new SyncFileStore(now, path, sessionID);
    }
    return new FileStore(now, path, sessionID);
}

void FileStoreFactory::destroy(MessageStore *pStore) { delete pStore; }

bool FileStore::set(SEQNUM msgSeqNum, const std::string &msg) EXCEPT(IOException)
{
    // The body and header files are written through their descriptors, in append mode, and the stdio
    // streams are kept for reading. Seeking both streams to the end, telling, printing and flushing each
    // took about five syscalls per message; this takes three (#151).
    const off_t offset = ::lseek(fileno(m_msgFile), 0, SEEK_END);
    if (offset < 0)
    {
        throw IOException("Cannot seek to end of " + m_msgFileName);
    }
    writeAll(fileno(m_msgFile), msg.data(), msg.size(), m_msgFileName);

    char header[96];
    const int length = snprintf(header, sizeof(header), "%" SCNu64 ",%" PRId64 ",%zu ", msgSeqNum,
                                static_cast<int64_t>(offset), msg.size());
    writeAll(fileno(m_headerFile), header, static_cast<std::size_t>(length), m_headerFileName);

    std::pair<NumToOffset::iterator, bool> it =
        m_offsets.insert(NumToOffset::value_type(msgSeqNum, std::make_pair(offset, msg.size())));
    if (it.second == false)
    {
        it.first->second = std::make_pair(offset, msg.size());
    }
    return true;
}

void FileStore::get(SEQNUM begin, SEQNUM end, std::vector<std::string> &result) const EXCEPT(IOException)
{
    result.clear();
    std::string msg;
    for (auto i = begin; i <= end && i != 0; ++i)
    {
        if (get(i, msg))
        {
            result.push_back(msg);
        }
    }
}

SEQNUM FileStore::getNextSenderMsgSeqNum() const EXCEPT(IOException) { return m_cache.getNextSenderMsgSeqNum(); }

SEQNUM FileStore::getNextTargetMsgSeqNum() const EXCEPT(IOException) { return m_cache.getNextTargetMsgSeqNum(); }

void FileStore::setNextSenderMsgSeqNum(SEQNUM value) EXCEPT(IOException)
{
    m_cache.setNextSenderMsgSeqNum(value);
    setSeqNum();
}

void FileStore::setNextTargetMsgSeqNum(SEQNUM value) EXCEPT(IOException)
{
    m_cache.setNextTargetMsgSeqNum(value);
    setSeqNum();
}

void FileStore::incrNextSenderMsgSeqNum() EXCEPT(IOException)
{
    m_cache.incrNextSenderMsgSeqNum();
    setSeqNum();
}

void FileStore::incrNextTargetMsgSeqNum() EXCEPT(IOException)
{
    m_cache.incrNextTargetMsgSeqNum();
    setSeqNum();
}

UtcTimeStamp FileStore::getCreationTime() const EXCEPT(IOException) { return m_cache.getCreationTime(); }

void FileStore::reset(const UtcTimeStamp &now) EXCEPT(IOException)
{
    try
    {
        m_cache.reset(now);
        m_offsets.clear();
        open(true);
        setSession();
    }
    catch (std::exception &e)
    {
        throw IOException(e.what());
    }
}

void FileStore::refresh() EXCEPT(IOException)
{
    try
    {
        m_cache.reset(UtcTimeStamp::now());
        m_offsets.clear();
        open(false);
    }
    catch (std::exception &e)
    {
        throw IOException(e.what());
    }
}

void FileStore::setSeqNum()
{
    // Fixed width, so one positioned write replaces the record in place (#151).
    char record[64];
    const int length = snprintf(record, sizeof(record), seqNumPairFileFormat.c_str(), getNextSenderMsgSeqNum(),
                                getNextTargetMsgSeqNum());
    if (::pwrite(fileno(m_seqNumsFile), record, static_cast<std::size_t>(length), 0) != length)
    {
        throw IOException("Unable to write to file " + m_seqNumsFileName);
    }
}

void FileStore::setSession()
{
    rewind(m_sessionFile);
    fprintf(m_sessionFile, "%s", UtcTimeStampConvertor::convert(m_cache.getCreationTime()).c_str());
    if (ferror(m_sessionFile))
    {
        throw IOException("Unable to write to file " + m_sessionFileName);
    }
    if (fflush(m_sessionFile))
    {
        throw IOException("Unable to flush file " + m_sessionFileName);
    }
}

bool FileStore::get(SEQNUM msgSeqNum, std::string &msg) const EXCEPT(IOException)
{
    NumToOffset::const_iterator find = m_offsets.find(msgSeqNum);
    if (find == m_offsets.end())
    {
        return false;
    }
    const OffsetSize &offset = find->second;
    if (FILE_SEEK(m_msgFile, offset.first, SEEK_SET))
    {
        throw IOException("Unable to seek in file " + m_msgFileName);
    }
    char *buffer = new char[offset.second + 1];
    size_t result = fread(buffer, sizeof(char), offset.second, m_msgFile);
    if (ferror(m_msgFile) || result != (size_t)offset.second)
    {
        delete[] buffer;
        throw IOException("Unable to read from file " + m_msgFileName);
    }
    buffer[offset.second] = 0;
    msg = buffer;
    delete[] buffer;
    return true;
}

void FileStore::syncToDisk(bool messages, bool seqNums) EXCEPT(IOException)
{
    if (messages && (::fdatasync(fileno(m_msgFile)) != 0 || ::fdatasync(fileno(m_headerFile)) != 0))
    {
        throw IOException("Unable to sync " + m_msgFileName + " or " + m_headerFileName + " to disk");
    }
    if (seqNums && ::fdatasync(fileno(m_seqNumsFile)) != 0)
    {
        throw IOException("Unable to sync " + m_seqNumsFileName + " to disk");
    }
}

bool SyncFileStore::set(SEQNUM msgSeqNum, const std::string &msg) EXCEPT(IOException)
{
    const bool result = FileStore::set(msgSeqNum, msg);
    syncToDisk(true, false);
    return result;
}

void SyncFileStore::setNextSenderMsgSeqNum(SEQNUM value) EXCEPT(IOException)
{
    FileStore::setNextSenderMsgSeqNum(value);
    syncToDisk(false, true);
}

void SyncFileStore::setNextTargetMsgSeqNum(SEQNUM value) EXCEPT(IOException)
{
    FileStore::setNextTargetMsgSeqNum(value);
    syncToDisk(false, true);
}

void SyncFileStore::incrNextSenderMsgSeqNum() EXCEPT(IOException)
{
    FileStore::incrNextSenderMsgSeqNum();
    syncToDisk(false, true);
}

void SyncFileStore::incrNextTargetMsgSeqNum() EXCEPT(IOException)
{
    FileStore::incrNextTargetMsgSeqNum();
    syncToDisk(false, true);
}

void SyncFileStore::reset(const UtcTimeStamp &now) EXCEPT(IOException)
{
    FileStore::reset(now);
    syncToDisk(true, true);
}
} // namespace FIX
