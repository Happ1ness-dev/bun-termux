# Bun-Termux Documentation

## Table of Contents
1. [Installation](#installation)
2. [Usage](#usage)
3. [How It Works](#how-it-works)
4. [Environment Variables](#environment-variables)
5. [Limitations](#limitations)
6. [Bundled Binaries](#bundled-binaries)
7. [Troubleshooting](#troubleshooting)

## Installation

### Prerequisites

Normal use:
- Termux with glibc installed (`pkg install glibc-repo; pkg install glibc-runner`)
- Original Bun installed with [official script](https://github.com/oven-sh/bun?tab=readme-ov-file#install) at `~/.bun/bin/bun` (or `buno` if already renamed)
- Clang compiler (`pkg install clang`)

For tests:
- `file` (`pkg install file`)
- `nodejs` (`pkg install nodejs`)

### Build

```bash
make
```

This will compile the wrapper and the LD_PRELOAD shim.

### Install

```bash
make install
```

Installs to `~/.bun/` by default (defined by `BUN_INSTALL`).

### Uninstall

To remove bun-termux and restore the original Bun binary:

```bash
make uninstall
```

This will:
- Restore the original `bun` binary (renamed back from `buno`)
- Remove `bun-shim.so` from the lib directory
- Clean up empty directories

## Usage

Use as normal Bun. (e.g. `bun script.js`)

For bun package manager, set `BUN_OPTIONS` to `--backend=copyfile` to avoid permission errors. Some native modules might need `--backend=copyfile --os=android`.

For bundled binaries, see [Bundled Binaries](#bundled-binaries).

Also see [other_projects/README.md](other_projects/README.md) for guides and examples of Bun-Termux usage with other projects (e.g. OpenCode).

## How It Works

1. Wrapper uses userland exec to replace itself with glibc's dynamic linker (`ld-linux-aarch64.so.1`) without calling `execve()` - since the kernel never updates `/proc/self/exe`, it still points to the wrapper, so `bun build --compile` embeds the wrapper (not bun itself, and not the linker like when using grun), making compiled binaries work out of the box.
2. Wrapper sets `BUN_FAKE_ROOT` env var if it's unset.
3. Shim preloads via the dynamic linker's `--preload` option and intercepts `openat()` on `/`, `/data`, `/data/data`, `/storage`, `/storage/emulated`, `/storage/emulated/0` (including trailing slashes). When `BUN_FAKE_ROOT` is set, these paths are redirected to that directory to avoid permission issues on Android.
4. Shim intercepts `execve()` for shebangs beginning with `/usr/bin/`, `/bin/`, `/usr/sbin/`, `/sbin/`, and redirects them to use `PREFIX`.
5. The shim intercepts reads to `/proc/stat` and generates minimal CPU statistics stub, allowing `os.cpus()` to work in bun.
6. `--library-path` is passed to the dynamic linker to make sure glibc libraries are found.
7. If `BUN_FAKE_ROOT` is not set, the shim falls back to `TMPDIR` (or `/data/data/com.termux/files/usr/tmp`).

## Environment Variables

| Variable | Default | Scope | Description |
|----------|---------|-------|-------------|
| `BUN_INSTALL` | `~/.bun` | Both | Installation prefix. Controls where the wrapper looks for `buno`, `bun-shim.so`, and the fake-root directory |
| `BUN_BINARY_PATH` | `$BUN_INSTALL/bin/buno` if `BUN_INSTALL` is set, otherwise `<wrapper_dir>/buno` | Runtime | Override the path to the original bun binary |
| `BUN_FAKE_ROOT` | `$(BUN_INSTALL)/tmp/fake-root` | Runtime | Set by the wrapper only if not already present; used by the shim as the redirect target for `/`, `/data`, `/data/data` |
| `BUN_OPTIONS` | Unset | Runtime | Pass options to Bun (e.g. `--backend=copyfile --os=android` for package manager operations) |
| `PREFIX` | `/data/data/com.termux/files/usr` | Runtime | Termux installation prefix; used by shim for shebang path translation |
| `TMPDIR` | `/data/data/com.termux/files/usr/tmp` | Runtime | Temporary directory for shim (fallback if `BUN_FAKE_ROOT` is unset) |
| `GLIBC_ROOT` | `/data/data/com.termux/files/usr/glibc` | Build | Build-time override for glibc installation path (Makefile only) |
| `GLIBC_LD_SO` | `/data/data/com.termux/files/usr/glibc/lib/ld-linux-aarch64.so.1` | Both | Path to glibc's dynamic linker |
| `GLIBC_LIB` | `/data/data/com.termux/files/usr/glibc/lib` | Both | Directory containing glibc shared libraries |

Scope: `Runtime` = read by wrapper/shim at runtime, `Build` = used by Makefile only, `Both` = used by both Makefile and runtime

## Limitations

1. aarch64 only, because of hardcoded assembly and syscalls. Maybe I'll add support for other architectures in the future.
2. Binaries built with `bun build --compile` have wrapper embedded, requiring `buno`, `bun-shim.so` and glibc to be present on the system where they run.
3. Bun install/add/update/remove commands still require `BUN_OPTIONS="--backend=copyfile"` env var due to Android being Android.
4. If bun somehow fails to walk the current path due to permission error, it'll fail to get the current env vars too. I'll have to investigate why.

## Bundled Binaries

Binaries produced with `bun build --compile` already have the wrapper embedded, but if you need to run a binary that's been bundled elsewhere, you can use `replace_runtime.py` script to replace default Bun runtime with the wrapper.

```
python helper_scripts/replace_runtime.py path/to/your/bundled_binary
```

This will back up the original binary with a `.bak` extension and patch it to use the wrapper.

## Troubleshooting

See [troubleshooting.md](troubleshooting.md) for common issues and solutions.