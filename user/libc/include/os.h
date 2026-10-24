#ifndef OS_LIBC_H
#define OS_LIBC_H
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#define abi_stat stat
#define abi_dirent dirent
#define abi_timespec timespec
#define abi_termios termios
#define abi_winsize winsize
#define abi_utsname utsname
#define abi_iovec iovec
#include <abi.h>

typedef long ssize_t;
typedef long off_t;
typedef int pid_t;
typedef unsigned uid_t;
typedef unsigned gid_t;
typedef unsigned mode_t;
typedef unsigned long ino_t;
typedef unsigned long dev_t;
typedef unsigned long nlink_t;
typedef long time_t;
typedef long suseconds_t;
typedef long clock_t;
typedef uint64_t sigset_t;
typedef void (*sighandler_t)(int);
struct sigaction {
    sighandler_t sa_handler;
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    sigset_t sa_mask;
};
struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};
struct rusage {
    struct timeval ru_utime, ru_stime;
    long ru_maxrss, ru_ixrss, ru_idrss, ru_isrss, ru_minflt, ru_majflt, ru_nswap, ru_inblock,
        ru_oublock, ru_msgsnd, ru_msgrcv, ru_nsignals, ru_nvcsw, ru_nivcsw;
};
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define WIFCONTINUED(s) ((s) == 0xffff)
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define EOF (-1)
#define BUFSIZ 4096
#define FOPEN_MAX 128
#define FILENAME_MAX 4096
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define RAND_MAX 2147483647
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0
#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_IRWXU 0700
#define S_IRWXG 0070
#define S_IRWXO 0007
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000

extern int errno;
extern char **environ;
long syscall(long number, ...);
ssize_t read(int fd, void *buffer, size_t count);
ssize_t write(int fd, const void *buffer, size_t count);
int open(const char *path, int flags, ...);
int openat(int dirfd, const char *path, int flags, ...);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int stat(const char *path, struct stat *stat);
int lstat(const char *path, struct stat *stat);
int fstat(int fd, struct stat *stat);
ssize_t getdents64(int fd, void *buffer, size_t count);
int mkdir(const char *path, mode_t mode);
int rmdir(const char *path);
int link(const char *oldpath, const char *newpath);
int unlink(const char *path);
int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *path, char *buffer, size_t size);
int rename(const char *oldpath, const char *newpath);
int access(const char *path, int mode);
int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);
int chown(const char *path, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);
mode_t umask(mode_t mode);
int chdir(const char *path);
int fchdir(int fd);
char *getcwd(char *buffer, size_t size);
int dup(int fd);
int dup2(int oldfd, int newfd);
int dup3(int oldfd, int newfd, int flags);
int pipe(int descriptors[2]);
int pipe2(int descriptors[2], int flags);
int fcntl(int fd, int command, ...);
int ioctl(int fd, unsigned long request, ...);
int fsync(int fd);
int ftruncate(int fd, off_t length);
pid_t fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
int execv(const char *path, char *const argv[]);
int execvp(const char *file, char *const argv[]);
int execl(const char *path, const char *arg, ...);
int execlp(const char *path, const char *arg, ...);
int execle(const char *path, const char *arg, ...);
pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);
pid_t wait4(pid_t pid, int *status, int options, struct rusage *usage);
int kill(pid_t pid, int signal);
int raise(int signal);
sighandler_t signal(int signal, sighandler_t handler);
int sigaction(int signal, const struct sigaction *action, struct sigaction *old);
int sigprocmask(int how, const sigset_t *set, sigset_t *old);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signal);
int sigdelset(sigset_t *set, int signal);
int sigismember(const sigset_t *set, int signal);
int sigpending(sigset_t *set);
int pause(void);
pid_t getpid(void);
pid_t getppid(void);
pid_t getpgrp(void);
pid_t getpgid(pid_t pid);
pid_t getsid(pid_t pid);
pid_t setsid(void);
int setpgid(pid_t pid, pid_t group);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int seteuid(uid_t uid);
int setgid(gid_t gid);
int setegid(gid_t gid);
int setresuid(uid_t real, uid_t effective, uid_t saved);
int getresuid(uid_t *real, uid_t *effective, uid_t *saved);
int getgroups(int count, gid_t *groups);
int sched_yield(void);
int yield(void);
int brk(void *address);
void *sbrk(intptr_t increment);
void *mmap(void *address, size_t length, int protection, int flags, int fd, off_t offset);
int munmap(void *address, size_t length);
int mumap(void *address, size_t length);
int mprotect(void *address, size_t length, int protection);
int nanosleep(const struct timespec *request, struct timespec *remaining);
unsigned sleep(unsigned seconds);
int usleep(unsigned microseconds);
int clock_gettime(int clock, struct timespec *time);
time_t time(time_t *result);
int uname(struct utsname *names);
int isatty(int fd);
int tcgetattr(int fd, struct termios *attributes);
int tcsetattr(int fd, int action, const struct termios *attributes);
void cfmakeraw(struct termios *attributes);
pid_t tcgetpgrp(int fd);
int tcsetpgrp(int fd, pid_t group);
_Noreturn void _exit(int status);
_Noreturn void _Exit(int status);
_Noreturn void exit(int status);
_Noreturn void abort(void);
int atexit(void (*function)(void));
int system(const char *command);

