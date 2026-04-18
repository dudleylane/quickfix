# QuickFIX (Hardened Fork)

[![License](https://img.shields.io/badge/license-AGPL--3.0-red.svg)](LICENSE)

A correctness-hardened fork of [QuickFIX](https://github.com/quickfix/quickfix), the open-source [FIX protocol](http://www.fixprotocol.org/) engine. Supports FIX 4.0 through 5.0 SP2, including FIXT 1.1.

## Changes from upstream

This fork applies the following fixes and improvements over [quickfix/quickfix](https://github.com/quickfix/quickfix):

### Thread safety
- **Mutex**: Replaced hand-rolled recursive lock (data race on `m_count`/`m_threadID`) with `PTHREAD_MUTEX_RECURSIVE`
- **Session::send()**: Added missing lock on `m_pResponder` read — fixes use-after-free on concurrent send + disconnect
- **Session::setResponder()**: Added missing lock on `m_pResponder` write — fixes race during connection establishment

### Memory safety
- **FieldMap copy assignment**: Copy-and-swap idiom for strong exception guarantee — fixes memory leak when copy throws mid-group
- **FieldMap copy constructor**: Direct member initialization instead of delegating to `operator=`
- **Parser**: Added `MAX_MESSAGE_SIZE` (8 MB) bound on `addToStream()` — prevents unbounded memory growth from malicious/malformed peers
- **Message**: Bounds check on `RawDataLength`-computed iterator — prevents out-of-bounds read from corrupted data length fields

### SSL/OpenSSL
- **X509 leak**: Added `X509_free()` after `SSL_get_peer_certificate()` in `acceptSSLConnection()`
- **EVP_PKEY leak**: Added `EVP_PKEY_free()` after `X509_get_pubkey()` in `typeofSSLAlgo()`
- **ssl_socket_close**: Added missing `socket_close()` after `SSL_shutdown()` — fixes socket fd leak on every SSL connection teardown; implemented proper two-phase shutdown
- **ERR_load_BIO_strings**: Removed deprecated no-op call

### FIX protocol
- **SequenceReset-GapFill**: Allow `NewSeqNo < ExpectedTargetNum` when `GapFillFlag=Y`, per FIX spec (was incorrectly rejected)

### Build system
- **C++23**: Minimum standard raised from C++17; `CMAKE_CXX_STANDARD_REQUIRED=ON`
- **CMake 3.31**: Minimum version raised from 3.5
- **Conditional compilation**: MySQL, PostgreSQL, ODBC sources now conditionally compiled (matching SSL pattern) — no longer requires database headers when features are disabled
- **TBB allocator**: New `ENABLE_TBB_ALLOCATOR` CMake option with automatic `tbbmalloc` link dependency
- **HAVE_ODBC**: Moved from `add_definitions()` to `cmake_config.h.in` for consistency with MySQL/PostgreSQL
- **Removed**: Dead AIX/Solaris platform code, duplicate `configure_file` call

## Supported Platforms

- **Linux**: Ubuntu (latest), CentOS Stream 10, various distributions
- **Windows**: Windows Server 2019, Windows Server 2022
- **macOS**: Latest versions

## Prerequisites

- C++23 compatible compiler (GCC 13+, Clang 16+, MSVC 19.35+)
- CMake 3.31+
- Optional: OpenSSL (for SSL/TLS support)
- Optional: MySQL, PostgreSQL, or ODBC (for database message stores)
- Optional: TBB (for scalable allocator)

## Building with CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

### CMake Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `HAVE_SSL` | OFF | Enable SSL/TLS support (requires OpenSSL) |
| `HAVE_MYSQL` | OFF | Enable MySQL message store/log |
| `HAVE_POSTGRESQL` | OFF | Enable PostgreSQL message store/log |
| `HAVE_ODBC` | OFF | Enable ODBC message store/log |
| `HAVE_PYTHON3` | OFF | Build Python 3 bindings |
| `ENABLE_TBB_ALLOCATOR` | OFF | Use TBB scalable allocator for internal containers |
| `QUICKFIX_SHARED_LIBS` | ON | Build shared libraries |
| `QUICKFIX_EXAMPLES` | ON | Build example applications |
| `QUICKFIX_TESTS` | ON | Build tests |

### Example: SSL + TBB allocator

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DHAVE_SSL=ON \
  -DENABLE_TBB_ALLOCATOR=ON
cmake --build build -j$(nproc)
```

## Testing

```bash
# Unit tests (Catch2)
./lib/ut --quickfix-config-file test/cfg/ut.cfg --quickfix-spec-path spec

# Acceptance tests (Ruby)
cd test && bash setup.sh <port> && ../lib/at -f cfg/at.cfg &
ruby -I. Runner.rb 127.0.0.1 <port> definitions/server/fix4*/*.def

# Performance tests
./lib/pt -p <port> -c <count>
```

## Building with Autotools

```bash
./bootstrap
./configure
make
make check
sudo make install
```

## License

This fork is licensed under [AGPL-3.0](LICENSE). The original QuickFIX code is under the [QuickFIX Software License](https://www.quickfixengine.org/LICENSE).

## Upstream

This is a fork of [quickfix/quickfix](https://github.com/quickfix/quickfix). Contributions that are not fork-specific should be submitted upstream.

## Issues

Report bugs and request features on [GitHub Issues](https://github.com/dudleylane/quickfix/issues).
