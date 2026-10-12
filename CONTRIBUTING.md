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

Security vulnerabilities don't go in issues: report them privately, as [SECURITY.md](SECURITY.md)
describes.

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

7. **Open a PR**: Target `dudleylane/quickfix:master`, whether or not the change is specific to
   this fork's hardening work. The fork no longer merges from
   [`quickfix/quickfix`](https://github.com/quickfix/quickfix), so a change sent there does not
   reach it.

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

Two mechanisms exempt files from that check, and both are deliberate. The pipeline above drops the
vendored `pugixml`, `scope_guard` and `catch_amalgamated` by name. `clang-format-22` itself then
honours the `.clang-format-ignore` files, which exempt:

- generated code: the message classes in `src/C++/fix40/` ... `fixt11/` and the SWIG wrappers
  (`src/python/QuickfixPython.*`, `src/python3/QuickfixPython.*`, `src/ruby/QuickfixRuby.*`) --
  regenerate them, never hand-format them;
- vendored code: `src/C++/double-conversion/`, `src/getopt.c` and `src/getopt-repl.h`;
- `src/C++/index.h`, the Doxygen main page, which is a comment only;
- `include/**`, which the build copies there.

Anything else is checked, and a new exemption needs a reason in this list. Formatting-only commits
are recorded in `.git-blame-ignore-revs`; run `git config blame.ignoreRevsFile
.git-blame-ignore-revs` once to keep `git blame` readable.

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
`runut.sh` can be invoked from any directory. Both exit with their suite's real status.
`runat.sh` waits up to 300 seconds for `at` to listen before running any definition, and fails
with a message if it exits or misses that deadline; a sanitizer build of `at` can take two minutes
to start. It prints its verdict: `470 tests passed`, or `FAILED n out of 470 tests`. It then stops
`at` with SIGTERM and also fails if `at` exits non-zero or takes more than 30 seconds to stop,
printing `FAILED: at exited with status n at shutdown` last; under AddressSanitizer that is how a
leak report surfaces. Arguments after the port go to `at`: `./runat.sh 54321 -l` logs session
events on stdout, and `-t` runs the threaded transport instead of the reactor. `AT` runs another
acceptor in `at`'s place -- `AT="python3 ../src/at.py" ./runat.sh 54321`, or
`AT="ruby -I ../src/ruby ../src/at.rb"`, the same acceptor through each binding, with
`LD_LIBRARY_PATH` and, for Python, `PYTHONPATH` set as `test-python3.sh` sets them -- and
`DEFINITIONS` narrows the run, e.g. `DEFINITIONS='definitions/server/fix42/*.def'`.

`test/runexamples.sh` runs the example applications against tradeclient -- the executor in C++,
over TLS, through Python and Ruby, and ordermatch -- and checks every order is answered (CI runs it
on pull requests).

The acceptance harness has tests of its own -- the comparator that judges each definition, and the
parser, reflector and socket server around it: `cd test && ruby -I. test.rb` (a few seconds; CI
runs it on every push).

CI runs, on every push: the Debug and Release builds and `ut`; the SWIG and generated-code
checks, the binding suites, the staged install with its C++ consumers, and the acceptance
harness's own tests (Debug); a `pt` smoke run (Release); and `ut` under ASan + UBSan and under
TSan. On pull requests and manual runs (`gh workflow run build_test_cmake.yml --ref master`) it
adds the acceptance suite in both legs and under both sanitizers, the acceptance subset through
the Python and Ruby bindings, the database link and test step, the no-SSL and TBB-allocator
builds, and the `pt` regression gate, which compares this commit's `pt` with the base's
(`-f pt_base=<full SHA>` for a manual run; otherwise the previous commit).

The Debug leg also covers the checked-in SWIG output, which went 33 commits without compiling at
all before anyone noticed. It configures with `-DHAVE_PYTHON3=ON`, so CMake compiles and links
`src/python/QuickfixPython.cpp` into `_quickfix.so`, and with `-DHAVE_RUBY=ON`, so it builds
`src/ruby/QuickfixRuby.cpp` into `lib/ruby/quickfix.so` too (#118). Before either, it
regenerates both wrappers and fails if the result differs from what is checked in, so **a change
to a header `src/quickfix.i` wraps must commit the regenerated output with it**: run
`src/python/swig.sh` and `src/ruby/swig.sh` (from their own directories) and never hand-edit the
output. The check needs the SWIG version the wrappers were generated with, 4.3.0 at present; a
SWIG upgrade means regenerating once and committing the result.

The Debug leg additionally syntax-checks all six database backends, which no build configures — CI
never sets `HAVE_MYSQL`, `HAVE_POSTGRESQL` or `HAVE_ODBC`, so `MySQLStore.cpp`, `MySQLLog.cpp`,
`PostgreSQLStore.cpp`, `PostgreSQLLog.cpp`, `OdbcStore.cpp` and `OdbcLog.cpp` would otherwise
compile for nobody. The runner needs `unixODBC-devel`, `libpq5-devel` and
`mariadb-connector-c-devel`.

On **pull requests and manual runs** a further step links them: it configures with `-DHAVE_MYSQL=ON
-DHAVE_POSTGRESQL=ON -DHAVE_ODBC=ON` into its own build directory and then checks `ldd` really
reports `libmariadb`, `libpq` and `libodbc`, since a build that quietly ignored the options would
otherwise exit 0 and prove nothing. That is a second full build (~2m15s), which is why it is not on
every push. It also restores `src/C++/config.h`, which `configure_file` rewrites — that file is
tracked.

The same step then runs `ut` in that configuration, so the PostgreSQL and ODBC stores are
exercised rather than just linked; the ODBC tests go through psqlODBC to the same PostgreSQL
server. MySQL stays link-only: the runner has no MySQL server, and its client library is MariaDB's
`libmariadb`, an API-compatible replacement for `libmysqlclient` rather than the same library.

The runner's database is set up once (PostgreSQL 18 from the PGDG repository):

```bash
sudo /usr/pgsql-18/bin/postgresql-18-setup initdb
sudo systemctl enable --now postgresql-18
sudo -u postgres psql -c "CREATE ROLE quickfix LOGIN"
sudo -u postgres createdb quickfix
cat src/sql/postgresql/{sessions,messages,messages_log,event_log}_table.sql \
  | sudo -u postgres psql -d quickfix
echo "GRANT INSERT, UPDATE, DELETE, SELECT ON ALL TABLES IN SCHEMA public TO quickfix;" \
  | sudo -u postgres psql -d quickfix
# In /var/lib/pgsql/18/data/pg_hba.conf, above the default host rules, trust this one
# role and database on loopback only:
#   host  quickfix  quickfix  127.0.0.1/32  trust
#   host  quickfix  quickfix  ::1/128       trust
sudo systemctl reload postgresql-18
sudo dnf install postgresql-odbc   # /etc/odbcinst.ini already names the driver [PostgreSQL]
```

`test/cfg/ut.cfg` connects as `quickfix` with no password; the ODBC tests use the DSN-less string
`DRIVER={PostgreSQL};SERVER=localhost;DATABASE=quickfix;`.

The Debug leg then **runs** both binding suites: the Python bindings' 52 tests
(`src/python/test/`) and the Ruby bindings' 48 (`src/ruby/test/`, via `TestSuite.rb`), the Ruby
one on the module CMake built. `src/ruby/make_ruby.sh` builds the same module with mkmf for builds
without CMake; CI builds and loads that one too, so the script cannot rot. Running the Ruby suite needs `rubygem-test-unit` on the runner, since Ruby 3.3 no longer bundles
`test/unit`.

To run both suites yourself, use the scripts CI runs. Each works from any directory and exits
with its suite's status:

```bash
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug -DHAVE_SSL=ON -DHAVE_PYTHON3=ON -DHAVE_RUBY=ON
cmake --build build -j"$(nproc)"
src/python/test-python3.sh
src/ruby/test.sh
```

To install them, `cmake --install` puts the Python module, with the library, in the directory the
interpreter searches for that prefix -- `lib64/python3.12/site-packages` here, from Python's own
`sysconfig`; `-DQUICKFIX_PYTHON3_INSTALL_DIR=<dir relative to the prefix>` overrides it. With
`-DHAVE_RUBY=ON` it puts the Ruby module and the `quickfix*.rb` files in Ruby's `sitearchdir` and
`sitelibdir`, relative to the prefix when they lie under it (`lib64/ruby/site_ruby` and
`share/ruby/site_ruby` for `/usr/local` here); `QUICKFIX_RUBY_ARCH_INSTALL_DIR` and
`QUICKFIX_RUBY_LIB_INSTALL_DIR` override them (#118). Without CMake, `src/ruby/install.sh` installs
`make_ruby.sh`'s module the same way and honours `DESTDIR`. The Debug leg installs both bindings into
a staging prefix and loads them from there (#108).

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
each sanitizer build its own `-DQUICKFIX_LIB_OUTPUT_DIR`, otherwise it overwrites `lib/`. Only a
tree building into the default `lib/` points the `test/{ut,at,pt}` symlinks at its binaries (#54),
so run a sanitizer tree's binaries by path, as README's recipes do.

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

4. **Enable the commit-msg hook** once per clone:
   ```bash
   git config core.hooksPath .githooks
   ```
   It rejects a commit touching executable content — C++, CMake, workflows, scripts, spec XML —
   whose message carries neither the five framing lines
   (`Issue:`, `Impact:`, `Found:`, `Fix:`, `Success:`) nor a `Deviation:` trailer.

5. **Fetch commit-message corrections** once per clone:
   ```bash
   git config --add remote.origin.fetch '+refs/notes/commits:refs/notes/commits'
   git fetch origin
   ```
   `master` cannot be force-pushed, so a pushed commit message that turns out to be wrong is corrected
   with a git note, which `git log` shows beneath the message. Git does not fetch notes by default, and
   GitHub does not display them, so the issue that found the error records the correction too.

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
- For MySQL: `mariadb-connector-c-devel` / `libmariadb-dev` (what CI builds against)
- For PostgreSQL: `libpq-devel` / `libpq-dev`

## Getting Help

- **Questions, bugs and feature requests**: open a
  [GitHub issue](https://github.com/dudleylane/quickfix/issues); the fork has no Discussions board
  and no mailing list
- **Security problems**: report them privately, as [SECURITY.md](SECURITY.md) describes, not in an
  issue

## Recognition

Contributors will be recognized in the project's contributor list. Significant contributions may be highlighted in release notes.

Thank you for contributing to QuickFIX!
