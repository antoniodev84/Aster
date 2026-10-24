#include "vfs.h"
#include "mm/mm.h"
#include "kstring.h"
#include "console/kprintf.h"
#include "proc/process.h"

struct ext2_inode {
    uint16_t mode, uid;
    uint32_t size, atime, ctime, mtime, dtime;
    uint16_t gid, links;
    uint32_t blocks, flags, osd1, block[15], generation, file_acl, size_high, faddr;
    uint8_t osd2[12];
} __attribute__((packed));
struct ext2_group {
    uint32_t block_bitmap, inode_bitmap, inode_table;
    uint16_t free_blocks, free_inodes, used_dirs, pad;
    uint32_t reserved[3];
} __attribute__((packed));
struct ext2_dir {
    uint32_t inode;
    uint16_t length;
    uint8_t name_length, type;
    char name[];
} __attribute__((packed));
struct ext2 {
    struct block_device *device;
    uint64_t start;
    uint8_t super[1024];
    uint32_t block_size, blocks, inodes, blocks_per_group, inodes_per_group, first_data,
        first_inode, groups;
    uint16_t inode_size;
    struct ext2_group *descriptors;
    struct vnode *cache;
};
static struct vfs_ops ext2_ops;
_Static_assert(sizeof(struct ext2_inode) == 128, "ext2 inode layout");
_Static_assert(sizeof(struct ext2_group) == 32, "ext2 group layout");
static uint16_t r16(const uint8_t *p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}
static uint32_t r32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
static void w16(uint8_t *p, uint16_t value) {
    memcpy(p, &value, sizeof(value));
}
static void w32(uint8_t *p, uint32_t v) {
    memcpy(p, &v, 4);
}
static long bread(struct ext2 *fs, uint32_t block, void *buffer) {
    if (block >= fs->blocks)
        return -EIO;
    return block_read(fs->device, fs->start + (uint64_t)block * (fs->block_size / 512),
                      fs->block_size / 512, buffer)
               ? 0
               : -EIO;
}
static long bwrite(struct ext2 *fs, uint32_t block, const void *buffer) {
    if (block >= fs->blocks)
        return -EIO;
    return block_write(fs->device, fs->start + (uint64_t)block * (fs->block_size / 512),
                       fs->block_size / 512, buffer)
               ? 0
               : -EIO;
}
static long metadata_sync(struct ext2 *fs) {
    if (!block_write(fs->device, fs->start + 2, 2, fs->super))
        return -EIO;
    size_t bytes = (size_t)fs->groups * sizeof(struct ext2_group),
           blocks = (bytes + fs->block_size - 1) / fs->block_size;
    uint8_t *buffer = kmalloc(fs->block_size);
    if (!buffer)
        return -ENOMEM;
    for (size_t i = 0; i < blocks; i++) {
        memset(buffer, 0, fs->block_size);
        size_t chunk = bytes - i * fs->block_size;
        if (chunk > fs->block_size)
            chunk = fs->block_size;
        memcpy(buffer, (uint8_t *)fs->descriptors + i * fs->block_size, chunk);
        long e = bwrite(fs, fs->first_data + 1 + i, buffer);
        if (e < 0) {
            kfree(buffer);
            return e;
        }
    }
    kfree(buffer);
    return fs->device->flush(fs->device) ? 0 : -EIO;
}
static long inode_io(struct ext2 *fs, uint32_t number, struct ext2_inode *inode, bool write) {
    if (!number || number > fs->inodes)
        return -EIO;
    uint32_t group = (number - 1) / fs->inodes_per_group,
             index = (number - 1) % fs->inodes_per_group;
    if (group >= fs->groups)
        return -EIO;
    uint64_t offset = (uint64_t)index * fs->inode_size;
    uint32_t block = fs->descriptors[group].inode_table + offset / fs->block_size;
    size_t within = offset % fs->block_size;
    if (within + sizeof(*inode) > fs->block_size)
        return -EIO;
    uint8_t *data = kmalloc(fs->block_size);
    if (!data)
        return -ENOMEM;
    long e = bread(fs, block, data);
    if (e >= 0) {
        if (write) {
            memcpy(data + within, inode, sizeof(*inode));
            e = bwrite(fs, block, data);
        } else
            memcpy(inode, data + within, sizeof(*inode));
    }
    kfree(data);
    return e;
}
static long bitmap_allocate(struct ext2 *fs, bool inode, uint32_t *out) {
    uint8_t *bits = kmalloc(fs->block_size);
    if (!bits)
        return -ENOMEM;
    for (uint32_t group = 0; group < fs->groups; group++) {
        struct ext2_group *g = &fs->descriptors[group];
        if (inode ? !g->free_inodes : !g->free_blocks)
            continue;
        uint32_t block = inode ? g->inode_bitmap : g->block_bitmap;
        long e = bread(fs, block, bits);
        if (e < 0) {
            kfree(bits);
            return e;
        }
        uint32_t total = inode ? fs->inodes_per_group : fs->blocks_per_group;
        for (uint32_t i = 0; i < total; i++) {
            uint32_t number = inode ? group * fs->inodes_per_group + i + 1
                                    : fs->first_data + group * fs->blocks_per_group + i;
            if (inode ? (number < fs->first_inode || number > fs->inodes) : number >= fs->blocks)
                continue;
            if (bits[i / 8] & (1u << (i % 8)))
                continue;
            bits[i / 8] |= 1u << (i % 8);
            e = bwrite(fs, block, bits);
            if (e < 0) {
                kfree(bits);
                return e;
            }
            if (inode) {
                g->free_inodes--;
                w32(fs->super + 16, r32(fs->super + 16) - 1);
            } else {
                g->free_blocks--;
                w32(fs->super + 12, r32(fs->super + 12) - 1);
                memset(bits, 0, fs->block_size);
                e = bwrite(fs, number, bits);
                if (e < 0) {
                    kfree(bits);
                    return e;
                }
            }
            *out = number;
            kfree(bits);
            return 0;
        }
    }
    kfree(bits);
    return -ENOSPC;
}
static long bitmap_free(struct ext2 *fs, bool inode, uint32_t number) {
    uint32_t index = inode ? number - 1 : number - fs->first_data,
             per = inode ? fs->inodes_per_group : fs->blocks_per_group, group = index / per;
    index %= per;
    if (group >= fs->groups)
        return -EIO;
    struct ext2_group *g = &fs->descriptors[group];
    uint8_t *bits = kmalloc(fs->block_size);
    if (!bits)
        return -ENOMEM;
    uint32_t block = inode ? g->inode_bitmap : g->block_bitmap;
    long e = bread(fs, block, bits);
    if (e >= 0) {
        if (!(bits[index / 8] & (1u << (index % 8))))
            e = -EIO;
        else {
            bits[index / 8] &= ~(1u << (index % 8));
            e = bwrite(fs, block, bits);
            if (e >= 0) {
                if (inode) {
                    g->free_inodes++;
                    w32(fs->super + 16, r32(fs->super + 16) + 1);
                } else {
                    g->free_blocks++;
                    w32(fs->super + 12, r32(fs->super + 12) + 1);
                }
            }
        }
    }
    kfree(bits);
    return e;
}
static long data_block(struct ext2 *fs, struct ext2_inode *inode, uint64_t logical, bool create,
                       uint32_t *out) {
    unsigned depth;
    uint64_t index = logical, per = fs->block_size / 4, span;
    if (logical < 12) {
        uint32_t block = inode->block[logical];
        if (!block && create) {
            long e = bitmap_allocate(fs, false, &block);
            if (e < 0)
                return e;
            inode->block[logical] = block;
            inode->blocks += fs->block_size / 512;
        }
        *out = block;
        return 0;
    }
    index -= 12;
    unsigned root = 12;
    span = per;
    for (depth = 1; depth <= 3; depth++, root++) {
        if (index < span)
            break;
        index -= span;
        span *= per;
    }
    if (depth > 3)
        return -EFBIG;
    uint32_t block = inode->block[root];
    if (!block) {
        if (!create) {
            *out = 0;
            return 0;
        }
        long e = bitmap_allocate(fs, false, &block);
        if (e < 0)
            return e;
        inode->block[root] = block;
        inode->blocks += fs->block_size / 512;
    }
    uint32_t *table = kmalloc(fs->block_size);
    if (!table)
        return -ENOMEM;
    while (depth) {
        long e = bread(fs, block, table);
        if (e < 0) {
            kfree(table);
            return e;
        }
        span /= per;
        uint32_t slot = index / span;
        index %= span;
        uint32_t next = table[slot];
        if (!next && create) {
            e = bitmap_allocate(fs, false, &next);
            if (e < 0) {
                kfree(table);
                return e;
            }
            table[slot] = next;
            inode->blocks += fs->block_size / 512;
            e = bwrite(fs, block, table);
            if (e < 0) {
                kfree(table);
                return e;
            }
        }
        if (!next) {
            *out = 0;
            kfree(table);
            return 0;
        }
        block = next;
        depth--;
    }
    *out = block;
    kfree(table);
    return 0;
}
static long inode_read_data(struct ext2 *fs, struct ext2_inode *inode, uint64_t offset,
                            void *buffer, size_t count) {
    uint64_t size = inode->size;
    if (S_ISREG(inode->mode))
        size |= (uint64_t)inode->size_high << 32;
    if (offset >= size)
        return 0;
    if (count > size - offset)
        count = size - offset;
    if (S_ISLNK(inode->mode) && !inode->blocks && size <= 60) {
        memcpy(buffer, (uint8_t *)inode->block + offset, count);
        return count;
    }
    uint8_t *data = kmalloc(fs->block_size);
    if (!data)
        return -ENOMEM;
    size_t done = 0;
    while (done < count) {
        uint32_t block;
        long e = data_block(fs, inode, (offset + done) / fs->block_size, false, &block);
        if (e < 0) {
            kfree(data);
            return done ? (long)done : e;
        }
        size_t within = (offset + done) % fs->block_size, chunk = fs->block_size - within;
        if (chunk > count - done)
            chunk = count - done;
        if (block) {
            e = bread(fs, block, data);
            if (e < 0) {
                kfree(data);
                return done ? (long)done : e;
            }
            memcpy((uint8_t *)buffer + done, data + within, chunk);
        } else
            memset((uint8_t *)buffer + done, 0, chunk);
        done += chunk;
    }
    kfree(data);
    return done;
}
static long inode_write_data(struct ext2 *fs, uint32_t number, struct ext2_inode *inode,
                             uint64_t offset, const void *buffer, size_t count) {
    if (offset > UINT64_MAX - count)
        return -EFBIG;
    uint64_t end = offset + count;
    uint8_t *data = kmalloc(fs->block_size);
    if (!data)
        return -ENOMEM;
    size_t done = 0;
    if (S_ISLNK(inode->mode) && !inode->blocks && end <= 60) {
        if (offset > inode->size)
            memset((uint8_t *)inode->block + inode->size, 0, offset - inode->size);
        memcpy((uint8_t *)inode->block + offset, buffer, count);
        if (end > inode->size)
            inode->size = end;
        kfree(data);
        long e = inode_io(fs, number, inode, true);
        return e < 0 ? e : (long)count;
    }
    if (S_ISLNK(inode->mode) && !inode->blocks && inode->size) {
        kfree(data);
        return -ENOTSUP;
    }
    while (done < count) {
        uint32_t block;
        long e = data_block(fs, inode, (offset + done) / fs->block_size, true, &block);
        if (e < 0) {
            kfree(data);
            inode_io(fs, number, inode, true);
            return done ? (long)done : e;
        }
        size_t within = (offset + done) % fs->block_size, chunk = fs->block_size - within;
        if (chunk > count - done)
            chunk = count - done;
        e = bread(fs, block, data);
        if (e < 0) {
            kfree(data);
            return done ? (long)done : e;
        }
        memcpy(data + within, (const uint8_t *)buffer + done, chunk);
        e = bwrite(fs, block, data);
        if (e < 0) {
            kfree(data);
            return done ? (long)done : e;
        }
        done += chunk;
    }
    kfree(data);
    uint64_t old = inode->size;
    if (S_ISREG(inode->mode))
        old |= (uint64_t)inode->size_high << 32;
    if (end > old) {
        inode->size = end;
        if (S_ISREG(inode->mode))
            inode->size_high = end >> 32;
    }
    long e = inode_io(fs, number, inode, true);
    return e < 0 ? e : (long)done;
}
static struct vnode *get_node(struct ext2 *fs, uint32_t number) {
    for (struct vnode *n = fs->cache; n; n = n->cache_next)
        if (n->inode == number)
            return n;
    struct ext2_inode inode;
    if (inode_io(fs, number, &inode, false) < 0 || !inode.mode)
        return NULL;
    struct vnode *n = vnode_new(inode.mode);
    if (!n)
        return NULL;
    n->inode = number;
    n->mount_id = 16 + (size_t)(fs->device - block_devices);
    n->uid = inode.uid | (uint32_t)r16(inode.osd2 + 4) << 16;
    n->gid = inode.gid | (uint32_t)r16(inode.osd2 + 6) << 16;
    n->atime = inode.atime;
    n->mtime = inode.mtime;
    n->ctime = inode.ctime;
    n->nlink = inode.links;
    n->size = inode.size;
    if (S_ISREG(inode.mode))
        n->size |= (uint64_t)inode.size_high << 32;
    n->filesystem = fs;
    n->ops = &ext2_ops;
    n->cache_next = fs->cache;
    fs->cache = n;
    return n;
}
static long ext_read(struct vnode *n, uint64_t offset, void *buffer, size_t count) {
    struct ext2_inode inode;
    struct ext2 *fs = n->filesystem;
    long e = inode_io(fs, n->inode, &inode, false);
    return e < 0 ? e : inode_read_data(fs, &inode, offset, buffer, count);
}
static long ext_write(struct vnode *n, uint64_t offset, const void *buffer, size_t count) {
    struct ext2_inode inode;
    struct ext2 *fs = n->filesystem;
    long e = inode_io(fs, n->inode, &inode, false);
    if (e < 0)
        return e;
    n->mtime = n->ctime = hal_realtime_ms() / 1000;
    inode.mtime = n->mtime;
    inode.ctime = n->ctime;
    e = inode_write_data(fs, n->inode, &inode, offset, buffer, count);
    n->size = inode.size;
    if (S_ISREG(n->mode))
        n->size |= (uint64_t)inode.size_high << 32;
    if (e >= 0) {
        long sync = metadata_sync(fs);
        if (sync < 0)
            return sync;
    }
    return e;
}
static long ext_iterate(struct vnode *n, uint64_t *offset, char *name, uint64_t *ino,
                        unsigned *type) {
    struct ext2 *fs = n->filesystem;
    uint8_t *data = kmalloc(fs->block_size);
    if (!data)
        return -ENOMEM;
    while (*offset < n->size) {
        uint64_t base = *offset & ~((uint64_t)fs->block_size - 1);
        long e = ext_read(n, base, data, fs->block_size);
        if (e < 0) {
            kfree(data);
            return e;
        }
        size_t within = *offset - base;
        if (within + 8 > fs->block_size) {
            kfree(data);
            return -EIO;
        }
        struct ext2_dir *d = (void *)(data + within);
        if (d->length < 8 || (d->length & 3) || within + d->length > fs->block_size ||
            d->name_length > d->length - 8) {
            kfree(data);
            return -EIO;
        }
        *offset += d->length;
        if (!d->inode)
            continue;
        memcpy(name, d->name, d->name_length);
        name[d->name_length] = 0;
        *ino = d->inode;
        static const unsigned types[] = {0, 8, 4, 2, 6, 1, 12, 10};
        *type = d->type < 8 ? types[d->type] : 0;
        kfree(data);
        return 1;
    }
    kfree(data);
    return 0;
}
static long ext_lookup(struct vnode *n, const char *name, struct vnode **out) {
    uint64_t offset = 0, ino;
    unsigned type;
    char candidate[256];
    long e;
    while ((e = ext_iterate(n, &offset, candidate, &ino, &type)) > 0)
        if (!kstrcmp(name, candidate)) {
            *out = get_node(n->filesystem, ino);
            if (!*out)
                return -EIO;
            if (S_ISDIR((*out)->mode) && kstrcmp(name, ".") && kstrcmp(name, ".."))
                (*out)->parent = n;
            return 0;
        }
    return e < 0 ? e : -ENOENT;
}
static unsigned dir_type(uint32_t mode) {
    switch (mode & S_IFMT) {
    case S_IFREG:
        return 1;
    case S_IFDIR:
        return 2;
    case S_IFCHR:
        return 3;
    case S_IFBLK:
        return 4;
    case S_IFIFO:
        return 5;
    case S_IFLNK:
        return 7;
    default:
        return 0;
    }
}
static long dir_add(struct vnode *parent, const char *name, uint32_t number, uint32_t mode) {
    struct vnode *existing;
    long e = ext_lookup(parent, name, &existing);
    if (e >= 0)
        return -EEXIST;
    if (e != -ENOENT)
        return e;
    size_t length = kstrlen(name), needed = (8 + length + 3) & ~3ul;
    struct ext2 *fs = parent->filesystem;
    struct ext2_inode inode;
    e = inode_io(fs, parent->inode, &inode, false);
    if (e < 0)
        return e;
    if (inode.flags & 0x1000)
        return -ENOTSUP;
    uint8_t *data = kmalloc(fs->block_size);
    if (!data)
        return -ENOMEM;
    for (uint64_t off = 0; off <= parent->size; off += fs->block_size) {
        bool append = off == parent->size;
        if (append) {
            memset(data, 0, fs->block_size);
            ((struct ext2_dir *)data)->length = fs->block_size;
        } else {
            e = ext_read(parent, off, data, fs->block_size);
            if (e < 0) {
                kfree(data);
                return e;
            }
        }
        for (size_t pos = 0; pos < fs->block_size;) {
            struct ext2_dir *d = (void *)(data + pos);
            if (d->length < 8 || (d->length & 3) || pos + d->length > fs->block_size) {
                kfree(data);
                return -EIO;
            }
            size_t actual = (8 + d->name_length + 3) & ~3ul;
            size_t available = d->inode ? d->length - actual : d->length;
            if (available >= needed) {
                if (d->inode) {
                    uint16_t rest = d->length - actual;
                    d->length = actual;
                    d = (void *)((uint8_t *)d + actual);
                    memset(d, 0, rest);
                    d->length = rest;
                }
                d->inode = number;
                d->name_length = length;
                d->type = dir_type(mode);
                memcpy(d->name, name, length);
                e = inode_write_data(fs, parent->inode, &inode, off, data, fs->block_size);
                parent->size = inode.size;
                kfree(data);
                return e < 0 ? e : 0;
            }
            pos += d->length;
        }
    }
    kfree(data);
    return -ENOSPC;
}
static long dir_remove(struct vnode *parent, const char *name) {
    struct ext2 *fs = parent->filesystem;
    uint8_t *data = kmalloc(fs->block_size);
    if (!data)
        return -ENOMEM;
    for (uint64_t off = 0; off < parent->size; off += fs->block_size) {
        long e = ext_read(parent, off, data, fs->block_size);
        if (e < 0) {
            kfree(data);
            return e;
        }
        struct ext2_dir *previous = NULL;
        for (size_t pos = 0; pos < fs->block_size;) {
            struct ext2_dir *d = (void *)(data + pos);
            if (d->length < 8 || (d->length & 3) || pos + d->length > fs->block_size) {
                kfree(data);
                return -EIO;
            }
            if (d->inode && d->name_length == kstrlen(name) &&
                !memcmp(d->name, name, d->name_length)) {
                if (previous)
                    previous->length += d->length;
                else
                    d->inode = 0;
                e = ext_write(parent, off, data, fs->block_size);
                kfree(data);
                return e < 0 ? e : 0;
            }
            previous = d;
            pos += d->length;
        }
    }
    kfree(data);
    return -ENOENT;
}
static long ext_sync(struct vnode *n) {
    struct ext2 *fs = n->filesystem;
    struct ext2_inode inode;
    long e = inode_io(fs, n->inode, &inode, false);
    if (e < 0)
        return e;
    inode.mode = n->mode;
    inode.uid = n->uid;
    inode.gid = n->gid;
    w16(inode.osd2 + 4, n->uid >> 16);
    w16(inode.osd2 + 6, n->gid >> 16);
    inode.links = n->nlink;
    inode.atime = n->atime;
    inode.mtime = n->mtime;
    inode.ctime = n->ctime;
    e = inode_io(fs, n->inode, &inode, true);
    return e < 0 ? e : metadata_sync(fs);
}
static long ext_create(struct vnode *parent, const char *name, uint32_t mode, struct vnode **out) {
    if (S_ISDIR(mode) && parent->nlink == 65535)
        return -EMLINK;
    struct ext2 *fs = parent->filesystem;
    uint32_t number;
    long e = bitmap_allocate(fs, true, &number);
    if (e < 0)
        return e;
    struct process *task = process_current();
    struct ext2_inode inode = {.mode = mode,
                               .uid = task ? task->euid : 0,
                               .gid = task ? task->egid : 0,
                               .links = S_ISDIR(mode) ? 2 : 1,
                               .atime = hal_realtime_ms() / 1000,
                               .mtime = hal_realtime_ms() / 1000,
                               .ctime = hal_realtime_ms() / 1000};
    if (task) {
        w16(inode.osd2 + 4, task->euid >> 16);
        w16(inode.osd2 + 6, task->egid >> 16);
    }
    e = inode_io(fs, number, &inode, true);
    if (e < 0) {
        bitmap_free(fs, true, number);
        return e;
    }
    struct vnode *n = get_node(fs, number);
    if (!n) {
        bitmap_free(fs, true, number);
        return -ENOMEM;
    }
    n->parent = parent;
    if (S_ISDIR(mode)) {
        uint8_t *data = kmalloc(fs->block_size);
        if (!data) {
            n->nlink = 0;
            vnode_reclaim(n);
            return -ENOMEM;
        }
        struct ext2_dir *dot = (void *)data;
        dot->inode = number;
        dot->length = 12;
        dot->name_length = 1;
        dot->type = 2;
        dot->name[0] = '.';
        struct ext2_dir *up = (void *)(data + 12);
        up->inode = parent->inode;
        up->length = fs->block_size - 12;
        up->name_length = 2;
        up->type = 2;
        up->name[0] = up->name[1] = '.';
        e = inode_write_data(fs, number, &inode, 0, data, fs->block_size);
        kfree(data);
        if (e < 0) {
            n->nlink = 0;
            vnode_reclaim(n);
            return e;
        }
        n->size = inode.size;
    }
    e = dir_add(parent, name, number, mode);
    if (e < 0) {
        n->nlink = 0;
        vnode_reclaim(n);
        return e;
    }
    if (S_ISDIR(mode)) {
        parent->nlink++;
        fs->descriptors[(number - 1) / fs->inodes_per_group].used_dirs++;
        e = ext_sync(parent);
        if (e < 0)
            return e;
    }
    *out = n;
    return metadata_sync(fs);
}
static long free_tree(struct ext2 *fs, uint32_t block, unsigned depth) {
    if (!block)
        return 0;
    if (depth) {
        uint32_t *table = kmalloc(fs->block_size);
        if (!table)
            return -ENOMEM;
        long e = bread(fs, block, table);
        if (e < 0) {
            kfree(table);
            return e;
        }
        for (unsigned i = 0; i < fs->block_size / 4; i++)
            if (table[i]) {
                e = free_tree(fs, table[i], depth - 1);
                if (e < 0) {
                    kfree(table);
                    return e;
                }
            }
        kfree(table);
    }
    return bitmap_free(fs, false, block);
}
static long ext_truncate(struct vnode *n, uint64_t size) {
    struct ext2 *fs = n->filesystem;
    struct ext2_inode inode;
    long e = inode_io(fs, n->inode, &inode, false);
    if (e < 0)
        return e;
    if (size < n->size) {
        if (size) {
            uint8_t *data = kmalloc(fs->block_size);
            if (!data)
                return -ENOMEM;
            uint64_t last = (size + fs->block_size - 1) / fs->block_size,
                     total = (n->size + fs->block_size - 1) / fs->block_size;
            for (uint64_t b = last; b < total; b++) {
                uint32_t physical;
                e = data_block(fs, &inode, b, false, &physical);
                if (e < 0) {
                    kfree(data);
                    return e;
                }
                if (!physical)
                    continue;
                if (b < 12) {
                    inode.block[b] = 0;
                } else {
                    unsigned depth;
                    uint64_t index = b - 12, per = fs->block_size / 4, span = per;
                    unsigned root = 12;
                    for (depth = 1; depth <= 3; depth++, root++) {
                        if (index < span)
                            break;
                        index -= span;
                        span *= per;
                    }
                    uint32_t parent = inode.block[root];
                    for (unsigned level = depth; level; level--) {
                        e = bread(fs, parent, data);
                        if (e < 0) {
                            kfree(data);
                            return e;
                        }
                        span /= per;
                        uint32_t slot = index / span;
                        index %= span;
                        uint32_t next = ((uint32_t *)data)[slot];
                        if (level == 1) {
                            ((uint32_t *)data)[slot] = 0;
                            e = bwrite(fs, parent, data);
                            if (e < 0) {
                                kfree(data);
                                return e;
                            }
                        }
                        parent = next;
                    }
                }
                e = bitmap_free(fs, false, physical);
                if (e < 0) {
                    kfree(data);
                    return e;
                }
                inode.blocks -= fs->block_size / 512;
            }
            if (size % fs->block_size) {
                uint32_t physical;
                e = data_block(fs, &inode, size / fs->block_size, false, &physical);
                if (e < 0) {
                    kfree(data);
                    return e;
                }
                if (physical) {
                    e = bread(fs, physical, data);
                    if (e >= 0) {
                        memset(data + size % fs->block_size, 0,
                               fs->block_size - size % fs->block_size);
                        e = bwrite(fs, physical, data);
                    }
                    if (e < 0) {
                        kfree(data);
                        return e;
                    }
                }
            }
            kfree(data);
        } else {
            if (!(S_ISLNK(n->mode) && !inode.blocks))
                for (unsigned i = 0; i < 15; i++) {
                    e = free_tree(fs, inode.block[i], i < 12 ? 0 : i - 11);
                    if (e < 0)
                        return e;
                }
            memset(inode.block, 0, sizeof(inode.block));
            inode.blocks = 0;
        }
    }
    inode.size = size;
    inode.size_high = S_ISREG(n->mode) ? size >> 32 : 0;
    n->size = size;
    e = inode_io(fs, n->inode, &inode, true);
    return e < 0 ? e : metadata_sync(fs);
}
static long ext_remove(struct vnode *parent, const char *name, bool directory) {
    struct vnode *n;
    long e = ext_lookup(parent, name, &n);
    if (e < 0)
        return e;
    if (directory && !S_ISDIR(n->mode))
        return -ENOTDIR;
    if (!directory && S_ISDIR(n->mode))
        return -EISDIR;
    if (directory) {
        uint64_t offset = 0, ino;
        unsigned type;
        char child[256];
        while ((e = ext_iterate(n, &offset, child, &ino, &type)) > 0)
            if (kstrcmp(child, ".") && kstrcmp(child, ".."))
                return -ENOTEMPTY;
        if (e < 0)
            return e;
    }
    e = dir_remove(parent, name);
    if (e < 0)
        return e;
    if (directory) {
        parent->nlink--;
        n->nlink = 0;
        struct ext2 *fs = n->filesystem;
        fs->descriptors[(n->inode - 1) / fs->inodes_per_group].used_dirs--;
        e = ext_sync(parent);
        if (e < 0)
            return e;
    } else
        n->nlink--;
    e = ext_sync(n);
    if (e >= 0)
        vnode_reclaim(n);
    return e;
}
static long ext_link(struct vnode *parent, const char *name, struct vnode *n) {
    if (n->nlink >= 65535)
        return -EMLINK;
    long e = dir_add(parent, name, n->inode, n->mode);
    if (e < 0)
        return e;
    n->nlink++;
    return ext_sync(n);
}
static long ext_destroy(struct vnode *n) {
    struct ext2 *fs = n->filesystem;
    long e = ext_truncate(n, 0);
    if (e < 0)
        return e;
    struct ext2_inode inode;
    memset(&inode, 0, sizeof(inode));
    inode.dtime = hal_realtime_ms() / 1000;
    e = inode_io(fs, n->inode, &inode, true);
    if (e < 0)
        return e;
    e = bitmap_free(fs, true, n->inode);
    if (e < 0)
        return e;
    struct vnode **link = &fs->cache;
    while (*link && *link != n)
        link = &(*link)->cache_next;
    if (*link)
        *link = n->cache_next;
    return metadata_sync(fs);
}
static struct vfs_ops ext2_ops = {ext_lookup, ext_iterate, ext_read,     ext_write, ext_create,
                                  ext_remove, ext_link,    ext_truncate, ext_sync,  ext_destroy};
