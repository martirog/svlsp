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
	cmake -B build/cpp -DCMAKE_BUILD_TYPE=Debug

build: configure
	cmake --build build/cpp

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

.PHONY: test test-unit test-integration

test: test-unit test-integration

test-unit: build
	./build/cpp/unit_tests

test-integration:
	bash tools/emacs-test-daemon.sh

# ---------------------------------------------------------------------------
# Housekeeping
# ---------------------------------------------------------------------------

.PHONY: clean-docs clean

clean-docs:
	rm -rf build/docs

clean: clean-docs
	rm -rf build/cpp
