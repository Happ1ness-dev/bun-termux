#!/usr/bin/env bash
#
# Full compile test for bun-termux
# This tests 'bun build --compile' workflow end-to-end
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/lib/test-common.sh"

trap cleanup_test_env EXIT

if [ ! -f "$SCRIPT_DIR/test-env.js" ]; then
    echo -e "${RED}Error: test-env.js not found${NC}"
    exit 1
fi

TEST_VALUE="compiled_test_$(date +%s)_$$"

echo "=== Bun Compile Full Test ==="
echo ""

setup_test_env

echo "This test will:"
echo "1. Compile test-env.js into a standalone binary"
echo "2. Run the compiled binary"
echo "3. Verify environment variables work in the compiled output"
echo ""

cd "$TEST_DIR"

BUN_TERMUX_TEST_VAR="$TEST_VALUE" BUN_INSTALL="$TEST_DIR" "$TEST_DIR/bin/bun" build --compile --outfile test-env-compiled "$SCRIPT_DIR/test-env.js"

if [ ! -f "$TEST_DIR/test-env-compiled" ]; then
    echo -e "${RED}FAIL: Compiled binary was not created${NC}"
    exit 1
fi

echo -e "${GREEN}PASS: Compiled binary created${NC}"
echo ""

echo "Test 1: Running without env var (should fail)..."
if "$TEST_DIR/test-env-compiled" 2>/dev/null; then
    echo -e "${RED}FAIL: Should have failed without env var${NC}"
    exit 1
fi
echo -e "${GREEN}PASS: Correctly failed without env var${NC}"
echo ""

echo "Test 2: Running with env var..."
if BUN_TERMUX_TEST_VAR="$TEST_VALUE" "$TEST_DIR/test-env-compiled"; then
    echo ""
    echo -e "${GREEN}PASS: Compiled binary works correctly!${NC}"
else
    echo ""
    echo -e "${RED}FAIL: Compiled binary failed${NC}"
    exit 1
fi

echo ""
echo "=== All compile tests passed! ==="
echo "The wrapper is correctly embedded in compiled binaries."
