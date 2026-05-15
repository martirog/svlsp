# Decision: Use lsp-framework for the LSP Protocol Layer

## Status
Accepted — Phase 2

## Candidates Evaluated

| Library | Stars | LSP Spec | Dependencies | Status |
|---|---|---|---|---|
| **lsp-framework** (leon-bckl) | 73 | 3.17 | None | Active (v1.3.1, Mar 2026) |
| LspCpp (kuafuwang) | 111 | Unspecified | Boost, RapidJSON, utfcpp | Active but heavy |
| libclsp (otreblan) | 20 | 3.15 | Unknown | WIP / stale |
| clangd internal LSP | N/A | 3.17 | All of LLVM | Not extractable |

## Decision

**Use lsp-framework** added as a git submodule at `third_party/lsp-framework`,
integrated via CMake `add_subdirectory`.

## Rationale

1. **Zero external dependencies** — critical for a project targeting EDA toolchains
   where adding Boost would create significant build complexity.
2. **LSP 3.17** — current spec; all types are generated from the official meta-model
   JSON, so the type system stays accurate as the spec evolves.
3. **Type-safe API** — LSP message types are proper C++ structs. Errors in handler
   signatures are caught at compile time rather than at runtime JSON parsing.
4. **Actively maintained** — v1.3.1 released March 2026; responsive to issues.
5. **Real-world validation** — the Slingshot SV language server uses lsp-framework
   in production.
6. **C++20 async dispatch** — `std::future<T>` / `std::async(deferred)` is sufficient
   for our needs without pulling in Asio or Boost.Asio.

## Trade-offs

- **C++20 required** — already mandated by this project; not an added constraint.
- **`add_subdirectory` vs `find_package`** — using `add_subdirectory` avoids the
  `EXACT` version pin fragility of `find_package(lsp 1.3.0 EXACT REQUIRED)` and
  keeps the library pinned to the submodule commit.
- **`messages.h` is generated at build time** — `lspgen` (built from source inside
  the submodule) generates `types.h` and `messages.h` from `metaModel.json`. This
  is handled transparently by CMake but means a clean build takes slightly longer.

## Integration Details

- Submodule at `third_party/lsp-framework` pinned to v1.3.1.
- CMake options: `LSP_BUILD_EXAMPLES=OFF`, `LSP_INSTALL=OFF`.
- Link target: `lsp` (via `target_link_libraries(svlsp_lib PUBLIC lsp ...)`).
- Include path provided automatically by lsp-framework's `target_include_directories`.
