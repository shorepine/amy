// teensy_support.cpp
#ifdef __IMXRT1062__

#include "teensy_support.h"
#include <Audio.h>
#include "amy.h"

AudioOutputPT8211           i2s1; 
//AudioOutputI2S unused;
AudioPlayQueue           queue_l;
AudioPlayQueue           queue_r;
AudioOutputUSB           usb_out;    
AudioConnection          patchCord1(queue_r, 0, usb_out, 1);
AudioConnection          patchCord2(queue_l, 0, usb_out, 0);
AudioConnection          patchCord3(queue_r, 0, i2s1, 1);
AudioConnection          patchCord4(queue_l, 0, i2s1, 0);




int16_t * samples_l;
int16_t * samples_r;

extern "C" {

	#include "usb_names.h"

	// Edit these lines to create your own name.  The length must
	// match the number of characters in your custom name.

	#define MIDI_NAME   {'A','M','Y',' ','S','y','n','t','h','e','s','i','z','e','r'}
	#define MIDI_NAME_LEN  15
	#define SPSS_NAME   {'S','P','S','S'}
	#define SPSS_NAME_LEN 4

	// Do not change this part.  This exact format is required by USB.

	struct usb_string_descriptor_struct usb_string_product_name = {
	        2 + MIDI_NAME_LEN * 2,
	        3,
	        MIDI_NAME
	};
	struct usb_string_descriptor_struct usb_string_manufacturer_name = {
			2 + SPSS_NAME_LEN * 2,
			3,
			SPSS_NAME
	};
	void teensy_setup_i2s() {
		samples_l = (int16_t*)malloc(sizeof(int16_t)*AMY_BLOCK_SIZE);
		samples_r = (int16_t*)malloc(sizeof(int16_t)*AMY_BLOCK_SIZE);
		AudioMemory(16);
		queue_l.setMaxBuffers(4);
		queue_r.setMaxBuffers(4);

	}

	void teensy_i2s_send(int16_t * samples) {
	    for(int16_t i=0;i<AMY_BLOCK_SIZE;i++) {
    		samples_l[i] = samples[i*2];
			samples_r[i] = samples[i*2+1];
		 }  
		queue_l.play(samples_l, AMY_BLOCK_SIZE);
		queue_r.play(samples_r, AMY_BLOCK_SIZE);
	}

	void teensy_start_midi() {
		Serial8.begin(31250);
	}
	int16_t teensy_get_serial_byte() {
		if(Serial8.available()) {
			return Serial8.read();
		}
		return -1;
	}
}

#endif