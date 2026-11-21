/*
 * gfx.c - the display the game was written for: a 640x480, 256-colour Windows
 * screen with a hardware palette, WinG on top of it.
 *
 * MADEURO.EXE draws every frame into one 8-bit WinG bitmap and WinGBitBlt's it
 * to its window; it fades and cycles colours by changing the palette under
 * pixels already on the screen (AnimatePalette). That only works on a palette
 * device, so this file is one: it keeps the screen as colour indices plus the
 * 256 colours currently "in the hardware", and re-colours the picture whenever
 * either changes. The game's whole GDI import list (19 calls), WING32.DLL's 4
 * and USER32's GetDC/ReleaseDC are answered here (the printer's part of them in
 * print.c); win32hle's truecolour GDI is not involved. The finished picture, with the mouse pointer drawn on it, is
 * handed to win32hle's SDL presenter as the window surface.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "win32hle.h"
#include "madeline.h"

#define SCR_W 640
#define SCR_H 480

static uint8_t  g_idx[SCR_W * SCR_H];        /* the screen, as palette indices */
static uint32_t g_rgb[SCR_W * SCR_H];        /* the same picture in XRGB */
static uint32_t g_soft[SCR_W * SCR_H];       /* the picture with its dithering melted (smoothing on) */
static uint32_t g_out[SCR_W * SCR_H];        /* what the presenter shows: g_soft or g_rgb, with the mouse pointer on it */
static int      g_smooth = 1;
/* upscale=xbr: g_out at twice the size with its edges redrawn (xbr.c), kept
 * up to date for the part of g_out that changed since it was last shown. */
static uint32_t g_big[4 * SCR_W * SCR_H];
static uint16_t g_stand_in[SCR_W * SCR_H];   /* what the presenter is told the screen is, while g_big is shown (madeline_gfx_upscale) */
static int      g_xbr;
static int      g_dx0 = SCR_W, g_dy0 = SCR_H, g_dx1, g_dy1;   /* the changed part of g_out */
static uint32_t g_hw[256];                 /* the hardware palette, 0x00RRGGBB */
static int      g_hw_set;                    /* a palette has been realized */
static int      g_trace;

/* ---- handles ---- */
#define H_SCREEN_DC 0x5C000001u
#define H_WING_DC   0x5C100000u              /* + slot */
#define H_BITMAP    0x5B000000u              /* + slot */
#define H_PALETTE   0x5A000000u              /* + slot */
#define H_STOCK     0x57000000u              /* + GetStockObject's index */
#define H_OLD_BMP   0x57000100u              /* the 1x1 bitmap a new memory DC holds */
#define H_OLD_PAL   (H_STOCK + 15u)          /* DEFAULT_PALETTE */

typedef struct { int used, w, h, pitch, topdown; uint8_t *bits; uint32_t ctab[256]; } bitmap_t;
typedef struct { int used, n; uint32_t c[256]; } palette_t;
typedef struct { int used; uint32_t bmp; } wingdc_t;
#define MAX_BITMAPS  64
#define MAX_PALETTES 64
#define MAX_WINGDCS  16
static bitmap_t  g_bmp[MAX_BITMAPS];
static palette_t g_pal[MAX_PALETTES];
static wingdc_t  g_wdc[MAX_WINGDCS];
static uint32_t  g_screen_pal;               /* the palette selected into the screen DC */

static bitmap_t  *BMP(uint32_t h) { uint32_t i = h - H_BITMAP;  return i < MAX_BITMAPS  && g_bmp[i].used ? &g_bmp[i] : NULL; }
static palette_t *PAL(uint32_t h) { uint32_t i = h - H_PALETTE; return i < MAX_PALETTES && g_pal[i].used ? &g_pal[i] : NULL; }
static wingdc_t  *WDC(uint32_t h) { uint32_t i = h - H_WING_DC; return i < MAX_WINGDCS  && g_wdc[i].used ? &g_wdc[i] : NULL; }

/* ---- the mouse pointer ----
 * The game uses Windows' own pointer: eight 32x32 monochrome cursors in its
 * resources, switched with SetCursor and hidden with ShowCursor during movies.
 * The host draws it into the picture itself rather than hand it to the window
 * system: it is then the right size at any scaling, and looks the same on
 * every platform. */
