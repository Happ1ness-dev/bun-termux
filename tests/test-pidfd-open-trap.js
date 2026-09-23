// pidfd_open is trapped with SECCOMP_RET_TRAP by the launcher running
// this script (issue #15).
// The shim must answer the trap with ENOSYS so spawn takes its
// fallback path, and must still forward user-raised SIGSYS to Bun's handler.

import { dlopen, FFIType } from 'bun:ffi';
import soPath from './native-modules/lib/sigsys-probe.so' with { type: "file" };

const probe = dlopen(soPath, {
  trap_probe_getsid_errno: { returns: FFIType.int, args: [] },
});

// Removal exercises Bun's glibc signal() path and must precede the first
// spawn. Once a spawn takes the ENOSYS fallback, Bun switches to its
// waiter thread permanently.
const noop = () => {};
process.on('SIGSYS', noop);
process.off('SIGSYS', noop);

let proc = Bun.spawn(['echo', 'spawn-ok']);
let out = (await new Response(proc.stdout).text()).trim();
let code = await proc.exited;
if (out !== 'spawn-ok' || code !== 0) {
  console.error('FAIL: spawn produced "' + out + '" exit ' + code);
  process.exit(1);
}
console.log('PASS: spawn works after listener removal');

// Registered before the spawn on purpose. The pre-exec child shares our
// memory, and registering after would mask clobbering of the saved
// forwarding target by re-saving it.
let fired = false;
process.on('SIGSYS', () => { fired = true; });

proc = Bun.spawn(['echo', 'spawn-ok']);
out = (await new Response(proc.stdout).text()).trim();
code = await proc.exited;
if (out !== 'spawn-ok' || code !== 0) {
  console.error('FAIL: second spawn produced "' + out + '" exit ' + code);
  process.exit(1);
}
console.log('PASS: spawn works with a listener registered');

// Positive evidence the trap fired, and it must not reach the listener.
if (probe.symbols.trap_probe_getsid_errno() !== 38 /* ENOSYS */) {
  console.error('FAIL: no evidence the trap fired');
  process.exit(1);
}
console.log('PASS: trap fired (getsid answered ENOSYS)');
if (fired) {
  console.error('FAIL: seccomp trap reached the SIGSYS listener');
  process.exit(1);
}
console.log('PASS: trap did not reach the SIGSYS listener');

process.kill(process.pid, 'SIGSYS');
await new Promise((r) => setTimeout(r, 100));
if (!fired) {
  console.error('FAIL: SIGSYS listener did not fire');
  process.exit(1);
}
console.log('PASS: user-raised SIGSYS reaches the listener');
