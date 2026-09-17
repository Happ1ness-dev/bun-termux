#!/usr/bin/env bun
/**
 * Exercises the sigaction interposition via a glibc-linked probe. The
 * interpose probe reads the prior disposition itself, so run order does
 * not matter.
 */

import { dlopen, FFIType } from 'bun:ffi';
import soPath from './lib/sigsys-probe.so' with { type: "file" };

const lib = dlopen(soPath, {
    sigsys_roundtrip_probe: { returns: FFIType.int, args: [] },
    sigsys_interpose_probe: { returns: FFIType.int, args: [] },
    sigsys_flags_probe: { returns: FFIType.int, args: [] },
    sigsys_ign_probe: { returns: FFIType.int, args: [] },
});

function check(name, rc) {
    if (rc !== 0) {
        console.error(`FAIL: ${name}: probe returned ${rc}`);
        process.exit(1);
    }
    console.log(`PASS: ${name}`);
}

check('save/restore round-trip', lib.symbols.sigsys_roundtrip_probe());
check('query and oldact report the saved disposition', lib.symbols.sigsys_interpose_probe());
check('flags pass through', lib.symbols.sigsys_flags_probe());
check('SIG_IGN swallows a raise', lib.symbols.sigsys_ign_probe());
