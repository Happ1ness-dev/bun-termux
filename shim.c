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
#include <time.h>
#include <sys/mman.h>
#include <errno.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define PREFIX_DEFAULT "/data/data/com.termux/files/usr"
#define SHEBANG_MAX 256

static const char *SAFE_DIR = NULL;
static int safe_dir_fd = -1;
static const char *PREFIX = NULL;
static const char *TMPDIR = NULL;
static const char *WRAPPER_PATH = NULL;
static const char *TARGET_PATH = NULL;

static int (*real_openat)(int, const char *, int, ...) = NULL;
static int (*real_openat64)(int, const char *, int, ...) = NULL;
static int (*real_execve)(const char *, char *const[], char *const[]) = NULL;
static int (*real_linkat)(int, const char *, int, const char *, int) = NULL;
static int (*real_mkdir)(const char *, mode_t) = NULL;
static int (*real_symlink)(const char *, const char *) = NULL;

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

/* Translate /tmp paths to use $TMPDIR instead.
 * path + 4 skips "/tmp" prefix to append remainder (e.g., "/tmp/foo" -> TMPDIR + "/foo").
 */
static const char *translate_tmp(const char *path, char *buf, size_t len) {
    if (!path || !TMPDIR) return path;
    if (strcmp(path, "/tmp") == 0 || strncmp(path, "/tmp/", 5) == 0) {
        int n = snprintf(buf, len, "%s%s", TMPDIR, path + 4);
        if (n < 0 || (size_t)n >= len) return path;
        return buf;
    }
    return path;
}

__attribute__((constructor))
static void init_shim(void) {
    const char *orig;

    real_openat = dlsym(RTLD_NEXT, "openat");
    real_openat64 = dlsym(RTLD_NEXT, "openat64");
    real_execve = dlsym(RTLD_NEXT, "execve");
    real_linkat = dlsym(RTLD_NEXT, "linkat");
    real_mkdir = dlsym(RTLD_NEXT, "mkdir");
    real_symlink = dlsym(RTLD_NEXT, "symlink");

    if (!real_openat || !real_openat64 || !real_execve || !real_linkat ||
        !real_mkdir || !real_symlink) {
        const char msg[] = "bun-shim: failed to resolve symbols\n";
        syscall(SYS_write, STDERR_FILENO, msg, sizeof(msg) - 1);
        _exit(1);
    }

    PREFIX = getenv("PREFIX");
    if (!PREFIX) PREFIX = PREFIX_DEFAULT;

    TMPDIR = getenv("TMPDIR");
    if (!TMPDIR) TMPDIR = "/data/data/com.termux/files/usr/tmp";

    WRAPPER_PATH = getenv("BUN_TERMUX_WRAPPER");
    TARGET_PATH = getenv("BUN_TERMUX_TARGET");

    SAFE_DIR = getenv("BUN_FAKE_ROOT");
    if (!SAFE_DIR) SAFE_DIR = getenv("TMPDIR");
    if (!SAFE_DIR) SAFE_DIR = "/data/data/com.termux/files/usr/tmp";
    safe_dir_fd = real_openat(AT_FDCWD, SAFE_DIR,
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);

    /* Restore original LD_* variables that were filtered during userland exec */
    if ((orig = getenv("BUN_TERMUX_ORIG_LD_PRELOAD"))) {
        setenv("LD_PRELOAD", orig, 1);
        unsetenv("BUN_TERMUX_ORIG_LD_PRELOAD");
    }
    if ((orig = getenv("BUN_TERMUX_ORIG_LD_LIBRARY_PATH"))) {
        setenv("LD_LIBRARY_PATH", orig, 1);
        unsetenv("BUN_TERMUX_ORIG_LD_LIBRARY_PATH");
    }
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

static int generate_proc_stat(char *buf, size_t size) {
    int ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) ncpu = 1;
    if (ncpu > 256) ncpu = 256;
    
    int total = 0;
    int n;
    
    n = snprintf(buf + total, size - total, "cpu  0 0 0 0 0 0 0 0 0 0\n");
    if (n < 0) return n;
    total += n;
    
    for (int i = 0; i < ncpu && total < (int)size - 32; i++) {
        n = snprintf(buf + total, size - total, "cpu%d 0 0 0 0 0 0 0 0 0 0\n", i);
        if (n < 0) return n;
        total += n;
    }
    
    n = snprintf(buf + total, size - total, "intr 0\nctxt 0\nbtime %ld\nprocesses 1\nprocs_running 1\nprocs_blocked 0\n", time(NULL));
    if (n < 0) return n;
    total += n;
    
    return total;
}

