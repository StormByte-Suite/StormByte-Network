# Changelog

All notable changes to **StormByte-Network** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

[Unreleased]: https://github.com/StormByte-Suite/StormByte-Network/compare/4.0.0...HEAD

## [4.0.0] - 2026-11-10

### Changed

- Expose Buffer 4's inherited read chunks, universal write-transfer limits,
  page-drain thresholds, coherent live configuration and transfer snapshots
  on remote-file handles; document effective limits and memory accounting.
- Require Buffer 4.0.0 and Crypto 3.1.0; bundled builds use their published
  release archives.
- Migrate remote-file leaves to Buffer's Parameters constructors while
  preserving automatic network-window probing.

### Fixed

- Segment remote reads larger than a protocol frame instead of faulting when
  the configured ReadChunk exceeds the wire payload limit.
- Report each acknowledged remote write segment to Buffer before attempting
  another frame, preserving confirmed progress if a later exchange fails.
- Cover configured direct, prefetched, page-cached and ring transfers in
  plaintext and TLS tests, including aggregation, live capacity reduction,
  protocol segmentation and sub-threshold Flush/Close tails.

### Removed

- **Breaking:** binary compatibility with remote-file handles built against
  Buffer 3, including virtual policy overrides inherited from Buffer.

[4.0.0]: https://github.com/StormByte-Suite/StormByte-Network/compare/3.2.2...4.0.0

## [3.2.2] - 2026-11-09

### Changed

- Update the bundled Crypto dependency to 3.0.2, which fixes TLS test certificate creation with OpenSSL 4.x while preserving OpenSSL 3.x compatibility.

[3.2.2]: https://github.com/StormByte-Suite/StormByte-Network/compare/3.2.1...3.2.2

## [3.2.1] - 2026-11-06

### Added

- Add caller-owned TLS diagnostic outputs to HTTP requests, application-client connections and client TLS configuration. Preserve Crypto's specific failure explanation through connection cleanup without changing public object layouts or error enumerations.

### Changed

- Require Crypto 3.0.1 for TLS diagnostics; bundled builds download its tagged release archive.
- Log detailed TLS failures for application, server and remote-file transports, including incremental handshakes and authenticated I/O.

[3.2.1]: https://github.com/StormByte-Suite/StormByte-Network/compare/3.2.0...3.2.1

## [3.2.0] - 2026-11-04

### Added

- Expose the accepted peer's numeric IP address and source port through `PeerAddress` and the protected `Server::GetPeerAddress(uuid)` accessor.
- Make installed Network packages discoverable with `find_package(StormByte COMPONENTS Network)`.
- Stream HTTP response bodies through an optional borrowed `HTTP::Receiver` (`Options::receiver`) over HTTP/1.1 and HTTP/2, for downloads with progress and without buffering the body in memory. `Begin` receives the final status and fields (including Content-Length) before any body byte, and `Write` receives every chunk in order; `Response::body` stays empty and trailers are still returned. `body_bytes` bounds the streamed total. A receiver that declines or throws ends the request with the new `Error::Rejected`; the existing cancellation flag and deadline keep working.

### Changed

- Bundled builds use StormByte Base, Buffer, Crypto, Logger and System 3.0.0; require BuildMaster 2.1.5.

[3.2.0]: https://github.com/StormByte-Suite/StormByte-Network/compare/3.1.0...3.2.0

## [3.1.0] - 2026-11-02

### Added

- Add typed `Client::Connect(..., std::chrono::milliseconds)` and protected `Send(packet, std::chrono::milliseconds)` overloads returning `Expected<..., Client::Error>`, with distinct timeout, cancellation, resolver-busy, TLS, transport, packet and precondition failures. Negative budgets are invalid, zero expires immediately and `milliseconds::max()` explicitly opts out.
- Add thread-safe, sticky application-stream `Cancel()`. Native shutdown wakes blocked plaintext/TLS I/O on POSIX; Windows waits observe cancellation within their bounded 50 ms slice because Winsock `select` does not report a local shutdown; serialized operation owners release failed streams. Connect/request operations serialize without mixing responses, while queued operations can expire under their own original deadline. Preserve moves, reconnects, telemetry and independent remote-file lifetime.

