/*
 * print.c - the printer the game prints its postcards and pictures on.
 *
 * MADEURO.EXE prints one way (0x40FBB0, called with a picture and its size):
 * it asks "Would you like to print?", reads the default printer from WIN.INI
 * (GetProfileStringA "windows" / "device": "name,driver,port"), opens it with
 * CreateDCA, checks a few GetDeviceCaps answers, and then sends one page:
 * StartDocA, StartPage, a single StretchDIBits of the picture (8-bit, with
 * its colours) across the page's whole width and three quarters of that in
 * height, EndPage, EndDoc. No print dialog, no choice of printer.
 *
 * There is no 1996 printer driver here, so this file is one. The page is kept
 * as the picture the game sent, at the size it sent it, and at EndDoc it
 * becomes a PDF in <data folder>/prints. That file is then handed to the
 * system: the print dialog on a Mac (which also saves PDFs), the PDF viewer
 * elsewhere (madeline_print_hand_over). Headless, the PDF is all that happens.
 *
 * One thing is not as a 1996 printer did it. The game's 4:3 box is not the
 * shape of either picture it prints (a painting is 596x349, the other
 * 548x290), so on paper they came out stretched tall, by a quarter and by
 * two fifths. Here a picture keeps the width it was given and its own shape,
 * and the page ends where the pictures do (keep_shape, write_pdf).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "win32hle.h"
#include "madeline.h"

/* The sheet the game is told of: 10 by 7.5 inches at 300 dots to the inch,
 * which its 4:3 box fills exactly. Any real paper is chosen later, in the
 * print dialog, which fits the page onto it. */
#define DPI     300
#define PAGE_W  3000
#define PAGE_H  2250

typedef struct { int w, h, x, y, dw, dh; uint8_t *rgb; } piece_t;   /* a picture and where on the page it goes */
#define MAX_PIECES 8
static piece_t g_piece[MAX_PIECES];
static int     g_pieces, g_open, g_doc;
static char    g_title[64], g_dir[1100];
static int     g_trace = -1;
static int trace(void) { if (g_trace < 0) g_trace = getenv("MADELINE_GFXTRACE") != NULL; return g_trace; }

void madeline_print_init(const char *save_root) { snprintf(g_dir, sizeof g_dir, "%s/prints", save_root); }

static void forget_page(void) {
    for (int i = 0; i < g_pieces; i++) free(g_piece[i].rgb);
    g_pieces = 0;
}

/* ---- the page as a PDF: one page, each picture an image stretched to its place ---- */
/* The colours are a monitor's (sRGB, near enough: its primaries, gamma 2.2).
 * Left as "device" colours, a Mac shows and prints them visibly off: the
 * paint box's red turns orange. */
static const char k_srgb[] = "[/CalRGB << /WhitePoint [0.9505 1 1.089] /Gamma [2.2 2.2 2.2] "
                             "/Matrix [0.4124 0.2126 0.0193 0.3576 0.7152 0.1192 0.1805 0.0722 0.9505] >>]";
