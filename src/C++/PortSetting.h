/* -*- C++ -*- */

#ifndef FIX_PORTSETTING_H
#define FIX_PORTSETTING_H

#include "Dictionary.h"
#include "Exceptions.h"
#include "SessionSettings.h"

#include <cstdint>
#include <map>
#include <string>
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

/// An acceptor sets TCP_NODELAY on the listening socket of each SocketAcceptPort, and every connection
/// accepted there inherits it, so the sessions sharing a port cannot differ. Reject a disagreement
/// rather than let the first session on the port decide for the rest without a word (#150).
inline void checkSocketNoDelayPerPort(const SessionSettings &sessionSettings)
{
    std::map<std::uint16_t, std::pair<bool, SessionID>> byPort;
    for (const SessionID &sessionID : sessionSettings.getSessions())
    {
        const Dictionary &settings = sessionSettings.get(sessionID);
        const std::uint16_t port = getPortSetting(settings, SOCKET_ACCEPT_PORT, true);
        const bool noDelay = getSocketNoDelay(settings);
        const auto [it, inserted] = byPort.try_emplace(port, noDelay, sessionID);
        if (!inserted && it->second.first != noDelay)
        {
            throw ConfigError(std::string(SOCKET_NODELAY) + " must be the same for every session on port " +
                              std::to_string(port) + ": " + it->second.second.toString() + " has " +
                              (it->second.first ? "Y" : "N") + ", " + sessionID.toString() + " has " +
                              (noDelay ? "Y" : "N"));
        }
    }
}
} // namespace FIX

#endif // FIX_PORTSETTING_H
