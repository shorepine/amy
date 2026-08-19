#ifndef __EMSCRIPTEN__
#define unichar OSX_UNICHAR
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMidi/CoreMidi.h>
#include <mach/mach_time.h>
#include <pthread.h>
#undef unichar
#import "amy_midi.h"

static CGEventSourceRef eventSource;
MIDIClientRef midi_client;
MIDIPortRef out_port;
static MIDIPortRef in_port;

/* WHICH PORT, and why it is not an index.
 *
 * amy listens to every source and sends to every destination unless told
 * otherwise, which is what a plugged-in keyboard playing straight away
 * depends on. A host that offers the choice hands us an INDEX, because
 * that is what it just drew a list of -- but an index is only true until
 * the next thing is plugged in, and a selection that quietly becomes a
 * different device is much worse than one that goes away. So the index is
 * resolved ONCE, at the moment it is chosen, into CoreMIDI's own
 * persistent unique id; everything afterwards asks for that.
 *
 * 0 is kMIDIInvalidUniqueID, so it does for "no choice made" -- i.e. all
 * of them. */
static MIDIUniqueID sel_uid[2];

/* The sources we have an open connection to, so a setup change can take
 * them all down and put the current ones back. 64 is far past any real
 * desk and costs 512 bytes. */
#define MAX_SOURCES 64
static MIDIEndpointRef connected[MAX_SOURCES];
static ItemCount connected_n;

static ItemCount port_count(uint8_t dir)
{
    /* NOT MERELY A "not started yet" SHORTCUT. A CoreMIDI call from a
     * process that has never made a client BLOCKS -- midi_out() has
     * carried a guard for this for the same reason, and the first host
     * to ask for the list before amy_start() got a machine that hung
     * with no output and no error. With no client there are no ports,
     * which is also true. */
    if (midi_client == 0) return 0;
    return dir == AMY_MIDI_PORT_OUT ? MIDIGetNumberOfDestinations()
                                    : MIDIGetNumberOfSources();
}

static MIDIEndpointRef port_at(uint8_t dir, ItemCount i)
{
    return dir == AMY_MIDI_PORT_OUT ? MIDIGetDestination(i) : MIDIGetSource(i);
}

static MIDIUniqueID port_uid(MIDIEndpointRef ep)
{
    MIDIUniqueID uid = 0;
    if (ep) MIDIObjectGetIntegerProperty(ep, kMIDIPropertyUniqueID, &uid);
    return uid;
}

/* Connect the input port to every source there is, tagging each with its
 * own unique id -- the callback then filters on that, so changing the
 * selection costs nothing and needs no run-loop interaction.
 *
 * Called from the MIDI thread only: once at startup and again from
 * NotifyProc, which CoreMIDI delivers on the very run loop this is
 * standing in. */
static void connect_sources(void)
{
    if (in_port == 0) return;
    for (ItemCount i = 0; i < connected_n; i++)
        MIDIPortDisconnectSource(in_port, connected[i]);
    connected_n = 0;
    ItemCount n = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < n && connected_n < MAX_SOURCES; i++) {
        MIDIEndpointRef src = MIDIGetSource(i);
        MIDIUniqueID uid = port_uid(src);
        OSStatus st = MIDIPortConnectSource(in_port, src,
                                            (void *)(intptr_t)uid);
        if (st != noErr)
            fprintf(stderr, "Error %d while connecting MIDI input port to source\n", (int)st);
        else
            connected[connected_n++] = src;
    }
}

/* A keyboard plugged in after we started is a source nothing is connected
 * to -- it appears in the list, it can be selected, and it is silent.
 * That was invisible while amy took every port; with a chooser in front of
 * it, it is the first thing anybody would try. */
static void NotifyProc(const MIDINotification *message, void *refCon)
{
    if (message->messageID == kMIDIMsgSetupChanged)
        connect_sources();
}

