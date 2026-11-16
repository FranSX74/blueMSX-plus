/*****************************************************************************
** blueMSX+ native port - MIDI with the ALSA sequencer
**
** The emulator gets one sequencer client, "blueMSX", with a port per open
** device: "MIDI out" (MSX-MIDI, SFG, Music Module... to a synth), "MIDI in"
** and "Keyboard in" (the Yamaha YK keyboards). A device is chosen in the
** properties by an id string "client:port" made of the ALSA names, so it
** survives the client numbers changing between sessions; "virtual" opens
** the port without connecting it, for aconnect/qpwgraph routing.
**
** Same behaviour as Win32Midi.c: MT-32 to GM program mapping, a note
** counter per key for the keyboards with an optional channel filter, and
** silence while the emulator is paused.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "Platform.h"
#include "PlatformInternal.h"
#include "ArchMidi.h"
#include "Language.h"
#include "Properties.h"
#include "SaveState.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef BLUEMSX_HAVE_ALSA

#include <alsa/asoundlib.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>

static const UInt8 MT32toGM[128] = {
    0,   1,   2,   4,   4,   5,   5,   3,   16,  16,
    16,  16,  19,  19,  19,  21,  6,   6,   6,   7,
    7,   7,   8,   8,   62,  57,  63,  58,  38,  38,
    39,  39,  88,  33,  52,  35,  97,  100, 38,  39,
    14,  102, 68,  103, 44,  92,  46,  80,  48,  49,
    51,  45,  40,  40,  42,  42,  43,  46,  46,  24,
    25,  28,  27,  104, 32,  32,  34,  33,  36,  37,
    39,  35,  79,  73,  76,  72,  74,  75,  64,  65,
    66,  67,  71,  71,  69,  70,  60,  22,  56,  59,
    57,  63,  60,  60,  58,  61,  61,  11,  11,  99,
    100, 9,   14,  13,  12,  107, 106, 77,  78,  78,
    76,  111, 47,  117, 127, 115, 118, 116, 118, 126,
    121, 121, 55,  124, 120, 125, 126, 127
};

#define MAX_SYSEX 4096

struct ArchMidi {
    int          device;        /* in: 0 MIDI in, 1 keyboard; out: -1 */
    ArchMidiInCb callback;
    void*        ref;
};

typedef struct {
    int       port;             /* our ALSA port, -1 when closed */
    int       refCount;
    ArchMidi* owner;            /* gets the bytes (input) */
    int       notes[256];
} Device;

static struct {
    pthread_mutex_t lock;
    snd_seq_t*      seq;
    int             client;
    int             failed;

    Device          out;
    snd_midi_event_t* encoder;
    int             noteOn;
    int             program[16];    /* last program per channel, -1 none */
    int             mt32ToGm;

    Device          in[2];
    snd_midi_event_t* decoder;
    int             channelFilter;  /* 0 all, else channel + 1 */
    pthread_t       thread;
    int             threadRunning;
    int             wakePipe[2];

    int             enabled;
} midi = { PTHREAD_MUTEX_INITIALIZER, NULL, -1, 0, { -1 } };

static void initDevices(void)
{
    static int done;
    int i;
    if (done) return;
    done = 1;
    midi.out.port = -1;
    for (i = 0; i < 2; i++) midi.in[i].port = -1;
    for (i = 0; i < 16; i++) midi.program[i] = -1;
    midi.enabled = 1;
    midi.wakePipe[0] = midi.wakePipe[1] = -1;
}

