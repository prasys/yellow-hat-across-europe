/* <sys/statvfs.h> for Windows: see ../compat_win.h. Free space on the volume
 * a path is on, for GetDiskFreeSpaceA. */
#ifndef MADELINE_COMPAT_SYS_STATVFS_H
#define MADELINE_COMPAT_SYS_STATVFS_H

#include <stdint.h>

struct statvfs {
    unsigned long f_bsize, f_frsize;
    uint64_t f_blocks, f_bfree, f_bavail;
};
int compat_statvfs(const char *path, struct statvfs *out);
#define statvfs(path, out) compat_statvfs(path, out)

#endif
