/*
 * pad.c - a game controller as the mouse, and which kind of input the player
 * is using (a desk's mouse and keys, a touch screen, a pad), which is what
 * decides whether the keyboard on the screen is offered (osk.c).
 *
 * The game knows only a mouse and a few keys, so the pad becomes those:
 *
 *   left stick, the cross   move the pointer (the stick by how far it is
 *                           pushed, the cross faster the longer it is held)
 *   right stick             move it slowly, for small targets
 *   A, right trigger        the left mouse button, down and up as held
 *   B                       Esc (Backspace while the keyboard is open)
 *   Start                   Enter: stops a movie, continues from the book
 *   Back                    full screen, as F11
 *   Y                       the keyboard on the screen, open and closed
 *   X                       its Shift
 *
 * The presenter's pump takes every SDL event and drops the ones it has no
 * use for, a controller's among them, so the pad is read as state
 * (hle_screen_poll_hook) and looked for once a second rather than waited for.
 * A touch screen needs nothing here to work - SDL makes a finger a mouse -
 * but the pump drops the finger events that tell the two apart: an event
 * watch sees them first.
 *
 * Time is the game's clock, like the test script's, whose `pad` verb puts
 * its presses into the same state the controller's go into.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL2/SDL.h>
#include "win32hle.h"
#include "madeline.h"

#define WM_MOUSEMOVE   0x0200u
#define WM_LBUTTONDOWN 0x0201u
#define WM_LBUTTONUP   0x0202u

enum { P_A, P_B, P_X, P_Y, P_START, P_BACK, P_UP, P_DOWN, P_LEFT, P_RIGHT, P_N };
static const char *const k_names[P_N] = { "a", "b", "x", "y", "start", "back", "up", "down", "left", "right" };
typedef struct { uint32_t buttons; float lx, ly, rx, ry; } pad_t;
static pad_t g_sdl, g_script;
static uint32_t g_until[P_N], g_stick_until, g_rstick_until;   /* when the script lets go */
static SDL_GameController *g_ctl;
static int g_on;

int madeline_input_device = DEV_DESK;

/* ---- the controller ---- */
static float axis(SDL_GameControllerAxis a) { return (float)SDL_GameControllerGetAxis(g_ctl, a) / 32767.0f; }
static void poll(void) {
    static uint32_t looked;
    static int looked_once;
    uint32_t t = SDL_GetTicks();
    if (g_ctl && !SDL_GameControllerGetAttached(g_ctl)) {
        fprintf(stderr, "[pad] controller gone\n");
        SDL_GameControllerClose(g_ctl);
        g_ctl = NULL;
        memset(&g_sdl, 0, sizeof g_sdl);
    }
    if (!g_ctl && (!looked_once || t - looked >= 1000)) {
        looked = t, looked_once = 1;
        for (int i = 0; i < SDL_NumJoysticks() && !g_ctl; i++)
            if (SDL_IsGameController(i) && (g_ctl = SDL_GameControllerOpen(i)) != NULL)
                fprintf(stderr, "[pad] %s\n", SDL_GameControllerName(g_ctl) ? SDL_GameControllerName(g_ctl) : "a controller");
    }
    if (!g_ctl) return;
    static const SDL_GameControllerButton k_sdl[P_N] = {
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_DPAD_UP,
        SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
    g_sdl.buttons = 0;
    for (int i = 0; i < P_N; i++) if (SDL_GameControllerGetButton(g_ctl, k_sdl[i])) g_sdl.buttons |= 1u << i;
    if (axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.5f) g_sdl.buttons |= 1u << P_A;
    g_sdl.lx = axis(SDL_CONTROLLER_AXIS_LEFTX), g_sdl.ly = axis(SDL_CONTROLLER_AXIS_LEFTY);
    g_sdl.rx = axis(SDL_CONTROLLER_AXIS_RIGHTX), g_sdl.ry = axis(SDL_CONTROLLER_AXIS_RIGHTY);
}

/* Which input the player has in their hands, from the events the pump is
 * about to take. A finger also arrives as a mouse button (SDL's, marked as
 * such, or the system's own, which is not): a mouse click just after a touch
 * is that touch. The F keys are the host's and say nothing. */
static int watch(void *user, SDL_Event *e) {
    static uint32_t touched;
    static int touched_once;
    (void)user;
    if (e->type == SDL_FINGERDOWN) touched = SDL_GetTicks(), touched_once = 1, madeline_input_device = DEV_TOUCH;
    else if (e->type == SDL_MOUSEBUTTONDOWN) {
        if (e->button.which != SDL_TOUCH_MOUSEID && !(touched_once && SDL_GetTicks() - touched < 700)) madeline_input_device = DEV_DESK;
    }
    else if (e->type == SDL_KEYDOWN) {
        SDL_Scancode sc = e->key.keysym.scancode;
        if (sc < SDL_SCANCODE_F1 || sc > SDL_SCANCODE_F12) madeline_input_device = DEV_DESK;
    }
    return 0;
}

void madeline_pad_init(int on) {
    g_on = on;
    if (madeline_headless) return;                       /* no SDL window: the script is the only pad */
    SDL_AddEventWatch(watch, NULL);
    if (!on) return;
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "[pad] SDL: %s\n", SDL_GetError()); return; }
    hle_screen_poll_hook = poll;
}

