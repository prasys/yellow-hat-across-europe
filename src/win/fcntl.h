/* <fcntl.h> for Windows: see compat_win.h. Files are opened binary by default
 * (compat_win.c sets that once at start), so only the flag is needed here. */
#ifndef MADELINE_COMPAT_FCNTL_H
#define MADELINE_COMPAT_FCNTL_H

#include_next <fcntl.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC O_NOINHERIT
#endif

#endif
