/*
 * Bun Wrapper for Termux - with userland exec
 * 
 * Uses userland exec to load ld.so and run bun with the shim preloaded.
 */

#define _GNU_SOURCE
#include <elf.h>

#ifndef SYS_getrandom
#define SYS_getrandom 278
#endif
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>  /* syscall() provided by glibc with _GNU_SOURCE */
#include <limits.h>
#include <errno.h>

#define LD_SO     "/data/data/com.termux/files/usr/glibc/lib/ld-linux-aarch64.so.1"
#define GLIBC_LIB "/data/data/com.termux/files/usr/glibc/lib"
#define MAX_ARGS  256
#define MAX_ENV   512
#define RESERVED_ENV_SLOTS 3  /* Space for orig_preload, orig_libpath, fake_root */
#define STACK_AUXV_RESERVE 256

static void die(const char *msg) {
    fprintf(stderr, "bun-termux: %s: %s\n", msg, strerror(errno));
    _exit(1);
}

static inline void path_build(char *buf, size_t bufsize, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, bufsize, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= bufsize) die("path too long");
}

static char *get_self_dir(char *buf, size_t bufsize) {
    ssize_t n = readlink("/proc/self/exe", buf, bufsize - 1);
    if (n < 0) return NULL;
    buf[n] = '\0';
    char *last_slash = strrchr(buf, '/');
    if (last_slash) *last_slash = '\0';
    return buf;
}

static const char *resolve_glibc_path(char *buf, size_t bufsize,
                                      const char *env_var, const char *prefix,
                                      const char *prefix_suffix,
                                      const char *default_path) {
    if (env_var) return env_var;
    if (prefix) {
        int n = snprintf(buf, bufsize, "%s%s", prefix, prefix_suffix);
        if (n < 0 || (size_t)n >= bufsize) die("path too long");
        return buf;
    }
    return default_path;
}

#define MAX_ENV_VAL (128 * 1024)  /* Safe upper limit for env var values */

static char orig_preload[MAX_ENV_VAL];
static char orig_libpath[MAX_ENV_VAL];
static int has_orig_preload = 0;
static int has_orig_libpath = 0;

static size_t filter_envp(char **src, const char **dst, size_t max) {
    size_t n = 0;
    for (char **e = src; *e && n < max; e++) {
        if (strncmp(*e, "LD_PRELOAD=", 11) == 0) {
            int len = snprintf(orig_preload, sizeof(orig_preload),
                               "BUN_TERMUX_ORIG_LD_PRELOAD=%s", *e + 11);
            has_orig_preload = (len > 0 && (size_t)len < sizeof(orig_preload));
            if (len > 0 && !has_orig_preload)
                fprintf(stderr, "bun-termux: warning: LD_PRELOAD too long, discarded\n");
            continue;
        }
        if (strncmp(*e, "LD_LIBRARY_PATH=", 16) == 0) {
            int len = snprintf(orig_libpath, sizeof(orig_libpath),
                               "BUN_TERMUX_ORIG_LD_LIBRARY_PATH=%s", *e + 16);
            has_orig_libpath = (len > 0 && (size_t)len < sizeof(orig_libpath));
            if (len > 0 && !has_orig_libpath)
                fprintf(stderr, "bun-termux: warning: LD_LIBRARY_PATH too long, discarded\n");
            continue;
        }
        dst[n++] = *e;
    }
    return n;
}

static void ensure_dir(const char *path) {
    char tmp[PATH_MAX];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) die("path too long");
    /* strlen + memcpy is ~2.5x faster than strlcpy for typical paths */
    memcpy(tmp, path, len + 1);
    
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

typedef struct {
    size_t base_addr;
    size_t entry;
    size_t phdr_addr;
    uint16_t phnum;
    uint16_t phent;
    size_t pagesz;
} elf_info_t;

static size_t page_round_down(size_t v, size_t ps) {
    return v & ~(ps - 1);
}

