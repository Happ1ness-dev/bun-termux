# Simple Bun Termux Wrapper Makefile
# Builds shim and installs it with a lightweight wrapper

# Architecture detection
HOST_ARCH := $(shell uname -m)
ifeq ($(HOST_ARCH),aarch64)
    TARGET_ARCH := aarch64
else ifeq ($(HOST_ARCH),x86_64)
    TARGET_ARCH := x86_64
else
    $(error Unsupported architecture: $(HOST_ARCH))
endif

# Architecture-specific settings
ifeq ($(TARGET_ARCH),aarch64)
    LD_SO_NAME = ld-linux-aarch64.so.1
else ifeq ($(TARGET_ARCH),x86_64)
    LD_SO_NAME = ld-linux-x86-64.so.2
endif

# Compiler configuration
# Wrapper: native Android binary (runs in Termux environment)
WRAPPER_CC = clang --target=$(TARGET_ARCH)-linux-android

# Shim and tests: glibc Linux binaries (run under glibc loader, not Android libc)
SHIM_CC = clang --target=$(TARGET_ARCH)-linux-gnu

BUN_PREFIX := $(if $(BUN_INSTALL),$(BUN_INSTALL),$(HOME)/.bun)

BUN_BIN_DIR = $(BUN_PREFIX)/bin
BUN_LIB_DIR = $(BUN_PREFIX)/lib
BUN_TMP_DIR = $(BUN_PREFIX)/tmp

CFLAGS = -Wall -Wextra -Werror=implicit-function-declaration \
         -Werror=format-security -O2

SHIM_CFLAGS = $(CFLAGS) -shared -fPIC -nostdlib
WRAPPER_CFLAGS = $(CFLAGS)

# Paths (GLIBC_ROOT can be overridden via env or make arg)
GLIBC_ROOT ?= /data/data/com.termux/files/usr/glibc
GLIBC_INC = $(GLIBC_ROOT)/include
GLIBC_LIB = $(GLIBC_ROOT)/lib

# Find original bun binary for compilation (buno preferred, fallback to bun)
ORIGINAL_BUN := $(shell [ -x $(HOME)/.bun/bin/buno ] && echo $(HOME)/.bun/bin/buno || echo $(HOME)/.bun/bin/bun)

all: bun-termux bun-shim.so

bun-shim.so: shim.c
	@echo "Building shim against glibc..."
	$(SHIM_CC) $(SHIM_CFLAGS) \
		-I$(GLIBC_INC) \
		-L$(GLIBC_LIB) \
		-Wl,-rpath,$(GLIBC_LIB) \
		-Wl,-rpath-link,$(GLIBC_LIB) \
		-o $@ $< \
		-l:libc.so.6 -l:libdl.so.2

bun-termux: bun-termux.c
	@echo "Building wrapper for $(TARGET_ARCH)..."
	$(WRAPPER_CC) $(WRAPPER_CFLAGS) -o $@ $<

install: bun-termux bun-shim.so
	@echo "Installing to $(BUN_PREFIX)..."
	mkdir -p $(BUN_BIN_DIR)
	mkdir -p $(BUN_LIB_DIR)
	mkdir -p $(BUN_TMP_DIR)
	@if [ ! -f "$(BUN_BIN_DIR)/buno" ]; then \
		echo "buno not found, renaming original bun to buno..."; \
		mv "$(BUN_BIN_DIR)/bun" "$(BUN_BIN_DIR)/buno"; \
	fi
	cp bun-termux $(BUN_BIN_DIR)/bun
	cp bun-shim.so $(BUN_LIB_DIR)/bun-shim.so
	cp helper_scripts/bunx $(BUN_BIN_DIR)/bunx
	chmod +x $(BUN_BIN_DIR)/bunx
	@echo "Installed!"
	@echo "  Wrapper: $(BUN_BIN_DIR)/bun"
	@echo "  Original: $(BUN_BIN_DIR)/buno"
	@echo "  Shim:    $(BUN_LIB_DIR)/bun-shim.so"
	@echo "  Helper:  $(BUN_BIN_DIR)/bunx"
	@echo "Run '$(BUN_BIN_DIR)/bun --version' to test"

