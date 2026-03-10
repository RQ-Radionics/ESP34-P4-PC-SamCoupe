/* sys/statvfs.h — stub for ESP32 (newlib has no statvfs)
 *
 * ghc::filesystem uses statvfs only in space() which SimCoupe never calls.
 * Provide a minimal struct so the header compiles; the function will
 * return an error if ever called at runtime.
 */
#pragma once

#include <sys/types.h>

struct statvfs {
    unsigned long f_bsize;   /* file system block size */
    unsigned long f_frsize;  /* fragment size */
    unsigned long f_blocks;  /* size of fs in f_frsize units */
    unsigned long f_bfree;   /* # free blocks */
    unsigned long f_bavail;  /* # free blocks for unprivileged users */
    unsigned long f_files;   /* # inodes */
    unsigned long f_ffree;   /* # free inodes */
    unsigned long f_favail;  /* # free inodes for unprivileged users */
    unsigned long f_fsid;    /* file system ID */
    unsigned long f_flag;    /* mount flags */
    unsigned long f_namemax; /* maximum filename length */
};

static inline int statvfs(const char *path, struct statvfs *buf)
{
    (void)path;
    (void)buf;
    return -1;  /* not supported on ESP32 */
}
