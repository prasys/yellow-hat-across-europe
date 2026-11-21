/*
 * shims.c - this game's own answers to a few Win32 calls, registered ahead of
 * win32hle's (the first registration of a name wins).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#include "win32hle.h"
#include "madeline.h"

/* The game looks for its disc by asking every drive letter what it is and
 * taking the first CD-ROM that has \MAD000\SCRN000.PCX. D: is the disc. */
static void m_GetDriveTypeA(void) {
    const char *r = ASTR(0);
    char c = r && r[0] ? (char)(r[0] & ~0x20) : 'C';
    RET(c == 'D' ? 5u /* DRIVE_CDROM */ : c == 'C' ? 3u /* DRIVE_FIXED */ : 1u, 1);
}

/* The registry. SETUP.EXE leaves exactly one thing there: the folder it
 * installed into, as the default value of
 * HKEY_CLASSES_ROOT\CreativeWonders\MadelineEuropeanAdventures. The game opens
 * the two keys in turn and reads that value with the Win16-era RegQueryValueA
 * (hkey, subkey, buffer, &size), which win32hle does not have; these three are
 * all it calls, so they are answered together. */
#define INSTALL_DIR "C:\\CWONDERS\\MADEURO"
static void m_RegOpenKeyA(void) {
    uint32_t out = A32(2);
    if (out) MEM32(out) = 0x00CE0001u;
    RET(0, 3);
}
static void m_RegQueryValueA(void) {
    uint32_t buf = A32(2), pcb = A32(3);
    const uint32_t need = sizeof INSTALL_DIR;
    if (buf && pcb && MEM32(pcb) < need) { MEM32(pcb) = need; RET(234u /* ERROR_MORE_DATA */, 4); }
    if (buf) memcpy(hle_ptr(buf), INSTALL_DIR, need);
    if (pcb) MEM32(pcb) = need;
    RET(0, 4);
}
static void m_RegCloseKey(void) { RET(0, 1); }

/* The clock. The game paces everything - a movie's frames, a fade, a wait
 * for a sound - by spinning on timeGetTime until enough has passed, as a 1996
 * game with the machine to itself would. Asked again within the same
 * millisecond, this sleeps out the rest of it instead of burning a core.
 *
 * MADELINE_SPEED=8 runs the game's clock eight times faster than the wall's
 * (and silently): for tests, which would otherwise sit through the opening. */
static double g_speed;
double madeline_speed(void) {
    if (g_speed == 0) { const char *e = getenv("MADELINE_SPEED"); g_speed = e && atof(e) > 0 ? atof(e) : 1.0; }
    return g_speed;
}
uint64_t madeline_clock_ns(void) {
    static uint64_t t0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t t = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    if (!t0) t0 = t - 1000000000ull;                     /* never 0: timeGetTime's 0 reads as "not yet" */
    return (uint64_t)((double)(t - t0) * madeline_speed());
}
/* Skipping the opening. The game already lets a key stop the movie that is
 * playing (any key but Space, in its WM_CHAR handler), so the two opening
 * movies take two key presses. With skip_intro set the host makes them: while
 * the start-up sequence is still running and a movie is on, it presses Enter.
 * Nothing in the game is changed, and later movies are left alone.
 *
 * The two addresses are this build's (docs/MADEURO.sha256): the start-up
 * step counter (0, then 1 and 2 as each movie starts) and "a movie is
 * playing". On another build the option does nothing useful - check them. */
#define G_INTRO_STEP    0x0041E1CCu
int madeline_skip_intro;
static void skip_intro_tick(uint32_t now) {
    static uint32_t pressed_at;
    if (!madeline_skip_intro) return;
    if (!MEM32(G_MOVIE_PLAYING)) {
        if (MEM32(G_INTRO_STEP) >= 2) madeline_skip_intro = 0;    /* both are over: done for this run */
        return;
    }
    if (now - pressed_at < 150) return;                           /* one press, then let the game take it */
    pressed_at = now;
    hle_input_key(1, 0x0D, 0x1C);
    hle_input_key(0, 0x0D, 0x1C);
}