#define CUR 32
static struct { int shown, x, y; uint32_t handle; } g_ptr;    /* as last drawn */
static const char *const k_arrow[] = {       /* for a cursor that is not one of the game's (IDC_ARROW) */
    "X", "XX", "X.X", "X..X", "X...X", "X....X", "X.....X", "X......X", "X.......X", "X........X",
    "X.....XXXXX", "X..X..X", "X.X X..X", "XX  X..X", "X    X..X", "     X..X", "      XX", NULL };
static void pointer_rect(int *x0, int *y0, int *x1, int *y1) {
    const hle_cursor_image_t *img = hle_cursor_image(g_ptr.handle);
    *x0 = g_ptr.x - (img ? img->hot_x : 0), *y0 = g_ptr.y - (img ? img->hot_y : 0);
    *x1 = *x0 + CUR, *y1 = *y0 + CUR;
}
static void plot(int x, int y, int kind) {   /* 0 black, 1 white, 2 invert */
    if (x < 0 || y < 0 || x >= SCR_W || y >= SCR_H) return;
    uint32_t *p = g_out + y * SCR_W + x;
    *p = kind == 0 ? 0x000000u : kind == 1 ? 0xFFFFFFu : *p ^ 0xFFFFFFu;
}
static void changed(int x0, int y0, int x1, int y1) {
    if (x0 < g_dx0) g_dx0 = x0;
    if (y0 < g_dy0) g_dy0 = y0;
    if (x1 > g_dx1) g_dx1 = x1;
    if (y1 > g_dy1) g_dy1 = y1;
}
static void draw_pointer(void) {
    if (!g_ptr.shown) return;
    int x0, y0, x1, y1;
    pointer_rect(&x0, &y0, &x1, &y1);
    changed(x0, y0, x1, y1);
    const hle_cursor_image_t *img = hle_cursor_image(g_ptr.handle);
    if (!img) {
        for (int j = 0; k_arrow[j]; j++)
            for (int i = 0; k_arrow[j][i]; i++)
                if (k_arrow[j][i] != ' ') plot(x0 + i, y0 + j, k_arrow[j][i] == '.');
        return;
    }
    for (int j = 0; j < CUR; j++)
        for (int i = 0; i < CUR; i++) {
            int and = img->and_mask[j * 4 + i / 8] >> (7 - i % 8) & 1, xor = img->xor_mask[j * 4 + i / 8] >> (7 - i % 8) & 1;
            if (!and) plot(x0 + i, y0 + j, xor);         /* opaque: black or white */
            else if (xor) plot(x0 + i, y0 + j, 2);       /* inverts what is under it */
        }
}
static void copy_out(int x0, int y0, int x1, int y1) {
    const uint32_t *src = g_smooth ? g_soft : g_rgb;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCR_W) x1 = SCR_W;
    if (y1 > SCR_H) y1 = SCR_H;
    changed(x0, y0, x1, y1);
    for (int y = y0; y < y1 && x0 < x1; y++) memcpy(g_out + y * SCR_W + x0, src + y * SCR_W + x0, (size_t)(x1 - x0) * 4);
}

/* ---- smoothing ----
 * The game's pictures are paintings cut down to 256 colours, so every wash of
 * paint is a dither: two or three near colours in a speckle. Scaled up, that
 * speckle is what looks rough. Each pixel is replaced by a weighted average of
 * the pixels around it (5x5) that are close to it in colour - so a speckle
 * melts into the colour it was standing for, while an ink line, which is
 * nothing like its surroundings, averages only with itself and stays sharp.
 * The presenter then scales the result smoothly. Off: the pixels as they are. */
