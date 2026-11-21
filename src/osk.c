/*
 * osk.c - a keyboard on the screen, for a player who has none: a gamepad or a
 * touch screen (pad.c says which the player is using).
 *
 * The game wants typing in three places (docs/NOTES.md, "Keys"): the sign-in
 * book (screen 39), the Word Game (34) and screen 14. In the book there is
 * nothing to press but real keys, so the keyboard opens there by itself. The
 * Word Game draws a keyboard of its own, so nothing is shown unless asked for
 * (Y on the pad). Screen 14 has not been seen: it gets a small tab to open
 * the keyboard with.
 *
 * It is drawn by the host over the finished picture (gfx.c asks for it
 * whenever it has redrawn part of the screen), with letters from the small
 * font below: nothing of it is the game's. A press on it is taken out of the
 * game's messages (madeline_osk_message, from the PeekMessageA shim) and
 * becomes the key, pressed as a real one would be.
 */
#include <string.h>
#include "win32hle.h"
#include "madeline.h"

#define WM_MOUSEMOVE     0x0200u
#define WM_LBUTTONDOWN   0x0201u
#define WM_LBUTTONUP     0x0202u
#define WM_LBUTTONDBLCLK 0x0203u

#define SCR_W   640
#define SCR_H   480
#define COLS    11
#define ROWS    4
#define KEY_W   56                           /* a key's cell; the key is drawn 2 inside it */
#define KEY_H   46
#define PANEL_X 8
#define PANEL_W (COLS * KEY_W + 8)
#define PANEL_H (ROWS * KEY_H + 8)
/* Low leaves the top of the screen (the book's NAME line) and the bottom
 * (its red arrow) in view; high is for a screen where that is the wrong half. */
#define PANEL_LOW  196
#define PANEL_HIGH 4
#define TAB_X 8
#define TAB_Y 436
#define TAB_W 56
#define TAB_H 40

/* Keys that are not characters: values no virtual key that is typed has. */
enum { K_HIDE = 1, K_FLIP = 2, K_SHIFT = 3 };
#define VK_BACK   0x08u
#define VK_RETURN 0x0Du
/* What the name line takes: letters, digits, Space, '.', Backspace, Enter. */
static const char *const k_rows[ROWS] = { "1234567890\b", "QWERTYUIOP.", "^ASDFGHJKL\r", "v~ZXCVBNM  " };
typedef struct { uint8_t what, col, row, span; } okey_t;
static okey_t g_keys[COLS * ROWS];
static int g_nkeys;

enum { M_NONE, M_TAB, M_OPEN };
typedef struct { int mode, high, shift, hover, flash; } look_t;
static look_t g_look;                        /* as drawn */
static int g_user = -1;                      /* the player's choice on this screen: -1 none, 0 closed, 1 open */
static int g_high, g_shift, g_flash = -1, g_eat_up;
static uint32_t g_flash_until, g_screen = ~0u, g_ready_since;

/* ---- the font: 5x7, one string of 35 per character ---- */
static const char *const k_font[] = {
    /* A */ "01110100011000111111100011000110001", /* B */ "11110100011000111110100011000111110",
    /* C */ "01110100011000010000100001000101110", /* D */ "11110100011000110001100011000111110",
    /* E */ "11111100001000011110100001000011111", /* F */ "11111100001000011110100001000010000",
    /* G */ "01110100011000010111100011000101111", /* H */ "10001100011000111111100011000110001",
    /* I */ "01110001000010000100001000010001110", /* J */ "00111000100001000010000101001001100",
    /* K */ "10001100101010011000101001001010001", /* L */ "10000100001000010000100001000011111",
    /* M */ "10001110111010110101100011000110001", /* N */ "10001110011010110011100011000110001",
    /* O */ "01110100011000110001100011000101110", /* P */ "11110100011000111110100001000010000",
    /* Q */ "01110100011000110001101011001001101", /* R */ "11110100011000111110101001001010001",
    /* S */ "01111100001000001110000010000111110", /* T */ "11111001000010000100001000010000100",
    /* U */ "10001100011000110001100011000101110", /* V */ "10001100011000110001100010101000100",
    /* W */ "10001100011000110101101011101110001", /* X */ "10001100010101000100010101000110001",
    /* Y */ "10001100010101000100001000010000100", /* Z */ "11111000010001000100010001000011111",
    /* 0 */ "01110100011001110101110011000101110", /* 1 */ "00100011000010000100001000010001110",
    /* 2 */ "01110100010000100010001000100011111", /* 3 */ "11111000100010000010000011000101110",
    /* 4 */ "00010001100101010010111110001000010", /* 5 */ "11111100001111000001000011000101110",
    /* 6 */ "00110010001000011110100011000101110", /* 7 */ "11111000010001000100010000100001000",
    /* 8 */ "01110100011000101110100011000101110", /* 9 */ "01110100011000101111000010001001100",
    /* . */ "00000000000000000000000000110001100",
};
/* The other keys' signs, drawn the same way: rows of any width. */
static const char *const k_back[]  = { "..#......", ".##......", "#########", ".##......", "..#......", NULL };
static const char *const k_enter[] = { "........#", "........#", "..#.....#", ".##.....#", "#########", ".##......", "..#......", NULL };
static const char *const k_shift[] = { "...#...", "..###..", ".#####.", "#######", "..###..", "..###..", "..###..", NULL };
static const char *const k_hide[]  = { "#.....#", "##...##", ".##.##.", "..###..", "...#...", NULL };
static const char *const k_flip[]  = { "...#...", "..###..", ".#.#.#.", "...#...", ".#.#.#.", "..###..", "...#...", NULL };
static const char *const k_space[] = { "#.........#", "#.........#", "###########", NULL };
static const char *const k_board[] = { "###########", "#.........#", "#.#.#.#.#.#", "#.........#", "#..#####..#", "#.........#", "###########", NULL };

