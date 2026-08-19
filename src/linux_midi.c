/* MIDI on Linux, over ALSA raw MIDI.
 *
 * The host layer for __linux__, the way macos_midi.m is the one for
 * macOS: run_midi / stop_midi / midi_out plus the port chooser
 * amy_midi.h declares. Started from A. Doinel's contribution (#563),
 * which is where the virtual-port arrangement below comes from.
 *
 * WHAT -1 MEANS IS NOT THE SAME ON THE TWO DESKTOPS, and it cannot be.
 * On macOS every source and every destination can be held open at once,
 * so -1 is "all of them". ALSA raw MIDI is one handle per device — there
 * is no listening to everything — so here -1 is the VIRTUAL PORT: amy
 * appears in the MIDI graph under its own name and anything is free to
 * connect to it with aconnect or a patchbay. That is the Linux idiom
 * for "no particular device", it is what a soft synth normally does,
 * and it is what #563 did before there was a choice at all. Picking a
 * device instead opens that hardware port directly.
 *
 * THE READ THREAD OWNS THE HANDLES. A selection made from the host
 * thread only sets what is WANTED and raises a flag; the thread notices
 * within one poll timeout, closes what it has and opens what was asked
 * for. Nothing else may touch the handles — snd_rawmidi_read blocks in
 * one thread while snd_rawmidi_close runs in another is exactly the
 * shape that crashes rarely and unreproducibly. midi_out is the one
 * crossing and takes the lock.
 */
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <alsa/asoundlib.h>

#include "amy_midi.h"

/* How long the reader waits on poll() before looking at its flags. Long
 * enough to cost nothing, short enough that a menu click and a quit both
 * feel immediate. #563 slept 500us in a tight read loop; polling on the
 * descriptors ALSA hands out does the same job without spinning. */
#define POLL_MS 100

/* An ALSA device string is "hw:C,D,S" — small. The display name is a
 * card name and a device name joined, so it wants more room. */
#define DEV_MAX  32
#define NAME_MAX 128

snd_rawmidi_t *midi_rx_port = NULL;
snd_rawmidi_t *midi_tx_port = NULL;
pthread_t midi_thread;

/* One lock over the handles and over what is wanted. Uncontended in the
 * ordinary case — it is taken once per outgoing message and once per
 * poll timeout. */
static pthread_mutex_t midi_lock = PTHREAD_MUTEX_INITIALIZER;

/* "" is the virtual port; anything else is an ALSA device string. The
 * NAME beside it is what makes a selection survive the numbers moving —
 * see resolve_port(). */
static char want_dev[2][DEV_MAX];
static char want_name[2][NAME_MAX];
static bool reopen_wanted = true;

int midi_linux_should_exit = false;


/* ---- what devices are there ---------------------------------------
 *
 * Walked in one fixed order — card, then device, then subdevice — so an
 * index means the same thing from one call to the next, which is the
 * whole contract of the count/name pair. */

typedef bool (*port_fn)(int card, int dev, int sub, const char *name, void *ctx);

