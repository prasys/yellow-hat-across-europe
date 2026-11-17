/* <time.h> for Windows: see compat_win.h. MinGW's, then the re-entrant and
 * UTC calls under their POSIX names. */
#ifndef MADELINE_COMPAT_TIME_H
#define MADELINE_COMPAT_TIME_H

#include_next <time.h>
#include "compat_win.h"

static inline struct tm *compat_localtime_r(const time_t *t, struct tm *out) { return localtime_s(out, t) == 0 ? out : NULL; }
static inline struct tm *compat_gmtime_r(const time_t *t, struct tm *out)    { return gmtime_s(out, t) == 0 ? out : NULL; }
#undef localtime_r
#undef gmtime_r
#define localtime_r compat_localtime_r
#define gmtime_r    compat_gmtime_r
#define timegm      _mkgmtime

/* struct tm has no tm_gmtoff here. win32hle reads it in one place, as
 * `(int32_t)t.tm_gmtoff` for the time now, which this turns into the zone's
 * offset now; it is not a general replacement. */
#define tm_gmtoff tm_isdst * 0 + compat_gmtoff()

/* see unistd.h on usleep: the same for nanosleep */
#define nanosleep compat_nanosleep

#endif
