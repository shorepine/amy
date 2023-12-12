// AMY-Arduino.h
// connector for Arduino

#ifndef AMYARDUINOH
#define AMYARDUINOH

#include "Arduino.h"
#include "amy.h"

class AMY
{
  public:
    AMY();
    void begin();
    void send_message(char * message);
    uint16_t * render_block();
  private:
    int _pin;
};

#endif