static size_t page_round_up(size_t v, size_t ps) {
    return (v + ps - 1) & ~(ps - 1);
}

static void load_elf_segments(int fd, const Elf64_Ehdr *eh, uint8_t *base, 
                              size_t vmin, size_t ps) {
    for (int i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)(
            (uint8_t *)eh + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;

        size_t off_a = page_round_down(ph->p_offset, ps);
        size_t va_a = page_round_down(ph->p_vaddr, ps);
        size_t diff = ph->p_offset - off_a;
        size_t mapsz = page_round_up(ph->p_filesz + diff, ps);

        int prot = 0;
        if (ph->p_flags & PF_R) prot |= PROT_READ;
        if (ph->p_flags & PF_W) prot |= PROT_WRITE;
        if (ph->p_flags & PF_X) prot |= PROT_EXEC;

        void *seg = mmap(base + va_a - vmin, mapsz, prot | PROT_WRITE,
                        MAP_PRIVATE | MAP_FIXED, fd, off_a);
        if (seg == MAP_FAILED) die("segment map failed");

        if (ph->p_memsz > ph->p_filesz) {
            uint8_t *bss = base + (ph->p_vaddr - vmin) + ph->p_filesz;
            size_t bsz = ph->p_memsz - ph->p_filesz;
            size_t in_page = page_round_up((size_t)bss, ps) - (size_t)bss;
            if (in_page > bsz) in_page = bsz;
            memset(bss, 0, in_page);
            if (bsz > in_page) {
                void *a = mmap(bss + in_page, page_round_up(bsz - in_page, ps),
                              prot | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
                if (a == MAP_FAILED) die("BSS map failed");
            }
        }

        if (!(ph->p_flags & PF_W))
            mprotect(seg, mapsz, prot);
    }
}

static size_t find_phdr_addr(const Elf64_Ehdr *eh, size_t base_addr) {
    for (int i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)(
            (uint8_t *)eh + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type == PT_PHDR) 
            return base_addr + ph->p_vaddr;
    }
    /* Fallback: calculate from first PT_LOAD */
    for (int i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)(
            (uint8_t *)eh + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type == PT_LOAD) {
            return base_addr + ph->p_vaddr + eh->e_phoff;
        }
    }
    return 0;
}

static elf_info_t load_elf(int fd, size_t ps) {
    struct stat st;
    fstat(fd, &st);

    uint8_t *fdata = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (fdata == MAP_FAILED) die("mmap ld.so failed");

    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)fdata;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG)) die("ld.so not ELF");

    size_t vmin = (size_t)-1, vmax = 0;
    for (int i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)(
            fdata + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type == PT_LOAD) {
            if (ph->p_vaddr < vmin) vmin = ph->p_vaddr;
            size_t e = ph->p_vaddr + ph->p_memsz;
            if (e > vmax) vmax = e;
        }
    }
    vmin = page_round_down(vmin, ps);
    vmax = page_round_up(vmax, ps);

    uint8_t *base = mmap(NULL, vmax - vmin, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED) die("reserve failed");

    load_elf_segments(fd, eh, base, vmin, ps);

    size_t base_addr = (size_t)base - vmin;
    elf_info_t info = {
        .base_addr = base_addr,
        .entry = base_addr + eh->e_entry,
        .phdr_addr = find_phdr_addr(eh, base_addr),
        .phnum = eh->e_phnum,
        .phent = eh->e_phentsize,
        .pagesz = ps
    };

    munmap(fdata, st.st_size);
    close(fd);
    return info;
}

