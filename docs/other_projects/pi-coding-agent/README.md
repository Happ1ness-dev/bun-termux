# Running Pi Coding Agent with Bun-Termux

[Learn more about Pi Coding Agent](https://github.com/badlogic/pi-mono/blob/main/packages/coding-agent/README.md)

> [!WARNING]  
> Do not report any issues with Pi to the original author unless you're 100% sure they're not caused by Bun-Termux.

## Prerequisites

- Bun and Bun-Termux installed (See [Quick Start](../../../README.md#quick-start))
- `node`, `npm`, `npx` installed (needed for build script, `pkg install nodejs`)
- `libpixman`, `libcairo`, `pango`, `xorgproto` installed (needed for canvas, `pkg install libpixman libcairo pango xorgproto`)
- Cloned pi-mono repo (`git clone https://github.com/badlogic/pi-mono`)

---

## Installation

Once you're inside the pi-mono repo, you can proceed further.

### Step 1: Install dependencies

```bash
# `GYP_DEFINES` and `--verbose` are needed for canvas to build successfully (I'm serious, verbose is necessary...)
# `--linker=hoisted` to make the node_modules structure similar to npm
GYP_DEFINES="android_ndk_path=''" BUN_OPTIONS="--os=linux --verbose --linker=hoisted" bun install

# Some deps still want Android, we need to install the rest
BUN_OPTIONS="--os=android --linker=hoisted" bun install
```

### Step 2: Patch TypeScript

TypeScript tries to find an Android package that doesn't exist. Make it look for the Linux package instead.

```bash
find node_modules -path "*@typescript/native-preview*" -name "getExePath.js" -exec sed -i 's/"native-preview-" + process.platform/"native-preview-" + (process.platform === "android" ? "linux" : process.platform)/g' {} \;
```

### Step 3: Build Pi

```bash
cd packages/coding-agent/

# Previous builds leave `dist`, so remove it just in case
bun run clean

# `--bytecode` makes the binary smaller and faster to load, but requires `--format=esm` for extensions to work
# `--shell=system` somehow makes `shx cp` fail on long command chains in `copy-binary-assets`. Seems like a bash 5.3.0(1) bug because dash 0.5.12 works fine.
BUN_OPTIONS="--minify --sourcemap --bytecode --target=bun --format=esm --shell=bun" bun run build:binary

# Needed for the /export command to work, since `copy-binary-assets` doesn't copy these for some reason.
cp src/core/export-html/template.css src/core/export-html/template.js dist/export-html/
```

### Step 4: Symlink Pi Globally

Inside `packages/coding-agent`, run:

```bash
ln -sf "$PWD/dist/pi" "$BUN_INSTALL/bin/"
```

Now your `pi` should be available and working.

Verify it works:
```bash
pi --version
```

---

## Consecutive Runs

After initial setup, `pi` is available globally via `$BUN_INSTALL/bin/pi`.
Make sure `$BUN_INSTALL/bin` is in your `PATH`.

```bash
pi
```

## Updating

```bash
cd /path/to/pi-mono
git checkout -- packages/ai/src/models.generated.ts && \
git pull --rebase --autostash

# If you encounter weird errors when updating, you might want to nuke node_modules:
# rm -rf node_modules packages/*/node_modules
```

Then just repeat steps 1-3.

---

## Known Issues

### `Duplicate package path`

If you're running `bun install`, you might see something like:
```
bun install v1.3.10 (30e609e0)
434 |     "@mariozechner/pi-coding-agent": ["@mariozechner/pi-coding-agent@0.30.2", "", {
          ^
error: Duplicate package path
    at bun.lock:434:5
InvalidPackageKey: failed to parse lockfile: 'bun.lock'
```

Just ignore it and continue. It's usually harmless.
