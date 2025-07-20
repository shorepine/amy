// midi.c
// i deal with parsing and receiving midi on many platforms

#include "amy.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#if (defined ARDUINO_ARCH_RP2040) || (defined ARDUINO_ARCH_RP2350)
//#define TUD_USB_GADGET
#include "tusb.h"
#include "class/midi/midi.h"
#include "class/midi/midi_device.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#endif


#include "amy_midi.h"
uint8_t current_midi_message[3] = {0,0,0};
uint8_t midi_message_slot = 0;
uint8_t sysex_flag = 0;

// Send a MIDI note on OUT
void amy_send_midi_note_on(uint16_t osc) {
    // don't forward on a note coming in through MIDI IN 
    if(synth[osc]->note_source != NOTE_SOURCE_MIDI) {
        uint8_t bytes[3];
        bytes[0] = 0x90;
        bytes[1] = (uint8_t)roundf(synth[osc]->midi_note);
        bytes[2] = (uint8_t)roundf(synth[osc]->velocity*127.0f);
        midi_out(bytes, 3);
    }
}

// Send a MIDI note off OUT
void amy_send_midi_note_off(uint16_t osc) {
    // don't forward on a note coming in through MIDI IN 
    if(synth[osc]->note_source != NOTE_SOURCE_MIDI) {
        uint8_t bytes[3];
        bytes[0] = 0x80;
        bytes[1] = (uint8_t)roundf(synth[osc]->midi_note);
        bytes[2] = (uint8_t)roundf(synth[osc]->velocity*127.0f);
        midi_out(bytes, 3);
    }
}

// Given a MIDI note on IN, create a AMY message on that instrument and play it
void amy_received_note_on(uint8_t channel, uint8_t note, uint8_t vel, uint32_t time) {
    if (!instrument_grab_midi_notes(channel)) return;
    amy_event e = amy_default_event();
    e.time = time;
    e.synth = channel;
    e.note_source = NOTE_SOURCE_MIDI;
    e.midi_note = note;
    e.velocity = ((float)vel/127.0f);
    amy_add_event(&e);
}

// Given a MIDI note off IN, create a AMY message on that instrument and play it
void amy_received_note_off(uint8_t channel, uint8_t note, uint8_t vel, uint32_t time) {
    if (!instrument_grab_midi_notes(channel)) return;
    amy_event e = amy_default_event();
    e.time = time;
    e.synth = channel;
    e.note_source = NOTE_SOURCE_MIDI;
    e.midi_note = note;
    e.velocity = 0;
    amy_add_event(&e);
}

void amy_received_control_change(uint8_t channel, uint8_t control, uint8_t value, uint32_t time) {
    if (control == 0) {
        // Bank select coarse.
        instrument_set_bank_number(channel, value);
    } else if (control == 7) {
        // Use CC 7 for global volume control (on any channel).
        //amy_event e = amy_default_event();
        //e.volume = (float)value/12.7;  // Max volume is 10.
        //e.note_source = NOTE_SOURCE_MIDI;
        //amy_add_event(&e);
        amy_global.volume = (float)value/12.7;  // Max volume is 10.
    }
}

void amy_received_program_change(uint8_t channel, uint8_t program, uint32_t time) {
    amy_event e = amy_default_event();
    e.time = time;
    e.synth = channel;
    e.note_source = NOTE_SOURCE_MIDI;
    // MIDI patches are in blocks of 128, potentially set by an earlier CC0.
    int bank_number = instrument_bank_number(channel);
    if (bank_number < 0) {
        // If the bank hasn't been set, stay within the block of 128 of the current patch
        // (so e.g. DX7 voices remain DX7).
        bank_number = (instrument_get_patch_number(e.synth) & 0xFF80) >> 7;
    }
    e.patch_number = program + 128 * bank_number;
    if (channel != AMY_MIDI_CHANNEL_DRUMS) {  // What would that even mean?
        amy_add_event(&e);
    }
}

void amy_received_pedal(uint8_t channel, uint8_t value, uint32_t time) {
    amy_event e = amy_default_event();
    e.time = time;
    e.synth = channel;
    e.note_source = NOTE_SOURCE_MIDI;
    e.pedal = value;
    amy_add_event(&e);
}

void amy_received_all_notes_off(uint8_t channel, uint32_t time) {
    amy_event e = amy_default_event();
    e.time = time;
    e.synth = channel;
    e.note_source = NOTE_SOURCE_MIDI;
    // All notes off is indicated by vel = 0 and note = 0
    e.velocity = 0;
    e.midi_note = 0;
    amy_add_event(&e);
}

