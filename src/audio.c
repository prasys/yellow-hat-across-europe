/*
 * audio.c - the game's sound: waveOut for speech, effects and the movies'
 * soundtracks, and MCI for the MIDI music.
 *
 * win32hle's waveOut is a silent device and it has no mciSendCommandA, so both
 * are answered here. waveOut data goes to win32hle's SDL mixer as a stream; a
 * buffer is reported done (WHDR_DONE, and the callback the game asked for)
 * when its last sample has actually been played, because the game times its
 * animation by that. MCI plays a MIDI file through midi.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "madeline.h"

static int g_trace = -1;
static int trace(void) { if (g_trace < 0) g_trace = getenv("MADELINE_AUDIOTRACE") != NULL; return g_trace; }

/* ------------------------------------------------------------- waveOut */
#define H_WAVEOUT       0x0A0D1001u
#define WHDR_DONE       0x01u
#define WHDR_PREPARED   0x02u
#define WHDR_INQUEUE    0x10u
#define MM_WOM_OPEN     0x3BBu
#define MM_WOM_CLOSE    0x3BCu
#define MM_WOM_DONE     0x3BDu
#define CALLBACK_TYPEMASK 0x00070000u
#define CALLBACK_WINDOW   0x00010000u
#define CALLBACK_FUNCTION 0x00030000u
#define WAVE_FORMAT_QUERY 0x0001u

typedef struct pending { uint32_t hdr; uint64_t end; struct pending *next; } pending_t;
static struct {
    int open, stream, rate, channels, bits;
    uint32_t cb, inst, cbtype;
    int16_t *fifo; size_t fifo_n, fifo_cap, fifo_rd;      /* samples waiting to enter the mixer's ring */
    uint64_t written, pushed;                              /* frames the game wrote; frames handed to the mixer */
    uint64_t t0_ns; uint64_t silent_base;                  /* no audio device: time stands in for the play cursor */
    pending_t *head, *tail;
} W;

static uint64_t now_ns(void) { return madeline_clock_ns(); }

static void wave_notify(uint32_t msg, uint32_t hdr) {
    if (W.cbtype == CALLBACK_WINDOW) hle_post_message(W.cb, msg, H_WAVEOUT, hdr);
    else if (W.cbtype == CALLBACK_FUNCTION && W.cb) {
        uint32_t a[5] = { H_WAVEOUT, msg, W.inst, hdr, 0 };
        hle_call_guest(W.cb, 5, a);
    }
}
/* Frames played so far. */
static uint64_t wave_played(void) {
    if (W.stream < 0) {
        uint64_t f = W.silent_base + (now_ns() - W.t0_ns) * (uint64_t)W.rate / 1000000000ull;
        return f > W.written ? W.written : f;
    }
    return W.pushed - (uint64_t)hle_audio_stream_queued(W.stream);
}
/* Feed the mixer (about half a second ahead) and retire what has been heard. */
static void wave_service(void) {
    if (!W.open) return;
    if (W.stream >= 0) {
        int room = W.rate / 2 - hle_audio_stream_queued(W.stream);
        size_t have = (W.fifo_n - W.fifo_rd) / (size_t)W.channels;
        if (room > 0 && have) {
            size_t n = have < (size_t)room ? have : (size_t)room;
            hle_audio_stream_push(W.stream, W.fifo + W.fifo_rd, (int)n);
            W.fifo_rd += n * (size_t)W.channels, W.pushed += n;
            if (W.fifo_rd == W.fifo_n) W.fifo_rd = W.fifo_n = 0;
        }
    }
    uint64_t played = wave_played();
    while (W.head && W.head->end <= played) {
        pending_t *p = W.head;
        W.head = p->next;
        if (!W.head) W.tail = NULL;
        MEM32(p->hdr + 16) = (MEM32(p->hdr + 16) & ~WHDR_INQUEUE) | WHDR_DONE;
        uint32_t hdr = p->hdr;
        free(p);
        wave_notify(MM_WOM_DONE, hdr);
    }
}
static void wave_flush(void) {                            /* everything queued is over, now */
    if (W.stream >= 0) { hle_audio_stream_close(W.stream); W.stream = hle_audio_stream_open(W.rate, W.channels); }   /* empties the ring */
    W.fifo_rd = W.fifo_n = 0;
    W.pushed = 0, W.written = 0, W.silent_base = 0, W.t0_ns = now_ns();
    while (W.head) {
        pending_t *p = W.head;
        W.head = p->next;
        MEM32(p->hdr + 16) = (MEM32(p->hdr + 16) & ~WHDR_INQUEUE) | WHDR_DONE;
        uint32_t hdr = p->hdr;
        free(p);
        wave_notify(MM_WOM_DONE, hdr);
    }
    W.tail = NULL;
}

