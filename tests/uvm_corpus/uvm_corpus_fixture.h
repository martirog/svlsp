#pragma once
#include "db/symbol_database.h"
#include <lsp/fileuri.h>
#include <string>

// Shared fixture for tests/uvm_corpus/*: compiles the real, external UVM-core
// corpus through the actual production pipeline (CompilationController,
// exactly as the real svlsp LSP server does) exactly once per test-binary run,
// then hands every test case the resulting DB to query.
//
// This binary is opt-in and deliberately NOT registered with ctest/`make
// test` (see CMakeLists.txt) -- it depends on an external checkout of UVM
// that isn't part of this repo, per the corpus root resolved by
// uvmCorpusRoot() below. Run it directly: ./build/release/uvm_corpus_tests

// Directory containing the corpus's uvm.sv (and everything it `includes).
// Resolved from the SVLSP_UVM_CORPUS_DIR environment variable, falling back
// to the path used throughout this whole line of work if unset.
std::string uvmCorpusRoot();

// Compiles uvmCorpusRoot()/uvm.sv (same includeDirs/defines every prior
// session's probe used: includeDirs={"."}, defines={"UVM_NO_DPI":""}) into an
// in-memory DB on first call; every later call returns the same instance.
// Throws std::runtime_error with a clear setup message if the corpus root or
// its uvm.sv can't be found -- appropriate for an opt-in binary that's only
// ever run by someone who deliberately has the corpus checked out.
SymbolDatabase& uvmCorpusDb();

// Reads a real corpus file's raw text from disk, for providers that need the
// open document's text (hover/definition/completion). relPath is
// corpus-root-relative, e.g. "base/uvm_component.svh".
std::string readCorpusFile(const std::string& relPath);

// The URI of a corpus file, from its corpus-root-relative path (e.g.
// "base/uvm_component.svh"). The fixture compiles uvm.sv by its absolute
// path, so the DB names every file by its absolute path, as `--build-db`
// and the server do.
lsp::DocumentUri uriForRelPath(const std::string& relPath);

// The path a provider's Location carries for that file.
std::string expectedUriPath(const std::string& relPath);
