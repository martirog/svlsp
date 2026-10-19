# Top-level Makefile for svlsp
#
# Documentation targets use pandoc to render Markdown to HTML and PDF.
# C++ build is delegated to CMake (see CMakeLists.txt).

PANDOC     := pandoc
PANDOC_CSS := docs/style.css

MD_SOURCES := plan.md $(wildcard docs/*.md docs/**/*.md)

HTML_OUT   := $(patsubst %.md, build/docs/%.html, $(MD_SOURCES))
PDF_OUT    := $(patsubst %.md, build/docs/%.pdf,  $(MD_SOURCES))

# ---------------------------------------------------------------------------
# Documentation
# ---------------------------------------------------------------------------

.PHONY: docs docs-html docs-pdf

docs: docs-html docs-pdf

docs-html: $(HTML_OUT)

docs-pdf: $(PDF_OUT)

build/docs/%.html: %.md
	@mkdir -p $(dir $@)
	$(PANDOC) --standalone --toc \
	    $(if $(wildcard $(PANDOC_CSS)),--css $(PANDOC_CSS),) \
	    -f markdown -t html5 \
	    -o $@ $<

build/docs/%.pdf: %.md
	@mkdir -p $(dir $@)
	$(PANDOC) --standalone --toc \
	    -f markdown \
	    -o $@ $< \
	    --pdf-engine=xelatex \
	    $(or $(shell which xelatex > /dev/null 2>&1 && echo ""), \
	         --pdf-engine=wkhtmltopdf 2>/dev/null || true)

# ---------------------------------------------------------------------------
# C++ build (delegates to CMake)
# ---------------------------------------------------------------------------

.PHONY: configure build clean

configure:
	cmake --preset debug

build: configure
	cmake --build --preset debug

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

.PHONY: test test-unit test-integration test-integration-uvm perf-lsp perf-emacs

test: test-unit test-integration

test-unit: build
	./build/debug/unit_tests

test-integration:
	bash tools/emacs-test-daemon.sh

# Opt-in: completion against the full UVM library DB (needs SVLSP_UVM_DB or
# the default corpus uvm.db; see tests/integration/uvm_corpus/uvm_completion.sh).
# Profile with SVLSP_BIN=build/release/svlsp.
test-integration-uvm:
	bash tools/emacs-test-daemon.sh tests/integration/uvm_corpus/uvm_completion.sh

# Opt-in latency benchmarks (tests/perf/README.md), against the release
# build by default. SCENARIO=uvm (needs the UVM library DB) or small.
SCENARIO ?= uvm
PERF_SVLSP ?= $(CURDIR)/build/release/svlsp

perf-lsp:
	python3 tools/lsp_perf.py tests/perf/$(SCENARIO).json --server $(PERF_SVLSP) $(PERF_ARGS)

perf-emacs:
	SVLSP_BIN=$(PERF_SVLSP) SVLSP_PERF_SCENARIO=$(SCENARIO) \
		bash tools/emacs-test-daemon.sh tests/perf/emacs_perf.sh

# ---------------------------------------------------------------------------
# Housekeeping
# ---------------------------------------------------------------------------

.PHONY: clean-docs clean

clean-docs:
	rm -rf build/docs

clean: clean-docs
	rm -rf build/
