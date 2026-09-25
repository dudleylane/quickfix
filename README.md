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
- **`FieldBase` is not polymorphic** (`Field.h`): the virtual destructor was removed — nothing owned a `FieldBase *` — taking every field from 88 to 80 bytes and making the class standard-layout.
- **No per-field encoded-string cache** (`Field.h`): `m_data`, a `mutable std::string` holding `tag=value<SOH>`, was another 32 bytes and a data race waiting to happen — it was written from `const` methods. `appendTo()` writes the field straight into the caller's buffer instead, and `getLength()`/`getTotal()` compute from the tag and value rather than building the string to measure it. A field is 48 bytes `FIX_ASSERT_FIELD_LAYOUT` in the `DEFINE_*` macros is the guard that replaces UBSan's `vptr` check
- **`FieldRef<F>`** (`FieldMap.h`): `FIELD_GET_REF`, `FIELD_GET_PTR` and `getField<T>()` no longer cast the stored `FieldBase` to a derived field type — `FieldMap` slices on `addField`, so that cast was undefined behaviour. They now read the value through `F::valueOf`, which takes a `FieldBase`. This is what makes the sanitizer suite clean; see "Known baseline". Note the one change a compiler will not catch: `auto x = msg.getField<T>()` used to copy the field, and now copies a view that aliases the stored `FieldBase`, so it must not outlive the next `setString()` or `clear()` on that message — bind the value (`const std::string &`, `SEQNUM`) instead of the view when it needs to

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

#### Benchmark baseline

Captured at `63c5c066` with `./pt -p <port> -c 100000 -r 9`, pinned via `taskset -c 1,2,3` on an
Intel i7-6820HQ (4C/8T, `performance` governor), Release build, GCC 15. Median of nine runs after a
discarded warm-up; **cv** is the coefficient of variation across those runs. The run waits for the
1-minute load average to fall below 0.20 before starting: a fixed settle is not enough after a
parallel build, and starting warm inflates every number in the run by several percent.

| Operation | Median (μs) | cv |
|---|---|---|
| Deserialize Heartbeat | 0.205 | 2.1% |
| Deserialize NewOrderSingle | 0.537 | 3.2% |
| Deserialize QuoteRequest (10 groups) | 5.269 | 2.2% |
| Serialize QuoteRequest | 1.363 | 2.5% |
| Read fields from QuoteRequest | 2.068 | 1.3% |
| Socket round-trip NOS | 4.808 | 1.1% |
| ThreadedSocket round-trip NOS | 3.603 | 1.1% |

**Serialize QuoteRequest rose from 0.945 μs** when `63c5c066` removed `FieldBase`'s cache of the
encoded field. That benchmark calls `toString()` in a loop over one unmodified message, so it used
to measure a cache hit after the first iteration. The engine has no such path — `Session::sendRaw`
serialises each message once and hands that one string to both `persist()` and `send()` — which is
why the round-trip rows did not move and the parse rows improved.

Message pooling, measured in the same process as reusing one `Message` across `setString()` calls
versus constructing a new one each time:

| Operation | Unpooled (μs) | Pooled (μs) | Delta |
|---|---|---|---|
| Heartbeat | 0.269 (cv 2.1%) | 0.205 (cv 2.1%) | −24% |
| NewOrderSingle | 0.528 (cv 5.0%) | 0.537 (cv 3.2%) | +2% |
| QuoteRequest (10 groups) | 6.124 (cv 0.4%) | 5.269 (cv 2.2%) | −14% |

Read these as indicative, not as guarantees. Pooling helps on Heartbeat and on the group-heavy
QuoteRequest, where the arena avoids repeated group allocation; on NewOrderSingle the +2% is inside
its own cv and resolves nothing either way. A difference smaller than roughly twice the cv is not
resolvable on this hardware.

These figures are absolute, not a comparison against upstream: the previous table's upstream column
was measured on different hardware with unrecorded methodology and could not be reproduced here. It
remains in git history.

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

**Linux only.** CentOS Stream 10 / RHEL 10 is what CI builds and tests; other distributions with a
new enough toolchain should work but are not verified.

