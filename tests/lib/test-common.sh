#!/usr/bin/env bash
#
# Shared test utilities for bun-termux tests
# Usage: source "$(dirname "$0")/lib/test-common.sh"
#

# Get directories
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/tmp_bun_install"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Test counter
TESTS_PASSED=0

# Find the original bun binary (buno preferred, fallback to bun)
find_original_bun() {
    if [ -n "$BUN_BINARY_PATH" ] && [ -x "$BUN_BINARY_PATH" ]; then
        echo "$BUN_BINARY_PATH"
        return 0
    fi

    local bun_dir="${HOME}/.bun/bin"

    if [ -x "$bun_dir/buno" ]; then
        echo "$bun_dir/buno"
        return 0
    fi

    if [ -x "$bun_dir/bun" ]; then
        echo "$bun_dir/bun"
        return 0
    fi

    echo -e "${RED}Error: Cannot find original bun binary${NC}" >&2
    echo "Looked for: $bun_dir/buno, $bun_dir/bun" >&2
    return 1
}

# Setup test environment - creates fresh TEST_DIR
setup_test_env() {
    echo "=== Setup ==="

    # Build if needed
    if [ ! -f "$PROJECT_DIR/bun-termux" ]; then
        echo "Building project..."
        make -C "$PROJECT_DIR"
    fi

    [ -x "$PROJECT_DIR/bun-termux" ] || { echo "Wrapper not found"; return 1; }
    [ -f "$PROJECT_DIR/bun-shim.so" ] || { echo "Shim not found"; return 1; }

    # Clean and create test directory (with safety check)
    case "$TEST_DIR" in
        */tmp_bun_install) rm -rf "$TEST_DIR" ;;
        *) echo "Error: TEST_DIR '$TEST_DIR' doesn't match expected pattern" >&2; return 1 ;;
    esac
    mkdir -p "$TEST_DIR/bin" "$TEST_DIR/lib" "$TEST_DIR/tmp/fake-root"

    # Copy files
    cp "$PROJECT_DIR/bun-termux" "$TEST_DIR/bin/bun"
    cp "$PROJECT_DIR/bun-shim.so" "$TEST_DIR/lib/bun-shim.so"

    # Link original bun
    local original_bun
    original_bun="$(find_original_bun)" || return 1
    ln -s "$original_bun" "$TEST_DIR/bin/buno"

    echo -e "${GREEN}Setup complete${NC}"
    echo "Test directory: $TEST_DIR"
    echo ""
}

# Cleanup test environment
cleanup_test_env() {
    # Safety check: only remove if TEST_DIR looks like our test directory
    case "$TEST_DIR" in
        */tmp_bun_install) rm -rf "$TEST_DIR" ;;
        *) echo "Warning: TEST_DIR '$TEST_DIR' doesn't match expected pattern, skipping cleanup" >&2 ;;
    esac
}

# Run a test function
run_test() {
    local name="$1"
    shift
    echo -e "${YELLOW}[TEST]${NC} $name"
    if "$@"; then
        echo -e "${GREEN}[PASS]${NC} $name"
        echo ""
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        echo -e "${RED}[FAIL]${NC} $name"
        echo ""
        return 1
    fi
}

# Verify test environment is set up
check_test_env() {
    if [ ! -d "$TEST_DIR/bin" ] || [ ! -f "$TEST_DIR/bin/bun" ]; then
        echo -e "${RED}Error: Test environment not set up${NC}" >&2
        echo "Run: make test-setup" >&2
        return 1
    fi
}