### Changed

- Bundled builds use the refreshed StormByte Base 2.2.3, Buffer 2.0.6, Crypto 2.3.1, Logger 2.0.6 and System 2.1.3 releases, including their final audit fixes and corrected bundled TLS trust-store initialization.
- Application connects use the existing bounded managed resolver and nonblocking connection scheduler. One total budget covers DNS caller waiting, connection attempts and configurable TLS authentication; the legacy Boolean connect defaults to ten seconds total. The legacy packet-handle send remains unlimited and preserves empty-on-error behavior.
- Investigate the reported approximately one-second loopback round trip with measured plaintext/TLS probes and add a 128-ping-per-mode latency regression. The pre-change baseline was already 115–878 µs plaintext and 27–207 µs TLS (five samples each), so no unconfirmed polling cause or latency fix is claimed.
- Archive client-control release/sanitizer evidence and reproduction scripts under `doc/logs/client-control`. Confirm six focused sanitizer suites pass with the released Clang/libstdc++ providers and, separately, GCC with unchanged source-built Base. Document the GCC/Clang constrained-template mangling mismatch rather than misidentifying it as missing release exports.

### Fixed

- Serialize native socket shutdown against close, so cancellation can wake I/O without racing handle reuse. Remove the unconditional 100 ms delay on socket disconnection; shutdown followed by close does not require a sleep.
- Share one absolute request deadline across upload, opcode, size, payload and TLS partial I/O instead of restarting waits on progress. Timeout, cancellation, truncated frames and callback failures discard the affected stream without replaying packets.
- Synchronize client connection publication/status, make `Disconnect()` wake an active legacy or timed request, support cancellation/disconnection from packet callbacks without deadlock, and clean up streams on exceptional exits.
- Fix two Windows CI failures in the new client-control tests; socket behavior is unchanged. The interrupt test asserted the POSIX-only wake of an uncancelled wait by a local shutdown; it now follows `Cancel()` (flag, then shutdown) on every platform, expects Winsock's `WSAESHUTDOWN` receive result after `SD_BOTH`, and keeps the uncancelled wake as a POSIX-only case. The 100 ms backpressure test spent its budget on non-preemptible 16 MiB framing before any socket write (about 0.1 s on Linux; the Windows CI request exceeded its 0.5 s bound). It now uses a fixed 16 KiB peer receive window and a 2 s budget, so expiry occurs while waiting for send capacity, and requires `TimedOut` within 2–2.4 s.

[3.1.0]: https://github.com/StormByte-Suite/StormByte-Network/compare/3.0.0...3.1.0

## Known limits

- OS DNS already in progress cannot be cancelled. A timed-out/cancelled lookup
  occupies its client's single managed slot until completion, and destruction
  joins it, potentially beyond the operation budget.
- Synchronous application callbacks and pipeline work cannot be preempted;
  deadlines/cancellation are checked on their return. Moves/destruction still
  require external synchronization with all active client calls.

## [3.0.0] - 2026-11-01

### Added

- Add the public, synchronous `HTTP::Client` with Safe-owned binary requests,
  duplicate-preserving response headers/trailers and typed errors. Support
  HTTP/1.1 (including HTTP/1.0 responses) and actual nghttp2 HTTP/2 over plain
  HTTP or Crypto TLS 1.3 HTTPS, with ALPN and explicit cleartext h2 prior knowledge.
- Reuse managed Network DNS, sockets and controlled Crypto scheduling for one
  total request deadline/cancellation across resolution, connect, authentication,
  upload and complete response. Bound URL, headers, fields, body, wire bytes and
  informational responses and native nghttp2 session allocations; validate
  framing, chunks, bodyless statuses,
  truncation, injection, HTTP/2 resets/GOAWAY and flow-controlled DATA.
- Deep-copy HTTP custom CA/identity, revocation, policy and mTLS inputs; validate
  at current time per session by default, with explicit fixed-time test opt-in.
  Keep one fresh connection per request, return 3xx unchanged, and perform no
  retries, redirects, automatic decompression, proxying or protocol downgrade.
  No HTTP server, HTTP/3, QUIC, libcurl or new dependency is introduced.
