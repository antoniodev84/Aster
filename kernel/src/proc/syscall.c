#include "net/net.h"
#include "process.h"
#include "drivers/tty.h"
#include "kstring.h"
#include "console/kprintf.h"

static struct open_file *get_file(int fd) {
    return fd >= 0 && fd < FD_MAX ? process_current()->fds[fd].file : NULL;
}
static long path_copy(uint64_t user, char *path) {
    long length = user_cstring(process_current()->space, path, user, PATH_MAX);
    return length < 0 ? length : 0;
}
static int descriptor_free(int start) {
    if (start < 0 || start >= FD_MAX)
        return -EINVAL;
    struct process *p = process_current();
    for (int i = start; i < FD_MAX; i++)
        if (!p->fds[i].file)
            return i;
    return -EMFILE;
}
static long close_fd(int fd) {
    struct process *p = process_current();
    struct open_file *f = get_file(fd);
    if (!f)
        return -EBADF;
    p->fds[fd] = (struct descriptor){0};
    return file_release(f);
}
static long open_path(int dirfd, uint64_t path_u, unsigned flags, uint32_t mode) {
    struct process *p = process_current();
    char path[PATH_MAX];
    long e = path_copy(path_u, path);
    if (e < 0)
        return e;
    if ((flags & 3) == 3)
        return -EINVAL;
    struct vnode *base = p->cwd;
    if (path[0] != '/' && dirfd != AT_FDCWD) {
        struct open_file *directory = get_file(dirfd);
        if (!directory)
            return -EBADF;
        if (!directory->node || directory->pipe || !S_ISDIR(directory->node->mode))
            return -ENOTDIR;
        base = directory->node;
    }
    struct vnode *node;
    e = vfs_resolve(base, path,
                    !(flags & O_NOFOLLOW) && ((flags & (O_CREAT | O_EXCL)) != (O_CREAT | O_EXCL)),
                    &node);
    if (e == -ENOENT && (flags & O_CREAT))
        e = vfs_create_follow(base, path, S_IFREG | (mode & 0777 & ~p->umask), &node);
    else if (e >= 0 && (flags & O_CREAT) && (flags & O_EXCL))
        return -EEXIST;
    if (e < 0)
        return e;
    if (S_ISLNK(node->mode))
        return -ELOOP;
    if ((flags & O_DIRECTORY) && !S_ISDIR(node->mode))
        return -ENOTDIR;
    if (S_ISDIR(node->mode) && (flags & 3) != O_RDONLY)
        return -EISDIR;
    unsigned access = (flags & 3) == O_RDONLY ? 4 : (flags & 3) == O_WRONLY ? 2 : 6;
    if ((e = vfs_access(node, access)) < 0)
        return e;
    if (flags & O_TRUNC) {
        if ((flags & 3) == O_RDONLY)
            return -EACCES;
        if (!S_ISREG(node->mode))
            return -EINVAL;
        if ((e = vfs_truncate(node, 0)) < 0)
            return e;
    }
    int fd = descriptor_free(0);
    if (fd < 0)
        return fd;
    struct open_file *file = file_new(node, flags);
    if (!file)
        return -ENOMEM;
    p->fds[fd] = (struct descriptor){file, (flags & O_CLOEXEC) ? FD_CLOEXEC : 0};
    return fd;
}
static long io_file(int fd, uint64_t pointer, size_t count, bool write) {
    struct process *p = process_current();
    struct open_file *file = get_file(fd);
    if (!file)
        return -EBADF;
    if (count > INT64_MAX)
        return -EINVAL;
    if (!user_range(p->space, pointer, count, !write))
        return -EFAULT;
    if (!count)
        return 0;
    if (file->socket)
        return net_syscall(write ? SYS_sendto : SYS_recvfrom, fd, pointer, count, 0, 0, 0);
    uint8_t *buffer = kmalloc(PAGE_SIZE);
    if (!buffer)
        return -ENOMEM;
    size_t done = 0;
    long result = 0;
    while (done < count) {
        size_t chunk = count - done;
        if (chunk > PAGE_SIZE)
            chunk = PAGE_SIZE;
        if (write && !copy_from_user(p->space, buffer, pointer + done, chunk)) {
            result = -EFAULT;
            break;
        }
        result = write ? file_write(file, buffer, chunk) : file_read(file, buffer, chunk);
        if (result == -EAGAIN) {
            if (done) {
                result = done;
                break;
            }
            if (!(file->flags & O_NONBLOCK))
                result = process_block(BLOCK_IO);
            break;
        }
        if (result == -EPIPE)
            process_signal(p, SIGPIPE);
        if (result <= 0)
            break;
        if (!write && !copy_to_user(p->space, pointer + done, buffer, result)) {
            result = -EFAULT;
            break;
        }
        done += result;
        if ((size_t)result < chunk)
            break;
        if (!write && file->node && file->node->device == 1)
            break;
    }
    kfree(buffer);
    return done ? (long)done : result;
}
static long duplicate(int oldfd, int newfd, unsigned flags, bool specified) {
    struct process *p = process_current();
    struct open_file *f = get_file(oldfd);
    if (!f)
        return -EBADF;
    if (newfd < 0 || newfd >= FD_MAX)
        return -EBADF;
    if (!specified) {
        newfd = descriptor_free(newfd);
        if (newfd < 0)
            return newfd;
    }
    if (oldfd == newfd)
        return oldfd;
    if (p->fds[newfd].file)
        close_fd(newfd);
    file_retain(f);
    p->fds[newfd] = (struct descriptor){f, flags};
    return newfd;
}
static long make_pipe(uint64_t descriptors, unsigned flags) {
    if (flags & ~(O_CLOEXEC | O_NONBLOCK))
        return -EINVAL;
    struct process *p = process_current();
    if (!user_range(p->space, descriptors, 8, true))
        return -EFAULT;
    int first = descriptor_free(0);
    if (first < 0)
        return first;
    struct open_file *reader = file_new(NULL, O_RDONLY | flags);
    if (!reader)
        return -ENOMEM;
    p->fds[first] = (struct descriptor){reader, flags & O_CLOEXEC ? FD_CLOEXEC : 0};
    int second = descriptor_free(0);
    if (second < 0) {
        close_fd(first);
        return second;
    }
    struct open_file *writer = file_new(NULL, O_WRONLY | flags);
    struct pipe *pipe = kcalloc(1, sizeof(*pipe));
    if (!writer || !pipe) {
        kfree(pipe);
        file_release(writer);
        close_fd(first);
        return -ENOMEM;
    }
    pipe->readers = pipe->writers = 1;
    reader->pipe = writer->pipe = pipe;
    writer->pipe_writer = true;
    p->fds[second] = (struct descriptor){writer, flags & O_CLOEXEC ? FD_CLOEXEC : 0};
    int fds[2] = {first, second};
    copy_to_user(p->space, descriptors, fds, 8);
    return 0;
}
static long getdents(int fd, uint64_t pointer, size_t count) {
    struct process *p = process_current();
    struct open_file *f = get_file(fd);
    if (!f)
        return -EBADF;
    if (count > 65536)
        count = 65536;
    if (!user_range(p->space, pointer, count, true))
        return -EFAULT;
    void *buffer = kmalloc(count);
    if (!buffer)
        return count ? -ENOMEM : -EINVAL;
    long result = file_getdents(f, buffer, count);
    if (result > 0)
        copy_to_user(p->space, pointer, buffer, result);
    kfree(buffer);
    return result;
}
static long memory_map(uint64_t address, size_t length, unsigned protection, unsigned flags, int fd,
                       uint64_t offset) {
    struct process *p = process_current();
    if (!length || length >= USER_TOP || protection & ~7u || offset & 4095 ||
        ((flags & (MAP_PRIVATE | MAP_SHARED)) != MAP_PRIVATE &&
         (flags & (MAP_PRIVATE | MAP_SHARED)) != MAP_SHARED) ||
        flags & ~(MAP_PRIVATE | MAP_SHARED | MAP_FIXED | MAP_ANONYMOUS))
        return -EINVAL;
    length = (length + 4095) & ~4095ull;
    unsigned vm = VM_USER | ((protection & PROT_WRITE) ? VM_WRITE : 0) |
                  ((protection & PROT_EXEC) ? VM_EXEC : 0) | (!protection ? VM_NONE : 0) |
                  ((flags & MAP_SHARED) ? VM_SHARED : 0);
    if (flags & MAP_FIXED) {
        if (address & 4095 || address < PAGE_SIZE || address >= USER_TOP ||
            length > USER_TOP - address)
            return -EINVAL;
        if (!vmm_unmap(p->space, address, length))
            return -EIO;
    } else {
        address &= ~4095ull;
        if (address < PAGE_SIZE)
            address = p->mmap_cursor;
        for (;;) {
            if (address >= 0x700000000000ull || length > 0x700000000000ull - address)
                return -ENOMEM;
            bool collision = false;
            for (struct vm_page *page = p->space->pages; page; page = page->next)
                if (page->address >= address && page->address < address + length) {
                    address = page->address + PAGE_SIZE;
                    collision = true;
                    break;
                }
            if (!collision)
                break;
        }
    }
    struct open_file *file = NULL;
    if (!(flags & MAP_ANONYMOUS)) {
        file = get_file(fd);
        if (!file)
            return -EBADF;
        if (!file->node || file->pipe || !S_ISREG(file->node->mode))
            return -ENODEV;
        if ((file->flags & 3) == O_WRONLY)
            return -EACCES;
        if ((flags & MAP_SHARED) && (protection & PROT_WRITE) && (file->flags & 3) != O_RDWR)
            return -EACCES;
    }
    size_t done = 0;
    for (; done < length; done += PAGE_SIZE) {
        uint64_t physical;
        if (file && (flags & MAP_SHARED))
            physical = file_cache_page(file->node, offset + done);
        else
            physical = pmm_alloc(1);
        if (!physical)
            break;
        if (file && !(flags & MAP_SHARED)) {
            long e = vfs_read(file->node, offset + done, physical_pointer(physical), PAGE_SIZE);
            if (e < 0) {
                pmm_free(physical, 1);
                break;
            }
        }
        unsigned page_flags = vm;
        if (file && offset + done >= file->node->size)
            page_flags |= VM_INVALID;
        if (!vmm_map(p->space, address + done, physical, page_flags)) {
            pmm_free(physical, 1);
            break;
        }
        if (file) {
            p->space->pages->file = file->node;
            p->space->pages->offset = offset + done;
            vnode_retain(file->node);
        }
    }
    if (done != length) {
        vmm_unmap(p->space, address, done);
        return -ENOMEM;
    }
    if (address + length > p->mmap_cursor)
        p->mmap_cursor = address + length;
    process_update_memory(p);
    return address;
}
static long change_brk(uint64_t address) {
    struct process *p = process_current();
    if (!address)
        return p->brk_end;
    if (address < p->brk_start || address >= 0x40000000ull)
        return p->brk_end;
    uint64_t old = (p->brk_end + 4095) & ~4095ull, new = (address + 4095) & ~4095ull;
    if (new > old && !vmm_allocate(p->space, old, new - old, VM_USER | VM_WRITE))
        return p->brk_end;
    if (new < old)
        vmm_unmap(p->space, new, old - new);
    p->brk_end = address;
    process_update_memory(p);
    return address;
}
static long metadata_path(uint64_t path_u, uint64_t out, bool follow) {
    char path[PATH_MAX];
    long e = path_copy(path_u, path);
    if (e < 0)
        return e;
    struct vnode *n;
    if ((e = vfs_resolve(process_current()->cwd, path, follow, &n)) < 0)
        return e;
    struct abi_stat stat;
    vfs_stat(n, &stat);
    return copy_to_user(process_current()->space, out, &stat, sizeof(stat)) ? 0 : -EFAULT;
}
static long attributes(struct vnode *n, uint32_t first, uint32_t second, bool owner) {
    struct process *p = process_current();
    if (!n)
        return -EBADF;
    if (n->read_only)
        return -EROFS;
    if (owner) {
        if (p->euid && ((first != (uint32_t)-1 && first != n->uid) || p->euid != n->uid ||
                        (second != (uint32_t)-1 && second != p->egid && second != n->gid)))
            return -EPERM;
        if (first != (uint32_t)-1)
            n->uid = first;
        if (second != (uint32_t)-1)
            n->gid = second;
        n->mode &= ~06000u;
    } else {
        if (p->euid && p->euid != n->uid)
            return -EPERM;
        n->mode = (n->mode & S_IFMT) | (first & 07777);
        if (p->euid && p->egid != n->gid)
            n->mode &= ~02000u;
    }
    n->ctime = hal_realtime_ms() / 1000;
    return n->ops->sync ? n->ops->sync(n) : 0;
}
static char **copy_vector(uint64_t pointer, long *error) {
    struct process *p = process_current();
    *error = 0;
    char **vector = kcalloc(128, sizeof(char *));
    if (!vector) {
        *error = -ENOMEM;
        return NULL;
    }
    if (!pointer)
        return vector;
    char *temporary = kmalloc(32768);
    if (!temporary) {
        kfree(vector);
        *error = -ENOMEM;
        return NULL;
    }
    size_t total = 0;
    for (unsigned i = 0; i < 128; i++) {
        uint64_t user;
        if (pointer > USER_TOP - i * 8 || !copy_from_user(p->space, &user, pointer + i * 8, 8)) {
            *error = -EFAULT;
            goto fail;
        }
        if (!user) {
            kfree(temporary);
            return vector;
        }
        if (i == 127) {
            *error = -E2BIG;
            goto fail;
        }
        long length = user_cstring(p->space, temporary, user, 32768);
        if (length < 0) {
            *error = length == -ENAMETOOLONG ? -E2BIG : length;
            goto fail;
        }
        if ((size_t)length + 1 > 32768 - total) {
            *error = -E2BIG;
            goto fail;
        }
        total += length + 1;
        vector[i] = kmalloc(length + 1);
        if (!vector[i]) {
            *error = -ENOMEM;
            goto fail;
        }
        memcpy(vector[i], temporary, length + 1);
    }
fail:
    for (unsigned i = 0; i < 128; i++)
        kfree(vector[i]);
    kfree(vector);
    kfree(temporary);
    return NULL;
}
static void free_vector(char **vector) {
    if (vector) {
        for (unsigned i = 0; vector[i]; i++)
            kfree(vector[i]);
        kfree(vector);
    }
}
static long exec_image(const char *path, char *const argv[], char *const envp[],
                       struct x64_frame *frame, unsigned depth) {
    long error = process_exec(path, argv, envp, frame);
    if (error != -ENOEXEC)
        return error;
    if (depth == 4)
        return -ELOOP;
    struct vnode *file;
    if (vfs_resolve(process_current()->cwd, path, true, &file) < 0)
        return error;
    char header[257];
    long length = vfs_read(file, 0, header, 256);
    if (length < 2 || header[0] != '#' || header[1] != '!')
        return error;
    header[length] = 0;
    char *end = kstrchr(header, '\n');
    if (end)
        *end = 0;
    else if (length == 256)
        return -ENOEXEC;
    char *interpreter = header + 2;
    while (*interpreter == ' ' || *interpreter == '\t')
        interpreter++;
    if (!*interpreter)
        return -ENOEXEC;
    char *argument = interpreter;
    while (*argument && *argument != ' ' && *argument != '\t')
        argument++;
    if (*argument) {
        *argument++ = 0;
        while (*argument == ' ' || *argument == '\t')
            argument++;
        end = argument + kstrlen(argument);
        while (end > argument && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
            *--end = 0;
    }
    char **next = kcalloc(128, sizeof(char *));
    if (!next)
        return -ENOMEM;
    size_t count = 0;
    next[count++] = interpreter;
    if (*argument)
        next[count++] = argument;
    next[count++] = (char *)path;
    if (argv && argv[0])
        for (size_t i = 1; argv[i]; i++) {
            if (count == 127) {
                kfree(next);
                return -E2BIG;
            }
            next[count++] = argv[i];
        }
    error = exec_image(interpreter, next, envp, frame, depth + 1);
    kfree(next);
    return error;
}
static long exec_user(uint64_t path_u, uint64_t argv_u, uint64_t envp_u, struct x64_frame *frame) {
    char path[PATH_MAX];
    long e = path_copy(path_u, path);
    if (e < 0)
        return e;
    long arguments_error, environment_error;
    char **argv = copy_vector(argv_u, &arguments_error),
         **envp = copy_vector(envp_u, &environment_error);
    if (!argv || !envp) {
        free_vector(argv);
        free_vector(envp);
        return arguments_error ? arguments_error : environment_error;
    }
    e = exec_image(path, argv, envp, frame, 0);
    free_vector(argv);
    free_vector(envp);
    return e;
}
static long ioctl_user(int fd, uint64_t request, uint64_t pointer) {
    struct process *p = process_current();
    struct open_file *f = get_file(fd);
    if (!f)
        return -EBADF;
    if (f->pipe || !f->node || f->node->device != 1)
        return -ENOTTY;
    size_t size;
    bool output = false;
    switch (request) {
    case TCGETS:
        output = true;
        size = sizeof(struct abi_termios);
        break;
    case TCSETS:
    case TCSETSW:
    case TCSETSF:
        size = sizeof(struct abi_termios);
        break;
    case TIOCGWINSZ:
        output = true;
        size = sizeof(struct abi_winsize);
        break;
    case TIOCSWINSZ:
        size = sizeof(struct abi_winsize);
        break;
    case FIONREAD:
    case TIOCGPGRP:
        output = true;
        size = 4;
        break;
    case TIOCSPGRP:
        size = 4;
        break;
    default:
        return -ENOTTY;
    }
    uint8_t buffer[sizeof(struct abi_termios)];
    if (!user_range(p->space, pointer, size, output))
        return -EFAULT;
    if (!output)
        copy_from_user(p->space, buffer, pointer, size);
    long e = tty_ioctl(request, buffer);
    if (e >= 0 && output)
        copy_to_user(p->space, pointer, buffer, size);
    return e;
}
static long vector_io(int fd, uint64_t vector, int count, bool write) {
    if (count < 0 || count > 1024)
        return -EINVAL;
    struct process *p = process_current();
    long done = 0;
    for (int i = 0; i < count; i++) {
        struct abi_iovec io;
        if (!copy_from_user(p->space, &io, vector + (uint64_t)i * sizeof(io), sizeof(io)))
            return done ? done : -EFAULT;
        long e = io_file(fd, io.base, io.length, write);
        if (e == SYSCALL_BLOCK && done) {
            p->state = PROCESS_RUNNABLE;
            return done;
        }
        if (e < 0)
            return done ? done : e;
        done += e;
        if ((uint64_t)e < io.length)
            break;
    }
    return done;
}
static long sys_dispatch(struct x64_frame *f) {
    struct process *p = process_current();
    uint64_t a = f->rdi, b = f->rsi, c = f->rdx, d = f->r10, e = f->r8, g = f->r9;
    switch (f->rax) {
    case SYS_socket: case SYS_connect: case SYS_accept: case SYS_sendto:
    case SYS_recvfrom: case SYS_shutdown: case SYS_bind: case SYS_listen:
    case SYS_getsockname: case SYS_getpeername: case SYS_setsockopt:
    case SYS_getsockopt: case SYS_netconfig: case SYS_resolve: case SYS_dhcp:
        return net_syscall(f->rax, a, b, c, d, e, g);
    case SYS_read:
        return io_file(a, b, c, false);
    case SYS_write:
        return io_file(a, b, c, true);
    case SYS_open:
        return open_path(AT_FDCWD, a, b, c);
    case SYS_openat:
        return open_path(a, b, c, d);
    case SYS_close:
        return close_fd(a);
    case SYS_readv:
        return vector_io(a, b, c, false);
    case SYS_writev:
        return vector_io(a, b, c, true);
    case SYS_stat:
        return metadata_path(a, b, true);
    case SYS_lstat:
        return metadata_path(a, b, false);
    case SYS_fstat: {
        struct open_file *file = get_file(a);
        if (!file)
            return -EBADF;
        struct abi_stat stat;
        if (file->pipe || file->socket) {
            memset(&stat, 0, sizeof(stat));
            stat.st_mode = (file->socket ? S_IFSOCK : S_IFIFO) | 0600;
            stat.st_nlink = 1;
            stat.st_blksize = 4096;
        } else
            vfs_stat(file->node, &stat);
        return copy_to_user(p->space, b, &stat, sizeof(stat)) ? 0 : -EFAULT;
    }
    case SYS_getdents64:
        return getdents(a, b, c);
    case SYS_lseek: {
        struct open_file *file = get_file(a);
        if (!file)
            return -EBADF;
        if (!file->node || file->pipe || S_ISCHR(file->node->mode))
            return -ESPIPE;
        int64_t offset = b, base = c == SEEK_SET   ? 0
                                   : c == SEEK_CUR ? file->offset
                                   : c == SEEK_END ? file->node->size
                                                   : -1;
        if (base < 0 || (offset < 0 && offset < -base) || (offset > 0 && offset > INT64_MAX - base))
            return -EINVAL;
        file->offset = base + offset;
        return file->offset;
    }
    case SYS_mmap:
        return memory_map(a, b, c, d, e, g);
    case SYS_munmap:
        if (!b || a & 4095 || a >= USER_TOP || b > USER_TOP - a)
            return -EINVAL;
        return vmm_unmap(p->space, a, b) ? 0 : -EIO;
    case SYS_mprotect: {
        if (!b || a & 4095 || c & ~7u)
            return -EINVAL;
        unsigned flags = VM_USER | ((c & PROT_WRITE) ? VM_WRITE : 0) |
                         ((c & PROT_EXEC) ? VM_EXEC : 0) | (!c ? VM_NONE : 0);
        return vmm_protect(p->space, a, b, flags) ? 0 : -ENOMEM;
    }
    case SYS_brk:
        return change_brk(a);
    case SYS_dup:
        return duplicate(a, 0, 0, false);
    case SYS_dup2:
        return duplicate(a, b, 0, true);
    case SYS_dup3:
        if (a == b || c & ~O_CLOEXEC)
            return -EINVAL;
        return duplicate(a, b, c & O_CLOEXEC ? FD_CLOEXEC : 0, true);
    case SYS_pipe:
        return make_pipe(a, 0);
    case SYS_pipe2:
        return make_pipe(a, b);
    case SYS_fcntl: {
        struct open_file *file = get_file(a);
        if (!file)
            return -EBADF;
        switch (b) {
        case F_DUPFD:
            return duplicate(a, c, 0, false);
        case F_DUPFD_CLOEXEC:
            return duplicate(a, c, FD_CLOEXEC, false);
        case F_GETFD:
            return p->fds[a].flags;
        case F_SETFD:
            p->fds[a].flags = c & FD_CLOEXEC;
            return 0;
        case F_GETFL:
            return file->flags;
        case F_SETFL:
            file->flags = (file->flags & ~(O_APPEND | O_NONBLOCK)) | (c & (O_APPEND | O_NONBLOCK));
            return 0;
        default:
            return -EINVAL;
        }
    }
    case SYS_fsync: {
        struct open_file *file = get_file(a);
        if (!file)
            return -EBADF;
        if (file->pipe)
            return -EINVAL;
        return file->socket ? -EINVAL : file_cache_flush(file->node);
    }
    case SYS_ftruncate: {
        struct open_file *file = get_file(a);
        if (!file)
            return -EBADF;
        if (!file->node || file->pipe || !S_ISREG(file->node->mode))
            return -EINVAL;
        if ((file->flags & 3) == O_RDONLY)
            return -EBADF;
        return vfs_truncate(file->node, b);
    }
    case SYS_ioctl:
        return ioctl_user(a, b, c);
    case SYS_mkdir:
    case SYS_rmdir:
    case SYS_unlink:
    case SYS_chdir:
    case SYS_access:
    case SYS_chmod:
    case SYS_chown:
    case SYS_readlink: {
        char path[PATH_MAX];
        long result = path_copy(a, path);
        if (result < 0)
            return result;
        if (f->rax == SYS_mkdir) {
            struct vnode *node;
            return vfs_create(p->cwd, path, S_IFDIR | (b & 0777 & ~p->umask), &node);
        }
        if (f->rax == SYS_rmdir || f->rax == SYS_unlink)
            return vfs_remove(p->cwd, path, f->rax == SYS_rmdir);
        struct vnode *node;
        result = vfs_resolve(p->cwd, path, f->rax != SYS_readlink, &node);
        if (result < 0)
            return result;
        if (f->rax == SYS_access) {
            if (b & ~7u)
                return -EINVAL;
            uint32_t saved = p->euid, saved_gid = p->egid;
            p->euid = p->uid;
            p->egid = p->gid;
            result = vfs_access(node, b);
            p->euid = saved;
            p->egid = saved_gid;
            return result;
        }
        if (f->rax == SYS_chmod)
            return attributes(node, b, 0, false);
        if (f->rax == SYS_chown)
            return attributes(node, b, c, true);
        if (f->rax == SYS_readlink) {
            if (!S_ISLNK(node->mode) || !c)
                return -EINVAL;
            if (c > PATH_MAX)
                c = PATH_MAX;
            char *buffer = kmalloc(c);
            if (!buffer)
                return -ENOMEM;
            result = vfs_read(node, 0, buffer, c);
            if (result > 0 && !copy_to_user(p->space, b, buffer, result))
                result = -EFAULT;
            kfree(buffer);
            return result;
        }
        if (!S_ISDIR(node->mode))
            return -ENOTDIR;
        if ((result = vfs_access(node, 1)) < 0)
            return result;
        vnode_retain(node);
        vnode_release(p->cwd);
        p->cwd = node;
        return 0;
    }
    case SYS_fchdir: {
        struct open_file *file = get_file(a);
        if (!file)
            return -EBADF;
        if (!file->node || file->pipe || !S_ISDIR(file->node->mode))
            return -ENOTDIR;
        long result = vfs_access(file->node, 1);
        if (result < 0)
            return result;
        vnode_retain(file->node);
        vnode_release(p->cwd);
        p->cwd = file->node;
        return 0;
    }
    case SYS_getcwd: {
        if (b > PATH_MAX)
            b = PATH_MAX;
        char *buffer = kmalloc(b);
        if (!buffer)
            return b ? -ENOMEM : -ERANGE;
        long result = vfs_getcwd(p->cwd, buffer, b);
        if (result > 0 && !copy_to_user(p->space, a, buffer, result))
            result = -EFAULT;
        kfree(buffer);
        return result;
    }
    case SYS_fchmod:
    case SYS_fchown: {
        struct open_file *file = get_file(a);
        if (!file || file->pipe)
            return -EBADF;
        return attributes(file->node, b, c, f->rax == SYS_fchown);
    }
    case SYS_link:
    case SYS_symlink:
    case SYS_rename: {
        char first[PATH_MAX], second[PATH_MAX];
        long result = path_copy(a, first);
        if (result < 0)
            return result;
        if ((result = path_copy(b, second)) < 0)
            return result;
        return f->rax == SYS_link      ? vfs_link(p->cwd, first, second)
               : f->rax == SYS_symlink ? vfs_symlink(p->cwd, first, second)
                                       : vfs_rename(p->cwd, first, second);
    }
    case SYS_umask: {
        unsigned old = p->umask;
        p->umask = a & 0777;
        return old;
    }
    case SYS_getpid:
    case SYS_gettid:
        return p->pid;
    case SYS_getppid:
        return p->ppid;
    case SYS_getuid:
        return p->uid;
    case SYS_geteuid:
        return p->euid;
    case SYS_getgid:
        return p->gid;
    case SYS_getegid:
        return p->egid;
    case SYS_setuid:
        if ((uint32_t)a == UINT32_MAX)
            return -EINVAL;
        if (p->euid && a != p->uid && a != p->suid)
            return -EPERM;
        if (!p->euid)
            p->uid = p->suid = a;
        p->euid = a;
        return 0;
    case SYS_setgid:
        if ((uint32_t)a == UINT32_MAX)
            return -EINVAL;
        if (p->euid && a != p->gid && a != p->sgid)
            return -EPERM;
        if (!p->euid)
            p->gid = p->sgid = a;
        p->egid = a;
        return 0;
    case SYS_setresuid: {
        uint32_t values[3] = {a, b, c};
        for (unsigned i = 0; i < 3; i++)
            if (values[i] != UINT32_MAX && p->euid && values[i] != p->uid && values[i] != p->euid &&
                values[i] != p->suid)
                return -EPERM;
        if (values[0] != UINT32_MAX)
            p->uid = values[0];
        if (values[1] != UINT32_MAX)
            p->euid = values[1];
        if (values[2] != UINT32_MAX)
            p->suid = values[2];
        return 0;
    }
    case SYS_setresgid: {
        uint32_t values[3] = {a, b, c};
        for (unsigned i = 0; i < 3; i++)
            if (values[i] != UINT32_MAX && p->euid && values[i] != p->gid && values[i] != p->egid &&
                values[i] != p->sgid)
                return -EPERM;
        if (values[0] != UINT32_MAX)
            p->gid = values[0];
        if (values[1] != UINT32_MAX)
            p->egid = values[1];
        if (values[2] != UINT32_MAX)
            p->sgid = values[2];
        return 0;
    }
    case SYS_getresgid:
        return copy_to_user(p->space, a, &p->gid, 4) && copy_to_user(p->space, b, &p->egid, 4) &&
                       copy_to_user(p->space, c, &p->sgid, 4)
                   ? 0
                   : -EFAULT;
    case SYS_getresuid:
        return copy_to_user(p->space, a, &p->uid, 4) && copy_to_user(p->space, b, &p->euid, 4) &&
                       copy_to_user(p->space, c, &p->suid, 4)
                   ? 0
                   : -EFAULT;
    case SYS_getgroups:
        if (!a)
            return 1;
        if (a < 1)
            return -EINVAL;
        return copy_to_user(p->space, b, &p->gid, 4) ? 1 : -EFAULT;
    case SYS_getpgrp:
        return p->pgrp;
    case SYS_getpgid:
    case SYS_getsid: {
        struct process *target = a ? process_find(a) : p;
        if (!target)
            return -ESRCH;
        return f->rax == SYS_getsid ? target->sid : target->pgrp;
    }
    case SYS_setsid:
        if (p->pgrp == p->pid)
            return -EPERM;
        p->sid = p->pgrp = p->pid;
        return p->sid;
    case SYS_setpgid: {
        struct process *target = a ? process_find(a) : p;
        if (!target)
            return -ESRCH;
        if (target != p && target->ppid != p->pid)
            return -ESRCH;
        if (target->sid != p->sid || target->pid == target->sid)
            return -EPERM;
        int group = b ? b : target->pid;
        if (group < 0)
            return -EINVAL;
        if (group != target->pid) {
            bool exists = false;
            for (struct process *t = process_list(); t; t = t->next)
                if (t->sid == p->sid && t->pgrp == group)
                    exists = true;
            if (!exists)
                return -EPERM;
        }
        target->pgrp = group;
        return 0;
    }
    case SYS_fork:
        return process_fork(f);
    case SYS_execve:
        return exec_user(a, b, c, f);
    case SYS_wait4:
        return process_wait((int)a, b, c, d);
    case SYS_exit:
    case SYS_exit_group:
        process_exit(a, false);
        return 0;
    case SYS_yield:
        return 0;
    case SYS_kill:
        return process_kill((int)a, b);
    case SYS_rt_sigaction: {
        if (a < 1 || a >= NSIG || a == SIGKILL || a == SIGSTOP || d != 8)
            return -EINVAL;
        struct abi_sigaction action;
        if (b && !copy_from_user(p->space, &action, b, sizeof(action)))
            return -EFAULT;
        if (c && !copy_to_user(p->space, c, &p->actions[a], sizeof(action)))
            return -EFAULT;
        if (b) {
            if (action.flags &
                ~(SA_NOCLDSTOP | SA_RESTART | SA_NODEFER | SA_RESETHAND | SA_RESTORER))
                return -EINVAL;
            action.mask &= ~((1ull << (SIGKILL - 1)) | (1ull << (SIGSTOP - 1)));
            p->actions[a] = action;
        }
        return 0;
    }
    case SYS_rt_sigprocmask: {
        if (d != 8)
            return -EINVAL;
        uint64_t mask;
        if (b && !copy_from_user(p->space, &mask, b, 8))
            return -EFAULT;
        if (c && !copy_to_user(p->space, c, &p->blocked, 8))
            return -EFAULT;
        if (b) {
            if (a == SIG_BLOCK)
                p->blocked |= mask;
            else if (a == SIG_UNBLOCK)
                p->blocked &= ~mask;
            else if (a == SIG_SETMASK)
                p->blocked = mask;
            else
                return -EINVAL;
            p->blocked &= ~((1ull << (SIGKILL - 1)) | (1ull << (SIGSTOP - 1)));
        }
        return 0;
    }
    case SYS_sigpending:
        if (b != 8)
            return -EINVAL;
        return copy_to_user(p->space, a, &p->pending, 8) ? 0 : -EFAULT;
    case SYS_rt_sigreturn:
        return process_sigreturn(f);
    case SYS_arch_prctl:
        if (a == 0x1002) {
            if (b >= USER_TOP)
                return -EPERM;
            p->fs_base = b;
            wrmsr(0xc0000100, b);
            return 0;
        }
        if (a == 0x1003)
            return copy_to_user(p->space, b, &p->fs_base, 8) ? 0 : -EFAULT;
        return -EINVAL;
    case SYS_set_tid_address:
        p->clear_tid = a;
        return p->pid;
    case SYS_nanosleep: {
        struct abi_timespec request;
        if (!copy_from_user(p->space, &request, a, sizeof(request)))
            return -EFAULT;
        if (request.tv_sec < 0 || request.tv_nsec < 0 || request.tv_nsec >= 1000000000)
            return -EINVAL;
        if (!request.tv_sec && !request.tv_nsec)
            return 0;
        uint64_t now = hal_counter_ns();
        if ((uint64_t)request.tv_sec >
            (UINT64_MAX - now - (uint64_t)request.tv_nsec) / 1000000000ull)
            return -EINVAL;
        if (b && !user_range(p->space, b, sizeof(struct abi_timespec), true))
            return -EFAULT;
        p->sleep_remaining = b;
        p->wake_deadline_ns = now + (uint64_t)request.tv_sec * 1000000000ull + request.tv_nsec;
        return process_block(BLOCK_SLEEP);
    }
    case SYS_clock_gettime: {
        if (a != 0 && a != 1 && a != 4)
            return -EINVAL;
        uint64_t ms = a ? hal_time_ms() : hal_realtime_ms();
        if (!ms && !a)
            return -EIO;
        struct abi_timespec time = {ms / 1000, (ms % 1000) * 1000000};
        return copy_to_user(p->space, b, &time, sizeof(time)) ? 0 : -EFAULT;
    }
    case SYS_uname: {
        struct abi_utsname names = {.sysname = "Aster",
                                    .nodename = "aster",
                                    .release = "0.1",
                                    .version = "x86_64",
                                    .machine = "x86_64",
                                    .domainname = "local"};
        return copy_to_user(p->space, a, &names, sizeof(names)) ? 0 : -EFAULT;
    }
    default:
        return -ENOSYS;
    }
}
struct x64_frame *syscall_dispatch(struct x64_frame *frame) {
    struct process *p = process_current();
    p->frame = frame;
    uint64_t number = frame->rax;
    p->last_syscall = number;
    uint64_t started = hal_counter_ns();
    long result = sys_dispatch(frame);
    p->system_ns += hal_counter_ns() - started;
    if (result == SYSCALL_BLOCK) {
        if (p->block_reason == BLOCK_SLEEP)
            frame->rax = 0;
        else {
            frame->rip -= 2;
            frame->rax = number;
        }
        return process_schedule(frame, true);
    }
    if (number != SYS_execve || result < 0)
        frame->rax = result;
    return process_schedule(frame, number == SYS_yield || p->state != PROCESS_RUNNABLE);
}
