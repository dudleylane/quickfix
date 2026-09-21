# QuickFIX (Hardened Fork)

[![License](https://img.shields.io/badge/license-AGPL--3.0-red.svg)](LICENSE)

A correctness-hardened fork of [QuickFIX](https://github.com/quickfix/quickfix), the open-source [FIX protocol](http://www.fixprotocol.org/) engine. Supports FIX 4.0 through 5.0 SP2, including FIXT 1.1.

## Changes from upstream

This fork applies the following fixes and improvements over [quickfix/quickfix](https://github.com/quickfix/quickfix):

### Thread safety
- **Mutex**: Replaced hand-rolled recursive lock (data race on `m_count`/`m_threadID`) with `PTHREAD_MUTEX_RECURSIVE`
- **Session::send()**: Added missing lock on `m_pResponder` read — fixes use-after-free on concurrent send + disconnect
- **Session::setResponder()**: Added missing lock on `m_pResponder` write — fixes race during connection establishment
- **Session::s_mutex**: Replaced global `Mutex` with `std::shared_mutex` — concurrent session lookups no longer serialize under multi-session load. Fixed unprotected `getSessions()`

### Memory safety
- **FieldMap copy assignment**: Copy-and-swap idiom for strong exception guarantee — fixes memory leak when copy throws mid-group
- **FieldMap copy constructor**: Direct member initialization instead of delegating to `operator=`
- **Parser**: Added `MAX_MESSAGE_SIZE` (8 MB) bound on `addToStream()` — prevents unbounded memory growth from malicious/malformed peers
- **Message**: Bounds check on `RawDataLength`-computed iterator — prevents out-of-bounds read from corrupted data length fields
- **FileStoreTestCase**: Added missing `destroy()` call — fixes test fixture memory leak

### SSL/OpenSSL
- **X509 leak**: Added `X509_free()` after `SSL_get_peer_certificate()` in `acceptSSLConnection()`
- **EVP_PKEY leak**: Added `EVP_PKEY_free()` after `X509_get_pubkey()` in `typeofSSLAlgo()`
- **ssl_socket_close**: Added missing `socket_close()` after `SSL_shutdown()` — fixes socket fd leak on every SSL connection teardown; implemented proper two-phase shutdown
- **ERR_load_BIO_strings**: Removed deprecated no-op call

### Latency
- **GroupArena**: Per-message bump-pointer arena for repeating group allocation — eliminates per-group `new`/`delete` during parsing. 32-slot arena (3.8 KB) lazily allocated on first group, bulk-reset on `clear()`. Pooled messages reuse the arena across parse cycles.
- **Move-semantic appendField**: Eliminates one string copy per field during message deserialization
- **Sorted-check guard**: `sortFields()` skips redundant `std::sort` for well-ordered messages
- **Group slicing fix**: Virtual `cloneInto()` preserves `Group::m_field`/`m_delim` during copy — `addGroup` and copy constructor previously sliced to `FieldMap`

#### Benchmark results (100K iterations, Intel i7-4770)

| Operation | Upstream (μs) | This fork (μs) | Improvement |
|---|---|---|---|
| Deserialize Heartbeat | 0.288 | 0.219 | **-24%** |
| Deserialize NewOrderSingle | 0.681 | 0.546 | **-20%** |
| Deserialize QuoteRequest (10 groups) | 5.451 | 5.543 | — |
| Pooled vs unpooled QuoteRequest | 6.465 | 5.543 | **-14%** |
| Socket round-trip NOS | 3.236 | 2.966 | **-8%** |

### OpenSSL 3.0
- **DH/ECDH**: Auto-negotiation via `SSL_CTX_set_dh_auto` on OpenSSL 3.0+ — eliminates all `DH_new`/`EC_KEY_new_by_curve_name` deprecation warnings
- **ERR_load_BIO_strings**: Removed (no-op since OpenSSL 1.1.0)
- Legacy DH parameter code guarded with `OPENSSL_VERSION_NUMBER < 0x30000000L`

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

# Performance tests (pooled vs unpooled benchmarks included)
./lib/pt -p <port> -c <count>
```

### Message pooling

For latency-critical applications, reuse `Message` objects instead of creating new ones per FIX string. The internal `GroupArena` survives `clear()` and is reused on the next `setString()` call — no arena reallocation between parse cycles:

```cpp
FIX::Message pooledMsg;
while (auto raw = receiveFromSocket()) {
  pooledMsg.setString(raw, false, &dataDictionary);
  processMessage(pooledMsg);
  // pooledMsg.clear() is called by the next setString() — arena is reset, not freed
}
```

### Sanitizer verification

These recipes are how changes are checked under ThreadSanitizer and AddressSanitizer + UBSan. Run
them for anything touching locking, object lifetime, or the repeating-group arena.

**The ASan build must not enable the TBB allocator.** ASan detects heap errors through the allocator
it interposes. `tbb::scalable_allocator` suballocates from `libtbbmalloc`'s own slabs, which ASan
does not intercept, so blocks it returns carry no redzones and are invisible to LeakSanitizer. With
`-DENABLE_TBB_ALLOCATOR=ON` that covers `FieldMap::Fields` and the `SocketConnection` /
`SSLSocketConnection` send queues — every use of `ALLOCATOR` in `Utility.h`. Measured on this tree:
an identical 240-byte heap overflow and 4000-byte leak are both reported under the default allocator
and both pass silently under `tbb::scalable_allocator`. No flag changes this; it is inherent to an
uninstrumented allocator, so the TBB configuration simply has no ASan coverage of those two
containers. (Only `TBB::tbbmalloc` is linked, not `tbbmalloc_proxy`, so global `new`/`malloc` are
unaffected and everything else remains visible to ASan.)

#### Known baseline — the suite is not currently clean

As of `76f1c56b` (2026-09-21), `ut` under ASan + UBSan **exits 1**:

- **173 leak records — 2,698,428 bytes in 39,867 allocations.** 152 of the records allocate inside
  `DataDictionary`. The two direct roots are libstdc++'s demangler and `string_concat`
  (`Utility.cpp:100`), whose `new char[]` result is dropped by the caller at
  `src/C++/test/UtilityTestCase.cpp:70`.
- **65 UBSan reports** — 63 `member call on address …` and 2 `downcast of address …`, i.e. the vptr
  check firing in `DataDictionary.h` and `Field.h`.

These counts are **identical with and without `ENABLE_TBB_ALLOCATOR`**, so they are not an artefact
of the allocator. Treat them as a baseline to diff against rather than an accepted state: a change
is clean if it does not add to them. The TSan recipe has not been re-measured against this baseline.

```bash
# TSan (thread safety)
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=thread" \
  -DHAVE_SSL=ON -DENABLE_TBB_ALLOCATOR=ON \
  -DQUICKFIX_LIB_OUTPUT_DIR=build-tsan/out
cmake --build build-tsan -j$(nproc)
build-tsan/out/ut --quickfix-config-file test/cfg/ut.cfg --quickfix-spec-path spec

# ASan + UBSan (memory errors)
# No -DENABLE_TBB_ALLOCATOR here: it would hide heap errors and leaks in
# FieldMap::Fields and the socket send queues (see above).
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DHAVE_SSL=ON \
  -DQUICKFIX_LIB_OUTPUT_DIR=build-asan/out
cmake --build build-asan -j$(nproc)
build-asan/out/ut --quickfix-config-file test/cfg/ut.cfg --quickfix-spec-path spec
```

## License

This fork is licensed under [AGPL-3.0](LICENSE). The original QuickFIX code is under the [QuickFIX Software License](https://www.quickfixengine.org/LICENSE).

## Upstream

This is a fork of [quickfix/quickfix](https://github.com/quickfix/quickfix). Contributions that are not fork-specific should be submitted upstream.

## Issues

Report bugs and request features on [GitHub Issues](https://github.com/dudleylane/quickfix/issues).
