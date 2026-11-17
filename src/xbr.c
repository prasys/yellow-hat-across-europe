/*
 * xbr.c - the picture at twice the size with its edges redrawn: Hyllian's xBR
 * rule (2x, "level 2"), written here from the published description of it.
 *
 * Doubling a 640x480 picture by repeating or blending pixels leaves every
 * slanted ink line a staircase. xBR looks, for each pixel, at the 21 around
 * it and asks of each of its four corners whether an edge runs diagonally
 * across it; where one does, that quarter of the pixel takes some of the
 * colour from the other side, more or less of it by how steep the edge is.
 * Flat areas and straight edges come through untouched.
 *
 * It suits this game's characters and ink outlines best; the painted
 * backgrounds lose a little brush texture. So it is a choice (upscale=xbr),
 * off unless asked for.
 *
 * The neighbourhood, with E the pixel being doubled:
 *
 *            A1 B1 C1
 *         A0  A  B  C C4
 *         D0  D  E  F F4
 *         G0  G  H  I I4
 *            G5 H5 I5
 */
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "madeline.h"

static const uint32_t *s_px;         /* the source picture */
static int16_t *s_y, *s_u, *s_v;     /* and its colours as brightness and two colour differences */
static size_t s_n;

/* How different two pixels look: brightness counts for much more than hue. */
static int df(int a, int b) {
    return 48 * abs(s_y[a] - s_y[b]) + 7 * abs(s_u[a] - s_u[b]) + 6 * abs(s_v[a] - s_v[b]);
}
static int same(int a, int b) { return df(a, b) < 155; }

/* a, with w/256 of b mixed in */
static uint32_t mix(uint32_t a, uint32_t b, unsigned w) {
    uint32_t rb = ((b & 0xFF00FFu) * w + (a & 0xFF00FFu) * (256 - w)) >> 8 & 0xFF00FFu;
    uint32_t g = ((b & 0x00FF00u) * w + (a & 0x00FF00u) * (256 - w)) >> 8 & 0x00FF00u;
    return rb | g;
}

/* One corner of E: the one towards I. n3 is that quarter of the doubled
 * pixel, n2 the quarter beside it towards G, n1 the one towards C. The other
 * three corners are this one with the neighbourhood turned. */
static void corner(uint32_t *n1, uint32_t *n2, uint32_t *n3,
                   int E, int I, int H, int F, int G, int C, int D, int B, int G5, int C4, int H5, int F4, int I5, int I4) {
    if (s_px[E] == s_px[H] || s_px[E] == s_px[F]) return;
    int along = df(E, C) + df(E, G) + df(I, H5) + df(I, F4) + 4 * df(H, F);     /* small: an edge runs H-F, cutting the corner off */
    int across = df(H, D) + df(H, I5) + df(F, I4) + df(F, B) + 4 * df(E, I);    /* small: E and I belong together */
    if (along > across) return;
    uint32_t px = s_px[df(E, F) <= df(E, H) ? F : H];
    if (along < across && ((!same(F, B) && !same(H, D)) || (same(E, I) && !same(F, I4) && !same(H, I5)) || same(E, G) || same(E, C))) {
        int ke = df(F, G), ki = df(H, C);
        int shallow = 2 * ke <= ki && s_px[E] != s_px[G] && s_px[D] != s_px[G];    /* the edge carries on towards G */
        int steep = ke >= 2 * ki && s_px[E] != s_px[C] && s_px[B] != s_px[C];      /* ... towards C */
        if (shallow && steep) { *n3 = mix(*n3, px, 224); *n2 = mix(*n2, px, 64); *n1 = *n2; }
        else if (shallow)     { *n3 = mix(*n3, px, 192); *n2 = mix(*n2, px, 64); }
        else if (steep)       { *n3 = mix(*n3, px, 192); *n1 = mix(*n1, px, 64); }
        else                    *n3 = mix(*n3, px, 128);
    } else *n3 = mix(*n3, px, 128);
}

#define MAX_BANDS 8
typedef struct { const uint32_t *src; uint32_t *dst; int w, h, x0, x1, y0, y1; } band_t;
static void *band_main(void *arg);

/* Double the part [x0,x1) x [y0,y1) of src (w x h) into dst (2w x 2h). A
 * changed source pixel changes the result up to two pixels away: the caller
 * widens the rectangle by that much. */