- Add deterministic loopback HTTP/HTTPS HTTP/1 and HTTP/2 regression tests,
  including sizable byte-exact h2 flow-control transfers and disposable offline
  client-authentication TLS fixtures.
- Add public pre-connect `ConfigureTLS`/`DisableTLS` with validated, deeply
  copied Crypto TLS identity, custom roots, policy, ALPN, revocation evidence
  and optional mTLS inputs. Retain configuration across reconnects and moves.
  Refresh validation time for each new session by default; an explicit
  `refresh_validation_time=false` opts into the supplied fixed instant for
  deterministic offline or fixed-time revocation-evidence validation.
  Plaintext remains supported only when TLS is unconfigured; configured
  connections fail closed with no plaintext fallback.
- Add `WITH_NGHTTP2=BUNDLED|SYSTEM`; bundled builds use the source submodule
  pinned to v1.70.0, while an explicitly missing system library is an error.
- Add a private TLS socket adapter driven by StormByte-Crypto, with authenticated
  handshake, ALPN, controlled partial I/O, truncation detection and orderly
  shutdown under one total deadline/cancellation policy.
- Add offline loopback TLS regression tests without direct OpenSSL linkage.
- Add opt-in private total-deadline/cancellation sends with retained partial-write
  progress and explicit partial-read data/would-block/EOF/error outcomes.
- Add a managed single-slot private resolver, explicit IPv4/IPv6 alternative
  address policies and total-deadline non-blocking connection fallback. Native
  DNS remains uncancellable; timed-out work stays owned until joined cleanup.

### Changed

- **Breaking:** all packet payloads now pass through input and output pipelines
  uniformly, regardless of opcode. Existing packet opcodes and wire framing
  are unchanged.
- Require StormByte-Crypto 2.3.0 for TLS integration, using its published binary
  release in bundled builds and the Crypto component in system builds.
- Advance server TLS handshakes and record I/O incrementally without blocking
  the event loop, cap pending primary handshakes at 64 and enforce ten-second
  handshake deadlines.
- Replace the client/server XOR fixture with real offline Crypto TLS.
- Allow 90 seconds for the complete HTTP test suite on native-policy platforms
  and report per-case durations, without relaxing individual operation deadlines.
- Flush HTTP per-case diagnostics before CI timeouts and report native
  resolution failures and macOS TLS fixture policy rejections in CI.

### Removed

- **Breaking:** remove public `Packet::PROCESS_THRESHOLD`.
- **Breaking:** remove protected `Client::ConfigurePipelines` and
  `Server::ConfigureClientPipelines`. Derived consumers, including
  Alchemist/Alchemy and other applications, must migrate and rebuild; their
  application packets, opcodes, serialization and handlers are unchanged.
- Remove the obsolete application-handshake suite and README example.

### Fixed

- Resolve numeric IPv4/IPv6 addresses without name-service lookup and give
  Windows select-based event loops enough capacity to preserve the 64-peer TLS
  handshake limit.
- Use nghttp2's portable signed-size API for HTTP/2 on Windows.
- Limit disposable TLS leaf certificates to 365 days for macOS native policy.
- Release connection-refusal fixture ports before connecting instead of using
  `127.0.0.2` or leaving a bound non-listening socket.
- Separate TLS cancellation from expiry and check resolver cancellation
  independently of thread scheduling latency.
- Accept an explicit transport reset as a rejected mTLS connection in HTTP tests.

### Security

- Protect all remote-file auxiliary traffic with independent TLS sessions
  inheriting primary configuration, including file bytes, tokens, framing,
  metadata, control operations and heartbeat/release exchanges.
- Admit callbacks and packet dispatch only after authenticated success;
  authenticate file channels before token processing.
- Keep bundled nghttp2 archive symbols hidden inside Network rather than
  re-exporting the private engine from the shared library.

[3.0.0]: https://github.com/StormByte-Suite/StormByte-Network/compare/2.0.5...3.0.0

## [Summary]

StormByte Network is the C++26 networking layer of the StormByte suite.

