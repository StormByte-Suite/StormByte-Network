# StormByte-Network

![Multiplatform](https://img.shields.io/badge/Linux%20%7C%20Windows%20%7C%20macOS-Supported-1793D1)
![C++26](https://img.shields.io/badge/C%2B%2B-26-00599C?logo=c%2B%2B&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-3.28+-064F8C?logo=cmake&logoColor=white)
![License: LGPL v3 or commercial](https://img.shields.io/badge/License-LGPL_v3_or_commercial-blue.svg)
[![CI](https://github.com/StormByte-Suite/StormByte-Network/actions/workflows/ci.yml/badge.svg)](https://github.com/StormByte-Suite/StormByte-Network/actions/workflows/ci.yml)
[![Sponsor](https://img.shields.io/badge/Sponsor-GitHub-ea4aaa?logo=github-sponsors&logoColor=white)](https://github.com/sponsors/StormBytePP)

StormByte-Network is the C++26 networking module of the [StormByte](http://suite.stormbyte.org/StormByte) suite.

It depends on [StormByte Base 1.1.0](https://github.com/StormByte-Suite/StormByte/releases/tag/1.1.0) (or newer), [StormByte Buffer 1.1.0](https://github.com/StormByte-Suite/StormByte-Buffer/releases/tag/1.1.0) (or newer), [StormByte Logger 1.1.0](https://github.com/StormByte-Suite/StormByte-Logger/releases/tag/1.1.0) (or newer), and [StormByte System 2.0.0](https://github.com/StormByte-Suite/StormByte-System/releases/tag/2.0.0) (or newer).

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
- Doxygen class reference: [http://suite.stormbyte.org/StormByte-Network/](http://suite.stormbyte.org/StormByte-Network/).

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
| [Base](https://github.com/StormByte-Suite/StormByte) | Exceptions, Expected, serialization, UUID, concepts, safe text and pointer abstractions | [/StormByte](http://suite.stormbyte.org/StormByte) |
| [Buffer](https://github.com/StormByte-Suite/StormByte-Buffer) | FIFO, SharedFIFO, Ring, Producer/Consumer and multi-stage pipelines | [/StormByte-Buffer](http://suite.stormbyte.org/StormByte-Buffer) |
| [Config](https://github.com/StormByte-Suite/StormByte-Config) | Human-readable text and versioned binary documents (groups, lists, raw bytes) | [/StormByte-Config](http://suite.stormbyte.org/StormByte-Config) |
| [Crypto](https://github.com/StormByte-Suite/StormByte-Crypto) | Hash, compression, encryption, signatures and key agreement; Crypto++ remains private | [/StormByte-Crypto](http://suite.stormbyte.org/StormByte-Crypto) |
| [Database](https://github.com/StormByte-Suite/StormByte-Database) | One SQL API over SQLite, PostgreSQL, MariaDB and MSSQL | [/StormByte-Database](http://suite.stormbyte.org/StormByte-Database) |
| [Logger](https://github.com/StormByte-Suite/StormByte-Logger) | Stream logging with levels, headers, components, `ThreadedLog` and `Scope` | [/StormByte-Logger](http://suite.stormbyte.org/StormByte-Logger) |
| [Multimedia](https://github.com/StormByte-Suite/StormByte-Multimedia) | Decode, filter, encode and mux media through FFmpeg; public APIs abstract FFmpeg types | [/StormByte-Multimedia](http://suite.stormbyte.org/StormByte-Multimedia) |
| **Network** | Framed packets, client/server endpoints, IPv4/IPv6 TCP, Buffer pipelines and authorized remote-file channels | [/StormByte-Network](http://suite.stormbyte.org/StormByte-Network) |
| [System](https://github.com/StormByte-Suite/StormByte-System) | Processes, pipes, devices, host information and environment variables | [/StormByte-System](http://suite.stormbyte.org/StormByte-System) |

## Public API

Under `StormByte::Network`:

| Type | Role |
|------|------|
| `Client` | Inherit; implement pipelines; call `Send` |
| `Server` | Inherit; implement `ProcessClientPacket` |
| `Transport::Packet` | Inherit; implement `DoSerialize` |
| `Connection::Protocol` | IPv4 / IPv6 |
| `Connection::Status` | Lifecycle |
| `Exception` / `ConnectionError` / `ConnectionClosed` | Errors |
| `RemoteFileMount` | Public, versioned descriptor/result for an application-authorized file mount |
| `BufferedRemoteFileReader` / `BufferedRemoteFileWriter` | Buffer file-like handles over private channels |

Sockets, frames and Winsock bootstrap are private.

### Remote file channels

The derived server performs its normal application-opcode dispatch and ACL check first. After authorization it calls the protected `MountRemoteFileReader` or `MountRemoteFileWriter` helper and includes the returned `RemoteFileMount` in its application response. A denied ACL can instead return an application-defined `Unauthorized` packet. The client's packet factory decodes the public DTO, then its derived client calls `CreateRemoteFileReader` or `CreateRemoteFileWriter`.

`RemoteFileMount::Status` distinguishes `Authorized`, `NotAuthorized`, `Unavailable` (a missing read file), `FileBeingRead`, `FileBeingWritten`, and `Failed`. Rejected descriptors have `Port() == 0`, an empty capability token, and `Access::None`. The private channel has its own bounded opcode protocol and is not sent through the application's packet factory or `ProcessClientPacket`.

The first authorized mount for an application client session creates one private server listener and assigns its ephemeral port. Later mounts for that session reuse the same peer plane and port. The client connects lazily when it creates its first reader or writer. Each `Authorized` descriptor carries its own capability token and access mode, along with the plane's heartbeat timeout. The first authorized mount selects that timeout (3 to 3600 seconds; default 30), and later mounts on the same plane use it. A server allows at most 128 active mount capabilities across all client sessions.

Each peer plane has one cloned `InputPipeline()` / `OutputPipeline()` pair and one shared network-device snapshot. `Pipe::Clone`/`Move` implementations must own or safely share their state and must not retain raw references to the endpoint. Stateful cryptographic pipes need independent plane state/nonces while using the application's negotiated secret/configuration. File operations carry the mount token and an absolute byte offset, so reader handles keep independent cursors without wire-level seek requests.

Remote readers and writers retain Buffer's I/O telemetry, including its existing operation counters and rates. Buffer telemetry derives from `StormByte::Telemetry` and measures operations with Base's named clocks, so remote I/O uses the same instrumentation as other Buffer locations.

The remote reader and writer expose the same shared polymorphic `System::Device` snapshot for their plane's local network interface. Its `Throughput()` and `Window()` report the interface link speed when the operating system exposes it, with a nominal network-rate fallback otherwise.

Readers of one canonical path share a mutex-protected host-side file handle; operations on it are serialized, while each reader keeps its own offset. A path may have multiple readers or one exclusive writer, but a writer mount is rejected while any reader or writer holds the path, and readers are rejected while a writer holds it. Mounting a writer does not truncate an existing file; a missing file is created, and `Truncate()` explicitly clears it. `Close` consumes that mount's token. The backing handle and path reservation are released when its final token closes; request a new mount to open another handle.

The data plane is owned by the `Client` object and survives `Client::Disconnect()` while that object remains alive, so already attached file handles can continue I/O without the application control connection. A subsequent successful `Connect()` replaces the old plane and invalidates its handles. Destroying the `Client`, a plane heartbeat timeout, or server shutdown also closes the plane and releases its tokens. The descriptor is a capability, not an authorization decision: every data operation revalidates the token and access. The private protocol bounds payloads and validates opcodes and sequence numbers. A failed plane marks every attached `BufferedLocation*` handle `Fault` so later I/O fails visibly.

`DeserializePacketFunction` accepts copyable callables. Its target storage and clone/destroy trampolines stay in the caller's module, so Network can retain and invoke the decoder without freeing callback memory through its own CRT.

## Examples

### A client

```cpp
#include <StormByte/network/client.hxx>
#include <utility>

class AppClient : public StormByte::Network::Client {
public:
	AppClient(StormByte::Network::DeserializePacketFunction fn,
	          StormByte::Shared<StormByte::Logger::Log> log)
		: Client(std::move(fn), std::move(log)) {}

protected:
	StormByte::Buffer::Pipeline InputPipeline() const noexcept override {
		return {};
	}
	StormByte::Buffer::Pipeline OutputPipeline() const noexcept override {
		return {};
	}
};
```

### A server

```cpp
#include <StormByte/network/server.hxx>

class AppServer : public StormByte::Network::Server {
public:
	using Server::Server;

protected:
	StormByte::Buffer::Pipeline InputPipeline() const noexcept override { return {}; }
	StormByte::Buffer::Pipeline OutputPipeline() const noexcept override { return {}; }

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
```

## Design notes

- One connection is not a thread-safe multiplex. The server keeps socket I/O in its event loop and dispatches packet handlers through a bounded internal pool.
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
