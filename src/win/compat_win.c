/*
 * compat_win.c - the POSIX calls win32hle makes, on the Windows API. See
 * compat_win.h for why this exists. This is the only file in the project that
 * includes <windows.h>; it includes nothing of win32hle's.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "compat_win.h"
/* this file reads the C runtime's own struct _stat64, whose members have these names */
#undef st_atime
#undef st_mtime
#undef st_ctime
#undef tm_gmtoff
#include "dirent.h"
#include "sys/mman.h"
#include "sys/statvfs.h"

/* ---- once, before main ----
 * Files are bytes: the game's data must not have its line ends "translated",
 * and win32hle opens with plain open()/fopen() as on POSIX, where there is no
 * text mode. The timer period makes a plain Sleep(1) last about 1 ms rather
 * than up to 15, for the waits that do not go through compat_usleep. */
__attribute__((constructor)) static void compat_init(void) {
    _set_fmode(_O_BINARY);
    timeBeginPeriod(1);
}

/* ---- memory ----
 * win32hle keeps the guest's 4 GB as one mapping (guest_mem.c, "based"): it
 * asks for the whole range with no access at a fixed address, then turns
 * 64 KB pieces of it on (fresh, zeroed) and off again with MAP_FIXED. On
 * Windows that is a reservation, with pieces committed and decommitted:
 * decommitting drops the contents, so a piece committed again reads as zero,
 * which is what mmap over the top gives on POSIX. */
static DWORD page_prot(int prot) {
    if (prot == PROT_NONE) return PAGE_NOACCESS;
    return prot & PROT_WRITE ? PAGE_READWRITE : PAGE_READONLY;
}

void *compat_mmap(void *addr, size_t len, int prot, int flags, int fd, long long off) {
    (void)off;
    if (fd >= 0 || !len) { errno = ENOTSUP; return MAP_FAILED; }   /* no file at an address: the caller copies instead */
    void *p;
    if (flags & MAP_FIXED) {
        /* Inside the reservation. Decommitting pages that are not committed is allowed. */
        VirtualFree(addr, len, MEM_DECOMMIT);
        if (prot == PROT_NONE) return addr;
        p = VirtualAlloc(addr, len, MEM_COMMIT, page_prot(prot));
        if (!p) p = VirtualAlloc(addr, len, MEM_RESERVE | MEM_COMMIT, page_prot(prot));   /* not in a reservation after all */
    } else if (prot == PROT_NONE) {
        p = VirtualAlloc(addr, len, MEM_RESERVE, PAGE_NOACCESS);    /* with an address: there or nowhere */
    } else {
        p = VirtualAlloc(addr, len, MEM_RESERVE | MEM_COMMIT, page_prot(prot));
    }
    if (!p) { errno = ENOMEM; return MAP_FAILED; }
    return p;
}
int compat_munmap(void *addr, size_t len) {
    (void)len;
    return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : -1;
}
int compat_mprotect(void *addr, size_t len, int prot) {
    DWORD old;
    return VirtualProtect(addr, len, page_prot(prot), &old) ? 0 : -1;
}
int compat_madvise(void *addr, size_t len, int advice) {
    if (advice != MADV_DONTNEED) return 0;
    VirtualFree(addr, len, MEM_DECOMMIT);
    return VirtualAlloc(addr, len, MEM_COMMIT, PAGE_READWRITE) ? 0 : -1;
}

