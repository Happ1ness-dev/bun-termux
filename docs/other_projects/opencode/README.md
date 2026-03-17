# Running OpenCode with Bun-Termux

## Prerequisites

- Bun and Bun-Termux installed (See [Quick Start](../../../README.md#quick-start))

## Option 1: Installation with bun install

For installation with `bun install`, refer to [this guide](https://github.com/Happ1ness-dev/bun-termux/discussions/3#discussion-9601980) by [TotoCodeFR](https://github.com/TotoCodeFR).

## Option 2: Direct Installation

Download the latest OpenCode for Linux Arm64
```bash
curl -LO https://github.com/anomalyco/opencode/releases/latest/download/opencode-linux-arm64.tar.gz
```

Extract it to your preferred directory
```bash
mkdir -p ~/.opencode/bin/
tar -xzf opencode-linux-arm64.tar.gz -C ~/.opencode/bin/ opencode
```

Patch the opencode binary to use wrapper instead of bun
```bash
# Assuming you've cloned `bun-termux` to Termux home
python ~/bun-termux/helper_scripts/replace_runtime.py ~/.opencode/bin/opencode
```

Open your shell login config in an editor and add opencode to PATH
(e.g. `~/.bashrc`)
```bash
# OpenCode
export PATH="$HOME/.opencode/bin:$PATH"
```

Then source it
```bash
source ~/.bashrc
```

Now opencode is available. Test it by running `opencode`.