#define SOFT_R      2                        /* neighbourhood radius */
#define SOFT_NEAR   56                       /* how far apart (largest channel difference) two colours of one wash can be */
static void soften(int x0, int y0, int x1, int y1) {
    static const int k_w[5] = { 1, 3, 4, 3, 1 };
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCR_W) x1 = SCR_W;
    if (y1 > SCR_H) y1 = SCR_H;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint32_t c = g_rgb[y * SCR_W + x];
            int cr = (int)(c >> 16 & 0xFF), cg = (int)(c >> 8 & 0xFF), cb = (int)(c & 0xFF);
            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int j = -SOFT_R; j <= SOFT_R; j++) {
                int yy = y + j;
                if (yy < 0 || yy >= SCR_H) continue;
                const uint32_t *row = g_rgb + yy * SCR_W;
                for (int i = -SOFT_R; i <= SOFT_R; i++) {
                    int xx = x + i;
                    if (xx < 0 || xx >= SCR_W) continue;
                    uint32_t p = row[xx];
                    int pr = (int)(p >> 16 & 0xFF), pg = (int)(p >> 8 & 0xFF), pb = (int)(p & 0xFF);
                    int dr = pr - cr, dg = pg - cg, db = pb - cb;
                    if (dr < 0) dr = -dr;
                    if (dg < 0) dg = -dg;
                    if (db < 0) db = -db;
                    if (dr > SOFT_NEAR || dg > SOFT_NEAR || db > SOFT_NEAR) continue;
                    unsigned w = (unsigned)(k_w[j + SOFT_R] * k_w[i + SOFT_R]);
                    r += w * (unsigned)pr, g += w * (unsigned)pg, b += w * (unsigned)pb, n += w;
                }
            }
            g_soft[y * SCR_W + x] = (r / n) << 16 | (g / n) << 8 | (b / n);
        }
}
/* Called while the game idles: follow the mouse, the cursor's shape, and whether it is hidden. */
void madeline_gfx_pointer(void) {
    int x, y, shown = hle_cursor_shown() && !getenv("MADELINE_NOPOINTER");
    uint32_t handle = hle_current_cursor();
    hle_input_position(&x, &y);
    if (shown == g_ptr.shown && (!shown || (x == g_ptr.x && y == g_ptr.y && handle == g_ptr.handle))) return;
    int x0, y0, x1, y1;
    pointer_rect(&x0, &y0, &x1, &y1);
    copy_out(x0, y0, x1, y1);                            /* the picture back where the pointer was */
    g_ptr.shown = shown, g_ptr.x = x, g_ptr.y = y, g_ptr.handle = handle;
    draw_pointer();
    hle_gdi_window_touched();
}

/* ---- the picture ---- */
static void recolour(int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCR_W) x1 = SCR_W;
    if (y1 > SCR_H) y1 = SCR_H;
    for (int y = y0; y < y1; y++) {
        const uint8_t *s = g_idx + y * SCR_W;
        uint32_t *d = g_rgb + y * SCR_W;
        for (int x = x0; x < x1; x++) d[x] = g_hw[s[x]];
    }
    if (x0 >= x1 || y0 >= y1) return;
    if (g_smooth) {                                      /* a changed pixel changes its neighbours' averages too */
        x0 -= SOFT_R, y0 -= SOFT_R, x1 += SOFT_R, y1 += SOFT_R;
        soften(x0, y0, x1, y1);
    }
    int px0, py0, px1, py1;
    pointer_rect(&px0, &py0, &px1, &py1);
    if (g_ptr.shown && px0 < x1 && px1 > x0 && py0 < y1 && py1 > y0) {   /* drawn under the pointer: both again */
        copy_out(px0, py0, px1, py1);
        copy_out(x0, y0, x1, y1);
        draw_pointer();
    } else copy_out(x0, y0, x1, y1);
    hle_gdi_window_touched();
}
static void set_hw(const uint32_t *c, int start, int n) {
    int changed = 0;
    for (int i = 0; i < n && start + i < 256; i++)
        if (g_hw[start + i] != c[i]) g_hw[start + i] = c[i], changed = 1;
    if (changed) recolour(0, 0, SCR_W, SCR_H);
}
/* The index whose hardware colour is nearest: for a DIB that brings RGB colours. */
static uint8_t nearest(uint32_t c) {
    int best = 0; long bd = -1;
    int r = c >> 16 & 0xFF, g = c >> 8 & 0xFF, b = c & 0xFF;
    for (int i = 0; i < 256; i++) {
        int dr = r - (int)(g_hw[i] >> 16 & 0xFF), dg = g - (int)(g_hw[i] >> 8 & 0xFF), db = b - (int)(g_hw[i] & 0xFF);
        long d = (long)dr * dr + (long)dg * dg + (long)db * db;
        if (bd < 0 || d < bd) { bd = d, best = i; if (!d) break; }
    }
    return (uint8_t)best;
}
int madeline_gfx_smooth(int on) {                        /* -1: just ask */
    if (on >= 0 && (on != 0) != g_smooth) {
        g_smooth = on != 0;
        if (g_smooth) soften(0, 0, SCR_W, SCR_H);
        copy_out(0, 0, SCR_W, SCR_H);
        draw_pointer();
        hle_gdi_window_touched();
    }
    return g_smooth;
}

