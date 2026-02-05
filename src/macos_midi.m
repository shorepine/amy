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


static void NotifyProc(const MIDINotification *message, void *refCon)
{
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
    if (@available(macOS 11, *))  {
        MIDIPacketList pl;
        MIDIPacket *p;
        p = MIDIPacketListInit(&pl);
        p = MIDIPacketListAdd(&pl, 1024, p, 0, len, bytes);
        for (NSUInteger endpointRefIndex = 0; endpointRefIndex < MIDIGetNumberOfDestinations(); ++endpointRefIndex) {
            MIDIObjectRef destinationEndpoint = MIDIGetDestination(endpointRefIndex);
            MIDISend(out_port, destinationEndpoint, &pl);
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
            ItemCount number_sources = MIDIGetNumberOfSources();
            for (unsigned long i = 0; i < number_sources; i++) {
                MIDIEndpointRef source = MIDIGetSource(i);
                MIDIPortRef in_port;
                status = MIDIInputPortCreateWithProtocol(midi_client, (__bridge CFStringRef)[NSString stringWithFormat:@"Tulip Input %lu", i], kMIDIProtocol_1_0, &in_port, ^(const MIDIEventList *evtlist, void *srcConnRefCon) {
                    OSStatus sysex_status = -1;
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
                status = MIDIPortConnectSource(in_port, source, NULL);
                if (status != noErr) {
                    fprintf(stderr, "Error %d while connecting MIDI input port to source\n", status);
                }
            }
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

#endif
