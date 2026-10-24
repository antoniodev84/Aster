#include "vfs.h"
#include "mm/mm.h"
#include "kstring.h"
#include "proc/process.h"
#include "console/kprintf.h"

struct vnode *vfs_root, *dev_root, *proc_root, *sys_root;
static uint64_t next_inode = 0x100000000ull;

struct vnode *vnode_new(uint32_t mode) {
    struct vnode *n = kcalloc(1, sizeof(*n));
    if (n) {
        *n = (struct vnode){.inode = next_inode++,
                            .mode = mode,
                            .nlink = S_ISDIR(mode) ? 2 : 1,
                            .references = 1,
                            .ops = &tmpfs_ops,
                            .atime = hal_realtime_ms() / 1000,
                            .mtime = hal_realtime_ms() / 1000,
                            .ctime = hal_realtime_ms() / 1000};
    }
    return n;
}
void vnode_retain(struct vnode *n) {
    if (n)
        n->references++;
}
void vnode_reclaim(struct vnode *n) {
    if (!n || n->references != 1 || n->nlink || n->children)
        return;
    if (n->ops->destroy) {
        long e = n->ops->destroy(n);
        if (e < 0) {
            kprintf("[vfs] inode reclamation failed: %ld\n", e);
            return;
        }
    }
    while (n->pages) {
        struct file_page *p = n->pages;
        n->pages = p->next;
        pmm_free(p->physical, 1);
        kfree(p);
    }
    if (n->owns_data)
        kfree(n->data);
    kfree(n);
}
void vnode_release(struct vnode *n) {
    if (!n)
        return;
    if (n->references > 1)
        n->references--;
    if (n->references == 1 && n->nlink) {
        while (n->pages) {
            struct file_page *p = n->pages;
            n->pages = p->next;
            pmm_free(p->physical, 1);
            kfree(p);
        }
    }
    vnode_reclaim(n);
}
static struct dentry *entry_find(struct vnode *n, const char *name) {
    for (struct dentry *e = n->children; e; e = e->next)
        if (!kstrcmp(e->name, name))
            return e;
    return NULL;
}
long tmpfs_insert(struct vnode *parent, const char *name, struct vnode *node) {
    if (entry_find(parent, name))
        return -EEXIST;
    struct dentry *e = kcalloc(1, sizeof(*e));
    if (!e)
        return -ENOMEM;
    kstrlcpy(e->name, name, sizeof(e->name));
    e->node = node;
    node->mount_id = parent->mount_id;
    e->next = parent->children;
    parent->children = e;
    if (S_ISDIR(node->mode)) {
        node->parent = parent;
        parent->nlink++;
    }
    return 0;
}
static long tmp_lookup(struct vnode *n, const char *name, struct vnode **out) {
    if (!kstrcmp(name, ".")) {
        *out = n;
        return 0;
    }
    if (!kstrcmp(name, "..")) {
        *out = n->parent ? n->parent : n;
        return 0;
    }
    struct dentry *e = entry_find(n, name);
    if (!e)
        return -ENOENT;
    *out = e->node;
    return 0;
}
static unsigned dtype(uint32_t mode) {
    return (mode & S_IFMT) >> 12;
}
static long tmp_iterate(struct vnode *n, uint64_t *offset, char *name, uint64_t *inode,
                        unsigned *type) {
    if (*offset < 2) {
        struct vnode *p = *offset ? n->parent : n;
        if (!p)
            p = n;
        kstrlcpy(name, *offset ? ".." : ".", 256);
        *inode = p->inode;
        *type = DT_DIR;
        (*offset)++;
        return 1;
    }
    uint64_t index = 2;
    for (struct dentry *e = n->children; e; e = e->next, index++)
        if (index == *offset) {
            kstrlcpy(name, e->name, 256);
            *inode = e->node->inode;
            *type = dtype(e->node->mode);
            (*offset)++;
            return 1;
        }
    return 0;
}
static long tmp_read(struct vnode *n, uint64_t offset, void *buffer, size_t count) {
    if (offset >= n->size)
        return 0;
    if (count > n->size - offset)
        count = n->size - offset;
    memcpy(buffer, n->data + offset, count);
    return count;
}
static long tmp_truncate(struct vnode *n, uint64_t length) {
    if (length > SIZE_MAX)
        return -EFBIG;
    if (length > n->capacity || !n->owns_data) {
        size_t capacity = length < 4096 ? 4096 : length;
        uint8_t *data = kmalloc(capacity);
        if (!data)
            return -ENOMEM;
        size_t old = n->size < length ? n->size : length;
        if (old)
            memcpy(data, n->data, old);
        if (n->owns_data)
            kfree(n->data);
        n->data = data;
        n->capacity = capacity;
        n->owns_data = true;
    }
    if (length > n->size)
        memset(n->data + n->size, 0, length - n->size);
    n->size = length;
    return 0;
}
static long tmp_write(struct vnode *n, uint64_t offset, const void *buffer, size_t count) {
    if (offset > SIZE_MAX - count)
        return -EFBIG;
    if (offset + count > n->size) {
        long e = tmp_truncate(n, offset + count);
        if (e < 0)
            return e;
    } else if (!n->owns_data) {
        long e = tmp_truncate(n, n->size);
        if (e < 0)
            return e;
    }
    memcpy(n->data + offset, buffer, count);
    return count;
}
static long tmp_create(struct vnode *p, const char *name, uint32_t mode, struct vnode **out) {
    struct vnode *n = vnode_new(mode);
    if (!n)
        return -ENOMEM;
    struct process *task = process_current();
    if (task) {
        n->uid = task->euid;
        n->gid = task->egid;
    }
    if (S_ISDIR(mode))
        n->nlink = 2;
    long e = tmpfs_insert(p, name, n);
    if (e < 0) {
        kfree(n);
        return e;
    }
    *out = n;
    return 0;
}
static long tmp_remove(struct vnode *p, const char *name, bool directory) {
    struct dentry **link = &p->children;
    while (*link && kstrcmp((*link)->name, name))
        link = &(*link)->next;
    if (!*link)
        return -ENOENT;
    struct vnode *n = (*link)->node;
    if (directory && !S_ISDIR(n->mode))
        return -ENOTDIR;
    if (!directory && S_ISDIR(n->mode))
        return -EISDIR;
    if (directory && n->children)
        return -ENOTEMPTY;
    if (directory) {
        n->nlink = 0;
        p->nlink--;
    } else
        n->nlink--;
    struct dentry *e = *link;
    *link = e->next;
    kfree(e);
    vnode_reclaim(n);
    return 0;
}
static long tmp_link(struct vnode *p, const char *name, struct vnode *n) {
    long e = tmpfs_insert(p, name, n);
    if (e >= 0)
        n->nlink++;
    return e;
}
static long tmp_sync(struct vnode *n) {
    (void)n;
    return 0;
}
struct vfs_ops tmpfs_ops = {tmp_lookup, tmp_iterate, tmp_read,     tmp_write, tmp_create,
                            tmp_remove, tmp_link,    tmp_truncate, tmp_sync,  NULL};

