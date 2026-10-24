#include "mm.h"
#include "memory.h"
#include "fs/vfs.h"
#include "console/kprintf.h"
#include "proc/process.h"
#include "arch/x86_64/internal.h"

#define ADDRESS_MASK 0x000ffffffffff000ull
static uint64_t kernel_root;
static bool nx;

void vmm_init(void) {
    uint64_t cr4;
    __asm__ volatile("mov %%cr3, %0; mov %%cr4, %1" : "=r"(kernel_root), "=r"(cr4));
    kernel_root &= ADDRESS_MASK;
    if (cr4 & (1u << 12))
        hal_panic("userspace currently requires four-level paging");
    nx = (rdmsr(0xc0000080) & (1u << 11)) != 0;
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 1ull << 16;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

static uint64_t *pte(struct address_space *space, uint64_t address, bool create) {
    uint64_t *table = physical_pointer(space->root);
    for (int level = 3; level > 0; level--) {
        unsigned index = (address >> (12 + level * 9)) & 511;
        if (!(table[index] & 1)) {
            if (!create)
                return NULL;
            uint64_t next = pmm_alloc(1);
            if (!next)
                return NULL;
            table[index] = next | 7;
        }
        if (table[index] & 128)
            return NULL;
        table = physical_pointer(table[index] & ADDRESS_MASK);
    }
    return &table[(address >> 12) & 511];
}

struct address_space *vmm_create(void) {
    struct address_space *space = kcalloc(1, sizeof(*space));
    if (!space)
        return NULL;
    space->root = pmm_alloc(1);
    if (!space->root) {
        kfree(space);
        return NULL;
    }
    uint64_t *root = physical_pointer(space->root);
    uint64_t *kernel = physical_pointer(kernel_root);
    memcpy(root + 256, kernel + 256, 256 * sizeof(uint64_t));
    return space;
}

static uint64_t page_flags(unsigned flags) {
    if (flags & VM_INVALID)
        return 0;
    uint64_t result = 1;
    if (flags & VM_WRITE)
        result |= 2;
    if ((flags & VM_USER) && !(flags & VM_NONE))
        result |= 4;
    if (!(flags & VM_EXEC) && nx)
        result |= 1ull << 63;
    return result;
}

bool vmm_map(struct address_space *space, uint64_t address, uint64_t physical, unsigned flags) {
    if (!space || address < PAGE_SIZE || address >= USER_TOP ||
        ((address | physical) & (PAGE_SIZE - 1)))
        return false;
    for (struct vm_page *p = space->pages; p; p = p->next)
        if (p->address == address)
            return false;
    uint64_t *entry = pte(space, address, true);
    if (!entry || (*entry & 1))
        return false;
    struct vm_page *page = kcalloc(1, sizeof(*page));
    if (!page)
        return false;
    *page = (struct vm_page){
        .address = address, .physical = physical, .flags = flags, .next = space->pages};
    space->pages = page;
    *entry = physical | page_flags(flags);
    return true;
}

bool vmm_allocate(struct address_space *space, uint64_t address, size_t size, unsigned flags) {
    if (!size || address & (PAGE_SIZE - 1) || address >= USER_TOP || size > USER_TOP - address)
        return false;
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    size_t done = 0;
    for (; done < size; done += PAGE_SIZE) {
        uint64_t physical = pmm_alloc(1);
        if (!physical)
            break;
        if (!vmm_map(space, address + done, physical, flags)) {
            pmm_free(physical, 1);
            break;
        }
    }
    if (done == size)
        return true;
    vmm_unmap(space, address, done);
    return false;
}

static bool unmap_pages(struct address_space *space, uint64_t address, size_t size, bool flush) {
    if (!space || address & (PAGE_SIZE - 1) || address >= USER_TOP || size > USER_TOP - address)
        return false;
    uint64_t end = address + ((size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    if (flush)
        for (struct vm_page *page = space->pages; page; page = page->next)
            if (page->address >= address && page->address < end && page->file &&
                (page->flags & VM_SHARED))
                if (file_cache_flush(page->file) < 0)
                    return false;
    struct vm_page **link = &space->pages;
    while (*link) {
        struct vm_page *page = *link;
        if (page->address >= address && page->address < end) {
            uint64_t *entry = pte(space, page->address, false);
            if (entry)
                *entry = 0;
            __asm__ volatile("invlpg (%0)" : : "r"(page->address) : "memory");
            if (page->file)
                vnode_release(page->file);
            pmm_free(page->physical, 1);
            *link = page->next;
            kfree(page);
        } else
            link = &page->next;
    }
    return true;
}

bool vmm_unmap(struct address_space *space, uint64_t address, size_t size) {
    return unmap_pages(space, address, size, true);
}

static void free_tables(uint64_t physical, unsigned level) {
    uint64_t *table = physical_pointer(physical);
    if (level > 1)
        for (unsigned i = 0; i < 512; i++)
            if (table[i] & 1)
                free_tables(table[i] & ADDRESS_MASK, level - 1);
    pmm_free(physical, 1);
}

void vmm_destroy(struct address_space *space) {
    if (!space)
        return;
    for (struct vm_page *page = space->pages; page; page = page->next)
        if (page->file && (page->flags & VM_SHARED)) {
            uint64_t *entry = pte(space, page->address, false);
            if (entry && (*entry & 64))
                for (struct file_page *cache = page->file->pages; cache; cache = cache->next)
                    if (cache->physical == page->physical)
                        cache->dirty = true;
            long e = file_cache_flush(page->file);
            if (e < 0)
                kprintf("[vmm] exit flush failed: %ld\n", e);
        }
    unmap_pages(space, 0, USER_TOP, false);
    uint64_t *root = physical_pointer(space->root);
    for (unsigned i = 0; i < 256; i++)
        if (root[i] & 1)
            free_tables(root[i] & ADDRESS_MASK, 3);
    pmm_free(space->root, 1);
    kfree(space);
}

struct address_space *vmm_clone(struct address_space *source) {
    struct address_space *copy = vmm_create();
    if (!copy)
        return NULL;
    for (struct vm_page *page = source->pages; page; page = page->next) {
        uint64_t physical;
        if (page->flags & VM_SHARED) {
            physical = page->physical;
            pmm_retain(physical);
        } else {
            physical = pmm_alloc(1);
            if (physical)
                memcpy(physical_pointer(physical), physical_pointer(page->physical), PAGE_SIZE);
        }
        if (!physical || !vmm_map(copy, page->address, physical, page->flags)) {
            if (physical)
                pmm_free(physical, 1);
            vmm_destroy(copy);
            return NULL;
        }
        copy->pages->file = page->file;
        copy->pages->offset = page->offset;
        if (page->file)
            vnode_retain(page->file);
    }
    return copy;
}

bool vmm_protect(struct address_space *space, uint64_t address, size_t size, unsigned flags) {
    if (!space || address & (PAGE_SIZE - 1) || address >= USER_TOP || size > USER_TOP - address)
        return false;
    uint64_t end = address + ((size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    for (uint64_t a = address; a < end; a += PAGE_SIZE) {
        uint64_t *entry = pte(space, a, false);
        if (!entry)
            return false;
        bool mapped = false;
        for (struct vm_page *p = space->pages; p; p = p->next)
            if (p->address == a)
                mapped = true;
        if (!mapped)
            return false;
    }
    for (struct vm_page *page = space->pages; page; page = page->next) {
        if (page->address < address || page->address >= end)
            continue;
        if (page->file && (page->flags & VM_SHARED))
            vmm_collect_dirty(page->file);
        page->flags = (page->flags & (VM_SHARED | VM_INVALID)) | flags;
        *pte(space, page->address, false) = page->physical | page_flags(flags);
        __asm__ volatile("invlpg (%0)" : : "r"(page->address) : "memory");
    }
    return true;
}

void vmm_activate(struct address_space *space) {
    uint64_t root = space ? space->root : kernel_root;
    __asm__ volatile("mov %0, %%cr3" : : "r"(root) : "memory");
}

uint64_t vmm_translate(struct address_space *space, uint64_t address, bool write) {
    if (!space || address < PAGE_SIZE || address >= USER_TOP)
        return 0;
    uint64_t *entry = pte(space, address, false);
    if (!entry || (*entry & 5) != 5 || (write && !(*entry & 2)))
        return 0;
    return (*entry & ADDRESS_MASK) | (address & (PAGE_SIZE - 1));
}

bool user_range(struct address_space *space, uint64_t address, size_t size, bool write) {
    if (address >= USER_TOP || size > USER_TOP - address)
        return false;
    while (size) {
        if (!vmm_translate(space, address, write))
            return false;
        size_t chunk = PAGE_SIZE - (address & (PAGE_SIZE - 1));
        if (chunk > size)
            chunk = size;
        address += chunk;
        size -= chunk;
    }
    return true;
}

static bool copy_user(struct address_space *space, uint64_t user, void *kernel, size_t size,
                      bool to_user) {
    if (!user_range(space, user, size, to_user))
        return false;
    uint8_t *bytes = kernel;
    while (size) {
        uint64_t physical = vmm_translate(space, user, to_user);
        size_t chunk = PAGE_SIZE - (user & (PAGE_SIZE - 1));
        if (chunk > size)
            chunk = size;
        if (to_user)
            memcpy(physical_pointer(physical), bytes, chunk);
        else
            memcpy(bytes, physical_pointer(physical), chunk);
        bytes += chunk;
        user += chunk;
        size -= chunk;
    }
    return true;
}

bool copy_from_user(struct address_space *space, void *destination, uint64_t source, size_t size) {
    return copy_user(space, source, destination, size, false);
}

bool copy_to_user(struct address_space *space, uint64_t destination, const void *source,
                  size_t size) {
    return copy_user(space, destination, (void *)source, size, true);
}

bool user_string(struct address_space *space, char *destination, uint64_t source, size_t capacity) {
    return user_cstring(space, destination, source, capacity) >= 0;
}

long user_cstring(struct address_space *space, char *destination, uint64_t source,
                  size_t capacity) {
    size_t done = 0;
    while (done < capacity) {
        if (source >= USER_TOP || done > USER_TOP - source)
            return -EFAULT;
        uint64_t physical = vmm_translate(space, source + done, false);
        if (!physical)
            return -EFAULT;
        size_t length = PAGE_SIZE - (physical & (PAGE_SIZE - 1));
        if (length > capacity - done)
            length = capacity - done;
        const char *data = physical_pointer(physical);
        for (size_t i = 0; i < length; i++) {
            destination[done + i] = data[i];
            if (!data[i])
                return done + i;
        }
        done += length;
    }
    return -ENAMETOOLONG;
}

void *mmio_map(uint64_t physical, size_t size) {
    if (!size || size > 1024 * 1024 || physical > UINT64_MAX - size)
        return NULL;
    uint64_t first = physical & ~(PAGE_SIZE - 1);
    uint64_t last = (physical + size - 1) & ~(PAGE_SIZE - 1);
    volatile uint32_t *base = x64_map_mmio(first);
    for (uint64_t page = first + PAGE_SIZE; page <= last; page += PAGE_SIZE) {
        volatile uint32_t *next = x64_map_mmio(page);
        if ((uintptr_t)next != (uintptr_t)base + page - first)
            return NULL;
    }
    return (void *)((uintptr_t)base + (physical - first));
}

void vmm_file_resize(struct vnode *node) {
    vmm_collect_dirty(node);
    for (struct process *task = process_list(); task; task = task->next) {
        if (!task->space)
            continue;
        for (struct vm_page *p = task->space->pages; p; p = p->next) {
            if (p->file != node)
                continue;
            if (p->offset >= node->size)
                p->flags |= VM_INVALID;
            else
                p->flags &= ~VM_INVALID;
            uint64_t *entry = pte(task->space, p->address, false);
            if (entry)
                *entry = p->physical | page_flags(p->flags);
            __asm__ volatile("invlpg (%0)" ::"r"(p->address) : "memory");
        }
    }
}

void vmm_collect_dirty(struct vnode *node) {
    for (struct process *task = process_list(); task; task = task->next) {
        if (!task->space)
            continue;
        for (struct vm_page *p = task->space->pages; p; p = p->next) {
            if (p->file != node || !(p->flags & VM_SHARED))
                continue;
            uint64_t *entry = pte(task->space, p->address, false);
            if (!entry || !(*entry & 64))
                continue;
            for (struct file_page *cache = node->pages; cache; cache = cache->next)
                if (cache->physical == p->physical)
                    cache->dirty = true;
            *entry &= ~64ull;
            if (task == process_current())
                __asm__ volatile("invlpg (%0)" ::"r"(p->address) : "memory");
        }
    }
}
