#ifndef PROCESS_H
#define PROCESS_H

#include "arch/x86_64/internal.h"
#include "mm/mm.h"
#include "fs/vfs.h"

#define PROCESS_RUNNABLE 0
#define PROCESS_BLOCKED 1
#define PROCESS_ZOMBIE 2
#define PROCESS_STOPPED 3
#define BLOCK_IO 1
#define BLOCK_WAIT 2
#define BLOCK_SLEEP 3
#define BLOCK_NET 4
#define SYSCALL_BLOCK (-4096l)

struct process {
    uint64_t net_deadline, net_operation;
    int pid, ppid, pgrp, sid, state, wait_status, block_reason;
    uint32_t uid, euid, suid, gid, egid, sgid, umask;
    char name[64], executable[PATH_MAX];
    struct address_space *space;
    uint64_t stack_physical, stack_top, fs_base, brk_start, brk_end, mmap_cursor;
    struct x64_frame *frame;
    uint8_t fx[512] __attribute__((aligned(16)));
    struct descriptor fds[FD_MAX];
    struct vnode *cwd;
    struct abi_sigaction actions[NSIG];
    uint64_t pending, blocked, wake_deadline_ns, clear_tid, alarm_tick, sleep_remaining,
        last_syscall, user_ticks, voluntary_switches, involuntary_switches, system_ns, maxrss,
        inblock, outblock, signal_count, child_ticks, child_system_ns;
    bool wait_reported, resources_freed;
    struct process *next;
};

struct process *process_current(void);
struct process *process_find(int pid);
struct process *process_list(void);
void process_init(void);
_Noreturn void process_start(const char *path);
struct x64_frame *process_schedule(struct x64_frame *frame, bool rotate);
struct x64_frame *process_interrupt(struct x64_frame *frame);
struct x64_frame *syscall_dispatch(struct x64_frame *frame);
void process_exit(int status, bool signal);
long process_fork(struct x64_frame *frame);
long process_exec(const char *path, char *const argv[], char *const envp[],
                  struct x64_frame *frame);
long process_wait(int pid, uint64_t status, int options, uint64_t usage);
void process_update_memory(struct process *process);
long process_sigreturn(struct x64_frame *frame);
long process_kill(int pid, int signal);
void process_signal(struct process *process, int signal);
void process_wake_io(void);
long process_block(int reason);
long elf_load(const char *path, char *const argv[], char *const envp[],
              struct address_space **space, uint64_t *entry, uint64_t *stack, uint64_t *heap);
void x64_set_kernel_stack(uint64_t stack);
void x64_usermode_init(void);
_Noreturn void x64_enter_frame(struct x64_frame *frame);

#endif
