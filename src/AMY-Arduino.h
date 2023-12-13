// AMY-Arduino.h
// connector for Arduino

#ifndef AMYARDUINOH
#define AMYARDUINOH

#ifdef ARDUINO_ARCH_RP2040
#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico-audio/audio_i2s.h"
#include "pico/binary_info.h"
#endif

#include "Arduino.h"
extern "C" {

  #include "amy.h"
  #include "examples.h"
}


class AMY
{
  public:
    AMY();
    void begin();
    int64_t sysclock();
    void fm(int64_t start);
    void render(uint8_t core);
    void send_message(char * message);
    uint16_t * render_block();
  private:
    int _pin;
    #ifdef ARDUINO_ARCH_RP2040
    struct audio_buffer_pool *ap;
    #endif
};

#endif




