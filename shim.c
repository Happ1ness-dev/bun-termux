/* Directory redirection shim for Bun on Termux */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

static const char *SAFE_DIR = NULL;
static int safe_dir_fd = -1;

static int (*real_openat)(int, const char *, int, ...) = NULL;
static int (*real_openat64)(int, const char *, int, ...) = NULL;

__attribute__((constructor))
static void init_shim(void) {
    real_openat = dlsym(RTLD_NEXT, "openat");
    real_openat64 = dlsym(RTLD_NEXT, "openat64");
    if (!real_openat || !real_openat64) {
        const char msg[] = "bun-shim: failed to resolve openat symbols\n";
        syscall(SYS_write, STDERR_FILENO, msg, sizeof(msg) - 1);
        _exit(1);
    }

    SAFE_DIR = getenv("BUN_FAKE_ROOT");
    if (!SAFE_DIR) SAFE_DIR = getenv("TMPDIR");
    if (!SAFE_DIR) SAFE_DIR = "/data/data/com.termux/files/usr/tmp";
    safe_dir_fd = real_openat(AT_FDCWD, SAFE_DIR,
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
}

static int get_safe_dir_fd(void) {
    return safe_dir_fd;
}

static int should_redirect(const char *pathname) {
    if (!pathname) return 0;
    size_t len = strlen(pathname);
    if (len == 1 && pathname[0] == '/') return 1;
    if (len == 5 && memcmp(pathname, "/data", 5) == 0) return 1;
    if (len == 6 && memcmp(pathname, "/data/", 6) == 0) return 1;
    if (len == 10 && memcmp(pathname, "/data/data", 10) == 0) return 1;
    if (len == 11 && memcmp(pathname, "/data/data/", 11) == 0) return 1;
    return 0;
}

static int do_openat(int (*real_fn)(int, const char *, int, ...),
                     int dirfd, const char *pathname, int flags, va_list ap) {
    if ((flags & O_DIRECTORY) && should_redirect(pathname)) {
        int safe_fd = get_safe_dir_fd();
        if (safe_fd >= 0) {
            return dup(safe_fd);
        }
    }

    if (flags & O_CREAT) {
        mode_t mode = va_arg(ap, mode_t);
        return real_fn(dirfd, pathname, flags, mode);
    }
    return real_fn(dirfd, pathname, flags);
}

int openat(int dirfd, const char *pathname, int flags, ...) {
    va_list ap;
    va_start(ap, flags);
    int result = do_openat(real_openat, dirfd, pathname, flags, ap);
    va_end(ap);
    return result;
}

int openat64(int dirfd, const char *pathname, int flags, ...) {
    va_list ap;
    va_start(ap, flags);
    int result = do_openat(real_openat64, dirfd, pathname, flags, ap);
    va_end(ap);
    return result;
}