/* Called with the lock held. */
static int openSequencer(void)
{
    initDevices();
    if (midi.seq != NULL) return 1;
    if (midi.failed) return 0;
    if (snd_seq_open(&midi.seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
        midi.seq = NULL;
        midi.failed = 1;
        fprintf(stderr, "MIDI: cannot open the ALSA sequencer\n");
        return 0;
    }
    snd_seq_set_client_name(midi.seq, "blueMSX");
    midi.client = snd_seq_client_id(midi.seq);
    snd_midi_event_new(MAX_SYSEX, &midi.encoder);
    snd_midi_event_new(MAX_SYSEX, &midi.decoder);
    snd_midi_event_no_status(midi.decoder, 1);  /* full status bytes for the MSX side */
    return 1;
}

/* ---- Port list ---------------------------------------------------------- */

/* Walks the sequencer's ports; `want` is the capability the other end needs. */
static int listPorts(int output, PlatformMidiPort* ports, int max)
{
    snd_seq_client_info_t* ci;
    snd_seq_port_info_t* pi;
    unsigned want = output ? (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE)
                           : (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ);
    int count = 0;

    snd_seq_client_info_alloca(&ci);
    snd_seq_port_info_alloca(&pi);
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(midi.seq, ci) >= 0) {
        int client = snd_seq_client_info_get_client(ci);
        if (client == SND_SEQ_CLIENT_SYSTEM || client == midi.client) continue;
        snd_seq_port_info_set_client(pi, client);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(midi.seq, pi) >= 0) {
            unsigned caps = snd_seq_port_info_get_capability(pi);
            unsigned type = snd_seq_port_info_get_type(pi);
            if ((caps & want) != want || (caps & SND_SEQ_PORT_CAP_NO_EXPORT)) continue;
            if (!(type & (SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_SYNTH |
                          SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_HARDWARE |
                          SND_SEQ_PORT_TYPE_SOFTWARE))) continue;
            if (count < max && ports != NULL) {
                PlatformMidiPort* p = &ports[count];
                snprintf(p->id, sizeof(p->id), "%s:%s", snd_seq_client_info_get_name(ci),
                         snd_seq_port_info_get_name(pi));
                const char* cname = snd_seq_client_info_get_name(ci);
                const char* pname = snd_seq_port_info_get_name(pi);
                /* "Midi Through Port-0" says it all; FreeBSD's "port-0" does not. */
                if (strncmp(pname, cname, strlen(cname)) == 0) {
                    snprintf(p->name, sizeof(p->name), "%s (%d:%d)", pname, client,
                             snd_seq_port_info_get_port(pi));
                }
                else {
                    snprintf(p->name, sizeof(p->name), "%s: %s (%d:%d)", cname, pname, client,
                             snd_seq_port_info_get_port(pi));
                }
            }
            count++;
        }
    }
    return count;
}

/* "client:port" by name, or a numeric "14:0" typed by hand. */
static int findPort(int output, const char* id, snd_seq_addr_t* addr)
{
    snd_seq_client_info_t* ci;
    snd_seq_port_info_t* pi;
    int c, p;
    char tail;

    if (sscanf(id, "%d:%d%c", &c, &p, &tail) == 2) {
        addr->client = c;
        addr->port = p;
        return 1;
    }
    snd_seq_client_info_alloca(&ci);
    snd_seq_port_info_alloca(&pi);
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(midi.seq, ci) >= 0) {
        const char* cname = snd_seq_client_info_get_name(ci);
        size_t n = strlen(cname);
        int client = snd_seq_client_info_get_client(ci);
        if (client == midi.client || strncmp(id, cname, n) != 0 || id[n] != ':') continue;
        snd_seq_port_info_set_client(pi, client);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(midi.seq, pi) >= 0) {
            if (strcmp(id + n + 1, snd_seq_port_info_get_name(pi)) == 0) {
                addr->client = client;
                addr->port = snd_seq_port_info_get_port(pi);
                return 1;
            }
        }
    }
    return 0;
}

static void openError(const char* id)
{
    char msg[512];
    snprintf(msg, sizeof(msg), langErrorMidiOpenFailed(), id);
    platformFrontendMessage(FRONTEND_MESSAGE_ERROR, msg);
}

/* Our port, connected to the chosen device unless it is "virtual". */
static int openPort(int output, const char* name, const char* id)
{
    unsigned caps = output ? (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)
                           : (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE);
    snd_seq_addr_t addr;
    int port;

    if (!openSequencer() || id == NULL || id[0] == 0) return -1;
    port = snd_seq_create_simple_port(midi.seq, name, caps,
                                      SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port < 0) return -1;
    if (strcmp(id, "virtual") == 0) return port;

    if (!findPort(output, id, &addr) ||
        (output ? snd_seq_connect_to(midi.seq, port, addr.client, addr.port)
                : snd_seq_connect_from(midi.seq, port, addr.client, addr.port)) < 0) {
        snd_seq_delete_simple_port(midi.seq, port);
        openError(id);
        return -1;
    }
    return port;
}

/* ---- Output ------------------------------------------------------------- */

static void sendEvent(snd_seq_event_t* ev)
{
    int tries = 100;
    snd_seq_ev_set_source(ev, midi.out.port);
    snd_seq_ev_set_subs(ev);
    snd_seq_ev_set_direct(ev);
    while (snd_seq_event_output_direct(midi.seq, ev) == -EAGAIN && --tries) {
        pthread_mutex_unlock(&midi.lock);
        usleep(200);
        pthread_mutex_lock(&midi.lock);
        if (midi.out.port < 0) return;
    }
}

static void sendProgram(int channel, int program)
{
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_pgmchange(&ev, channel, midi.mt32ToGm ? MT32toGM[program & 0x7f] : program);
    sendEvent(&ev);
}