long ext2_mount(struct block_device *device, struct vnode **root) {
    uint8_t sector[512];
    if (!block_read(device, 0, 1, sector))
        return -EIO;
    uint64_t start = 0;
    uint8_t super[1024];
    if (!block_read(device, 2, 2, super))
        return -EIO;
    if (r16(super + 56) != 0xef53) {
        if (sector[510] != 0x55 || sector[511] != 0xaa)
            return -EINVAL;
        bool found = false;
        for (unsigned i = 0; i < 4; i++)
            if (sector[446 + i * 16 + 4] == 0x83) {
                start = r32(sector + 446 + i * 16 + 8);
                if (start + 4 < device->sectors && block_read(device, start + 2, 2, super) &&
                    r16(super + 56) == 0xef53) {
                    found = true;
                    break;
                }
            }
        if (!found)
            return -EINVAL;
    }
    if (r32(super + 24) > 2 || (r32(super + 96) & ~2u) || (r32(super + 100) & ~3u) ||
        r32(super + 76) > 1)
        return -ENOTSUP;
    struct ext2 *fs = kcalloc(1, sizeof(*fs));
    if (!fs)
        return -ENOMEM;
    fs->device = device;
    fs->start = start;
    memcpy(fs->super, super, 1024);
    fs->block_size = 1024u << r32(super + 24);
    fs->blocks = r32(super + 4);
    fs->inodes = r32(super);
    fs->first_data = r32(super + 20);
    fs->blocks_per_group = r32(super + 32);
    fs->inodes_per_group = r32(super + 40);
    fs->inode_size = r32(super + 76) ? r16(super + 88) : 128;
    fs->first_inode = r32(super + 76) ? r32(super + 84) : 11;
    if (!fs->blocks_per_group || !fs->inodes_per_group ||
        fs->blocks_per_group > fs->block_size * 8 || fs->inodes_per_group > fs->block_size * 8 ||
        fs->inode_size < 128 || fs->inode_size > fs->block_size ||
        fs->block_size % fs->inode_size || fs->blocks <= fs->first_data ||
        (uint64_t)fs->blocks * (fs->block_size / 512) > device->sectors - start) {
        kfree(fs);
        return -EINVAL;
    }
    fs->groups =
        ((uint64_t)fs->blocks - fs->first_data + fs->blocks_per_group - 1) / fs->blocks_per_group;
    if ((uint64_t)fs->groups * fs->inodes_per_group < fs->inodes) {
        kfree(fs);
        return -EINVAL;
    }
    size_t bytes = (size_t)fs->groups * 32, blocks = (bytes + fs->block_size - 1) / fs->block_size;
    fs->descriptors = kmalloc(blocks * fs->block_size);
    if (!fs->descriptors) {
        kfree(fs);
        return -ENOMEM;
    }
    for (size_t i = 0; i < blocks; i++)
        if (bread(fs, fs->first_data + 1 + i, (uint8_t *)fs->descriptors + i * fs->block_size) <
            0) {
            kfree(fs->descriptors);
            kfree(fs);
            return -EIO;
        }
    *root = get_node(fs, 2);
    if (!*root || !S_ISDIR((*root)->mode)) {
        kfree(fs->descriptors);
        kfree(fs);
        return -EINVAL;
    }
    kprintf("[ext2] %s start=%llu block=%u groups=%u mounted read-write\n", device->name,
            (unsigned long long)start, fs->block_size, fs->groups);
    return 0;
}
