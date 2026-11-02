# Network 3.1.0 client-control validation

Recorded on Linux x86-64, 2026-11-02, for implementation commits
`b6c8563b4219e239f9082e191f88c72331e3438e` and
`d4a9046c8a66d441c781e1e4c288bca1c2e06365`.
This directory contains small, durable evidence, not dependency archives,
generated documentation or build trees. Reproduction work stays under the
repository's ignored `.validation/` directory; compiler scratch files also
stay there. Archived logs normalize trailing whitespace only.

## Result

- Ordinary Release: **10/10 suites, 167 registered Linux cases passed**.
- The 23-case client-control suite additionally passed ten repetitions.
- Optimized ASan + UBSan + leak detection, **untouched released binary
  dependencies with Clang 22/libstdc++: 6/6 suites, 132 cases passed**.
- Optimized ASan + UBSan + leak detection, **GCC 16 with unchanged Base 2.2.3
  source built in a private prefix: 6/6 suites, 132 cases passed**.
- Both sanitizer reproduction scripts were run from fresh build prefixes and
  passed again: 40.24 s with Clang/binary providers and 54.52 s with GCC/source
  Base. No sanitizer diagnostic occurred in either passing run.
- No dependency source, pin, tag, public ABI workaround, undefined-symbol shim
  or Network implementation was changed during this follow-up.

The sanitizer gate is **not blocked for the release's matching Clang ABI**.
The GCC-consumer/Clang-provider combination described below remains incompatible
when constrained template calls cross the shared-library boundary. Do not
advertise that combination as universally supported because an optimized
ordinary build happens to link. If mixed-compiler binary compatibility is a
release requirement, it remains a separate dependency/toolchain-policy blocker;
this work does not repair or retag Base.

## Exact original linker cause

The initial description, “missing Base exports,” was incomplete. The exports
are present, but use **different constrained-template mangled names** from the
GCC consumer's references. This is not a stale archive, a libc++/libstdc++
mix-up, or an invalid consumer cast.

Verified current Base tag `2.2.3` resolves through annotated tag object
`579f3dcac03443192a91920aaf32de177bcce04b` to release source commit
`57f5e29a04e0ae3d83edf42c6c3851ea22bcd012`. Freshly downloaded archives:

| Archive | SHA-256 |
|---|---|
| `StormByte-2.2.3-linux-x64-libc++.zip` | `0e2aea8b6365ea95158b1fa2be27215e805fe89b750a9dc34ab48b0ebe2e8a18` |
| `StormByte-2.2.3-linux-x64-libstdc++.zip` | `f637abba35f8dfb81faf7cd6ed7a29ea22b8f6fc7bb232dd157936f2131d4269` |

The original libstdc++ cached archive is byte-identical to the current download,
and its installed library is byte-identical to the freshly extracted library.
The release workflow builds Linux archives with Clang, including the archive
using libstdc++. Both affected builds used libstdc++, but that alone does not
guarantee identical compiler template ABI.

For the documented, explicitly instantiated conversion
`static_cast<long>(StormByte::ByteSize)`:

```text
GCC 16 consumer requires:
_ZNK9StormByte8ByteSizecvT_IlEEv

Clang 22 consumer requires, and both released archives export:
_ZNK9StormByte8ByteSizecvT_ITkNS_4Type8IntegralElQntsr4TypeE6SameAsIS1_mEEEv
```

Base declares the constrained conversion template in `byte_size.hxx`, declares
its `extern template` for `long`, and explicitly instantiates it in
`byte_size.cxx`. GCC 16's unchanged source-provider build exports the first
name; Clang's released provider exports the second. Older demangling tools do
not necessarily decode the latter, so searching only demangled display names
incorrectly suggests no exports exist. Raw `nm` names establish the actual
boundary.

The standalone conversion probe links and runs with Clang at `-O0` using
either matching runtime archive. GCC `-O0`, with or without sanitizers, fails
against the released Clang/libstdc++ provider; GCC `-O2` passes this small probe
because it inlines the conversion. The full optimized GCC sanitizer build
still emitted one such call from existing `remote_file_host.cxx`. Thus
optimization can mask the mismatch, and ASan is not the root cause.

See `release-identity.log`, `conversion-abi.log`, `abi-probe-matrix.log`,
`gcc-conversion-link.log` and the original GCC link logs.

## Sanitizer method and timing

The successful Clang run instruments Network and test executables; its StormByte
dependencies are the actual unmodified release binaries, not instrumented
source substitutes. The GCC fallback additionally instruments unchanged Base
source, including its pinned SDS submodule
`cc59ce0b12e91521476ad54a1e069fb6fd933bbc`. Other StormByte dependencies remain
released binaries. These are not claims of fully instrumented dependency trees,
Windows cross-CRT safety, or Windows/macOS runtime validation.