/* A key pressed for the player (a remembered name, the keyboard on the
 * screen). A capital needs Shift, and win32hle decides the letter's case when
 * the game translates the key press, from Shift as it is at that moment, not
 * as it was when the key was queued: Shift pressed and let go around the key
 * all at once gives a small letter. So Shift stays down until the game has
 * taken the key's release out of its queue (the PeekMessageA shim sees it
 * go), or for three seconds if it never does. */
#define WM_KEYUP 0x0101u
static uint32_t g_shift_for, g_shift_at;                 /* the key Shift is held for, and since when */
void madeline_type(uint32_t vk, int shifted) {
    if (shifted && !g_shift_for) hle_input_key(1, 0x10, 0);
    hle_input_key(1, vk, 0);
    hle_input_key(0, vk, 0);
    if (shifted) g_shift_for = vk, g_shift_at = (uint32_t)(madeline_clock_ns() / 1000000u);
}
static void shift_release(void) { g_shift_for = 0; hle_input_key(0, 0x10, 0); }

/* Remembering the player. The sign-in book only ever shows an empty NAME
 * line: the game finds a saved game by the exact name typed, and never lists
 * the names it has. So that carrying on does not depend on spelling a name
 * the same way twice, the host types the most recently saved name into the
 * book when it opens (once per run). It is ordinary typing: Backspace takes
 * it out again. remember_name=0 in madeline.ini turns this off.
 *
 * A save starts with the name (NUL-terminated, 16 bytes); a non-zero word at
 * 0xF0 marks one the game will discard. */
int madeline_remember_name = 1;
char madeline_save_dir[1024];            /* host path of the folder the .sav files are in */
static int last_player(char *name, size_t n) {
    time_t newest = 0;
    name[0] = 0;
    for (int i = 0; i < 10; i++) {
        char path[1100];
        unsigned char head[0xF2];
        struct stat st;
        snprintf(path, sizeof path, "%s/%d.sav", madeline_save_dir, i);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        size_t got = fread(head, 1, sizeof head, f);
        fclose(f);
        if (got != sizeof head || stat(path, &st) != 0 || head[0xF0] || head[0xF1] || !head[0]) continue;
        if (st.st_mtime >= newest) { newest = st.st_mtime; head[15] = 0; snprintf(name, n, "%s", (const char *)head); }
    }
    return name[0] != 0;
}
/* One letter at a time, each checked against the game's own copy of the name
 * (the start of the player record G_PLAYER points at) before the next: a key
 * that arrives while the book is still asking "what's your name?" is dropped,
 * so a letter is pressed again if it has not shown after a few seconds. If what is there stops matching
 * (the player is typing), the host leaves it alone. */
#define G_PLAYER 0x00421D9Cu
static void remember_name_tick(uint32_t now) {
    static char want[16];
    static int have_name, typed = -1;
    static uint32_t ready_since, pressed_at;
    if (!madeline_remember_name) return;
    if ((MEM32(G_SCREEN) & 0xFFFFu) != SCREEN_SIGN_IN || !MEM32(G_INPUT_ENABLED) || MEM32(G_MOVIE_PLAYING) || !MEM32(G_PLAYER)) { ready_since = 0; return; }
    if (!ready_since) { ready_since = now ? now : 1; return; }
    if (now - ready_since < 400) return;                 /* the book is up */
    if (!have_name) {
        have_name = 1;
        if (!last_player(want, sizeof want)) { madeline_remember_name = 0; return; }
    }
    const char *got = (const char *)hle_ptr(MEM32(G_PLAYER));
    size_t n = strnlen(got, 11);
    if (n > strlen(want) || strncmp(got, want, n)) { madeline_remember_name = 0; return; }   /* not ours any more */
    if (!want[n]) { madeline_remember_name = 0; return; }                                      /* all there */
    /* A key is queued, and the book takes about a second over each (it says
     * the letter aloud), so a press is only repeated when it has plainly been lost. */
    if ((int)n == typed && now - pressed_at < 3000) return;
    char c = want[n];
    int upper = c >= 'A' && c <= 'Z';
    uint32_t vk = c >= 'a' && c <= 'z' ? (uint32_t)(c - 32) : upper || (c >= '0' && c <= '9') || c == ' ' ? (uint32_t)c : c == '.' ? 0xBEu : 0;
    if (!vk) { madeline_remember_name = 0; return; }
    madeline_type(vk, upper);
    typed = (int)n, pressed_at = now;
}

