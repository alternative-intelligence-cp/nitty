#!/usr/bin/env bash
# tests/e2e/run_e2e.sh — Nitty v0.11.0 End-to-End Test Runner
#
# Builds all E2E test binaries and runs them in sequence.
# Reports pass/fail counts and exits with:
#   0 — all tests passed
#   1 — one or more tests failed
#
# Usage: bash tests/e2e/run_e2e.sh [--no-build]
#
# Options:
#   --no-build    Skip the build step (use pre-built binaries in .nitpick_make/build/)
#
# Environment:
#   NITTY_E2E_VERBOSE=1    Show full test output even on pass
#   NPKBLD_PATH            Override path to npkbld (default: searches PATH then
#                          ../nitpick-build/build/npkbld relative to repo root)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$REPO_ROOT/.nitpick_make/build"

# ── Locate npkbld ─────────────────────────────────────────────────────────────
NPKBLD="${NPKBLD_PATH:-}"
if [[ -z "$NPKBLD" ]]; then
    # Try PATH first
    if command -v npkbld &>/dev/null; then
        NPKBLD="$(command -v npkbld)"
    # Then try the standard location relative to the repo
    elif [[ -x "$(dirname "$REPO_ROOT")/nitpick-build/build/npkbld" ]]; then
        NPKBLD="$(dirname "$REPO_ROOT")/nitpick-build/build/npkbld"
    else
        NPKBLD="$REPO_ROOT/../nitpick-build/build/npkbld"
    fi
fi

# ── Parse flags ───────────────────────────────────────────────────────────────
NO_BUILD=0
for arg in "$@"; do
    if [[ "$arg" == "--no-build" ]]; then
        NO_BUILD=1
    fi
done

# ── Colours ───────────────────────────────────────────────────────────────────
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

# ── E2E test targets (build target name → binary name → description) ──────────
# Format: "build_target:binary_name:description"
E2E_TESTS=(
    "test_vt_conformance:test_vt_conformance:VT Conformance (cursor/erase/SGR/colors/modes)"
    "test_e2e_shell:test_e2e_shell:Shell Interaction (echo/env/cd/sigint/exit/resize)"
    "test_e2e_tabs_panes:test_e2e_tabs_panes:Tab & Pane Management (create/split/close/layout)"
    "test_e2e_config:test_e2e_config:Configuration System (schema/defaults/validation)"
    "test_e2e_session:test_e2e_session:Session Persistence (save/load/getters)"
)

# ── Build step ────────────────────────────────────────────────────────────────
if [[ "$NO_BUILD" -eq 0 ]]; then
    echo -e "${BOLD}${CYAN}Building E2E test binaries...${RESET}"
    cd "$REPO_ROOT"

    if [[ ! -x "$NPKBLD" ]]; then
        echo -e "${RED}npkbld not found at: $NPKBLD${RESET}"
        echo "  Set NPKBLD_PATH or ensure nitpick-build is adjacent to the repo."
        exit 1
    fi

    # Collect all build targets in one npkbld invocation.
    # npkbld may return non-zero if OTHER (unrelated) targets fail —
    # we only care whether our specific E2E binaries were produced.
    TARGETS=()
    for entry in "${E2E_TESTS[@]}"; do
        TARGETS+=("${entry%%:*}")
    done

    echo "  Running: $NPKBLD build ${TARGETS[*]}"
    "$NPKBLD" build "${TARGETS[@]}" >/tmp/nitty_e2e_build.log 2>&1 || true
    echo ""

    BUILD_OK=1
    for entry in "${E2E_TESTS[@]}"; do
        IFS=':' read -r target binary desc <<< "$entry"
        BIN="$BUILD_DIR/$binary"
        printf "  %-32s " "$target:"
        if [[ -x "$BIN" ]]; then
            echo -e "${GREEN}OK${RESET}"
        else
            echo -e "${RED}MISSING${RESET}"
            BUILD_OK=0
        fi
    done

    if [[ "$BUILD_OK" -eq 0 ]]; then
        echo ""
        echo -e "${RED}${BOLD}Some E2E binaries were not built. Build log (last 30 lines):${RESET}"
        tail -30 /tmp/nitty_e2e_build.log | sed 's/^/  /'
        echo ""
        echo -e "${RED}${BOLD}Build failed — aborting test run.${RESET}"
        exit 1
    fi
    echo -e "${GREEN}All E2E binaries built.${RESET}\n"
fi

# ── Run tests ─────────────────────────────────────────────────────────────────
echo -e "${BOLD}${CYAN}=== Nitty v0.11.0 End-to-End Test Suite ===${RESET}\n"

PASS_SUITES=0
FAIL_SUITES=0
FAIL_NAMES=()

for entry in "${E2E_TESTS[@]}"; do
    IFS=':' read -r target binary desc <<< "$entry"
    BIN="$BUILD_DIR/$binary"

    printf "${BOLD}%-55s${RESET} " "$desc"

    if [[ ! -x "$BIN" ]]; then
        echo -e "${YELLOW}SKIP${RESET} (binary not found: $BIN)"
        FAIL_SUITES=$((FAIL_SUITES + 1))
        FAIL_NAMES+=("$desc (binary missing)")
        continue
    fi

    LOG="/tmp/nitty_e2e_run_${binary}.log"
    if "$BIN" >"$LOG" 2>&1; then
        echo -e "${GREEN}PASS${RESET}"
        if [[ "${NITTY_E2E_VERBOSE:-0}" == "1" ]]; then
            sed 's/^/  /' "$LOG"
        fi
        PASS_SUITES=$((PASS_SUITES + 1))
    else
        echo -e "${RED}FAIL${RESET}"
        sed 's/^/  /' "$LOG"
        FAIL_SUITES=$((FAIL_SUITES + 1))
        FAIL_NAMES+=("$desc")
    fi
done

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}Results: ${GREEN}${PASS_SUITES} passed${RESET}${BOLD}, ${RED}${FAIL_SUITES} failed${RESET}"
if [[ "${#FAIL_NAMES[@]}" -gt 0 ]]; then
    echo -e "${RED}Failed suites:${RESET}"
    for name in "${FAIL_NAMES[@]}"; do
        echo "  - $name"
    done
    exit 1
fi
echo -e "${GREEN}${BOLD}All E2E tests passed.${RESET}"
exit 0
