// pico_support.cpp
#if (defined ARDUINO_ARCH_RP2040) || (defined ARDUINO_ARCH_RP2350)
//#include <Arduino.h>
//#include <Adafruit_TinyUSB.h>
//#include <MIDI.h>
#ifdef USE_TUSB
#include <Adafruit_TinyUSB.h>
#endif



extern "C" {
	void check_tusb_midi();
	
	void pico_setup_midi() {
#ifdef USE_TUSB
            if (!TinyUSBDevice.isInitialized()) {
			TinyUSBDevice.begin(0);
		}
		//usb_midi.setStringDescriptor("AMY Synthesizer");

#endif
	}

	void pico_process_midi() {

#ifdef USE_TUSB
#ifdef TINYUSB_NEED_POLLING_TASK
			// Manual call tud_task since it isn't called by Core's background
			TinyUSBDevice.task();
		#endif
		check_tusb_midi();
#endif
	}
}
#endif