static void a_waveOutOpen(void) {                        /* (&hwo, device, &WAVEFORMATEX, callback, instance, flags) */
    uint32_t out = A32(0), fmt = A32(2), flags = A32(5);
    int tag = MEM16(fmt), ch = MEM16(fmt + 2), rate = (int)MEM32(fmt + 4), bits = MEM16(fmt + 14);
    if (trace()) fprintf(stderr, "[audio] waveOutOpen tag %d, %d Hz, %d ch, %d bit, flags 0x%X\n", tag, rate, ch, bits, flags);
    if (tag != 1 || (bits != 8 && bits != 16) || ch < 1 || ch > 2 || rate < 4000) RET(32u /* WAVERR_BADFORMAT */, 6);
    if (flags & WAVE_FORMAT_QUERY) RET(0, 6);
    if (W.open) RET(4u /* MMSYSERR_ALLOCATED */, 6);
    memset(&W, 0, sizeof W);
    W.open = 1, W.rate = rate, W.channels = ch, W.bits = bits;
    W.cb = A32(3), W.inst = A32(4), W.cbtype = flags & CALLBACK_TYPEMASK;
    W.stream = madeline_speed() == 1.0 ? hle_audio_stream_open(rate, ch) : -1;   /* fast-forward is silent */
    W.t0_ns = now_ns();
    if (W.stream < 0 && trace()) fprintf(stderr, "[audio] no audio device: playing silently, in time\n");
    if (out) MEM32(out) = H_WAVEOUT;
    wave_notify(MM_WOM_OPEN, 0);
    RET(0, 6);
}
static void a_waveOutPrepareHeader(void)   { if (A32(1)) MEM32(A32(1) + 16) = (MEM32(A32(1) + 16) | WHDR_PREPARED) & ~WHDR_DONE; RET(0, 3); }
static void a_waveOutUnprepareHeader(void) {
    wave_service();
    uint32_t h = A32(1);
    if (h && (MEM32(h + 16) & WHDR_INQUEUE)) RET(33u /* WAVERR_STILLPLAYING */, 3);
    if (h) MEM32(h + 16) &= ~WHDR_PREPARED;
    RET(0, 3);
}
static void a_waveOutWrite(void) {                       /* (hwo, &WAVEHDR, size) */
    uint32_t h = A32(1);
    if (!W.open || !h) RET(5u /* MMSYSERR_INVALHANDLE */, 3);
    const uint8_t *data = (const uint8_t *)hle_ptr(MEM32(h));
    uint32_t bytes = MEM32(h + 4);
    size_t samples = W.bits == 16 ? bytes / 2 : bytes, frames = samples / (size_t)W.channels;
    samples = frames * (size_t)W.channels;
    if (W.fifo_n + samples > W.fifo_cap) {
        W.fifo_cap = (W.fifo_n + samples) * 2;
        W.fifo = (int16_t *)realloc(W.fifo, W.fifo_cap * sizeof(int16_t));
    }
    if (data) {
        if (W.bits == 16) memcpy(W.fifo + W.fifo_n, data, samples * 2);
        else for (size_t i = 0; i < samples; i++) W.fifo[W.fifo_n + i] = (int16_t)((data[i] - 128) << 8);
    } else memset(W.fifo + W.fifo_n, 0, samples * 2);
    if (W.stream >= 0) W.fifo_n += samples;
    if (!W.head && W.stream < 0) { W.silent_base = W.written; W.t0_ns = now_ns(); }   /* the device was idle: time starts here */
    W.written += frames;
    pending_t *p = (pending_t *)calloc(1, sizeof *p);
    p->hdr = h, p->end = W.written;
    if (W.tail) W.tail->next = p; else W.head = p;
    W.tail = p;
    MEM32(h + 16) = (MEM32(h + 16) | WHDR_INQUEUE) & ~WHDR_DONE;
    if (trace() > 1) fprintf(stderr, "[audio] waveOutWrite %u bytes\n", bytes);
    wave_service();
    RET(0, 3);
}
static void a_waveOutReset(void) { if (W.open) wave_flush(); RET(0, 1); }
static void a_waveOutClose(void) {
    if (!W.open) RET(5u, 1);
    wave_flush();
    if (W.stream >= 0) hle_audio_stream_close(W.stream);
    free(W.fifo);
    wave_notify(MM_WOM_CLOSE, 0);
    memset(&W, 0, sizeof W);
    RET(0, 1);
}

