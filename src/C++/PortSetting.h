/* -*- C++ -*- */

#ifndef FIX_PORTSETTING_H
#define FIX_PORTSETTING_H

#include "Dictionary.h"
#include "Exceptions.h"

#include <cstdint>
#include <string>

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
} // namespace FIX

#endif // FIX_PORTSETTING_H
