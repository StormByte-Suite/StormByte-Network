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
