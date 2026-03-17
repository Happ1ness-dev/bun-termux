<h1 align="center">Bun-Termux</h1>

<p align="center">
  Run Bun on Termux (Android) without proot. <b>aarch64 only.</b>
</p>

<p align="center">
  <a href="docs/README.md#installation">Installation</a>
  &nbsp;&nbsp;|&nbsp;&nbsp;
  <a href="docs/README.md">Documentation</a>
  &nbsp;&nbsp;|&nbsp;&nbsp;
  <a href="docs/troubleshooting.md">Known Issues</a>
  &nbsp;&nbsp;|&nbsp;&nbsp;
  <a href="#credits">Credits</a>
</p>

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

# "--backend=copyfile" is REQUIRED on Android
BUN_OPTIONS="--backend=copyfile --os=android" bun install -g cowsay

cowsay "bun-termux works!"
```

## How It Works

Bun is built for glibc, not Android's bionic libc, and needs access to paths that Android restricts. bun-termux solves this with two components:

**1. Wrapper (`bun`)**
Uses "userland exec" to launch the real Bun binary through glibc's dynamic linker. This technique means `bun build --compile` outputs embed the wrapper, making binaries work.

**2. LD_PRELOAD Shim (`bun-shim.so`)**
Intercepts system calls to work around Android restrictions:
- Redirects `/`, `/data`, `/storage` → fake root directory (avoids permission denied)
- Redirects shebang paths (e.g. `/usr/bin/env`) → Termux prefix
- Fakes `/proc/stat` → makes `os.cpus()` work

See [docs/README.md](docs/README.md#how-it-works) for the full technical breakdown.

## Environment Variables

| Variable | Description |
|----------|-------------|
| `BUN_INSTALL` | Installation prefix (`~/.bun`) |
| `BUN_BINARY_PATH` | Override path to original bun binary |
| `BUN_OPTIONS` | Used by Bun for options/args |

See [docs/README.md](docs/README.md#environment-variables) for all variables and defaults.

## Limitations

1. aarch64 only, because of hardcoded assembly and syscalls. Maybe I'll add support for other architectures in the future.
2. Binaries built with `bun build --compile` have wrapper embedded, requiring `buno`, `bun-shim.so` and glibc to be present on the system where they run.
3. Bun install/add/update/remove commands still require `BUN_OPTIONS="--backend=copyfile"` env var due to Android being Android.
4. If bun somehow fails to walk the current path due to permission error, it'll fail to get the current env vars too. I'll have to investigate why.


## Troubleshooting

For troubleshooting, refer to [troubleshooting.md](docs/troubleshooting.md)

## Credits

This project is based on the userland exec technique from [bun-termux-loader](https://github.com/kaan-escober/bun-termux-loader) by [@kaan-escober](https://github.com/kaan-escober).

While bun-termux-loader focuses on creating self-contained bundled binaries with embedded Bun runtimes, this project takes a simpler approach: a lightweight wrapper that enables the standard Bun installation to work on Termux, including `bun build --compile` output, without embedding the entire Bun runtime.

## License

MIT License - see [LICENSE](LICENSE) file for details.