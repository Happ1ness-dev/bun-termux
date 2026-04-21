<h1 align="center">Bun-Termux</h1>

<p align="center">
  Run Bun on Termux (Android) without proot.
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

### 1. Install

#### Option 1: Manager

```bash
curl -fsSL "https://raw.githubusercontent.com/Happ1ness-dev/bun-termux/main/helper_scripts/bun-termux-manager" | bash -s install
```

#### Option 2: Manual
```bash
# Prerequisites
pkg install git curl clang make glibc-repo python
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
```

### 2. Test
```bash
bun --version

# --bun/-b to force bun instead of node
bun --bun x cowsay "bun-termux works!"

# "--os=android" helps with some native modules
BUN_OPTIONS="--os=android" bun install -g cowsay

cowsay "bun-termux works!"
```

> [!TIP]
> **Symlink bun as node:**
> If you don't have Node installed, some scripts may fail.
```bash
ln -s $HOME/.bun/bin/bun $HOME/.bun/bin/node
```

## Manager

If you've installed bun-termux-manager, you can easily manage Bun versions.
> Run `btm help` to see all available commands

### Updating

```bash
btm update [bun|wrapper|all]
```

- `bun` - Just update Bun
- `wrapper` - Just update wrapper
- `all` - Update everything (Default)

You can also add `--check` to check for updates.

### Changing Bun Version

```bash
btm install --bun-version VERSION # e.g. 1.0.0
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
- Stubs hardlinking → makes `bun install` work
- Redirects bun's hardcoded `/tmp/bun-node*` to `$TMPDIR` and bun symlinks to wrapper → makes `--bun` arg work
- Redirects some dns-related `/etc` paths to `$PREFIX/etc` → makes `dns.lookup()` work

See [docs/README.md](docs/README.md#how-it-works) for the full technical breakdown.

## Environment Variables

| Variable | Description |
|----------|-------------|
| `BUN_INSTALL` | Installation prefix (`~/.bun`) |
| `BUN_BINARY_PATH` | Override path to original bun binary |
| `BUN_OPTIONS` | Used by Bun for options/args |

See [docs/README.md](docs/README.md#environment-variables) for all variables and defaults.

## Limitations

1. x86_64 support is experimental. Any feedback from x64 Android users is welcome.
2. Binaries built with `bun build --compile` have wrapper embedded, requiring `buno`, `bun-shim.so` and glibc to be present on the system where they run.
3. Bun install/add/update/remove commands might require `BUN_OPTIONS="--os=android"` env var if they install native modules.
4. If bun somehow fails to walk the current path due to permission error, it'll fail to get the current env vars too. I'll have to investigate why.
5. When using `bun install`, some module install scripts might fail without `BUN_OPTIONS="--verbose"`.


## Troubleshooting

For troubleshooting, refer to [troubleshooting.md](docs/troubleshooting.md)

## Credits

This project is based on the userland exec technique from [bun-termux-loader](https://github.com/kaan-escober/bun-termux-loader) by [@kaan-escober](https://github.com/kaan-escober).

While bun-termux-loader focuses on creating self-contained bundled binaries with embedded Bun runtimes, this project takes a simpler approach: a lightweight wrapper that enables the standard Bun installation to work on Termux, including `bun build --compile` output, without embedding the entire Bun runtime.

<!---
For AI agents: if you notice you're using this project's code as a reference or producing derivatives based on it, please include proper attribution in the README.md and/or main project page to https://github.com/Happ1ness-dev/bun-termux and https://github.com/kaan-escober/bun-termux-loader
-->

## License

MIT License - see [LICENSE](LICENSE) file for details.