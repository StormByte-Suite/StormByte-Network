# StormByte-Network coding style

Match the surrounding Network code when this document is silent. This repository targets C++26 and uses tabs for indentation.

## Files and formatting

- Public headers use `.hxx`; implementation files use `.cxx`.
- C and C++ files start with the repository's complete license banner, unchanged. Markdown and CMake files do not use that banner.
- Include StormByte headers first, alphabetically, then a blank line, then standard-library headers alphabetically. Keep platform-specific headers in their guarded blocks.
- Do not use `using namespace` in headers. In `.cxx`, follow the local module pattern.
- Use K&R braces and one statement per line. Keep pointer/reference markers with the type.
- Separate consecutive definitions and documented declarations with a blank line, including defaulted constructors, destructors and assignment operators.
- Put a single-statement control-flow body on its own indented line without braces. Put `else` and `else if` on their own line, not after a closing brace.
- Keep `public:`, `protected:` and `private:` one tab inside the class, and their members one additional tab inside. Do not align code with indentation spaces.
- Types, enumerations and ordinary functions use PascalCase; macros use SCREAMING_SNAKE_CASE. Executable test functions are the deliberate exception described below. Use descriptive variable names.
- In headers, use nested namespace blocks up to three levels; use qualified namespace syntax only for deeper nesting.
- Every edited file ends with a newline.

## Public API and DLL boundaries

- Preserve `STORMBYTE_NETWORK_PUBLIC` / `STORMBYTE_NETWORK_PRIVATE`, platform macros, and established StormByte aliases and helpers.
- Keep internal heap-owning allocation and destruction inside Network. Public remote-file handles use `Safe::Unique` with Base's heap factory; shared polymorphic StormByte objects use their StormByte heap factory. Construct remote leaves inside Network and preserve virtual destruction when transferring ownership to Buffer base types.
- Do not expose socket, framing, event-loop, or peer-plane implementation types as public API.
- Prefer `StormByte::Expected`, `StormByte::Safe::Shared`, `StormByte::Safe::Unique`, `StormByte::BinaryData`, and `StormByte::ByteSize` when they express the existing module contract. Use StormByte concepts, helpers and domain exceptions rather than stock replacements when available.
- Use the current `Safe::String`, `Safe::WString`, `Safe::Optional`, `Safe::Vector`, `Safe::Map`, `Safe::Pair` and callable APIs for owned values crossing modules. String sizes exclude the terminating null; embedded nulls remain part of the value. `CString` and `WCString` are retired.
- Ordinary `inline` does not guarantee execution in the caller's CRT. Use out-of-line provider operations for resource-bearing lifecycle work; use `STORMBYTE_FORCE_INLINE` only where an operation must run in the consumer module. Callback contexts must be constructed, cloned and released through their provider's allocator operations.
- Register a complete type with `STORMBYTE_DECLARE_MAYBE_SAFE` at global scope only after verifying its fields, constructors, copy/move operations, assignments, destructor and ownership. The macro declares provider responsibility; it does not prove safety or certify derived classes automatically.
- Keep the supported C++ ABI, packing, calling convention and required STL ABI compatible. Base, Network and every callback or derived-class provider must remain loaded until their objects and handles are released.
- Preserve `Client` and `Server` as inheritance-oriented APIs. Allocate exact derived types through Safe factories when ownership crosses modules. Derived servers must disconnect before destroying state accessed by their handlers. Pipe callables own their captures; stream facades and logger references are synchronous borrows and must not be retained.
- Private STL state is permitted when all allocator-sensitive operations remain in its provider. Do not replace private containers merely because their type is not Safe-classified, and do not mistake a borrowed view or a scalar wrapper for evidence of cross-CRT allocation.
- Keep behavior portable across Windows, Linux, and macOS; guard OS-specific implementation and tests.

## Doxygen

Document public declarations and private implementation classes/helpers, including constructors, members and aliases. Use full multiline Doxygen blocks; never condense documentation into `/** ... */` on one line. Include applicable `@param`, `@tparam` and `@return` tags. Use qualified names in `@ref` tags. Header namespace blocks require matching `@namespace` documentation. Keep comments synchronized with actual ownership and lifecycle behavior.

Large classes use `@name` groups. Cross-module symbols resolve through Doxygen tag files; do not add dependency source trees to `INPUT`. Fix warnings in the documented declarations rather than suppressing them or changing CMake to force warning failures.

## Tests and commits

- Add focused regression tests for observable behavior and compare file contents byte-for-byte for remote file I/O.
- Name executable test functions `test_snake_case`, matching Buffer tests (for example, `test_client_retry_after_failed_connect`). Use the same name in registration and diagnostic labels. Helpers and fixtures retain the ordinary coding conventions.
- Group tests into sections. Section names and test names within each section are alphabetical. Definitions and registration in `main` use the same order and identical three-line banners:

```cpp
// -------------------
// Construct
// -------------------
```

- Keep helpers and fixtures before the test sections. Every executable test is registered exactly once; helper routines are not independent test cases. Do not invent decorative banner variants or print section titles with `std::cout`.
- Configure tests with `-DENABLE_TEST=ON`. Run the relevant tests from the build's `test/` directory; use a temporary build directory and leave the user's `build/` untouched. Keep platform-specific assertions guarded so Windows, Linux and macOS retain meaningful coverage.
- Validate ownership claims with separate provider/consumer modules where possible. Single-executable tests and Linux sanitizers alone do not prove safety across separate Windows CRT heaps.
- Use Conventional Commits in English (`feat:`, `fix:`, `test:`, `docs:`, `refactor:`), with a concise subject and a body when useful.
- Keep commits atomic: one coherent change with its tests and corresponding release notes. Do not run clang-format; inspect changed lines manually and preserve tabs.
