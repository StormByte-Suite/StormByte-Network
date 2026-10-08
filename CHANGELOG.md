# Changelog

All notable changes to **StormByte-Network** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Summary]

StormByte Network is the C++26 networking layer of the StormByte suite.

It depends on [StormByte Base 2.0.0](https://github.com/StormByte-Suite/StormByte/releases/tag/2.0.0), [StormByte Buffer 2.0.0](https://github.com/StormByte-Suite/StormByte-Buffer/releases/tag/2.0.0), [StormByte Logger 2.0.0](https://github.com/StormByte-Suite/StormByte-Logger/releases/tag/2.0.0), and [StormByte System 2.0.0](https://github.com/StormByte-Suite/StormByte-System/releases/tag/2.0.0).

Inherit `Client` or `Server`, define packets, and attach Buffer pipelines.
IPv4 and IPv6, framed request/response, POSIX and Winsock stay behind the public API.

## [Unreleased]

[Unreleased]: https://github.com/StormByte-Suite/StormByte-Network/compare/2.0.0...HEAD

## [2.0.0] - 2026-10-08

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
    - Aligned Network exceptions with Base's `Path` hierarchy and `std::string_view` message inputs, keeping formatting in the caller module.
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
    - `PROCESS_THRESHOLD` to optionally pipeline payloads
    - `Frame` on-wire layout: opcode, payload size, payload
    - Pipeline processing on frame input/output when the threshold is met
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
