# Private transport foundation

These opt-in socket operations do not change public APIs, packet protocols or
legacy `Connect`, `Send`, `Receive`, `TryRead` and `ReadOnce` behavior.

`Socket::OperationControl` borrows a `Safe::Atomic<bool>` cancellation flag and
uses an absolute `steady_clock` deadline (maximum means unlimited). Cancellation
wins over expiry. Callers serialize socket I/O and lifecycle operations; cancel
through the flag, not a concurrent disconnect. Waits poll cancellation at most
every 50 ms, subject to OS scheduling. Deadlines never restart after progress,
would-block or interrupted waits.

`Client::Send(span, control)` returns `OperationResult`: completion, timeout,
cancellation, peer close or error, with the exact bytes accepted by `send` before
stopping and the captured native error when available. Acceptance is not proof of
remote delivery. Failure does not undo transmitted bytes or close the socket;
callers decide whether to continue from the offset or abandon the connection.
An empty send succeeds without checking controls, matching legacy empty sends.

`Client::ReadSome(nonempty_span)` attempts a single non-blocking read:
`Complete` means positive bytes, `WouldBlock` means no data now, `PeerClosed`
means clean receive EOF, and `Error` means a hard error (including invalid input
or an interrupted syscall). It neither waits nor changes connection status.

`TransportTest` compiles the private transport provider locally because shared
Network symbols are hidden and bundled dependency archives are shared-only.
This tests native behavior without making internals public. It does not certify
separate Windows CRT ownership; provider/consumer lifetime rules still apply.

## Managed resolution and connection

`Info::ResolveAll(host, port, AddressPolicy)` is a **blocking**, uncancellable
native lookup. It copies every usable IPv4/IPv6 address, preserving ports and
IPv6 scope IDs. Policies are strict `IPv4Only` / `IPv6Only`, `SystemOrder`, or
stable family grouping `PreferIPv4` / `PreferIPv6`. No preferred-family failure
silently becomes a different strict family. Legacy `FromHost` still selects
the first address of its original family.

Retain and share a `Connection::Resolver` to bound DNS resource use: each
instance admits **one** lookup, has **no queue**, and owns at most one joinable
`Safe::Thread`. Concurrent admission or still-running abandoned work returns
`Busy`; no per-call detached worker is created. `Resolve(..., control, state)`
bounds **caller waiting**, polling at most every 10 ms. It copies the host
before launch, retains no caller cancellation flag on the worker, and frees
native `addrinfo` inside the provider when lookup eventually completes.
Abandoned late results are discarded; one bounded result snapshot can remain
until slot reuse or destruction if completion races abandonment.

**Timeout does not cancel `getaddrinfo`.** Resolver destruction joins outstanding
work and may wait indefinitely for the OS resolver. Keep the resolver and its
Network/Base/lookup providers alive until that join completes. A permanently
stalled OS lookup permanently occupies that instance's slot; applications must
not create replacement resolvers per timeout to bypass the resource bound.
There is no claim of a hard real-time bound on thread start, join, scheduling,
native option initialization or Safe allocation.

`Client::Connect(host, port, resolver, control)` uses the constructor's strict
family. The additional policy overload is the only opt-in mixed-family route.
The **same absolute deadline** covers caller DNS waiting and every sequential
connection attempt. `ConnectResolved(address_span, control)` skips DNS entirely,
creates a fresh non-blocking socket per usable candidate and checks `SO_ERROR`
after writability (also checking immediately completed connects). Refused or
unusable alternatives fall through; timeout/cancellation stops attempts.
Failure closes handles, releases peer info and leaves the client disconnected
and retryable. An already-connected client returns `Error` without mutation.
The constructor's original family preference survives mixed-family calls.
Connection results transfer no bytes; native error is the last captured socket
failure, or zero for resolution/precondition/allocation errors.

Loopback tests use ephemeral ports and a deterministic slow lookup provider to
exercise abandonment, bounded admission, retry and joined late cleanup without
depending on external DNS latency. Native family/order tests depend on the
host's `localhost` configuration; IPv6 address parsing does not prove that IPv6
routing is enabled. Windows and macOS code paths require validation there.
