/*
 * platform.c - the few things that differ between the systems this runs on:
 * where the player's files are kept, and making a folder.
 *
 *   macOS    ~/Library/Application Support/MadelineEuropeanAdventures
 *   Linux    $XDG_DATA_HOME/madeline-european-adventures  (~/.local/share/...)
 *   Windows  %APPDATA%\MadelineEuropeanAdventures
 *
 * MADELINE_SAVE_DIR overrides all three (the tests use it). The macOS and
 * Windows branches have been run; Linux not yet (docs/briefs/04).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "madeline.h"

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#define make_dir(p) mkdir(p, 0755)
#endif

void madeline_data_dir(char *out, size_t n) {
    const char *over = getenv("MADELINE_SAVE_DIR");
    if (over && *over) { snprintf(out, n, "%s", over); return; }
#if defined(_WIN32)
    const char *base = getenv("APPDATA");
    snprintf(out, n, "%s\\MadelineEuropeanAdventures", base && *base ? base : ".");
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    snprintf(out, n, "%s/Library/Application Support/MadelineEuropeanAdventures", home && *home ? home : ".");
#else
    const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    if (xdg && *xdg) snprintf(out, n, "%s/madeline-european-adventures", xdg);
    else snprintf(out, n, "%s/.local/share/madeline-european-adventures", home && *home ? home : ".");
#endif
}

/* Make a folder and any missing ones above it. */
void madeline_make_dirs(const char *path) {
    char tmp[2048];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/' && *p != '\\') continue;
        char c = *p;
        *p = 0;
        make_dir(tmp);
        *p = c;
    }
    make_dir(tmp);
}
