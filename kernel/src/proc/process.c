#include "net/net.h"
#include "process.h"
#include "console/kprintf.h"
#include "drivers/tty.h"
#include "kstring.h"

static struct process *tasks, *current, *idle;
static int next_pid = 1;
static uint8_t clean_fx[512] __attribute__((aligned(16)));
static bool active;

struct process *process_current(void) {
    return current && current != idle ? current : NULL;
}
struct process *process_list(void) {
    return tasks;
}
struct process *process_find(int pid) {
    for (struct process *p = tasks; p; p = p->next)
        if (p->pid == pid)
            return p;
    return NULL;
}
static void idle_entry(void) {
    for (;;)
        hal_idle();
}
static struct process *allocate_task(bool kernel) {
    struct process *p = kcalloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->stack_physical = pmm_alloc(8);
    if (!p->stack_physical) {
        kfree(p);
        return NULL;
    }
    p->stack_top = (uintptr_t)physical_pointer(p->stack_physical) + 8 * PAGE_SIZE;
    p->frame = (void *)(uintptr_t)(p->stack_top - sizeof(*p->frame));
    *p->frame = (struct x64_frame){.cs = kernel ? 8 : 0x23,
                                   .ss = kernel ? 16 : 0x1b,
                                   .rflags = 0x202,
                                   .rsp = p->stack_top - 8};
    memcpy(p->fx, clean_fx, 512);
    p->mmap_cursor = 0x40000000;
    p->umask = 0022;
    return p;
}
void process_init(void) {
    uint64_t cr0, cr4;
    __asm__ volatile("mov %%cr0,%0;mov %%cr4,%1" : "=r"(cr0), "=r"(cr4));
    cr0 = (cr0 & ~12ull) | 2;
    cr4 |= 0x600;
    __asm__ volatile("mov %0,%%cr0;mov %1,%%cr4;fninit" ::"r"(cr0), "r"(cr4) : "memory");
    uint32_t mxcsr = 0x1f80;
    __asm__ volatile("ldmxcsr %0;fxsave64 %1" ::"m"(mxcsr), "m"(clean_fx) : "memory");
    x64_usermode_init();
    idle = allocate_task(true);
    if (!idle)
        hal_panic("idle process allocation failed");
    idle->frame->rip = (uintptr_t)idle_entry;
}
long process_exec(const char *path, char *const argv[], char *const envp[],
                  struct x64_frame *frame) {
    struct address_space *space;
    uint64_t entry, stack, heap;
    long e = elf_load(path, argv, envp, &space, &entry, &stack, &heap);
    if (e < 0)
        return e;
    struct process *p = current;
    struct vnode *executable;
    if (vfs_resolve(p->cwd, path, true, &executable) >= 0) {
        if (executable->mode & S_ISUID)
            p->euid = executable->uid;
        if (executable->mode & S_ISGID)
            p->egid = executable->gid;
        p->suid = p->euid;
        p->sgid = p->egid;
    }
    struct address_space *old = p->space;
    p->space = space;
    vmm_activate(space);
    vmm_destroy(old);
    p->brk_start = p->brk_end = heap;
    p->mmap_cursor = 0x40000000;
    p->fs_base = 0;
    wrmsr(0xc0000100, 0);
    memcpy(p->fx, clean_fx, 512);
    __asm__ volatile("fxrstor64 %0" ::"m"(p->fx) : "memory");
    for (unsigned i = 0; i < FD_MAX; i++)
        if (p->fds[i].file && (p->fds[i].flags & FD_CLOEXEC)) {
            file_release(p->fds[i].file);
            p->fds[i] = (struct descriptor){0};
        }
    for (unsigned i = 1; i < NSIG; i++)
        if (p->actions[i].handler != 1)
            p->actions[i] = (struct abi_sigaction){0};
    kstrlcpy(p->executable, path, sizeof(p->executable));
    const char *name = path;
    for (const char *s = path; *s; s++)
        if (*s == '/')
            name = s + 1;
    kstrlcpy(p->name, name, sizeof(p->name));
    process_update_memory(p);
    *frame =
        (struct x64_frame){.rip = entry, .rsp = stack, .cs = 0x23, .ss = 0x1b, .rflags = 0x202};
    p->frame = frame;
    return 0;
}
_Noreturn void process_start(const char *path) {
    current = allocate_task(false);
    if (!current)
        hal_panic("init process allocation failed");
    current->pid = next_pid++;
    current->pgrp = current->sid = current->pid;
    current->cwd = vfs_root;
    vnode_retain(vfs_root);
    tasks = current;
    struct vnode *console;
    if (vfs_resolve(vfs_root, "/dev/console", true, &console) < 0)
        hal_panic("console missing");
    for (unsigned i = 0; i < 3; i++) {
        current->fds[i].file = file_new(console, i ? O_WRONLY : O_RDONLY);
        if (!current->fds[i].file)
            hal_panic("init descriptor allocation failed");
    }
    char *argv[] = {(char *)path, NULL},
         *envp[] = {"PATH=/bin:/sbin", "HOME=/root", "TERM=ansi", "USER=root", NULL};
    if (process_exec(path, argv, envp, current->frame) < 0)
        hal_panic("cannot execute /sbin/init");
    procfs_add(current->pid);
    active = true;
    x64_set_kernel_stack(current->stack_top);
    kprintf("[scheduler] preemptive round-robin, entering ring 3 pid=%d\n", current->pid);
    x64_enter_frame(current->frame);
}
long process_fork(struct x64_frame *frame) {
    struct process *parent = current, *child = allocate_task(false);
    if (!child)
        return -ENOMEM;
    child->space = vmm_clone(parent->space);
    if (!child->space) {
        pmm_free(child->stack_physical, 8);
        kfree(child);
        return -ENOMEM;
    }
    child->pid = next_pid++;
    child->ppid = parent->pid;
    child->pgrp = parent->pgrp;
    child->sid = parent->sid;
    child->uid = parent->uid;
    child->euid = parent->euid;
    child->suid = parent->suid;
    child->gid = parent->gid;
    child->egid = parent->egid;
    child->sgid = parent->sgid;
    child->umask = parent->umask;
    child->brk_start = parent->brk_start;
    child->brk_end = parent->brk_end;
    child->mmap_cursor = parent->mmap_cursor;
    child->fs_base = parent->fs_base;
    child->blocked = parent->blocked;
    memcpy(child->actions, parent->actions, sizeof(child->actions));
    child->cwd = parent->cwd;
    vnode_retain(child->cwd);
    kstrlcpy(child->name, parent->name, sizeof(child->name));
    kstrlcpy(child->executable, parent->executable, sizeof(child->executable));
    for (unsigned i = 0; i < FD_MAX; i++) {
        child->fds[i] = parent->fds[i];
        file_retain(child->fds[i].file);
    }
    __asm__ volatile("fxsave64 %0" : "=m"(parent->fx));
    memcpy(child->fx, parent->fx, 512);
    *child->frame = *frame;
    child->frame->rax = 0;
    process_update_memory(child);
    child->next = tasks;
    tasks = child;
    procfs_add(child->pid);
    return child->pid;
}
void process_wake_io(void) {
    for (struct process *p = tasks; p; p = p->next)
        if (p->state == PROCESS_BLOCKED && p->block_reason == BLOCK_IO)
            p->state = PROCESS_RUNNABLE;
}
long process_block(int reason) {
    current->state = PROCESS_BLOCKED;
    current->block_reason = reason;
    return SYSCALL_BLOCK;
}
static void notify_parent(struct process *p) {
    struct process *parent = process_find(p->ppid);
    if (parent) {
        if (!(p->state == PROCESS_STOPPED && (parent->actions[SIGCHLD].flags & SA_NOCLDSTOP)))
            process_signal(parent, SIGCHLD);
        if (parent->state == PROCESS_BLOCKED && parent->block_reason == BLOCK_WAIT)
            parent->state = PROCESS_RUNNABLE;
    }
}
void process_exit(int status, bool signal) {
    struct process *p = current;
    if (!p || p == idle)
        return;
    p->state = PROCESS_ZOMBIE;
    p->wait_status = signal ? (status & 127) : ((status & 255) << 8);
    p->wait_reported = false;
    for (unsigned i = 0; i < FD_MAX; i++) {
        file_release(p->fds[i].file);
        p->fds[i].file = NULL;
    }
    for (struct process *child = tasks; child; child = child->next)
        if (child->ppid == p->pid) {
            child->ppid = 1;
            if (child->state == PROCESS_ZOMBIE) {
                struct process *init = process_find(1);
                if (init)
                    process_signal(init, SIGCHLD);
            }
        }
    notify_parent(p);
    if (p->pid == 1)
        hal_panic("init exited");
}
static void reclaim(struct process *p) {
    if (p->clear_tid) {
        uint32_t zero = 0;
        copy_to_user(p->space, p->clear_tid, &zero, 4);
    }
    vmm_destroy(p->space);
    vnode_release(p->cwd);
    pmm_free(p->stack_physical, 8);
    procfs_remove(p->pid);
    struct process **link = &tasks;
    while (*link && *link != p)
        link = &(*link)->next;
    if (*link)
        *link = p->next;
    kfree(p);
}
long process_wait(int pid, uint64_t status, int options, uint64_t usage) {
    if (options & ~(WNOHANG | WUNTRACED | WCONTINUED))
        return -EINVAL;
    bool children = false;
    for (struct process *p = tasks; p; p = p->next) {
        if (p->ppid != current->pid)
            continue;
        if (pid > 0 && p->pid != pid)
            continue;
        if (pid == 0 && p->pgrp != current->pgrp)
            continue;
        if (pid < -1 && p->pgrp != -pid)
            continue;
        children = true;
        if (p->state == PROCESS_ZOMBIE ||
            (p->state == PROCESS_STOPPED && (options & WUNTRACED) && !p->wait_reported) ||
            (p->state != PROCESS_STOPPED && p->state != PROCESS_ZOMBIE &&
             p->wait_status == 0xffff && (options & WCONTINUED) && !p->wait_reported)) {
            uint64_t utime = (p->user_ticks + p->child_ticks) * 10000,
                     stime = (p->system_ns + p->child_system_ns) / 1000;
            struct abi_rusage report = {.user_sec = utime / 1000000,
                                        .user_usec = utime % 1000000,
                                        .system_sec = stime / 1000000,
                                        .system_usec = stime % 1000000,
                                        .maxrss = p->maxrss,
                                        .inblock = p->inblock,
                                        .oublock = p->outblock,
                                        .nsignals = p->signal_count,
                                        .nvcsw = p->voluntary_switches,
                                        .nivcsw = p->involuntary_switches};
            if (usage && !copy_to_user(current->space, usage, &report, sizeof(report)))
                return -EFAULT;
            int result = p->pid;
            if (status && !copy_to_user(current->space, status, &p->wait_status, 4))
                return -EFAULT;
            p->wait_reported = true;
            if (p->state == PROCESS_ZOMBIE) {
                current->child_ticks += p->user_ticks + p->child_ticks;
                current->child_system_ns += p->system_ns + p->child_system_ns;
                reclaim(p);
            }
            return result;
        }
    }
    if (!children)
        return -ECHILD;
    if (options & WNOHANG)
        return 0;
    return process_block(BLOCK_WAIT);
}
void process_signal(struct process *p, int signal) {
    if (!p || signal < 1 || signal >= NSIG || p->state == PROCESS_ZOMBIE)
        return;
    if (signal == SIGCONT) {
        if (p->state == PROCESS_STOPPED) {
            bool sleeping = p->block_reason == BLOCK_SLEEP && p->frame && p->frame->vector == 256 &&
                            p->last_syscall == SYS_nanosleep &&
                            p->wake_deadline_ns > hal_counter_ns();
            p->state = sleeping ? PROCESS_BLOCKED : PROCESS_RUNNABLE;
            p->wait_status = 0xffff;
            p->wait_reported = false;
            notify_parent(p);
        }
        p->pending &= ~((1ull << (SIGSTOP - 1)) | (1ull << (SIGTSTP - 1)) |
                        (1ull << (SIGTTIN - 1)) | (1ull << (SIGTTOU - 1)));
    }
    if (signal == SIGSTOP || signal == SIGTSTP || signal == SIGTTIN || signal == SIGTTOU)
        p->pending &= ~(1ull << (SIGCONT - 1));
    bool ignored = p->actions[signal].handler == 1 ||
                   (!p->actions[signal].handler && (signal == SIGCHLD || signal == SIGWINCH ||
                                                    signal == SIGCONT || signal == SIGURG));
    if (ignored && signal != SIGKILL && signal != SIGSTOP)
        return;
    p->pending |= 1ull << (signal - 1);
    if (signal == SIGKILL && p->state == PROCESS_STOPPED)
        p->state = PROCESS_RUNNABLE;
    if (p->state == PROCESS_BLOCKED &&
        (!(p->blocked & (1ull << (signal - 1))) || signal == SIGKILL)) {
        p->state = PROCESS_RUNNABLE;
        if (p->frame && p->frame->vector == 256 && p->actions[signal].handler > 1 &&
            (!(p->actions[signal].flags & SA_RESTART) || p->block_reason == BLOCK_SLEEP)) {
            if (p->block_reason != BLOCK_SLEEP)
                p->frame->rip += 2;
            else if (p->sleep_remaining) {
                uint64_t now = hal_counter_ns();
                uint64_t remaining = p->wake_deadline_ns > now ? p->wake_deadline_ns - now : 0;
                struct abi_timespec value = {remaining / 1000000000, remaining % 1000000000};
                copy_to_user(p->space, p->sleep_remaining, &value, sizeof(value));
            }
            p->net_deadline = p->net_operation = 0;
            p->frame->rax = -EINTR;
        }
    }
}
long process_kill(int pid, int signal) {
    if (signal < 0 || signal >= NSIG)
        return -EINVAL;
    bool found = false, permitted = false;
    for (struct process *p = tasks; p; p = p->next) {
        if ((pid > 0 && p->pid != pid) || (pid == 0 && p->pgrp != current->pgrp) ||
            (pid < -1 && p->pgrp != -pid) || (pid == -1 && p->pid == 1))
            continue;
        if (p->state == PROCESS_ZOMBIE)
            continue;
        found = true;
        if (current->euid && current->uid != p->uid && current->uid != p->suid &&
            current->euid != p->uid && current->euid != p->suid &&
            !(signal == SIGCONT && current->sid == p->sid))
            continue;
        permitted = true;
        if (signal)
            process_signal(p, signal);
    }
    return permitted ? 0 : found ? -EPERM : -ESRCH;
}
struct signal_frame {
    uint64_t cookie, mask;
    struct x64_frame interrupted;
    uint8_t fx[512] __attribute__((aligned(16)));
};
static bool deliver_signal(struct process *p) {
    uint64_t pending = p->pending & ~p->blocked;
    pending |= p->pending & ((1ull << (SIGKILL - 1)) | (1ull << (SIGSTOP - 1)));
    while (pending) {
        unsigned index = __builtin_ctzll(pending);
        int signal = index + 1;
        uint64_t bit = 1ull << index;
        p->pending &= ~bit;
        pending &= ~bit;
        struct abi_sigaction action = p->actions[signal];
        if (signal == SIGKILL)
            action.handler = 0;
        if (signal == SIGSTOP)
            action.handler = 0;
        if (action.handler == 1)
            continue;
        if (!action.handler) {
            if (signal == SIGCHLD || signal == SIGCONT || signal == SIGWINCH || signal == SIGURG)
                continue;
            if (signal == SIGSTOP || signal == SIGTSTP || signal == SIGTTIN || signal == SIGTTOU) {
                if (p->frame->vector == 256 && (int64_t)p->frame->rax == -EINTR &&
                    p->last_syscall != SYS_nanosleep) {
                    p->frame->rip -= 2;
                    p->frame->rax = p->last_syscall;
                }
                p->state = PROCESS_STOPPED;
                p->wait_status = (signal << 8) | 127;
                p->wait_reported = false;
                notify_parent(p);
                return false;
            }
            process_exit(signal, true);
            return false;
        }
        if (action.handler >= USER_TOP || action.restorer < PAGE_SIZE ||
            action.restorer >= USER_TOP) {
            process_exit(SIGSEGV, true);
            return false;
        }
        p->signal_count++;
        struct signal_frame saved = {.mask = p->blocked, .interrupted = *p->frame};
        __asm__ volatile("fxsave64 %0" : "=m"(p->fx));
        memcpy(saved.fx, p->fx, 512);
        uint64_t stack = ((p->frame->rsp - 128 - sizeof(saved) - 8) & ~15ull) - 8;
        saved.cookie = 0x5349474652414d45ull ^ stack ^ (uint64_t)p->pid;
        if (stack >= USER_TOP || !copy_to_user(p->space, stack, &action.restorer, 8) ||
            !copy_to_user(p->space, stack + 8, &saved, sizeof(saved))) {
            process_exit(SIGSEGV, true);
            return false;
        }
        p->frame->rsp = stack;
        p->frame->rip = action.handler;
        p->frame->rdi = signal;
        p->frame->vector = 0;
        p->blocked |= action.mask;
        if (!(action.flags & SA_NODEFER))
            p->blocked |= bit;
        p->blocked &= ~((1ull << (SIGKILL - 1)) | (1ull << (SIGSTOP - 1)));
        if (action.flags & SA_RESETHAND)
            p->actions[signal].handler = 0;
        return true;
    }
    return true;
}
long process_sigreturn(struct x64_frame *frame) {
    struct signal_frame saved;
    uint64_t address = frame->rsp;
    if (!copy_from_user(current->space, &saved, address, sizeof(saved)) ||
        saved.cookie != (0x5349474652414d45ull ^ (address - 8) ^ (uint64_t)current->pid) ||
        saved.interrupted.rip < PAGE_SIZE || saved.interrupted.rip >= USER_TOP ||
        saved.interrupted.rsp < PAGE_SIZE || saved.interrupted.rsp >= USER_TOP) {
        process_exit(SIGSEGV, true);
        return 0;
    }
    current->blocked = saved.mask & ~((1ull << (SIGKILL - 1)) | (1ull << (SIGSTOP - 1)));
    *frame = saved.interrupted;
    frame->cs = 0x23;
    frame->ss = 0x1b;
    frame->rflags = (frame->rflags & 0xcd5) | 0x202;
    frame->vector = 0;
    memcpy(current->fx, saved.fx, 512);
    uint32_t mxcsr;
    memcpy(&mxcsr, current->fx + 24, 4);
    mxcsr &= 0xffbf;
    memcpy(current->fx + 24, &mxcsr, 4);
    __asm__ volatile("fxrstor64 %0" ::"m"(current->fx) : "memory");
    return frame->rax;
}
struct x64_frame *process_schedule(struct x64_frame *frame, bool rotate) {
    if (!active)
        return frame;
    struct process *old = current;
    old->frame = frame;
    __asm__ volatile("fxsave64 %0" : "=m"(old->fx));
    for (struct process *p = tasks; p; p = p->next)
        if (p->state == PROCESS_BLOCKED && p->block_reason == BLOCK_SLEEP &&
            hal_counter_ns() >= p->wake_deadline_ns)
            p->state = PROCESS_RUNNABLE;
    struct process *choice = NULL, *start = (rotate && old != idle) ? old->next : old;
    if (!start || start == idle)
        start = tasks;
    struct process *p = start;
    if (p)
        do {
            if (p->state == PROCESS_RUNNABLE) {
                choice = p;
                break;
            }
            p = p->next ? p->next : tasks;
        } while (p != start);
    if (!choice)
        choice = idle;
    if (choice != old && old != idle) {
        if (frame->vector == 48)
            old->involuntary_switches++;
        else
            old->voluntary_switches++;
    }
    current = choice;
    vmm_activate(choice == idle ? NULL : choice->space);
    x64_set_kernel_stack(choice->stack_top);
    wrmsr(0xc0000100, choice == idle ? 0 : choice->fs_base);
    __asm__ volatile("fxrstor64 %0" ::"m"(choice->fx) : "memory");
    if (choice != idle && !deliver_signal(choice))
        return process_schedule(choice->frame, true);
    return choice->frame;
}
struct x64_frame *process_interrupt(struct x64_frame *frame) {
    if (frame->vector < 32) {
        if ((frame->cs & 3) == 3 && active) {
            int signal = frame->vector == 0 || frame->vector == 16 || frame->vector == 19 ? SIGFPE
                         : frame->vector == 6                                             ? SIGILL
                         : (frame->vector == 1 || frame->vector == 3)                     ? SIGTRAP
                         : frame->vector == 17                                            ? SIGBUS
                                                                                          : SIGSEGV;
            if (frame->vector == 14) {
                uint64_t fault;
                __asm__ volatile("mov %%cr2,%0" : "=r"(fault));
                for (struct vm_page *page = current->space->pages; page; page = page->next)
                    if (page->address == (fault & ~4095ull) && (page->flags & VM_INVALID)) {
                        signal = SIGBUS;
                        break;
                    }
            }
            if ((current->blocked & (1ull << (signal - 1))) ||
                current->actions[signal].handler == 1)
                process_exit(signal, true);
            else
                process_signal(current, signal);
            return process_schedule(frame, false);
        }
        return NULL;
    }
    x64_apic_dispatch((uint8_t)frame->vector);
    if (frame->vector == 48) {
        tty_poll();
        if (active && ((frame->cs & 3) == 3 || current == idle))
            net_poll();
        if (active && ((frame->cs & 3) == 3 || current == idle)) {
            if (current != idle)
                current->user_ticks++;
            return process_schedule(frame, true);
        }
    }
    return frame;
}

void process_update_memory(struct process *p) {
    uint64_t pages = 0;
    for (struct vm_page *v = p->space->pages; v; v = v->next)
        pages++;
    if (pages * 4 > p->maxrss)
        p->maxrss = pages * 4;
}