It depends on [StormByte Base](https://github.com/StormByte-Suite/StormByte/releases/tag/2.2.2) 2.2.2, [StormByte Buffer](https://github.com/StormByte-Suite/StormByte-Buffer/releases/tag/2.0.5) 2.0.5, [StormByte Logger](https://github.com/StormByte-Suite/StormByte-Logger/releases/tag/2.0.5) 2.0.5, and [StormByte System](https://github.com/StormByte-Suite/StormByte-System/releases/tag/2.1.2) 2.1.2. Each is its own archive. This repository is not Base, Buffer, Config, Crypto, Database, Logger, Multimedia or System.

Inherit `Client` or `Server`, define packets, and optionally configure Crypto TLS.
IPv4 and IPv6, framed request/response, POSIX and Winsock stay behind the public API.

[Unreleased]: https://github.com/StormByte-Suite/StormByte-Network/compare/3.0.0...HEAD

## [2.0.5] - 2026-10-28

### Changed

- Bundled builds use Base 2.2.2, Buffer 2.0.5, Logger 2.0.5 and System 2.1.2. Require BuildMaster 2.1.3 and pin its bundled submodule to the published release.

[2.0.5]: https://github.com/StormByte-Suite/StormByte-Network/compare/2.0.4...2.0.5

## [2.0.4] - 2026-10-24

### Changed

- Bundled third-party StormByte dependencies now require the matching patch releases (Base 2.2.1, Buffer 2.0.4, Logger 2.0.4, System 2.1.1 where applicable) so a bundled build picks up the /arch:AVX change.


### Changed

- Windows Release optimizations use `/arch:AVX` instead of `/arch:AVX2`. The previous baseline required a Haswell-or-newer CPU (AVX2). `/arch:AVX` runs on Sandy Bridge and Ivy Bridge (including the i7-3770K) and later.

[2.0.4]: https://github.com/StormByte-Suite/StormByte-Network/compare/2.0.3...2.0.4

## [2.0.3] - 2026-10-23

### Changed

- Updated StormByte Base, Buffer, Logger and System to their latest releases. Each dependency target now declares its own link dependencies, following Buffer's component-linkage pattern; Network keeps separate direct links to the libraries it uses.
- Bumped the project version to 2.0.3.

[2.0.3]: https://github.com/StormByte-Suite/StormByte-Network/compare/2.0.2...2.0.3

## [2.0.2] - 2026-10-19

### Added

- `ENABLE_PGO` and `ENABLE_LTO`. Profile-guided optimization trains in `pgo-instr` and installs this build. Link-time optimization follows `ENABLE_LTO` (default ON) for every build type. Both apply to Network. Base, Buffer, Logger and System are not rebuilt.
- Release archives include `THIRD-PARTY`. No third-party object code is compiled into this shared library. Base, Buffer, Logger and System are linked at run time and keep their own notices. `COPYING.*` is copied into the zip when it is present.
- macOS CI and the release archive are Apple Clang only. The file is `macos-arm64`, with no standard-library suffix. Linux still ships `libstdc++` and `libc++`. Windows has no runtime suffix.

### Changed

- CI builds with `ENABLE_LTO=ON`, `ENABLE_PGO=OFF` and `ENABLE_TEST=ON`, then runs ctest. Profile-guided optimization stays on the release workflow.
- Requires [StormByte Base](https://github.com/StormByte-Suite/StormByte/releases/tag/2.1.0) 2.1.0, [StormByte Buffer](https://github.com/StormByte-Suite/StormByte-Buffer/releases/tag/2.0.2) 2.0.2, [StormByte Logger](https://github.com/StormByte-Suite/StormByte-Logger/releases/tag/2.0.2) 2.0.2 and [StormByte System](https://github.com/StormByte-Suite/StormByte-System/releases/tag/2.0.2) 2.0.2. Each is a release archive (`stormbyte_component_url`) linked with `BACKEND=binary`. Base is declared here: Buffer no longer carries it as a submodule. The archive matches this OS and C++ runtime. Doxygen reads headers from `${BUILDMASTER_INSTALL_INCLUDEDIR}` after those archives unpack. BuildMaster is >= 2.1.0.
- **Breaking**: Network exceptions replace literal `{}` with `Safe::String::ReplaceToken`. They no longer call `std::format`, so a specifier such as `{:04x}` is not a token. A number uses `std::to_string` (`true` becomes `1`). An empty extra path is `StormByte.Network`, not `StormByte.Network.`.
- Client and server telemetry text uses `ReplaceToken` instead of `std::format`.
- Release builds no longer pass `/fp:fast`. `OPTIMIZATION_FLAGS_{LINUX,MAC,WINDOWS}` append `-O2` / `/O2` and the CPU baseline only when the caller did not already set flags.
- Bumped the project version to 2.0.2.

### Fixed

- `-stdlib=libc++` and `-stdlib=libstdc++` do not count as custom C++ flags. If either is the only extra flag, Release still gets `-O2` and the CPU baseline.
- MSVC PGO no longer puts `/LTCG:PGINSTRUMENT` or `/LTCG:PGOPTIMIZE` in `CMAKE_EXE_LINKER_FLAGS`. Those flags are `add_link_options` on this project. A nested `project()` inherits the cache and its compiler test links `cmTC_*.exe`, which was never instrumented, so `/LTCG:PGOPTIMIZE` aborted with LNK1266 (`cmTC_*.pgd` missing). The cache keeps a bare `/LTCG` so those `/GL` objects still link. A static dependency stays LTCG and is optimized by the link that owns the `.pgd`.
- `HandshakeTest` includes `<sys/time.h>` before using `timeval`. musl does not declare that type through `<sys/socket.h>`.
- Clang PGO drops `.profraw` files left by compiler probes before ctest, and `llvm-profdata merge` uses `-failure-mode=all`. A probe writes a few hundred bytes for `main`, with a padded empty symbol name. The default failure mode is `any`, so that one file aborted the merge even after the instrumented tests had passed.

[2.0.2]: https://github.com/StormByte-Suite/StormByte-Network/compare/2.0.1...2.0.2

## [2.0.1] - 2026-10-13

### Changed

- Updated dependency pins for BuildMaster and StormByte Buffer, and bumped the project version to 2.0.1.

[2.0.1]: https://github.com/StormByte-Suite/StormByte-Network/compare/2.0.0...2.0.1

## [2.0.0] - 2026-10-10

### Added

- **Client/server telemetry**
    - Added Base-clock telemetry snapshots scoped per Client and aggregated across all Server sessions, using independent, stable `Clock::Sample` values for nested and concurrent measurements without thread/depth coupling; samples support thread transfer and idempotent Stop.
    - Return telemetry through `Safe::Shared` handles that remain valid after the owning endpoint is destroyed.
- **Authorized remote file channels**
    - Added the public, validated `RemoteFileMount` descriptor and application-facing protected mount/attach helpers.
    - Added file-like buffered remote readers and exclusive writers over one bounded peer data plane per Client, with per-operation capabilities and offsets, independent reader cursors, shared-reader/exclusive-writer path reservations, one heartbeat and shared NIC snapshot per peer, and token-scoped CloseToken cleanup.
    - Copy negotiated input/output pipeline templates per peer data plane using Buffer's callable `Pipe` API with independently copied callable state; add dedicated authorization, conflict, deterministic file I/O, transformed framing, concurrent reader, and application-disconnect coverage. Transport transformations are transparent to file contents: readers return original bytes and writers store decoded raw data, never transport ciphertext or framing. Binary regression coverage verifies exact on-disk length and all byte values after negotiation.
    - Apply the configured endpoint pipelines to all private requests and responses, including capabilities and heartbeat. Preserve the order of transformed response frames for stateful stages and reject failed pipeline output. Document that cryptographic protection depends on authenticated encryption, peer/key establishment, replay protection and independent key/nonce domains; Network does not supply encryption automatically.

### Changed

- **StormByte Base 2.0 compatibility**
    - Migrated public and private header state to Base 2.0 Safe containers, shared/unique ownership, synchronization, atomics and threads, including `Safe::Binary` payloads in place of removed `BinaryData`. Standard borrowed views and scalars remain permitted; no `std::shared_ptr` ownership bridge is used. String sizes exclude the null terminator.
    - Distinguished `Size` element counts from `ByteSize` byte lengths and offsets, with explicit numeric conversions at protocol and OS boundaries; retained optional remote sizes so unknown and known zero remain distinct.
    - Aligned Network exceptions with Base's `Path` hierarchy and `std::string_view` message inputs, keeping formatting in the caller module. Removed the competing `Safe::String` message overload and restricted format constructors to calls with arguments, avoiding GCC ambiguity for literals and dynamic text. Callers convert Safe text explicitly to a view; plain messages retain their leaf prefixes and literal braces. Convert Safe socket-error text to a view before formatting with libstdc++.
    - Remote file leaves retain Buffer telemetry, whose counters derive from `StormByte::Telemetry` and use Base's named clocks.
- **Build, ABI and packaging**
    - Ported the library to BuildMaster's in-process CMake backend with selectable shared/static builds; static consumers receive the bundled components' private link closure without vendor repacking.
    - Moved heap-owning public/private lifecycle operations, frame payload operations, and exported exception RTTI anchors out of headers to keep DLL allocation and destruction inside their owning modules.
    - Replaced the deserializer's `std::function` storage with caller-allocated callback trampolines so the Network DLL never allocates, clones, or frees the callback target across CRTs; callback copies own independently copied targets, while reference/shared captures retain their sharing semantics.
    - Propagated the selected shared/static mode to the bundled Buffer component instead of forcing it shared in Network's wrapper.
    - Replaced hidden Client backend and Server event-loop owning STL state with explicit Safe state. Reserve PIMPL for native OS resources or external-library state, not Safe values or a workaround for header ownership rules.
    - Store retained client and server endpoint addresses in Base-owned `Safe::String` values instead of module-CRT `std::string` buffers.
- **Suite integration and documentation**
    - Updated repository and documentation links for the StormByte-Suite move, refreshed the suite catalog, removed retired StormByte-String references, and switched suite documentation links, tag downloads and tag mappings to verified HTTPS endpoints.
    - Doxygen (`ENABLE_DOC`) resolves Buffer, Logger, System and Base headers via `INCLUDE_PATH` and skips `thirdparty`; completed full multiline public-header documentation, including lifecycle and provider obligations. Updated the coding style with current Safe contracts and the suite's alphabetically grouped `test_snake_case` convention, with identical definition and registration banners. No dependency pin change.
- **Public API and licensing**
    - Replace automatic endpoint pipeline factory overrides with protected one-time `Client::ConfigurePipelines` and per-UUID `Server::ConfigureClientPipelines`. New connections start no-op; control and file planes use the negotiated configuration with independent templates. Add incoming/outgoing opcode-policy filters, session lifecycle hooks and server-initiated closure after a rejection reply, discarding pending unprocessed frames.
    - Ported Network APIs and payload handling to the StormByte 2.0 types and Buffer interfaces, including `Safe::Binary`, `ByteSize`, `string_view`, `Safe::Shared<Log>`, and callable `Pipe` stages; replaced polymorphic Pipe clone/move hooks with independent callable copies and synchronous borrowed endpoints.
    - PacketPointer now uses `Safe::Shared<Transport::Packet>` with `MakePointer<Derived>()` factories and Safe pointer casts, preserving exact derived allocation and virtual destruction. Public lifecycle operations and `MAYBE_SAFE` declarations document provider-owned resources, compatible ABIs and module residency; derived servers must disconnect before destroying handler state.
    - Use the exact `STORMBYTE_DECLARE_MAYBE_SAFE` macro for complete public and appropriate private types, with implemented or explicitly deleted lifecycle operations and valid moved-from cleanup. The declaration does not certify arbitrary derived state. Keep callbacks provider-owned and caller-sensitive construction or owning STL conversions `STORMBYTE_FORCE_INLINE`; do not attach Doxygen blocks to macro invocations.
    - Remote file handles now use `Safe::Unique`, allowing ownership transfer to `BufferedLocationReader`/`BufferedLocationWriter` consumers without slicing or relocating the remote leaf. Storage is allocated and freed on Base's heap; virtual destruction releases mount tokens. Remote reader sizes use `Safe::Optional<ByteSize>` to distinguish an unavailable size from a present zero. The handle-type change replaces Network's custom deleters and requires consumers to rebuild.
    - Network's original source is now dual-licensed under LGPL-3.0-or-later or a commercial license; third-party and bundled module licenses remain separate.
- **Integration coverage**
    - Add a separate, documented handshake/XOR example covering trusted-key rejection, actual pipeline execution after acceptance, per-client secrets, clone-failure retry, single installation, reconnection, coalesced frames and binary file transfers. Fictitious keys, XOR and hardcoded login credentials demonstrate application protocol extension, not cryptographic security. Add unknown-opcode, corrupted-payload and incompatible-transform factory-failure/disconnection tests on both endpoints.
    - Expanded integration coverage for failed-connect/listen retry, server restart, malformed and truncated peer frames, and concurrent clients issuing repeated requests; added nested/concurrent telemetry, repeated Stop, cross-thread sample transfer, destructor cleanup and exact-derived lifetime coverage.
    - Remote writer lifecycle tests distinguish incomplete writes, failed flushes, unexpected file sizes, and the first mismatching byte offset to diagnose platform-specific visibility failures.
    - Registered fourteen public Safe lifecycle suites covering endpoints, client/server, packets, decoder callbacks, exceptions, telemetry and mount descriptors, alongside integration coverage for callback copy/move, exact-derived destruction and remote reader/writer lifecycle. No private-class suites are registered. These tests do not alone establish separate Windows CRT heap safety, and their presence is not a claim of passing validation on every platform.

### Fixed

- **Closed-connection exception messages**
    - Pass formatted Base-owned text as a borrowed string view when adding the connection-closed prefix, preserving message bytes and embedded nulls instead of formatting `Safe::String` as a character range.
- **Application packet admission and decoding**
    - Delay input pipeline and factory execution until the previous handler completes and the current opcode policy accepts the raw frame. Factory exceptions/null results fail closed, including client disconnection on invalid replies. Preserve encoded-response order for stateful pipelines; server rejection replies drain before closure without processing queued messages.
- **Remote capability close validation**
    - Validate CloseToken offset, value and body before changing capability ownership. Malformed close requests no longer detach a live token while leaving its file handle and path reservation retained; the capability remains usable until a valid close. Added regressions for malformed close fields, revoked-token reuse, foreign/forged tokens and attempts to mutate files through read-only capabilities.
- **Remote-file heartbeat during disk I/O**
    - Service Ping/Pong independently of a pending file operation, so disk I/O taking longer than the heartbeat interval no longer fails the peer plane merely because its response is delayed. A single client receiver correlates operation and heartbeat responses; frames remain serialized, pending work and response queues stay bounded, and the receiver sleeps when no response is outstanding.
    - Missing heartbeat revokes the plane, wakes waiting callers and faults attached buffers. Late worker responses are discarded; a stream retained by already executing I/O is released when that operation returns. Interrupted writes may leave partial contents, which must be treated as unreliable; cancellation does not promise rollback or immediate interruption of a system call.
- **Remote writer size and flush**
    - Flush the writable host stream before querying filesystem size without switching the bidirectional stream's get cursor, avoiding stale size results and size-probe interference with subsequent writes and flushes. Added repeated write/size/flush byte-integrity coverage and operation/transport failure diagnostics that do not expose capability tokens or file contents.
- **Worker task initialization**
    - Explicitly initialized the optional worker operation for packet tasks and every event-loop result field on POSIX and Windows, eliminating Clang's missing-field-initializer warnings without changing task or event behavior.
- **Windows build and socket compatibility**
    - Included `<ws2tcpip.h>` in the server socket implementation when inspecting bound IPv6 addresses.
    - Propagated Windows network system libraries (`ws2_32`, `iphlpapi`) via BuildMaster `LINK=` option string in [lib/CMakeLists.txt](lib/CMakeLists.txt).

[2.0.0]: https://github.com/StormByte-Suite/StormByte-Network/compare/1.1.0...2.0.0

## [1.1.0] - 2026-09-13

### Changed

- Updated dependencies to [StormByte Base 1.1.0](https://github.com/StormByte-Suite/StormByte/releases/tag/1.1.0), [StormByte Logger 1.1.0](https://github.com/StormByte-Suite/StormByte-Logger/releases/tag/1.1.0), and [StormByte Buffer 1.1.0](https://github.com/StormByte-Suite/StormByte-Buffer/releases/tag/1.1.0).
- **Server and socket concurrency**
    - Reduced non-blocking socket wait overhead by waiting for readability/writability only when the system call reports backpressure.
    - Added private incremental server-side frame parsing while preserving the existing client/server API and worker behavior.
    - Added a portable accept-loop wakeup channel so server shutdown does not tear down the listener to interrupt a wait.
    - Replaced per-client workers with one event-loop thread for session parsing, packet processing, and synchronous replies.
    - Added a bounded private packet worker pool with per-session in-flight control and EventLoop completions.
    - Routed worker-originated client/server disconnect requests through EventLoop commands without worker self-joins.
    - Added bounded per-session output streaming with non-blocking writes and POLLOUT/select-driven draining.
- **Integration and lifecycle coverage**
    - Expanded client/server integration coverage with empty-payload, text-echo, and numeric-sum commands.
    - Added disconnect/reconnect, slow-handler, concurrent-client, pending-task shutdown, and late-completion coverage.
    - Documented client/server lifecycle invariants and the Windows `FD_SETSIZE` boundary.

### Fixed

- Avoided passing unsupported `/GL` and `/LTCG` flags to clang-cl Release builds by using CMake's interprocedural optimization setting.
- Fixed macOS polling portability when `POLLRDHUP` is unavailable.
- Fixed the Windows test target's Winsock linkage using a consistent CMake link signature.

- Guarded polymorphic large-data packet handling against unexpected packet types.

[1.1.0]: https://github.com/StormByte-Suite/StormByte-Network/releases/tag/1.1.0

## [1.0.0] - 2026-09-05

Initial public release of StormByte-Network.

### Added

- **Endpoint abstraction** (`Client` / `Server`) for inheritance-oriented application protocols
    - Pluggable `DeserializePacketFunction` for domain packet construction
    - Overridable `InputPipeline()` / `OutputPipeline()` (`Buffer::Pipeline`) for compression, encryption, etc.
    - Request/response via framed `Transport::Packet` (`Send` / `Reply`)
- **Client**
    - Connect over IPv4/IPv6 (`Connection::Protocol`)
    - Status query and clean disconnect
- **Server**
    - Listen/accept loop on a dedicated thread
    - Per-client worker threads and UUID-keyed client map
    - Pure virtual `ProcessClientPacket()` for application logic
    - Safe shutdown (stop accept, join workers, disconnect peers)
- **Transport layer**
    - `Packet` base: opcode + `DoSerialize()` payload hook
    - `Frame` on-wire layout: opcode, payload size, payload
- **Sockets**
    - Cross-platform TCP client/server (POSIX + Windows Winsock)
    - Non-blocking I/O, configurable SO_SNDBUF/SO_RCVBUF, TCP_NODELAY
    - Chunked send/receive with timeouts and exact-size `ReceiveInto`
    - `WaitForData` via epoll (Linux), poll (other UNIX), WSA events (Windows)
    - Peer shutdown detection, peek, and lightweight ping
    - `Reader` / `Writer` adapters implementing `Buffer::ExternalReader` / `ExternalWriter`
- **Connection helpers**
    - Singleton `Handler` (WSAStartup/cleanup, last-error helpers)
    - `Info` hostname resolution and sockaddr metadata
    - Connection status and read/write result enums with string helpers
- **Exceptions**: `ConnectionError`, `ConnectionClosed`, `PacketError`, `FrameError`
- CMake build, tests, LGPL-3.0-or-later

### Notes

- `Client` and `Server` are designed to be **subclassed**, not used as generic drop-in types without derivation.
- Public API surface is stable for the 1.x series; private socket/connection types remain implementation details.

[1.0.0]: https://github.com/StormByte-Suite/StormByte-Network/releases/tag/1.0.0
