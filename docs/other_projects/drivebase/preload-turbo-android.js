// Patches process.platform/arch for Turborepo on Android (Termux)
// Usage: NODE_OPTIONS="--require /path/to/preload-turbo-android.js" bun dev

const REAL_PLATFORM = process.platform;
const REAL_ARCH = process.arch;

function isTurboBinaryResolution() {
  const stack = new Error().stack || "";
  return (
    stack.includes("/turbo/bin/turbo") ||
    stack.includes("getBinaryPath") ||
    stack.includes("node_modules/turbo/")
  );
}

Object.defineProperty(process, "platform", {
  get() {
    return isTurboBinaryResolution() ? "linux" : REAL_PLATFORM;
  },
  enumerable: true,
  configurable: true,
});

Object.defineProperty(process, "arch", {
  get() {
    if (!isTurboBinaryResolution()) return REAL_ARCH;
    return REAL_ARCH === "x64" ? "64" : REAL_ARCH;
  },
  enumerable: true,
  configurable: true,
});

console.error(`[preload-turbo] Patched for Android (${REAL_PLATFORM} → linux)`);
