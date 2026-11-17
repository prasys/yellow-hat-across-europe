/* <sys/mman.h> for Windows: see ../compat_win.h. Anonymous memory only: a
 * file cannot be mapped at a chosen address here, and win32hle falls back to
 * copying when that fails. */
#ifndef MADELINE_COMPAT_SYS_MMAN_H
#define MADELINE_COMPAT_SYS_MMAN_H

#include "../compat_win.h"

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_NORESERVE 0x4000
#define MAP_FAILED    ((void *)-1)

#define MADV_DONTNEED 4

#define mmap     compat_mmap
#define munmap   compat_munmap
#define mprotect compat_mprotect
#define madvise  compat_madvise

#endif
