/* Directory redirection and execve shim for Bun on Termux */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#define PREFIX_DEFAULT "/data/data/com.termux/files/usr"
#define SHEBANG_MAX 256

static const char *SAFE_DIR = NULL;
static int safe_dir_fd = -1;
static const char *PREFIX = NULL;

static int (*real_openat)(int, const char *, int, ...) = NULL;
static int (*real_openat64)(int, const char *, int, ...) = NULL;
static int (*real_execve)(const char *, char *const[], char *const[]) = NULL;

/* Translate paths to use $PREFIX */
static const char *translate_path(const char *path, char *buf, size_t bufsize) {
    if (!path) return path;
    int n;
    
    if (strncmp(path, "/usr/bin/", 9) == 0) {
        n = snprintf(buf, bufsize, "%s/bin/%s", PREFIX, path + 9);
        if (n < 0 || (size_t)n >= bufsize) return path;
        return buf;
    }
    if (strncmp(path, "/bin/", 5) == 0) {
        n = snprintf(buf, bufsize, "%s/bin/%s", PREFIX, path + 5);
        if (n < 0 || (size_t)n >= bufsize) return path;
        return buf;
    }
    if (strncmp(path, "/usr/sbin/", 10) == 0) {
        n = snprintf(buf, bufsize, "%s/bin/%s", PREFIX, path + 10);
        if (n < 0 || (size_t)n >= bufsize) return path;
        return buf;
    }
    if (strncmp(path, "/sbin/", 6) == 0) {
        n = snprintf(buf, bufsize, "%s/bin/%s", PREFIX, path + 6);
        if (n < 0 || (size_t)n >= bufsize) return path;
        return buf;
    }
    return path;
}

__attribute__((constructor))
static void init_shim(void) {
    real_openat = dlsym(RTLD_NEXT, "openat");
    real_openat64 = dlsym(RTLD_NEXT, "openat64");
    real_execve = dlsym(RTLD_NEXT, "execve");
    
    if (!real_openat || !real_openat64 || !real_execve) {
        const char msg[] = "bun-shim: failed to resolve symbols\n";
        syscall(SYS_write, STDERR_FILENO, msg, sizeof(msg) - 1);
        _exit(1);
    }

    PREFIX = getenv("PREFIX");
    if (!PREFIX) PREFIX = PREFIX_DEFAULT;

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
    if (len == 8 && memcmp(pathname, "/storage", 8) == 0) return 1;
    if (len == 9 && memcmp(pathname, "/storage/", 9) == 0) return 1;
    if (len == 17 && memcmp(pathname, "/storage/emulated", 17) == 0) return 1;
    if (len == 18 && memcmp(pathname, "/storage/emulated/", 18) == 0) return 1;
    if (len == 19 && memcmp(pathname, "/storage/emulated/0", 19) == 0) return 1;
    if (len == 20 && memcmp(pathname, "/storage/emulated/0/", 20) == 0) return 1;
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

/*
 * Parse shebang and return translated interpreter path.
 * Returns: 0 on success (shebang found and parsed), -1 if not a shebang.
 */
static int parse_shebang(const char *path, char *interp, size_t interp_size,
                         char *interp_arg, size_t arg_size) {
    int fd = real_openat(AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return -1;
    
    char buf[SHEBANG_MAX];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    
    if (n < 2 || buf[0] != '#' || buf[1] != '!') return -1;
    
    char *start = buf + 2;
    while (start < buf + n && (*start == ' ' || *start == '\t')) start++;
    if (start >= buf + n) return -1;
    
    char *end = start;
    while (end < buf + n && *end != ' ' && *end != '\t' && *end != '\n' && *end != '\r') end++;
    
    size_t interp_len = end - start;
    if (interp_len == 0 || interp_len >= interp_size) return -1;
    
    memcpy(interp, start, interp_len);
    interp[interp_len] = '\0';
    
    interp_arg[0] = '\0';
    char *arg_start = end;
    while (arg_start < buf + n && (*arg_start == ' ' || *arg_start == '\t')) arg_start++;
    
    if (arg_start < buf + n && *arg_start != '\n' && *arg_start != '\r') {
        char *arg_end = arg_start;
        while (arg_end < buf + n && *arg_end != ' ' && *arg_end != '\t' && 
               *arg_end != '\n' && *arg_end != '\r') arg_end++;
        size_t arg_len = arg_end - arg_start;
        if (arg_len > 0 && arg_len < arg_size) {
            memcpy(interp_arg, arg_start, arg_len);
            interp_arg[arg_len] = '\0';
        }
    }
    
    return 0;
}

int execve(const char *pathname, char *const argv[], char *const envp[]) {
    static char interp_buf[256], arg_buf[256], translated_buf[256];
    
    if (parse_shebang(pathname, interp_buf, sizeof(interp_buf),
                      arg_buf, sizeof(arg_buf)) == 0) {
        const char *translated = translate_path(interp_buf, translated_buf, sizeof(translated_buf));
        
        int orig_argc = 0;
        while (argv[orig_argc]) orig_argc++;
        
        int has_arg = arg_buf[0] ? 1 : 0;
        int new_argc = 1 + has_arg + 1 + orig_argc;
        char **new_argv = malloc((new_argc + 1) * sizeof(char *));
        if (!new_argv) {
            errno = ENOMEM;
            return -1;
        }
        
        int i = 0;
        new_argv[i++] = (char *)translated;
        if (has_arg) new_argv[i++] = arg_buf;
        new_argv[i++] = (char *)(argv[0] ? argv[0] : pathname);
        for (int j = 1; j < orig_argc; j++) new_argv[i++] = argv[j];
        new_argv[i] = NULL;
        
        int ret = real_execve(translated, new_argv, envp);
        int saved_errno = errno;
        free(new_argv);
        errno = saved_errno;
        return ret;
    }
    
    return real_execve(pathname, argv, envp);
}
