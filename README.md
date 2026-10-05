# StormByte-Network

![Multiplatform](https://img.shields.io/badge/Linux%20%7C%20Windows%20%7C%20macOS-Supported-1793D1)
![C++26](https://img.shields.io/badge/C%2B%2B-26-00599C?logo=c%2B%2B&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-3.28+-064F8C?logo=cmake&logoColor=white)
![License: LGPL v3 or commercial](https://img.shields.io/badge/License-LGPL_v3_or_commercial-blue.svg)
[![CI](https://github.com/StormByte-Suite/StormByte-Network/actions/workflows/ci.yml/badge.svg)](https://github.com/StormByte-Suite/StormByte-Network/actions/workflows/ci.yml)
[![Sponsor](https://img.shields.io/badge/Sponsor-GitHub-ea4aaa?logo=github-sponsors&logoColor=white)](https://github.com/sponsors/StormBytePP)

StormByte-Network is the C++26 networking module of the [StormByte](https://suite.stormbyte.org/StormByte) suite.

It depends on [StormByte Base 2.0.0](https://github.com/StormByte-Suite/StormByte/releases/tag/2.0.0) (or newer), [StormByte Buffer 2.0.0](https://github.com/StormByte-Suite/StormByte-Buffer/releases/tag/2.0.0) (or newer), [StormByte Logger 2.0.0](https://github.com/StormByte-Suite/StormByte-Logger/releases/tag/2.0.0) (or newer), and [StormByte System 2.0.0](https://github.com/StormByte-Suite/StormByte-System/releases/tag/2.0.0) (or newer).

It is not a thin socket wrapper. You inherit `Client` or `Server`, define packets, and attach Buffer pipelines. POSIX and Winsock, framing, event-driven I/O and bounded packet processing stay private.

## Table of Contents

- [Repository](#repository)
- [Documentation](#documentation)
- [Installation](#installation)
- [Why StormByte-Network](#why-stormbyte-network)
- [Features](#features)
- [Dependencies](#dependencies)
- [The rest of the suite](#the-rest-of-the-suite)
- [Public API](#public-api)
- [Examples](#examples)
	- [A client](#a-client)
	- [A server](#a-server)
	- [A packet](#a-packet)
- [Design notes](#design-notes)
- [Testing](#testing)
- [Contributing](#contributing)
- [License](#license)

## Repository

- [StormByte-Network](https://github.com/StormByte-Suite/StormByte-Network)

## Documentation

- This README: how to build, architecture, and examples.
- Doxygen class reference: [https://suite.stormbyte.org/StormByte-Network/](https://suite.stormbyte.org/StormByte-Network/).

## Installation

```bash
git clone https://github.com/StormByte-Suite/StormByte-Network.git
cd StormByte-Network
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build build -j
cmake --install build
```

`BUILD_SHARED_LIBS` defaults to `ON`; use `-DBUILD_SHARED_LIBS=OFF` for a static build. BuildMaster carries the selected mode through the bundled StormByte dependency graph. In static mode it flattens private dependency link requirements for consumers; no vendor repack is needed.

## Why StormByte-Network

| Goal | How it is achieved |
|------|--------------------|
| **Inherit, don't wrap sockets** | `Client` / `Server` are abstract application endpoints. |
| **Framed messages** | `Transport::Packet` + private `Frame` (opcode, size, payload). |
| **Buffer pipelines** | Optional processing of inbound/outbound payloads. |
| **IPv4 and IPv6** | `Connection::Protocol`. |
| **Cross-platform** | POSIX and Winsock behind `Socket` / `Handler`. |

## Features

- Abstract `Endpoint`, `Client`, `Server`
- Packet factory (`DeserializePacketFunction`)
- Connection status, read/write results
- Request/response (`Send`) and fire-and-forget (`Reply`)
- Server event loop with bounded packet-handler workers
- Per-session in-flight ordering, bounded output buffering and backpressure handling
- Optional payload processing for opcodes ≥ `Packet::PROCESS_THRESHOLD`
- Authorized, file-like remote readers and exclusive writers over private TCP channels

## Dependencies

| Dependency | Required Version | Role |
|------------|------------------|------|
| [StormByte (base)](https://github.com/StormByte-Suite/StormByte/releases/tag/2.0.0) | [2.0.0](https://github.com/StormByte-Suite/StormByte/releases/tag/2.0.0) | Expected, BinaryData, Size, ByteSize, visibility |
| [StormByte-Buffer](https://github.com/StormByte-Suite/StormByte-Buffer/releases/tag/2.0.0) | [2.0.0](https://github.com/StormByte-Suite/StormByte-Buffer/releases/tag/2.0.0) | FIFO, Pipeline, Consumer, ReadOnly/WriteOnly |
| [StormByte-Logger](https://github.com/StormByte-Suite/StormByte-Logger/releases/tag/2.0.0) | [2.0.0](https://github.com/StormByte-Suite/StormByte-Logger/releases/tag/2.0.0) | Diagnostics |
| [StormByte-System](https://github.com/StormByte-Suite/StormByte-System/releases/tag/2.0.0) | [2.0.0](https://github.com/StormByte-Suite/StormByte-System/releases/tag/2.0.0) | Calling-thread utilities |

## The rest of the suite

| Module | Role | API |
| --- | --- | --- |
| [Base](https://github.com/StormByte-Suite/StormByte) | Exceptions, Expected, serialization, UUID, concepts, safe text and pointer abstractions | [/StormByte](https://suite.stormbyte.org/StormByte) |
| [Buffer](https://github.com/StormByte-Suite/StormByte-Buffer) | FIFO, SharedFIFO, Ring, Producer/Consumer and multi-stage pipelines | [/StormByte-Buffer](https://suite.stormbyte.org/StormByte-Buffer) |
| [Config](https://github.com/StormByte-Suite/StormByte-Config) | Human-readable text and versioned binary documents (groups, lists, raw bytes) | [/StormByte-Config](https://suite.stormbyte.org/StormByte-Config) |
| [Crypto](https://github.com/StormByte-Suite/StormByte-Crypto) | Hash, compression, encryption, signatures and key agreement; Crypto++ remains private | [/StormByte-Crypto](https://suite.stormbyte.org/StormByte-Crypto) |
| [Database](https://github.com/StormByte-Suite/StormByte-Database) | One SQL API over SQLite, PostgreSQL, MariaDB and MSSQL | [/StormByte-Database](https://suite.stormbyte.org/StormByte-Database) |
| [Logger](https://github.com/StormByte-Suite/StormByte-Logger) | Stream logging with levels, headers, components, `ThreadedLog` and `Scope` | [/StormByte-Logger](https://suite.stormbyte.org/StormByte-Logger) |
| [Multimedia](https://github.com/StormByte-Suite/StormByte-Multimedia) | Decode, filter, encode and mux media through FFmpeg; public APIs abstract FFmpeg types | [/StormByte-Multimedia](https://suite.stormbyte.org/StormByte-Multimedia) |
| **Network** | Framed packets, client/server endpoints, IPv4/IPv6 TCP, Buffer pipelines and authorized remote-file channels | [/StormByte-Network](https://suite.stormbyte.org/StormByte-Network) |
| [System](https://github.com/StormByte-Suite/StormByte-System) | Processes, pipes, devices, host information and environment variables | [/StormByte-System](https://suite.stormbyte.org/StormByte-System) |

## Public API

Under `StormByte::Network`:

| Type | Role |
|------|------|
| `Client` | Inherit; call `Send`; install negotiated pipelines once per connection |
| `Server` | Inherit; implement `ProcessClientPacket` |
| `Transport::Packet` | Inherit; implement `DoSerialize` |
| `Connection::Protocol` | IPv4 / IPv6 |
| `Connection::Status` | Lifecycle |
| `Exception` / `ConnectionError` / `ConnectionClosed` | Errors |
| `Telemetry` / `ClientTelemetry` / `ServerTelemetry` | Base-clock measurements and retained per-client / aggregate-server snapshots |
| `RemoteFileMount` | Public, versioned descriptor/result for an application-authorized file mount |
| `BufferedRemoteFileReader` / `BufferedRemoteFileWriter` | Buffer file-like handles over private channels |

Sockets, frames and Winsock bootstrap are private.

Public polymorphic and callback types are `MAYBE_SAFE`: safe ownership does not make arbitrary derived state or captures safe across modules. Keep Base, Network, relevant dependencies and every provider module loaded with matching ABIs until all objects and callback copies are released. Allocate the exact derived packet type with `PacketPointer::MakePointer<Derived>()`, retain its virtual destruction path, and keep derived payload allocation/destruction in its provider module. Use `StormByte::Safe::DynamicPointerCast` or `StormByte::Safe::StaticPointerCast` for packet casts. Derived servers must call `Disconnect()` in their destructor body before handler state is destroyed. Safe string sizes count content bytes (or wide characters), excluding the null terminator.

### Network telemetry

`Client::Telemetry()` returns `StormByte::Safe::Shared<ClientTelemetry>` for that client instance. It records connection attempts/results and application `Send` request/response/no-response counts with mean round-trip latency. A no-response result is not necessarily a transport failure: applications may intentionally use one-way commands. `Server::Telemetry()` returns `StormByte::Safe::Shared<ServerTelemetry>` aggregated across all application sessions handled by that server. It records current, accepted, closed and peak connections; packets dispatched; handlers completed, empty and errored; mean handler duration; and worker-queue backpressure events. Counters are cumulative for the telemetry object's lifetime; connection count is a live gauge.

Keep either shared handle to retain its last snapshot after the owning `Client` or `Server` is destroyed. Timed operations use independent, stable `StormByte::Clock::Sample` values sharing named clocks from `StormByte::Telemetry`; nested or concurrent measurements do not depend on thread-local nesting depth, and a sample can move between threads. Repeated `Stop()` calls do not record it again. Remote-file byte/read/write statistics remain on Buffer's existing per-handle telemetry and are not folded into the application-protocol counters.

### Negotiated pipelines and admission

Network provides the transport pieces without imposing a handshake, login opcode or cryptographic algorithm. Connections start with empty, no-op pipelines. A derived client installs a negotiated pair through protected `ConfigurePipelines(input, output)`; a derived server calls `ConfigureClientPipelines(uuid, input, output)` for the relevant session. This replaces automatic `InputPipeline()` / `OutputPipeline()` factory overrides. The pair is installed only once and atomically: a failed template clone leaves configuration unchanged and retryable, while a second successful installation is rejected. Creating the first file plane seals configuration, even when no-op pipelines are used. Reconnection creates a new configurable session.

Configure a client between `Send` calls with externally serialized access. Configure the server from that session's packet handler or `OnClientConnected(uuid)`, not an unrelated thread. File planes receive independent copies of immutable configuration templates, not the advanced state of a running control pipeline. Provider lifetimes and independent cryptographic key/nonce domains remain the application's responsibility.

Protected `AllowIncomingOpcode(uuid, opcode)` and `AllowOutgoingOpcode(uuid, opcode)` accept all opcodes by default. A derived policy can allow only Hello before negotiation, only login messages before authentication, and authorized application messages afterward. Incoming checks run before transformation and the packet factory; outgoing checks run before encoding. A rejection closes the affected connection. Server hooks run on the event loop and must synchronize state shared with worker handlers without blocking on network exchanges. `OnClientConnected(uuid)` admits and initializes transport state; `OnClientDisconnected(uuid)` removes it, including at shutdown. Late workers must not recreate authorization for a disconnected UUID.

Frames following a handshake remain raw and unprocessed while its handler is active, even when multiple messages arrived in the same TCP read. Only after the previous handler completes are the next opcode's policy, pipeline and factory evaluated using the updated state. Unknown opcodes, invalid payloads and factory exceptions fail closed; clients disconnect on an undecodable response rather than retaining uncertain protocol state.

Application opcodes below `Transport::Packet::PROCESS_THRESHOLD` (10) bypass payload pipelines in both directions. Higher opcodes use the configured stages; opcode and length headers remain visible. A plaintext Hello with a public key can use the lower range, but those opcodes still require an application allowlist and field validation. Authenticate key establishment and peer identity: sending a public key alone does not prevent MITM. Real key agreement uses a private local key and the remote public key, not two public keys alone.

To return a rejection message before closing, a handler calls protected `DisconnectClientAfterReply(uuid)` and returns its application-defined rejection packet. Network discards pending input without executing its pipelines, factory or handlers, drains the response, then closes the socket. `DisconnectClient(uuid)` closes immediately without guaranteeing a reply. Draining completes local sends; it does not prove the remote application read them. Use an application ACK when that confirmation is required.

[test/handshake_test.cxx](test/handshake_test.cxx) is an executable example of `Hello("pubkey_contents")`, a trusted-key policy, `Welcome`/`Rejected`, a random test secret and the algorithm label `"xor"`. It verifies per-UUID isolation, configuration once, clone-failure retry, reconnection, coalesced messages and negotiated file transfers. **It is not cryptographic security:** public keys are labels, the secret travels in plaintext and XOR provides no authentication or meaningful confidentiality. Use vetted authenticated cryptography before adapting it to real secrets or credentials. The hardcoded login fixture in [test/client_server_test.cxx](test/client_server_test.cxx) likewise demonstrates authorization only, not production credential handling.

### Remote file channels

The derived server performs its normal application-opcode dispatch and ACL check first. After authorization it calls the protected `MountRemoteFileReader` or `MountRemoteFileWriter` helper and includes the returned `RemoteFileMount` in its application response. A denied ACL can instead return an application-defined `Unauthorized` packet. The client's packet factory decodes the public DTO, then its derived client calls `CreateRemoteFileReader` or `CreateRemoteFileWriter`.

`RemoteFileReaderHandle` and `RemoteFileWriterHandle` are `StormByte::Safe::Unique` owners of their concrete remote types. They can be moved into consumer functions accepting `Safe::Unique<Buffer::IO::BufferedLocationReader>` or `Safe::Unique<Buffer::IO::BufferedLocationWriter>` without moving or slicing the underlying object. Construction stays in Network; object storage is allocated and freed on Base's heap, and virtual destruction releases the remote mount. These handles do not convert to `std::unique_ptr` with its default deleter. This handle-type change requires consumers to rebuild.

`RemoteFileMount::Status` distinguishes `Authorized`, `NotAuthorized`, `Unavailable` (a missing read file), `FileBeingRead`, `FileBeingWritten`, and `Failed`. Rejected descriptors have `Port() == 0`, an empty capability token, and `Access::None`. The private channel has its own bounded opcode protocol and is not sent through the application's packet factory or `ProcessClientPacket`.

The first authorized mount for an application client session creates one private server listener and assigns its ephemeral port. Later mounts for that session reuse the same peer plane and port. The client connects lazily when it creates its first reader or writer. Each `Authorized` descriptor carries its own capability token and access mode, along with the plane's heartbeat timeout. The first authorized mount selects that timeout (3 to 3600 seconds; default 30), and later mounts on the same plane use it. A server allows at most 128 active mount capabilities across all client sessions.

The heartbeat timeout measures peer liveness, not disk-operation duration. Ping/Pong continues while a disk worker is occupied; a synchronous file call may remain pending beyond that interval while the peer keeps responding. Initial attachment remains time-bounded. File operations are serialized, with a heartbeat exchange allowed alongside them and one socket receiver correlating responses by request ID. Frames, pending work and response queues remain bounded; responses retain their pipeline transformation order without interleaving bytes. The receiver sleeps when no response is outstanding.

File transfers use independent copies of the input/output templates installed by `ConfigurePipelines` or `ConfigureClientPipelines` for that application session. Each endpoint creates a separate pipeline pair for its peer's file plane, rather than reusing the running application connection's pipeline instances. Every private request and response passes through these pipelines, including file bytes, capability tokens, operation metadata and Ping/Pong; this is independent of the application packet opcode threshold. The framing length prefix remains outside the pipelines, so message sizes and timing are still observable.

**Transport encryption is transparent to file contents.** A client write is transformed by its output pipeline, decoded by the server's input pipeline and only then written as the original bytes to the host file. A server read obtains the original disk bytes, transforms the response and lets the client's input pipeline restore those bytes before returning them through Buffer. Network does not store transport ciphertext or protocol framing in the file and does not provide encryption at rest. With correctly paired authenticated-encryption stages, applications therefore use normal buffered reads, writes, seeks and flushes while the file messages travel encrypted. The negotiated file-plane test verifies binary data including NUL and all byte values, actual transformation activity, exact on-disk length and byte-for-byte plaintext contents.

The file plane inherits the protection implemented by those pipelines; Network does not add encryption automatically. Correctly implemented authenticated encryption, with authenticated peer/key establishment and replay protection, can protect file contents and capabilities against sniffing and tampering. Encryption alone does not authenticate the peer or prevent replay. Protect the application response carrying the mount descriptor too: a capability exposed on the control connection is not made secret retroactively by encrypting the file plane. Empty pipelines provide no cryptographic protection, and the reversible transforms used in tests are not security mechanisms.

Each peer plane has one pipeline pair and one shared network-device snapshot. Construct each `Buffer::Pipe` from a copyable callable taking `const PipeInput&`, `const PipeOutput&` and `const Safe::Shared<Logger::Log>&`; close or fail the output before returning and do not retain these borrowed arguments. Copies independently clone the callable and its value captures; references and shared handles still share their targets. Providers must supply valid independent clone/release operations, own or safely share captured state, and avoid raw endpoint references. Cryptographic pipes must use independent per-plane and per-direction state, or derived keys and distinct nonce domains: copying a cipher counter or nonce state must never reuse a key/nonce pair across the application connection and file planes. Reject unauthenticated input before it reaches file-operation dispatch. File operations carry the mount token and an absolute byte offset, so reader handles keep independent cursors without wire-level seek requests.

Remote readers and writers retain Buffer's I/O telemetry, including its existing operation counters and rates. Buffer telemetry derives from `StormByte::Telemetry` and measures operations with Base's named clocks, so remote I/O uses the same instrumentation as other Buffer locations.

Remote reader size queries return `StormByte::Safe::Optional<StormByte::ByteSize>`: an empty value means the size is unavailable, not that the file is empty; a present zero means an empty file.

The remote reader and writer expose the same shared polymorphic `System::Device` snapshot for their plane's local network interface. Its `Throughput()` and `Window()` report the interface link speed when the operating system exposes it, with a nominal network-rate fallback otherwise.

Readers of one canonical path share a mutex-protected host-side file handle; operations on it are serialized, while each reader keeps its own offset. A path may have multiple readers or one exclusive writer, but a writer mount is rejected while any reader or writer holds the path, and readers are rejected while a writer holds it. Mounting a writer does not truncate an existing file; a missing file is created, and `Truncate()` explicitly clears it. `Close` consumes that mount's token. The backing handle and path reservation are released when its final token closes; request a new mount to open another handle.

The data plane is owned by the `Client` object and survives `Client::Disconnect()` while that object remains alive, so already attached file handles can continue I/O without the application control connection. A subsequent successful `Connect()` replaces the old plane and invalidates its handles. Destroying the `Client`, a plane heartbeat timeout, or server shutdown also closes the plane and releases its tokens. The descriptor is a capability, not an authorization decision: every data operation revalidates the token and access. The private protocol bounds payloads and validates opcodes and sequence numbers. A failed plane marks every attached `BufferedLocation*` handle `Fault` so later I/O fails visibly.

Heartbeat failure during disk I/O revokes the plane's capabilities, stops new work and wakes failed client buffers. Results completed after revocation are discarded. A system call already in progress may finish and partially modify a file: its contents must then be treated as unreliable, with no rollback guarantee. Its worker retains the stream until the operation returns, allowing safe deferred release. Transport cancellation does not guarantee immediate interruption of that call or bounded shutdown time while the operating system keeps it blocked.

`DeserializePacketFunction` accepts copyable callables. Its target storage and clone/destroy trampolines stay in the caller's module, so Network can retain and invoke the decoder without freeing callback memory through its own CRT. Copies own independently copied targets; reference/shared captures remain shared and must stay valid for every retained copy.

## Examples

### A client

```cpp
#include <StormByte/network/client.hxx>
#include <utility>

class AppClient : public StormByte::Network::Client {
public:
	AppClient(StormByte::Network::DeserializePacketFunction fn,
	          StormByte::Safe::Shared<StormByte::Logger::Log> log)
		: Client(std::move(fn), std::move(log)) {}

};
```

### A server

```cpp
#include <StormByte/network/server.hxx>

class AppServer : public StormByte::Network::Server {
public:
	using Server::Server;
	~AppServer() noexcept override { Disconnect(); }

protected:
	StormByte::Network::PacketPointer ProcessClientPacket(
		std::string_view uuid,
		StormByte::Network::PacketPointer packet) noexcept override {
		(void)uuid;
		return packet;
	}
};
```

### A packet

```cpp
#include <StormByte/network/transport/packet.hxx>

class PingPacket : public StormByte::Network::Transport::Packet {
public:
	PingPacket() : Packet(1) {}

protected:
	StormByte::BinaryData DoSerialize() const noexcept override {
		return {};
	}
};

StormByte::Network::PacketPointer MakePingPacket() {
	return StormByte::Network::PacketPointer::MakePointer<PingPacket>();
}
```

## Design notes

- Application request/response calls on one connection are not a thread-safe multiplex. The private remote-file plane separately multiplexes heartbeat with serialized file operations. The server keeps socket I/O in its event loop and dispatches packet handlers through a bounded internal pool.
- Client request/response APIs remain synchronous to the caller; a slow packet handler no longer blocks socket I/O for unrelated clients.
- A slow peer is isolated by per-session output limits; once a session exceeds its output budget, the server closes that session rather than allowing unbounded memory growth.
- `Connect` on `Server` means bind + listen + accept loop.
- Frame layout uses host `size_t` for payload length. Same architecture on both ends.
- Pipelines run only when the opcode is at or above `PROCESS_THRESHOLD`.

## Testing

Enable tests in CMake (`ENABLE_TEST`) and run CTest from the build tree.

## Contributing

Issues on GitHub. No wiki, no discussions.

## License

StormByte-Network original source is dual-licensed under the GNU Lesser General Public License v3.0 or later, or a commercial license from the copyright holder (David C. Manuelda, StormBytePP).

See [LICENSE](LICENSE), [COPYING.LGPLv3](COPYING.LGPLv3), and <https://www.gnu.org/licenses/lgpl-3.0.html>. These licenses do not cover bundled StormByte modules or other third-party material under `thirdparty/`; each keeps its own license. Neither license grants patent rights.