void amy_received_pitch_bend(uint8_t channel, uint8_t low_byte, uint8_t high_byte, uint32_t time) {
    amy_event e = amy_default_event();
    e.time = time;
    // Currently, pitch bend is global and not applied per-channel, but preserve the info.
    e.synth = channel;
    e.note_source = NOTE_SOURCE_MIDI;
    // The integer range is -8192 to 8191, the float range is -1/6th to +1/6th,
    // units are octaves, so +/- 2 semitones.
    e.pitch_bend = ((float)(((int)((high_byte << 7) | low_byte)) - 8192)) / (6.0f * 8192.0f);
    amy_add_event(&e);
}

// I'm called when we get a fully formed MIDI message from any interface -- usb, gadget, uart, mac, and either sysex or normal
void amy_event_midi_message_received(uint8_t * data, uint32_t len, uint8_t sysex, uint32_t time) {
    if(!sysex) {
        uint8_t status = data[0] & 0xF0;
        uint8_t channel = data[0] & 0x0F;
        // Do the AMY instrument things here
        if(status == 0x80) amy_received_note_off(channel+1, data[1], data[2], time);
        else if(status == 0x90) amy_received_note_on(channel+1, data[1], data[2], time);
        else if(status == 0xB0 && data[1] == 0x40) amy_received_pedal(channel+1, data[2], time);
        else if(status == 0xB0 && data[1] == 0x7B) amy_received_all_notes_off(channel+1, time);
        else if(status == 0XB0) amy_received_control_change(channel+1, data[1], data[2], time);
        else if(status == 0xC0) amy_received_program_change(channel+1, data[1], time);
        else if(status == 0xE0) amy_received_pitch_bend(channel+1, data[1], data[2], time);
    }

    // Also send the external hooks if set
    if(amy_external_midi_input_hook != NULL) {
        amy_external_midi_input_hook(data, len, sysex);
    }

    // Update web MIDI out hook if set
    #ifdef __EMSCRIPTEN__
    EM_ASM(
        if(typeof amy_external_midi_input_js_hook === 'function') {
            amy_external_midi_input_js_hook(HEAPU8.subarray($0, $0+$1), $1, $2);
        }, data, len, sysex);
    #endif
}


void midi_clock_received() {
    // one day update the AMY sequencer
}


/*
    3 0x8X - note off    |   note number    |  velocity 
    3 0x9X - note on     |   note number    |  velocity
    3 0xAX - Paftertouch |   note number    |  pressure
    3 0xBX - CC          |   controller #   |  value 
    2 0xCX - PC          |   program        |  XXXX
    2 0xDX - Caftertouch |   pressure       |  XXXX
    3 0xEX - pitch bend  |    LSB.          |  MSB
    X 0xF0  - sysex start|  ... wait until F7
    2 0xF1  - time code  | data
    3 0xF2 song pos      | lsb              | msb
    2 0xF3 song sel      | data
    1 0xF4 reserved      | XXXX
    1 0xF5 reserved.     | XXXX
    1 0xF6 tune request  | XXXX
    X 0xF7 end of sysex  | XXXX
    1 0xF8 timing clock  | XXXX
    1 0xF9 reserved.     | XXXX
    1 0xFA start         | XXXX
    1 0xFB continue      | XXXX
    1 0xFC stop          | XXXX
    1 0xFD reserved      | XXXX
    1 0xFE sensing       | XXXX
    1 0xFF reset         | XXXX
*/

uint16_t sysex_len = 0;


uint8_t * sysex_buffer;
void parse_sysex() {
    uint32_t time = AMY_UNSET_VALUE(time);
    if(sysex_len>3) {
        // let's use 0x00 0x03 0x45 for SPSS
        if(sysex_buffer[0] == 0x00 && sysex_buffer[1] == 0x03 && sysex_buffer[2] == 0x45) {
            sysex_buffer[sysex_len] = 0;
            amy_add_message((char*)(sysex_buffer+3));
            sysex_len = 0; // handled
        } else {
    	   amy_event_midi_message_received(sysex_buffer, sysex_len, 1, time);
        }
    }
}