long vfs_access(struct vnode *node, unsigned mask) {
    if (node->read_only && (mask & 2))
        return -EROFS;
    struct process *p = process_current();
    uint32_t uid = p ? p->euid : 0, gid = p ? p->egid : 0;
    if (!uid) {
        if ((mask & 1) && !S_ISDIR(node->mode) && !(node->mode & 0111))
            return -EACCES;
        return 0;
    }
    unsigned bits = node->mode >> (uid == node->uid ? 6 : gid == node->gid ? 3 : 0);
    return (bits & mask) == mask ? 0 : -EACCES;
}
static long lookup(struct vnode *base, const char *name, struct vnode **out) {
    if (!S_ISDIR(base->mode))
        return -ENOTDIR;
    long e = vfs_access(base, 1);
    if (e < 0)
        return e;
    if (base == vfs_root) {
        if (dev_root && !kstrcmp(name, "dev")) {
            *out = dev_root;
            return 0;
        }
        if (proc_root && !kstrcmp(name, "proc")) {
            *out = proc_root;
            return 0;
        }
        if (sys_root && !kstrcmp(name, "sys")) {
            *out = sys_root;
            return 0;
        }
    }
    if (!kstrcmp(name, "..") && (base == dev_root || base == proc_root || base == sys_root)) {
        *out = vfs_root;
        return 0;
    }
    long result = base->ops->lookup(base, name, out);
    if (result >= 0 && base->read_only)
        (*out)->read_only = true;
    return result;
}
long vfs_resolve(struct vnode *base, const char *path, bool follow, struct vnode **out) {
    if (!path || !path[0])
        return -ENOENT;
    char *work = kmalloc(PATH_MAX), *next = kmalloc(PATH_MAX);
    if (!work || !next) {
        kfree(work);
        kfree(next);
        return -ENOMEM;
    }
    long result = 0;
    if (kstrlcpy(work, path, PATH_MAX) >= PATH_MAX) {
        result = -ENAMETOOLONG;
        goto end;
    }
    struct vnode *node = work[0] == '/' ? vfs_root : base;
    size_t cursor = 0;
    unsigned links = 0;
    while (work[cursor]) {
        while (work[cursor] == '/')
            cursor++;
        if (!work[cursor])
            break;
        char name[256];
        size_t length = 0;
        while (work[cursor] && work[cursor] != '/') {
            if (length == 255) {
                result = -ENAMETOOLONG;
                goto end;
            }
            name[length++] = work[cursor++];
        }
        name[length] = 0;
        bool trailing = work[cursor] == '/';
        size_t rest = cursor;
        while (work[rest] == '/')
            rest++;
        if (!kstrcmp(name, "."))
            continue;
        struct vnode *found;
        result = lookup(node, name, &found);
        if (result < 0)
            goto end;
        if (S_ISLNK(found->mode) && (follow || work[rest] || trailing)) {
            if (++links > 40) {
                result = -ELOOP;
                goto end;
            }
            long n = vfs_read(found, 0, next, PATH_MAX - 1);
            if (!n) {
                result = -ENOENT;
                goto end;
            }
            if (n < 0) {
                result = n;
                goto end;
            }
            next[n] = 0;
            if ((size_t)n + 1 + kstrlen(work + rest) >= PATH_MAX) {
                result = -ENAMETOOLONG;
                goto end;
            }
            if (work[rest]) {
                next[n++] = '/';
                kstrlcpy(next + n, work + rest, PATH_MAX - n);
            }
            if (next[0] == '/')
                node = vfs_root;
            char *swap = work;
            work = next;
            next = swap;
            cursor = 0;
            continue;
        }
        node = found;
        if (trailing && !S_ISDIR(node->mode)) {
            result = -ENOTDIR;
            goto end;
        }
    }
    *out = node;
    result = 0;
end:
    kfree(work);
    kfree(next);
    return result;
}
long vfs_parent(struct vnode *base, const char *path, struct vnode **parent, char *name) {
    size_t n = kstrlen(path);
    if (!n)
        return -ENOENT;
    if (n >= PATH_MAX)
        return -ENAMETOOLONG;
    char *copy = kmalloc(n + 1);
    if (!copy)
        return -ENOMEM;
    kstrlcpy(copy, path, n + 1);
    while (n > 1 && copy[n - 1] == '/')
        copy[--n] = 0;
    size_t split = n;
    while (split && copy[split - 1] != '/')
        split--;
    if (n - split > 255) {
        kfree(copy);
        return -ENAMETOOLONG;
    }
    kstrlcpy(name, copy + split, 256);
    if (!name[0] || !kstrcmp(name, ".") || !kstrcmp(name, "..")) {
        kfree(copy);
        return -EINVAL;
    }
    long e = 0;
    if (!split)
        *parent = base;
    else {
        copy[split] = 0;
        e = vfs_resolve(base, copy, true, parent);
    }
    if (e >= 0 && !S_ISDIR((*parent)->mode))
        e = -ENOTDIR;
    kfree(copy);
    return e;
}
long vfs_create(struct vnode *base, const char *path, uint32_t mode, struct vnode **out) {
    size_t length = kstrlen(path);
    if (length && path[length - 1] == '/' && !S_ISDIR(mode))
        return -ENOENT;
    char name[256];
    struct vnode *p;
    long e = vfs_parent(base, path, &p, name);
    if (e < 0)
        return e;
    if (!p->nlink)
        return -ENOENT;
    if ((e = vfs_access(p, 3)) < 0)
        return e;
    struct vnode *existing;
    e = lookup(p, name, &existing);
    if (e >= 0)
        return -EEXIST;
    if (e != -ENOENT)
        return e;
    return p->ops->create(p, name, mode, out);
}
long vfs_create_follow(struct vnode *base, const char *path, uint32_t mode, struct vnode **out) {
    char *work = kmalloc(PATH_MAX), *target = kmalloc(PATH_MAX);
    if (!work || !target) {
        kfree(work);
        kfree(target);
        return -ENOMEM;
    }
    long error = -ELOOP;
    if (kstrlcpy(work, path, PATH_MAX) >= PATH_MAX) {
        error = -ENAMETOOLONG;
        goto done;
    }
    for (unsigned links = 0; links <= 40; links++) {
        size_t length = kstrlen(work);
        if (length && work[length - 1] == '/') {
            error = -ENOENT;
            break;
        }
        struct vnode *parent, *node;
        char name[256];
        error = vfs_parent(base, work, &parent, name);
        if (error < 0)
            break;
        if (!parent->nlink) {
            error = -ENOENT;
            break;
        }
        if ((error = vfs_access(parent, 3)) < 0)
            break;
        error = lookup(parent, name, &node);
        if (error == -ENOENT) {
            error = parent->ops->create(parent, name, mode, out);
            break;
        }
        if (error < 0)
            break;
        if (!S_ISLNK(node->mode)) {
            error = -EEXIST;
            break;
        }
        if (links == 40) {
            error = -ELOOP;
            break;
        }
        long n = vfs_read(node, 0, target, PATH_MAX - 1);
        if (n <= 0) {
            error = n < 0 ? n : -ENOENT;
            break;
        }
        target[n] = 0;
        base = parent;
        char *swap = work;
        work = target;
        target = swap;
    }
done:
    kfree(work);
    kfree(target);
    return error;
}
long vfs_remove(struct vnode *base, const char *path, bool directory) {
    char name[256];
    struct vnode *p, *n;
    long e = vfs_parent(base, path, &p, name);
    if (e < 0)
        return e;
    if ((e = vfs_access(p, 3)) < 0)
        return e;
    if ((e = lookup(p, name, &n)) < 0)
        return e;
    size_t length = kstrlen(path);
    if (length && path[length - 1] == '/' && !S_ISDIR(n->mode))
        return -ENOTDIR;
    if (n == vfs_root || n == dev_root || n == proc_root || n == sys_root)
        return -EBUSY;
    struct process *task = process_current();
    if (task && task->euid && (p->mode & 01000) && task->euid != p->uid && task->euid != n->uid)
        return -EPERM;
    return p->ops->remove(p, name, directory);
}
long vfs_link(struct vnode *base, const char *source, const char *destination) {
    struct vnode *node, *parent;
    char name[256];
    long e = vfs_resolve(base, source, false, &node);
    if (e < 0)
        return e;
    if (S_ISDIR(node->mode))
        return -EPERM;
    if ((e = vfs_parent(base, destination, &parent, name)) < 0)
        return e;
    if (parent->filesystem != node->filesystem || parent->ops != node->ops ||
        parent->mount_id != node->mount_id)
        return -EXDEV;
    if (!parent->nlink)
        return -ENOENT;
    if ((e = vfs_access(parent, 3)) < 0)
        return e;
    return parent->ops->link(parent, name, node);
}
long vfs_symlink(struct vnode *base, const char *target, const char *path) {
    size_t len = kstrlen(target);
    if (!len)
        return -ENOENT;
    if (len >= PATH_MAX)
        return -ENAMETOOLONG;
    struct vnode *n;
    long e = vfs_create(base, path, S_IFLNK | 0777, &n);
    if (e < 0)
        return e;
    e = n->ops->write(n, 0, target, len);
    if (e < 0) {
        vfs_remove(base, path, false);
        return e;
    }
    return 0;
}
long vfs_rename(struct vnode *base, const char *oldpath, const char *newpath) {
    struct vnode *node;
    long e = vfs_resolve(base, oldpath, false, &node);
    if (e < 0)
        return e;
    if (S_ISDIR(node->mode))
        return -ENOTSUP;
    struct vnode *target;
    e = vfs_resolve(base, newpath, false, &target);
    if (e >= 0) {
        if (target == node)
            return 0;
        if (S_ISDIR(target->mode))
            return -EISDIR;
        if ((e = vfs_remove(base, newpath, false)) < 0)
            return e;
    } else if (e != -ENOENT)
        return e;
    e = vfs_link(base, oldpath, newpath);
    if (e < 0)
        return e;
    e = vfs_remove(base, oldpath, false);
    if (e < 0)
        vfs_remove(base, newpath, false);
    return e;
}
long vfs_read(struct vnode *n, uint64_t off, void *b, size_t count) {
    if (S_ISDIR(n->mode))
        return -EISDIR;
    if (n->generate) {
        char *text = kmalloc(16384);
        if (!text)
            return -ENOMEM;
        long len = n->generate(n, text, 16384);
        if (len < 0) {
            kfree(text);
            return len;
        }
        if (off >= (uint64_t)len)
            count = 0;
        else if (count > (uint64_t)len - off)
            count = len - off;
        if (count)
            memcpy(b, text + off, count);
        kfree(text);
        return count;
    }
    long result = n->ops->read(n, off, b, count);
    if (result > 0)
        for (struct file_page *p = n->pages; p; p = p->next) {
            uint64_t a = off > p->offset ? off : p->offset, z = off + result < p->offset + PAGE_SIZE
                                                                    ? off + result
                                                                    : p->offset + PAGE_SIZE;
            if (a < z)
                memcpy((uint8_t *)b + a - off,
                       (uint8_t *)physical_pointer(p->physical) + a - p->offset, z - a);
        }
    return result;
}
long vfs_write(struct vnode *n, uint64_t off, const void *b, size_t count) {
    if (n->read_only)
        return -EROFS;
    if (S_ISDIR(n->mode))
        return -EISDIR;
    if (n->generate)
        return -EROFS;
    n->mtime = n->ctime = hal_realtime_ms() / 1000;
    long result = n->ops->write(n, off, b, count);
    if (result > 0) {
        for (struct file_page *p = n->pages; p; p = p->next) {
            uint64_t a = off > p->offset ? off : p->offset, z = off + result < p->offset + PAGE_SIZE
                                                                    ? off + result
                                                                    : p->offset + PAGE_SIZE;
            if (a < z)
                memcpy((uint8_t *)physical_pointer(p->physical) + a - p->offset,
                       (const uint8_t *)b + a - off, z - a);
        }
        vmm_file_resize(n);
    }
    return result;
}
long vfs_stat(struct vnode *n, struct abi_stat *s) {
    memset(s, 0, sizeof(*s));
    s->st_dev = n->mount_id;
    s->st_ino = n->inode;
    s->st_nlink = n->nlink;
    s->st_mode = n->mode;
    s->st_uid = n->uid;
    s->st_gid = n->gid;
    s->st_size = n->size;
    s->st_blksize = 4096;
    s->st_blocks = (n->size + 511) / 512;
    s->st_rdev = n->device;
    s->st_atime = n->atime;
    s->st_mtime = n->mtime;
    s->st_ctime = n->ctime;
    return 0;
}
long vfs_getcwd(struct vnode *node, char *buffer, size_t size) {
    if (!size)
        return -ERANGE;
    char *path = kmalloc(PATH_MAX);
    if (!path)
        return -ENOMEM;
    size_t used = 0;
    path[0] = 0;
    for (unsigned depth = 0; node != vfs_root && depth < 256; depth++) {
        struct vnode *parent = node->parent;
        if (!parent) {
            long e = node->ops->lookup(node, "..", &parent);
            if (e < 0) {
                kfree(path);
                return e;
            }
        }
        uint64_t offset = 0, ino;
        unsigned type;
        char name[256];
        bool found = false;
        if (node == dev_root) {
            kstrlcpy(name, "dev", 256);
            found = true;
        } else if (node == proc_root) {
            kstrlcpy(name, "proc", 256);
            found = true;
        } else if (node == sys_root) {
            kstrlcpy(name, "sys", 256);
            found = true;
        } else
            while (parent->ops->iterate(parent, &offset, name, &ino, &type) > 0)
                if (ino == node->inode && kstrcmp(name, ".") && kstrcmp(name, "..")) {
                    found = true;
                    break;
                }
        if (!found || parent == node) {
            kfree(path);
            return -ENOENT;
        }
        size_t len = kstrlen(name);
        if (used + len + 1 >= PATH_MAX) {
            kfree(path);
            return -ENAMETOOLONG;
        }
        memmove(path + len + 1, path, used + 1);
        path[0] = '/';
        memcpy(path + 1, name, len);
        used += len + 1;
        node = parent;
    }
    if (!used) {
        path[0] = '/';
        path[1] = 0;
        used = 1;
    }
    if (used + 1 > size) {
        kfree(path);
        return -ERANGE;
    }
    memcpy(buffer, path, used + 1);
    kfree(path);
    return used + 1;
}
struct open_file *file_new(struct vnode *n, unsigned flags) {
    struct open_file *f = kcalloc(1, sizeof(*f));
    if (f) {
        f->references = 1;
        f->node = n;
        f->flags = flags;
        vnode_retain(n);
    }
    return f;
}
void file_retain(struct open_file *f) {
    if (f)
        f->references++;
}
long file_release(struct open_file *f) {
    if (!f || --f->references)
        return 0;
    long result = 0;
    if (f->pipe) {
        if (f->pipe_writer)
            f->pipe->writers--;
        else
            f->pipe->readers--;
        process_wake_io();
        if (!f->pipe->readers && !f->pipe->writers)
            kfree(f->pipe);
    }
    if (f->node) {
        result = file_cache_flush(f->node);
        vnode_release(f->node);
    }
    kfree(f);
    return result;
}
long file_read(struct open_file *f, void *b, size_t count) {
    if ((f->flags & 3) == O_WRONLY)
        return -EBADF;
    if (!count)
        return 0;
    if (f->pipe) {
        if (f->pipe_writer)
            return -EBADF;
        struct pipe *p = f->pipe;
        if (!p->used)
            return p->writers ? -EAGAIN : 0;
        if (count > p->used)
            count = p->used;
        for (size_t i = 0; i < count; i++) {
            ((uint8_t *)b)[i] = p->data[p->tail];
            p->tail = (p->tail + 1) % PIPE_CAPACITY;
        }
        p->used -= count;
        process_wake_io();
        return count;
    }
    long result = vfs_read(f->node, f->offset, b, count);
    if (result > 0)
        f->offset += result;
    return result;
}
long file_write(struct open_file *f, const void *b, size_t count) {
    if ((f->flags & 3) == O_RDONLY)
        return -EBADF;
    if (!count)
        return 0;
    if (f->pipe) {
        if (!f->pipe_writer)
            return -EBADF;
        struct pipe *p = f->pipe;
        if (!p->readers)
            return -EPIPE;
        if ((count <= PIPE_CAPACITY && count > PIPE_CAPACITY - p->used) || p->used == PIPE_CAPACITY)
            return -EAGAIN;
        if (count > PIPE_CAPACITY - p->used)
            count = PIPE_CAPACITY - p->used;
        for (size_t i = 0; i < count; i++) {
            p->data[p->head] = ((const uint8_t *)b)[i];
            p->head = (p->head + 1) % PIPE_CAPACITY;
        }
        p->used += count;
        process_wake_io();
        return count;
    }
    if (f->flags & O_APPEND)
        f->offset = f->node->size;
    long result = vfs_write(f->node, f->offset, b, count);
    if (result > 0)
        f->offset += result;
    return result;
}
long file_getdents(struct open_file *f, void *buffer, size_t count) {
    if (f->pipe || !S_ISDIR(f->node->mode))
        return -ENOTDIR;
    size_t used = 0;
    while (used < count) {
        char name[256];
        uint64_t ino, offset = f->offset;
        unsigned type;
        long e = f->node->ops->iterate(f->node, &offset, name, &ino, &type);
        if (e <= 0)
            return used ? (long)used : e;
        size_t length = (offsetof(struct abi_dirent, d_name) + kstrlen(name) + 1 + 7) & ~7ul;
        if (length > count - used)
            return used ? (long)used : -EINVAL;
        struct abi_dirent *d = (void *)((uint8_t *)buffer + used);
        memset(d, 0, length);
        d->d_ino = ino;
        d->d_off = offset;
        d->d_reclen = length;
        d->d_type = type;
        kstrlcpy(d->d_name, name, length - offsetof(struct abi_dirent, d_name));
        f->offset = offset;
        used += length;
    }
    return used;
}
uint64_t file_cache_page(struct vnode *n, uint64_t offset) {
    for (struct file_page *p = n->pages; p; p = p->next)
        if (p->offset == offset) {
            pmm_retain(p->physical);
            return p->physical;
        }
    struct file_page *p = kcalloc(1, sizeof(*p));
    if (!p)
        return 0;
    p->physical = pmm_alloc(1);
    if (!p->physical) {
        kfree(p);
        return 0;
    }
    long e = n->ops->read(n, offset, physical_pointer(p->physical), PAGE_SIZE);
    if (e < 0) {
        pmm_free(p->physical, 1);
        kfree(p);
        return 0;
    }
    p->offset = offset;
    p->next = n->pages;
    n->pages = p;
    pmm_retain(p->physical);
    return p->physical;
}
long file_cache_flush(struct vnode *n) {
    vmm_collect_dirty(n);
    for (struct file_page *p = n->pages; p; p = p->next) {
        if (!p->dirty || p->offset >= n->size)
            continue;
        size_t size = n->size - p->offset;
        if (size > PAGE_SIZE)
            size = PAGE_SIZE;
        n->mtime = n->ctime = hal_realtime_ms() / 1000;
        long e = n->ops->write(n, p->offset, physical_pointer(p->physical), size);
        if (e != (long)size)
            return e < 0 ? e : -EIO;
        p->dirty = false;
    }
    return n->ops->sync ? n->ops->sync(n) : 0;
}
void vfs_init(const void *initrd, size_t size) {
    vfs_root = vnode_new(S_IFDIR | 0755);
    if (!vfs_root)
        hal_panic("root allocation failed");
    vfs_root->parent = vfs_root;
    vfs_root->mount_id = 1;
    vfs_root->nlink = 2;
    for (size_t i = 0; i < block_device_count; i++) {
        struct vnode *root;
        long e = ext2_mount(&block_devices[i], &root);
        if (!e) {
            vfs_root = root;
            root->parent = root;
            break;
        }
    }
    if (initrd_extract(initrd, size) < 0)
        hal_panic("initrd extraction failed");
    pseudo_init();
    kprintf("[vfs] root=%s initrd loaded; /dev /proc /sys mounted\n",
            vfs_root->filesystem ? "ext2 rw" : "ramfs");
}

long vfs_truncate(struct vnode *n, uint64_t size) {
    if (n->read_only)
        return -EROFS;
    if (!n->ops->truncate)
        return -EINVAL;
    n->mtime = n->ctime = hal_realtime_ms() / 1000;
    long e = n->ops->truncate(n, size);
    if (e < 0)
        return e;
    for (struct file_page *p = n->pages; p; p = p->next) {
        if (p->offset >= size)
            memset(physical_pointer(p->physical), 0, PAGE_SIZE);
        else if (size - p->offset < PAGE_SIZE)
            memset((uint8_t *)physical_pointer(p->physical) + size - p->offset, 0,
                   PAGE_SIZE - (size - p->offset));
    }
    vmm_file_resize(n);
    return 0;
}
