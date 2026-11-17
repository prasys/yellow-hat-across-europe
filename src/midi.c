/*
 * midi.c - playing a Standard MIDI File with the system's own synthesizer.
 * On a Mac that is AudioToolbox's MusicPlayer and the built-in General MIDI
 * sound bank, so nothing has to be installed. On Windows it is MCI's
 * sequencer (src/win/midi_win.c). Elsewhere there is no player yet: the
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
#elif !defined(_WIN32)                                  /* Windows: src/win/midi_win.c */
int  madeline_midi_open(const char *host_path) { (void)host_path; return 0; }
void madeline_midi_close(void) {}
void madeline_midi_seek(int position) { (void)position; }
void madeline_midi_play(int from) { (void)from; }
void madeline_midi_stop(void) {}
int  madeline_midi_finished(void) { return 0; }
#endif