__attribute__((noreturn))
static void userland_exec(const char *ldso, const char **argv, size_t argc,
                          const char **envp, size_t envc) {
    int fd = open(ldso, O_RDONLY);
    if (fd < 0) die("open ld.so failed");

    size_t ps = sysconf(_SC_PAGESIZE);
    elf_info_t elf = load_elf(fd, ps);

    size_t stack_base;
    uint8_t *stk = mmap(NULL, 10 * 1024 * 1024, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANON | MAP_STACK, -1, 0);
    if (stk == MAP_FAILED) die("stack alloc failed");
    stack_base = (size_t)stk;
    uint8_t *sp = stk + 10 * 1024 * 1024;

    #define PUSH_STR(s) ({ \
        size_t _l = strlen(s) + 1; \
        if ((size_t)sp - _l < stack_base + STACK_AUXV_RESERVE) die("stack overflow"); \
        sp -= _l; memcpy(sp, s, _l); (size_t)sp; })
    #define PUSH_VAL(v) do { size_t _v = (v); sp -= 8; memcpy(sp, &_v, 8); } while(0)

    size_t plat_addr = PUSH_STR("aarch64");

    uint8_t rnd[16];
    ssize_t got = 0;
    while (got < 16) {
        ssize_t n = syscall(SYS_getrandom, rnd + got, 16 - got, 0);
        if (n < 0 && errno != EINTR) die("getrandom failed");
        if (n > 0) got += n;
    }
    sp -= 16; memcpy(sp, rnd, 16);
    size_t rnd_addr = (size_t)sp;

    size_t argv_a[MAX_ARGS];
    for (size_t i = 0; i < argc; i++)
        argv_a[i] = PUSH_STR(argv[i]);
    size_t execfn = argc ? argv_a[0] : 0;

    size_t envp_a[MAX_ENV];
    for (size_t i = 0; i < envc; i++)
        envp_a[i] = PUSH_STR(envp[i]);

    size_t auxv[][2] = {
        { AT_PHDR, elf.phdr_addr },
        { AT_PHENT, elf.phent },
        { AT_PHNUM, elf.phnum },
        { AT_PAGESZ, elf.pagesz },
        { AT_BASE, elf.base_addr },
        { AT_FLAGS, 0 },
        { AT_ENTRY, elf.entry },
        { AT_UID, getuid() },
        { AT_EUID, geteuid() },
        { AT_GID, getgid() },
        { AT_EGID, getegid() },
        { AT_HWCAP, getauxval(AT_HWCAP) },
        { AT_HWCAP2, getauxval(AT_HWCAP2) },
        { AT_CLKTCK, sysconf(_SC_CLK_TCK) },
        { AT_RANDOM, rnd_addr },
        { AT_SECURE, getauxval(AT_SECURE) },
        { AT_SYSINFO_EHDR, getauxval(AT_SYSINFO_EHDR) },
        { AT_EXECFN, execfn },
        { AT_PLATFORM, plat_addr },
        { AT_NULL, 0 },
    };
    size_t auxc = sizeof(auxv) / sizeof(auxv[0]);

    size_t nwords = 1 + (argc + 1) + (envc + 1) + auxc * 2;
    size_t data_sz = nwords * 8;
    sp = (uint8_t *)(((size_t)sp - data_sz) & ~(size_t)15);

    size_t *w = (size_t *)sp;
    *w++ = argc;
    for (size_t i = 0; i < argc; i++) *w++ = argv_a[i];
    *w++ = 0;
    for (size_t i = 0; i < envc; i++) *w++ = envp_a[i];
    *w++ = 0;
    for (size_t i = 0; i < auxc; i++) { *w++ = auxv[i][0]; *w++ = auxv[i][1]; }

    /* Block all signals before stack switch to prevent handlers running on new stack
       with old context. ld.so will set up its own signal handling. */
    sigset_t all;
    sigfillset(&all);
    sigprocmask(SIG_BLOCK, &all, NULL);

    __asm__ volatile(
        "mov sp, %[sp]\n"
        "mov x0, sp\n"
        "mov x1, xzr\n"
        "mov x2, xzr\n"
        "mov x3, xzr\n"
        "mov x4, xzr\n"
        "mov x5, xzr\n"
        "mov x30, xzr\n"
        "br  %[entry]\n"
        :
        : [sp] "r"((size_t)sp), [entry] "r"(elf.entry)
        : "x0","x1","x2","x3","x4","x5","x6","x7","x8","x9","x10",
          "x11","x12","x13","x14","x15","x16","x17","x30","memory"
    );
    __builtin_unreachable();
}

