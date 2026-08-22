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

// Builds a DocumentUri whose .path() is EXACTLY relPath, with no filesystem
// absolutization -- matching the bare corpus-root-relative paths
// CompilationController stores for `include`d files (e.g.
// "base/uvm_component.svh"; confirmed by every prior session's probe, and by
// reading db/compilation_controller.cpp: included-file paths come straight
// from the preprocessor's `include resolution, never through
// lsp::FileUri::fromPath's std::filesystem::absolute()).
//
// lsp::FileUri::fromPath (used by every existing test's makeParams, and by
// this project's own pathToUri()/symbol_utils.cpp for constructing *output*
// Locations) always calls std::filesystem::absolute() internally, so it
// cannot represent a bare relative path verbatim -- it's the wrong tool for
// building *input* query params against these DB keys. This instead goes
// through the generic lsp::Uri::parse("file:" + relPath), which performs no
// filesystem access at all and preserves the path text exactly (verified:
// Uri::parse("file:base/x.svh").path() == "base/x.svh", no leading slash,
// since there's no "//" authority marker to force absolute-path syntax).
lsp::DocumentUri uriForRelPath(const std::string& relPath);

// The inverse of the above, for comparing against Locations providers return
// (they build output URIs via pathToUri()/FileUri::fromPath(), which DOES
// absolutize relative to the current working directory at call time) -- so
// an expected relative path must be run through the same transform before
// comparing, rather than compared against the bare literal.
std::string expectedUriPath(const std::string& relPath);
