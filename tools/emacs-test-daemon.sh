#!/usr/bin/env bash
# emacs-test-daemon.sh — launch an Emacs daemon, run all integration tests,
# then tear the daemon down.
#
# Usage:
#   ./tools/emacs-test-daemon.sh              # run all tests/integration/test_*.sh
#   ./tools/emacs-test-daemon.sh <test.sh>    # run a single test file
#
# Exit code: 0 if all tests pass, 1 if any fail.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export SVLSP_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
export SVLSP_BIN="${SVLSP_ROOT}/build/debug/svlsp"

DAEMON_NAME="svlsp-test-$$"   # unique per invocation; allows parallel runs
export DAEMON_NAME

INIT_FILE="${SCRIPT_DIR}/emacs-test-init.el"
LIB_FILE="${SCRIPT_DIR}/emacs-test-lib.sh"
INTEGRATION_DIR="${SVLSP_ROOT}/tests/integration"
PACKAGE_DIR="${SVLSP_ROOT}/.emacs-test/elpa"
STAMP="${SVLSP_ROOT}/.emacs-test/packages-installed"
LOG_DIR="${SVLSP_ROOT}/.emacs-test"

mkdir -p "${LOG_DIR}"

DAEMON_PID=""

# ---------------------------------------------------------------------------
# Cleanup: always kill the daemon on exit.
# ---------------------------------------------------------------------------
cleanup() {
    if [ -n "${DAEMON_PID}" ]; then
        emacsclient -s "${DAEMON_NAME}" -e "(kill-emacs 0)" 2>/dev/null || true
        wait "${DAEMON_PID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Step 1: Pre-install Emacs packages via batch mode (once only).
# ---------------------------------------------------------------------------
if [ ! -f "${STAMP}" ]; then
    printf "Installing Emacs packages into .emacs-test/elpa/ (first run, requires internet)...\n"
    emacs --batch \
          --no-init-file \
          --no-site-file \
          --eval "(progn
  (setq user-emacs-directory \"${LOG_DIR}/\")
  (setq package-user-dir \"${PACKAGE_DIR}\")
  (require 'package)
  (setq package-archives
        '((\"melpa\" . \"https://melpa.org/packages/\")
          (\"gnu\"   . \"https://elpa.gnu.org/packages/\")))
  (package-initialize)
  (unless (package-installed-p 'lsp-mode)
    (package-refresh-contents)
    (package-install 'lsp-mode))
  (message \"Package install done.\"))" 2>&1 | tee "${LOG_DIR}/package-install.log"
    touch "${STAMP}"
    printf "Packages installed.\n\n"
fi

# ---------------------------------------------------------------------------
# Step 2: Start the daemon (fast — packages are already on disk).
# ---------------------------------------------------------------------------
printf "Starting Emacs daemon '%s'...\n" "${DAEMON_NAME}"

SVLSP_ROOT="${SVLSP_ROOT}" \
SVLSP_BIN="${SVLSP_BIN}" \
emacs --daemon="${DAEMON_NAME}" \
      --no-init-file \
      --no-site-file \
      --load "${INIT_FILE}" \
      2>"${LOG_DIR}/daemon-${DAEMON_NAME}.log" &
DAEMON_PID=$!

# Poll for the socket (up to 30 s; should be fast now).
SOCKET_READY=0
for i in $(seq 1 60); do
    if emacsclient -s "${DAEMON_NAME}" -e "t" >/dev/null 2>&1; then
        SOCKET_READY=1
        break
    fi
    sleep 0.5
done

if [ "${SOCKET_READY}" -eq 0 ]; then
    printf "ERROR: Emacs daemon did not start within 30 seconds.\n" >&2
    printf "       See %s/daemon-%s.log\n" "${LOG_DIR}" "${DAEMON_NAME}" >&2
    exit 1
fi

printf "Daemon ready.\n\n"

# ---------------------------------------------------------------------------
# Step 3: Load shared helpers and run test files.
# ---------------------------------------------------------------------------
# shellcheck source=tools/emacs-test-lib.sh
source "${LIB_FILE}"

if [ "$#" -gt 0 ]; then
    TEST_FILES=("$@")
else
    mapfile -t TEST_FILES < <(find "${INTEGRATION_DIR}" -name "test_*.sh" | sort)
fi

if [ "${#TEST_FILES[@]}" -eq 0 ]; then
    printf "No integration tests found in %s\n" "${INTEGRATION_DIR}"
    exit 0
fi

for test_file in "${TEST_FILES[@]}"; do
    printf "Running: %s\n" "$(basename "${test_file}")"
    # shellcheck source=/dev/null
    source "${test_file}"
done

# ---------------------------------------------------------------------------
# Step 4: Report.
# ---------------------------------------------------------------------------
print_summary
