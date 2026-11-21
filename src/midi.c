/*
 * midi.c - playing a Standard MIDI File with the system's own synthesizer.
 * On a Mac that is AudioToolbox's MusicPlayer and the built-in General MIDI
 * sound bank, so nothing has to be installed. On Windows it is MCI's
 * sequencer (src/win/midi_win.c). On Linux it is FluidSynth, when the build
 * found it (pkg-config fluidsynth), and a SoundFont. Without either the
 * music is silent and the game goes on.
 */
#include "madeline.h"

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

static MusicSequence g_seq;
static MusicPlayer   g_player;
static MusicTimeStamp g_length;

void madeline_midi_close(void) {
    if (g_player) { MusicPlayerStop(g_player); DisposeMusicPlayer(g_player); g_player = NULL; }
    if (g_seq) { DisposeMusicSequence(g_seq); g_seq = NULL; }
}
int madeline_midi_open(const char *host_path) {
    madeline_midi_close();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)host_path, (CFIndex)strlen(host_path), false);
    if (!url) return 0;
    int ok = NewMusicSequence(&g_seq) == noErr
          && MusicSequenceFileLoad(g_seq, url, kMusicSequenceFile_MIDIType, 0) == noErr
          && NewMusicPlayer(&g_player) == noErr
          && MusicPlayerSetSequence(g_player, g_seq) == noErr;
    CFRelease(url);
    if (!ok) { madeline_midi_close(); return 0; }
    UInt32 n = 0;
    g_length = 0;
    MusicSequenceGetTrackCount(g_seq, &n);
    for (UInt32 i = 0; i < n; i++) {
        MusicTrack t; MusicTimeStamp len = 0; UInt32 size = sizeof len;
        if (MusicSequenceGetIndTrack(g_seq, i, &t) == noErr
            && MusicTrackGetProperty(t, kSequenceTrackProperty_TrackLength, &len, &size) == noErr && len > g_length) g_length = len;
    }
    MusicPlayerPreroll(g_player);
    return 1;
}
void madeline_midi_seek(int position) { if (g_player) MusicPlayerSetTime(g_player, position > 0 ? (MusicTimeStamp)position : 0); }
void madeline_midi_play(int from) {
    if (!g_player) return;
    if (from >= 0) MusicPlayerSetTime(g_player, (MusicTimeStamp)from);
    else if (madeline_midi_finished()) MusicPlayerSetTime(g_player, 0);
    MusicPlayerStart(g_player);
}
void madeline_midi_stop(void) { if (g_player) MusicPlayerStop(g_player); }
int madeline_midi_finished(void) {
    if (!g_player) return 0;                             /* nothing playing: nothing to report */
    MusicTimeStamp t = 0;
    MusicPlayerGetTime(g_player, &t);
    return t >= g_length + 1.0;                          /* a beat for the last notes to ring */
}
#elif defined(HAVE_FLUIDSYNTH)
/* Linux (and other systems with FluidSynth): a FluidSynth player renders into
 * one of win32hle's mixer streams, beside the game's waveOut sound, so the
 * music goes wherever SDL's audio goes (PipeWire, PulseAudio, ALSA) and
 * FluidSynth's own audio drivers, which probe JACK and OSS noisily, are never
 * started. A small thread keeps the stream topped up: the game's main loop
 * stalls while a scene loads, and music fed from the idle tick would stutter.
 *
 * Linux has no General MIDI sound bank built in, so a SoundFont is needed:
 * soundfont= in madeline.ini, else the usual system places. With none, the
 * music is silent (one line says so) and the game goes on, as before. */
#include <fluidsynth.h>
#include "soundfonts.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MIDI_RATE  44100
#define MIDI_AHEAD (MIDI_RATE / 5)                       /* frames kept queued in the mixer: 200 ms */
#define MIDI_CHUNK 512

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;   /* the synth and the player, between the game and the render thread */
static fluid_settings_t *g_settings;
static fluid_synth_t *g_synth;
static fluid_player_t *g_player;
static int g_stream = -1, g_tried, g_started, g_division = 480;
static char g_path[1024];