Both use `-O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer`, LTO off,
ASan leak detection/abort/strict-string/mismatched-allocation checks, and
UBSan halt-on-error/stack traces. `final-runtime-identity.log` records linked
providers and proves Clang's private prefix retained the exact released Base.

An earlier fully unoptimized GCC/Debug source-provider run is retained in
`sanitizer-tests.log`: four suites passed, client control exceeded three
fixed timing assertions, and the large remote-file suite exceeded its 300 s
timeout. No memory-safety report occurred. The same **unchanged tests** pass
under optimized instrumentation, without loosening deadlines or suite
timeouts. Debug serialization/copying of large payloads and instrumented
remote-file throughput must not be confused with a sanitizer finding.

## Latency

`latency-before.log` preserves all original samples, measured before fixes:
five plaintext pings **115–878 µs**, five TLS pings **27–207 µs**.
The final 128-ping-per-mode probe in `latency-final.log` reports:

| Mode | Minimum | Mean | Maximum |
|---|---:|---:|---:|
| Plaintext | 86 µs | 113 µs | 886 µs |
| TLS | 12 µs | 15 µs | 107 µs |

The reported approximately one-second round trip was not reproduced.
No speculative polling-interval cause or RTT fix is claimed. Removal of
the independently verified fixed disconnect sleep is a lifecycle correction,
not evidence of an RTT root cause.

## Reproduce

Run from any directory; scripts resolve the repository root themselves.
They require the existing GitHub CLI, CMake, Make, ccache, compilers and
ordinary archive/binutils tools. They install no packages and modify no
tracked dependency source.

```bash
bash doc/logs/client-control/verify-abi.sh
bash doc/logs/client-control/validate-sanitizers.sh binary-clang
bash doc/logs/client-control/validate-sanitizers.sh source-gcc
```

The ABI script deliberately reports expected GCC link failures rather than
treating them as its own failure. Its archive hashes are pinned above; a
future retag with different assets intentionally fails hash verification.
The source fallback uses CMake/Make `/fast` targets only **after** preparing
all dependency targets and installing Base in the isolated prefix, preventing
BuildMaster's binary installation target from replacing it again.
Every compilation detects available parallelism with `-j"$(nproc)"`.

The focused sanitizer selection is:

```bash
ctest --test-dir <private-build>/test --output-on-failure \
	-R '^(ClientControlTest|ClientServerTest|TransportTest|TLSTransportTest|HTTPTest|RemoteFileTest)$'
```

Full logs for the two fresh reproductions are `reproduce-clang.log` and
`reproduce-source-gcc.log`; earlier optimized runs are also retained.
Ordinary release and stress evidence are `release-tests.log` and
`control-repeat.log`.

## Windows CI follow-up (run 41422940802)

`windows-ci-41422940802-failure.log`: build and link passed; 19/21 suites
passed. `ClientControlTest` failed at `test_send_backpressure_deadline`
(elapsed bound) and `TransportTest` at
`test_interrupt_wakes_read_without_releasing_handle` (wait state). Both are new
3.1.0 tests. The resolver/refusal diagnostics in the same log are intentional
negative cases. The pre-change run on `5ff76eb`
(`windows-ci-41416426281-prechange.log`) passed all 20 suites. At `2a274d7`, every
established suite also passed on Windows, including HTTP, TLS transport,
client/server and remote-file suites. Between `5ff76eb` and `2a274d7`, existing
socket paths changed only by moving shutdown into the mutex-serialized
`Interrupt()` and removing the post-shutdown sleep. `WaitReady`, `select` use and
`Frame::ProcessOutput` were unchanged.

- Interrupt: the test waited on a raw handle with no cancellation flag, so only a
  readiness event could end it before 2 s. POSIX `poll` reports local
  `shutdown`; Winsock `select` readability does not include it, and
  `recv` after `SD_BOTH` returns `WSAESHUTDOWN`, not EOF. The product cancellation
  path always publishes its flag before `Interrupt()`, and waits recheck it at
  most every 50 ms. Windows passed `test_cancel_send`, `test_cancel_tls_send` and
  `test_disconnect_unblocks_legacy_read`, as expected. The test now uses the
  cancellation order on every platform and keeps the uncancelled wake as a
  POSIX-only case.
- Backpressure: `windows-backpressure-diagnosis.log` shows the original
  16 MiB/100 ms case never reached a socket write even on Linux. Synchronous
  framing consumed the budget, and Windows CI exceeded the 500 ms bound. A fixed
  16 KiB peer receive window plus a 2 s budget now makes the deadline expire in
  the send-capacity wait; the test requires `TimedOut` within 2–2.4 s.
- `windows-fix-linux-focused.log`: three Linux runs of both corrected suites
  passed (19 transport cases, 23 client-control cases).