static void allSoundOff(void)
{
    int ch;
    for (ch = 0; ch < 16; ch++) {
        snd_seq_event_t ev;
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_controller(&ev, ch, MIDI_CTL_ALL_SOUNDS_OFF, 0);
        sendEvent(&ev);
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_controller(&ev, ch, MIDI_CTL_ALL_NOTES_OFF, 0);
        sendEvent(&ev);
    }
}

ArchMidi* archMidiOutCreate(int device)
{
    Properties* p = propGetGlobalProperties();
    ArchMidi* archMidi = (ArchMidi*)calloc(1, sizeof(ArchMidi));
    archMidi->device = -1;
    if (device != 0) return archMidi;

    pthread_mutex_lock(&midi.lock);
    initDevices();
    if (midi.out.port < 0) {
        midi.out.port = openPort(1, "MIDI out", p->sound.MidiOut.name);
        if (midi.encoder != NULL) snd_midi_event_reset_encode(midi.encoder);
        midi.out.refCount = 0;
    }
    if (midi.out.port >= 0) {
        midi.out.refCount++;
        archMidi->device = 0;
    }
    pthread_mutex_unlock(&midi.lock);
    return archMidi;
}

void archMidiOutDestroy(ArchMidi* archMidi)
{
    if (archMidi == NULL) return;
    pthread_mutex_lock(&midi.lock);
    if (archMidi->device == 0 && midi.out.port >= 0 && --midi.out.refCount <= 0) {
        allSoundOff();
        snd_seq_delete_simple_port(midi.seq, midi.out.port);
        midi.out.port = -1;
    }
    pthread_mutex_unlock(&midi.lock);
    free(archMidi);
}

void archMidiOutTransmit(ArchMidi* archMidi, UInt8 value)
{
    snd_seq_event_t ev;

    if (archMidi == NULL || archMidi->device != 0) return;
    pthread_mutex_lock(&midi.lock);
    if (midi.out.port >= 0 && snd_midi_event_encode_byte(midi.encoder, value, &ev) == 1) {
        switch (ev.type) {
        case SND_SEQ_EVENT_NOTEON:
            if (ev.data.note.velocity > 0) midi.noteOn = 1;
            break;
        case SND_SEQ_EVENT_PGMCHANGE:
            midi.program[ev.data.control.channel & 15] = ev.data.control.value & 0x7f;
            if (midi.mt32ToGm) ev.data.control.value = MT32toGM[ev.data.control.value & 0x7f];
            break;
        }
        sendEvent(&ev);
    }
    pthread_mutex_unlock(&midi.lock);
}

int archMidiGetNoteOn(void)
{
    int on;
    pthread_mutex_lock(&midi.lock);
    on = midi.noteOn;
    midi.noteOn = 0;
    pthread_mutex_unlock(&midi.lock);
    return on;
}

/* The synth's volume belongs to the synth; the mixer slider is not applied. */
void archMidiUpdateVolume(int left, int right) {}

void archMidiEnable(int enable)
{
    pthread_mutex_lock(&midi.lock);
    initDevices();
    if (midi.enabled && !enable && midi.out.port >= 0) allSoundOff();
    midi.enabled = enable;
    pthread_mutex_unlock(&midi.lock);
}

void platformMidiSetMt32ToGm(int enable)
{
    int ch;
    pthread_mutex_lock(&midi.lock);
    initDevices();
    midi.mt32ToGm = enable;
    if (midi.out.port >= 0) {
        for (ch = 0; ch < 16; ch++) {
            if (midi.program[ch] >= 0) sendProgram(ch, midi.program[ch]);
        }
    }
    pthread_mutex_unlock(&midi.lock);
}

/* Saved like Win32Midi.c's program change history, so states move between
** the two; the running message buffers are not kept. */
void archMidiSaveState(void)
{
    SaveState* state = saveStateOpenForWrite("ArchMidi");
    int ch, n = 0;
    char key[32];

    pthread_mutex_lock(&midi.lock);
    initDevices();
    for (ch = 0; ch < 16; ch++) {
        if (midi.program[ch] < 0) continue;
        snprintf(key, sizeof(key), "history_%d", n++);
        saveStateSet(state, key, 0xc0 | ch | (midi.program[ch] << 8));
    }
    pthread_mutex_unlock(&midi.lock);
    for (; n < 256; n++) {
        snprintf(key, sizeof(key), "history_%d", n);
        saveStateSet(state, key, 0);
    }
    saveStateSet(state, "history_head", 0);
    saveStateClose(state);
}