void convert_midi_bytes_to_messages(uint8_t * data, size_t len, uint8_t usb) {
    // i take any amount of bytes and add messages 
    // remember this can start in the middle of a midi message, so act accordingly
    // running status is handled by keeping the status byte around after getting a message.
    // remember that USB midi always comes in groups of 3 here, even if it's just a one byte message
    // so we have USB (and mac IAC) set a usb flag so we know to end the loop once a message is parsed
    uint32_t time = AMY_UNSET_VALUE(time);
    for(size_t i=0;i<len;i++) {

        uint8_t byte = data[i];

        // Skip sysex in this parser until we get an F7. We do not pass sysex over to python (yet)
        if(sysex_flag) {
            if(byte == 0xF7) {
                sysex_flag = 0;
                parse_sysex();
            } else {
                sysex_buffer[sysex_len++] = byte;
            }
        } else {
            if(byte & 0x80) { // new status byte 
                sysex_flag = 0; sysex_len = 0;
                // Single byte message?
                current_midi_message[0] = byte;
                if(byte == 0xF4 || byte == 0xF5 || byte == 0xF6 || byte == 0xF9 || 
                    byte == 0xFA || byte == 0xFB || byte == 0xFC || byte == 0xFD || byte == 0xFE || byte == 0xFF) {
		    amy_event_midi_message_received(current_midi_message, 1, 0, time);
                    if(usb) i = len+1; // exit the loop if usb
                }  else if(byte == 0xF0) { // sysex start 
                    // if that's there we then assume everything is an AMY message until 0xF7
                    sysex_flag = 1;
                } else if(byte == 0xF8) { // clock. don't forward this on to Tulip userspace
                    midi_clock_received();
                    if(usb) i = len+1; // exit the loop if usb
                } else { // a new status message that expects at least one byte of message after
                    current_midi_message[0] = byte;
                }
            } else { // data byte of some kind
                uint8_t status = current_midi_message[0] & 0xF0;

                // a 2 bytes of data message
                if(status == 0x80 || status == 0x90 || status == 0xA0 || status == 0xB0 || status == 0xE0 || current_midi_message[0] == 0xF2) {
                    if(midi_message_slot == 0) {
                        current_midi_message[1] = byte;
                        midi_message_slot = 1;
                    } else {
                        current_midi_message[2] = byte;
                        midi_message_slot = 0;
                        amy_event_midi_message_received(current_midi_message, 3, 0, time);
                    }
                // a 1 byte data message
                } else if (status == 0xC0 || status == 0xD0 || current_midi_message[0] == 0xF3 || current_midi_message[0] == 0xF1) {
                    current_midi_message[1] = byte;
                    amy_event_midi_message_received(current_midi_message, 2, 0, time);
                    if(usb) i = len+1; // exit the loop if usb
                }
            }
        }
    }
    
}

// This is used for web emscripten hooks + external linkers of AMY
// set from_web_or_usb to 1 if this is a 4 packet type interface -- WebMIDI or USB MIDI gadget/host, 0 otherwise
void amy_process_single_midi_byte(uint8_t byte, uint8_t from_web_or_usb) {
    uint8_t data[1];
    data[0] = byte;
    convert_midi_bytes_to_messages(data, 1, from_web_or_usb); 
}

// for external programs to send MIDI data OUT using AMY
void amy_external_midi_output(uint8_t * data, uint32_t len) {
    midi_out(data, len);
}


///// Per platform MIDI in and out stuff
///////////////////////////////////////////////


#if (defined __EMSCRIPTEN__)
void run_midi() {
    // do nothing, this is all done with callbacks
}

void midi_out(uint8_t * bytes, uint16_t len) {
    EM_ASM(
            if(midiOutputDevice != null) {
                midiOutputDevice.send(HEAPU8.subarray($0, $0 + $1));
            }, bytes, len
        );
}
#endif

#if !defined(MACOS) && !defined(__EMSCRIPTEN__) // this code is for NOT macos desktop , which is in macos_midi.m

// "run_midi" sets up MIDI on MCU platforms


#if (defined ESP_PLATFORM)
TaskHandle_t midi_handle;

int8_t esp_get_uart(int8_t index) {
    if(index==0) return UART_NUM_0;
    if(index==1) return UART_NUM_1;
    if(index==2) return UART_NUM_2;
    return -1;
}

void run_midi_task() {
    const int uart_num = esp_get_uart(amy_global.config.midi_uart);
    uart_config_t uart_config = {
        .baud_rate = 31250,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
    };

    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, amy_global.config.midi_out, amy_global.config.midi_in, UART_PIN_NO_CHANGE , UART_PIN_NO_CHANGE ));

    const int uart_buffer_size = (MAX_MIDI_BYTES_TO_PARSE);
    // Install UART driver using an event queue here
    ESP_ERROR_CHECK(uart_driver_install(uart_num, uart_buffer_size,  0, 0, NULL, 0));

    uart_intr_config_t uart_intr = {
          .intr_enable_mask = UART_RXFIFO_FULL_INT_ENA_M
                              | UART_RXFIFO_TOUT_INT_ENA_M
                              | UART_FRM_ERR_INT_ENA_M
                              | UART_RXFIFO_OVF_INT_ENA_M
                              | UART_BRK_DET_INT_ENA_M
                              | UART_PARITY_ERR_INT_ENA_M,
          .rxfifo_full_thresh = 1,
          .rx_timeout_thresh = 0,
          .txfifo_empty_intr_thresh = 10
    };

    uart_intr_config(uart_num, &uart_intr);

    uint8_t data[MAX_MIDI_BYTES_TO_PARSE];
    size_t length = 0;
    while(1) {
        length = uart_read_bytes(uart_num, data, MAX_MIDI_BYTES_TO_PARSE /*MAX_MIDI_BYTES_PER_MESSAGE*MIDI_QUEUE_DEPTH*/, 1/portTICK_PERIOD_MS);
        if(length > 0) {
            convert_midi_bytes_to_messages(data,length,0);
        }
    } // end loop forever
}