static uint8_t midi_status_len(uint8_t status) {
    // MIDI realtime/system common lengths.
    if (status >= 0xF8) return 1;
    if (status == 0xF6) return 1;
    if (status == 0xF1 || status == 0xF3) return 2;
    if (status == 0xF2) return 3;
    if (status == 0xF4 || status == 0xF5 || status == 0xF9 || status == 0xFD) return 1;

    // Channel voice lengths.
    switch (status & 0xF0) {
        case 0x80:
        case 0x90:
        case 0xA0:
        case 0xB0:
        case 0xE0:
            return 3;
        case 0xC0:
        case 0xD0:
            return 2;
        default:
            return 0;
    }
}


void midi_out(uint8_t * bytes, uint16_t len) {
    midi_out_external_hook(bytes, len);
    // If run_midi() was never called (config.midi didn't include MACOS),
    // there's no client/port; CoreMIDI calls from an uninitialized process
    // can block for a long time (e.g. per-tick clock out after a wire zC2).
    if (out_port == 0) return;
    if (@available(macOS 11, *))  {
        MIDIPacketList pl;
        MIDIPacket *p;
        p = MIDIPacketListInit(&pl);
        p = MIDIPacketListAdd(&pl, 1024, p, 0, len, bytes);
        if (sel_uid[AMY_MIDI_PORT_OUT]) {
            /* One destination was chosen. If it is not on the desk right
             * now this sends nowhere, which is the honest answer -- the
             * alternative is quietly reverting to broadcasting at
             * everything, which is not what was asked for. */
            MIDIObjectRef obj = 0;
            MIDIObjectType type;
            if (MIDIObjectFindByUniqueID(sel_uid[AMY_MIDI_PORT_OUT], &obj, &type) == noErr
                && type == kMIDIObjectType_Destination)
                MIDISend(out_port, (MIDIEndpointRef)obj, &pl);
        } else {
            for (NSUInteger endpointRefIndex = 0; endpointRefIndex < MIDIGetNumberOfDestinations(); ++endpointRefIndex) {
                MIDIObjectRef destinationEndpoint = MIDIGetDestination(endpointRefIndex);
                MIDISend(out_port, destinationEndpoint, &pl);
            }
        }
    } else {
        fprintf(stderr, "Can only run MIDI on macOS Big Sur (11.0) or later, sorry\n");   
    }

}

int midi_macos_should_exit = false;

void* run_midi_macos(void*argp){
    //sysex_buffer = malloc(MAX_SYSEX_BYTES);

    if (@available(macOS 11, *))  {
        @autoreleasepool {
            //py_midi_callback = 0;

            OSStatus status = MIDIClientCreate((__bridge CFStringRef)@"Tulip", NotifyProc, NULL, &midi_client);
            if (status != noErr) {
                fprintf(stderr, "Error %d while setting up handlers\n", status);
            }


            status = MIDIOutputPortCreate(midi_client, (__bridge CFStringRef)[NSString stringWithFormat:@"Tulip Output"], &out_port );
            if(status != noErr) {
                fprintf(stderr, "Error %d while setting up MIDI output port\n", status);
            }

            eventSource = CGEventSourceCreate(kCGEventSourceStatePrivate);
            /* ONE input port, every source connected to it, each tagged
             * with its own unique id. It was a port per source, which
             * gave the callback nothing to tell them apart by -- and a
             * chooser needs exactly that. */
            status = MIDIInputPortCreateWithProtocol(midi_client, (__bridge CFStringRef)@"Tulip Input", kMIDIProtocol_1_0, &in_port, ^(const MIDIEventList *evtlist, void *srcConnRefCon) {
                    OSStatus sysex_status = -1;
                    /* Which source this arrived on -- connect_sources()
                     * put its unique id in the refCon. Filtering here
                     * rather than by connecting and disconnecting means a
                     * selection change is one store from any thread. */
                    if (sel_uid[AMY_MIDI_PORT_IN]
                        && (MIDIUniqueID)(intptr_t)srcConnRefCon != sel_uid[AMY_MIDI_PORT_IN])
                        return;
                    for (uint32_t i = 0; i < evtlist->numPackets; i++) {
                        const MIDIEventPacket* packet = &evtlist->packet[i];
                        if(@available(macos 14, *)) {
                            CFDataRef  outData;
                            sysex_status = MIDIEventPacketSysexBytesForGroup(packet,0, &outData);
                            if(sysex_status == noErr) {
                                const uint8_t * sysex_bytes = CFDataGetBytePtr(outData);
                                for(uint16_t i=0;i<CFDataGetLength(outData);i++) {
                                    sysex_buffer[sysex_len++] = sysex_bytes[i];
                                    if(sysex_bytes[i]==0xf7) parse_sysex();
                                }
                            }
                        }
                        // Just a plain ol midi packet
                        if(sysex_status != noErr) { 
                            for(uint32_t j=0 ; j < packet->wordCount; j++) {
                                const unsigned char *bytes = (unsigned char*)(&packet->words[j]);
                                uint8_t mt = bytes[3] >> 4;
                                // Accept MIDI 1.0 Channel Voice (0x2) and System (0x1) UMP words.
                                if (mt == 0x1 || mt == 0x2) {
                                    uint8_t data[3] = { bytes[2], bytes[1], bytes[0] };
                                    uint8_t len = midi_status_len(data[0]);
                                    if (len > 0) {
                                        convert_midi_bytes_to_messages(data, len, 1);
                                    }
                                }
                            }
                        }
                    }
                });
            if (status != noErr) {
                fprintf(stderr, "Error %d while setting up MIDI input port\n", status);
            }
            connect_sources();
            CFRunLoopRun();
        }
    } else {
        fprintf(stderr, "Can only run MIDI on macOS Big Sur (11.0) or later, sorry\n");
    }
    return NULL;
}

