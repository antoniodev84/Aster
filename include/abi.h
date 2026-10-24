#ifndef OS_ABI_H
#define OS_ABI_H

#include <stdint.h>
#include <stddef.h>

#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_close 3
#define SYS_stat 4
#define SYS_fstat 5
#define SYS_lstat 6
#define SYS_poll 7
#define SYS_lseek 8
#define SYS_mmap 9
#define SYS_mprotect 10
#define SYS_munmap 11
#define SYS_brk 12
#define SYS_rt_sigaction 13
#define SYS_rt_sigprocmask 14
#define SYS_rt_sigreturn 15
#define SYS_ioctl 16
#define SYS_readv 19
#define SYS_writev 20
#define SYS_access 21
#define SYS_pipe 22
#define SYS_yield 24
#define SYS_dup 32
#define SYS_dup2 33
#define SYS_nanosleep 35
#define SYS_getpid 39
#define SYS_fork 57
#define SYS_execve 59
#define SYS_exit 60
#define SYS_wait4 61
#define SYS_kill 62
#define SYS_uname 63
#define SYS_fcntl 72
#define SYS_fsync 74
#define SYS_ftruncate 77
#define SYS_getcwd 79
#define SYS_chdir 80
#define SYS_fchdir 81
#define SYS_rename 82
#define SYS_mkdir 83
#define SYS_rmdir 84
#define SYS_link 86
#define SYS_unlink 87
#define SYS_symlink 88
#define SYS_readlink 89
#define SYS_chmod 90
#define SYS_fchmod 91
#define SYS_chown 92
#define SYS_fchown 93
#define SYS_umask 95
#define SYS_getuid 102
#define SYS_getgid 104
#define SYS_setuid 105
#define SYS_setgid 106
#define SYS_geteuid 107
#define SYS_getegid 108
#define SYS_setpgid 109
#define SYS_getppid 110
#define SYS_getpgrp 111
#define SYS_setsid 112
#define SYS_setreuid 113
#define SYS_setregid 114
#define SYS_getgroups 115
#define SYS_setgroups 116
#define SYS_setresuid 117
#define SYS_getresuid 118
#define SYS_setresgid 119
#define SYS_getresgid 120
#define SYS_getpgid 121
#define SYS_getsid 124
#define SYS_sigpending 127
#define SYS_sigaltstack 131
#define SYS_arch_prctl 158
#define SYS_mount 165
#define SYS_umount2 166
#define SYS_gettid 186
#define SYS_getdents64 217
#define SYS_set_tid_address 218
#define SYS_clock_gettime 228
#define SYS_exit_group 231
#define SYS_openat 257
#define SYS_pipe2 293
#define SYS_dup3 292
#define SYS_getrandom 318

#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define EXDEV 18
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define EPIPE 32
#define ERANGE 34
#define ENAMETOOLONG 36
#define ENOSYS 38
#define ENOTEMPTY 39
#define ELOOP 40
#define EOVERFLOW 75
#define ENOTSUP 95

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_EXCL 0200
#define O_NOCTTY 0400
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_DIRECTORY 0200000
#define O_NOFOLLOW 0400000
#define O_CLOEXEC 02000000
#define AT_FDCWD (-100)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1
#define F_DUPFD_CLOEXEC 1030

#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_IFMT 0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFLNK 0120000
#define S_IFCHR 0020000
#define S_IFBLK 0060000
#define S_IFIFO 0010000
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FIXED 16
#define MAP_ANONYMOUS 32
#define MAP_FAILED ((void *)-1)

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGWINCH 28
#define SIGURG 23
#define NSIG 65
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define SA_NOCLDSTOP 1
#define SA_RESTART 0x10000000u
#define SA_NODEFER 0x40000000u
#define SA_RESETHAND 0x80000000u
#define SA_RESTORER 0x04000000u
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int)) - 1)
#define WNOHANG 1
#define WUNTRACED 2
#define WCONTINUED 8
#define WIFEXITED(s) (((s) & 127) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 255)
#define WIFSIGNALED(s) (((s) & 127) != 0 && ((s) & 127) != 127)
#define WTERMSIG(s) ((s) & 127)
#define WIFSTOPPED(s) (((s) & 255) == 127)
#define WSTOPSIG(s) (((s) >> 8) & 255)

#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCSETSW 0x5403
#define TCSETSF 0x5404
#define TIOCGPGRP 0x540f
#define TIOCSPGRP 0x5410
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define FIONREAD 0x541b
#define IGNBRK 1
#define ICRNL 0400
#define IXON 02000
#define OPOST 1
#define ONLCR 4
#define ISIG 1
#define ICANON 2
#define ECHO 010
#define ECHOE 020
#define ECHOK 040
#define IEXTEN 0100000
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSUSP 10
#define NCCS 32

struct abi_stat {
    uint64_t st_dev, st_ino, st_nlink;
    uint32_t st_mode, st_uid, st_gid, pad;
    uint64_t st_rdev;
    int64_t st_size, st_blksize, st_blocks;
    int64_t st_atime, st_atime_nsec, st_mtime, st_mtime_nsec, st_ctime, st_ctime_nsec;
    int64_t reserved[3];
};

struct abi_dirent {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[];
};

struct abi_sigaction {
    uint64_t handler, flags, restorer, mask;
};
struct abi_rusage {
    int64_t user_sec, user_usec, system_sec, system_usec, maxrss, ixrss, idrss, isrss, minflt,
        majflt, nswap, inblock, oublock, msgsnd, msgrcv, nsignals, nvcsw, nivcsw;
};
struct abi_timespec {
    int64_t tv_sec, tv_nsec;
};
struct abi_termios {
    uint32_t c_iflag, c_oflag, c_cflag, c_lflag;
    uint8_t c_line, c_cc[NCCS];
};
struct abi_winsize {
    uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel;
};
struct abi_iovec {
    uint64_t base, length;
};
struct abi_utsname {
    char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65];
};

#endif
