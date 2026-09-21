// AMY-Arduino.h
// connector for Arduino

#ifndef AMYARDUINOH
#define AMYARDUINOH

#include "Arduino.h"

extern "C" {
  #include "amy.h"
  #ifdef ESP_PLATFORM
    // ES8311 codec setup for boards that need it (Freenove FNK0104 and friends).
    // Declared here so sketches get the real prototype instead of hand-copying it.
    #include "es8311.h"
  #endif
}


#endif

