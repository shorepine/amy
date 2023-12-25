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


void AMY::reset() {
    amy_reset_oscs();
}



// (From either core) prepare to render multicore
void AMY::prepare() {
    amy_prepare_buffer();
}

void AMY::volume(float vol) {
    global.volume = vol;
}


// From each core, render
void AMY::render(uint16_t start, uint16_t end, uint8_t core) {
    amy_render(start, end, core);
}

// Get the rendered buffer back.
// If you're in single core, this also renders and prepares, so don't do that as well!
int16_t * AMY::get_buffer() {
    if(AMY_CORES > 1) {
        return amy_fill_buffer();
    }
    return amy_simple_fill_buffer();
}


void AMY::fm(int32_t start) {
    example_multimbral_fm(start, 0);
}

void AMY::drums(int32_t start, uint16_t loops) {
    example_drums(start, loops);
}

void AMY::send_message(char * message) {
    amy_add_event(amy_parse_message(message));
}






