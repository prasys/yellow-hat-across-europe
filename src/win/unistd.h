/* <unistd.h> for Windows: see compat_win.h. MinGW's, then what it lacks. */
#ifndef MADELINE_COMPAT_UNISTD_H
#define MADELINE_COMPAT_UNISTD_H

#include_next <unistd.h>
#include "compat_win.h"

#define _SC_NPROCESSORS_ONLN 84
#define sysconf  compat_sysconf
#define readlink compat_readlink       /* only "/proc/self/exe": this program's own path */
#define pread    compat_pread

/* Windows' Sleep counts in milliseconds and, left alone, in 15 ms steps. The
 * game waits by spinning with sleeps of 50 to 300 microseconds (shims.c's
 * idle tick, win32hle's message pump), so those have to be short for real. */
#undef usleep
#define usleep compat_usleep

/* Guest pages are 4 KB and so are Windows'. */
#undef getpagesize
#define getpagesize() 4096

#endif
