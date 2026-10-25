#include "ncc.h"
#include "abi.h"

extern long ncc_aster_syscall(long, long, long, long, long, long, long);

static long invoke(long number, long a, long b, long c, long d, long e, long f) {
    return ncc_aster_syscall(number,a,b,c,d,e,f);
}

static ncc_status error_status(long r) {
    if (r == -2) return NCC_NOT_FOUND;
    if (r == -12) return NCC_NOMEM;
    if (r == -22) return NCC_INVALID;
    return NCC_IO;
}

static ncc_status allocate(void *context, size_t size, void **out) {
    long r;
    (void)context;
    if (!out || !size || size > (size_t)INT64_MAX) return NCC_INVALID;
    *out = NULL;
    r = invoke(SYS_mmap, 0, (long)size, 3, 34, -1, 0);
    if ((unsigned long)r >= (unsigned long)-4095) return error_status(r);
    *out = (void *)r;
    return NCC_OK;
}

static void release(void *context, void *p, size_t size) {
    (void)context;
    invoke(SYS_munmap, (long)p, (long)size, 0, 0, 0, 0);
}

static ncc_status open_file(void *context, const char *path, int writing, intptr_t *out) {
    long r;
    (void)context;
    if (!path || !out) return NCC_INVALID;
    do { r = invoke(SYS_openat, -100, (long)path, writing ? 577 : 0, 0666, 0, 0); } while (r == -4);
    if (r < 0) return error_status(r);
    *out = r;
    return NCC_OK;
}

static ncc_status read_file(void *context, intptr_t fd, void *data, size_t size, size_t *out) {
    long r;
    (void)context;
    if (!out || (!data && size)) return NCC_INVALID;
    if (size > 0x7ffff000u) size = 0x7ffff000u;
    do { r = invoke(SYS_read, fd, (long)data, (long)size, 0, 0, 0); } while (r == -4);
    if (r < 0) return error_status(r);
    *out = (size_t)r;
    return NCC_OK;
}

static ncc_status write_file(void *context, intptr_t fd, const void *data, size_t size, size_t *out) {
    long r;
    (void)context;
    if (!out || (!data && size)) return NCC_INVALID;
    if (size > 0x7ffff000u) size = 0x7ffff000u;
    do { r = invoke(SYS_write, fd, (long)data, (long)size, 0, 0, 0); } while (r == -4);
    if (r < 0) return error_status(r);
    *out = (size_t)r;
    return NCC_OK;
}

static ncc_status close_file(void *context, intptr_t fd) {
    long r;
    (void)context;
    r = invoke(SYS_close, fd, 0, 0, 0, 0, 0);
    return r < 0 ? error_status(r) : NCC_OK;
}

static ncc_status executable(void *context, intptr_t fd) {
    long r;
    (void)context;
    r = invoke(SYS_fchmod, fd, 0755, 0, 0, 0, 0);
    return r < 0 ? error_status(r) : NCC_OK;
}

static void terminate(void *context, int status) {
    (void)context;
    invoke(SYS_exit_group, status, 0, 0, 0, 0, 0);
    for (;;) {}
}

const ncc_platform *ncc_aster_platform(void) {
    static const ncc_platform p = {
        NULL, allocate, release, open_file, read_file, write_file, close_file, executable, terminate
    };
    return &p;
}
