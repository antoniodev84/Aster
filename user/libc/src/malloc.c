#include <os.h>

struct arena;
struct block {
    size_t size;
    struct block *previous, *next;
    struct arena *arena;
    uint64_t magic;
    unsigned available, pad;
};
struct arena {
    size_t length;
    struct arena *previous, *next;
    struct block *first;
};
struct aligned_tag {
    uint64_t magic;
    void *original;
    size_t size;
};
static struct arena *arenas;
#define MAGIC 0x4d414c4c4f43424cull
#define ALIGN_MAGIC 0x414c49474e454430ull
_Static_assert(sizeof(struct block) % 16 == 0, "malloc alignment");
static void split(struct block *b, size_t size) {
    if (b->size < size + sizeof(struct block) + 16)
        return;
    struct block *n = (void *)((char *)(b + 1) + size);
    *n = (struct block){.size = b->size - size - sizeof(*n),
                        .previous = b,
                        .next = b->next,
                        .arena = b->arena,
                        .magic = MAGIC,
                        .available = 1};
    if (n->next)
        n->next->previous = n;
    if (n->next && n->next->available) {
        struct block *after = n->next;
        n->size += sizeof(*after) + after->size;
        n->next = after->next;
        if (n->next)
            n->next->previous = n;
        after->magic = 0;
    }
    b->next = n;
    b->size = size;
}
void *malloc(size_t size) {
    if (!size)
        size = 1;
    if (size > SIZE_MAX - 65536) {
        errno = ENOMEM;
        return NULL;
    }
    size = (size + 15) & ~15ul;
    for (struct arena *a = arenas; a; a = a->next)
        for (struct block *b = a->first; b; b = b->next)
            if (b->available && b->size >= size) {
                split(b, size);
                b->available = 0;
                return b + 1;
            }
    size_t length = (size + sizeof(struct arena) + sizeof(struct block) + 4095) & ~4095ul;
    if (length < 65536)
        length = 65536;
    struct arena *a =
        mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (a == MAP_FAILED)
        return NULL;
    *a = (struct arena){.length = length, .next = arenas, .first = (void *)(a + 1)};
    if (arenas)
        arenas->previous = a;
    arenas = a;
    struct block *b = a->first;
    *b = (struct block){.size = length - sizeof(*a) - sizeof(*b), .arena = a, .magic = MAGIC};
    split(b, size);
    return b + 1;
}
static struct block *normal(void *p) {
    struct block *b = (struct block *)p - 1;
    return b->magic == MAGIC ? b : NULL;
}
void free(void *p) {
    if (!p)
        return;
    struct block *b = normal(p);
    if (!b) {
        struct aligned_tag *tag = (struct aligned_tag *)p - 1;
        if (tag->magic == ALIGN_MAGIC) {
            void *original = tag->original;
            tag->magic = 0;
            free(original);
            return;
        }
        abort();
    }
    if (b->available)
        abort();
    b->available = 1;
    if (b->next && b->next->available) {
        struct block *next = b->next;
        b->size += sizeof(*next) + next->size;
        b->next = next->next;
        if (b->next)
            b->next->previous = b;
        next->magic = 0;
    }
    if (b->previous && b->previous->available) {
        struct block *previous = b->previous;
        previous->size += sizeof(*b) + b->size;
        previous->next = b->next;
        if (b->next)
            b->next->previous = previous;
        b->magic = 0;
        b = previous;
    }
    if (!b->previous && !b->next) {
        struct arena *a = b->arena;
        if (a->previous)
            a->previous->next = a->next;
        else
            arenas = a->next;
        if (a->next)
            a->next->previous = a->previous;
        munmap(a, a->length);
    }
}
size_t malloc_usable_size(void *p) {
    if (!p)
        return 0;
    struct block *b = normal(p);
    if (b)
        return b->size;
    struct aligned_tag *t = (struct aligned_tag *)p - 1;
    return t->magic == ALIGN_MAGIC ? t->size : 0;
}
void *calloc(size_t n, size_t s) {
    if (s && n > SIZE_MAX / s) {
        errno = ENOMEM;
        return NULL;
    }
    void *p = malloc(n * s);
    if (p)
        memset(p, 0, n * s);
    return p;
}
void *realloc(void *p, size_t size) {
    if (!p)
        return malloc(size);
    if (!size) {
        free(p);
        return NULL;
    }
    struct block *b = normal(p);
    size_t old = malloc_usable_size(p);
    if (b && size <= b->size) {
        split(b, (size + 15) & ~15ul);
        return p;
    }
    void *n = malloc(size);
    if (!n)
        return NULL;
    memcpy(n, p, old < size ? old : size);
    free(p);
    return n;
}
void *reallocarray(void *p, size_t n, size_t s) {
    if (s && n > SIZE_MAX / s) {
        errno = ENOMEM;
        return NULL;
    }
    return realloc(p, n * s);
}
int posix_memalign(void **result, size_t alignment, size_t size) {
    if (alignment < sizeof(void *) || (alignment & (alignment - 1)))
        return EINVAL;
    if (alignment > SIZE_MAX - size - sizeof(struct aligned_tag))
        return ENOMEM;
    void *original = malloc(size + alignment + sizeof(struct aligned_tag));
    if (!original)
        return ENOMEM;
    uintptr_t address =
        ((uintptr_t)original + sizeof(struct aligned_tag) + alignment - 1) & ~(alignment - 1);
    struct aligned_tag *tag = (void *)(address - sizeof(*tag));
    *tag = (struct aligned_tag){ALIGN_MAGIC, original, size};
    *result = (void *)address;
    return 0;
}
void *aligned_alloc(size_t a, size_t n) {
    if (!a || n % a) {
        errno = EINVAL;
        return NULL;
    }
    void *p;
    int e = posix_memalign(&p, a, n);
    if (e) {
        errno = e;
        return NULL;
    }
    return p;
}