void madeline_xbr2x(const uint32_t *src, int w, int h, uint32_t *dst, int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    if (x0 >= x1 || y0 >= y1) return;
    if (s_n != (size_t)w * h) {
        free(s_y);
        s_n = (size_t)w * h;
        s_y = malloc(s_n * 3 * sizeof *s_y), s_u = s_y + s_n, s_v = s_u + s_n;
    }
    s_px = src;
    int cx0 = x0 - 2 < 0 ? 0 : x0 - 2, cy0 = y0 - 2 < 0 ? 0 : y0 - 2, cx1 = x1 + 2 > w ? w : x1 + 2, cy1 = y1 + 2 > h ? h : y1 + 2;
    for (int y = cy0; y < cy1; y++)
        for (int x = cx0; x < cx1; x++) {
            uint32_t c = src[y * w + x];
            int r = (int)(c >> 16 & 0xFF), g = (int)(c >> 8 & 0xFF), b = (int)(c & 0xFF);
            s_y[y * w + x] = (int16_t)((306 * r + 601 * g + 117 * b) >> 10);      /* 0.299, 0.587, 0.114 */
            s_u[y * w + x] = (int16_t)((-173 * r - 339 * g + 512 * b) / 1024);   /* -0.169, -0.331, 0.5 */
            s_v[y * w + x] = (int16_t)((512 * r - 429 * g - 83 * b) / 1024);     /* 0.5, -0.419, -0.081 */
        }
    /* A whole screen is some 25 ms of work on one core, and a fade or a movie
     * changes the whole screen many times a second: large jobs are cut into
     * bands of rows, one to a thread. Each band writes only its own rows. */
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    int bands = (x1 - x0) * (y1 - y0) < 128 * 128 ? 1 : cores > MAX_BANDS ? MAX_BANDS : cores < 1 ? 1 : (int)cores;
    if (bands > y1 - y0) bands = y1 - y0;
    band_t band[MAX_BANDS];
    pthread_t thread[MAX_BANDS];
    int started[MAX_BANDS] = { 0 };
    for (int i = 0; i < bands; i++) {
        band[i] = (band_t){ src, dst, w, h, x0, x1, y0 + (y1 - y0) * i / bands, y0 + (y1 - y0) * (i + 1) / bands };
        if (i > 0) started[i] = pthread_create(&thread[i], NULL, band_main, &band[i]) == 0;
    }
    for (int i = 0; i < bands; i++) if (i == 0 || !started[i]) band_main(&band[i]);
    for (int i = 1; i < bands; i++) if (started[i]) pthread_join(thread[i], NULL);
}

static void *band_main(void *arg) {
    const band_t *job = arg;
    const uint32_t *src = job->src;
    uint32_t *dst = job->dst;
    int w = job->w, h = job->h, x0 = job->x0, x1 = job->x1;
    for (int y = job->y0; y < job->y1; y++) {
        /* rows two up to two down, held at the picture's edge */
        int r2 = (y < 2 ? 0 : y - 2) * w, r1 = (y < 1 ? 0 : y - 1) * w, r0 = y * w, q1 = (y + 1 >= h ? h - 1 : y + 1) * w, q2 = (y + 2 >= h ? h - 1 : y + 2) * w;
        uint32_t *top = dst + (size_t)(2 * y) * (2 * w), *bottom = top + 2 * w;
        for (int x = x0; x < x1; x++) {
            int l2 = x < 2 ? 0 : x - 2, l1 = x < 1 ? 0 : x - 1, p1 = x + 1 >= w ? w - 1 : x + 1, p2 = x + 2 >= w ? w - 1 : x + 2;
            int A1 = r2 + l1, B1 = r2 + x, C1 = r2 + p1;
            int A0 = r1 + l2, A = r1 + l1, B = r1 + x, C = r1 + p1, C4 = r1 + p2;
            int D0 = r0 + l2, D = r0 + l1, E = r0 + x, F = r0 + p1, F4 = r0 + p2;
            int G0 = q1 + l2, G = q1 + l1, H = q1 + x, I = q1 + p1, I4 = q1 + p2;
            int G5 = q2 + l1, H5 = q2 + x, I5 = q2 + p1;
            uint32_t e[4] = { src[E], src[E], src[E], src[E] };         /* top left, top right, bottom left, bottom right */
            /* Most of a picture is not an edge. Where the four pixels beside E
             * look the same as it, whatever a corner took from them would not
             * show: skipping those saves a third of the time, and leaves the
             * brush texture of a wash as it was. */
            if (!same(E, B) || !same(E, D) || !same(E, F) || !same(E, H)) {
                corner(&e[1], &e[2], &e[3], E, I, H, F, G, C, D, B, G5, C4, H5, F4, I5, I4);
                corner(&e[0], &e[3], &e[1], E, C, F, B, I, A, H, D, I4, A1, F4, B1, C4, C1);
                corner(&e[2], &e[1], &e[0], E, A, B, D, C, G, F, H, C1, G0, B1, D0, A1, A0);
                corner(&e[3], &e[0], &e[2], E, G, D, H, A, I, B, F, A0, I5, D0, H5, G0, G5);
            }
            top[2 * x] = e[0], top[2 * x + 1] = e[1], bottom[2 * x] = e[2], bottom[2 * x + 1] = e[3];
        }
    }
    return NULL;
}
