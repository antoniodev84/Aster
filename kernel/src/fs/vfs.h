#ifndef VFS_H
#define VFS_H

#include <abi.h>
#include <stdbool.h>
#include "drivers/block.h"

#define PATH_MAX 4096
#define NAME_MAX 255
#define FD_MAX 128
#define PIPE_CAPACITY 4096

struct vnode;
struct vfs_ops {
    long (*lookup)(struct vnode *, const char *, struct vnode **);
    long (*iterate)(struct vnode *, uint64_t *, char *, uint64_t *, unsigned *);
    long (*read)(struct vnode *, uint64_t, void *, size_t);
    long (*write)(struct vnode *, uint64_t, const void *, size_t);
    long (*create)(struct vnode *, const char *, uint32_t, struct vnode **);
    long (*remove)(struct vnode *, const char *, bool);
    long (*link)(struct vnode *, const char *, struct vnode *);
    long (*truncate)(struct vnode *, uint64_t);
    long (*sync)(struct vnode *);
    long (*destroy)(struct vnode *);
};

struct dentry {
    char name[256];
    struct vnode *node;
    struct dentry *next;
};
struct file_page {
    uint64_t offset, physical;
    bool dirty;
    struct file_page *next;
};
struct vnode {
    uint64_t inode, size, mount_id;
    int64_t atime, mtime, ctime;
    uint32_t mode, uid, gid, nlink, references;
    struct vfs_ops *ops;
    void *filesystem;
    struct vnode *parent, *cache_next;
    struct dentry *children;
    uint8_t *data;
    size_t capacity;
    bool owns_data, read_only;
    unsigned device;
    uint64_t auxiliary;
    long (*generate)(struct vnode *, char *, size_t);
    struct file_page *pages;
};

struct pipe {
    uint8_t data[PIPE_CAPACITY];
    size_t head, tail, used;
    unsigned readers, writers;
};

struct net_socket;
struct open_file {
    struct net_socket *socket;
    unsigned references, flags;
    uint64_t offset;
    struct vnode *node;
    struct pipe *pipe;
    bool pipe_writer;
};

struct descriptor {
    struct open_file *file;
    unsigned flags;
};
extern struct vnode *vfs_root, *dev_root, *proc_root, *sys_root;
extern struct vfs_ops tmpfs_ops;
void vfs_init(const void *initrd, size_t size);
long vfs_resolve(struct vnode *base, const char *path, bool follow, struct vnode **out);
long vfs_parent(struct vnode *base, const char *path, struct vnode **parent, char *name);
long vfs_create(struct vnode *base, const char *path, uint32_t mode, struct vnode **out);
long vfs_create_follow(struct vnode *base, const char *path, uint32_t mode, struct vnode **out);
long vfs_remove(struct vnode *base, const char *path, bool directory);
long vfs_link(struct vnode *base, const char *source, const char *destination);
long vfs_symlink(struct vnode *base, const char *target, const char *path);
long vfs_rename(struct vnode *base, const char *oldpath, const char *newpath);
long vfs_read(struct vnode *node, uint64_t offset, void *buffer, size_t count);
long vfs_truncate(struct vnode *node, uint64_t size);
long vfs_write(struct vnode *node, uint64_t offset, const void *buffer, size_t count);
long vfs_stat(struct vnode *node, struct abi_stat *stat);
long vfs_access(struct vnode *node, unsigned mask);
long vfs_getcwd(struct vnode *node, char *buffer, size_t size);
struct vnode *vnode_new(uint32_t mode);
void vnode_retain(struct vnode *node);
void vnode_release(struct vnode *node);
void vnode_reclaim(struct vnode *node);
long tmpfs_insert(struct vnode *parent, const char *name, struct vnode *node);
struct open_file *file_new(struct vnode *node, unsigned flags);
void file_retain(struct open_file *file);
long file_release(struct open_file *file);
long file_read(struct open_file *file, void *buffer, size_t count);
long file_write(struct open_file *file, const void *buffer, size_t count);
long file_getdents(struct open_file *file, void *buffer, size_t count);
long ext2_mount(struct block_device *device, struct vnode **root);
void pseudo_init(void);
void procfs_add(int pid);
void procfs_remove(int pid);
long initrd_extract(const void *data, size_t size);
uint64_t file_cache_page(struct vnode *node, uint64_t offset);
long file_cache_flush(struct vnode *node);

#endif