/* Both ways the game waits come through here: it spins on timeGetTime, or on
 * PeekMessage with an empty queue (madeline_idle is the message pump's hook).
 * Either way a spin within one millisecond is slept out, and the things that
 * must move while the game waits - sound, the test script - are moved. */
static uint32_t idle_tick(void) {
    static uint32_t last, same;
    uint32_t now = (uint32_t)(madeline_clock_ns() / 1000000u);
    if (now != last) last = now, same = 0;
    else if (++same >= 8) {
        hle_block_begin(); usleep(300); hle_block_end();
        now = (uint32_t)(madeline_clock_ns() / 1000000u);
        if (now != last) last = now, same = 0;
    }
    madeline_audio_tick();
    madeline_debug_tick(now);
    skip_intro_tick(now);
    remember_name_tick(now);
    if (g_shift_for && (int32_t)(now - g_shift_at) >= 3000) shift_release();   /* signed: the press may be a millisecond after this tick's now */
    madeline_pad_tick(now);
    madeline_osk_tick(now);
    madeline_gfx_pointer();
    return now;
}
/* A wait on the clock alone (a movie, a line of speech) does not pump
 * messages, so nothing would read the mouse and the pointer would freeze:
 * the window's events are taken here too, a hundred times a second. */
static void m_timeGetTime(void) {
    static uint32_t pumped;
    uint32_t now = idle_tick();
    if (now - pumped >= 10) { pumped = now; hle_screen_pump(); madeline_gfx_pointer(); }
    RET(now, 0);
}

/* The game makes its window 0 by 0 and has Windows maximize it
 * (ShowWindow(SW_SHOWMAXIMIZED)); win32hle shows it but leaves its size alone,
 * and a window with no area is never under the mouse. Give it the screen. */
static void maximize_window(void) {
    static int done;
    uint32_t h = done ? 0 : hle_first_hwnd();
    int l, t, r, b;
    if (!h || !hle_window_screen_rect(h, &l, &t, &r, &b)) return;
    done = 1;
    if (r - l >= 640 && b - t >= 480) return;
    uint32_t args[6] = { h, 0, 0, 640, 480, 0 };
    hle_call_guest(hle_resolve("MoveWindow"), 6, args);
}
void madeline_idle(void) { hle_screen_pump(); maximize_window(); idle_tick(); }

/* win32hle's own shim for a name this file also answers: the registry keeps
 * every registration and resolves a name to the first, so the stock one is
 * the last. Shim addresses are evenly spaced; the first two give the spacing. */
static uint32_t stock_shim_va(const char *name) {
    int n = hle_shim_total(), found = -1;
    for (int i = 0; i < n; i++) if (!strcmp(hle_shim_name_at(i), name)) found = i;
    if (found < 1) return 0;
    uint32_t va0 = hle_resolve(hle_shim_name_at(0)), va1 = hle_resolve(hle_shim_name_at(1));
    return va0 + (uint32_t)found * (va1 - va0);
}
/* Run it in place of the caller: same arguments on the guest stack, its return.
 * The lookup has to be made for every call (it is what selects the shim). */
static void stock_shim_call(uint32_t va) {
    recomp_func_t fn = va ? recomp_lookup_import(va) : NULL;
    if (!fn) hle_fatal("no stock shim to pass a call on to");
    fn();
}

/* Closing the window. On Windows the game's borderless window is closed with
 * Alt+F4, which arrives as WM_SYSCOMMAND/SC_CLOSE; the game answers that by
 * asking "Exiting this way will NOT save your game... are you sure?". The
 * presenter's close button and Cmd+Q arrive as a bare WM_CLOSE, which the game
 * does not handle, so the window would just go and the game with it, unsaved
 * and unasked. A WM_CLOSE that reaches DefWindowProc is turned back into the
 * message the game knows. (Its own exit destroys the window directly.) */
