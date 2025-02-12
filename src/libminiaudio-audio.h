// libminiaudio-audio.h

#ifndef __LIBMINIAUDIO_AUDIO_H
#define __LIBMINIAUDIO_AUDIO_H
#include "amy.h"
#include <pthread.h>

extern int16_t amy_channel;
extern int16_t amy_playback_device_id;
extern int16_t amy_capture_device_id;
extern uint8_t amy_running;

void amy_print_devices();
void amy_live_start(uint8_t audio_in);
void amy_live_stop();
#endif