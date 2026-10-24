#include <os.h>
int errno;
char **environ;
static long raw(long number, long a, long b, long c, long d, long e, long f) {
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    long result;
    __asm__ volatile("syscall"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return result;
}
static long checked(long result) {
    if ((unsigned long)result >= (unsigned long)-4095) {
        errno = -result;
        return -1;
    }
    return result;
}
#define C0(n) checked(raw(SYS_##n, 0, 0, 0, 0, 0, 0))
#define C1(n, a) checked(raw(SYS_##n, (long)(a), 0, 0, 0, 0, 0))
#define C2(n, a, b) checked(raw(SYS_##n, (long)(a), (long)(b), 0, 0, 0, 0))
#define C3(n, a, b, c) checked(raw(SYS_##n, (long)(a), (long)(b), (long)(c), 0, 0, 0))
#define C4(n, a, b, c, d) checked(raw(SYS_##n, (long)(a), (long)(b), (long)(c), (long)(d), 0, 0))

ssize_t read(int f, void *b, size_t n) {
    return C3(read, f, b, n);
}
ssize_t write(int f, const void *b, size_t n) {
    return C3(write, f, b, n);
}
int open(const char *p, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list a;
        va_start(a, flags);
        mode = va_arg(a, unsigned);
        va_end(a);
    }
    return C3(open, p, flags, mode);
}
int openat(int fd, const char *p, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list a;
        va_start(a, flags);
        mode = va_arg(a, unsigned);
        va_end(a);
    }
    return C4(openat, fd, p, flags, mode);
}
int close(int f) {
    return C1(close, f);
}
off_t lseek(int f, off_t o, int w) {
    return C3(lseek, f, o, w);
}
int stat(const char *p, struct stat *s) {
    return C2(stat, p, s);
}
int lstat(const char *p, struct stat *s) {
    return C2(lstat, p, s);
}
int fstat(int fd, struct stat *s) {
    return C2(fstat, fd, s);
}
ssize_t getdents64(int f, void *b, size_t n) {
    return C3(getdents64, f, b, n);
}
int mkdir(const char *p, mode_t m) {
    return C2(mkdir, p, m);
}
int rmdir(const char *p) {
    return C1(rmdir, p);
}
int unlink(const char *p) {
    return C1(unlink, p);
}
int link(const char *a, const char *b) {
    return C2(link, a, b);
}
int symlink(const char *a, const char *b) {
    return C2(symlink, a, b);
}
ssize_t readlink(const char *p, char *b, size_t n) {
    return C3(readlink, p, b, n);
}
int rename(const char *a, const char *b) {
    return C2(rename, a, b);
}
int access(const char *p, int m) {
    return C2(access, p, m);
}
int chmod(const char *p, mode_t m) {
    return C2(chmod, p, m);
}
int fchmod(int f, mode_t m) {
    return C2(fchmod, f, m);
}
int chown(const char *p, uid_t u, gid_t g) {
    return C3(chown, p, u, g);
}
int fchown(int f, uid_t u, gid_t g) {
    return C3(fchown, f, u, g);
}
mode_t umask(mode_t m) {
    return C1(umask, m);
}
int chdir(const char *p) {
    return C1(chdir, p);
}
int fchdir(int f) {
    return C1(fchdir, f);
}
char *getcwd(char *b, size_t n) {
    bool allocated = false;
    if (!b) {
        if (!n)
            n = 4096;
        b = malloc(n);
        if (!b)
            return NULL;
        allocated = true;
    }
    long e = C2(getcwd, b, n);
    if (e < 0) {
        if (allocated)
            free(b);
        return NULL;
    }
    return b;
}
int dup(int f) {
    return C1(dup, f);
}
int dup2(int a, int b) {
    return C2(dup2, a, b);
}
int dup3(int a, int b, int flags) {
    return C3(dup3, a, b, flags);
}
int pipe(int f[2]) {
    return C1(pipe, f);
}
int pipe2(int f[2], int flags) {
    return C2(pipe2, f, flags);
}
int fcntl(int f, int c, ...) {
    long arg = 0;
    if (c != F_GETFD && c != F_GETFL) {
        va_list a;
        va_start(a, c);
        arg = va_arg(a, int);
        va_end(a);
    }
    return C3(fcntl, f, c, arg);
}
int ioctl(int f, unsigned long r, ...) {
    va_list a;
    va_start(a, r);
    void *p = va_arg(a, void *);
    va_end(a);
    return C3(ioctl, f, r, p);
}
int fsync(int f) {
    return C1(fsync, f);
}
int ftruncate(int f, off_t n) {
    return C2(ftruncate, f, n);
}
pid_t fork(void) {
    return C0(fork);
}
int execve(const char *p, char *const a[], char *const e[]) {
    return C3(execve, p, a, e);
}
int execv(const char *p, char *const a[]) {
    return execve(p, a, environ);
}
int execvp(const char *file, char *const argv[]) {
    if (strchr(file, '/'))
        return execv(file, argv);
    const char *path = getenv("PATH");
    if (!path)
        path = "/bin:/sbin";
    int saved = ENOENT;
    while (1) {
        const char *end = strchr(path, ':');
        size_t n = end ? (size_t)(end - path) : strlen(path), len = strlen(file);
        if (n + len + 2 < 4096) {
            char name[4096];
            if (n) {
                memcpy(name, path, n);
                name[n++] = '/';
            }
            memcpy(name + n, file, len + 1);
            execv(name, argv);
            if (errno != ENOENT && errno != ENOTDIR)
                saved = errno;
        }
        if (!end)
            break;
        path = end + 1;
    }
    errno = saved;
    return -1;
}
static int exec_variadic(const char *path, const char *first, va_list ap, bool search,
                         bool environment) {
    char *args[128];
    size_t n = 0;
    const char *arg = first;
    while (arg && n < 127) {
        args[n++] = (char *)arg;
        arg = va_arg(ap, const char *);
    }
    if (arg) {
        errno = E2BIG;
        return -1;
    }
    args[n] = NULL;
    char **env = environment ? va_arg(ap, char **) : environ;
    return search ? execvp(path, args) : execve(path, args, env);
}
int execl(const char *p, const char *a, ...) {
    va_list v;
    va_start(v, a);
    int r = exec_variadic(p, a, v, false, false);
    va_end(v);
    return r;
}
int execlp(const char *p, const char *a, ...) {
    va_list v;
    va_start(v, a);
    int r = exec_variadic(p, a, v, true, false);
    va_end(v);
    return r;
}
int execle(const char *p, const char *a, ...) {
    va_list v;
    va_start(v, a);
    int r = exec_variadic(p, a, v, false, true);
    va_end(v);
    return r;
}
pid_t wait4(pid_t p, int *s, int o, struct rusage *r) {
    return C4(wait4, p, s, o, r);
}
pid_t waitpid(pid_t p, int *s, int o) {
    return wait4(p, s, o, NULL);
}
pid_t wait(int *s) {
    return waitpid(-1, s, 0);
}
int kill(pid_t p, int s) {
    return C2(kill, p, s);
}
int raise(int s) {
    return kill(getpid(), s);
}
extern void __signal_restorer(void);
int sigaction(int s, const struct sigaction *a, struct sigaction *o) {
    struct sigaction action;
    if (a) {
        action = *a;
        action.sa_flags |= SA_RESTORER;
        action.sa_restorer = __signal_restorer;
        a = &action;
    }
    return C4(rt_sigaction, s, a, o, 8);
}
sighandler_t signal(int s, sighandler_t h) {
    struct sigaction a = {.sa_handler = h, .sa_flags = SA_RESTART}, o;
    if (sigaction(s, &a, &o) < 0)
        return SIG_ERR;
    return o.sa_handler;
}
int sigprocmask(int h, const sigset_t *s, sigset_t *o) {
    return C4(rt_sigprocmask, h, s, o, 8);
}
int sigemptyset(sigset_t *s) {
    *s = 0;
    return 0;
}
int sigfillset(sigset_t *s) {
    *s = ~0ull;
    return 0;
}
int sigaddset(sigset_t *s, int n) {
    if (n < 1 || n > 64) {
        errno = EINVAL;
        return -1;
    }
    *s |= 1ull << (n - 1);
    return 0;
}
int sigdelset(sigset_t *s, int n) {
    if (n < 1 || n > 64) {
        errno = EINVAL;
        return -1;
    }
    *s &= ~(1ull << (n - 1));
    return 0;
}
int sigismember(const sigset_t *s, int n) {
    if (n < 1 || n > 64) {
        errno = EINVAL;
        return -1;
    }
    return (*s >> (n - 1)) & 1;
}
int sigpending(sigset_t *s) {
    return C2(sigpending, s, 8);
}
pid_t getpid(void) {
    return C0(getpid);
}
pid_t getppid(void) {
    return C0(getppid);
}
pid_t getpgrp(void) {
    return C0(getpgrp);
}
pid_t getpgid(pid_t p) {
    return C1(getpgid, p);
}
pid_t getsid(pid_t p) {
    return C1(getsid, p);
}
pid_t setsid(void) {
    return C0(setsid);
}
int setpgid(pid_t p, pid_t g) {
    return C2(setpgid, p, g);
}
uid_t getuid(void) {
    return C0(getuid);
}
uid_t geteuid(void) {
    return C0(geteuid);
}
gid_t getgid(void) {
    return C0(getgid);
}
gid_t getegid(void) {
    return C0(getegid);
}
int setuid(uid_t u) {
    return C1(setuid, u);
}
int seteuid(uid_t u) {
    if (u == (uid_t)-1) {
        errno = EINVAL;
        return -1;
    }
    return setresuid(-1, u, -1);
}
int setgid(gid_t g) {
    return C1(setgid, g);
}
int setegid(gid_t g) {
    if (g == (gid_t)-1) {
        errno = EINVAL;
        return -1;
    }
    return C3(setresgid, -1, g, -1);
}
int setresuid(uid_t r, uid_t e, uid_t s) {
    return C3(setresuid, r, e, s);
}
int getresuid(uid_t *r, uid_t *e, uid_t *s) {
    return C3(getresuid, r, e, s);
}
int getgroups(int n, gid_t *g) {
    return C2(getgroups, n, g);
}
int sched_yield(void) {
    return C0(yield);
}
int yield(void) {
    return sched_yield();
}
int brk(void *a) {
    long result = raw(SYS_brk, (long)a, 0, 0, 0, 0, 0);
    if ((uint64_t)result != (uintptr_t)a) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}
