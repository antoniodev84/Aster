#include "process.h"
#include "kstring.h"

struct elf_header {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct elf_segment {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
};

long elf_load(const char *path, char *const argv[], char *const envp[], struct address_space **out,
              uint64_t *entry, uint64_t *stack, uint64_t *heap) {
    struct vnode *file;
    long e = vfs_resolve(process_current() ? process_current()->cwd : vfs_root, path, true, &file);
    if (e < 0)
        return e;
    if (!S_ISREG(file->mode))
        return -EACCES;
    if ((e = vfs_access(file, 1)) < 0)
        return e;
    struct elf_header header;
    if (vfs_read(file, 0, &header, sizeof(header)) != sizeof(header))
        return -ENOEXEC;
    if (memcmp(header.ident, "\177ELF", 4) || header.ident[4] != 2 || header.ident[5] != 1 ||
        header.ident[6] != 1 || header.ehsize != sizeof(header) || header.type != 2 ||
        header.machine != 62 || header.version != 1 ||
        header.phentsize != sizeof(struct elf_segment) || !header.phnum || header.phnum > 128 ||
        header.phoff > file->size ||
        (uint64_t)header.phnum * sizeof(struct elf_segment) > file->size - header.phoff)
        return -ENOEXEC;
    struct address_space *space = vmm_create();
    if (!space)
        return -ENOMEM;
    uint64_t highest = 0;
    for (unsigned i = 0; i < header.phnum; i++) {
        struct elf_segment segment;
        if (vfs_read(file, header.phoff + (uint64_t)i * sizeof(segment), &segment,
                     sizeof(segment)) != sizeof(segment)) {
            e = -EIO;
            goto fail;
        }
        if (segment.type == 1 && segment.filesz > segment.memsz) {
            e = -ENOEXEC;
            goto fail;
        }
        if (segment.memsz && segment.align > 1 && ((segment.align & (segment.align - 1)) ||
                                  ((segment.vaddr - segment.offset) & (segment.align - 1)))) {
            e = -ENOEXEC;
            goto fail;
        }
        if (segment.type == 3) {
            e = -ENOEXEC;
            goto fail;
        }
        if (segment.type != 1 || !segment.memsz)
            continue;
        if (segment.filesz > segment.memsz || segment.vaddr < PAGE_SIZE ||
            segment.vaddr >= USER_TOP || segment.memsz > USER_TOP - segment.vaddr ||
            segment.offset > file->size || segment.filesz > file->size - segment.offset ||
            ((segment.offset ^ segment.vaddr) & 4095)) {
            e = -ENOEXEC;
            goto fail;
        }
        uint64_t first = segment.vaddr & ~4095ull,
                 end = (segment.vaddr + segment.memsz + 4095) & ~4095ull;
        unsigned flags =
            VM_USER | ((segment.flags & 2) ? VM_WRITE : 0) | ((segment.flags & 1) ? VM_EXEC : 0);
        if (!vmm_allocate(space, first, end - first, flags | VM_WRITE)) {
            e = -ENOMEM;
            goto fail;
        }
        size_t done = 0;
        while (done < segment.filesz) {
            uint64_t address = segment.vaddr + done;
            size_t chunk = PAGE_SIZE - (address & 4095);
            if (chunk > segment.filesz - done)
                chunk = segment.filesz - done;
            uint64_t physical = vmm_translate(space, address, true);
            if (!physical || vfs_read(file, segment.offset + done, physical_pointer(physical),
                                      chunk) != (long)chunk) {
                e = -EIO;
                goto fail;
            }
            done += chunk;
        }
        if (!vmm_protect(space, first, end - first, flags)) {
            e = -EIO;
            goto fail;
        }
        if (end > highest)
            highest = end;
    }
    bool executable = false;
    for (struct vm_page *p = space->pages; p; p = p->next)
        if ((p->flags & VM_EXEC) && header.entry >= p->address &&
            header.entry < p->address + PAGE_SIZE)
            executable = true;
    if (!executable) {
        e = -ENOEXEC;
        goto fail;
    }
    const uint64_t top = 0x7ffffff00000ull;
    const size_t stack_size = 256 * 1024;
    if (!vmm_allocate(space, top - stack_size, stack_size, VM_USER | VM_WRITE)) {
        e = -ENOMEM;
        goto fail;
    }
    uint64_t sp = top;
    uint64_t argv_p[128], env_p[128];
    size_t argc = 0, envc = 0, total = 0;
    while (argv && argv[argc]) {
        if (argc == 127) {
            e = -E2BIG;
            goto fail;
        }
        size_t len = kstrlen(argv[argc]) + 1;
        if (len > 32768 - total) {
            e = -E2BIG;
            goto fail;
        }
        total += len;
        sp -= len;
        if (!copy_to_user(space, sp, argv[argc], len)) {
            e = -EFAULT;
            goto fail;
        }
        argv_p[argc++] = sp;
    }
    while (envp && envp[envc]) {
        if (envc == 127) {
            e = -E2BIG;
            goto fail;
        }
        size_t len = kstrlen(envp[envc]) + 1;
        if (len > 32768 - total) {
            e = -E2BIG;
            goto fail;
        }
        total += len;
        sp -= len;
        if (!copy_to_user(space, sp, envp[envc], len)) {
            e = -EFAULT;
            goto fail;
        }
        env_p[envc++] = sp;
    }
    uint64_t auxiliary[] = {6,  PAGE_SIZE,
                            11, process_current() ? process_current()->uid : 0,
                            12, process_current() ? process_current()->euid : 0,
                            13, 0,
                            14, 0,
                            0,  0};
    size_t words = 1 + argc + 1 + envc + 1 + sizeof(auxiliary) / 8;
    sp = (sp - words * 8) & ~15ull;
    uint64_t cursor = sp, value = argc;
    copy_to_user(space, cursor, &value, 8);
    cursor += 8;
    for (size_t i = 0; i < argc; i++, cursor += 8)
        copy_to_user(space, cursor, &argv_p[i], 8);
    value = 0;
    copy_to_user(space, cursor, &value, 8);
    cursor += 8;
    for (size_t i = 0; i < envc; i++, cursor += 8)
        copy_to_user(space, cursor, &env_p[i], 8);
    copy_to_user(space, cursor, &value, 8);
    cursor += 8;
    copy_to_user(space, cursor, auxiliary, sizeof(auxiliary));
    *out = space;
    *entry = header.entry;
    *stack = sp;
    *heap = highest;
    return 0;
fail:
    vmm_destroy(space);
    return e;
}
