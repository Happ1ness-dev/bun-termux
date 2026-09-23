/* Sigaction here resolves to the shim's interposed symbol,
 * the same path Bun's libc sigaction takes. */
#include <errno.h>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

static void my_handler(int s, siginfo_t *i, void *u) { (void)s; (void)i; (void)u; }
static void noop_handler(int s) { (void)s; }

/* oldact must report the previous saved disposition, a query the new one. */
int sigsys_interpose_probe(void) {
    struct sigaction act, before, old, cur;
    if (sigaction(SIGSYS, NULL, &before) < 0) return -1;
    act.sa_sigaction = my_handler;
    act.sa_flags = SA_SIGINFO;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGSYS, &act, &old) < 0) return -2;
    if (old.sa_sigaction != before.sa_sigaction) return -3;
    if (sigaction(SIGSYS, NULL, &cur) < 0) return -4;
    if (cur.sa_sigaction != my_handler) return -5;
    return 0;
}

/* A query reports the saved flags. Never raised, so SA_ONSTACK without an
 * altstack stays inert. */
int sigsys_flags_probe(void) {
    struct sigaction act, cur;
    const int flags = SA_RESTART | SA_ONSTACK | SA_NODEFER;
    act.sa_handler = noop_handler;
    act.sa_flags = flags;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGSYS, &act, NULL) < 0) return -1;
    if (sigaction(SIGSYS, NULL, &cur) < 0) return -2;
    return cur.sa_flags == flags ? 0 : -3;
}

/* SA_RESETHAND emulation: the first raise forwards and consumes the saved
 * handler, so the second is fatal. The write() is the only evidence that
 * survives. Reaching the return means emulation is broken. */
static void oneshot_handler(int s) {
    (void)s;
    write(STDOUT_FILENO, "fired\n", 6);
}
int sigsys_oneshot_probe(void) {
    struct sigaction act;
    act.sa_handler = oneshot_handler;
    act.sa_flags = SA_RESETHAND;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGSYS, &act, NULL) < 0) return -1;
    raise(SIGSYS);
    raise(SIGSYS);
    return 0;
}

/* Save/restore: reinstalling a queried disposition keeps forwarding to it. */
static volatile int g_roundtrip_fired;
static void roundtrip_handler(int s, siginfo_t *i, void *u) {
    (void)s; (void)i; (void)u;
    g_roundtrip_fired = 1;
}
int sigsys_roundtrip_probe(void) {
    struct sigaction mine, old, cur, swap;
    mine.sa_sigaction = roundtrip_handler;
    mine.sa_flags = SA_SIGINFO;
    sigemptyset(&mine.sa_mask);
    if (sigaction(SIGSYS, &mine, NULL) < 0) return -1;
    if (sigaction(SIGSYS, NULL, &old) < 0) return -2;
    if (old.sa_sigaction != roundtrip_handler) return -3;
    if (sigaction(SIGSYS, &old, NULL) < 0) return -4;
    if (sigaction(SIGSYS, NULL, &cur) < 0) return -5;
    if (cur.sa_sigaction != roundtrip_handler) return -6;
    raise(SIGSYS);
    if (!g_roundtrip_fired) return -7;
    /* act may alias oldact. The swap must still install the new handler. */
    swap.sa_sigaction = my_handler;
    swap.sa_flags = SA_SIGINFO;
    sigemptyset(&swap.sa_mask);
    if (sigaction(SIGSYS, &swap, &swap) < 0) return -8;
    if (swap.sa_sigaction != roundtrip_handler) return -9;
    if (sigaction(SIGSYS, NULL, &cur) < 0) return -10;
    if (cur.sa_sigaction != my_handler) return -11;
    if (sigaction(SIGSYS, &swap, NULL) < 0) return -12;
    g_roundtrip_fired = 0;
    raise(SIGSYS);
    return g_roundtrip_fired ? 0 : -13;
}

/* SIG_IGN: reaching the query proves the raise was swallowed. */
int sigsys_ign_probe(void) {
    struct sigaction act, cur;
    act.sa_handler = SIG_IGN;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGSYS, &act, NULL) < 0) return -1;
    raise(SIGSYS);
    if (sigaction(SIGSYS, NULL, &cur) < 0) return -2;
    if (cur.sa_handler != SIG_IGN) return -3;
    return 0;
}

/* ENOSYS here can only come from the shim answering the launcher's trap. */
int trap_probe_getsid_errno(void) {
    errno = 0;
    return syscall(SYS_getsid, 0) < 0 ? errno : 0;
}
