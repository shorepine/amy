#ifdef __IMXRT1062__

#include "Teensy4i2s.h"

AudioOutputI2S audioOutputI2S;
AudioInputI2S audioInputI2S;
extern "C" {
	void teensy_setup_i2s() {
		audioOutputI2S.begin();

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