void archMidiLoadState(void)
{
    SaveState* state = saveStateOpenForRead("ArchMidi");
    int head = saveStateGet(state, "history_head", 0);
    int i;
    char key[32];

    pthread_mutex_lock(&midi.lock);
    initDevices();
    for (i = 0; i < 16; i++) midi.program[i] = -1;
    for (i = 0; i < 256; i++) {     /* oldest first, the newest wins */
        UInt32 msg;
        snprintf(key, sizeof(key), "history_%d", (head + i) & 255);
        msg = saveStateGet(state, key, 0);
        if ((msg & 0xf0) == 0xc0) midi.program[msg & 15] = (msg >> 8) & 0x7f;
    }
    if (midi.encoder != NULL) snd_midi_event_reset_encode(midi.encoder);
    pthread_mutex_unlock(&midi.lock);
    saveStateClose(state);

    platformMidiSetMt32ToGm(midi.mt32ToGm);     /* resend the programs */
}

/* ---- Input -------------------------------------------------------------- */

static void handleInput(snd_seq_event_t* ev)
{
    unsigned char bytes[MAX_SYSEX];
    long length;
    int i;

    snd_midi_event_reset_decode(midi.decoder);
    length = snd_midi_event_decode(midi.decoder, bytes, sizeof(bytes), ev);
    if (length <= 0) return;

    for (i = 0; i < 2; i++) {
        Device* d = &midi.in[i];
        if (d->port < 0 || d->port != ev->dest.port) continue;
        if ((ev->type == SND_SEQ_EVENT_NOTEON || ev->type == SND_SEQ_EVENT_NOTEOFF) &&
            (midi.channelFilter == 0 || midi.channelFilter == ev->data.note.channel + 1)) {
            int note = ev->data.note.note & 0x7f;
            if (ev->type == SND_SEQ_EVENT_NOTEON && ev->data.note.velocity > 0) d->notes[note]++;
            else if (d->notes[note] > 0) d->notes[note]--;
        }
        if (midi.enabled && d->owner != NULL && d->owner->callback != NULL) {
            d->owner->callback(d->owner->ref, bytes, (UInt32)length);
        }
    }
}

static void* inputThread(void* arg)
{
    for (;;) {
        struct pollfd fds[8];
        int n, count;

        pthread_mutex_lock(&midi.lock);
        if (!midi.threadRunning) {
            pthread_mutex_unlock(&midi.lock);
            break;
        }
        fds[0].fd = midi.wakePipe[0];
        fds[0].events = POLLIN;
        count = snd_seq_poll_descriptors(midi.seq, fds + 1, 7, POLLIN);
        pthread_mutex_unlock(&midi.lock);

        n = poll(fds, count + 1, -1);
        if (n <= 0) continue;
        if (fds[0].revents & POLLIN) {
            char c;
            if (read(midi.wakePipe[0], &c, 1) < 0) {}
            continue;
        }

        pthread_mutex_lock(&midi.lock);
        for (;;) {
            snd_seq_event_t* ev = NULL;
            int r = snd_seq_event_input(midi.seq, &ev);
            if (r == -EAGAIN || r == -ENOSPC) {
                if (r == -ENOSPC) continue;     /* overrun: some input was lost */
                break;
            }
            if (r < 0 || ev == NULL) break;
            handleInput(ev);
            if (snd_seq_event_input_pending(midi.seq, 0) <= 0) break;
        }
        pthread_mutex_unlock(&midi.lock);
    }
    return NULL;
}

static void startInput(void)
{
    if (midi.threadRunning) return;
    if (midi.wakePipe[0] < 0 && pipe(midi.wakePipe) < 0) return;
    midi.threadRunning = 1;
    if (pthread_create(&midi.thread, NULL, inputThread, NULL) != 0) midi.threadRunning = 0;
}

/* Called without the lock. */
static void stopInput(void)
{
    char c = 0;
    pthread_mutex_lock(&midi.lock);
    if (!midi.threadRunning || midi.in[0].port >= 0 || midi.in[1].port >= 0) {
        pthread_mutex_unlock(&midi.lock);
        return;
    }
    midi.threadRunning = 0;
    pthread_mutex_unlock(&midi.lock);
    if (write(midi.wakePipe[1], &c, 1) < 0) {}
    pthread_join(midi.thread, NULL);
}