void run_midi() {
    if (sysex_buffer == NULL) {  // has not been started yet.
        sysex_buffer = malloc(MAX_SYSEX_BYTES);
        pthread_t midi_thread_id;
        pthread_create(&midi_thread_id, NULL, run_midi_macos, NULL);
    }
}

void stop_midi() {
     // Normally, we'd have to remove all the sources and timers from the CFRunLoop, but we'll fudge it.
}


/* ---- Which MIDI port (see amy_midi.h) ------------------------------ */

uint32_t amy_midi_port_count(uint8_t dir) {
    return (uint32_t)port_count(dir);
}

uint32_t amy_midi_port_name(uint8_t dir, uint32_t index, char *buf, uint32_t buflen) {
    if (buflen == 0) return 0;
    buf[0] = 0;
    if (index >= (uint32_t)port_count(dir)) return 0;
    MIDIEndpointRef ep = port_at(dir, index);
    CFStringRef name = NULL;
    /* The DISPLAY name -- what the Audio MIDI Setup window calls it,
     * which is "Scarlett 2i2 USB" where the endpoint's own name is
     * "Port 1". kMIDIPropertyName is the fallback for the rare endpoint
     * that has no display name. */
    if (MIDIObjectGetStringProperty(ep, kMIDIPropertyDisplayName, &name) != noErr || name == NULL)
        if (MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &name) != noErr)
            return 0;
    if (name == NULL) return 0;
    Boolean ok = CFStringGetCString(name, buf, (CFIndex)buflen, kCFStringEncodingUTF8);
    CFRelease(name);
    if (!ok) { buf[0] = 0; return 0; }
    return (uint32_t)strlen(buf);
}

int32_t amy_midi_port_selected(uint8_t dir) {
    if (dir > AMY_MIDI_PORT_OUT || midi_client == 0) return -1;
    if (sel_uid[dir] == 0) return -1;
    ItemCount n = port_count(dir);
    for (ItemCount i = 0; i < n; i++)
        if (port_uid(port_at(dir, i)) == sel_uid[dir]) return (int32_t)i;
    /* Chosen, and not on the desk right now. Not the same as -1: the
     * choice stands and comes back with the cable. */
    return -2;
}

void amy_midi_port_select(uint8_t dir, int32_t index) {
    if (dir > AMY_MIDI_PORT_OUT || midi_client == 0) return;
    if (index < 0 || (ItemCount)index >= port_count(dir)) {
        sel_uid[dir] = 0;
        return;
    }
    /* Resolved to a unique id HERE and never looked up by number again --
     * the index is only true until the next thing is plugged in. */
    sel_uid[dir] = port_uid(port_at(dir, (ItemCount)index));
}

#endif
