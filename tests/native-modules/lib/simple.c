// Simple C library for FFI testing
#include <stdio.h>
#include <string.h>

const char* greet(const char* name) {
    static char buffer[256];
    snprintf(buffer, sizeof(buffer), "Hello from .so: %s!", name);
    return buffer;
}

int add(int a, int b) {
    return a + b;
}
