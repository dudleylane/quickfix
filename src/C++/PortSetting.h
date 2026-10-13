/* -*- C++ -*- */

#ifndef FIX_PORTSETTING_H
#define FIX_PORTSETTING_H

#include "Dictionary.h"
#include "Exceptions.h"
#include "SessionSettings.h"

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <utility>

namespace FIX
{
/// Reads a TCP port setting. Ports used to be read as (short)getInt(), so 70000 became 4464 without
/// a word and everything above 32767 was negative (#99). allowAny admits 0, meaning "any": a
/// listening port of 0 lets the kernel choose, and a source port of 0 leaves it unbound.
inline std::uint16_t getPortSetting(const Dictionary &settings, const std::string &key, bool allowAny = false)
{
    const int port = settings.getInt(key);
    if (port < (allowAny ? 0 : 1) || port > 65535)
    {
        throw ConfigError(key + " must be " + (allowAny ? "0" : "1") + " to 65535, not " + std::to_string(port));
    }
    return static_cast<std::uint16_t>(port);
}

/// Reads SocketNodelay. TCP_NODELAY is on unless a session says N: with Nagle's algorithm on, a small
/// message sent while an earlier one is unacknowledged waits for the peer's delayed ACK, up to ~40 ms
/// on Linux (#150).
inline bool getSocketNoDelay(const Dictionary &settings)
{
    return settings.has(SOCKET_NODELAY) ? settings.getBool(SOCKET_NODELAY) : true;
}

/// Reads SocketSendBufferSize or SocketReceiveBufferSize: 0, the default, leaves the kernel's size.
inline int getSocketBufferSize(const Dictionary &settings, const std::string &key)
{
    return settings.has(key) ? settings.getInt(key) : 0;
}

/// An acceptor sets TCP_NODELAY and the buffer sizes on the listening socket of each SocketAcceptPort,
/// and every connection accepted there inherits them, so the sessions sharing a port cannot differ.
/// Reject a disagreement rather than let the first session on the port decide for the rest without a
/// word (#150, #170).
inline void checkSocketOptionsPerPort(const SessionSettings &sessionSettings)
{
    struct PortOptions
    {
        SessionID first;
        std::string noDelay;
        std::string sendBufferSize;
        std::string receiveBufferSize;
    };
    std::map<std::uint16_t, PortOptions> byPort;
    for (const SessionID &sessionID : sessionSettings.getSessions())
    {
        const Dictionary &settings = sessionSettings.get(sessionID);
        const std::uint16_t port = getPortSetting(settings, SOCKET_ACCEPT_PORT, true);
        const PortOptions options{sessionID, getSocketNoDelay(settings) ? "Y" : "N",
                                  std::to_string(getSocketBufferSize(settings, SOCKET_SEND_BUFFER_SIZE)),
                                  std::to_string(getSocketBufferSize(settings, SOCKET_RECEIVE_BUFFER_SIZE))};
        const auto [it, inserted] = byPort.try_emplace(port, options);
        if (inserted)
        {
            continue;
        }
        const PortOptions &first = it->second;
        for (const auto &[key, mine, theirs] :
             {std::tuple<const char *, const std::string &, const std::string &>{SOCKET_NODELAY, options.noDelay,
                                                                                 first.noDelay},
              {SOCKET_SEND_BUFFER_SIZE, options.sendBufferSize, first.sendBufferSize},
              {SOCKET_RECEIVE_BUFFER_SIZE, options.receiveBufferSize, first.receiveBufferSize}})
        {
            if (mine != theirs)
            {
                throw ConfigError(std::string(key) + " must be the same for every session on port " +
                                  std::to_string(port) + ": " + first.first.toString() + " has " + theirs + ", " +
                                  sessionID.toString() + " has " + mine);
            }
        }
    }
}
} // namespace FIX

#endif // FIX_PORTSETTING_H