/* ---- the test script: `pad a`, `pad left 500`, `pad stick 100 0 300` (percent), `pad rstick ...` ---- */
void madeline_pad_script(const char *s, uint32_t now) {
    char name[16];
    int x = 0, y = 0, ms = 80;
    if (sscanf(s, "stick %d %d %d", &x, &y, &ms) == 3) g_script.lx = (float)x / 100, g_script.ly = (float)y / 100, g_stick_until = now + (uint32_t)ms;
    else if (sscanf(s, "rstick %d %d %d", &x, &y, &ms) == 3) g_script.rx = (float)x / 100, g_script.ry = (float)y / 100, g_rstick_until = now + (uint32_t)ms;
    else if (sscanf(s, "%15s %d", name, &ms) >= 1) {
        for (char *c = name; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
        for (int i = 0; i < P_N; i++)
            if (!strcmp(name, k_names[i])) { g_script.buttons |= 1u << i, g_until[i] = now + (uint32_t)ms; return; }
        fprintf(stderr, "[script] pad: no button called %s\n", name);
    }
}
static void script_tick(uint32_t now) {
    for (int i = 0; i < P_N; i++)
        if ((g_script.buttons & 1u << i) && now >= g_until[i]) g_script.buttons &= ~(1u << i);
    if ((g_script.lx != 0 || g_script.ly != 0) && now >= g_stick_until) g_script.lx = g_script.ly = 0;
    if ((g_script.rx != 0 || g_script.ry != 0) && now >= g_rstick_until) g_script.rx = g_script.ry = 0;
}

/* ---- the pad as a mouse ---- */
#define DEAD 0.22f                           /* a stick at rest is not quite at zero */
/* A stick's push as a speed in pixels a second: nothing inside the dead
 * middle, then slow for most of the way and fast at the rim. */
static void stick(float x, float y, float slow, float fast, float *vx, float *vy) {
    float m = sqrtf(x * x + y * y);
    if (m < DEAD) return;
    float f = m > 1 ? 1 : (m - DEAD) / (1 - DEAD);
    float v = slow + (fast - slow) * f * f;
    *vx += x / m * v, *vy += y / m * v;
}
static void tap(uint32_t vk, uint32_t scan) { hle_input_key(1, vk, scan); hle_input_key(0, vk, scan); }

void madeline_pad_tick(uint32_t now) {
    static uint32_t last, was, cross_since, stepped;
    static float fx, fy;
    static int at_x = -1, at_y = -1;
    uint32_t dt = now - last;
    last = now;
    if (!g_on) return;
    script_tick(now);
    uint32_t held = g_sdl.buttons | g_script.buttons, down = held & ~was, up = was & ~held;
    was = held;
    float lx = g_sdl.lx + g_script.lx, ly = g_sdl.ly + g_script.ly, rx = g_sdl.rx + g_script.rx, ry = g_sdl.ry + g_script.ry;
    float vx = 0, vy = 0;
    stick(lx, ly, 40, 600, &vx, &vy);
    stick(rx, ry, 20, 110, &vx, &vy);

    /* the cross: key to key on the keyboard, else the pointer, gathering speed */
    uint32_t cross = held & (1u << P_UP | 1u << P_DOWN | 1u << P_LEFT | 1u << P_RIGHT);
    int cx = !!(held & 1u << P_RIGHT) - !!(held & 1u << P_LEFT), cy = !!(held & 1u << P_DOWN) - !!(held & 1u << P_UP);
    if (!cross) cross_since = 0;
    else {
        int fresh = !cross_since || (down & cross);
        if (!cross_since) cross_since = now ? now : 1;
        if (madeline_osk_open()) {                       /* a step when pressed, then repeating while held */
            if (fresh || (now - cross_since >= 400 && now - stepped >= 130)) { stepped = now; madeline_osk_step(cx, cy); }
        } else {
            uint32_t t = now - cross_since;
            float v = t < 250 ? 90 : t > 900 ? 420 : 90 + 330 * (float)(t - 250) / 650;
            vx += (float)cx * v, vy += (float)cy * v;
        }
    }

    if (held || vx != 0 || vy != 0) madeline_input_device = DEV_PAD;
    int x, y;
    hle_input_position(&x, &y);
    if (vx != 0 || vy != 0) {
        if (x != at_x || y != at_y) fx = (float)x, fy = (float)y;        /* something else moved it: carry on from there */
        if (dt > 250) dt = 250;                                           /* a long wait is not a long push */
        fx += vx * (float)dt / 1000, fy += vy * (float)dt / 1000;
        if (fx < 0) fx = 0;
        if (fy < 0) fy = 0;
        if (fx > 639) fx = 639;
        if (fy > 479) fy = 479;
        if ((int)fx != x || (int)fy != y) hle_input_mouse(WM_MOUSEMOVE, x = (int)fx, y = (int)fy);
        at_x = x, at_y = y;
    }
    if (down & 1u << P_A) hle_input_mouse(WM_LBUTTONDOWN, x, y);
    if (up & 1u << P_A) hle_input_mouse(WM_LBUTTONUP, x, y);
    if (down & 1u << P_B) { if (madeline_osk_open()) tap(0x08, 0x0E); else tap(0x1B, 0x01); }
    if (down & 1u << P_START) tap(0x0D, 0x1C);
    if (down & 1u << P_Y) madeline_osk_toggle();
    if (down & 1u << P_X) madeline_osk_shift();
    if ((down & 1u << P_BACK) && !madeline_headless) {
        int bars, full;
        hle_screen_settings(&bars, &full);
        hle_screen_fullscreen(!full);
    }
}
