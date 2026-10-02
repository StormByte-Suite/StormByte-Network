# Changelog

All notable changes to **StormByte-Network** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Summary]

StormByte Network is the C++26 networking layer of the StormByte suite.

It depends on [StormByte Base 1.1.0](https://github.com/StormByte-Suite/StormByte/releases/tag/1.1.0), [StormByte Logger 1.1.0](https://github.com/StormByte-Suite/StormByte-Logger/releases/tag/1.1.0), and [StormByte Buffer 1.1.0](https://github.com/StormByte-Suite/StormByte-Buffer/releases/tag/1.1.0).

Inherit `Client` or `Server`, define packets, and attach Buffer pipelines.
IPv4 and IPv6, framed request/response, POSIX and Winsock stay behind the public API.

## [Unreleased]

### Added

- Added Base-clock telemetry snapshots for each Client and aggregate Server, with shared handles that can outlive their endpoints.
- **Authorized remote file channels**
    - Added the public, validated `RemoteFileMount` descriptor and application-facing protected mount/attach helpers.
    - Added file-like buffered remote readers and exclusive writers over one bounded peer data plane per Client, with per-operation capabilities and offsets, independent reader cursors, shared-reader/exclusive-writer path reservations, one heartbeat and shared NIC snapshot per peer, and token-scoped CloseToken cleanup.
    - Clone one input/output pipeline pair per peer data plane; add dedicated authorization, conflict, deterministic file I/O, transformed framing, concurrent reader, and application-disconnect coverage.

### Changed

- Aligned Network exceptions with Base's `Path` hierarchy and `std::string_view` message inputs, keeping formatting in the caller module.
- Updated repository and documentation links for the StormByte-Suite move, refreshed the suite catalog, and removed retired StormByte-String references.
- Migrated Network to Base 2.0's `Safe::Shared`, `Safe::String` and `Safe::WString` APIs and the current UUID return type. Remote file leaves continue to use Buffer telemetry, whose counters derive from `StormByte::Telemetry` and use Base's named clocks.
- Doxygen (`ENABLE_DOC`) resolves Buffer, Logger and Base headers via `INCLUDE_PATH` and skips `thirdparty`. No dependency pin change.
- Ported the library to BuildMaster's in-process CMake backend with selectable shared/static builds; static consumers receive the bundled components' private link closure without vendor repacking.
- Ported Network APIs and payload handling to the StormByte 2.0 types and Buffer interfaces, including `BinaryData`, `ByteSize`, `string_view`, `Shared<Log>`, and `ReadOnly`/`WriteOnly` pipes.
- Network's original source is now dual-licensed under LGPL-3.0-or-later or a commercial license; third-party and bundled module licenses remain separate.
- Expanded integration coverage for failed-connect/listen retry, server restart, malformed and truncated peer frames, and concurrent clients issuing repeated requests.
- Moved heap-owning public/private lifecycle operations, frame payload operations, and exported exception RTTI anchors out of headers to keep DLL allocation and destruction inside their owning modules.
- Replaced the deserializer's `std::function` storage with caller-allocated callback trampolines so the Network DLL never allocates, clones, or frees the callback target across CRTs.
- Propagated the selected shared/static mode to the bundled Buffer component instead of forcing it shared in Network's wrapper.

### Fixed

- Fixed missing `<ws2tcpip.h>` header in Windows server socket implementation when inspecting bound IPv6 addresses.
- Propagated Windows network system libraries (`ws2_32`, `iphlpapi`) via BuildMaster `LINK=` option string in [lib/CMakeLists.txt](lib/CMakeLists.txt).

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

[Unreleased]: https://github.com/StormByte-Suite/StormByte-Network/compare/1.1.0...HEAD
[1.1.0]: https://github.com/StormByte-Suite/StormByte-Network/releases/tag/1.1.0
[1.0.0]: https://github.com/StormByte-Suite/StormByte-Network/releases/tag/1.0.0