/* ---- drawing, into the rectangle gfx.c is redrawing ---- */
static uint32_t *g_px;
static int g_cx0, g_cy0, g_cx1, g_cy1;
static void fill(int x0, int y0, int x1, int y1, uint32_t c, int alpha) {   /* alpha: 0..256 of c over what is there */
    if (x0 < g_cx0) x0 = g_cx0;
    if (y0 < g_cy0) y0 = g_cy0;
    if (x1 > g_cx1) x1 = g_cx1;
    if (y1 > g_cy1) y1 = g_cy1;
    for (int y = y0; y < y1; y++) {
        uint32_t *p = g_px + y * SCR_W;
        for (int x = x0; x < x1; x++) {
            if (alpha >= 256) { p[x] = c; continue; }
            uint32_t rb = ((p[x] & 0xFF00FFu) * (uint32_t)(256 - alpha) + (c & 0xFF00FFu) * (uint32_t)alpha) >> 8 & 0xFF00FFu;
            uint32_t g = ((p[x] & 0x00FF00u) * (uint32_t)(256 - alpha) + (c & 0x00FF00u) * (uint32_t)alpha) >> 8 & 0x00FF00u;
            p[x] = rb | g;
        }
    }
}
static void box(int x, int y, int w, int h, uint32_t c, int alpha) {        /* a rectangle with its corners taken off */
    fill(x + 2, y, x + w - 2, y + 1, c, alpha);
    fill(x + 1, y + 1, x + w - 1, y + 2, c, alpha);
    fill(x, y + 2, x + w, y + h - 2, c, alpha);
    fill(x + 1, y + h - 2, x + w - 1, y + h - 1, c, alpha);
    fill(x + 2, y + h - 1, x + w - 2, y + h, c, alpha);
}
static void sign(const char *const *rows, int cx, int cy, int s, uint32_t c) {   /* centred on cx, cy */
    int n = 0, w = (int)strlen(rows[0]);
    while (rows[n]) n++;
    int x0 = cx - w * s / 2, y0 = cy - n * s / 2;
    for (int j = 0; j < n; j++)
        for (int i = 0; i < w; i++)
            if (rows[j][i] == '#') fill(x0 + i * s, y0 + j * s, x0 + (i + 1) * s, y0 + (j + 1) * s, c, 256);
}
static void letter(char ch, int cx, int cy, int s, uint32_t c) {
    int i = ch >= 'A' && ch <= 'Z' ? ch - 'A' : ch >= '0' && ch <= '9' ? 26 + ch - '0' : ch == '.' ? 36 : -1;
    if (i < 0) return;
    int x0 = cx - 5 * s / 2, y0 = cy - 7 * s / 2;
    for (int k = 0; k < 35; k++)
        if (k_font[i][k] == '1') fill(x0 + k % 5 * s, y0 + k / 5 * s, x0 + (k % 5 + 1) * s, y0 + (k / 5 + 1) * s, c, 256);
}

