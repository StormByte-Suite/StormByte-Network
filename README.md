# StormByte-Network

![Multiplatform](https://img.shields.io/badge/Linux%20%7C%20Windows%20%7C%20macOS-Supported-1793D1)
![C++26](https://img.shields.io/badge/C%2B%2B-26-00599C?logo=c%2B%2B&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-3.28+-064F8C?logo=cmake&logoColor=white)
![License: LGPL v3 or commercial](https://img.shields.io/badge/License-LGPL_v3_or_commercial-blue.svg)
[![CI](https://github.com/StormBytePP/StormByte-Network/actions/workflows/ci.yml/badge.svg)](https://github.com/StormBytePP/StormByte-Network/actions/workflows/ci.yml)
[![Sponsor](https://img.shields.io/badge/Sponsor-GitHub-ea4aaa?logo=github-sponsors&logoColor=white)](https://github.com/sponsors/StormBytePP)

StormByte-Network is the C++26 networking module of the [StormByte](https://dev.stormbyte.org/StormByte) suite.

It depends on [StormByte Base 1.1.0](https://github.com/StormBytePP/StormByte/releases/tag/1.1.0) (or newer), [StormByte Buffer 1.1.0](https://github.com/StormBytePP/StormByte-Buffer/releases/tag/1.1.0) (or newer), and [StormByte Logger 1.1.0](https://github.com/StormBytePP/StormByte-Logger/releases/tag/1.1.0) (or newer).

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

- [StormByte-Network](https://github.com/StormBytePP/StormByte-Network)

## Documentation

- This README: how to build, architecture, and examples.
- Doxygen class reference: [https://dev.stormbyte.org/StormByte-Network/](https://dev.stormbyte.org/StormByte-Network/).

## Installation

```bash
git clone https://github.com/StormBytePP/StormByte-Network.git
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
| [StormByte (base)](https://github.com/StormBytePP/StormByte/releases/tag/2.0.0) | [2.0.0](https://github.com/StormBytePP/StormByte/releases/tag/2.0.0) | Expected, BinaryData, Size, ByteSize, visibility |
| [StormByte-Buffer](https://github.com/StormBytePP/StormByte-Buffer/releases/tag/2.0.0) | [2.0.0](https://github.com/StormBytePP/StormByte-Buffer/releases/tag/2.0.0) | FIFO, Pipeline, Consumer, ReadOnly/WriteOnly |
| [StormByte-Logger](https://github.com/StormBytePP/StormByte-Logger/releases/tag/2.0.0) | [2.0.0](https://github.com/StormBytePP/StormByte-Logger/releases/tag/2.0.0) | Diagnostics |
| [StormByte-String](https://github.com/StormBytePP/StormByte-String/releases/tag/1.0.0) | [1.0.0](https://github.com/StormBytePP/StormByte-String/releases/tag/1.0.0) | Owned UTF-8 text |
| [StormByte-System](https://github.com/StormBytePP/StormByte-System/releases/tag/2.0.0) | [2.0.0](https://github.com/StormBytePP/StormByte-System/releases/tag/2.0.0) | Calling-thread utilities |

## The rest of the suite

| Module | Role | API |
| --- | --- | --- |
| [Base](https://github.com/StormBytePP/StormByte) | Exceptions, Expected, serialization, strings, UUID, concepts | [/StormByte](https://dev.stormbyte.org/StormByte) |
| [Buffer](https://github.com/StormBytePP/StormByte-Buffer) | FIFO, SharedFIFO, Ring, Producer/Consumer and multi-stage pipelines | [/StormByte-Buffer](https://dev.stormbyte.org/StormByte-Buffer) |
| [Config](https://github.com/StormBytePP/StormByte-Config) | Human-readable text and versioned binary documents (groups, lists, raw bytes) | [/StormByte-Config](https://dev.stormbyte.org/StormByte-Config) |
| [Crypto](https://github.com/StormBytePP/StormByte-Crypto) | Hash, compress, encrypt, sign and key agreement — Crypto++ never leaves the private tree | [/StormByte-Crypto](https://dev.stormbyte.org/StormByte-Crypto) |
| [Database](https://github.com/StormBytePP/StormByte-Database) | One API over SQLite, PostgreSQL and MariaDB | [/StormByte-Database](https://dev.stormbyte.org/StormByte-Database) |
| [Logger](https://github.com/StormBytePP/StormByte-Logger) | Stream logger with levels, headers, human-readable sizes and redaction (`ThreadedLog`) | [/StormByte-Logger](https://dev.stormbyte.org/StormByte-Logger) |
| [Multimedia](https://github.com/StormBytePP/StormByte-Multimedia) | Decode, encode and containers without raw FFmpeg types; codecs enabled only if present | [/StormByte-Multimedia](https://dev.stormbyte.org/StormByte-Multimedia) |
| **Network** | This repository | [/StormByte-Network](https://dev.stormbyte.org/StormByte-Network) |
| [String](https://github.com/StormBytePP/StormByte-String) | Owned UTF-8 and wide text | [/StormByte-String](https://dev.stormbyte.org/StormByte-String) |
| [System](https://github.com/StormBytePP/StormByte-System) | Processes, pipes and environment variables across Linux, Windows and macOS | [/StormByte-System](https://dev.stormbyte.org/StormByte-System) |

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

The first authorized mount on a `Client` opens one peer data plane. All of that client's reader and writer leaves register capabilities on the same socket and share one heartbeat, pipeline pair and network-device snapshot. File frames carry the mount token and an absolute offset; reader cursors stay independent without wire seeks. The server keeps one synchronized host handle per canonical path and releases it when the final token closes.

`InputPipeline()` and `OutputPipeline()` are cloned once per peer plane. `Pipe::Clone`/`Move` implementations must own or safely share their state and must not retain raw references to the endpoint. Stateful cryptographic pipes need independent plane state/nonces while using the application's negotiated secret/configuration.

The remote reader and writer expose the same shared polymorphic `System::Device` snapshot for their plane's local network interface. Its `Throughput()` and `Window()` report the interface link speed when the operating system exposes it, with a nominal network-rate fallback otherwise.

Readers of one path share the host-side handle but each operation uses its token's absolute offset, so their logical cursors remain independent. A path may have only one writer, and a writer mount is rejected while readers or another writer hold a mount. A reader mount is rejected while a writer holds it. `Close` releases that token; the final token closes the backing handle and releases the path reservation.

The data plane is owned by the `Client` object and deliberately survives `Client::Disconnect()` while that object remains alive, matching the established detached-handle behavior without leaving one socket per file. Destroying the `Client`, a plane heartbeat timeout, or server shutdown closes the plane and releases all of its tokens. The descriptor is a capability, not an authorization decision: every data operation revalidates the token and access. The private protocol bounds payloads, validates opcodes/sequence numbers, and uses one configurable heartbeat timeout per plane (3 to 3600 seconds; default 30). A dead plane marks every attached `BufferedLocation*` handle `Fault` so later I/O fails visibly.

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
