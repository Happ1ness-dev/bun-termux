#!/usr/bin/env bash
# Test runner for bun-termux

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source "$SCRIPT_DIR/lib/test-common.sh"

trap cleanup_test_env EXIT

# Tests
test_version() {
    BUN_INSTALL="$TEST_DIR" "$TEST_DIR/bin/bun" --version
}

test_proc_self_exe() {
    BUN_INSTALL="$TEST_DIR" BUN_BINARY_PATH="$SCRIPT_DIR/test-proc-self-exe" \
        "$TEST_DIR/bin/bun"
}

test_env_passthrough() {
    export BUN_TERMUX_TEST_VAR="test_$$"
    BUN_INSTALL="$TEST_DIR" "$TEST_DIR/bin/bun" "$SCRIPT_DIR/test-env.js"
    unset BUN_TERMUX_TEST_VAR
}

test_os_cpus() {
    BUN_INSTALL="$TEST_DIR" "$TEST_DIR/bin/bun" "$SCRIPT_DIR/test-os-cpus.js"
}

test_node_shebang() {
    BUN_INSTALL="$TEST_DIR" "$TEST_DIR/bin/bun" -e "const result = Bun.spawnSync(['$SCRIPT_DIR/test-node-shebang.js']); console.log(result.stdout.toString());"
}

test_dns_lookup() {
    BUN_INSTALL="$TEST_DIR" "$TEST_DIR/bin/bun" "$SCRIPT_DIR/test-dns-lookup.mjs"
}

test_nested_fake_root() {
    local nested="$TEST_DIR/tmp/nested-fake-root"
    mkdir -p "$nested"

    cat > "$nested/test.js" << 'EOF'
const result = Bun.spawnSync([process.argv[2], '-e', 'console.log(process.env.BUN_FAKE_ROOT)']);
const output = result.stdout.toString().trim();
if (output !== process.env.BUN_FAKE_ROOT) {
  console.error('Expected:', process.env.BUN_FAKE_ROOT, 'Got:', output);
  process.exit(1);
}
EOF

    export BUN_FAKE_ROOT="$nested"
    BUN_INSTALL="$TEST_DIR" "$TEST_DIR/bin/bun" "$nested/test.js" "$TEST_DIR/bin/bun"
    unset BUN_FAKE_ROOT
}

test_native_modules() {
    BUN_INSTALL="$TEST_DIR" "$TEST_DIR/bin/bun" "$SCRIPT_DIR/native-modules/test.js"
}

test_sigaction_interposition() {
    BUN_INSTALL="$TEST_DIR" timeout 10 "$TEST_DIR/bin/bun" "$SCRIPT_DIR/native-modules/test-sigaction-interposition.js"
}

test_sigsys_oneshot() {
    # Death test: emulation ends in the default disposition, 128 + SIGSYS.
    local rc=0 out
    out=$(BUN_INSTALL="$TEST_DIR" timeout 10 "$TEST_DIR/bin/bun" \
        "$SCRIPT_DIR/native-modules/test-sigsys-oneshot.js") || rc=$?
    [ "$rc" -eq 159 ] && [ "$out" = fired ]
}

test_pidfd_open_trap() {
    BUN_INSTALL="$TEST_DIR" timeout 10 "$SCRIPT_DIR/trap-launcher" \
        "$TEST_DIR/bin/bun" "$SCRIPT_DIR/test-pidfd-open-trap.js"
}

test_native_modules_compiled() {
    BUN_INSTALL="$TEST_DIR" "$TEST_DIR/bin/bun" build --compile \
        --outfile "$TEST_DIR/native-test" \
        "$SCRIPT_DIR/native-modules/test.js" \
        "$SCRIPT_DIR/native-modules/lib/simple.so" \
        "$SCRIPT_DIR/native-modules/addon/simple.node" || return

    local rc=0 out
    out=$(BUN_INSTALL="$TEST_DIR" "$TEST_DIR/native-test") || rc=$?
    echo "$out"
    # If payload detection fails, the binary runs as the Bun CLI, which exits 0.
    [ "$rc" -eq 0 ] && grep -q '^PASS: ' <<< "$out"
}

# Main
setup_test_env

echo "=== Running Tests ==="
run_test "Bun version works" test_version
run_test "/proc/self/exe preservation" test_proc_self_exe
run_test "Environment variable passthrough" test_env_passthrough
run_test "/proc/stat spoofing" test_os_cpus
run_test "Shebang redirection" test_node_shebang
run_test "DNS lookup" test_dns_lookup
run_test "Nested BUN_FAKE_ROOT inheritance" test_nested_fake_root
run_test "Native modules (.so and .node)" test_native_modules
run_test "Sigaction interposition of SIGSYS" test_sigaction_interposition
run_test "One-shot SA_RESETHAND emulation" test_sigsys_oneshot
run_test "Seccomp-trapped pidfd_open falls back to ENOSYS" test_pidfd_open_trap
run_test "Native modules compiled binary" test_native_modules_compiled

echo "=== Summary ==="
echo -e "${GREEN}All $TESTS_PASSED tests passed!${NC}"
