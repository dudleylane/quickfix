#include "catch_amalgamated.hpp"

#include <quickfix/QuickFIXVersion.h>

#include <string>

// QuickFIXVersion.h is generated from project() in the top-level CMakeLists.txt, which also sets the
// library's soname, and lives only in include/quickfix/. Including it as <quickfix/...> goes through
// the quickfix target's INTERFACE include directory, as an in-tree consumer's include would; the
// expected values come from the same project() call, as compile definitions on ut.

TEST_CASE("VersionTestCase")
{
    SECTION("headerMatchesProjectVersion")
    {
        CHECK(QUICKFIX_VERSION_MAJOR == QUICKFIX_EXPECTED_VERSION_MAJOR);
        CHECK(QUICKFIX_VERSION_MINOR == QUICKFIX_EXPECTED_VERSION_MINOR);
        CHECK(QUICKFIX_VERSION_PATCH == QUICKFIX_EXPECTED_VERSION_PATCH);
        CHECK(std::string(QUICKFIX_VERSION) == std::to_string(QUICKFIX_EXPECTED_VERSION_MAJOR) + "." +
                                                   std::to_string(QUICKFIX_EXPECTED_VERSION_MINOR) + "." +
                                                   std::to_string(QUICKFIX_EXPECTED_VERSION_PATCH));
    }
}