void *malloc(size_t size);
void free(void *pointer);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void *reallocarray(void *pointer, size_t count, size_t size);
void *aligned_alloc(size_t alignment, size_t size);
int posix_memalign(void **pointer, size_t alignment, size_t size);
size_t malloc_usable_size(void *pointer);
void *memcpy(void *restrict destination, const void *restrict source, size_t size);
void *memmove(void *destination, const void *source, size_t size);
void *memset(void *destination, int byte, size_t size);
int memcmp(const void *a, const void *b, size_t size);
void *memchr(const void *data, int byte, size_t size);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maximum);
char *strcpy(char *restrict destination, const char *restrict source);
char *strncpy(char *restrict destination, const char *restrict source, size_t size);
char *stpcpy(char *restrict destination, const char *restrict source);
char *strcat(char *restrict destination, const char *restrict source);
char *strncat(char *restrict destination, const char *restrict source, size_t size);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t size);
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t size);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strpbrk(const char *s, const char *accept);
char *strdup(const char *s);
char *strndup(const char *s, size_t size);
char *strtok(char *restrict s, const char *restrict delimiters);
char *strtok_r(char *restrict s, const char *restrict delimiters, char **restrict state);
char *strsep(char **string, const char *delimiters);
char *strerror(int error);
void perror(const char *prefix);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);
double strtod(const char *s, char **end);
float strtof(const char *s, char **end);
long double strtold(const char *s, char **end);
int atoi(const char *s);
long atol(const char *s);
long long atoll(const char *s);
double atof(const char *s);
int abs(int n);
long labs(long n);
long long llabs(long long n);
typedef struct {
    int quot, rem;
} div_t;
typedef struct {
    long quot, rem;
} ldiv_t;
typedef struct {
    long long quot, rem;
} lldiv_t;
div_t div(int n, int d);
ldiv_t ldiv(long n, long d);
lldiv_t lldiv(long long n, long long d);
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *));
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int putenv(char *entry);
int clearenv(void);
int rand(void);
void srand(unsigned seed);

int isalpha(int c);
int isdigit(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isxdigit(int c);
int isprint(int c);
int isgraph(int c);
int ispunct(int c);
int iscntrl(int c);
int isblank(int c);
int tolower(int c);
int toupper(int c);

typedef struct FILE FILE;
extern FILE *stdin, *stdout, *stderr;
FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *stream);
int fclose(FILE *stream);
int fflush(FILE *stream);
int fileno(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int setvbuf(FILE *stream, char *buffer, int mode, size_t size);
void setbuf(FILE *stream, char *buffer);
size_t fread(void *restrict pointer, size_t size, size_t count, FILE *restrict stream);
size_t fwrite(const void *restrict pointer, size_t size, size_t count, FILE *restrict stream);
int fgetc(FILE *stream);
int getc(FILE *stream);
int getchar(void);
int fputc(int c, FILE *stream);
int putc(int c, FILE *stream);
int putchar(int c);
int ungetc(int c, FILE *stream);
char *fgets(char *restrict buffer, int size, FILE *restrict stream);
int fputs(const char *restrict text, FILE *restrict stream);
int puts(const char *text);
ssize_t getline(char **restrict line, size_t *restrict capacity, FILE *restrict stream);
ssize_t getdelim(char **restrict line, size_t *restrict capacity, int delimiter,
                 FILE *restrict stream);
int fseek(FILE *stream, long offset, int whence);
int fseeko(FILE *stream, off_t offset, int whence);
long ftell(FILE *stream);
off_t ftello(FILE *stream);
void rewind(FILE *stream);
typedef off_t fpos_t;
int fgetpos(FILE *stream, fpos_t *position);
int fsetpos(FILE *stream, const fpos_t *position);
int vfprintf(FILE *stream, const char *format, va_list args);
int fprintf(FILE *stream, const char *format, ...) __attribute__((format(printf, 2, 3)));
int printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int vprintf(const char *format, va_list args);
int snprintf(char *buffer, size_t size, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
int sprintf(char *buffer, const char *format, ...) __attribute__((format(printf, 2, 3)));
int vsprintf(char *buffer, const char *format, va_list args);
int asprintf(char **buffer, const char *format, ...);
int vasprintf(char **buffer, const char *format, va_list args);
int sscanf(const char *text, const char *format, ...);
int vsscanf(const char *text, const char *format, va_list args);
int scanf(const char *format, ...);
int fscanf(FILE *stream, const char *format, ...);
int vfscanf(FILE *stream, const char *format, va_list args);
int remove(const char *path);
FILE *tmpfile(void);
int mkstemp(char *template);

typedef struct DIR DIR;
DIR *opendir(const char *path);
DIR *fdopendir(int fd);
struct dirent *readdir(DIR *directory);
int closedir(DIR *directory);
void rewinddir(DIR *directory);
int dirfd(DIR *directory);
long telldir(DIR *directory);
void seekdir(DIR *directory, long offset);
char *realpath(const char *path, char *resolved);
char *basename(char *path);
char *dirname(char *path);
#endif
