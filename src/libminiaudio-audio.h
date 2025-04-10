// libminiaudio-audio.h

#ifndef __LIBMINIAUDIO_AUDIO_H
#define __LIBMINIAUDIO_AUDIO_H
#include "amy.h"
#include <pthread.h>

void amy_print_devices();
void amy_live_start();
void amy_live_stop();
#endif