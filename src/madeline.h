/* madeline.h - what this host's own files share. */
#ifndef MADELINE_H
#define MADELINE_H
#include <stdint.h>
#include "win32hle.h"

extern const win32hle_shim madeline_shims[];    /* shims.c: drives, the registry */
uint64_t madeline_clock_ns(void);               /* the game's clock (shims.c) */
void madeline_ini_set(const char *key, const char *value);   /* remember a setting (main.c) */
extern int madeline_headless;                   /* no window: message boxes answer themselves */
extern int madeline_remember_name;              /* type the last saved name into the sign-in book (shims.c) */
extern char madeline_save_dir[1024];            /* where the .sav files are, on the host */
extern int madeline_skip_intro;                 /* press a key through the opening movies (shims.c) */
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
void madeline_gfx_pointer(void);                /* while idle: redraw the mouse pointer if it moved or changed */
const uint32_t *madeline_frame(int *w, int *h); /* the picture now (pointer included), XRGB */
void madeline_screenshots_init(const char *save_root);   /* F9 / SIGUSR1 -> <save_root>/screenshots */
void madeline_debug_tick(uint32_t now_ms);      /* debug.c: MADELINE_SCRIPT */

#endif
