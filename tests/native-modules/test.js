#!/usr/bin/env bun
/**
 * Native module test for bun-termux
 * Tests both .so (FFI) and .node (Node-API) loading
 * 
 * Works in both normal mode and compiled mode:
 *   bun run test.js
 *   bun build --compile test.js ./lib/simple.so ./addon/simple.node --outfile native-test
 */

import { dlopen, FFIType } from 'bun:ffi';

// Import .so as file asset - returns real path in normal mode, $bunfs path in compiled mode
import soPath from './lib/simple.so' with { type: "file" };

let passed = 0;
let failed = 0;

function test(name, fn) {
    try {
        fn();
        console.log(`PASS: ${name}`);
        passed++;
    } catch (err) {
        console.log(`FAIL: ${name}: ${err.message}`);
        failed++;
    }
}

console.log("\n--- Testing .so library (bun:ffi) ---");

test("dlopen .so file", () => {
    const lib = dlopen(soPath, {
        add: { returns: FFIType.int, args: [FFIType.int, FFIType.int] },
    });
    if (!lib.symbols.add) throw new Error("add symbol not found");
});

test("call add(5, 3)", () => {
    const lib = dlopen(soPath, {
        add: { returns: FFIType.int, args: [FFIType.int, FFIType.int] },
    });
    const result = lib.symbols.add(5, 3);
    if (result !== 8) throw new Error(`expected 8, got ${result}`);
});

console.log("\n--- Testing .node addon (Node-API) ---");

test("require .node file", () => {
    const addon = require("./addon/simple.node");
    if (typeof addon.multiply !== 'function') throw new Error("multiply not a function");
});

test("call multiply(4, 7)", () => {
    const addon = require("./addon/simple.node");
    const result = addon.multiply(4, 7);
    if (result !== 28) throw new Error(`expected 28, got ${result}`);
});

console.log("\n--- Summary ---");
console.log(`Passed: ${passed}, Failed: ${failed}`);

if (failed > 0) {
    process.exit(1);
}
