/* madeline.h - what this host's own files share. */
#ifndef MADELINE_H
#define MADELINE_H
#include <stddef.h>
#include <stdint.h>
#include "win32hle.h"

extern const win32hle_shim madeline_shims[];    /* shims.c: drives, the registry */
uint64_t madeline_clock_ns(void);               /* the game's clock (shims.c) */
/* platform.c */
void madeline_data_dir(char *out, size_t n);     /* where settings, saves and screenshots live */
void madeline_make_dirs(const char *path);      /* mkdir -p */

void madeline_ini_set(const char *key, const char *value);   /* remember a setting (main.c) */
extern int madeline_headless;                   /* no window: message boxes answer themselves */
extern int madeline_remember_name;              /* type the last saved name into the sign-in book (shims.c) */
extern char madeline_save_dir[1024];            /* where the .sav files are, on the host */
extern int madeline_skip_intro;                 /* press a key through the opening movies (shims.c) */
void     madeline_type(uint32_t vk, int shifted);   /* press a key for the player; shifted: a capital (shims.c) */
void     madeline_idle(void);                   /* the message pump's hook */
double   madeline_speed(void);                  /* 1, or MADELINE_SPEED for tests */
extern const win32hle_shim madeline_gfx[];      /* gfx.c: GDI and WinG on a 256-colour screen */

extern const win32hle_shim madeline_audio[];    /* audio.c: waveOut and MCI */
void madeline_audio_tick(void);                 /* called from the clock */

/* midi.c: one MIDI file at a time, on the system's synthesizer */
int  madeline_midi_open(const char *host_path); /* 1 if it can be played */
void madeline_midi_close(void);
void madeline_midi_seek(int position);
void madeline_midi_play(int from);              /* from < 0: where it is */
void madeline_midi_stop(void);
int  madeline_midi_finished(void);

void madeline_gfx_init(void);                   /* after recomp_host_init */
int  madeline_gfx_smooth(int on);               /* melt the dithering (1), raw pixels (0), ask (-1); returns the state */
int  madeline_gfx_upscale(int on);              /* show the picture doubled by xBR (1), as it is (0), ask (-1) */
void madeline_xbr2x(const uint32_t *src, int w, int h, uint32_t *dst, int x0, int y0, int x1, int y1);   /* xbr.c */
void madeline_gfx_plain_pointer(int on);      /* pointer=plain: one arrow instead of the game's cursor shapes */
void madeline_gfx_pointer(void);              /* while idle: redraw the mouse pointer if it moved or changed */
const uint32_t *madeline_frame(int *w, int *h); /* the picture now (pointer included), XRGB */

/* print.c: the printer. gfx.c owns the GDI calls a printer shares with the
 * screen and passes them on when they are for this device context. */
#define MADELINE_PRINTER_DC 0x5D000001u
extern const win32hle_shim madeline_print[];    /* WIN.INI's default printer, CreateDCA, StartDoc ... EndDoc */
void     madeline_print_init(const char *save_root);    /* pages go to <save_root>/prints, as PDFs */
uint32_t madeline_print_caps(uint32_t index);   /* GetDeviceCaps */
uint32_t madeline_print_stretch(void);          /* StretchDIBits, inside that shim: reads its arguments */
void     madeline_print_close(void);            /* DeleteDC */
/* Give a finished PDF to the system: its print dialog (macOS, src/mac), or
 * whatever opens PDFs (platform.c, src/win). Returns when the player is done
 * with a dialog, or at once; 0 if nothing could be started. */
int      madeline_print_hand_over(const char *pdf, const char *title);

/* Where this build of MADEURO.EXE keeps what the host watches (docs/NOTES.md, "Keys"). */
#define G_SCREEN        0x0041E540u             /* word: MADnnn / 10 */
#define G_INPUT_ENABLED 0x00421C80u
#define G_MOVIE_PLAYING 0x00421D84u
#define SCREEN_14        14                     /* takes typing; not seen yet */
#define SCREEN_WORD_GAME 34
#define SCREEN_SIGN_IN   39

/* pad.c: a game controller as the mouse, and which input the player is using */
enum { DEV_DESK, DEV_TOUCH, DEV_PAD };          /* a mouse and keys; a touch screen; a controller */
extern int madeline_input_device;
void madeline_pad_init(int on);                 /* after the window is open; gamepad=0 in madeline.ini is off */
void madeline_pad_tick(uint32_t now_ms);        /* while idle */
void madeline_pad_script(const char *args, uint32_t now_ms);    /* the test script's `pad` */

/* osk.c: a keyboard drawn on the screen where the game wants typing, for touch and the pad */
void madeline_osk_tick(uint32_t now_ms);        /* while idle */
void madeline_osk_paint(uint32_t *px, int x0, int y0, int x1, int y1);   /* gfx.c: over the picture, inside this rectangle */
int  madeline_osk_message(uint32_t msg, int x, int y);          /* a mouse message: 1 if the keyboard took it */
int  madeline_osk_open(void);
int  madeline_osk_step(int dx, int dy);         /* the pad's cross: to the next key */
void madeline_osk_toggle(void);
void madeline_osk_shift(void);
void madeline_gfx_redraw(int x0, int y0, int x1, int y1);       /* gfx.c: that part of the picture again (the keyboard changed) */

void madeline_screenshots_init(const char *save_root);   /* F9 / SIGUSR1 -> <save_root>/screenshots */
void madeline_debug_tick(uint32_t now_ms);      /* debug.c: MADELINE_SCRIPT */

#endif
