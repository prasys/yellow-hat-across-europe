/*
 * midi_win.c - the game's MIDI music on Windows, played by Windows itself:
 * MCI's "sequencer" device and the built-in General MIDI synthesizer, the
 * very thing the game asked for in 1996. See ../midi.c for the interface.
 *
 * The game's own mciSendCommandA is answered by audio.c inside the lifted
 * machine; this is the host calling the real MCI, by its string interface,
 * which is why it lives here with <windows.h> and apart from win32hle.
 *
 * MCI ties a device to the thread that opened it. Every call here comes from
 * the game's main thread (audio.c's MCI shim and the idle tick).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#define DEV "madeline_midi"

static int   g_open, g_started;
static DWORD g_checked;
static int   g_finished;

static int mci(const char *cmd, char *reply, unsigned n) {
    return mciSendStringA(cmd, reply, n, NULL) == 0;
}

void madeline_midi_close(void) {
    if (g_open) mci("close " DEV, NULL, 0);
    g_open = g_started = g_finished = 0;
}
int madeline_midi_open(const char *host_path) {
    char path[MAX_PATH * 2], cmd[MAX_PATH * 2 + 64];
    madeline_midi_close();
    snprintf(path, sizeof path, "%s", host_path);
    for (char *p = path; *p; p++) if (*p == '/') *p = '\\';
    snprintf(cmd, sizeof cmd, "open \"%s\" type sequencer alias " DEV, path);
    g_open = mci(cmd, NULL, 0);
    return g_open;
}
void madeline_midi_seek(int position) {
    char cmd[64];
    if (!g_open) return;
    if (position > 0) snprintf(cmd, sizeof cmd, "seek " DEV " to %d", position);
    else snprintf(cmd, sizeof cmd, "seek " DEV " to start");
    mci(cmd, NULL, 0);
    g_started = g_finished = 0;
}
int madeline_midi_finished(void) {
    /* MCI says only "stopped", for a tune that ended and for one never begun
     * alike: it has finished if it was started and is no longer playing. The
     * idle tick asks many times a second; MCI is asked four times. */
    if (!g_open || !g_started) return 0;
    DWORD now = GetTickCount();
    if (now - g_checked >= 250) {
        char mode[32] = "";
        g_checked = now;
        g_finished = mci("status " DEV " mode", mode, sizeof mode) && !strcmp(mode, "stopped");
    }
    return g_finished;
}
void madeline_midi_play(int from) {
    char cmd[64];
    if (!g_open) return;
    if (from >= 0) snprintf(cmd, sizeof cmd, "play " DEV " from %d", from);
    else {
        g_checked = GetTickCount() - 250;                /* ask now */
        if (madeline_midi_finished()) mci("seek " DEV " to start", NULL, 0);
        snprintf(cmd, sizeof cmd, "play " DEV);
    }
    mci(cmd, NULL, 0);
    g_started = 1, g_finished = 0, g_checked = GetTickCount();
}
void madeline_midi_stop(void) {
    if (g_open) mci("stop " DEV, NULL, 0);
    g_started = g_finished = 0;
}
