/* <stdlib.h> for Windows: see compat_win.h. MinGW's, then realpath. */
#ifndef MADELINE_COMPAT_STDLIB_H
#define MADELINE_COMPAT_STDLIB_H

#include_next <stdlib.h>
#include "compat_win.h"

#define realpath compat_realpath

#endif