/* ---- upscale=xbr ----
 * The presenter shows the window surface it is given, and maps the mouse by
 * that surface's size: handed a 1280x960 picture, it would tell the game the
 * mouse was at 1279,959. What it has instead is a hook that turns a 16-bit
 * screen into a picture twice the size, leaving the mouse alone (made for
 * another game). So while xBR is on, the presenter is told the screen is a
 * 16-bit one - a stand-in nobody draws on - and the hook hands it g_big. */
static void big_update(void) {
    if (g_dx0 < g_dx1 && g_dy0 < g_dy1) {
        clock_t t = clock();
        madeline_xbr2x(g_out, SCR_W, SCR_H, g_big, g_dx0 - 2, g_dy0 - 2, g_dx1 + 2, g_dy1 + 2);
        if (g_trace) fprintf(stderr, "[gfx] xBR %dx%d: %.1f ms\n", g_dx1 - g_dx0, g_dy1 - g_dy0, 1000.0 * (double)(clock() - t) / CLOCKS_PER_SEC);
    }
    g_dx0 = SCR_W, g_dy0 = SCR_H, g_dx1 = g_dy1 = 0;
}
static int compose(const uint8_t *px16, int pitch, int w, int h, uint32_t *out) {
    (void)px16; (void)pitch;
    if (!g_xbr || w != SCR_W || h != SCR_H) return 0;
    big_update();
    memcpy(out, g_big, sizeof g_big);
    return 1;
}
int madeline_gfx_upscale(int on) {                       /* -1: just ask */
    if (on >= 0 && (on != 0) != g_xbr) {
        g_xbr = on != 0;
        if (g_xbr) {
            changed(0, 0, SCR_W, SCR_H);
            hle_screen_compose_hook = compose;
            hle_gdi_window_surface((uint8_t *)g_stand_in, SCR_W, SCR_H, SCR_W * 2, 16);
        } else {
            hle_screen_compose_hook = NULL;
            hle_gdi_window_surface((uint8_t *)g_out, SCR_W, SCR_H, SCR_W * 4, 32);
        }
        hle_gdi_window_touched();
    }
    return g_xbr;
}
/* The picture as it is shown: a screenshot is of what the player sees. */
const uint32_t *madeline_frame(int *w, int *h) {
    if (g_xbr) { big_update(); *w = 2 * SCR_W, *h = 2 * SCR_H; return g_big; }
    *w = SCR_W, *h = SCR_H;
    return g_out;
}

void madeline_gfx_init(void) {
    g_trace = getenv("MADELINE_GFXTRACE") != NULL;
    for (int i = 0; i < 256; i++) g_hw[i] = (uint32_t)i * 0x010101u;
    hle_gdi_window_surface((uint8_t *)g_out, SCR_W, SCR_H, SCR_W * 4, 32);
    hle_set_screen_size(SCR_W, SCR_H);
}

/* ---- device contexts ---- */
static void g_GetDC(void)     { RET(H_SCREEN_DC, 1); }
static void g_ReleaseDC(void) { RET(1, 2); }
static void g_DeleteDC(void) {
    wingdc_t *d = WDC(A32(0));
    if (d) d->used = 0;
    if (A32(0) == MADELINE_PRINTER_DC) madeline_print_close();
    RET(1, 1);
}
static void g_GetDeviceCaps(void) {
    uint32_t v = 0;
    if (A32(0) == MADELINE_PRINTER_DC) RET(madeline_print_caps(A32(1)), 2);
    switch (A32(1)) {
        case 8:   v = SCR_W; break;                      /* HORZRES */
        case 10:  v = SCR_H; break;                      /* VERTRES */
        case 12:  v = 8; break;                          /* BITSPIXEL */
        case 14:  v = 1; break;                          /* PLANES */
        case 24:  v = 20; break;                         /* NUMCOLORS: the static ones */
        case 38:  v = 0x0100u | 0x0001u | 0x0008u | 0x0800u; break;   /* RASTERCAPS: RC_PALETTE, BITBLT, BITMAP64, STRETCHDIB */
        case 88: case 90: v = 96; break;                 /* LOGPIXELSX/Y */
        case 104: v = 256; break;                        /* SIZEPALETTE */
        case 106: v = 20; break;                         /* NUMRESERVED */
        case 108: v = 18; break;                         /* COLORRES */
    }
    RET(v, 2);
}

