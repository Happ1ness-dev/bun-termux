#!/usr/bin/env bun
/**
 * One-shot (SA_RESETHAND) emulation is observable only as process death.
 * The second raise must kill with SIGSYS. The harness asserts the exit
 * code, so reaching the bottom of this file means emulation is broken.
 */

import { dlopen, FFIType } from 'bun:ffi';
import soPath from './lib/sigsys-probe.so' with { type: "file" };

const lib = dlopen(soPath, {
    sigsys_oneshot_probe: { returns: FFIType.int, args: [] },
});

const rc = lib.symbols.sigsys_oneshot_probe();
console.error(`FAIL: survived second raise, probe returned ${rc}`);
process.exit(1);
