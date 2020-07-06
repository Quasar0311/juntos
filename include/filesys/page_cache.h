#ifndef FILESYS_PAGE_CACHE_H
#define FILESYS_PAGE_CACHE_H
#include "vm/vm.h"
#include "devices/disk.h"
#include "filesys/off_t.h"
#include "threads/synch.h"

struct page;
enum vm_type;

struct page_cache {
    bool dirty;
    bool used;
    disk_sector_t sector;
    int clock;
    struct lock cache_lock;
    void *data;
};

void page_cache_init (void);
bool page_cache_initializer (struct page *page, enum vm_type type, void *kva);

void pc_writeback_all_entries(void);
bool pc_read(disk_sector_t sector_idx, void *buffer, off_t bytes_read, 
    int chunk_size, int sector_ofs);
bool pc_write(disk_sector_t sector_idx, void *buffer, off_t bytes_written,
    int chunk_size, int sector_ofs);
#endif