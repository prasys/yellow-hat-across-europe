/*
 * main.c - the host for Madeline: European Adventures (Creative Wonders, 1996).
 *
 * The game's own code (MADEURO.EXE, a 148 KB Borland C++ PE32) is lifted to C
 * by pcrecomp and linked into this program; pcrecomp's win32hle answers the
 * Win32 calls it makes on libc + SDL2. This file only tells that layer where
 * the player's own copy of the CD is and what the installer would have left
 * in the registry.
 *
 *   madeline [--skip-intro] [--fullscreen|--windowed] [--headless] <path to the extracted CD>
 *
 * The same choices can be kept in madeline.ini in the save folder
 * (skip_intro=1, fullscreen=1, remember_name=0); the command line wins.
 *
 * No game data is built into this program: it maps MADEURO.EXE from the CD
 * folder at run time for its data sections and resources.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include "win32hle.h"
#include "host.h"
#include "madeline.h"


/* madeline.ini: "key=value" lines; '#' starts a comment. Missing file: no settings. */
static int ini_flag(const char *file, const char *key, int fallback) {
    FILE *f = fopen(file, "r");
    char line[256];
    size_t n = strlen(key);
    if (!f) return fallback;
    while (fgets(line, sizeof line, f)) {
        char *p = line + strspn(line, " \t");
        if (strncmp(p, key, n)) continue;
        p += n; p += strspn(p, " \t");
        if (*p == '=') { fallback = atoi(p + 1) != 0 || !strncmp(p + 1 + strspn(p + 1, " "), "yes", 3) || !strncmp(p + 1 + strspn(p + 1, " "), "true", 4); }
    }
    fclose(f);
    return fallback;
}

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

int main(int argc, char **argv) {
    const char *cd = NULL;
    int headless = getenv("MADELINE_HEADLESS") != NULL, fullscreen = -1, skip_intro = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless = 1;
        else if (!strcmp(argv[i], "--fullscreen")) fullscreen = 1;
        else if (!strcmp(argv[i], "--windowed")) fullscreen = 0;
        else if (!strcmp(argv[i], "--skip-intro")) skip_intro = 1;
        else if (!strcmp(argv[i], "--intro")) skip_intro = 0;
        else if (argv[i][0] == '-') { fprintf(stderr, "madeline: unknown option %s\n", argv[i]); return 64; }
        else cd = argv[i];
    }
    if (skip_intro < 0 && getenv("MADELINE_SKIP_INTRO")) skip_intro = 1;
    if (!cd) cd = getenv("MADELINE_CD");
    if (!cd) cd = "game/cd";

    char root[PATH_MAX], exe[PATH_MAX + 32], save[PATH_MAX + 64];
    if (!realpath(cd, root)) {
        fprintf(stderr, "madeline: no game folder at '%s'.\n"
                        "Extract your own CD first: scripts/extract_iso.sh <your.iso>\n", cd);
        return 66;
    }
    snprintf(exe, sizeof exe, "%s/MADEURO.EXE", root);
    if (!exists(exe)) {
        fprintf(stderr, "madeline: %s not found - is '%s' the extracted CD?\n", exe, root);
        return 66;
    }

    /* D: is the CD. C:\CWONDERS\MADEURO is the "installed" folder: saves and scratch
     * files go there, in the user's home, so the CD folder stays untouched. */
    const char *home = getenv("HOME");
    snprintf(save, sizeof save, "%s/Library/Application Support/MadelineEuropeanAdventures", home ? home : ".");
    if (getenv("MADELINE_SAVE_DIR")) snprintf(save, sizeof save, "%s", getenv("MADELINE_SAVE_DIR"));   /* tests: not the player's saves */
    {
        char cmd[PATH_MAX + 128];
        mkdir(save, 0755);
        snprintf(cmd, sizeof cmd, "%s/CWONDERS", save);
        mkdir(cmd, 0755);
        snprintf(cmd, sizeof cmd, "%s/CWONDERS/MADEURO", save);
        mkdir(cmd, 0755);
    }

    {
        char ini[PATH_MAX + 80];
        snprintf(ini, sizeof ini, "%s/madeline.ini", save);
        if (fullscreen < 0) fullscreen = ini_flag(ini, "fullscreen", 0);
        if (skip_intro < 0) skip_intro = ini_flag(ini, "skip_intro", 0);
        madeline_skip_intro = skip_intro;
        madeline_headless = headless;
        madeline_remember_name = ini_flag(ini, "remember_name", 1) && !(getenv("MADELINE_NO_NAME") && *getenv("MADELINE_NO_NAME"));
        snprintf(madeline_save_dir, sizeof madeline_save_dir, "%s/CWONDERS/MADEURO", save);
    }

    win32hle_register(madeline_shims);          /* ours first: the first of a name wins */
    win32hle_register(madeline_gfx);
    win32hle_register(madeline_audio);
    recomp_host_init();
    madeline_gfx_init();
    if (getenv("MADELINE_TRACE")) win32hle_trace = 1;
    hle_set_drive('D', root);
    hle_set_drive('C', save);

    /* The game opens some files by bare relative name ("mad400\\palanim.dat"):
     * it expects to be started in the disc's root. */
    if (chdir(root) != 0) { perror(root); return 66; }

    hle_set_module_path(exe);
    hle_set_command_line("D:\\MADEURO.EXE");

    hle_screen_confine(0);                      /* a point-and-click game in a window: the mouse is free to leave */
    if (hle_screen_open("Madeline: European Adventures", fullscreen, headless) != 0 && !headless) {
        fprintf(stderr, "madeline: could not open a window\n");
        return 70;
    }
    madeline_screenshots_init(save);
    hle_set_pump_hook(madeline_idle);           /* the presenter's pump, then ours */
    if (recomp_host_boot_permissive(exe) != 0) return 65;
    return (int)recomp_host_run();
}
