# QuickFIX (Hardened Fork)

[![License](https://img.shields.io/badge/license-AGPL--3.0-red.svg)](LICENSE)

A correctness-hardened fork of [QuickFIX](https://github.com/quickfix/quickfix), the open-source [FIX protocol](http://www.fixprotocol.org/) engine. Supports FIX 4.0 through 5.0 SP2, including FIXT 1.1.

## Changes from upstream

This fork applies the following fixes and improvements over [quickfix/quickfix](https://github.com/quickfix/quickfix). It no longer merges from upstream (see [Upstream](#upstream)), so check a hand-ported upstream fix against this list: a port into a file changed here can quietly undo one of these changes.

### Thread safety
- **Mutex**: Replaced hand-rolled recursive lock (data race on `m_count`/`m_threadID`) with `PTHREAD_MUTEX_RECURSIVE`
- **Session::send()**: Added missing lock on `m_pResponder` read — fixes use-after-free on concurrent send + disconnect
- **Session::setResponder()**: Added missing lock on `m_pResponder` write — fixes race during connection establishment
- **Callbacks outside the session lock** (#73): `Session` holds no lock of its own while it calls `toApp`, `toAdmin` or `onLogout`, or while it consults the application about a retransmission, which it then sends in one piece. Upstream calls them under the session lock, so an application lock taken in a callback and held while sending deadlocks. `toApp` and `toAdmin` now run before the message is numbered and see no `MsgSeqNum`, except on a gap fill or a resent message. `SendingTime` is stamped before them too, so when two threads send at once the message numbered later can carry the earlier `SendingTime` (#75)
- **Session::s_mutex**: Replaced global `Mutex` with `std::shared_mutex` — concurrent session lookups no longer serialize under multi-session load. Fixed unprotected `getSessions()`
- **SessionState**: the flags and counters read and written from several threads (`m_enabled`, `m_receivedLogon`, `m_sentLogon`, `m_logonTimeout`, …) are `std::atomic` (`0852bc4a`)
- **Thread helpers** (`Utility.cpp`): the two-argument `thread_spawn` detaches the thread it starts, since it returns no id that anything could join (`f4c9c0c3`)
- **Session settings**: the run-time-settable members behind `setCheckLatency`, `setResetOnLogon` and the rest are `std::atomic`, and `SessionState`'s last-sent and last-received times are read and written under its mutex, since any thread may send or change a setting while the session's own thread reads them (GHSA-7hm8-h6g7-8vf3, `da37aab8`)
- **SocketMonitor owner thread**: `drop()` called from a thread other than the one blocking on the monitor only shuts the socket down, and the blocking thread drops it, so the socket sets are touched by one thread (`da37aab8`)
- **Threaded acceptor shutdown**: a connection accepted while the acceptor stops is closed rather than spawned after `onStop()`'s snapshot of its threads (`edacfa1e`)

### Memory safety
- **FieldMap copy assignment**: Copy-and-swap idiom for strong exception guarantee — fixes memory leak when copy throws mid-group
- **FieldMap copy constructor**: Direct member initialization instead of delegating to `operator=`
- **Parser**: Added `MAX_MESSAGE_SIZE` (8 MB) bound on `addToStream()` — prevents unbounded memory growth from malicious/malformed peers
- **Parser bound handled and linear**: every connection class catches the bound's `MessageParseError` and drops just that connection, and the parser neither rescans bytes that hold no `8=` nor restarts its length-header search from the front on each read (GHSA-gmqc-6vqm-j7w7, `548109ca`, `197692dc`, `6a6c9a78`, `ff1e0ac5`)
- **Message**: Bounds check on `RawDataLength`-computed iterator — prevents out-of-bounds read from corrupted data length fields
- **FileStoreTestCase**: Added missing `destroy()` call — fixes test fixture memory leak
- **`FieldBase` is not polymorphic** (`Field.h`): the virtual destructor was removed — nothing owned a `FieldBase *` — taking every field from 88 to 80 bytes and making the class standard-layout.
- **No per-field encoded-string cache** (`Field.h`): `m_data`, a `mutable std::string` holding `tag=value<SOH>`, was another 32 bytes and a data race waiting to happen — it was written from `const` methods. `appendTo()` writes the field straight into the caller's buffer instead, and `getLength()`/`getTotal()` compute from the tag and value rather than building the string to measure it. A field is 48 bytes `FIX_ASSERT_FIELD_LAYOUT` in the `DEFINE_*` macros is the guard that replaces UBSan's `vptr` check
- **`FieldRef<F>`** (`FieldMap.h`): `FIELD_GET_REF`, `FIELD_GET_PTR` and `getField<T>()` no longer cast the stored `FieldBase` to a derived field type — `FieldMap` slices on `addField`, so that cast was undefined behaviour. They now read the value through `F::valueOf`, which takes a `FieldBase`. This is what makes the sanitizer suite clean; see "Known baseline". Note the one change a compiler will not catch: `auto x = msg.getField<T>()` used to copy the field, and now copies a view that aliases the stored `FieldBase`, so it must not outlive the next `setString()` or `clear()` on that message — bind the value (`const std::string &`, `SEQNUM`) instead of the view when it needs to
- **Generated `getHeader()`/`getTrailer()`**: the per-version message classes no longer cast a `FIX::Header` or `FIX::Trailer` to a subclass it never was; typed get and set moved to `FieldMap` (`85f53b62`)
- **SocketServer under `poll()`**: `SocketAcceptor` and `SSLSocketAcceptor` free their `SocketServer` when driven through `poll()`, which never runs `onStart()` (`7383a9ef`)
- **`message_order` sized by its fields** (`MessageSorters.h`): a group, header, trailer or message order keeps upstream's array indexed by field number — the largest field number + 1 ints — only while that costs at most eight times an open-addressed table of (field, position) pairs, and is the table otherwise. FIX 5.0 SP2 numbers fields up to 50,002, so each generated SP2 group object allocated up to 200 KB, and a loaded SP2 `DataDictionary` — which primes every group's order since `4c75cd41` — held 3.8 GB; it holds 76 MB now. The ordering is unchanged (#86)

### SSL/OpenSSL
- **X509 leak**: Added `X509_free()` after `SSL_get_peer_certificate()`, now in `checkSSLClient()`, which `ee3b1780` split out of `acceptSSLConnection()`
- **EVP_PKEY leak**: Added `EVP_PKEY_free()` after `X509_get_pubkey()` in `typeofSSLAlgo()`
- **findCAList leaks**: fixed the leaks it had on every call (`7bc5c74e`)
- **SSL descriptors closed once**: each SSL connection's descriptor has exactly one owner and is closed once (#25, `d0819d09`)
- **Server verified by default**: an SSL initiator verifies the server's certificate — against the configured CA, or the system's default trust store when none is configured — and checks that it names the host or address connected to; `CertificateVerifyLevel=0` is the only opt-out. A configured CA is the only trust anchor, and an acceptor honours `CertificateVerifyLevel` with or without one. Upstream verifies nothing unless a CA is configured and never checks the name (GHSA-rh2f-46w4-p7j3, `36c2baaf`)
- **Threaded TLS**: `ThreadedSSLSocketConnection` waits with `poll()` rather than `select()` (GHSA-ppch-w7ph-7qw4, `1c707427`), and its handshake runs on a non-blocking socket so the ten-second deadline fires (`b51bde5f`)

### Transport
- **TLS handshake stepped from the reactor**: `SSLSocketAcceptor` steps `SSL_accept` from the reactor's read and write events, with a 10 s deadline, instead of completing each handshake inline (GHSA-ph6x-vg87-665p, `ee3b1780`)
- **First message read incrementally**: both reactor acceptors read a connection's first message one readiness event at a time, with a 10 s setup deadline (GHSA-4jw9-9f3x-55wx, `98e23483`)
- **One owner per reactor descriptor**: teardown closes the listening socket once instead of up to three times, and closes connections still waiting in the monitor's connect set, which it used to leak (#29, `bd123e3a`)
- **Dropped sockets stay open until reported**: `SocketMonitor::drop()` keeps a descriptor open until its drop has been reported, so a reused number cannot receive another socket's report (#26, `f5d9c9ab`)
- **Threaded teardown from the owning thread**: other threads shut a threaded connection's socket down, and the connection's own thread closes it, once (#27, #28, `c3ce6f24`)
- **Setup deadline on every acceptor**: an accepted connection that has not logged on within ten seconds is dropped — including one whose first Logon drew only a reject (#71) — and the threaded acceptors keep listening after a failed `accept()` (GHSA-x32r-xvq9-g2g9, `063a9e5b`, `b51bde5f`)
- **Pending-connection limit**: `MaxPendingConnections` bounds how many accepted connections that have not logged on each acceptor holds; a new one beyond it is closed at once. Off by default (#72, `8106631a`)
- **Allow-list before the first message**: `AllowedRemoteAddresses` is checked on every acceptor before the first message reaches the session (GHSA-2ppv-6433-rfh2, `6347ee9d`)
- **HTTP admin server restricted**: it listens on 127.0.0.1 unless `HttpAcceptAddress` says otherwise, changes state only on a POST, refuses a Host that is a DNS name other than `localhost` and a POST from another Origin, and closes each request's socket once; it still has no authentication (GHSA-7hm8-h6g7-8vf3, `da37aab8`)

### Latency
- **GroupArena**: Per-message bump-pointer arena for the groups a message gets through `addGroup()` or a copy: building or copying a message with repeating groups takes each `Group` object from the arena instead of `new`/`delete` (its field vector still allocates). 32 slots of 128 bytes (4 KB), allocated on first use and bulk-reset on `clear()`. Parsing does not use it: `setString()` allocates each group with `new`, and moving those onto the arena measured at no more than about 4% of a QuoteRequest parse, which did not justify the risk (#52).
- **Move-semantic appendField**: Eliminates one string copy per field during message deserialization
- **Sorted-check guard**: `sortFields()` skips redundant `std::sort` for well-ordered messages
- **Group slicing fix**: Virtual `cloneInto()` preserves `Group::m_field`/`m_delim` during copy — `addGroup` and copy constructor previously sliced to `FieldMap`
- **Groups in a `std::flat_map`**: `FieldMap::Groups` is a `std::flat_map` rather than a `std::map` (`02f84c2d`)
- **Component index per dictionary load** (`DataDictionary.cpp`): each component reference is looked up in a name index built once per load, not by an XPath query from the document root. A FIX 5.0 SP2 load made 36,605 of those queries over 725 components; it now takes 0.32 s in a Release build rather than 1.42 s, and the result is identical (#87)

#### Benchmark baseline

Captured at `2c9ed23a` with `./pt -p <port> -c 100000 -r 9`, pinned via `taskset -c 1,2,3` on an
Intel i7-6820HQ (4C/8T, `performance` governor), Release build, GCC 15. Median of nine runs after a
discarded warm-up; **cv** is the coefficient of variation across those runs. The run waits for the
1-minute load average to fall below 0.20 before starting: a fixed settle is not enough after a
parallel build, and starting warm inflates every number in the run by several percent.

| Operation | Median (μs) | cv | vs `63c5c066` |
|---|---|---|---|
| Deserialize Heartbeat | 0.205 | 2.7% | +0.2% |
| Deserialize NewOrderSingle | 0.553 | 1.5% | +2.9% |
| Deserialize QuoteRequest (10 groups) | 5.241 | 0.6% | −0.5% |
| Serialize QuoteRequest | 1.349 | 1.5% | −1.1% |
| Read fields from QuoteRequest | 2.058 | 4.7% | −0.5% |
| Socket round-trip NOS | 4.668 | 0.9% | −2.9% |
| ThreadedSocket round-trip NOS | 3.407 | 1.1% | −5.4% |

The last column spans four commits — `41415ed7`, `f4c9c0c3`, `26aa506c`, `74fe9320` — and two
separately captured runs, so no single row is attributable to one change. Read it as drift, not
as a measurement of anything.

Two rows are worth a word. **Deserialize NewOrderSingle** is up because `26aa506c` put an
embedded-SOH check on the parse path; a controlled same-session A/B of that commit alone measured
~+3% median across the nine parse benchmarks, which is consistent with this row and with the other
two parse rows being flat. That cost is real and was accepted deliberately — see "Known baseline"
for what it buys. The **round-trip rows moving down** is not explained by anything in those four
commits and is most likely cross-session variation; treat it as noise rather than an improvement.

**Serialize QuoteRequest rose from 0.945 μs** when `63c5c066` removed `FieldBase`'s cache of the
encoded field. That benchmark calls `toString()` in a loop over one unmodified message, so it used
to measure a cache hit after the first iteration. The engine has no such path — `Session::sendRaw`
serialises each message once and hands that one string to both `persist()` and `send()` — which is
why the round-trip rows did not move and the parse rows improved.

Message pooling, measured in the same process as reusing one `Message` across `setString()` calls
versus constructing a new one each time. Both sides parse with the FIX 4.2 data dictionary; before
`ed9178c6` the unpooled rows had no dictionary, built no repeating groups, and so credited pooling
with the cost of group construction (#52). Captured at `ed9178c6` the same way as the table above:
`./pt -p <port> -c 100000 -r 9`, pinned via `taskset -c 1,2,3`, Release build, started once the
1-minute load average was below 0.20.

| Operation | Unpooled (μs) | Pooled (μs) | Delta |
|---|---|---|---|
| Heartbeat | 0.334 (cv 5.7%) | 0.221 (cv 5.7%) | −34% |
| NewOrderSingle | 0.675 (cv 2.7%) | 0.597 (cv 16.7%) | −12% |
| QuoteRequest (10 groups) | 5.636 (cv 0.8%) | 5.344 (cv 1.2%) | −5% |

Read these as indicative, not as guarantees. What reuse saves is constructing the `Message` and
regrowing its field vectors on every parse: 0.11 μs on Heartbeat, a third of its parse, and 0.29 μs
on QuoteRequest, about 5%. Repeating groups are allocated afresh on every parse either way, so
pooling saves nothing per group. NewOrderSingle's −12% is inside twice its pooled cv, which one slow
repetition inflated, and resolves nothing. A difference smaller than roughly twice the cv is not
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
- **Queued messages a SequenceReset skips over are discarded**, in GapFill mode (`9c2865b9`) and in Reset mode (#8, `73f1c5a5`), instead of staying in the queue for good
- **Embedded SOH**: a message whose field value is cut short by an embedded SOH is rejected with a session-level Reject naming the field, instead of being dropped as garbled (`26aa506c`)
- **Out-of-order repeating group members**: rejected with the misplaced member named, instead of a misleading diagnosis (`74fe9320`)
- **FIX 4.2 QuoteAcknowledgement**: uses QuoteStatus (297), as `FIX42.xml` requires, instead of tag 1865, which FIX 4.2 does not define (`25b23432`)
- **Logon reset after verification**: `ResetSeqNumFlag=Y` and `ResetOnLogon` reset the sequence numbers only once `verify()` — and the application's `fromAdmin` — has accepted the Logon, and a reset or `setNextTargetMsgSeqNum` also clears the inbound queue and resend range (GHSA-jg3m-mc7p-8mww, `389a9d38`)
- **No reject before logon**: the string-reason `generateReject` refuses to send while not logged on, like the int-reason one, and formats Text the same way (GHSA-fgwx-rgmv-2fgh, `1ba01631`)
- **Canonical field metrics**: a field parsed from a non-canonical tag carries the length and checksum of what `appendTo()` writes, so copies of it serialise consistently (GHSA-4459-9vwq-69r6, `1d9a6fcd`)
- **SQL stores**: `MySQLStore` and `PostgreSQLStore` escape the message in their UPDATE fallback as in the INSERT (GHSA-ghf2-fj46-jv6p, `4a9628ac`)

### Build system
- **C++23**: Minimum standard raised from C++17; `CMAKE_CXX_STANDARD_REQUIRED=ON`. The code uses `std::flat_map` and `std::move_only_function`, so it needs GCC 15
- **CMake only**: the autotools build was retired (`3d832348`)
- **Linux only**: the Windows-specific sources, the MSVC precompiled header and their build wiring were removed (`4604c72d`, `6ff4a20f`)
- **Reformatted**: the whole tree follows `.clang-format` (BSD/Allman braces, 4-space indent, 120 columns), so upstream patches do not apply textually and are ported by hand (`7e48e4ff`, `98339fae`)
- **CMake 3.31**: Minimum version raised from 3.5
- **Conditional compilation**: MySQL, PostgreSQL, ODBC sources now conditionally compiled (matching SSL pattern) — no longer requires database headers when features are disabled
- **TBB allocator**: New `ENABLE_TBB_ALLOCATOR` CMake option with automatic `tbbmalloc` link dependency; the choice is recorded in the installed `QuickFIXBuildConfig.h`, so a consumer compiles against the library's allocator (`3a112309`)
- **Package files**: `cmake --install` ships a CMake package config (`quickfix::quickfix`) and `quickfix.pc` (`a19da887`)
- **HAVE_ODBC**: Moved from `add_definitions()` to `cmake_config.h.in` for consistency with MySQL/PostgreSQL
- **Removed**: Dead AIX/Solaris platform code, duplicate `configure_file` call
- **Generated code**: `spec/generate.sh` reproduces the checked-in tree (`d0214a62`), and the generated `FixFields.h` and `FixFieldNumbers.h` restore a caller's `ReplaceText` macro instead of losing it (`52d7a4f7`)
- **Bindings**: the Python and Ruby bindings build again from the current headers, and CI builds and tests both (`4cb3d655`, `68bb468f`, `d0ff75c7`, `96abec8a`)
- **Soname `libquickfix.so.21`**: the fork has changed the layout of exported classes (`FieldBase`, `Message`), so a binary built against upstream must not load this library. 19.0.0 changed the layout of `Parser`, `Session` and the connection classes again, 20.0.0 that of `Acceptor` and the threaded acceptors, and 21.0.0 changed no layout but the callback contract (`toApp` and `toAdmin` no longer see `MsgSeqNum`, #73), so a binary built against an earlier major must be rebuilt. Upstream's CMake build uses 17 and its autotools build `libquickfix.so.18`, which collided with this fork's 18.x where both were installed (#51); 19 and later no longer do. 18 also covered the untagged series before the first release, v18.0.0; from v18.0.0 on, any layout change to an exported class, or a change to the callback contract, bumps the major version in `project()` (top-level `CMakeLists.txt`), the one place the version is set — the soname, `QuickFIXVersion.h` and the Python module all take it from there

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
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHAVE_SSL=ON
cmake --build build -j$(nproc)
sudo cmake --install build
```

Always configure with `-DHAVE_SSL=ON` or `-DHAVE_SSL=OFF` as you intend: configure rewrites the
tracked `src/C++/config.h` from it. A program built against an SSL-enabled install can include the
SSL headers without defining `HAVE_SSL` itself; the generated `QuickFIXBuildConfig.h` records the
choice (#63).

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

Programs built against the installed headers get the matching allocator from the generated
`QuickFIXBuildConfig.h`; don't define `ENABLE_TBB_ALLOCATOR` yourself.

### Using the installed library

`cmake --install` ships a CMake package and a pkg-config file. The headers need C++23.

```cmake
find_package(quickfix 21 CONFIG REQUIRED)   # any 21.x: the major version is the soname
target_link_libraries(app PRIVATE quickfix::quickfix)
```

```bash
g++ -std=c++23 app.cpp $(pkg-config --cflags --libs quickfix)
```

A project that adds this tree with `add_subdirectory` or `FetchContent` links `quickfix::quickfix`
too; the target makes its headers available in the tree before anything that links it compiles
(#48). Keep `/usr/local/include` or another installed copy of quickfix off that project's include
path ahead of it, or the compiler can take an installed header that does not match the library it
links.

## Testing

```bash
# All three run from test/, through the ut, at and pt links the build creates there.
cd test

# Unit tests (Catch2)
./ut --quickfix-config-file cfg/ut.cfg --quickfix-spec-path ../spec

# Acceptance tests (Ruby): regenerates cfg/at.cfg, starts at and runs every definition.
# runat.sh begins with `killall ut at`, so don't run it beside another ut or at.
./runat.sh <port>

# Performance tests (pooled vs unpooled benchmarks included)
./pt -p <port> -c <count>
```

### Message pooling

For latency-critical applications, reuse `Message` objects instead of creating new ones per FIX string. `setString()` clears the message first, and clearing keeps the field vectors' capacity, so a reused message reallocates less. Repeating groups are still built afresh on every parse (#52):

```cpp
FIX::Message pooledMsg;
while (auto raw = receiveFromSocket()) {
  pooledMsg.setString(raw, false, &dataDictionary);
  processMessage(pooledMsg);
  // the next setString() clears pooledMsg; its field storage is kept for reuse
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
# -fno-sanitize-recover=undefined makes a UBSan finding fail the run: without it
# UBSan prints the runtime error and ut still exits 0, while the baseline above
# is "exits 0" (#65).
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DHAVE_SSL=ON \
  -DQUICKFIX_LIB_OUTPUT_DIR=build-asan/out
cmake --build build-asan -j$(nproc)
build-asan/out/ut --quickfix-config-file test/cfg/ut.cfg --quickfix-spec-path spec
```

The acceptance suite runs under the ASan build too: point `test/at` at `build-asan/out/at` and run
`./runat.sh 54321` and `./runat.sh 54321 -t`. Until #84 that could not report a leak — `at` never
returned from `main`, and SIGTERM's default action skips LeakSanitizer's at-exit check. `at` now
stops its acceptor and returns on SIGTERM, and `runat.sh` fails when `at` exits non-zero, so a leak
report, a crash in `Acceptor::stop()` or a shutdown that takes more than 30 seconds fails the run.
As of #84 all 470 definitions pass under ASan + UBSan in both transports, and `at` exits 0 — no
leak — after each; a leak injected into the same binary is reported and fails it.

A sanitizer build of `at` is slow to start — about 30 seconds under ASan or TSan for the reactor,
two minutes for the threaded transport under TSan — so `runat.sh` waits up to 300 seconds for `at`
to listen before running any definition. Before #85 it did not wait, and `Runner.rb` gives each
definition only 29 seconds to connect, so the first one or two definitions of a TSan run failed with
`Connection refused`. As of #85 all 470 pass under TSan (default allocator) in both transports,
with no report and `at` exiting 0.

#### Exercising the concurrent paths

`ut` under TSan barely drives what is concurrent in production — the reactor's
`select()` loop, the per-connection threads of the `Threaded*` transports, and the
session registry under `s_mutex`. Two runs cover those (this is issue #15):

- The acceptance suite against a TSan build of `at`, in both transports
  (`./runat.sh 54321` and `./runat.sh 54321 -t`, pointing `test/at` at the TSan
  binary). It drives one connection at a time.
- A concurrent-churn driver — many workers repeatedly connect, log on to the
  configured sessions and close (cleanly, with a reset, or mid-message) — which is
  what actually races connections against each other, the accept path and the
  registry. `test/churn.rb` is the driver and `test/runchurn.sh` runs `at` under it:
  `./runchurn.sh 54321 16 120` (16 workers for 120 seconds) or with `-t` after the
  seconds for the threaded transport, again with `test/at` pointing at the TSan
  binary. Like `runat.sh` it waits for `at` to listen and fails if `at` exits
  non-zero, which is how a sanitizer report surfaces; the driver itself fails only
  if `at` stops accepting connections. The driver behind the results below was never
  checked in; this one was written to the description above (#94).

As of this writing both are clean in both transports: the engine reported no data
race under either. The one race the churn driver found was in the acceptance
driver's own `MessageCracker` (a set shared across sessions), now guarded; the
engine was clean.

Re-run with the checked-in driver on 2026-10-10 (16 workers, 120 seconds, the 7
sessions of `cfg/at.cfg`): clean under TSan with the default allocator and under
ASan + UBSan, in both transports, with `at` exiting 0 every time — 143,000 to 167,000
connections per reactor run and 30,000 to 32,000 per threaded run. The threaded transport
completes fewer because `ThreadedSocketConnection::setSession` holds a connection
whose session is already logged on for up to 5 seconds, waiting for it to free up,
where the reactor refuses it at once; that is upstream's design, and the driver
gives up on a logon after one second so that it closes connections inside that
wait as well.

**Build the concurrent-path TSan run with the default allocator, not
`ENABLE_TBB_ALLOCATOR`.** `tbb::scalable_allocator` keeps its own per-thread
freelists, and TSan cannot see the synchronization in them, so under the threaded
transport — where one connection's freed message memory is handed to another
connection's thread — it reports races on recycled `FieldBase`/`std::string`
blocks that are not races. Measured on this tree: the churn driver produced 31
such reports under `ENABLE_TBB_ALLOCATOR=ON` and **zero** under the default
allocator, same code. (The `ut` TSan recipe above keeps `ENABLE_TBB_ALLOCATOR=ON`
because `ut` does not recycle memory across threads heavily enough to trip this,
and it stays clean; the heavily threaded run needs the default allocator to be
readable.) The run is slow — full acceptance twice under TSan — so it is a local
gate for transport and concurrency changes, not a per-push CI step.

## License

This fork is licensed under [AGPL-3.0](LICENSE). The original QuickFIX code remains under the QuickFIX Software License, Version 1.0, in [LICENSE-QuickFIX](LICENSE-QuickFIX) — upstream's file headers, which say that license appears "in the file LICENSE", refer to that text.

This product includes software developed by quickfixengine.org (http://www.quickfixengine.org/).

## Upstream

This is a fork of [quickfix/quickfix](https://github.com/quickfix/quickfix). It no longer merges from upstream; fixes from there are ported by hand when they are wanted. Send contributions here, fork-specific or not: a change submitted upstream does not reach this fork.

## Issues

Report bugs and request features on [GitHub Issues](https://github.com/dudleylane/quickfix/issues). Report security vulnerabilities privately instead, as [SECURITY.md](SECURITY.md) describes.
