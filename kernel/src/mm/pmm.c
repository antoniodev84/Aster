#include "mm.h"
#include "memory.h"
#include "console/kprintf.h"

static uint32_t *references;
static uint64_t page_count, free_pages, cursor = 1, hhdm;

void *physical_pointer(uint64_t physical) {
    return (void *)(uintptr_t)(hhdm + physical);
}
uint64_t direct_physical(const void *pointer) {
    return (uintptr_t)pointer - hhdm;
}

void pmm_init(const struct hal_boot_info *boot) {
    hhdm = boot->hhdm;
    uint64_t highest = 0;
    for (size_t i = 0; i < boot->region_count; i++) {
        const struct hal_memory_region *r = &boot->regions[i];
        if (r->usable && r->length <= UINT64_MAX - r->base && r->base + r->length > highest)
            highest = r->base + r->length;
    }
    page_count = highest / PAGE_SIZE;
    if (!page_count || page_count > SIZE_MAX / sizeof(*references))
        hal_panic("invalid physical memory map");
    uint64_t metadata_size = (page_count * sizeof(*references) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t metadata = 0;
    for (size_t i = 0; i < boot->region_count; i++) {
        const struct hal_memory_region *r = &boot->regions[i];
        uint64_t first = (r->base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (r->usable && first >= PAGE_SIZE && first >= r->base && first <= r->base + r->length &&
            metadata_size <= r->base + r->length - first) {
            metadata = first;
            break;
        }
    }
    if (!metadata)
        hal_panic("physical allocator metadata allocation failed");
    references = physical_pointer(metadata);
    memset(references, 0xff, metadata_size);
    for (size_t i = 0; i < boot->region_count; i++) {
        const struct hal_memory_region *r = &boot->regions[i];
        if (!r->usable)
            continue;
        uint64_t first = (r->base + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t end = (r->base + r->length) / PAGE_SIZE;
        for (uint64_t p = first; p < end && p < page_count; p++) {
            uint64_t physical = p * PAGE_SIZE;
            if (p && (physical < metadata || physical >= metadata + metadata_size)) {
                if (references[p] != 0)
                    free_pages++;
                references[p] = 0;
            }
        }
    }
    kprintf("[pmm] pages=%llu free=%llu metadata=0x%llx\n", (unsigned long long)page_count,
            (unsigned long long)free_pages, (unsigned long long)metadata);
}

uint64_t pmm_alloc(size_t pages) {
    if (!pages)
        return 0;
    uint64_t state = hal_irq_save();
    if (pages > free_pages) {
        hal_irq_restore(state);
        return 0;
    }
    uint64_t run = 0, first = 0, index = cursor;
    for (uint64_t checked = 0; checked < page_count; checked++) {
        if (index >= page_count) {
            index = 1;
            run = 0;
        }
        if (!references[index]) {
            if (!run)
                first = index;
            if (++run == pages) {
                for (size_t j = 0; j < pages; j++)
                    references[first + j] = 1;
                free_pages -= pages;
                cursor = first + pages;
                hal_irq_restore(state);
                memset(physical_pointer(first * PAGE_SIZE), 0, pages * PAGE_SIZE);
                return first * PAGE_SIZE;
            }
        } else
            run = 0;
        index++;
    }
    hal_irq_restore(state);
    return 0;
}

void pmm_retain(uint64_t physical) {
    uint64_t state = hal_irq_save();
    uint64_t index = physical / PAGE_SIZE;
    if ((physical & (PAGE_SIZE - 1)) || index >= page_count || !references[index] ||
        references[index] >= UINT32_MAX - 1)
        hal_panic("invalid physical page retain");
    references[index]++;
    hal_irq_restore(state);
}

void pmm_free(uint64_t physical, size_t pages) {
    uint64_t state = hal_irq_save();
    uint64_t index = physical / PAGE_SIZE;
    if ((physical & (PAGE_SIZE - 1)) || index >= page_count || pages > page_count - index)
        hal_panic("invalid physical page free");
    for (size_t i = 0; i < pages; i++) {
        if (!references[index + i] || references[index + i] == UINT32_MAX)
            hal_panic("physical page double free or reserved page");
        if (--references[index + i] == 0)
            free_pages++;
    }
    hal_irq_restore(state);
}

struct pmm_stats pmm_get_stats(void) {
    uint64_t state = hal_irq_save();
    struct pmm_stats stats = {page_count, free_pages};
    hal_irq_restore(state);
    return stats;
}

struct allocation {
    uint64_t magic, pages;
    size_t size;
    uint64_t reserved;
};

void *kmalloc(size_t size) {
    if (!size || size > SIZE_MAX - sizeof(struct allocation) - PAGE_SIZE)
        return NULL;
    size_t pages = (size + sizeof(struct allocation) + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t physical = pmm_alloc(pages);
    if (!physical)
        return NULL;
    struct allocation *header = physical_pointer(physical);
    *header = (struct allocation){0x4b48454150414c4cull, pages, size, 0};
    return header + 1;
}

void *kcalloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size)
        return NULL;
    return kmalloc(count * size);
}

void kfree(void *pointer) {
    if (!pointer)
        return;
    struct allocation *header = (struct allocation *)pointer - 1;
    if (header->magic != 0x4b48454150414c4cull)
        hal_panic("invalid kernel heap free");
    uint64_t physical = direct_physical(header);
    size_t pages = header->pages;
    header->magic = 0;
    pmm_free(physical, pages);
}

uint64_t pmm_alloc_below(size_t pages, uint64_t limit) {
    if (!pages)
        return 0;
    uint64_t state = hal_irq_save(), end = limit / PAGE_SIZE;
    if (end > page_count)
        end = page_count;
    size_t run = 0;
    uint64_t first = 0;
    for (uint64_t index = 1; index < end; index++) {
        if (references[index]) {
            run = 0;
            continue;
        }
        if (!run)
            first = index;
        if (++run == pages) {
            for (size_t i = 0; i < pages; i++)
                references[first + i] = 1;
            free_pages -= pages;
            hal_irq_restore(state);
            memset(physical_pointer(first * PAGE_SIZE), 0, pages * PAGE_SIZE);
            return first * PAGE_SIZE;
        }
    }
    hal_irq_restore(state);
    return 0;
}
