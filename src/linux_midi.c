#include <stdbool.h>
#include <pthread.h>
#include <alsa/asoundlib.h>
#include "amy_midi.h"

#define MIDI_SHORT_SLEEP	500L
// 1000L is slightly longer than 3-byte messages
//  250L is somewhat shorter than 1-byte message
//  500L is a reasonable compromise ???

snd_rawmidi_t *midi_rx_port = NULL;
snd_rawmidi_t *midi_tx_port = NULL;
pthread_t midi_thread;

//static void NotifyProc(const MIDINotification *message, void *refCon) {}

//                           ____system common_____   ___system realtime____
uint8_t _sys_msg_len[16] = { 1, 2, 3, 2, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1 };
//                           __data (or run stat)__   ___channel msgs____ sys
uint8_t _chn_msg_len[16] = { 0, 0, 0, 0, 0, 0, 0, 0,  3, 3, 3, 3, 2, 2, 3, 0 };

static uint8_t midi_status_len(uint8_t status)
{
    if      (status >= 0xF0) return _sys_msg_len[status & 15];
    else if (status >= 0x80) return _chn_msg_len[status >> 4];
    else                     return 0;
}

void midi_out(uint8_t *bytes, uint16_t len)
{
    snd_rawmidi_write(midi_tx_port, bytes, len);
}

int midi_linux_should_exit = false;

void* run_midi_linux(void*argp)
{
    uint8_t data[MAX_MIDI_BYTES_TO_PARSE];
    //sysex_buffer = malloc(MAX_SYSEX_BYTES);
    int err = snd_rawmidi_open(&midi_rx_port, &midi_tx_port, "virtual", 0);
    if (err < 0) {
	fprintf(stderr, "Could not create virtual raw port: %s\n", snd_strerror(err));
	exit(1);
    }
    while (!midi_linux_should_exit) {
	int len = snd_rawmidi_read(midi_rx_port, data, MAX_MIDI_BYTES_TO_PARSE);
	if (len > 0) convert_midi_bytes_to_messages(data, len, 0);
#ifdef MIDI_SHORT_SLEEP
	else usleep(MIDI_SHORT_SLEEP);  // <- is this required at all?!
#endif
    }
    if (midi_rx_port) {
	snd_rawmidi_close(midi_rx_port);
	midi_rx_port = NULL;
    }
    if (midi_tx_port) {
	snd_rawmidi_close(midi_tx_port);
	midi_tx_port = NULL;
    }
    return NULL;
}

void run_midi()
{
    if (sysex_buffer == NULL) {  // has not been started yet.
	fprintf(stderr, "*** opening MIDI port ***\n");
	sysex_buffer = malloc(MAX_SYSEX_BYTES);
	pthread_create(&midi_thread, NULL, run_midi_linux, NULL);
    }
}

void stop_midi()
{
    if (sysex_buffer) {
	fprintf(stderr, "*** closing MIDI port ***\n");
	midi_linux_should_exit = true;
	usleep(MIDI_SHORT_SLEEP * 100);
	pthread_join(midi_thread, NULL);
	free(sysex_buffer);
	sysex_buffer = NULL;
    }
}