static int panel_y(int high) { return high ? PANEL_HIGH : PANEL_LOW; }
static void key_rect(const okey_t *k, int high, int *x, int *y, int *w, int *h) {
    *x = PANEL_X + 4 + k->col * KEY_W + 2, *y = panel_y(high) + 4 + k->row * KEY_H + 2;
    *w = k->span * KEY_W - 4, *h = KEY_H - 4;
}
static int key_at(int px, int py) {
    for (int i = 0; i < g_nkeys; i++) {
        int x, y, w, h;
        key_rect(&g_keys[i], g_look.high, &x, &y, &w, &h);
        if (px >= x - 2 && px < x + w + 2 && py >= y - 2 && py < y + h + 2) return i;   /* its whole cell: no dead lines between keys */
    }
    return -1;
}
static void region(const look_t *l, int *x0, int *y0, int *x1, int *y1) {
    if (l->mode == M_OPEN) *x0 = PANEL_X, *y0 = panel_y(l->high), *x1 = PANEL_X + PANEL_W, *y1 = *y0 + PANEL_H;
    else if (l->mode == M_TAB) *x0 = TAB_X, *y0 = TAB_Y, *x1 = TAB_X + TAB_W, *y1 = TAB_Y + TAB_H;
    else *x0 = *y0 = *x1 = *y1 = 0;
}

#define INK    0x2B2B3Cu
#define CREAM  0xFFF7DEu
#define YELLOW 0xFFD23Fu                     /* the key the pointer is on */
#define ORANGE 0xF58A1Fu                     /* the key just pressed */
void madeline_osk_paint(uint32_t *px, int x0, int y0, int x1, int y1) {
    int rx0, ry0, rx1, ry1;
    region(&g_look, &rx0, &ry0, &rx1, &ry1);
    if (g_look.mode == M_NONE || x0 >= rx1 || x1 <= rx0 || y0 >= ry1 || y1 <= ry0) return;
    g_px = px, g_cx0 = x0, g_cy0 = y0, g_cx1 = x1, g_cy1 = y1;
    if (g_look.mode == M_TAB) {
        box(TAB_X, TAB_Y, TAB_W, TAB_H, INK, 150);
        box(TAB_X + 3, TAB_Y + 3, TAB_W - 6, TAB_H - 6, g_look.hover == 0 ? YELLOW : CREAM, 256);
        sign(k_board, TAB_X + TAB_W / 2, TAB_Y + TAB_H / 2, 3, INK);
        return;
    }
    box(rx0, ry0, PANEL_W, PANEL_H, INK, 150);
    for (int i = 0; i < g_nkeys; i++) {
        const okey_t *k = &g_keys[i];
        int x, y, w, h;
        key_rect(k, g_look.high, &x, &y, &w, &h);
        if (x >= x1 || x + w <= x0 || y >= y1 || y + h <= y0) continue;
        uint32_t face = i == g_look.flash ? ORANGE : i == g_look.hover || (k->what == K_SHIFT && g_look.shift) ? YELLOW : CREAM;
        box(x, y, w, h, 0x8A8470u, 256);                 /* the key's edge, seen along its bottom */
        box(x, y, w, h - 3, face, 256);
        int cx = x + w / 2, cy = y + (h - 3) / 2;
        switch (k->what) {
        case VK_BACK:   sign(k_back, cx, cy, 3, INK); break;
        case VK_RETURN: sign(k_enter, cx, cy, 3, INK); break;
        case K_SHIFT:   sign(k_shift, cx, cy, 3, INK); break;
        case K_HIDE:    sign(k_hide, cx, cy, 3, INK); break;
        case K_FLIP:    sign(k_flip, cx, cy, 3, INK); break;
        case ' ':       sign(k_space, cx, cy + 4, 3, INK); break;
        default:        letter((char)k->what, cx, cy, 3, INK); break;
        }
    }
}

static void redraw(const look_t *was) {
    int x0, y0, x1, y1;
    region(was, &x0, &y0, &x1, &y1);
    if (x1 > x0) madeline_gfx_redraw(x0, y0, x1, y1);
    if (was->mode == g_look.mode && was->high == g_look.high) return;
    region(&g_look, &x0, &y0, &x1, &y1);
    if (x1 > x0) madeline_gfx_redraw(x0, y0, x1, y1);
}

static void build(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            char ch = k_rows[r][c];
            uint8_t what = ch == '^' ? K_SHIFT : ch == 'v' ? K_HIDE : ch == '~' ? K_FLIP : (uint8_t)ch;
            if (c && k_rows[r][c - 1] == ch) { g_keys[g_nkeys - 1].span++; continue; }   /* Space is two cells wide */
            g_keys[g_nkeys++] = (okey_t){ what, (uint8_t)c, (uint8_t)r, 1 };
        }
}

