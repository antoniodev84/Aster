#include "vfs.h"
#include "kstring.h"
#include "console/kprintf.h"

static bool octal(const char *s, size_t count, uint64_t *value) {
    uint64_t n = 0;
    size_t i = 0;
    while (i < count && (s[i] == ' ' || s[i] == 0))
        i++;
    for (; i < count && s[i] && s[i] != ' '; i++) {
        if (s[i] < '0' || s[i] > '7' || n > (UINT64_MAX - 7) / 8)
            return false;
        n = n * 8 + s[i] - '0';
    }
    *value = n;
    return true;
}

static bool same_content(struct vnode *n, const uint8_t *data, size_t length) {
    if (n->size != length)
        return false;
    size_t done = 0;
    uint8_t buffer[4096];
    while (done < length) {
        size_t chunk = length - done;
        if (chunk > sizeof(buffer))
            chunk = sizeof(buffer);
        long e = vfs_read(n, done, buffer, chunk);
        if (e != (long)chunk)
            return false;
        if (memcmp(buffer, data + done, chunk))
            return false;
        done += chunk;
    }
    return true;
}

long initrd_extract(const void *data, size_t size) {
    if (!data || size < 1024)
        return -EINVAL;
    const uint8_t *archive = data;
    size_t offset = 0;
    unsigned files = 0;
    while (offset + 512 <= size) {
        const uint8_t *h = archive + offset;
        bool zero = true;
        for (unsigned i = 0; i < 512; i++)
            if (h[i]) {
                zero = false;
                break;
            }
        if (zero)
            break;
        uint64_t checksum, length, mode, uid, gid;
        if (!octal((const char *)h + 148, 8, &checksum) ||
            !octal((const char *)h + 124, 12, &length) || !octal((const char *)h + 100, 8, &mode) ||
            !octal((const char *)h + 108, 8, &uid) || !octal((const char *)h + 116, 8, &gid))
            return -EINVAL;
        unsigned sum = 0;
        for (unsigned i = 0; i < 512; i++)
            sum += (i >= 148 && i < 156) ? ' ' : h[i];
        if (sum != checksum || length > size - offset - 512)
            return -EINVAL;
        char path[PATH_MAX] = {0}, name[101], prefix[156];
        memcpy(name, h, 100);
        name[100] = 0;
        memcpy(prefix, h + 345, 155);
        prefix[155] = 0;
        if (prefix[0]) {
            kappend(path, sizeof(path), prefix);
            kappend(path, sizeof(path), "/");
        }
        kappend(path, sizeof(path), name);
        char *relative = path;
        while (relative[0] == '.' && relative[1] == '/')
            relative += 2;
        if (relative[0] && kstrcmp(relative, ".")) {
            struct vnode *existing;
            long e = vfs_resolve(vfs_root, relative, false, &existing);
            if (e == -ENOENT) {
                char type = h[156];
                uint32_t kind = type == '5' ? S_IFDIR : type == '2' ? S_IFLNK : S_IFREG;
                if (type != '0' && type != 0 && type != '5' && type != '2' && type != '1')
                    return -ENOTSUP;
                if (type == '1') {
                    char target[101];
                    memcpy(target, h + 157, 100);
                    target[100] = 0;
                    e = vfs_link(vfs_root, target, relative);
                } else if (type == '2') {
                    char target[101];
                    memcpy(target, h + 157, 100);
                    target[100] = 0;
                    e = vfs_symlink(vfs_root, target, relative);
                } else {
                    struct vnode *n;
                    e = vfs_create(vfs_root, relative, kind | (mode & 07777), &n);
                    if (e >= 0) {
                        n->uid = uid;
                        n->gid = gid;
                        if (kind == S_IFREG && length) {
                            long written = vfs_write(n, 0, h + 512, length);
                            if (written != (long)length)
                                return written < 0 ? written : -EIO;
                        }
                        e = n->ops->sync(n);
                    }
                }
                if (e < 0)
                    return e;
                files++;
            } else if (e < 0)
                return e;
            else if (h[156] == '5') {
                if (!S_ISDIR(existing->mode))
                    return -ENOTDIR;
                uint32_t target_mode = S_IFDIR | (mode & 07777);
                if (existing->mode == target_mode && existing->uid == uid && existing->gid == gid)
                    goto next_entry;
                existing->mode = target_mode;
                existing->uid = uid;
                existing->gid = gid;
                e = existing->ops->sync(existing);
                if (e < 0)
                    return e;
            } else if ((h[156] == '0' || h[156] == 0) &&
                       (!kstrncmp(relative, "bin/", 4) || !kstrncmp(relative, "sbin/", 5) ||
                        !kstrncmp(relative, "usr/apps/bin/", 13) ||
                        !kstrncmp(relative, "usr/include/", 12) ||
                        !kstrncmp(relative, "usr/lib/", 8))) {
                if (!S_ISREG(existing->mode))
                    return -EINVAL;
                uint32_t target_mode = S_IFREG | (mode & 07777);
                bool data_same = same_content(existing, h + 512, length);
                bool meta_same =
                    existing->mode == target_mode && existing->uid == uid && existing->gid == gid;
                if (data_same && meta_same)
                    goto next_entry;
                if (!data_same) {
                    e = existing->ops->truncate(existing, 0);
                    if (e < 0)
                        return e;
                    if (length) {
                        long written = vfs_write(existing, 0, h + 512, length);
                        if (written != (long)length)
                            return written < 0 ? written : -EIO;
                    }
                }
                existing->mode = target_mode;
                existing->uid = uid;
                existing->gid = gid;
                e = existing->ops->sync(existing);
                if (e < 0)
                    return e;
            }
        }
    next_entry:
        uint64_t rounded = (length + 511) & ~511ull;
        if (rounded > SIZE_MAX - offset - 512)
            return -EINVAL;
        offset += 512 + rounded;
    }
    kprintf("[initrd] extracted %u new entries from %llu bytes\n", files, (unsigned long long)size);
    return 0;
}
