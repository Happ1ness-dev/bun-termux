/*
 * Test that verifies /proc/self/exe preservation through the wrapper
 * When run through the wrapper, /proc/self/exe should point to the wrapper
 * (not this test binary or ld), which is key for binaries built with 'bun build --compile' to work
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main() {
    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len < 0) {
        perror("readlink /proc/self/exe failed");
        return 1;
    }
    exe_path[len] = '\0';

    printf("Testing /proc/self/exe behavior...\n");
    printf("  Current /proc/self/exe: %s\n", exe_path);

    // When run through the wrapper, /proc/self/exe should point to the wrapper
    // (bun-termux or ~/.bun/bin/bun), NOT to this test binary
    if (strstr(exe_path, "test-proc-self-exe")) {
        printf("  FAIL: Not running through wrapper (expected for standalone test)\n");
        printf("\n");
        printf("Note: To verify wrapper behavior, run through wrapper:\n");
        printf("      BUN_BINARY_PATH=./tests/test-proc-self-exe ~/.bun/bin/bun\n");
        return 1;
    }

    if (strstr(exe_path, "/bun") || strstr(exe_path, "bun-termux")) {
        printf("  OK: /proc/self/exe points to wrapper path\n");
        return 0;
    }

    fprintf(stderr, "FAIL: /proc/self/exe points to unexpected path: %s\n", exe_path);
    return 1;
}
