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
 * (skip_intro, fullscreen, remember_name, smooth, upscale, scale, bars,
 * gamepad, pointer, soundfont); the command
 * line wins.
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
#include "lift_id.h"      /* generated: which MADEURO.EXE this program's code was lifted from */


/* madeline.ini, in the save folder: "key=value" lines; '#' starts a comment.
 * Read at start; a setting changed while playing (F10, F11, F12) is written
 * back, so the game comes up next time the way it was left. */
static char g_ini[PATH_MAX + 80];
char madeline_soundfont[1024];
static int ini_get(const char *key, char *out, size_t n) {
    FILE *f = fopen(g_ini, "r");
    char line[256];
    size_t k = strlen(key);
    int found = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line + strspn(line, " \t");
        if (strncmp(p, key, k)) continue;
        p += k; p += strspn(p, " \t");
        if (*p != '=') continue;
        p++; p += strspn(p, " \t");
        p[strcspn(p, "#\r\n")] = 0;
        size_t len = strlen(p);
        while (len && (p[len - 1] == ' ' || p[len - 1] == '\t')) p[--len] = 0;
        snprintf(out, n, "%s", p);
        found = 1;
    }
    fclose(f);
    return found;
}
static int ini_flag(const char *key, int fallback) {
    char v[64];
    if (!ini_get(key, v, sizeof v)) return fallback;
    return atoi(v) != 0 || !strcmp(v, "yes") || !strcmp(v, "true") || !strcmp(v, "on");
}
void madeline_ini_set(const char *key, const char *value) {       /* replace the key's line, or add one; the rest is kept */
    char tmp[PATH_MAX + 96], line[256];
    size_t k = strlen(key);
    int done = 0;
    if (!g_ini[0]) return;
    snprintf(tmp, sizeof tmp, "%s.new", g_ini);
    FILE *in = fopen(g_ini, "r"), *out = fopen(tmp, "w");
    if (!out) { if (in) fclose(in); return; }
    while (in && fgets(line, sizeof line, in)) {
        const char *p = line + strspn(line, " \t");
        if (!strncmp(p, key, k) && p[k + strspn(p + k, " \t")] == '=') {
            if (!done) fprintf(out, "%s=%s\n", key, value);
            done = 1;
        } else fputs(line, out);
    }
    if (!done) fprintf(out, "%s=%s\n", key, value);
    if (in) fclose(in);
    fclose(out);
    rename(tmp, g_ini);
}
static void screen_settings_changed(const char *scale, int bars, int fullscreen) {
    madeline_ini_set("scale", scale);
    madeline_ini_set("bars", bars ? "blur" : "black");
    madeline_ini_set("fullscreen", fullscreen ? "1" : "0");
}

/* POSIX cksum of a file (what `cksum` prints), so the Makefile and this agree. */
static int file_cksum(const char *path, uint32_t *crc_out, uint32_t *size_out) {
    FILE *f = fopen(path, "rb");
    uint32_t crc = 0, size = 0;
    int c;
    if (!f) return 0;
    while ((c = fgetc(f)) != EOF) {
        crc ^= (uint32_t)c << 24;
        for (int k = 0; k < 8; k++) crc = crc & 0x80000000u ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
        size++;
    }
    fclose(f);
    for (uint32_t n = size; n; n >>= 8) {
        crc ^= (n & 0xFFu) << 24;
        for (int k = 0; k < 8; k++) crc = crc & 0x80000000u ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
    }
    *crc_out = ~crc, *size_out = size;
    return 1;
}

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