#define WM_CLOSE      0x0010u
#define WM_SYSCOMMAND 0x0112u
#define SC_CLOSE      0xF060u
static void m_DefWindowProcA(void) {                     /* (hwnd, msg, wparam, lparam) */
    static uint32_t stock;
    if (A32(1) == WM_CLOSE) { hle_send(A32(0), WM_SYSCOMMAND, SC_CLOSE, 0); RET(0, 4); }
    if (!stock) stock = stock_shim_va("DefWindowProcA");
    stock_shim_call(stock);
}

/* The game's messages, watched as it removes them from the queue (PeekMessageA
 * is the only way this game reads them).
 * A press on the host's keyboard (osk.c) is not the game's: the message is
 * taken as the game removes it from the queue, whichever of the mouse, a
 * finger, the pad or a test script made it, and WM_NULL is left in its place
 * (the game's window procedure passes that on to DefWindowProc). */
static void m_PeekMessageA(void) {                       /* (msg, hwnd, min, max, remove) */
    static uint32_t stock;
    uint32_t msg = A32(0), remove = A32(4) & 1;
    if (!stock) stock = stock_shim_va("PeekMessageA");
    stock_shim_call(stock);
    if (!g_eax || !remove || !msg) return;
    if (g_shift_for && MEM32(msg + 4) == WM_KEYUP && MEM32(msg + 8) == g_shift_for) shift_release();
    uint32_t l = MEM32(msg + 12);
    if (madeline_osk_message(MEM32(msg + 4), (int16_t)(l & 0xFFFF), (int16_t)(l >> 16))) MEM32(msg + 4) = 0;
}

/* Message boxes. win32hle prints them and answers Yes/OK; this game's are few
 * and matter (the question above; "CD-ROM missing"), so they are shown. */
int madeline_headless;
static void m_MessageBoxA(void) {                        /* (hwnd, text, caption, type) */
    const char *text = ASTR(1) ? ASTR(1) : "", *caption = ASTR(2) ? ASTR(2) : "Madeline";
    uint32_t type = A32(3) & 0xFu, answer;
    fprintf(stderr, "[MessageBox] %s: %s\n", caption, text);
    answer = type == 4 || type == 3 ? 6u /* IDYES */ : type == 5 ? 4u /* IDRETRY */ : 1u /* IDOK */;
    if (!madeline_headless) {
        static const SDL_MessageBoxButtonData ok[] = { { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "OK" } };
        static const SDL_MessageBoxButtonData okcancel[] = { { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 2, "Cancel" }, { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "OK" } };
        static const SDL_MessageBoxButtonData yesno[] = { { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 7, "No" }, { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 6, "Yes" } };
        static const SDL_MessageBoxButtonData retry[] = { { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 2, "Cancel" }, { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 4, "Retry" } };
        SDL_MessageBoxData box = { SDL_MESSAGEBOX_INFORMATION, hle_screen_sdl_window(), caption, text, 1, ok, NULL };
        if (type == 1) box.numbuttons = 2, box.buttons = okcancel;
        else if (type == 3 || type == 4) box.numbuttons = 2, box.buttons = yesno;
        else if (type == 5) box.numbuttons = 2, box.buttons = retry;
        int hit = 0;
        hle_block_begin();
        int rc = SDL_ShowMessageBox(&box, &hit);
        hle_block_end();
        if (rc == 0 && hit > 0) answer = (uint32_t)hit;
    }
    RET(answer, 4);
}

const win32hle_shim madeline_shims[] = {
    { "DefWindowProcA", m_DefWindowProcA }, { "MessageBoxA", m_MessageBoxA }, { "PeekMessageA", m_PeekMessageA },
    { "timeGetTime", m_timeGetTime },
    { "RegOpenKeyA", m_RegOpenKeyA }, { "RegQueryValueA", m_RegQueryValueA }, { "RegCloseKey", m_RegCloseKey },
    { "GetDriveTypeA", m_GetDriveTypeA },
    { 0, 0 }
};