# Test targets
TEST_DIR = tests
TEST_TMP_DIR = $(TEST_DIR)/tmp_bun_install
NATIVE_DIR = $(TEST_DIR)/native-modules
NODE_INCLUDE = /data/data/com.termux/files/usr/include/node
test-setup: all test-proc-self-exe test-native-modules
	@echo "Setting up test environment..."
	@if [ -z "$(TEST_TMP_DIR)" ]; then echo "Error: env var TEST_TMP_DIR is empty"; exit 1; fi
	@if [ "$(TEST_TMP_DIR)" = "/" ]; then echo "Error: TEST_TMP_DIR is root"; exit 1; fi
	@rm -rf $(TEST_TMP_DIR)
	@mkdir -p $(TEST_TMP_DIR)/bin $(TEST_TMP_DIR)/lib $(TEST_TMP_DIR)/tmp/fake-root
	@cp bun-termux $(TEST_TMP_DIR)/bin/bun
	@cp bun-shim.so $(TEST_TMP_DIR)/lib/bun-shim.so
	@ln -s $(ORIGINAL_BUN) $(TEST_TMP_DIR)/bin/buno
	@echo "Test environment ready at $(TEST_TMP_DIR)"

test-clean:
	@if [ -z "$(TEST_TMP_DIR)" ]; then echo "Error: TEST_TMP_DIR is empty"; exit 1; fi
	@if [ "$(TEST_TMP_DIR)" = "/" ]; then echo "Error: TEST_TMP_DIR is root"; exit 1; fi
	@rm -rf $(TEST_TMP_DIR)
	@echo "Test environment cleaned"

test-compile-full: test-setup
	@bash $(TEST_DIR)/test-compile-full.sh

test-proc-self-exe: $(TEST_DIR)/test-proc-self-exe.c
	@echo "Building /proc/self/exe test..."
	@$(SHIM_CC) -c $(CFLAGS) -I$(GLIBC_INC) -o $(TEST_DIR)/test-proc-self-exe.o $(TEST_DIR)/test-proc-self-exe.c
	@env LD_PRELOAD= $(GLIBC_ROOT)/bin/ld.bfd -dynamic-linker $(GLIBC_LIB)/$(LD_SO_NAME) \
		-o $(TEST_DIR)/test-proc-self-exe \
		$(GLIBC_LIB)/crt1.o $(GLIBC_LIB)/crti.o \
		$(TEST_DIR)/test-proc-self-exe.o \
		-L$(GLIBC_LIB) -lc \
		$(GLIBC_LIB)/crtn.o
	@rm -f $(TEST_DIR)/test-proc-self-exe.o

# Native module test targets
$(NATIVE_DIR)/lib/simple.so: $(NATIVE_DIR)/lib/simple.c
	@echo "Building native .so library..."
	@$(SHIM_CC) -shared -fPIC -nostdlib \
		-I$(GLIBC_INC) -L$(GLIBC_LIB) \
		-Wl,-rpath,$(GLIBC_LIB) -Wl,-rpath-link,$(GLIBC_LIB) \
		-o $@ $< -l:libc.so.6

$(NATIVE_DIR)/addon/simple.node: $(NATIVE_DIR)/addon/simple.c
	@echo "Building native .node addon..."
	@$(SHIM_CC) -c -fPIC \
		-I$(GLIBC_INC) -I$(NODE_INCLUDE) \
		-o $(NATIVE_DIR)/addon/simple.o $<
	@env LD_PRELOAD= $(GLIBC_ROOT)/bin/ld.bfd -shared \
		-dynamic-linker $(GLIBC_LIB)/$(LD_SO_NAME) \
		-o $@ $(NATIVE_DIR)/addon/simple.o -L$(GLIBC_LIB) -l:libc.so.6
	@rm -f $(NATIVE_DIR)/addon/simple.o

test-native-modules: $(NATIVE_DIR)/lib/simple.so $(NATIVE_DIR)/addon/simple.node
	@echo "Native modules built successfully"

tests: test-setup
	@echo "Running tests..."
	@chmod +x $(TEST_DIR)/run-tests.sh
	@bash $(TEST_DIR)/run-tests.sh

clean:
	rm -f bun-shim.so bun-termux
	rm -f $(TEST_DIR)/test-proc-self-exe
	rm -f $(TEST_DIR)/*.o
	rm -f $(NATIVE_DIR)/lib/simple.so
	rm -f $(NATIVE_DIR)/addon/simple.node
	rm -f $(NATIVE_DIR)/addon/*.o
	rm -rf $(TEST_TMP_DIR)

.PHONY: all install uninstall clean tests test-setup test-clean test-compile-full test-native-modules

uninstall:
	@echo "Uninstalling from $(BUN_PREFIX)..."
	@-mv "$(BUN_BIN_DIR)/buno" "$(BUN_BIN_DIR)/bun" 2>/dev/null && echo "Restored original bun"
	@-rm -f "$(BUN_LIB_DIR)/bun-shim.so"
	@-rm -f "$(BUN_BIN_DIR)/bunx"
	@-rmdir "$(BUN_LIB_DIR)" 2>/dev/null
	@-rmdir "$(BUN_TMP_DIR)/fake-root" 2>/dev/null
	@-rmdir "$(BUN_TMP_DIR)" 2>/dev/null
	@echo "Uninstall complete"