static int readable(const char *p) { return p && *p && access(p, R_OK) == 0; }
static int find_soundfont(char *out, size_t n) {
    if (madeline_soundfont[0]) {                         /* the player's own choice: no guessing behind it */
        if (readable(madeline_soundfont)) { snprintf(out, n, "%s", madeline_soundfont); return 1; }
        fprintf(stderr, "[midi] soundfont=%s cannot be read\n", madeline_soundfont);
        return 0;
    }
    for (size_t i = 0; i < MADELINE_SOUNDFONT_COUNT; i++)   /* soundfonts.h, shared with the launcher */
        if (readable(madeline_soundfont_paths[i])) { snprintf(out, n, "%s", madeline_soundfont_paths[i]); return 1; }
    char *def = NULL;                                    /* FluidSynth's own compiled-in default */
    if (g_settings && fluid_settings_dupstr(g_settings, "synth.default-soundfont", &def) == FLUID_OK && readable(def)) {
        snprintf(out, n, "%s", def);
        fluid_free(def);
        return 1;
    }
    if (def) fluid_free(def);
    return 0;
}

static void *render(void *arg) {
    (void)arg;
    static int16_t pcm[MIDI_CHUNK * 2];
    for (;;) {
        pthread_mutex_lock(&g_lock);
        for (int queued = hle_audio_stream_queued(g_stream); queued < MIDI_AHEAD; queued += MIDI_CHUNK) {
            fluid_synth_write_s16(g_synth, MIDI_CHUNK, pcm, 0, 2, pcm, 1, 2);
            hle_audio_stream_push(g_stream, pcm, MIDI_CHUNK);
        }
        pthread_mutex_unlock(&g_lock);
        usleep(10000);
    }
    return NULL;
}

/* The synth, its SoundFont and the render thread: made at the first tune and kept. */
static int synth_up(void) {
    char sf[1024];
    pthread_t t;
    if (g_synth) return 1;
    if (g_tried) return 0;
    g_tried = 1;
    fluid_set_log_function(FLUID_INFO, NULL, NULL);     /* it would print a line per tune */
    {
        /* Making the settings lists every audio driver's devices, and the
         * libraries behind them (ALSA, JACK, OSS, via PortAudio) print a screen
         * of errors doing so, for drivers this program never starts. */
        int err = dup(2), null = open("/dev/null", O_WRONLY);
        if (err >= 0 && null >= 0) dup2(null, 2);
        g_settings = new_fluid_settings();
        if (err >= 0 && null >= 0) dup2(err, 2);
        if (err >= 0) close(err);
        if (null >= 0) close(null);
    }
    if (!g_settings) return 0;
    if (!find_soundfont(sf, sizeof sf)) {
        fprintf(stderr, "[midi] no SoundFont: the music is silent. Install one (Arch: soundfont-fluid, "
                        "Debian/Ubuntu: fluid-soundfont-gm) or set soundfont=/path/to.sf2 in madeline.ini\n");
        return 0;
    }
    fluid_settings_setnum(g_settings, "synth.sample-rate", MIDI_RATE);
    fluid_settings_setnum(g_settings, "synth.gain", 0.5);   /* FluidSynth's 0.2 is quiet beside the game's speech */
    g_synth = new_fluid_synth(g_settings);
    if (!g_synth || fluid_synth_sfload(g_synth, sf, 1) == FLUID_FAILED) {
        fprintf(stderr, "[midi] could not load the SoundFont %s: the music is silent\n", sf);
        if (g_synth) delete_fluid_synth(g_synth);
        g_synth = NULL;
        return 0;
    }
    g_stream = hle_audio_stream_open(MIDI_RATE, 2);
    if (g_stream < 0 || pthread_create(&t, NULL, render, NULL) != 0) {
        delete_fluid_synth(g_synth);
        g_synth = NULL;
        return 0;
    }
    pthread_detach(t);
    fprintf(stderr, "[midi] FluidSynth, SoundFont %s\n", sf);
    return 1;
}

