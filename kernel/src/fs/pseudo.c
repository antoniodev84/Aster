#include "vfs.h"
#include "console/framebuffer.h"
#include "drivers/ps2.h"
#include "mm/mm.h"
#include "proc/process.h"
#include "drivers/tty.h"
#include "drivers/pci.h"
#include "kstring.h"

static struct vfs_ops device_ops;
static struct vnode *make(struct vnode *p, const char *name, uint32_t mode) {
    struct vnode *n = vnode_new(mode);
    if (!n)
        return NULL;
    if (tmpfs_insert(p, name, n) < 0) {
        kfree(n);
        return NULL;
    }
    return n;
}
static long dev_read(struct vnode *n, uint64_t off, void *b, size_t count) {
    if (n->device == 4) {
        const struct framebuffer_info *f = framebuffer_get();
        if (!f)
            return -ENODEV;
        size_t size = f->pitch * f->height;
        if (off >= size)
            return 0;
        if (count > size - off)
            count = size - off;
        memcpy(b, (uint8_t *)f->address + off, count);
        return count;
    }
    if (n->device == 5) {
        if (!ps2_get_status().mouse)
            return -ENODEV;
        if (count < sizeof(struct abi_mouse_event))
            return -EINVAL;
        size_t done = 0;
        struct ps2_mouse_event e;
        while (count - done >= sizeof(struct abi_mouse_event) && ps2_read_mouse(&e)) {
            struct abi_mouse_event event = {e.dx, e.dy, e.buttons, 0};
            memcpy((uint8_t *)b + done, &event, sizeof(event));
            done += sizeof(event);
        }
        return done ? (long)done : -EAGAIN;
    }
    if (n->device == 6) {
        if (!ps2_get_status().keyboard)
            return -ENODEV;
        if (count < sizeof(struct abi_key_event))
            return -EINVAL;
        size_t done = 0;
        struct ps2_key_event e;
        while (count - done >= sizeof(struct abi_key_event) && ps2_read_key(&e)) {
            struct abi_key_event event = {e.code, e.pressed, e.character};
            memcpy((uint8_t *)b + done, &event, sizeof(event));
            done += sizeof(event);
        }
        return done ? (long)done : -EAGAIN;
    }
    if (n->device == 1)
        return tty_read(b, count);
    if (n->device == 2)
        return 0;
    if (n->device == 3) {
        memset(b, 0, count);
        return count;
    }
    if (n->device >= 16 && n->device < 16 + block_device_count) {
        struct block_device *disk = &block_devices[n->device - 16];
        if (off >= disk->sectors * 512)
            return 0;
        if (count > disk->sectors * 512 - off)
            count = disk->sectors * 512 - off;
        uint8_t block[512];
        size_t done = 0;
        while (done < count) {
            size_t within = (off + done) % 512, chunk = 512 - within;
            if (chunk > count - done)
                chunk = count - done;
            if (!block_read(disk, (off + done) / 512, 1, block))
                return done ? (long)done : -EIO;
            memcpy((uint8_t *)b + done, block + within, chunk);
            done += chunk;
        }
        return done;
    }
    return -ENODEV;
}
static long dev_write(struct vnode *n, uint64_t off, const void *b, size_t count) {
    if (n->device == 4) {
        const struct framebuffer_info *f = framebuffer_get();
        if (!f)
            return -ENODEV;
        size_t size = f->pitch * f->height;
        if (off >= size)
            return -ENOSPC;
        if (count > size - off)
            count = size - off;
        memcpy((uint8_t *)f->address + off, b, count);
        return count;
    }
    if (n->device == 1)
        return tty_write(b, count);
    if (n->device == 2 || n->device == 3)
        return count;
    if (n->device >= 16 && n->device < 16 + block_device_count) {
        struct block_device *disk = &block_devices[n->device - 16];
        if (off >= disk->sectors * 512)
            return -ENOSPC;
        if (count > disk->sectors * 512 - off)
            count = disk->sectors * 512 - off;
        uint8_t block[512];
        size_t done = 0;
        while (done < count) {
            size_t within = (off + done) % 512, chunk = 512 - within;
            if (chunk > count - done)
                chunk = count - done;
            if ((within || chunk != 512) && !block_read(disk, (off + done) / 512, 1, block))
                return done ? (long)done : -EIO;
            memcpy(block + within, (const uint8_t *)b + done, chunk);
            if (!block_write(disk, (off + done) / 512, 1, block))
                return done ? (long)done : -EIO;
            done += chunk;
        }
        if (!disk->flush(disk))
            return -EIO;
        return done;
    }
    return -ENODEV;
}
static long dev_sync(struct vnode *n) {
    if (n->device >= 16 && n->device < 16 + block_device_count)
        return block_devices[n->device - 16].flush(&block_devices[n->device - 16]) ? 0 : -EIO;
    return 0;
}
static struct vfs_ops device_ops = {.read = dev_read, .write = dev_write, .sync = dev_sync};
static long generated(struct vnode *n, char *buffer, size_t size) {
    buffer[0] = 0;
    switch (n->auxiliary) {
    case 1: {
        struct pmm_stats stats = pmm_get_stats();
        kappend(buffer, size, "MemTotal: ");
        knumber(buffer, size, stats.total * 4, 10);
        kappend(buffer, size, " kB\nMemFree: ");
        knumber(buffer, size, stats.free * 4, 10);
        kappend(buffer, size, " kB\n");
        break;
    }
    case 2:
        knumber(buffer, size, hal_time_ms() / 1000, 10);
        kappend(buffer, size, ".");
        knumber(buffer, size, hal_time_ms() % 1000, 10);
        kappend(buffer, size, "\n");
        break;
    case 3:
        kappend(buffer, size, "Aster kernel x86_64\n");
        break;
    case 4:
        kappend(buffer, size,
                vfs_root->filesystem ? "/dev/sda / ext2 rw 0 0\n" : "initrd / ramfs rw 0 0\n");
        kappend(buffer, size,
                "devfs /dev devfs rw 0 0\nprocfs /proc procfs ro 0 0\nsysfs /sys sysfs ro 0 0\n");
        break;
    default: {
        int pid = n->auxiliary >> 8;
        unsigned kind = n->auxiliary & 255;
        struct process *p = process_find(pid);
        if (!p)
            return -ENOENT;
        if (kind == 5) {
            kappend(buffer, size, "Name:\t");
            kappend(buffer, size, p->name);
            kappend(buffer, size, "\nPid:\t");
            knumber(buffer, size, p->pid, 10);
            kappend(buffer, size, "\nPPid:\t");
            knumber(buffer, size, p->ppid, 10);
            kappend(buffer, size, "\nState:\t");
            kappend(buffer, size,
                    p->state == PROCESS_RUNNABLE  ? "R"
                    : p->state == PROCESS_BLOCKED ? "S"
                    : p->state == PROCESS_STOPPED ? "T"
                                                  : "Z");
            kappend(buffer, size, "\nUid:\t");
            knumber(buffer, size, p->uid, 10);
            kappend(buffer, size, "\t");
            knumber(buffer, size, p->euid, 10);
            kappend(buffer, size, "\n");
        } else if (kind == 6) {
            kappend(buffer, size, p->executable);
        } else if (kind == 7) {
            kappend(buffer, size, p->executable);
        } else if (kind == 8) {
            char *path = kmalloc(PATH_MAX);
            if (!path)
                return -ENOMEM;
            long e = vfs_getcwd(p->cwd, path, PATH_MAX);
            if (e < 0) {
                kfree(path);
                return e;
            }
            kappend(buffer, size, path);
            kfree(path);
        }
        break;
    }
    }
    return kstrlen(buffer);
}
static struct vnode *genfile(struct vnode *parent, const char *name, uint64_t kind, uint32_t mode) {
    struct vnode *n = make(parent, name, mode);
    if (n) {
        n->generate = generated;
        n->auxiliary = kind;
    }
    return n;
}
static long self_generate(struct vnode *n, char *buffer, size_t size) {
    (void)n;
    buffer[0] = 0;
    struct process *p = process_current();
    if (!p)
        return -ENOENT;
    knumber(buffer, size, p->pid, 10);
    return kstrlen(buffer);
}
void procfs_add(int pid) {
    char name[32] = {0};
    knumber(name, sizeof(name), pid, 10);
    struct vnode *directory = make(proc_root, name, S_IFDIR | 0555);
    if (!directory)
        return;
    genfile(directory, "status", ((uint64_t)pid << 8) | 5, S_IFREG | 0444);
    genfile(directory, "cmdline", ((uint64_t)pid << 8) | 6, S_IFREG | 0444);
    genfile(directory, "exe", ((uint64_t)pid << 8) | 7, S_IFLNK | 0777);
    genfile(directory, "cwd", ((uint64_t)pid << 8) | 8, S_IFLNK | 0777);
}
void procfs_remove(int pid) {
    char name[32] = {0};
    knumber(name, sizeof(name), pid, 10);
    struct dentry **link = &proc_root->children;
    while (*link && kstrcmp((*link)->name, name))
        link = &(*link)->next;
    if (*link) {
        struct dentry *e = *link;
        *link = e->next;
        struct vnode *directory = e->node;
        while (directory->children) {
            struct dentry *child = directory->children;
            directory->children = child->next;
            child->node->nlink = 0;
            vnode_reclaim(child->node);
            kfree(child);
        }
        directory->nlink = 0;
        vnode_reclaim(directory);
        if (proc_root->nlink > 2)
            proc_root->nlink--;
        kfree(e);
    }
}
static void pci_sysfs(const struct pci_device *device, void *context) {
    struct vnode *parent = context;
    char name[40] = {0};
    knumber(name, sizeof(name), device->address.bus, 16);
    kappend(name, sizeof(name), ":");
    knumber(name, sizeof(name), device->address.slot, 16);
    kappend(name, sizeof(name), ".");
    knumber(name, sizeof(name), device->address.function, 16);
    struct vnode *dir = make(parent, name, S_IFDIR | 0555);
    if (!dir)
        return;
    const char *names[] = {"vendor", "device", "class"};
    uint32_t values[] = {device->vendor, device->device,
                         (uint32_t)device->class_code << 16 | (uint32_t)device->subclass << 8 |
                             device->interface};
    for (unsigned i = 0; i < 3; i++) {
        struct vnode *n = make(dir, names[i], S_IFREG | 0444);
        if (!n)
            continue;
        char text[32] = "0x";
        knumber(text, sizeof(text), values[i], 16);
        kappend(text, sizeof(text), "\n");
        n->ops->write(n, 0, text, kstrlen(text));
    }
}
void pseudo_init(void) {
    dev_root = vnode_new(S_IFDIR | 0755);
    proc_root = vnode_new(S_IFDIR | 0555);
    sys_root = vnode_new(S_IFDIR | 0555);
    if (!dev_root || !proc_root || !sys_root)
        hal_panic("pseudo filesystem allocation failed");
    dev_root->parent = proc_root->parent = sys_root->parent = vfs_root;
    dev_root->mount_id = 2;
    proc_root->mount_id = 3;
    sys_root->mount_id = 4;
    const char *names[] = {"console", "tty", "null", "zero", "fb0", "mouse0", "kbd0"};
    unsigned device_numbers[] = {1, 1, 2, 3, 4, 5, 6};
    for (unsigned i = 0; i < 7; i++) {
        struct vnode *n = make(dev_root, names[i], S_IFCHR | 0666);
        if (!n)
            hal_panic("device node allocation failed");
        n->device = device_numbers[i];
        if (n->device >= 4)
            n->mode = S_IFCHR | 0600;
        if (n->device == 4 && framebuffer_get())
            n->size = framebuffer_get()->pitch * framebuffer_get()->height;
        n->ops = &device_ops;
    }
    if (!make(dev_root, "shm", S_IFDIR | 01777))
        hal_panic("shared memory directory allocation failed");
    for (size_t i = 0; i < block_device_count; i++) {
        struct vnode *n = make(dev_root, block_devices[i].name, S_IFBLK | 0600);
        if (!n)
            continue;
        n->device = 16 + i;
        n->size = block_devices[i].sectors * 512;
        n->ops = &device_ops;
    }
    proc_root->read_only = sys_root->read_only = true;
    genfile(proc_root, "meminfo", 1, S_IFREG | 0444);
    genfile(proc_root, "uptime", 2, S_IFREG | 0444);
    genfile(proc_root, "version", 3, S_IFREG | 0444);
    genfile(proc_root, "mounts", 4, S_IFREG | 0444);
    struct vnode *self = make(proc_root, "self", S_IFLNK | 0777);
    if (self)
        self->generate = self_generate;
    struct vnode *bus = make(sys_root, "bus", S_IFDIR | 0555),
                 *pci = bus ? make(bus, "pci", S_IFDIR | 0555) : NULL,
                 *devices = pci ? make(pci, "devices", S_IFDIR | 0555) : NULL;
    if (devices)
        pci_enumerate(pci_sysfs, devices);
}