static void press(int i) {
    const okey_t *k = &g_keys[i];
    g_flash = i, g_flash_until = (uint32_t)(madeline_clock_ns() / 1000000u) + 150;
    switch (k->what) {
    case K_HIDE:  g_user = 0; break;
    case K_FLIP:  g_high = !g_high; break;
    case K_SHIFT: g_shift = !g_shift; break;
    case '.':     madeline_type(0xBE, 0); break;                   /* VK_OEM_PERIOD */
    default:                                             /* a letter is small unless Shift was pressed first, as on real keys */
        madeline_type(k->what, g_shift && k->what >= 'A' && k->what <= 'Z');
        if (k->what >= 'A' && k->what <= 'Z') g_shift = 0;
        break;
    }
}

int madeline_osk_open(void) { return g_look.mode == M_OPEN; }
void madeline_osk_toggle(void) { g_user = g_look.mode == M_OPEN ? 0 : 1; }
void madeline_osk_shift(void) { if (g_look.mode == M_OPEN) g_shift = !g_shift; }

/* A mouse message the game is about to be given: 1 if it was for the keyboard
 * (the shim then gives the game nothing in its place). */
int madeline_osk_message(uint32_t msg, int x, int y) {
    if (msg == WM_LBUTTONUP && g_eat_up) { g_eat_up = 0; return 1; }
    if (msg != WM_LBUTTONDOWN && msg != WM_LBUTTONDBLCLK) return 0;
    int x0, y0, x1, y1;
    region(&g_look, &x0, &y0, &x1, &y1);
    if (x < x0 || x >= x1 || y < y0 || y >= y1) return 0;
    g_eat_up = 1;
    if (g_look.mode == M_TAB) g_user = 1;
    else { int i = key_at(x, y); if (i >= 0) press(i); }
    return 1;
}

/* The pad's cross, while the keyboard is open: the pointer goes from key to
 * key. 0 if the keyboard is not open (the cross then moves the pointer freely). */
int madeline_osk_step(int dx, int dy) {
    if (g_look.mode != M_OPEN) return 0;
    int px, py, x, y, w, h;
    hle_input_position(&px, &py);
    int from = key_at(px, py), to = from;
    if (from < 0) to = 11;                               /* not on it yet: start at Q */
    else {
        int c = (px - (PANEL_X + 4)) / KEY_W, r = g_keys[from].row;
        while (to == from) {
            c += dx, r += dy;
            if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return 1;      /* the edge: stay */
            for (int i = 0; i < g_nkeys; i++)
                if (g_keys[i].row == r && c >= g_keys[i].col && c < g_keys[i].col + g_keys[i].span) to = i;
        }
    }
    key_rect(&g_keys[to], g_look.high, &x, &y, &w, &h);
    /* up and down keep the column the pointer is in, across the wide key */
    int tx = dy && from >= 0 && px >= x && px < x + w ? px : x + w / 2;
    hle_input_mouse(WM_MOUSEMOVE, tx, y + h / 2);
    return 1;
}

/* Called while the game idles: is the keyboard wanted now, and does it look as drawn? */
void madeline_osk_tick(uint32_t now) {
    if (!g_nkeys) build();
    uint32_t screen = MEM32(G_SCREEN) & 0xFFFFu;
    if (screen != g_screen) g_screen = screen, g_user = -1, g_shift = 0, g_high = 0, g_ready_since = 0;
    int text = screen == SCREEN_SIGN_IN || screen == SCREEN_WORD_GAME || screen == SCREEN_14;
    int ready = madeline_input_device != DEV_DESK && text
                && MEM32(G_INPUT_ENABLED) && !MEM32(G_MOVIE_PLAYING) && hle_cursor_shown();
    if (!ready) g_ready_since = 0;
    else if (!g_ready_since) g_ready_since = now ? now : 1;
    look_t want = { M_NONE, g_high, g_shift, -1, -1 };
    if (ready && now - g_ready_since >= 400) {           /* the screen is up and waiting, not passing through */
        if (g_user == 1 || (g_user < 0 && screen == SCREEN_SIGN_IN)) want.mode = M_OPEN;
        else if (screen != SCREEN_WORD_GAME) want.mode = M_TAB;          /* it has keys of its own to press */
    }
    if (g_flash >= 0 && now >= g_flash_until) g_flash = -1;
    look_t was = g_look;
    g_look.mode = want.mode, g_look.high = want.high;    /* key_at measures the keyboard as it will be drawn */
    int px, py;
    hle_input_position(&px, &py);
    if (want.mode == M_OPEN) want.hover = key_at(px, py), want.flash = g_flash;
    else if (want.mode == M_TAB) want.hover = px >= TAB_X && px < TAB_X + TAB_W && py >= TAB_Y && py < TAB_Y + TAB_H ? 0 : -1;
    g_look = want;
    if (memcmp(&was, &g_look, sizeof was)) redraw(&was);
}
