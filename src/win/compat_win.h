/*
 * compat_win.h - the POSIX calls pcrecomp's win32hle makes that MinGW-w64 does
 * not have, declared here and written over the Windows API in compat_win.c.
 *
 * win32hle is someone else's code and is written for POSIX (docs/briefs/05).
 * Rather than change it, this folder is put first on the include path on
 * Windows: its <unistd.h>, <dirent.h>, <sys/mman.h>... pull in MinGW's own
 * header (where there is one) and then add what is missing, under compat_
 * names so nothing clashes with the C runtime. Nothing here includes
 * <windows.h>: win32hle defines Win32's constants itself and the two cannot
 * meet in one file. Every real Windows call is in compat_win.c.
 */
#ifndef MADELINE_COMPAT_WIN_H
#define MADELINE_COMPAT_WIN_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct timespec;

/* <sys/mman.h> */
void *compat_mmap(void *addr, size_t len, int prot, int flags, int fd, long long off);
int   compat_munmap(void *addr, size_t len);
int   compat_mprotect(void *addr, size_t len, int prot);
int   compat_madvise(void *addr, size_t len, int advice);

/* <unistd.h> */
long     compat_sysconf(int name);
intptr_t compat_readlink(const char *path, char *buf, size_t n);
intptr_t compat_pread(int fd, void *buf, size_t n, long long off);
int      compat_usleep(unsigned int usec);

/* <time.h> */
int  compat_nanosleep(const struct timespec *req, struct timespec *rem);
long compat_gmtoff(void);              /* local time minus UTC now, in seconds */

/* <stdlib.h> */
char *compat_realpath(const char *path, char *resolved);

/* <sys/stat.h> */
int compat_futimens(int fd, const struct timespec ts[2]);

#endif
