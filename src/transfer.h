#ifndef TRANSFER_H
#define TRANSFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#if (defined TULIP) || (defined AMYBOARD)
#define AMY_FILE mp_obj_t
#elif defined ARDUINO
// unclear
#else
#define AMY_FILE FILE
#endif

// File operations
AMY_FILE* get_file_pointer(char* filename);
void close_file(AMY_FILE * file_pointer);

// WAV parsing structures and functions
typedef struct {
    AMY_FILE *fp;
    uint32_t num_samples;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;
} wav_reader_t;

wav_reader_t *wav_open(AMY_FILE *fp);
size_t wav_read(wav_reader_t *wav, uint32_t start_frame, uint32_t num_frames, int16_t *samples);
void wav_close(wav_reader_t *wav);

// Base64 transfer structures and functions
typedef struct {
    char *ptr;
    uint8_t bufc;
} b64_buffer_t;

#define B64_BUFFER_SIZE 1024

extern const char b64_table[64];

void start_receiving_transfer(uint32_t length, uint8_t * storage);
void parse_transfer_message(char * message, uint16_t len);
int b64_buf_malloc(b64_buffer_t * buf);
char * b64_encode (const unsigned char *src, b64_buffer_t * encbuf, size_t len);
unsigned char * b64_decode_ex (const char *src, size_t len, b64_buffer_t * decbuf, size_t *decsize);

#endif // TRANSFER_H