void *sbrk(intptr_t n) {
    uintptr_t old = raw(SYS_brk, 0, 0, 0, 0, 0, 0);
    if ((n > 0 && (uintptr_t)n > UINTPTR_MAX - old) || (n < 0 && (0ull - (uintptr_t)n) > old)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    if (n && brk((void *)(old + n)) < 0)
        return (void *)-1;
    return (void *)old;
}
void *mmap(void *a, size_t n, int p, int f, int d, off_t o) {
    return (void *)checked(raw(SYS_mmap, (long)a, n, p, f, d, o));
}
int munmap(void *a, size_t n) {
    return C2(munmap, a, n);
}
int mumap(void *a, size_t n) {
    return munmap(a, n);
}
int mprotect(void *a, size_t n, int p) {
    return C3(mprotect, a, n, p);
}
int nanosleep(const struct timespec *r, struct timespec *m) {
    return C2(nanosleep, r, m);
}
unsigned sleep(unsigned n) {
    struct timespec r = {n, 0}, m = {0};
    if (nanosleep(&r, &m) < 0)
        return m.tv_sec + (m.tv_nsec != 0);
    return 0;
}
int usleep(unsigned n) {
    struct timespec r = {n / 1000000, (n % 1000000) * 1000};
    return nanosleep(&r, NULL);
}
int clock_gettime(int c, struct timespec *t) {
    return C2(clock_gettime, c, t);
}
time_t time(time_t *p) {
    struct timespec t;
    if (clock_gettime(0, &t) < 0)
        return -1;
    if (p)
        *p = t.tv_sec;
    return t.tv_sec;
}
int uname(struct utsname *u) {
    return C1(uname, u);
}
int isatty(int f) {
    struct termios t;
    return tcgetattr(f, &t) == 0;
}
int tcgetattr(int f, struct termios *a) {
    return ioctl(f, TCGETS, a);
}
int tcsetattr(int f, int action, const struct termios *a) {
    if (action < 0 || action > 2) {
        errno = EINVAL;
        return -1;
    }
    return ioctl(f, action == TCSANOW ? TCSETS : action == TCSADRAIN ? TCSETSW : TCSETSF, a);
}
void cfmakeraw(struct termios *t) {
    t->c_iflag = 0;
    t->c_oflag = 0;
    t->c_lflag = 0;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}
pid_t tcgetpgrp(int f) {
    int p;
    if (ioctl(f, TIOCGPGRP, &p) < 0)
        return -1;
    return p;
}
int tcsetpgrp(int f, pid_t p) {
    return ioctl(f, TIOCSPGRP, &p);
}
_Noreturn void _exit(int s) {
    raw(SYS_exit, s, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}
_Noreturn void _Exit(int s) {
    _exit(s);
}
