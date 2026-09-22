#include "config.h"

#include "TestHelper.h"

namespace FIX
{
FIX::SessionSettings TestSettings::sessionSettings;
std::string TestSettings::specPath = "";

std::string TestSettings::pathForSpec(const std::string &spec) { return TestSettings::specPath + "/" + spec + ".xml"; }
} // namespace FIX