/* ---- objects ---- */
static void g_GetStockObject(void) { RET(H_STOCK + (A32(0) & 0xFF), 1); }
static void g_SelectObject(void) {
    wingdc_t *d = WDC(A32(0));
    if (d && BMP(A32(1))) { uint32_t old = d->bmp ? d->bmp : H_OLD_BMP; d->bmp = A32(1); RET(old, 2); }
    if (d && A32(1) == H_OLD_BMP) { uint32_t old = d->bmp; d->bmp = 0; RET(old ? old : H_OLD_BMP, 2); }
    RET(H_STOCK, 2);
}
static void g_DeleteObject(void) {
    bitmap_t *b = BMP(A32(0)); palette_t *p = PAL(A32(0));
    if (b) { hle_free(b->bits); b->used = 0; }
    if (p) { p->used = 0; if (g_screen_pal == A32(0)) g_screen_pal = 0; }
    RET(1, 1);
}
static void g_SetBkMode(void) { RET(2 /* OPAQUE */, 2); }
static void g_TextOutA(void) {
    if (g_trace) fprintf(stderr, "[gfx] TextOutA(%d,%d,\"%.*s\")\n", (int)A32(1), (int)A32(2), (int)A32(4), ASTR(3) ? ASTR(3) : "");
    RET(1, 5);
}

/* ---- palettes ---- */
static uint32_t entry(uint32_t va) { return (uint32_t)MEM8(va) << 16 | (uint32_t)MEM8(va + 1) << 8 | MEM8(va + 2); }   /* PALETTEENTRY r,g,b,flags */
static void g_CreatePalette(void) {                      /* LOGPALETTE: version, count, entries */
    uint32_t lp = A32(0);
    for (int i = 0; i < MAX_PALETTES; i++) if (!g_pal[i].used) {
        palette_t *p = &g_pal[i];
        memset(p, 0, sizeof *p);
        p->used = 1, p->n = MEM16(lp + 2) > 256 ? 256 : MEM16(lp + 2);
        for (int k = 0; k < p->n; k++) p->c[k] = entry(lp + 4 + 4u * (uint32_t)k);
        if (g_trace) fprintf(stderr, "[gfx] CreatePalette: %d colours\n", p->n);
        RET(H_PALETTE + (uint32_t)i, 1);
    }
    RET(0, 1);
}
static void g_SelectPalette(void) {
    uint32_t old = H_OLD_PAL;
    if (A32(0) == H_SCREEN_DC) { old = g_screen_pal ? g_screen_pal : H_OLD_PAL; g_screen_pal = PAL(A32(1)) ? A32(1) : 0; }
    RET(old, 3);
}
static void g_RealizePalette(void) {
    palette_t *p = A32(0) == H_SCREEN_DC ? PAL(g_screen_pal) : NULL;
    if (!p) RET(0, 1);
    g_hw_set = 1;
    set_hw(p->c, 0, p->n);
    RET((uint32_t)p->n, 1);
}
static void set_entries(int animate) {                   /* (hpal, start, count, entries) */
    palette_t *p = PAL(A32(0));
    uint32_t start = A32(1), n = A32(2), lp = A32(3);
    if (!p || !lp || start >= 256) RET(0, 4);
    if (start + n > 256) n = 256 - start;
    for (uint32_t i = 0; i < n; i++) p->c[start + i] = entry(lp + 4 * i);
    if (!animate && (int)(start + n) > p->n) p->n = (int)(start + n);
    /* AnimatePalette reaches the screen at once; SetPaletteEntries waits for RealizePalette. */
    if (animate && A32(0) == g_screen_pal && g_hw_set) set_hw(p->c + start, (int)start, (int)n);
    RET(animate ? 1u : n, 4);
}
static void g_AnimatePalette(void)    { set_entries(1); }
static void g_SetPaletteEntries(void) { set_entries(0); }