static void each_port(uint8_t dir, port_fn fn, void *ctx)
{
    snd_rawmidi_stream_t stream = (dir == AMY_MIDI_PORT_OUT)
                                      ? SND_RAWMIDI_STREAM_OUTPUT
                                      : SND_RAWMIDI_STREAM_INPUT;
    int card = -1;
    while (snd_card_next(&card) >= 0 && card >= 0) {
        char ctlname[DEV_MAX];
        snprintf(ctlname, sizeof(ctlname), "hw:%d", card);
        snd_ctl_t *ctl;
        if (snd_ctl_open(&ctl, ctlname, 0) < 0) continue;

        char *cardname = NULL;
        if (snd_card_get_name(card, &cardname) < 0) cardname = NULL;

        int dev = -1;
        while (snd_ctl_rawmidi_next_device(ctl, &dev) >= 0 && dev >= 0) {
            snd_rawmidi_info_t *info;
            snd_rawmidi_info_alloca(&info);
            snd_rawmidi_info_set_device(info, dev);
            snd_rawmidi_info_set_stream(info, stream);
            snd_rawmidi_info_set_subdevice(info, 0);
            /* A device that has no stream in this direction is not an
             * error, it is an input-only or output-only interface. */
            if (snd_ctl_rawmidi_info(ctl, info) < 0) continue;

            unsigned int subs = snd_rawmidi_info_get_subdevices_count(info);
            for (unsigned int sub = 0; sub < subs; sub++) {
                snd_rawmidi_info_set_subdevice(info, sub);
                if (snd_ctl_rawmidi_info(ctl, info) < 0) continue;
                const char *devname = snd_rawmidi_info_get_name(info);
                const char *subname = snd_rawmidi_info_get_subdevice_name(info);

                char name[NAME_MAX];
                /* The card is what somebody recognises ("UMC404HD"), the
                 * device is what it does ("MIDI 1"), and the subdevice
                 * only earns its room when there is more than one. */
                if (subs > 1 && subname && subname[0])
                    snprintf(name, sizeof(name), "%s: %s (%s)",
                             cardname ? cardname : ctlname,
                             devname ? devname : "MIDI", subname);
                else
                    snprintf(name, sizeof(name), "%s: %s",
                             cardname ? cardname : ctlname,
                             devname ? devname : "MIDI");

                if (!fn(card, dev, (int)sub, name, ctx)) {
                    if (cardname) free(cardname);
                    snd_ctl_close(ctl);
                    return;
                }
            }
        }
        if (cardname) free(cardname);
        snd_ctl_close(ctl);
    }
}

struct counter { uint32_t n; };
static bool count_one(int card, int dev, int sub, const char *name, void *ctx)
{
    (void)card; (void)dev; (void)sub; (void)name;
    ((struct counter *)ctx)->n++;
    return true;
}

struct finder {
    uint32_t want;          /* by index... */
    const char *want_name;  /* ...or by name, whichever is set */
    uint32_t seen;
    bool found;
    char dev[DEV_MAX];
    char name[NAME_MAX];
    uint32_t index;
};

static bool find_one(int card, int dev, int sub, const char *name, void *ctx)
{
    struct finder *f = (struct finder *)ctx;
    bool hit = f->want_name ? (strcmp(name, f->want_name) == 0)
                            : (f->seen == f->want);
    if (hit) {
        snprintf(f->dev, sizeof(f->dev), "hw:%d,%d,%d", card, dev, sub);
        snprintf(f->name, sizeof(f->name), "%s", name);
        f->index = f->seen;
        f->found = true;
        return false;   /* stop walking */
    }
    f->seen++;
    return true;
}


uint32_t amy_midi_port_count(uint8_t dir)
{
    if (dir > AMY_MIDI_PORT_OUT) return 0;
    struct counter c = {0};
    each_port(dir, count_one, &c);
    return c.n;
}

uint32_t amy_midi_port_name(uint8_t dir, uint32_t index, char *buf, uint32_t buflen)
{
    if (buflen == 0) return 0;
    buf[0] = 0;
    if (dir > AMY_MIDI_PORT_OUT) return 0;
    struct finder f = {0};
    f.want = index;
    each_port(dir, find_one, &f);
    if (!f.found) return 0;
    snprintf(buf, buflen, "%s", f.name);
    return (uint32_t)strlen(buf);
}

int32_t amy_midi_port_selected(uint8_t dir)
{
    if (dir > AMY_MIDI_PORT_OUT) return -1;
    pthread_mutex_lock(&midi_lock);
    bool none = want_name[dir][0] == 0;
    struct finder f = {0};
    char name[NAME_MAX];
    snprintf(name, sizeof(name), "%s", want_name[dir]);
    pthread_mutex_unlock(&midi_lock);
    if (none) return -1;
    f.want_name = name;
    each_port(dir, find_one, &f);
    /* Chosen, and not plugged in right now. Deliberately not -1: the
     * choice stands and comes back with the cable. */
    return f.found ? (int32_t)f.index : -2;
}

