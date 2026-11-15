/*
 * debug.c - a script for driving the game without a person, for testing:
 *
 *   MADELINE_SCRIPT="3000 shot /tmp/a.bmp; 4000 click 320 240; 9000 key 27; 12000 quit"
 *
 * Each step is a time in milliseconds since start and an action: `shot FILE`
 * writes the picture as a BMP, `click X Y` presses and releases the left
 * button there, `move X Y` moves the mouse, `key VK` presses and releases a
 * virtual key, `type TEXT` presses its letters, `peek ADDR` prints a
 * guest dword, `close` is the window's close button, `quit` leaves. Times are on the
 * game's clock, so MADELINE_SPEED shortens them. madeline_debug_tick runs the steps that are due.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "madeline.h"

#define WM_MOUSEMOVE   0x0200u
#define WM_LBUTTONDOWN 0x0201u
#define WM_LBUTTONUP   0x0202u

static char *g_script, *g_next;
static uint32_t g_t0, g_release_at;
static int g_rx, g_ry, g_started;

static void shot(const char *file) {
    int w, h;
    const uint32_t *px = madeline_frame(&w, &h);
    FILE *f = fopen(file, "wb");
    if (!f) { perror(file); return; }
    uint8_t hd[54] = { 'B', 'M' };
    uint32_t size = 54u + (uint32_t)(w * h * 4), v;
    memcpy(hd + 2, &size, 4);
    v = 54; memcpy(hd + 10, &v, 4);
    v = 40; memcpy(hd + 14, &v, 4);
    memcpy(hd + 18, &w, 4);
    int32_t nh = -h; memcpy(hd + 22, &nh, 4);
    hd[26] = 1, hd[28] = 32;
    fwrite(hd, 1, 54, f);
    fwrite(px, 4, (size_t)(w * h), f);
    fclose(f);
    fprintf(stderr, "[script] shot %s\n", file);
}

/* A screenshot on request: F9 in the window, or SIGUSR1 from outside
 * (`pkill -USR1 madeline`). It is the game's own 640x480 picture, pointer
 * included, written to <save folder>/screenshots. */
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
static volatile sig_atomic_t g_shot_wanted;
static char g_shot_dir[1100];
static void on_usr1(int sig) { (void)sig; g_shot_wanted = 1; }
static int on_key(int scancode, int down) {
    if (scancode == 66 /* SDL_SCANCODE_F9 */) { if (down) g_shot_wanted = 1; return 1; }
    if (scancode == 67 /* SDL_SCANCODE_F10 */) {         /* smoothing on and off, to compare */
        if (down) {
            int on = madeline_gfx_smooth(!madeline_gfx_smooth(-1));
            madeline_ini_set("smooth", on ? "1" : "0");
            fprintf(stderr, "[gfx] smoothing %s\n", on ? "on" : "off");
        }
        return 1;
    }
    return 0;
}
void madeline_screenshots_init(const char *save_root) {
    snprintf(g_shot_dir, sizeof g_shot_dir, "%s/screenshots", save_root);
    signal(SIGUSR1, on_usr1);
    hle_screen_key_hook = on_key;
}
static void shot_if_wanted(void) {
    if (!g_shot_wanted || !g_shot_dir[0]) return;
    g_shot_wanted = 0;
    char file[1200], stamp[32];
    static int n;
    time_t t = time(NULL);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&t));
    mkdir(g_shot_dir, 0755);
    snprintf(file, sizeof file, "%s/madeline-%s-%02d.bmp", g_shot_dir, stamp, n++ % 100);
    shot(file);
}

void madeline_debug_tick(uint32_t now) {
    shot_if_wanted();
    if (!g_started) {
        g_started = 1, g_t0 = now;
        const char *s = getenv("MADELINE_SCRIPT");
        if (s) g_script = g_next = strdup(s);
    }
    if (g_release_at && now >= g_release_at) { g_release_at = 0; hle_input_mouse(WM_LBUTTONUP, g_rx, g_ry); }
    while (g_next && *g_next) {
        char *end;
        unsigned long at = strtoul(g_next, &end, 10);
        if (now - g_t0 < at) return;
        while (*end == ' ') end++;
        char *stop = strchr(end, ';');
        if (stop) *stop = 0;
        int x = 0, y = 0;
        char arg[512];
        if (sscanf(end, "shot %511s", arg) == 1) shot(arg);
        else if (sscanf(end, "click %d %d", &x, &y) == 2) {
            hle_input_mouse(WM_MOUSEMOVE, x, y);
            hle_input_mouse(WM_LBUTTONDOWN, x, y);
            g_rx = x, g_ry = y, g_release_at = now + 80;
        }
        else if (sscanf(end, "move %d %d", &x, &y) == 2) hle_input_mouse(WM_MOUSEMOVE, x, y);
        else if (!strncmp(end, "type ", 5)) {
            for (const char *c = end + 5; *c; c++) {
                uint32_t vk = (uint32_t)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
                hle_input_key(1, vk, 0); hle_input_key(0, vk, 0);
            }
        }
        else if (sscanf(end, "key %d", &x) == 1) { hle_input_key(1, (uint32_t)x, 0); hle_input_key(0, (uint32_t)x, 0); }
        else if (sscanf(end, "peek %x", (unsigned *)&x) == 1)
            fprintf(stderr, "[script] %lu: [%08X] = %08X\n", at, (unsigned)x, MEM32((uint32_t)x));
        else if (!strncmp(end, "win", 3)) {
            int l = 0, t = 0, r = 0, b = 0;
            uint32_t h = hle_first_hwnd();
            hle_window_screen_rect(h, &l, &t, &r, &b);
            fprintf(stderr, "[script] window %08X: %d,%d - %d,%d style %08X, at 320,240: %08X\n", h, l, t, r, b,
                    hle_window_style(h), hle_window_at(320, 240, NULL, NULL));
        }
        else if (!strncmp(end, "close", 5)) hle_post_message(hle_first_hwnd(), 0x0010u /* WM_CLOSE */, 0, 0);   /* the window's close button */
        else if (!strncmp(end, "quit", 4)) { fprintf(stderr, "[script] quit\n"); hle_exit(0); }
        else if (*end) fprintf(stderr, "[script] not understood: %s\n", end);
        g_next = stop ? stop + 1 : NULL;
        while (g_next && *g_next == ' ') g_next++;
    }
}