int main(int argc, char **argv, char **envp) {
    if (argc >= MAX_ARGS) 
        die("too many arguments");
    
    const char *bun_install = getenv("BUN_INSTALL");
    const char *bun_binary = getenv("BUN_BINARY_PATH");
    const char *prefix = getenv("PREFIX");
    static char ld_path[PATH_MAX], lib_path[PATH_MAX];
    
    const char *ld_so = resolve_glibc_path(ld_path, sizeof(ld_path),
                                           getenv("GLIBC_LD_SO"), prefix,
                                           "/glibc/lib/ld-linux-aarch64.so.1", LD_SO);
    
    const char *glibc_lib = resolve_glibc_path(lib_path, sizeof(lib_path),
                                               getenv("GLIBC_LIB"), prefix,
                                               "/glibc/lib", GLIBC_LIB);
    
    char self_dir[PATH_MAX];
    if (!get_self_dir(self_dir, sizeof(self_dir)))
        die("cannot find binary directory");
    
    char shim_path[PATH_MAX];
    char fake_root[PATH_MAX];
    char bun_path[PATH_MAX];
    struct stat st;

    if (bun_install) {
        path_build(shim_path, sizeof(shim_path), "%s/lib/bun-shim.so", bun_install);
        if (stat(shim_path, &st) == 0) {
            path_build(fake_root, sizeof(fake_root), "%s/tmp/fake-root", bun_install);
            goto have_shim;
        }
    }
    
    path_build(shim_path, sizeof(shim_path), "%s/bun-shim.so", self_dir);
    if (stat(shim_path, &st) == 0) {
        path_build(fake_root, sizeof(fake_root), "%s/tmp/fake-root", self_dir);
        goto have_shim;
    }
    
    path_build(shim_path, sizeof(shim_path), "%s/../lib/bun-shim.so", self_dir);
    if (stat(shim_path, &st) != 0)
        die("shim not found. Run 'make install' first.");
    path_build(fake_root, sizeof(fake_root), "%s/../tmp/fake-root", self_dir);

have_shim:
    if (bun_binary) {
        path_build(bun_path, sizeof(bun_path), "%s", bun_binary);
    } else if (bun_install) {
        path_build(bun_path, sizeof(bun_path), "%s/bin/buno", bun_install);
    } else {
        path_build(bun_path, sizeof(bun_path), "%s/buno", self_dir);
    }
    
    ensure_dir(fake_root);
    
    const char *new_argv[MAX_ARGS];
    size_t na = 0;
    
    new_argv[na++] = ld_so;
    new_argv[na++] = "--preload";
    new_argv[na++] = shim_path;
    new_argv[na++] = "--library-path";
    new_argv[na++] = glibc_lib;
    new_argv[na++] = bun_path;
    
    for (int i = 1; i < argc && na < MAX_ARGS - 1; i++) {
        new_argv[na++] = argv[i];
    }

    const char *new_envp[MAX_ENV];
    size_t ne = filter_envp(envp, new_envp, MAX_ENV - RESERVED_ENV_SLOTS);

    if (has_orig_preload)
        new_envp[ne++] = orig_preload;
    if (has_orig_libpath)
        new_envp[ne++] = orig_libpath;

    static char fake_root_env[PATH_MAX + 20];
    if (!getenv("BUN_FAKE_ROOT")) {
        path_build(fake_root_env, sizeof(fake_root_env), "BUN_FAKE_ROOT=%s", fake_root);
        new_envp[ne++] = fake_root_env;
    }

    userland_exec(ld_so, new_argv, na, new_envp, ne);
}