/* ---- WinG ---- */
static int32_t rd32(const uint8_t *b, int o) { int32_t v; memcpy(&v, b + o, 4); return v; }
static void g_WinGCreateDC(void) {
    for (int i = 0; i < MAX_WINGDCS; i++) if (!g_wdc[i].used) {
        g_wdc[i].used = 1, g_wdc[i].bmp = 0;
        RET(H_WING_DC + (uint32_t)i, 0);
    }
    RET(0, 0);
}
static void g_WinGCreateBitmap(void) {                   /* (hdc, BITMAPINFO *, void **bits) */
    const uint8_t *bmi = (const uint8_t *)APTR(1);
    uint32_t pp = A32(2);
    int w = rd32(bmi, 4), h = rd32(bmi, 8), bpp = bmi[14];
    if (bpp != 8 || w <= 0 || !h) { fprintf(stderr, "[gfx] WinGCreateBitmap: %dx%d, %d bpp is not a WinG bitmap\n", w, h, bpp); RET(0, 3); }
    for (int i = 0; i < MAX_BITMAPS; i++) if (!g_bmp[i].used) {
        bitmap_t *b = &g_bmp[i];
        memset(b, 0, sizeof *b);
        b->used = 1, b->w = w, b->h = h < 0 ? -h : h, b->topdown = h < 0, b->pitch = (w + 3) & ~3;
        b->bits = (uint8_t *)hle_alloc((uint32_t)(b->pitch * b->h));
        const uint8_t *q = bmi + rd32(bmi, 0);           /* RGBQUAD b,g,r,0 */
        for (int k = 0; k < 256; k++) b->ctab[k] = (uint32_t)q[4 * k + 2] << 16 | (uint32_t)q[4 * k + 1] << 8 | q[4 * k];
        if (pp) MEM32(pp) = hle_va(b->bits);
        if (g_trace) fprintf(stderr, "[gfx] WinGCreateBitmap: %dx%d %s\n", b->w, b->h, b->topdown ? "top-down" : "bottom-up");
        RET(H_BITMAP + (uint32_t)i, 3);
    }
    RET(0, 3);
}
static void g_WinGSetDIBColorTable(void) {               /* (hdc, start, count, RGBQUAD *) */
    wingdc_t *d = WDC(A32(0));
    bitmap_t *b = d ? BMP(d->bmp) : NULL;
    uint32_t start = A32(1), n = A32(2);
    const uint8_t *q = (const uint8_t *)APTR(3);
    if (!b || !q || start >= 256) RET(0, 4);
    if (start + n > 256) n = 256 - start;
    for (uint32_t k = 0; k < n; k++) b->ctab[start + k] = (uint32_t)q[4 * k + 2] << 16 | (uint32_t)q[4 * k + 1] << 8 | q[4 * k];
    /* A game that never realizes a palette shows the bitmap's own colours. */
    if (!g_hw_set) set_hw(b->ctab + start, (int)start, (int)n);
    RET(n, 4);
}
static void g_WinGBitBlt(void) {                         /* (dst, x, y, w, h, src, xs, ys) */
    int dx = (int)A32(1), dy = (int)A32(2), w = (int)A32(3), h = (int)A32(4), sx = (int)A32(6), sy = (int)A32(7);
    wingdc_t *s = WDC(A32(5));
    bitmap_t *b = s ? BMP(s->bmp) : NULL;
    if (A32(0) != H_SCREEN_DC || !b) RET(0, 8);
    if (!g_hw_set) memcpy(g_hw, b->ctab, sizeof g_hw);
    for (int j = 0; j < h; j++) {
        int py = dy + j, qy = sy + j;
        if (py < 0 || py >= SCR_H || qy < 0 || qy >= b->h) continue;
        const uint8_t *row = b->bits + (size_t)(b->topdown ? qy : b->h - 1 - qy) * (size_t)b->pitch;
        int x0 = dx, x1 = dx + w, q0 = sx;
        if (x0 < 0) q0 -= x0, x0 = 0;
        if (q0 < 0) x0 -= q0, q0 = 0;
        if (x1 > SCR_W) x1 = SCR_W;
        if (x1 - x0 > b->w - q0) x1 = x0 + b->w - q0;
        if (x1 > x0) memcpy(g_idx + py * SCR_W + x0, row + q0, (size_t)(x1 - x0));
    }
    recolour(dx, dy, dx + w, dy + h);
    RET(1, 8);
}

