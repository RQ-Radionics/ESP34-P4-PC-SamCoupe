/*
 * fatfs_alloc.c — Override ff_memalloc/ff_memfree to use internal DRAM.
 *
 * Problem: ff_memalloc() defaults to malloc() which on ESP32-P4 with PSRAM
 * can return PSRAM addresses. The FAT VFS context (vfs_fat_ctx_t) contains
 * a _lock_t (newlib mutex). When esp_cache_msync() invalidates the PSRAM
 * cache during display flush, the lock handle reads as garbage → Load access
 * fault in xQueueSemaphoreTake (MTVAL=0x6d2424ad, xQueue=0x6d24246d).
 *
 * Fix: force all FatFS allocations to internal DRAM via MALLOC_CAP_INTERNAL.
 * This covers: vfs_fat_ctx_t, FIL, DIR, LFN buffer, sector buffer, etc.
 */

#include "ff.h"
#include "esp_heap_caps.h"

void* ff_memalloc(unsigned msize)
{
    return heap_caps_malloc(msize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void ff_memfree(void* mblock)
{
    heap_caps_free(mblock);
}