/* ----------------------------------------------------------------- MCI */
#define MCI_OPEN    0x0803u
#define MCI_CLOSE   0x0804u
#define MCI_PLAY    0x0806u
#define MCI_SEEK    0x0807u
#define MCI_STOP    0x0808u
#define MCI_PAUSE   0x0809u
#define MCI_SET     0x080Du
#define MCI_STATUS  0x0814u
#define MCI_RESUME  0x0855u
#define MCI_NOTIFY          0x0001u
#define MCI_FROM            0x0004u
#define MCI_STATUS_ITEM     0x0100u
#define MCI_OPEN_ELEMENT    0x0200u
#define MCI_SEEK_TO_START   0x0100u
#define MM_MCINOTIFY        0x03B9u
#define MCI_NOTIFY_SUCCESSFUL 1u
#define MCI_MODE_STOP 525u
#define MCI_MODE_PLAY 526u
#define MCI_DEVICE_ID 1u

static struct { int open, playing; uint32_t notify_hwnd; } M;

static void a_mciSendCommandA(void) {                    /* (device, message, flags, &parms) */
    uint32_t dev = A32(0), msg = A32(1), flags = A32(2), p = A32(3);
    uint32_t rc = 0;
    switch (msg) {
    case MCI_OPEN: {                                     /* MCI_OPEN_PARMS: callback, id, type, element, alias */
        const char *type = p && MEM32(p + 8) > 0xFFFF ? (const char *)hle_ptr(MEM32(p + 8)) : "";
        const char *name = p && (flags & MCI_OPEN_ELEMENT) && MEM32(p + 12) ? (const char *)hle_ptr(MEM32(p + 12)) : NULL;
        char host[1024];
        if (trace()) fprintf(stderr, "[audio] MCI open type \"%s\" file \"%s\"\n", type, name ? name : "");
        if (!name || !hle_host_path(name, host, sizeof host)) { rc = 275u /* MCIERR_FILE_NOT_FOUND */; break; }
        if (M.open) madeline_midi_close();
        M.open = madeline_speed() == 1.0 && madeline_midi_open(host), M.playing = 0;
        if (!M.open && trace()) fprintf(stderr, "[audio] no MIDI player here: %s stays silent\n", host);
        M.open = 1;                                      /* the game goes on either way */
        MEM32(p + 4) = MCI_DEVICE_ID;
        break;
    }
    case MCI_PLAY:                                       /* MCI_PLAY_PARMS: callback, from, to */
        if (trace()) fprintf(stderr, "[audio] MCI play flags 0x%X\n", flags);
        if (!M.open) { rc = 263u /* MCIERR_INVALID_DEVICE_ID */; break; }
        madeline_midi_play((flags & MCI_FROM) && p ? (int)MEM32(p + 4) : -1);
        M.playing = 1;
        M.notify_hwnd = (flags & MCI_NOTIFY) && p ? MEM32(p) & 0xFFFFu ? MEM32(p) : 0 : 0;
        break;
    case MCI_SEEK:
        if (M.open) madeline_midi_seek((flags & MCI_SEEK_TO_START) || !p ? 0 : (int)MEM32(p + 4));
        break;
    case MCI_STOP: case MCI_PAUSE:
        if (trace()) fprintf(stderr, "[audio] MCI stop\n");
        if (M.open) madeline_midi_stop();
        M.playing = 0, M.notify_hwnd = 0;
        break;
    case MCI_RESUME:
        if (M.open) { madeline_midi_play(-1); M.playing = 1; }
        break;
    case MCI_CLOSE:
        if (trace()) fprintf(stderr, "[audio] MCI close\n");
        if (M.open) madeline_midi_close();
        memset(&M, 0, sizeof M);
        break;
    case MCI_STATUS:                                     /* MCI_STATUS_PARMS: callback, return, item, track */
        if (p && (flags & MCI_STATUS_ITEM)) MEM32(p + 4) = MEM32(p + 8) == 4u /* MCI_STATUS_MODE */ ? (M.playing ? MCI_MODE_PLAY : MCI_MODE_STOP) : 0;
        break;
    case MCI_SET:
        break;
    default:
        if (trace()) fprintf(stderr, "[audio] MCI message 0x%X (device %u) not answered\n", msg, dev);
        break;
    }
    RET(rc, 4);
}

/* Called often (from the clock): move sound along, and tell the game when the music ends. */
void madeline_audio_tick(void) {
    wave_service();
    if (M.playing && madeline_midi_finished()) {
        M.playing = 0;
        if (trace()) fprintf(stderr, "[audio] MIDI finished\n");
        if (M.notify_hwnd) hle_post_message(M.notify_hwnd, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, MCI_DEVICE_ID);
        M.notify_hwnd = 0;
    }
}

const win32hle_shim madeline_audio[] = {
    { "waveOutOpen", a_waveOutOpen }, { "waveOutClose", a_waveOutClose }, { "waveOutReset", a_waveOutReset },
    { "waveOutPrepareHeader", a_waveOutPrepareHeader }, { "waveOutUnprepareHeader", a_waveOutUnprepareHeader },
    { "waveOutWrite", a_waveOutWrite }, { "mciSendCommandA", a_mciSendCommandA },
    { 0, 0 }
};