void amy_midi_port_select(uint8_t dir, int32_t index)
{
    if (dir > AMY_MIDI_PORT_OUT) return;
    char dev[DEV_MAX] = {0};
    char name[NAME_MAX] = {0};
    if (index >= 0) {
        struct finder f = {0};
        f.want = (uint32_t)index;
        each_port(dir, find_one, &f);
        /* An index past the end is "no particular device" rather than an
         * error: the list a caller picked from is a snapshot, and
         * unplugging something between the two is nobody's bug. */
        if (f.found) {
            snprintf(dev, sizeof(dev), "%s", f.dev);
            snprintf(name, sizeof(name), "%s", f.name);
        }
    }
    pthread_mutex_lock(&midi_lock);
    memcpy(want_dev[dir], dev, sizeof(dev));
    memcpy(want_name[dir], name, sizeof(name));
    reopen_wanted = true;
    pthread_mutex_unlock(&midi_lock);
}


/* ---- opening what was asked for ------------------------------------ */

/* Where a remembered port is NOW, which is not where it was.
 * "hw:1,0,0" is a position in a list, not an identity — plug a second
 * interface in and yesterday's string is somebody else's device. So the
 * stored string is only a HINT: it is used when the name at that
 * position still matches, and otherwise the name is looked for
 * elsewhere. Same guarantee macOS gets from CoreMIDI's unique ids,
 * which ALSA has no equivalent of. */
static bool resolve_port(uint8_t dir, char *out, size_t outlen)
{
    if (want_name[dir][0] == 0) return false;     /* the virtual port */
    struct finder f = {0};
    f.want_name = want_name[dir];
    each_port(dir, find_one, &f);
    if (!f.found) return false;                   /* gone: fall back */
    snprintf(out, outlen, "%s", f.dev);
    return true;
}

/* Called on the read thread ONLY, with the lock held. */
static void reopen_ports(void)
{
    if (midi_rx_port) { snd_rawmidi_close(midi_rx_port); midi_rx_port = NULL; }
    if (midi_tx_port) { snd_rawmidi_close(midi_tx_port); midi_tx_port = NULL; }

    char in_dev[DEV_MAX], out_dev[DEV_MAX];
    bool have_in = resolve_port(AMY_MIDI_PORT_IN, in_dev, sizeof(in_dev));
    bool have_out = resolve_port(AMY_MIDI_PORT_OUT, out_dev, sizeof(out_dev));

    int err;
    if (!have_in && !have_out) {
        /* BOTH DEFAULT: one duplex virtual port, which is exactly the
         * call #563 shipped and the only arrangement anybody has run.
         * Kept as its own case rather than opening two halves, because
         * the known-working path should stay the common one. */
        err = snd_rawmidi_open(&midi_rx_port, &midi_tx_port, "virtual",
                               SND_RAWMIDI_NONBLOCK);
        if (err < 0) {
            /* NOT exit(1). This is a library inside somebody's
             * application: no MIDI is a machine that still makes sound,
             * and taking the process down over it is not amy's call. */
            fprintf(stderr, "amy: could not open the virtual MIDI port: %s\n",
                    snd_strerror(err));
            midi_rx_port = midi_tx_port = NULL;
        }
        return;
    }

    err = snd_rawmidi_open(&midi_rx_port, NULL, have_in ? in_dev : "virtual",
                           SND_RAWMIDI_NONBLOCK);
    if (err < 0) {
        fprintf(stderr, "amy: could not open MIDI in (%s): %s\n",
                have_in ? in_dev : "virtual", snd_strerror(err));
        midi_rx_port = NULL;
    }
    err = snd_rawmidi_open(NULL, &midi_tx_port, have_out ? out_dev : "virtual", 0);
    if (err < 0) {
        fprintf(stderr, "amy: could not open MIDI out (%s): %s\n",
                have_out ? out_dev : "virtual", snd_strerror(err));
        midi_tx_port = NULL;
    }
}


