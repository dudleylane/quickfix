# Contributing to QuickFIX

Thank you for your interest in contributing to QuickFIX! This document provides guidelines and information for contributors.

## Table of Contents

- [How to Contribute](#how-to-contribute)
- [Reporting Bugs](#reporting-bugs)
- [Suggesting Features](#suggesting-features)
- [Pull Requests](#pull-requests)
- [Coding Standards](#coding-standards)
- [Testing](#testing)
- [Documentation](#documentation)
- [Development Workflow](#development-workflow)
- [Platform Notes](#platform-notes)

## How to Contribute

### Reporting Bugs

Before creating a bug report:
1. Check the [existing issues](https://github.com/dudleylane/quickfix/issues) to avoid duplicates
2. Collect relevant information about your environment

When creating a bug report, include:
- **Title**: Clear and descriptive title
- **Description**: Detailed description of the issue
- **Steps to Reproduce**: Step-by-step instructions
- **Expected Behavior**: What you expected to happen
- **Actual Behavior**: What actually happened
- **Environment**:
  - QuickFIX version
  - Operating system and version
  - Compiler and version
  - FIX version being used
  - Any relevant configuration
- **Code Sample**: Minimal code to reproduce the issue
- **Logs**: Relevant log output (use code blocks)

### Suggesting Features

Feature requests are welcome! When suggesting a feature:
1. Check existing issues and discussions first
2. Provide a clear use case
3. Explain why this feature would benefit QuickFIX users
4. Consider backward compatibility

### Pull Requests

We actively welcome pull requests!

#### Before Submitting

1. **Fork and Clone**: Fork the repository and clone your fork
   ```bash
   git clone https://github.com/YOUR-USERNAME/quickfix.git
   cd quickfix
   git remote add upstream https://github.com/dudleylane/quickfix.git
   ```

2. **Create a Branch**: Create a feature branch from `master`
   ```bash
   git checkout -b feature/your-feature-name
   ```

3. **Make Changes**: Implement your changes following our [coding standards](#coding-standards)

4. **Test**: Build, then run the suites from `test/` (see [Testing](#testing))
   ```bash
   cmake --build build -j"$(nproc)"
   cd test
   ./ut --quickfix-config-file cfg/ut.cfg --quickfix-spec-path ../spec
   ./runat.sh 54321
   ```

5. **Commit**: Single-line, imperative, no prefix or scope tag — match the existing history
   ```bash
   git commit -m "Bound Parser input at MAX_MESSAGE_SIZE"
   ```

6. **Push**: Push to your fork
   ```bash
   git push origin feature/your-feature-name
   ```

7. **Open a PR**: Target `dudleylane/quickfix:master`. Changes that are not specific to this
   fork's hardening work belong upstream at
   [`quickfix/quickfix`](https://github.com/quickfix/quickfix) instead.

#### Pull Request Guidelines

- **Title**: Clear, descriptive title
- **Description**:
  - What changes were made
  - Why the changes were necessary
  - Reference any related issues (e.g., "Fixes #123")
- **Scope**: Keep PRs focused on a single feature or bug fix
- **Tests**: Include tests for new functionality
- **Documentation**: Update documentation for API changes
- **Format**: Ensure code is properly formatted (run clang-format)
- **Backward Compatibility**: Avoid breaking changes when possible

## Coding Standards

### C++ Style

`.clang-format` is LLVM-based with BSD/Allman braces, 4-space indent, and a 120-column limit.
Format your changes before submitting:

```bash
git diff --name-only | grep -E '\.(cpp|h)$' | xargs clang-format-22 -i
```

CI (`.github/workflows/format.yml`) re-checks the whole tracked tree, so an unformatted file
anywhere fails the build:

```bash
git ls-files \
  | grep -E '\.(cpp|cc|cxx|h|hpp|hh|hxx|ixx|c)$' \
  | grep -vE '(^|/)(catch_amalgamated|pugixml|scope_guard)\.[^/]+$' \
  | tr '\n' '\0' | xargs -0 -r clang-format-22 --dry-run -Werror
```

Vendored sources (`pugixml`, `scope_guard`, `catch_amalgamated`, `src/C++/double-conversion/`) are
excluded — leave them formatted as upstream ships them. Two tree-wide reformats are recorded in
`.git-blame-ignore-revs`; run `git config blame.ignoreRevsFile .git-blame-ignore-revs` once to keep
`git blame` readable.

### General Guidelines

- **C++ Standard**: Minimum C++23
- **Naming Conventions**:
  - Classes: `PascalCase` (e.g., `SocketInitiator`)
  - Functions/Methods: `camelCase` (e.g., `sendMessage()`)
  - Member variables: `m_camelCase` (e.g., `m_sessionID`)
  - Constants: `UPPER_CASE` (e.g., `MAX_BUFFER_SIZE`)
- **Headers**: Use `#ifndef FIX_<NAME>_H` / `#define` / `#endif` include guards, the convention
  throughout `src/C++/` — every header there uses them
- **Includes**: Order includes as:
  1. Corresponding header (for .cpp files)
  2. C++ standard library
  3. Third-party libraries
  4. QuickFIX headers
- **Comments**:
  - Use Doxygen-style comments for public APIs
  - Explain "why" not "what" in implementation comments
- **Error Handling**: Use exceptions appropriately
- **Memory Management**: Use RAII, smart pointers when appropriate

### Example

```cpp
#ifndef FIX_SESSION_H
#define FIX_SESSION_H

#include <memory>
#include <string>

namespace FIX
{

/// @brief Represents a FIX session
class Session
{
public:
    /// @brief Constructor
    /// @param sessionID The session identifier
    explicit Session(const SessionID& sessionID);

    /// @brief Send a message
    /// @param message The message to send
    /// @return true if sent successfully
    bool sendMessage(const Message& message);

private:
    SessionID m_sessionID;
    std::unique_ptr<MessageStore> m_store;
};

} // namespace FIX

#endif // FIX_SESSION_H
```

## Testing

There is no CTest integration — no `test` target exists. Build first, then run the three suites
from the `test/` directory, where the build leaves `ut`, `at`, and `pt` symlinks.

### Running Tests

```bash
cd test

# Unit tests (Catch2). Both flags are required: they populate FIX::TestSettings
# before Catch2 runs, and the store/dictionary cases fail without them.
./ut --quickfix-config-file cfg/ut.cfg --quickfix-spec-path ../spec

# A single test case, or a single SECTION within one
./ut "SessionTestCase" --quickfix-config-file cfg/ut.cfg --quickfix-spec-path ../spec
./ut "SessionTestCase" -c "lookupSession" --quickfix-config-file cfg/ut.cfg --quickfix-spec-path ../spec
./ut --list-tests

# Acceptance tests: 470 definitions, ~7.5 min, needs ruby. Regenerates cfg/at.cfg,
# starts `at`, and drives it with the Ruby reflector.
./runat.sh 54321

# Performance benchmarks (meaningful only against a Release build)
./pt -p 54323 -c 500000
```

`runat.sh` and `runut.sh` start with `killall ut at`, so don't run them alongside another `ut`.
`runut.sh` can be invoked from any directory and returns ut's real exit status; `runat.sh` still
returns 143 from its own trap, so judge that one by its output line.

Judge the acceptance run by its output, not its exit status: `runat.sh`'s `trap … EXIT` runs
`kill -- -$$`, so when the script is its own process-group leader it SIGTERMs itself and returns
143 even though every definition passed. The last line of output is the real verdict —
`470 tests passed`, or `FAILED n out of 470 tests`.

CI runs the unit tests on every push, the acceptance suite on pull requests only, and `pt` only in
the Release configuration.

The Debug leg also covers the checked-in SWIG output, which went 33 commits without compiling at
all before anyone noticed. It configures with `-DHAVE_PYTHON3=ON`, so CMake compiles and links
`src/python/QuickfixPython.cpp` into `_quickfix.so`; Ruby has no CMake path (`extconf.rb` /
`make_ruby.sh`), so `src/ruby/QuickfixRuby.cpp` is compiled separately with `-fsyntax-only`. If a
change to `src/C++` breaks either, regenerate with `src/python/swig.sh` and `src/ruby/swig.sh`
rather than hand-editing the output.

The Debug leg additionally syntax-checks all six database backends, which no build configures — CI
never sets `HAVE_MYSQL`, `HAVE_POSTGRESQL` or `HAVE_ODBC`, so `MySQLStore.cpp`, `MySQLLog.cpp`,
`PostgreSQLStore.cpp`, `PostgreSQLLog.cpp`, `OdbcStore.cpp` and `OdbcLog.cpp` would otherwise
compile for nobody. The runner needs `unixODBC-devel`, `libpq5-devel` and
`mariadb-connector-c-devel`.

On **pull requests** a further step links them: it configures with `-DHAVE_MYSQL=ON
-DHAVE_POSTGRESQL=ON -DHAVE_ODBC=ON` into its own build directory and then checks `ldd` really
reports `libmariadb`, `libpq` and `libodbc`, since a build that quietly ignored the options would
otherwise exit 0 and prove nothing. That is a second full build (~2m15s), which is why it is not on
every push. It also restores `src/C++/config.h`, which `configure_file` rewrites — that file is
tracked.

One limit remains: linking proves the symbols exist, not that they behave. Exercising them needs
live database servers, so `ut` is not run in that configuration — enabling the backends takes it
from 56 test cases to 60, and the four new ones fail to connect.

The Debug leg then **runs** both binding suites: the Python bindings' 32 tests
(`src/python/test/`) and the Ruby bindings' 33 (`src/ruby/test/`, via `TestSuite.rb`). Ruby has no
CMake path, so CI builds it with the project's own `make_ruby.sh` rather than a direct compiler
invocation — if that script breaks, the step goes red instead of something else quietly covering for
it. Running the Ruby suite needs `rubygem-test-unit` on the runner, since Ruby 3.3 no longer bundles
`test/unit`.

To run the Python bindings' tests yourself:

```bash
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug -DHAVE_SSL=ON -DHAVE_PYTHON3=ON
cmake --build build -j"$(nproc)"
cd src/python
PYTHONPATH=../../lib:. LD_LIBRARY_PATH=../../lib QUICKFIX_PATH=../.. \
  sh -c 'for t in test/*TestCase.py; do python3 "$t"; done'
```

### Writing Tests

- Unit tests live in `src/C++/test/`, one `<Subject>TestCase.cpp` per subject, using Catch2
  (`TEST_CASE` / `TEST_CASE_METHOD` with a fixture, plus `SECTION`s)
- **Add every new file to the `ut_SOURCES` list in `src/C++/test/CMakeLists.txt`** — the list is
  explicit, there is no glob, and an unlisted file compiles for nobody
- Session- and protocol-level behaviour is usually better covered by an acceptance script under
  `test/definitions/server/<version>/*.def`: `I…` lines are sent to the engine, `E…` lines are the
  expected responses
- Test edge cases and error conditions
- Ensure tests are deterministic and don't depend on external state

### Sanitizers

Run the sanitizer builds for anything touching locking, object lifetime, or the repeating-group
arena; "Sanitizer verification" in `README.md` has the exact configure lines. Two things to know
before you read the output: `ut` **exits 0** under every sanitizer — ASan + UBSan with no leak
record and no vptr report, and ThreadSanitizer with no warning — so anything a run prints is yours — and the ASan build must not set `ENABLE_TBB_ALLOCATOR`,
which makes ASan and LeakSanitizer blind to `FieldMap::Fields` and the socket send queues. Give
each sanitizer build its own `-DQUICKFIX_LIB_OUTPUT_DIR`, otherwise it overwrites `lib/` and
re-points the `test/{ut,at,pt}` symlinks belonging to your ordinary build.

### Test Coverage

We aim for high test coverage. When adding new features:
- Write tests that cover the main functionality
- Test error paths
- Test boundary conditions

## Documentation

### Code Documentation

- Use Doxygen comments for public APIs
- Keep comments up-to-date with code changes
- Document parameters, return values, and exceptions

### User Documentation

When making user-facing changes:
- Update relevant HTML documentation in `doc/html/`
- Update README.md if needed
- Update configuration.html for new settings
- Provide examples when appropriate

### Generating Documentation

```bash
cd doc
./document.sh
```

## Development Workflow

### Setting Up Development Environment

1. **Install Prerequisites**:
   - **GCC 15+**. `FieldMap.h` uses `std::flat_map`, which GCC 14's standard library does not ship.
   - CMake 3.31+, and Ninja for the CI-equivalent build
   - Ruby, for the acceptance suite and the code generators; the generators also need
     the `rexml` gem, which Ruby 3 no longer bundles (`gem install --user-install rexml`)
   - `clang-format-22`, matching the version CI enforces
   - Optional: OpenSSL, MySQL, PostgreSQL, ODBC, TBB

2. **Build in Development Mode**:
   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DHAVE_SSL=ON
   cmake --build build -j"$(nproc)"
   ```

   Pass `-DHAVE_SSL=ON`: the top-level `configure_file` rewrites the tracked `src/C++/config.h`
   on every configure, and the committed version has SSL enabled — configuring without it shows up
   as an unrelated diff. Binaries land in `lib/`, not in the build directory.

3. **Enable clang-format integration** in your IDE/editor

### Debugging

- Build with `-DCMAKE_BUILD_TYPE=Debug`
- Use `gdb`, or `lldb` if you prefer it
- Check logs in the FileStore directory

## Platform Notes

**Linux is the only supported platform**, and the Windows-specific files have been removed: the
`WIN32` socket monitor, the MSVC shims, the `stdafx` precompiled-header shim, the `.bat` runners
and the Windows-only `atrun` supervisor. A few inline `_MSC_VER` guards remain in shared sources.
Treat those as dead weight rather than a constraint: don't let one shape a design, and don't spend
the gate on it.

- **CentOS Stream 10 / RHEL 10** (what CI runs): `gcc-toolset-15`, `cmake`, `ninja-build`, `ruby`;
  put the toolset ahead of the system GCC 14 for every build and test step:
  ```bash
  export CC=/opt/rh/gcc-toolset-15/root/usr/bin/gcc
  export CXX=/opt/rh/gcc-toolset-15/root/usr/bin/g++
  export PATH=/opt/rh/gcc-toolset-15/root/usr/bin:$PATH
  export LD_LIBRARY_PATH=/opt/rh/gcc-toolset-15/root/usr/lib64
  ```
- **Debian/Ubuntu**: `g++-15 cmake ninja-build ruby`
- For SSL: `openssl-devel` / `libssl-dev`
- For MySQL: `mysql-devel` / `libmysqlclient-dev`
- For PostgreSQL: `libpq-devel` / `libpq-dev`

## Getting Help

- **Questions**: Use [GitHub Discussions](https://github.com/dudleylane/quickfix/discussions)
- **Chat**: Join the community on the mailing list
- **Issues**: For bugs and feature requests only

## Recognition

Contributors will be recognized in the project's contributor list. Significant contributions may be highlighted in release notes.

Thank you for contributing to QuickFIX!