/* ---- sleeping ----
 * A waitable timer of the high-resolution kind (Windows 10 1803 and later)
 * wakes within about half a millisecond; each thread keeps one. Older
 * systems fall back to Sleep, rounded up so a short wait is not a busy spin. */
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
static void sleep_100ns(long long ticks) {
    static __thread HANDLE timer;
    static __thread int tried;
    if (ticks <= 0) { SwitchToThread(); return; }
    if (!tried) {
        tried = 1;
        timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    }
    LARGE_INTEGER due;
    due.QuadPart = -ticks;
    if (timer && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(timer, INFINITE);
    else Sleep((DWORD)((ticks + 9999) / 10000));
}
int compat_usleep(unsigned int usec) {
    sleep_100ns((long long)usec * 10);
    return 0;
}
int compat_nanosleep(const struct timespec *req, struct timespec *rem) {
    if (rem) rem->tv_sec = 0, rem->tv_nsec = 0;
    sleep_100ns((long long)req->tv_sec * 10000000ll + (req->tv_nsec + 99) / 100);
    return 0;
}

/* ---- paths ----
 * win32hle builds host paths with '/' and tells an absolute one by its first
 * character being '/'. Windows takes '/' as a separator, so paths are handed
 * back as "C:/Users/..." and work everywhere; but such a path does not start
 * with '/', so win32hle's hle_guest_path puts the current directory in front
 * of it ("C:\here/C:/there/MADEURO.EXE") before asking for the real path. A
 * ':' cannot be part of a Windows file name, so the last "X:" at the start of
 * a component is where the real path begins. */
static const char *last_drive_spec(const char *p) {
    const char *at = p;
    for (const char *s = p; s[0] && s[1]; s++) {
        int letter = (s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z');
        if (letter && s[1] == ':' && (s == p || s[-1] == '/' || s[-1] == '\\')) at = s;
    }
    return at;
}

char *compat_realpath(const char *path, char *resolved) {
    char full[MAX_PATH * 2], final[MAX_PATH * 2];
    if (!path || !*path) { errno = EINVAL; return NULL; }
    if (!_fullpath(full, last_drive_spec(path), sizeof full)) { errno = ENAMETOOLONG; return NULL; }
    if (GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES) { errno = ENOENT; return NULL; }
    /* The name as the disk has it (case included): two spellings of one
     * folder must compare equal, since win32hle matches paths as strings. */
    const char *out = full;
    HANDLE h = CreateFileA(full, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD n = GetFinalPathNameByHandleA(h, final, sizeof final, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        CloseHandle(h);
        if (n && n < sizeof final && !strncmp(final, "\\\\?\\", 4) && final[5] == ':') out = final + 4;   /* a drive path, not \\?\UNC\ */
    }
    size_t len = strlen(out);
    char *r = resolved ? resolved : (char *)malloc(len + 1);
    if (!r) return NULL;
    for (size_t i = 0; i <= len; i++) r[i] = out[i] == '\\' ? '/' : out[i];
    while (len > 3 && r[len - 1] == '/') r[--len] = 0;              /* "C:/" keeps its slash */
    return r;
}

intptr_t compat_readlink(const char *path, char *buf, size_t n) {
    if (strcmp(path, "/proc/self/exe") != 0) { errno = EINVAL; return -1; }
    char exe[MAX_PATH * 2];
    DWORD len = GetModuleFileNameA(NULL, exe, sizeof exe);
    if (!len || len >= sizeof exe || len > n) { errno = ENAMETOOLONG; return -1; }
    for (DWORD i = 0; i < len; i++) buf[i] = exe[i] == '\\' ? '/' : exe[i];
    return (intptr_t)len;                                           /* not terminated, as readlink */
}

/* ---- directories ---- */
struct compat_dir {
    HANDLE h;
    WIN32_FIND_DATAA data;
    int have;                       /* data holds an entry not yet returned */
    struct compat_dirent ent;
};
compat_dir *compat_opendir(const char *path) {
    char pat[MAX_PATH * 2];
    size_t len = strlen(path);
    if (!len || len + 3 > sizeof pat) { errno = ENOENT; return NULL; }
    snprintf(pat, sizeof pat, "%s%s*", path, path[len - 1] == '/' || path[len - 1] == '\\' ? "" : "/");
    compat_dir *d = (compat_dir *)calloc(1, sizeof *d);
    if (!d) return NULL;
    d->h = FindFirstFileA(pat, &d->data);
    if (d->h == INVALID_HANDLE_VALUE) { free(d); errno = ENOENT; return NULL; }
    d->have = 1;
    return d;
}
struct compat_dirent *compat_readdir(compat_dir *d) {
    if (!d->have && !FindNextFileA(d->h, &d->data)) return NULL;
    d->have = 0;
    d->ent.d_type = d->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? DT_DIR : DT_REG;
    snprintf(d->ent.d_name, sizeof d->ent.d_name, "%s", d->data.cFileName);
    return &d->ent;
}
int compat_closedir(compat_dir *d) {
    FindClose(d->h);
    free(d);
    return 0;
}

/* ---- files ---- */
static void to_timespec(__time64_t t, struct timespec *out) { out->tv_sec = t, out->tv_nsec = 0; }
static void fill_stat(const struct _stat64 *s, struct compat_stat *out) {
    memset(out, 0, sizeof *out);
    out->st_dev = (unsigned)s->st_dev, out->st_ino = s->st_ino, out->st_nlink = (unsigned)s->st_nlink;
    out->st_mode = s->st_mode, out->st_size = s->st_size;
    to_timespec(s->st_atime, &out->st_atim), to_timespec(s->st_mtime, &out->st_mtim), to_timespec(s->st_ctime, &out->st_ctim);
}
int compat_stat(const char *path, struct compat_stat *out) {
    /* Windows' stat refuses "folder/", which POSIX allows */
    char tmp[MAX_PATH * 2];
    size_t len = strlen(path);
    if (len >= sizeof tmp) { errno = ENAMETOOLONG; return -1; }
    memcpy(tmp, path, len + 1);
    while (len > 3 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\')) tmp[--len] = 0;
    struct _stat64 s;
    if (_stat64(tmp, &s) != 0) return -1;
    fill_stat(&s, out);
    return 0;
}
int compat_fstat(int fd, struct compat_stat *out) {
    struct _stat64 s;
    if (_fstat64(fd, &s) != 0) return -1;
    fill_stat(&s, out);
    return 0;
}

/* Read at an offset, leaving the file's position where it was. Windows moves
 * the position even for a read given its own offset, so it is put back, and
 * the two steps are kept together. */
intptr_t compat_pread(int fd, void *buf, size_t n, long long off) {
    static CRITICAL_SECTION lock;
    static LONG ready;
    if (InterlockedCompareExchange(&ready, 1, 0) == 0) { InitializeCriticalSection(&lock); InterlockedExchange(&ready, 2); }
    while (ready != 2) SwitchToThread();
    EnterCriticalSection(&lock);
    long long was = _telli64(fd);
    intptr_t r = -1;
    if (was >= 0 && _lseeki64(fd, off, SEEK_SET) >= 0) {
        r = _read(fd, buf, n > 0x7FFFFFFFu ? 0x7FFFFFFFu : (unsigned)n);
        _lseeki64(fd, was, SEEK_SET);
    }
    LeaveCriticalSection(&lock);
    return r;
}

int compat_futimens(int fd, const struct timespec ts[2]) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    FILETIME ft[2], *use[2] = { NULL, NULL };                       /* access, write */
    for (int i = 0; i < 2; i++) {
        if (ts[i].tv_nsec == UTIME_OMIT) continue;
        unsigned long long v = ((unsigned long long)ts[i].tv_sec + 11644473600ull) * 10000000ull + (unsigned long long)ts[i].tv_nsec / 100;
        ft[i].dwLowDateTime = (DWORD)v, ft[i].dwHighDateTime = (DWORD)(v >> 32);
        use[i] = &ft[i];
    }
    return SetFileTime(h, NULL, use[0], use[1]) ? 0 : -1;
}

int compat_statvfs(const char *path, struct statvfs *out) {
    ULARGE_INTEGER avail, total, free_all;
    char full[MAX_PATH * 2], root[8];
    if (!_fullpath(full, path, sizeof full) || full[1] != ':') { errno = ENOENT; return -1; }
    snprintf(root, sizeof root, "%c:\\", full[0]);
    if (!GetDiskFreeSpaceExA(root, &avail, &total, &free_all)) { errno = ENOENT; return -1; }
    out->f_bsize = out->f_frsize = 4096;
    out->f_blocks = total.QuadPart / 4096, out->f_bfree = free_all.QuadPart / 4096, out->f_bavail = avail.QuadPart / 4096;
    return 0;
}

long compat_gmtoff(void) {
    TIME_ZONE_INFORMATION tz;
    DWORD r = GetTimeZoneInformation(&tz);
    if (r == TIME_ZONE_ID_INVALID) return 0;
    return -60l * (tz.Bias + (r == TIME_ZONE_ID_DAYLIGHT ? tz.DaylightBias : tz.StandardBias));
}

long compat_sysconf(int name) {
    if (name != _SC_NPROCESSORS_ONLN) { errno = EINVAL; return -1; }
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (long)si.dwNumberOfProcessors;
}
