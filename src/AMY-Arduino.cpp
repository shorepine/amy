// AMY-Arduino.cpp
// connector for Arduino

#include "Arduino.h"
#include "AMY-Arduino.h"

AMY::AMY()
{

}

struct event AMY::default_event() {
    return amy_default_event();
}

void AMY::add_event(struct event e) {
    amy_add_event(e);
}

int32_t AMY::sysclock() {
    return amy_sysclock();
}

void AMY::begin() {
    amy_start();
}

// Unclear if we want to support multicore rendering in Arduino yet. The board supports do their own thing with cores. TBD
void AMY::render(uint8_t core) {
#if AMY_CORES == 2
    if(core == 0) {
        render_task(AMY_OSCS, AMY_OSCS/2, core);
    } else {
        render_task(AMY_OSCS/2, AMY_OSCS, core);        
    }
#else
    render_task(0,AMY_OSCS,0);
#endif
}

void AMY::reset() {
    amy_reset_oscs();
}

int16_t * AMY::get_buffer() {
    // This renders all oscs, too, if AMY_CORES = 1
    return fill_audio_buffer_task();
}

void AMY::fm(int32_t start) {
    example_multimbral_fm(start);
}

void AMY::drums(int32_t start, uint16_t loops) {
    example_drums(start, loops);
}

void AMY::send_message(char * message) {
    amy_add_i_event(amy_parse_message(message));
}