static int write_pdf(const char *file) {
    FILE *f = fopen(file, "wb");
    if (!f) { perror(file); return 0; }
    long at[4 + 2 * MAX_PIECES];
    int n = 0;
    char body[160 * MAX_PIECES];
    size_t len = 0;
    const double pt = 72.0 / DPI;
    int page_h = 1;                                      /* the page stops under the lowest picture */
    for (int i = 0; i < g_pieces; i++) if (g_piece[i].y + g_piece[i].dh > page_h) page_h = g_piece[i].y + g_piece[i].dh;
    for (int i = 0; i < g_pieces; i++) {                 /* PDF measures from the bottom of the page */
        const piece_t *p = &g_piece[i];
        len += (size_t)snprintf(body + len, sizeof body - len, "q %.2f 0 0 %.2f %.2f %.2f cm /P%d Do Q\n",
                                p->dw * pt, p->dh * pt, p->x * pt, (page_h - p->y - p->dh) * pt, i);
    }
    fprintf(f, "%%PDF-1.4\n%%\xE2\xE3\xCF\xD3\n");
    at[n++] = ftell(f); fprintf(f, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    at[n++] = ftell(f); fprintf(f, "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
    at[n++] = ftell(f);
    fprintf(f, "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.2f %.2f] /Contents 4 0 R /Resources << /XObject <<", PAGE_W * pt, page_h * pt);
    for (int i = 0; i < g_pieces; i++) fprintf(f, " /P%d %d 0 R", i, 5 + i);
    fprintf(f, " >> >> >>\nendobj\n");
    at[n++] = ftell(f); fprintf(f, "4 0 obj\n<< /Length %lu >>\nstream\n%sendstream\nendobj\n", (unsigned long)len, body);
    for (int i = 0; i < g_pieces; i++) {
        const piece_t *p = &g_piece[i];
        at[n++] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace %s /BitsPerComponent 8 /Length %lu >>\nstream\n",
                5 + i, p->w, p->h, k_srgb, (unsigned long)p->w * (unsigned long)p->h * 3ul);
        fwrite(p->rgb, 3, (size_t)p->w * (size_t)p->h, f);
        fprintf(f, "\nendstream\nendobj\n");
    }
    at[n] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Title (%s) /Creator (Madeline: European Adventures) >>\nendobj\n", n + 1, g_title);
    long xref = ftell(f);
    fprintf(f, "xref\n0 %d\n0000000000 65535 f \n", n + 2);
    for (int i = 0; i <= n; i++) fprintf(f, "%010ld 00000 n \n", at[i]);
    fprintf(f, "trailer\n<< /Size %d /Root 1 0 R /Info %d 0 R >>\nstartxref\n%ld\n%%%%EOF\n", n + 2, n + 1, xref);
    int ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

/* ---- WIN.INI ----
 * GetProfileStringA(section, key, default, buffer, size). The one thing the
 * game reads is [windows] device=, the default printer, and it wants three
 * parts with commas between. Anything else gets its default, as from an
 * empty WIN.INI. */
static void p_GetProfileStringA(void) {
    const char *sec = ASTR(0), *key = ASTR(1), *def = ASTR(2), *v;
    uint32_t n = A32(4);
    char *out = ASTR(3);
    v = sec && key && !strcmp(sec, "windows") && !strcmp(key, "device") ? "Print or PDF,madeline,PDF:" : def ? def : "";
    if (!A32(3) || !n) RET(0, 5);
    snprintf(out, n, "%s", v);
    RET((uint32_t)strlen(out), 5);
}

/* ---- the device context ---- */
static void p_CreateDCA(void) {                          /* (driver, device, port, DEVMODE *): only ever the printer */
    if (trace()) fprintf(stderr, "[print] CreateDCA(%s, %s)\n", ASTR(0) ? ASTR(0) : "", ASTR(1) ? ASTR(1) : "");
    g_open = 1;
    RET(MADELINE_PRINTER_DC, 4);
}
void madeline_print_close(void) { g_open = g_doc = 0; forget_page(); }   /* DeleteDC */

uint32_t madeline_print_caps(uint32_t index) {
    switch (index) {
        /* The game treats four "is it zero?" answers as abilities (8, 2, 1 and
         * 4: the RC_ bits of RASTERCAPS, asked for as if they were indices)
         * and shows a warning box for each that is zero. 8, 2 and 4 are real
         * indices that no printer answers with zero. 1 is not an index at all:
         * Windows 95 answered it from the middle of the driver's version and
         * technology words, which is never zero either. */
        case 1:   return 1;
        case 2:   return 2;                              /* TECHNOLOGY: DT_RASPRINTER */
        case 4:   return PAGE_W * 254 / (DPI * 10);      /* HORZSIZE, mm */
        case 6:   return PAGE_H * 254 / (DPI * 10);      /* VERTSIZE */
        case 8:   return PAGE_W;                         /* HORZRES */
        case 10:  return PAGE_H;                         /* VERTRES */
        case 12:  return 24;                             /* BITSPIXEL */
        case 14:  return 1;                              /* PLANES */
        case 24:  return (uint32_t)-1;                   /* NUMCOLORS: more than 256 */
        case 38:  return 0x0001u | 0x0008u | 0x0200u | 0x0800u;   /* RASTERCAPS: BITBLT, BITMAP64, DIBTODEV, STRETCHDIB */
        case 88: case 90: return DPI;                    /* LOGPIXELSX/Y */
        case 110: return PAGE_W;                         /* PHYSICALWIDTH */
        case 111: return PAGE_H;                         /* PHYSICALHEIGHT */
    }
    return 0;
}

/* StretchDIBits onto the page (arguments as in gfx.c): the part of the DIB
 * that was asked for is kept as it is, with the place it is to fill. Scaling
 * it up to the page is left to whatever shows or prints the PDF, which does
 * it better than 300 dots of our own would. Returns the lines taken. */
uint32_t madeline_print_stretch(void) {
    int xd = (int)A32(1), yd = (int)A32(2), wd = (int)A32(3), hd = (int)A32(4);
    int xs = (int)A32(5), ys = (int)A32(6), ws = (int)A32(7), hs = (int)A32(8);
    const uint8_t *bits = (const uint8_t *)APTR(9), *bmi = (const uint8_t *)APTR(10);
    int32_t w, h;
    if (!g_open || !A32(9) || !A32(10) || wd <= 0 || hd <= 0 || ws <= 0 || hs <= 0) return 0;
    memcpy(&w, bmi + 4, 4), memcpy(&h, bmi + 8, 4);
    int bpp = bmi[14], topdown = h < 0;
    if (h < 0) h = -h;
    if (trace()) fprintf(stderr, "[print] StretchDIBits %dx%d %dbpp usage %u, %d,%d %dx%d -> %d,%d %dx%d\n", w, h, bpp, A32(11), xs, ys, ws, hs, xd, yd, wd, hd);
    if ((bpp != 8 && bpp != 24) || (bpp == 8 && A32(11) != 0) || xs < 0 || ys < 0 || xs + ws > w || ys + hs > h) {
        fprintf(stderr, "[print] StretchDIBits: a %dx%d, %d bpp picture (usage %u) is not printed\n", w, h, bpp, A32(11));
        return 0;
    }
    if (g_pieces == MAX_PIECES) { fprintf(stderr, "[print] more than %d pictures on a page: the rest are left out\n", MAX_PIECES); return 0; }
    int32_t head;
    memcpy(&head, bmi, 4);
    const uint8_t *tab = bmi + head;                     /* RGBQUAD b,g,r,0 */
    int pitch = (w * bpp + 31) / 32 * 4;
    piece_t *p = &g_piece[g_pieces];
    p->rgb = (uint8_t *)malloc((size_t)ws * (size_t)hs * 3);
    if (!p->rgb) return 0;
    p->w = ws, p->h = hs, p->x = xd, p->y = yd, p->dw = wd;
    p->dh = (int)((long long)wd * hs / ws);              /* keep_shape: not hd, the game's 4:3 (see the top of the file) */
    if (p->dh < 1) p->dh = 1;
    for (int j = 0; j < hs; j++) {                       /* j: from the top of the part asked for; ys counts from the bottom of a bottom-up DIB */
        int qy = topdown ? ys + j : h - ys - hs + j;
        const uint8_t *row = bits + (size_t)(topdown ? qy : h - 1 - qy) * (size_t)pitch;
        uint8_t *d = p->rgb + (size_t)j * (size_t)ws * 3;
        for (int i = 0; i < ws; i++, d += 3) {
            const uint8_t *c = bpp == 8 ? tab + 4 * row[xs + i] : row + 3 * (xs + i);
            d[0] = c[2], d[1] = c[1], d[2] = c[0];
        }
    }
    g_pieces++;
    return (uint32_t)hs;
}

/* ---- the document ---- */
static void p_StartDocA(void) {                          /* (hdc, DOCINFO *: size, name, output, ...) */
    if (A32(0) != MADELINE_PRINTER_DC || !g_open) RET((uint32_t)-1, 2);
    const char *name = A32(1) && MEM32(A32(1) + 4) ? (const char *)hle_ptr(MEM32(A32(1) + 4)) : "";
    size_t k = 0;
    for (; *name && k + 1 < sizeof g_title; name++)      /* a PDF string: no brackets or backslashes */
        if (*name >= ' ' && *name < 127 && !strchr("()\\", *name)) g_title[k++] = *name;
    g_title[k] = 0;
    forget_page();
    g_doc = 1;
    RET(1, 2);                                           /* the job's number */
}
static void p_StartPage(void) { RET(A32(0) == MADELINE_PRINTER_DC && g_doc ? 1u : 0u, 1); }
static void p_EndPage(void)   { RET(A32(0) == MADELINE_PRINTER_DC && g_doc ? 1u : 0u, 1); }
static void p_AbortDoc(void)  { if (A32(0) == MADELINE_PRINTER_DC) { g_doc = 0; forget_page(); } RET(1, 1); }
static void p_EndDoc(void) {
    if (A32(0) != MADELINE_PRINTER_DC || !g_doc) RET((uint32_t)-1, 1);
    g_doc = 0;
    if (!g_pieces) RET(1, 1);                            /* an empty job prints nothing */
    char file[1200], stamp[32];
    static int n;
    time_t t = time(NULL);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&t));
    madeline_make_dirs(g_dir[0] ? g_dir : ".");
    snprintf(file, sizeof file, "%s/madeline-%s-%02d.pdf", g_dir[0] ? g_dir : ".", stamp, n++ % 100);
    int ok = write_pdf(file);
    forget_page();
    if (!ok) { fprintf(stderr, "[print] could not write %s\n", file); RET((uint32_t)-1, 1); }
    fprintf(stderr, "[print] %s\n", file);
    if (!madeline_headless) {
        hle_block_begin();                               /* a dialog may sit there a while: the game's clock should not see it */
        if (!madeline_print_hand_over(file, g_title)) fprintf(stderr, "[print] nothing here opens a PDF: it is in %s\n", g_dir);
        hle_block_end();
    }
    RET(1, 1);
}

const win32hle_shim madeline_print[] = {
    { "GetProfileStringA", p_GetProfileStringA }, { "CreateDCA", p_CreateDCA },
    { "StartDocA", p_StartDocA }, { "StartPage", p_StartPage }, { "EndPage", p_EndPage }, { "EndDoc", p_EndDoc }, { "AbortDoc", p_AbortDoc },
    { 0, 0 }
};