/* StretchDIBits(hdc, xd,yd,wd,hd, xs,ys,ws,hs, bits, bmi, usage, rop): a DIB
 * straight onto the screen. An 8-bit one's colour table is indices into the
 * selected palette (DIB_PAL_COLORS) or colours to be matched (DIB_RGB_COLORS). */
static void g_StretchDIBits(void) {
    int xd = (int)A32(1), yd = (int)A32(2), wd = (int)A32(3), hd = (int)A32(4);
    int xs = (int)A32(5), ys = (int)A32(6), ws = (int)A32(7), hs = (int)A32(8);
    const uint8_t *bits = (const uint8_t *)APTR(9), *bmi = (const uint8_t *)APTR(10);
    uint32_t usage = A32(11);
    if (A32(0) == MADELINE_PRINTER_DC) RET(madeline_print_stretch(), 13);
    if (A32(0) != H_SCREEN_DC || !bits || !bmi || wd <= 0 || hd <= 0 || ws <= 0 || hs <= 0) RET(0, 13);
    int w = rd32(bmi, 4), h = rd32(bmi, 8), bpp = bmi[14], topdown = h < 0;
    if (h < 0) h = -h;
    int pitch = (w * bpp + 31) / 32 * 4;
    const uint8_t *tab = bmi + rd32(bmi, 0);
    uint8_t map[256];
    if (g_trace) fprintf(stderr, "[gfx] StretchDIBits %dx%d %dbpp usage %u -> %d,%d %dx%d\n", w, h, bpp, usage, xd, yd, wd, hd);
    if (bpp == 8)
        for (int k = 0; k < 256; k++)
            map[k] = usage == 1 ? tab[2 * k] : nearest((uint32_t)tab[4 * k + 2] << 16 | (uint32_t)tab[4 * k + 1] << 8 | tab[4 * k]);
    else if (bpp != 24) { fprintf(stderr, "[gfx] StretchDIBits: %d bpp not drawn\n", bpp); RET(0, 13); }
    for (int j = 0; j < hd; j++) {
        int py = yd + j, sj = j * hs / hd;
        int qy = topdown ? ys + sj : h - 1 - (ys + hs - 1 - sj);    /* row from the top of the picture */
        if (py < 0 || py >= SCR_H || qy < 0 || qy >= h) continue;
        const uint8_t *row = bits + (size_t)(topdown ? qy : h - 1 - qy) * (size_t)pitch;
        for (int i = 0; i < wd; i++) {
            int px = xd + i, qx = xs + i * ws / wd;
            if (px < 0 || px >= SCR_W || qx < 0 || qx >= w) continue;
            g_idx[py * SCR_W + px] = bpp == 8 ? map[row[qx]]
                : nearest((uint32_t)row[3 * qx + 2] << 16 | (uint32_t)row[3 * qx + 1] << 8 | row[3 * qx]);
        }
    }
    recolour(xd, yd, xd + wd, yd + hd);
    RET((uint32_t)hs, 13);
}

const win32hle_shim madeline_gfx[] = {
    { "GetDC", g_GetDC }, { "ReleaseDC", g_ReleaseDC }, { "DeleteDC", g_DeleteDC },
    { "GetDeviceCaps", g_GetDeviceCaps }, { "GetStockObject", g_GetStockObject }, { "SelectObject", g_SelectObject },
    { "DeleteObject", g_DeleteObject }, { "SetBkMode", g_SetBkMode }, { "TextOutA", g_TextOutA },
    { "CreatePalette", g_CreatePalette }, { "SelectPalette", g_SelectPalette }, { "RealizePalette", g_RealizePalette },
    { "AnimatePalette", g_AnimatePalette }, { "SetPaletteEntries", g_SetPaletteEntries },
    { "WinGCreateDC", g_WinGCreateDC }, { "WinGCreateBitmap", g_WinGCreateBitmap },
    { "WinGSetDIBColorTable", g_WinGSetDIBColorTable }, { "WinGBitBlt", g_WinGBitBlt },
    { "StretchDIBits", g_StretchDIBits },
    { 0, 0 }
};