void midi_out(uint8_t *bytes, uint16_t len)
{
    /* EVERY midi_out must call this (amy_midi.h says so, and macOS
     * does): it is how amy_config's external output hook is reached, so
     * an embedding host that mirrors outgoing MIDI hears nothing without
     * it. #563 left it out. */
    midi_out_external_hook(bytes, len);
    pthread_mutex_lock(&midi_lock);
    /* A write to a port that was never opened, or has just been closed
     * under a selection change, is a crash rather than a dropped
     * message — the same guard macOS keeps on out_port. */
    if (midi_tx_port) snd_rawmidi_write(midi_tx_port, bytes, len);
    pthread_mutex_unlock(&midi_lock);
}


void *run_midi_linux(void *argp)
{
    (void)argp;
    uint8_t data[MAX_MIDI_BYTES_TO_PARSE];

    while (!midi_linux_should_exit) {
        pthread_mutex_lock(&midi_lock);
        if (reopen_wanted) {
            reopen_wanted = false;
            reopen_ports();
        }
        snd_rawmidi_t *rx = midi_rx_port;
        pthread_mutex_unlock(&midi_lock);

        if (rx == NULL) {
            /* Nothing open — usually a device that would not open. Wait
             * out a poll interval so a later selection still gets in. */
            usleep(POLL_MS * 1000);
            continue;
        }

        int nfds = snd_rawmidi_poll_descriptors_count(rx);
        if (nfds <= 0) { usleep(POLL_MS * 1000); continue; }
        struct pollfd pfds[16];
        if (nfds > (int)(sizeof(pfds) / sizeof(pfds[0])))
            nfds = (int)(sizeof(pfds) / sizeof(pfds[0]));
        snd_rawmidi_poll_descriptors(rx, pfds, (unsigned int)nfds);

        /* The TIMEOUT is what makes a selection change and a quit take
         * effect: a blocking read would sit here until the next note
         * arrived, which on an idle keyboard is never. */
        if (poll(pfds, (nfds_t)nfds, POLL_MS) <= 0) continue;

        unsigned short revents = 0;
        if (snd_rawmidi_poll_descriptors_revents(rx, pfds, (unsigned int)nfds,
                                                 &revents) < 0) continue;
        if (!(revents & POLLIN)) continue;

        pthread_mutex_lock(&midi_lock);
        /* Re-check: the handle may have been replaced between the poll
         * and here by a selection made on the host thread. */
        ssize_t len = midi_rx_port ? snd_rawmidi_read(midi_rx_port, data, sizeof(data)) : -1;
        pthread_mutex_unlock(&midi_lock);

        if (len > 0) convert_midi_bytes_to_messages(data, (size_t)len, 0);
    }

    pthread_mutex_lock(&midi_lock);
    if (midi_rx_port) { snd_rawmidi_close(midi_rx_port); midi_rx_port = NULL; }
    if (midi_tx_port) { snd_rawmidi_close(midi_tx_port); midi_tx_port = NULL; }
    pthread_mutex_unlock(&midi_lock);
    return NULL;
}

void run_midi(void)
{
    if (sysex_buffer == NULL) {   // has not been started yet.
        sysex_buffer = malloc(MAX_SYSEX_BYTES);
        pthread_create(&midi_thread, NULL, run_midi_linux, NULL);
    }
}

void stop_midi(void)
{
    if (sysex_buffer) {
        midi_linux_should_exit = true;
        pthread_join(midi_thread, NULL);
        free(sysex_buffer);
        sysex_buffer = NULL;
        midi_linux_should_exit = false;
    }
}
