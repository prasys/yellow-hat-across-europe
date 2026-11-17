/* <sys/stat.h> for Windows: see ../compat_win.h. MinGW's, then:
 *   - a struct stat with the nanosecond times win32hle reads (st_mtim);
 *   - mkdir(path, mode): Windows' takes no mode;
 *   - futimens. */
#ifndef MADELINE_COMPAT_SYS_STAT_H
#define MADELINE_COMPAT_SYS_STAT_H

#include_next <sys/stat.h>
#include <direct.h>
#include <io.h>
#include <time.h>
#include "../compat_win.h"

struct compat_stat {
    unsigned int   st_dev, st_ino, st_nlink;
    unsigned short st_mode;
    long long      st_size;
    struct timespec st_atim, st_mtim, st_ctim;
};
int compat_stat(const char *path, struct compat_stat *out);
int compat_fstat(int fd, struct compat_stat *out);

/* One name for the struct and the function, as in POSIX: the macros rename both. */
#undef stat
#undef fstat
#undef lstat
#define stat  compat_stat
#define fstat compat_fstat
#define lstat compat_stat          /* no symlinks to tell apart */
#undef st_atime
#undef st_mtime
#undef st_ctime
#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec

#undef mkdir
#define mkdir(path, ...) _mkdir(path)

#ifndef UTIME_OMIT
#define UTIME_OMIT ((1l << 30) - 2l)
#endif
#define futimens compat_futimens

#endif