/* A player at the start of the file, not playing. FluidSynth's player cannot
 * be rewound reliably once it has finished, so a rewind is a new player. Lock held. */
static int player_new(void) {
    if (g_player) { fluid_player_stop(g_player); delete_fluid_player(g_player); g_player = NULL; }
    fluid_synth_all_notes_off(g_synth, -1);
    g_started = 0;
    g_player = new_fluid_player(g_synth);
    if (g_player && fluid_player_add(g_player, g_path) == FLUID_OK) return 1;
    if (g_player) delete_fluid_player(g_player);
    g_player = NULL;
    return 0;
}
/* MCI's sequencer counts in milliseconds; FluidSynth seeks in ticks. The
 * tempo is the one playing now (120 before the file has started). */
static void player_seek_ms(int ms) {
    double bpm = fluid_player_get_bpm(g_player);
    if (bpm <= 0) bpm = 120;
    fluid_player_seek(g_player, (int)((double)ms * bpm / 60000.0 * g_division));
}
static int player_done(void) {
    return g_player && g_started && fluid_player_get_status(g_player) == FLUID_PLAYER_DONE;
}

void madeline_midi_close(void) {
    if (!g_synth) return;
    pthread_mutex_lock(&g_lock);
    if (g_player) { fluid_player_stop(g_player); delete_fluid_player(g_player); g_player = NULL; }
    fluid_synth_all_notes_off(g_synth, -1);
    g_started = 0;
    pthread_mutex_unlock(&g_lock);
}
int madeline_midi_open(const char *host_path) {
    unsigned char hdr[14];
    FILE *f;
    madeline_midi_close();
    if (!fluid_is_midifile(host_path) || !synth_up()) return 0;
    if ((f = fopen(host_path, "rb"))) {                 /* MThd: ticks per quarter note, unless it counts SMPTE frames */
        if (fread(hdr, 1, sizeof hdr, f) == sizeof hdr && !(hdr[12] & 0x80)) g_division = hdr[12] << 8 | hdr[13];
        fclose(f);
    }
    snprintf(g_path, sizeof g_path, "%s", host_path);
    pthread_mutex_lock(&g_lock);
    int ok = player_new();
    pthread_mutex_unlock(&g_lock);
    return ok;
}
void madeline_midi_seek(int position) {                 /* MCI's seek also stops it */
    if (!g_synth) return;
    pthread_mutex_lock(&g_lock);
    if (g_player && player_new() && position > 0) player_seek_ms(position);
    pthread_mutex_unlock(&g_lock);
}
void madeline_midi_play(int from) {
    if (!g_synth) return;
    pthread_mutex_lock(&g_lock);
    if (g_player && (from >= 0 || player_done())) player_new();   /* from a place, or again from the top */
    if (g_player) {
        if (from > 0) player_seek_ms(from);
        fluid_player_play(g_player);                     /* after a stop: on from where it stopped */
        g_started = 1;
    }
    pthread_mutex_unlock(&g_lock);
}
void madeline_midi_stop(void) {
    if (!g_synth) return;
    pthread_mutex_lock(&g_lock);
    if (g_player) fluid_player_stop(g_player);
    fluid_synth_all_notes_off(g_synth, -1);
    g_started = 0;
    pthread_mutex_unlock(&g_lock);
}
/* At the end of the file: the game starts its music again on being told so
 * (MM_MCINOTIFY). The tune is up to 200 ms ahead of what is heard. */
int madeline_midi_finished(void) {
    if (!g_synth) return 0;
    pthread_mutex_lock(&g_lock);
    int done = player_done();
    pthread_mutex_unlock(&g_lock);
    return done;
}
#elif !defined(_WIN32)                                  /* Windows: src/win/midi_win.c */
int  madeline_midi_open(const char *host_path) { (void)host_path; return 0; }
void madeline_midi_close(void) {}
void madeline_midi_seek(int position) { (void)position; }
void madeline_midi_play(int from) { (void)from; }
void madeline_midi_stop(void) {}
int  madeline_midi_finished(void) { return 0; }
#endif
