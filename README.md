# Bun-Termux

Run Bun on Termux (Android) without proot. **aarch64 only.**

## Quick Start

```bash
# Prerequisites
pkg install git curl build-essential glibc-repo
pkg install glibc-runner

# Bun install script will skip bashrc if it doesn't exist
touch ~/.bashrc

# Install Bun
curl -fsSL https://bun.sh/install | bash

# Make bun install visible
source ~/.bashrc

# Obtaining bun-termux source
git clone https://github.com/Happ1ness-dev/bun-termux.git
cd bun-termux

# Build and install wrapper
make && make install

# Test
bun --version
```

## Prerequisites

Normal use:
- Termux with glibc installed (`pkg install glibc-repo; pkg install glibc-runner`)
- Original Bun installed with [official script](https://github.com/oven-sh/bun?tab=readme-ov-file#install) at `~/.bun/bin/bun` (or `buno` if already renamed)
- Clang compiler

For tests:
- `file` (`pkg install file`)
- `nodejs` (`pkg install nodejs`)

## Build

```bash
make
```

This will compile the wrapper and the LD_PRELOAD shim.

## Install

```bash
make install
```

Installs to `~/.bun/` by default (defined by `BUN_INSTALL`).

## How It Works

1. Wrapper uses userland exec to replace itself with glibc's `ld.so` without calling `execve()` - since the kernel never updates `/proc/self/exe`, it still points to the wrapper, so `bun build --compile` embeds the wrapper (not bun itself, and not the ld library like when using grun), making compiled binaries work out of the box.
2. Shim preloads via `ld.so --preload` and intercepts `openat()` on `/`, `/data`, `/data/data` (including trailing slashes). When `BUN_FAKE_ROOT` is set, these paths are redirected to that directory to avoid permission issues on Android.
3. Wrapper sets `BUN_FAKE_ROOT` env var if it's unset. The shim uses this variable to know where to redirect `/`, `/data`, `/data/data`.
4. `--library-path` is passed to `ld.so` to make sure glibc libraries are found
5. If `BUN_FAKE_ROOT` is not set, the shim falls back to `TMPDIR` (`/data/data/com.termux/files/usr/tmp`).

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `BUN_INSTALL` | `~/.bun` | Installation prefix. Controls where the wrapper looks for `buno`, `bun-shim.so`, and the fake-root directory |
| `BUN_BINARY_PATH` | `$(BUN_INSTALL)/bin/buno` | Override the path to the original bun binary |
| `BUN_FAKE_ROOT` | `$(BUN_INSTALL)/tmp/fake-root` | Set by the wrapper only if not already present; used by the shim as the redirect target for `/`, `/data`, `/data/data`. |
| `GLIBC_LD_SO` | `/data/data/com.termux/files/usr/glibc/lib/ld-linux-aarch64.so.1` | Path to glibc's dynamic linker |
| `GLIBC_LIB` | `/data/data/com.termux/files/usr/glibc/lib` | Directory containing glibc shared libraries |
| `TMPDIR` | `/data/data/com.termux/files/usr/tmp` | Temporary directory for shim (fallback if `BUN_FAKE_ROOT` is unset) |


## Limitations

1. aarch64 only, because of hardcoded assembly and syscalls. Maybe I'll add support for other architectures in the future.
2. Binaries built with `bun build --compile` have wrapper embedded, requiring `buno`, `bun-shim.so` and glibc to be present on the system where they run.
3. Bun install/add/update/remove commands still require `BUN_OPTIONS="--backend=copyfile"` env var due to Android being Android.
4. If bun somehow fails to walk the current path due to permission error, it'll fail to get the current env vars too. I'll have to investigate why.

For troubleshooting, refer to [troubleshooting.md](troubleshooting.md)

## Credits

This project is based on the userland exec technique from [bun-termux-loader](https://github.com/kaan-escober/bun-termux-loader) by [@kaan-escober](https://github.com/kaan-escober).

While bun-termux-loader focuses on creating self-contained bundled binaries with embedded Bun runtimes, this project takes a simpler approach: a lightweight wrapper that enables the standard Bun installation to work on Termux, including `bun build --compile` output, without embedding the entire Bun runtime.

## License

MIT License - see [LICENSE](LICENSE) file for details.