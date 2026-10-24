#ifndef MM_H
#define MM_H

#include "hal/hal.h"

#define PAGE_SIZE 4096ull
#define USER_TOP 0x0000800000000000ull
#define VM_WRITE 1u
#define VM_EXEC 2u
#define VM_USER 4u
#define VM_SHARED 8u
#define VM_NONE 16u
#define VM_INVALID 32u

struct vnode;
struct vm_page {
    uint64_t address, physical;
    unsigned flags;
    struct vnode *file;
    uint64_t offset;
    struct vm_page *next;
};

struct address_space {
    uint64_t root;
    struct vm_page *pages;
};

struct pmm_stats {
    uint64_t total, free;
};

void pmm_init(const struct hal_boot_info *boot);
uint64_t pmm_alloc(size_t pages);
uint64_t pmm_alloc_below(size_t pages, uint64_t limit);
void pmm_retain(uint64_t physical);
void pmm_free(uint64_t physical, size_t pages);
void *physical_pointer(uint64_t physical);
uint64_t direct_physical(const void *pointer);
struct pmm_stats pmm_get_stats(void);
void *kmalloc(size_t size);
void *kcalloc(size_t count, size_t size);
void kfree(void *pointer);
void vmm_init(void);
struct address_space *vmm_create(void);
void vmm_destroy(struct address_space *space);
struct address_space *vmm_clone(struct address_space *space);
bool vmm_map(struct address_space *space, uint64_t address, uint64_t physical, unsigned flags);
bool vmm_allocate(struct address_space *space, uint64_t address, size_t size, unsigned flags);
bool vmm_unmap(struct address_space *space, uint64_t address, size_t size);
bool vmm_protect(struct address_space *space, uint64_t address, size_t size, unsigned flags);
void vmm_activate(struct address_space *space);
uint64_t vmm_translate(struct address_space *space, uint64_t address, bool write);
bool user_range(struct address_space *space, uint64_t address, size_t size, bool write);
bool copy_from_user(struct address_space *space, void *destination, uint64_t source, size_t size);
bool copy_to_user(struct address_space *space, uint64_t destination, const void *source,
                  size_t size);
bool user_string(struct address_space *space, char *destination, uint64_t source, size_t capacity);
long user_cstring(struct address_space *space, char *destination, uint64_t source, size_t capacity);
void vmm_collect_dirty(struct vnode *node);
void vmm_file_resize(struct vnode *node);
void *mmio_map(uint64_t physical, size_t size);

#endif
