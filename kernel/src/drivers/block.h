#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct block_device {
    char name[16];
    uint64_t sectors;
    void *driver;
    bool (*transfer)(struct block_device *, uint64_t, uint32_t, void *, bool);
    bool (*flush)(struct block_device *);
};

extern struct block_device block_devices[32];
extern size_t block_device_count;
void ahci_init(void);
bool block_read(struct block_device *device, uint64_t lba, uint32_t sectors, void *buffer);
bool block_write(struct block_device *device, uint64_t lba, uint32_t sectors, const void *buffer);

#endif
