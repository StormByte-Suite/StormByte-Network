# StormByte-Network coding style

Match the surrounding Network code when this document is silent. This repository targets C++26 and uses tabs for indentation.

## Files and formatting

- Public headers use `.hxx`; implementation files use `.cxx`.
- C and C++ files start with the repository's complete license banner, unchanged. Markdown and CMake files do not use that banner.
- Include StormByte headers first, alphabetically, then a blank line, then standard-library and platform headers.
- Do not use `using namespace` in headers. In `.cxx`, follow the local module pattern.
- Use K&R braces and one statement per line. Keep pointer/reference markers with the type.
- Every edited file ends with a newline.

## Public API and DLL boundaries

- Preserve `STORMBYTE_NETWORK_PUBLIC` / `STORMBYTE_NETWORK_PRIVATE`, platform macros, and established StormByte aliases and helpers.
- Keep heap-owning allocation and destruction inside Network. Public handles use the module's deleters; shared polymorphic StormByte objects use their StormByte heap factory.
- Do not expose socket, framing, event-loop, or peer-plane implementation types as public API.
- Prefer `StormByte::Expected`, `StormByte::Shared`, `StormByte::BinaryData`, and `StormByte::ByteSize` when they express the existing module contract.
- Keep behavior portable across Windows, Linux, and macOS; guard OS-specific implementation and tests.

## Doxygen

Document public declarations and private implementation classes/helpers. Use qualified names in `@ref` tags. Public namespace blocks require matching `@namespace` documentation. Keep comments synchronized with actual ownership and lifecycle behavior.

## Tests and commits

- Add focused regression tests for observable behavior and compare file contents byte-for-byte for remote file I/O.
- Run the relevant tests from the build's `test/` directory; use a temporary build directory and leave the user's `build/` untouched.
- Use Conventional Commits in English (`feat:`, `fix:`, `test:`, `docs:`, `refactor:`), with a concise subject and a body when useful.