int main(int argc, char **argv) {
    const char *cd = NULL;
    int headless = getenv("MADELINE_HEADLESS") != NULL, fullscreen = -1, skip_intro = -1, check_only = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless = 1;
        else if (!strcmp(argv[i], "--fullscreen")) fullscreen = 1;
        else if (!strcmp(argv[i], "--windowed")) fullscreen = 0;
        else if (!strcmp(argv[i], "--skip-intro")) skip_intro = 1;
        else if (!strcmp(argv[i], "--intro")) skip_intro = 0;
        else if (!strcmp(argv[i], "--check")) check_only = 1;       /* is this the disc I was built for? exit 0 yes, 3 no */
        else if (argv[i][0] == '-') { fprintf(stderr, "madeline: unknown option %s\n", argv[i]); return 64; }
        else cd = argv[i];
    }
    if (skip_intro < 0 && getenv("MADELINE_SKIP_INTRO")) skip_intro = 1;

    /* C:\CWONDERS\MADEURO is the "installed" folder: saves and scratch files
     * go there, in the user's home, so the disc's folder stays untouched. The
     * settings file is beside it. */
    char root[PATH_MAX], exe[PATH_MAX + 32], save[PATH_MAX + 64], cd_ini[PATH_MAX];
    madeline_data_dir(save, sizeof save);                /* per system: platform.c */
    {
        char dir[PATH_MAX + 128];
        snprintf(dir, sizeof dir, "%s/CWONDERS/MADEURO", save);
        madeline_make_dirs(dir);
    }
    snprintf(g_ini, sizeof g_ini, "%s/madeline.ini", save);

    /* D: is the disc: the folder named on the command line, else MADELINE_CD,
     * else the one the launcher remembered (cd= in madeline.ini), else ./game/cd. */
    if (!cd) cd = getenv("MADELINE_CD");
    if (!cd && ini_get("cd", cd_ini, sizeof cd_ini) && cd_ini[0]) cd = cd_ini;
    if (!cd) cd = "game/cd";
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
    {
        /* This program is the game's code, lifted from one particular MADEURO.EXE;
         * it reads that file again for its data. Another build of the game needs
         * its own lift: rebuild with `make CD=<that disc>`. */
        uint32_t crc, size;
        if (!file_cksum(exe, &crc, &size) || crc != LIFT_CKSUM || size != LIFT_SIZE) {
            fprintf(stderr, "madeline: this program was built from a different MADEURO.EXE than the one in '%s'.\n"
                            "Rebuild it for this disc: make CD='%s'\n", root, root);
            return 3;
        }
        if (check_only) return 0;
    }

    char scale[32] = "smooth", bars[32] = "blur";
    int smooth;
    if (fullscreen < 0) fullscreen = ini_flag("fullscreen", 0);
    if (skip_intro < 0) skip_intro = ini_flag("skip_intro", 0);
    smooth = ini_flag("smooth", 1) && !getenv("MADELINE_RAW");
    ini_get("scale", scale, sizeof scale);
    ini_get("bars", bars, sizeof bars);
    madeline_skip_intro = skip_intro;
    madeline_headless = headless;
    madeline_remember_name = ini_flag("remember_name", 1) && !(getenv("MADELINE_NO_NAME") && *getenv("MADELINE_NO_NAME"));
    snprintf(madeline_save_dir, sizeof madeline_save_dir, "%s/CWONDERS/MADEURO", save);
    if (getenv("MADELINE_SOUNDFONT")) snprintf(madeline_soundfont, sizeof madeline_soundfont, "%s", getenv("MADELINE_SOUNDFONT"));
    else ini_get("soundfont", madeline_soundfont, sizeof madeline_soundfont);

    win32hle_register(madeline_shims);          /* ours first: the first of a name wins */
    win32hle_register(madeline_print);
    win32hle_register(madeline_gfx);
    win32hle_register(madeline_audio);
    recomp_host_init();
    madeline_gfx_init();
    madeline_gfx_smooth(smooth);
    {
        char pointer[16] = "plain";
        ini_get("pointer", pointer, sizeof pointer);
        madeline_gfx_plain_pointer(strcmp(pointer, "game") != 0);
    }
    {
        /* upscale=xbr: the picture doubled with its edges redrawn (xbr.c). Off unless asked for. */
        char upscale[32] = "";
        const char *e = getenv("MADELINE_UPSCALE");
        if (e) snprintf(upscale, sizeof upscale, "%s", e); else ini_get("upscale", upscale, sizeof upscale);
        madeline_gfx_upscale(!strcmp(upscale, "xbr"));
    }
    /* The picture is 640x480 on a display many times that: it is scaled up
     * smoothly unless the player chose otherwise (F12), with a blur of itself
     * beside it where the screen is wider. */
    if (!hle_screen_scale(scale)) hle_screen_scale("smooth");
    hle_screen_bars(strcmp(bars, "black") != 0);
    hle_screen_settings_hook = screen_settings_changed;
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
    madeline_pad_init(ini_flag("gamepad", 1));
    madeline_screenshots_init(save);
    madeline_print_init(save);
    hle_set_pump_hook(madeline_idle);           /* the presenter's pump, then ours */
    if (recomp_host_boot_permissive(exe) != 0) return 65;
    return (int)recomp_host_run();
}