ArchMidi* archMidiInCreate(int device, ArchMidiInCb cb, void* ref)
{
    Properties* p = propGetGlobalProperties();
    ArchMidi* archMidi = (ArchMidi*)calloc(1, sizeof(ArchMidi));
    Device* d;

    archMidi->device = -1;
    archMidi->callback = cb;
    archMidi->ref = ref;
    if (device != 0 && device != 1) return archMidi;

    pthread_mutex_lock(&midi.lock);
    initDevices();
    d = &midi.in[device];
    if (d->port < 0) {
        d->port = openPort(0, device == 1 ? "Keyboard in" : "MIDI in",
                           device == 1 ? p->sound.YkIn.name : p->sound.MidiIn.name);
        d->refCount = 0;
        memset(d->notes, 0, sizeof(d->notes));
        if (d->port >= 0) startInput();
    }
    if (d->port >= 0) {
        d->refCount++;
        d->owner = archMidi;
        archMidi->device = device;
    }
    pthread_mutex_unlock(&midi.lock);
    return archMidi;
}

void archMidiInDestroy(ArchMidi* archMidi)
{
    if (archMidi == NULL) return;
    pthread_mutex_lock(&midi.lock);
    if (archMidi->device == 0 || archMidi->device == 1) {
        Device* d = &midi.in[archMidi->device];
        if (d->owner == archMidi) d->owner = NULL;
        if (d->port >= 0 && --d->refCount <= 0) {
            snd_seq_delete_simple_port(midi.seq, d->port);
            d->port = -1;
        }
    }
    pthread_mutex_unlock(&midi.lock);
    stopInput();
    free(archMidi);
}

int archMidiInGetNoteOn(ArchMidi* archMidi, int note)
{
    int on = 0;
    if (archMidi == NULL || (archMidi->device != 0 && archMidi->device != 1) || note < 0 || note >= 256) return 0;
    pthread_mutex_lock(&midi.lock);
    on = midi.in[archMidi->device].notes[note];
    pthread_mutex_unlock(&midi.lock);
    return on;
}

void platformMidiSetChannelFilter(int channel)
{
    int i;
    pthread_mutex_lock(&midi.lock);
    initDevices();
    midi.channelFilter = channel;
    for (i = 0; i < 2; i++) memset(midi.in[i].notes, 0, sizeof(midi.in[i].notes));
    pthread_mutex_unlock(&midi.lock);
}

int platformMidiAvailable(void)
{
    int ok;
    pthread_mutex_lock(&midi.lock);
    ok = openSequencer();
    pthread_mutex_unlock(&midi.lock);
    return ok;
}

int platformMidiListPorts(int output, PlatformMidiPort* ports, int max)
{
    int count = 0;
    pthread_mutex_lock(&midi.lock);
    if (openSequencer()) count = listPorts(output, ports, max);
    pthread_mutex_unlock(&midi.lock);
    return count;
}

void platformMidiShutdown(void)
{
    pthread_mutex_lock(&midi.lock);
    initDevices();
    if (midi.out.port >= 0) {
        allSoundOff();
        snd_seq_delete_simple_port(midi.seq, midi.out.port);
        midi.out.port = -1;
    }
    if (midi.in[0].port >= 0) snd_seq_delete_simple_port(midi.seq, midi.in[0].port);
    if (midi.in[1].port >= 0) snd_seq_delete_simple_port(midi.seq, midi.in[1].port);
    midi.in[0].port = midi.in[1].port = -1;
    pthread_mutex_unlock(&midi.lock);
    stopInput();
    pthread_mutex_lock(&midi.lock);
    if (midi.seq != NULL) {
        snd_midi_event_free(midi.encoder);
        snd_midi_event_free(midi.decoder);
        snd_seq_close(midi.seq);
        midi.seq = NULL;
    }
    pthread_mutex_unlock(&midi.lock);
}

#else   /* no ALSA: MIDI goes nowhere (a .mid file still works) */

void archMidiEnable(int enable) {}
int  archMidiGetNoteOn(void) { return 0; }
void archMidiUpdateVolume(int left, int right) {}
void archMidiLoadState(void) {}
void archMidiSaveState(void) {}
ArchMidi* archMidiInCreate(int device, ArchMidiInCb cb, void* ref) { return NULL; }
void archMidiInDestroy(ArchMidi* archMidi) {}
int  archMidiInGetNoteOn(ArchMidi* archMidi, int note) { return 0; }
ArchMidi* archMidiOutCreate(int device) { return NULL; }
void archMidiOutDestroy(ArchMidi* archMidi) {}
void archMidiOutTransmit(ArchMidi* archMidi, UInt8 value) {}

int  platformMidiAvailable(void) { return 0; }
int  platformMidiListPorts(int output, PlatformMidiPort* ports, int max) { return 0; }
void platformMidiSetMt32ToGm(int enable) {}
void platformMidiSetChannelFilter(int channel) {}
void platformMidiShutdown(void) {}

#endif
