# Client/Server Invariants

This document records the observable behavior that the internal server redesign must preserve.

## Public Contract

- `Client`, `Server`, `Endpoint`, packet factories, pipelines, `Send`, and `Reply` keep their legacy signatures; Network 3.1 adds typed, timed client overloads and `Cancel()`.
- Client request/response calls remain synchronous to their caller.
- `ProcessClientPacket(client_uuid, packet)` remains synchronous from the handler's point of view and keeps its current signature.
- A response is sent only after the request packet has been fully received, deserialized, and processed.

## Application Client Operations

- One absolute `steady_clock` deadline covers serialized-operation admission, DNS caller waiting, TCP connect alternatives and TLS authentication, or the complete send/frame/read/transform/deserialize request path. Neither dribbled headers nor partial payload/TLS progress restarts it.
- Negative budgets are invalid, zero expires before starting I/O and `milliseconds::max()` means unlimited. Legacy Boolean connect uses ten seconds total; legacy packet-handle send remains unlimited.
- Timeout and cancellation are distinct `Client::Error` values, not empty response packets. EOF or an incomplete wire frame is a transport failure. Packet policy/transformation/deserialization failures remain isolated to the affected application stream.
- `Cancel()` publishes a sticky flag and interrupts native I/O with shutdown; only the serialized operation owner closes the handle. Native close and interruption are mutually synchronized. A later connect discards a cancelled stream and resets cancellation.
- `Disconnect()` never waits for a packet callback or active I/O owner. The active request/connect performs deferred cleanup, including exceptional exit. Idle disconnect releases immediately.
- Connect and request calls serialize internally. Queueing counts toward the original budget and an expired queued request does not interrupt the active one.
- Packet callbacks may call `Cancel`, `Disconnect`, `Status` and `Telemetry`, but must not reenter other client operations. No application callback can be preempted; check the original deadline/cancellation after it returns.
- Moves/destruction require external synchronization with every call. The moved-to object retains its active connection, cancellation and managed DNS slot; moved-from clients remain disconnected and reusable.
- Application cancellation leaves independent remote-file handles/planes alone. OS DNS work remains single-slot, non-detached and uncancellable until it completes; destruction joins it even beyond the caller budget.

## Current Server Lifecycle

- `Server::Connect` creates one listening socket and one accept thread.
- The accept thread waits for the listening socket, accepts clients, stores a connection by UUID, and creates one communication worker per client.
- Each communication worker owns the receive/process/reply loop for one client.
- `Server::Disconnect` marks the server as disconnecting, wakes the accept wait, joins the accept thread, snapshots client UUIDs, disconnects each client, joins each worker, closes the listening socket, and finally marks the server disconnected.
- The redesign must not require a worker to join itself. A worker may request cleanup for its own UUID; joining must be performed by another thread.

## Packet Processing

- A TCP read may contain a partial frame, one frame, or multiple frames. The current external behavior is request/response in order; an internal redesign must preserve that order.
- The packet factory returning `nullptr` means deserialization or packet construction failed. The current worker logs the failure and terminates that client communication loop; it does not terminate the server.
- `ProcessClientPacket` returning `nullptr` means no response was produced. The current worker logs the condition and terminates that client communication loop; it does not terminate the server.
- A valid response is sent through the configured output pipeline and the worker then waits for the next request.
- The first redesigned implementation must keep at most one in-flight request per connection unless ordering is explicitly preserved by sequence numbers.

## Connection Outcomes

- Peer shutdown is distinct from a local server shutdown and must remain observable through the existing read/status behavior.
- A timeout is an idle wait result, not a server failure; the accept or communication loop continues.
- A socket error, malformed frame, failed deserialization, or null response closes only the affected connection path unless the server itself is already shutting down.
- A client disconnect must not prevent the server from accepting and serving a later client.

## Concurrency Rules

- The current mutex protects client and worker maps. No socket I/O or worker join should happen while holding that mutex.
- The server must remain usable while another client handler is slow or is disconnecting.
- Shutdown must wake blocked I/O, stop new work, close or drain per-connection state according to the selected lifecycle policy, and complete without deadlock.
- Any internal queues must be bounded. Backpressure must not create an unbounded allocation path.
- The event loop owns the application listener, wakeup, active session sockets, and peer data-plane listeners/sockets.
- `ProcessClientPacket` and synchronous replies currently run in the event loop.
- A slow `ProcessClientPacket` or synchronous send currently blocks the whole server; a bounded worker pool and output queues are required before claiming isolation from slow handlers.
- Packet handlers now run in a bounded private pool; the EventLoop applies completions and still performs synchronous replies until output queues are introduced.
- Disconnect requests from pool workers are commands consumed by the EventLoop; session maps are never mutated by workers.
- Responses are serialized into bounded per-session output streams and drained through writable events without calling the blocking public send path.
- A peer data plane has one bounded in-flight file operation. The EventLoop parses its private frames, submits disk work to the bounded pool, and applies the queued response on a later writable event; a full pool pauses reads without discarding the frame or closing the plane.

## Platform Boundaries

- The server accept wait uses `poll()` on POSIX and `select()` on Windows.
- The Windows implementation is bounded by `FD_SETSIZE`; a future high-connection implementation must replace `select()` before exceeding that limit.

## Design Boundaries

- These invariants must be proven by documentation and parity tests before changing the server execution model.
- A private session object may isolate incremental parsing while retaining one worker per client.
- Event-loop and worker-pool changes must remain separate from parser and lifecycle changes so each behavior change can be validated independently.
