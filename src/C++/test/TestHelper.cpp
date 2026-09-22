#include "config.h"

#include "TestHelper.h"

namespace FIX
{
const uint16_t TestSettings::port = 54321;
FIX::SessionSettings TestSettings::sessionSettings;
std::string TestSettings::specPath = "";

std::string TestSettings::pathForSpec(const std::string &spec) { return TestSettings::specPath + "/" + spec + ".xml"; }
} // namespace FIX