void run_midi() {
    sysex_buffer = malloc_caps(MAX_SYSEX_BYTES, amy_global.config.ram_caps_sysex);
    if(amy_global.config.midi & AMY_MIDI_IS_UART) {
        xTaskCreatePinnedToCore(run_midi_task, MIDI_TASK_NAME, (MIDI_TASK_STACK_SIZE) / sizeof(StackType_t), NULL, MIDI_TASK_PRIORITY, &midi_handle, MIDI_TASK_COREID);
    }
}

#endif

#if (defined ARDUINO_ARCH_RP2040) || (defined ARDUINO_ARCH_RP2350)

uart_inst_t * rp_get_uart(int8_t index) {
    if(index==0) return uart0;
    if(index==1) return uart1;
    return NULL;
}

// RX interrupt handler
void on_pico_uart_rx() {
    const int midi_buffer_size = 16;
    uint8_t bytes[midi_buffer_size];
    uint8_t i = 0;
    while (uart_is_readable(rp_get_uart(amy_global.config.midi_uart)) && i < midi_buffer_size) {
        uart_read_blocking (rp_get_uart(amy_global.config.midi_uart), bytes + i, 1);
        i++;
    }
    //if (i >= midi_buffer_size)
    //    fprintf(stderr, "midi_buffer_size %d of %d\n", i, midi_buffer_size);
    convert_midi_bytes_to_messages(bytes,i,0);
}

extern void pico_setup_midi();

void run_midi() {
    sysex_buffer = malloc_caps(MAX_SYSEX_BYTES, amy_global.config.ram_caps_sysex);
    if(amy_global.config.midi & AMY_MIDI_IS_UART) {
        uart_init(rp_get_uart(amy_global.config.midi_uart), 31250);
        gpio_set_function(amy_global.config.midi_out, UART_FUNCSEL_NUM(rp_get_uart(amy_global.config.midi_uart), amy_global.config.midi_out));
        gpio_set_function(amy_global.config.midi_in, UART_FUNCSEL_NUM(rp_get_uart(amy_global.config.midi_uart), amy_global.config.midi_in));
        uart_set_hw_flow(rp_get_uart(amy_global.config.midi_uart), false, false);
        uart_set_format(rp_get_uart(amy_global.config.midi_uart), 8, 1, UART_PARITY_NONE);
        uart_set_fifo_enabled(rp_get_uart(amy_global.config.midi_uart), true);
    } else if(amy_global.config.midi & AMY_MIDI_IS_USB_GADGET) {
        pico_setup_midi();
    }

}

void check_tusb_midi() {
    while ( tud_midi_available() ) {
        uint8_t packet[4];
        tud_midi_packet_read(packet);
        convert_midi_bytes_to_messages(packet+1, 3, 1);
    }
}

#endif

#ifdef __IMXRT1062__
extern void teensy_start_midi();

void run_midi() {
    if(amy_global.config.midi & AMY_MIDI_IS_UART) teensy_start_midi();
}
#endif


#ifdef __linux__
void run_midi() {
    //fprintf(stderr, "no MIDI support on linux yet\n");
}
#endif

#ifdef AMY_DAISY
// Daisy seed
void run_midi() {
    // MIDI handled in main.
}
#endif

void midi_out(uint8_t * bytes, uint16_t len) {
#if defined TUD_USB_GADGET
    if(amy_global.config.midi & AMY_MIDI_IS_USB_GADGET) tud_midi_stream_write(0, bytes, len);
#elif defined ESP_PLATFORM
    if(amy_global.config.midi & AMY_MIDI_IS_UART) uart_write_bytes(esp_get_uart(amy_global.config.midi_uart), bytes, len);
#elif (defined ARDUINO_ARCH_RP2040) || (defined ARDUINO_ARCH_RP2350)
    if(amy_global.config.midi & AMY_MIDI_IS_UART) uart_write_blocking(rp_get_uart(amy_global.config.midi_uart), bytes, len);
#else
    // teensy
    // linux
#endif

}

#endif // check for macos desktop 