Windows and macOS support has been removed: the `WIN32` socket monitor, the MSVC shims, the `.bat`
runners, the `stdafx` precompiled-header shim and the Windows-only `atrun` supervisor are all gone.
A few inline `_MSC_VER` guards remain in shared sources; they are inert here and not maintained.

## Prerequisites

- **GCC 15+**. `FieldMap.h` uses `std::flat_map`, which GCC 14's standard library does not ship.
- CMake 3.31+, and Ninja for the CI-equivalent build
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

#### Known baseline — the suite is clean

As of `74fe9320` (2026-09-25), `ut` runs **56 test cases / 2034 assertions**, all passing, and
**exits 0 under every sanitizer** — ASan + UBSan with no leak record and no report, and
ThreadSanitizer with no warning. That is the baseline to diff against — anything a run reports is
yours. Re-verified at that commit rather than carried forward: `26aa506c` and `74fe9320` changed the
parse path, and `26aa506c` in particular added hand-written indexing over the message buffer on a
malformed-input path, which is exactly what ASan is for.

It took three changes to get there, all of one idiom. `FieldMap` stores fields by value in
`std::vector<FieldBase>`, so `addField` slices any derived field; reading the stored object back as
a `StringField`, `UInt64Field`, `IntField`, `MsgType` or `MsgSeqNum` is undefined behaviour, and
UBSan's `vptr` check saw every instance. The idiom is upstream and long-standing — the
`virtual ~FieldBase` that made it detectable dated to 2006 — and was not introduced by this fork.
It was inert **only** because no derived field type adds a data member or a virtual override, so
each access stayed inside the base object; one added member would have turned each into an
out-of-bounds read.

That destructor is gone as of `7782f9a9` — nothing ever owned a `FieldBase *`, so it bought nothing
and cost 8 bytes on every stored field — which also removes the `vptr` check as a detector. The
invariant it was detecting is asserted directly in its place: `FIX_ASSERT_FIELD_LAYOUT`, carried by
the `DEFINE_*_NUM` macros in `Field.h`, fails to compile if any of the 6,107 generated field classes
or the 11 hand-written intermediates ever stops being layout-identical to `FieldBase`. **If you add
a member to a field class, that assertion is what will stop you, and it is telling you the truth:
`FieldMap` would silently drop the member.**

| Cast | Removed by | Reports |
|---|---|---|
| Generated `FIXnn::Message::getHeader()`/`getTrailer()` casting `FIX::Header`/`FIX::Trailer` to the per-version subclass | the generator change; see `FieldMap`'s typed `set`/`get`/`isSet`/`getIfSet` | 17 |
| `FIELD_GET_REF` / `FIELD_GET_PTR` | `92877495` — now a `FieldRef<F>` and a `const FieldBase *` | 23 |
| `getField<T>()`'s `reinterpret_cast` | `2c065998` — now a `FieldRef<T>` | 25 |

All three read the value through `F::valueOf`, which takes a `FieldBase` and needs no cast.

The single leak record went with them: it was libstdc++'s `d_growable_string_callback_adapter`, the
buffer `__cxa_demangle` grows for each type name a UBSan report prints and never frees, so it
tracked the report count. It was 2,697,884 bytes in 173 records until `558f56d0`, `7383a9ef` and
`7bc5c74e` removed the orphaned test Sessions, the `poll()`-path `SocketServer` leak and the
`findCAList` leaks; 1,536 then 800 bytes while reports remained; zero once they did not.

The counts were identical with and without `ENABLE_TBB_ALLOCATOR` and in a static build, so they
were never an allocator or shared-library RTTI artefact.

**ThreadSanitizer is clean too, as of `f4c9c0c3`** — `ut` exits 0 with no warning, 56 test cases and
2034 assertions. That is the first result the TSan recipe has ever produced on this tree, and the
reason is worth recording rather than filing as an absence: `UtilityTestCase` joined a thread and
then detached the same id, which is undefined behaviour once `pthread_join` has invalidated the
handle. TSan does not report that — it aborts its own runtime on it, so `ut` exited 66 having run no
tests. Behind it, the two-argument `thread_spawn(func, var)` left its thread joinable while
discarding the id, which is a leak by construction. Two lines in the thread helpers were hiding the
entire thread-safety signal of a fork whose first stated divergence from upstream is thread safety.
The engine itself was correct on both counts; see the commit.

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