static int do_openat(int (*real_fn)(int, const char *, int, ...),
                     int dirfd, const char *pathname, int flags, va_list ap) {
    if (pathname && strcmp(pathname, "/proc/stat") == 0) {
        int fd = memfd_create("proc_stat", MFD_CLOEXEC);
        if (fd >= 0) {
            char buf[2048];
            int n = generate_proc_stat(buf, sizeof(buf));
            if (n > 0) {
                ssize_t written = write(fd, buf, n);
                if (written == n && lseek(fd, 0, SEEK_SET) == 0) {
                    return fd;
                }
            }
            close(fd);
        }
    }

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

/* Replace first occurrence of 'search' with 'replace' in 'path_var'.
 * Used specifically for PATH variable rewriting.
 * Caller must free the returned string.
 */
static char *replace_in_path(const char *path_var, const char *search, const char *replace) {
    const char *found = strstr(path_var, search);
    if (!found) return NULL;
    
    size_t prefix_len = found - path_var;
    size_t search_len = strlen(search);
    size_t replace_len = strlen(replace);
    size_t suffix_len = strlen(found + search_len);
    
    char *result = malloc(prefix_len + replace_len + suffix_len + 1);
    if (!result) return NULL;
    
    memcpy(result, path_var, prefix_len);
    memcpy(result + prefix_len, replace, replace_len);
    memcpy(result + prefix_len + replace_len, found + search_len, suffix_len + 1);
    
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
    char interp_buf[256], arg_buf[256], translated_buf[256];
    char **new_envp = NULL, **new_argv = NULL;
    char *new_path = NULL;
    int ret = -1;
    
    /* Rewrite PATH if it contains /tmp/bun-node */
    for (int i = 0; envp && envp[i]; i++) {
        if (strncmp(envp[i], "PATH=", 5) == 0) {
            char replacement[PATH_MAX];
            int n = snprintf(replacement, sizeof(replacement), "%s/bun-node", TMPDIR);
            if (n < 0 || (size_t)n >= sizeof(replacement))
                break; /* TMPDIR too long - skip PATH rewrite */
            
            new_path = replace_in_path(envp[i], "/tmp/bun-node", replacement);
            if (new_path) {
                int env_count = 0;
                while (envp[env_count]) env_count++;
                
                new_envp = malloc((env_count + 1) * sizeof(char *));
                if (new_envp) {
                    for (int j = 0; j < env_count; j++)
                        new_envp[j] = (j == i) ? new_path : (char *)envp[j];
                    new_envp[env_count] = NULL;
                    envp = (char *const *)new_envp;
                }
            }
            break;
        }
    }
    
    if (parse_shebang(pathname, interp_buf, sizeof(interp_buf),
                      arg_buf, sizeof(arg_buf)) == 0) {
        const char *translated = translate_path(interp_buf, translated_buf, sizeof(translated_buf));
        
        int orig_argc = 0;
        while (argv[orig_argc]) orig_argc++;
        
        int has_arg = arg_buf[0] ? 1 : 0;
        int new_argc = 1 + has_arg + 1 + orig_argc;
        new_argv = malloc((new_argc + 1) * sizeof(char *));
        if (!new_argv) {
            errno = ENOMEM;
            goto cleanup;
        }
        
        int i = 0;
        new_argv[i++] = (char *)translated;
        if (has_arg) new_argv[i++] = arg_buf;
        new_argv[i++] = (char *)(argv[0] ? argv[0] : pathname);
        for (int j = 1; j < orig_argc; j++) new_argv[i++] = argv[j];
        new_argv[i] = NULL;
        
        ret = real_execve(translated, new_argv, envp);
    } else {
        ret = real_execve(pathname, argv, envp);
    }
    
cleanup:
    free(new_argv);
    free(new_path);
    free(new_envp);
    return ret;
}

/*
 * Intercept linkat() and return EXDEV (error.NotSameFileSystem) to force bun to fallback to copyfile.
 * 
 * See: bun-bun-v1.3.10/src/install/PackageInstall.zig
 */
int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags) {
    (void)olddirfd;
    (void)oldpath;
    (void)newdirfd;
    (void)newpath;
    (void)flags;

    errno = EXDEV;
    return -1;
}

int mkdir(const char *pathname, mode_t mode) {
    char buf[PATH_MAX];
    pathname = translate_tmp(pathname, buf, sizeof(buf));
    return real_mkdir(pathname, mode);
}

int symlink(const char *target, const char *linkpath) {
    char tbuf[PATH_MAX], lbuf[PATH_MAX];
    
    target = translate_tmp(target, tbuf, sizeof(tbuf));
    linkpath = translate_tmp(linkpath, lbuf, sizeof(lbuf));
    
    if (TARGET_PATH && WRAPPER_PATH && strcmp(target, TARGET_PATH) == 0) {
        target = WRAPPER_PATH;
    }
    
    return real_symlink(target, linkpath);
}
