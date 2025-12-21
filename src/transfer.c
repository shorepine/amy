// transfer.c
// data transfer over AMY messages

#include <stdlib.h>
#include <ctype.h>
#include "transfer.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>



// per platform file reading / writing
// posix (linux, mac)
// micropython / littlefs - tulip & amybard
// (later) arduino / SPIFFS / SD
// if yours isnt' here, you just write your own amy_external_fopen_hook etc


// mac / linux / generic posix 

#if defined(__unix__) || defined(__APPLE__) || defined(_POSIX_VERSION)

// Map from FILE * to a uint32_t handle to pass to AMY

static FILE *g_files[MAX_OPEN_FILES]; // index 1..MAX_OPEN_FILES-1 used

static uint32_t alloc_handle(FILE *f) {
    for (uint32_t i = 1; i < MAX_OPEN_FILES; i++) {
        if (g_files[i] == NULL) {
            g_files[i] = f;
            return i;
        }
    }
    return HANDLE_INVALID; // table full
}

static FILE *lookup_handle(uint32_t h) {
    if (h == 0 || h >= MAX_OPEN_FILES) return NULL;
    return g_files[h];
}

static void free_handle(uint32_t h) {
    if (h == 0 || h >= MAX_OPEN_FILES) return;
    g_files[h] = NULL;
}


// These should map to 
// uint32_t (*amy_external_fopen_hook)(char * filename, char * mode) = NULL;
// uint32_t (*amy_external_fwrite_hook)(uint32_t fptr, uint8_t * bytes, uint32_t len) = NULL;
// uint32_t (*amy_external_fread_hook)(uint32_t fptr, uint8_t * bytes, uint32_t len) = NULL;

// void (*amy_external_fclose_hook)(uint32_t fptr) = NULL;

uint32_t external_fopen_hook(char * filename, char *mode) {
    FILE *f = fopen(filename, mode);
    if (!f) {
        return HANDLE_INVALID;
    }
    uint32_t h = alloc_handle(f);
    if (h == HANDLE_INVALID) {
        fclose(f);
        return HANDLE_INVALID;
    }
    return h;
}

uint32_t external_fread_hook(uint32_t h, uint8_t *buf, uint32_t len) {
    FILE *f = lookup_handle(h);
    if (!f) {
        return 0;
    }
    uint32_t r = fread(buf, 1, len, f);
    return r;
}

uint32_t external_fwrite_hook(uint32_t h, uint8_t *buf, uint32_t n) {
    FILE *f = lookup_handle(h);
    if (!f) {
        return 0;
    }
    uint32_t w = fwrite(buf, 1, n, f);
    return w;
}

void external_fclose_hook(uint32_t h) {
    FILE *f = lookup_handle(h);
    if (f) {
        fclose(f);
        free_handle(h);
    }
}

#endif



// We keep one decbuf around and reuse it during transfer
b64_buffer_t decbuf;

// signals to AMY that i'm now receiving a transfer of length (bytes!) into allocated storage
void start_receiving_transfer(uint32_t length, uint8_t * storage) {
    amy_global.transfer_flag = AMY_TRANSFER_TYPE_AUDIO;
    amy_global.transfer_storage = storage;
    amy_global.transfer_length = length;
    amy_global.transfer_stored = 0;
    amy_global.transfer_file_handle = 0;
    amy_global.transfer_reboot = 0;
    amy_global.transfer_filename[0] = '\0';
    b64_buf_malloc(&decbuf);
}

void start_receiving_sample(uint32_t frames, uint8_t bus, int16_t *storage) {
    amy_global.transfer_flag = AMY_TRANSFER_TYPE_SAMPLE;
    amy_global.transfer_storage = (uint8_t *)storage;
    amy_global.transfer_length = frames;
    amy_global.transfer_stored = 0;
    amy_global.transfer_file_handle = bus; // use file handle to store bus number
    amy_global.transfer_reboot = 0;
    amy_global.transfer_filename[0] = '\0';
    fprintf(stderr, "start receiving sample to bus %d length %d\n", bus, frames);
}

void stop_receiving_sample() {
    amy_global.transfer_file_handle = 0;
    amy_global.transfer_flag = AMY_TRANSFER_TYPE_NONE;
    amy_global.transfer_stored = 0;
    amy_global.transfer_length = 0;
}

// signals to AMY that i'm now receiving a file transfer of length (bytes!) to filename
void start_receiving_file_transfer(uint32_t length, const char *filename, uint32_t reboot) {
    if (filename == NULL || filename[0] == '\0') {
        return;
    }
    if (amy_external_fopen_hook == NULL || amy_external_fwrite_hook == NULL || amy_external_fclose_hook == NULL) {
        fprintf(stderr, "file transfer hooks not enabled on platform\n");
        return;
    }
    uint32_t handle = amy_external_fopen_hook((char *)filename, "wb");
    if (handle == HANDLE_INVALID) {
        fprintf(stderr, "could not open file for transfer: %s\n", filename);
        return;
    }
    amy_global.transfer_flag = AMY_TRANSFER_TYPE_FILE;
    amy_global.transfer_storage = NULL;
    amy_global.transfer_length = length;
    amy_global.transfer_stored = 0;
    amy_global.transfer_file_handle = handle;
    amy_global.transfer_reboot = reboot;
    strncpy(amy_global.transfer_filename, filename, sizeof(amy_global.transfer_filename) - 1);
    amy_global.transfer_filename[sizeof(amy_global.transfer_filename) - 1] = '\0';
    b64_buf_malloc(&decbuf);
}

// takes a wire message and adds it to storage after decoding it. stops transfer when it's done
void parse_transfer_message(char * message, uint16_t len) {
    size_t decoded = 0;
    uint8_t * block = b64_decode_ex (message, len, &decbuf, &decoded);
    if (amy_global.transfer_flag == AMY_TRANSFER_TYPE_FILE) {
        if (amy_external_fwrite_hook != NULL && amy_global.transfer_file_handle != HANDLE_INVALID) {
            uint32_t wrote = amy_external_fwrite_hook(amy_global.transfer_file_handle, block, (uint32_t)decoded);
            amy_global.transfer_stored += wrote;
        }
    } else {
        for (uint16_t i = 0; i < decoded; i++) {
            amy_global.transfer_storage[amy_global.transfer_stored++] = block[i];
        }
    }
    if (amy_global.transfer_stored >= amy_global.transfer_length) { // we're done
        if (amy_global.transfer_flag == AMY_TRANSFER_TYPE_FILE) {
            if (amy_external_fclose_hook != NULL && amy_global.transfer_file_handle != HANDLE_INVALID) {
                amy_external_fclose_hook(amy_global.transfer_file_handle);
            }
            if (amy_external_file_transfer_done_hook != NULL) {
                amy_external_file_transfer_done_hook(amy_global.transfer_filename,
                                                    amy_global.transfer_reboot);
            }
            amy_global.transfer_file_handle = 0;
            amy_global.transfer_reboot = 0;
            amy_global.transfer_filename[0] = '\0';
        }
        amy_global.transfer_flag = AMY_TRANSFER_TYPE_NONE;
        free(decbuf.ptr);
    }
}

int b64_buf_malloc(b64_buffer_t * buf)
{
    buf->ptr = malloc(B64_BUFFER_SIZE);
    if (!buf->ptr) return -1;

    buf->bufc = 1;

    return 0;
}

// We don't do encoding in AMY, we rely on python for that, but we'll leave it here if you want it in C
// Just like decode you pass it an allocated encbuf. 
char * b64_encode (const unsigned char *src, b64_buffer_t * encbuf, size_t len) {
    int i = 0;
    int j = 0;
    size_t size = 0;
    unsigned char buf[4];
    unsigned char tmp[3];


    // parse until end of source
    while (len--) {
        // read up to 3 bytes at a time into `tmp'
        tmp[i++] = *(src++);

        // if 3 bytes read then encode into `buf'
        if (3 == i) {
            buf[0] = (tmp[0] & 0xfc) >> 2;
            buf[1] = ((tmp[0] & 0x03) << 4) + ((tmp[1] & 0xf0) >> 4);
            buf[2] = ((tmp[1] & 0x0f) << 2) + ((tmp[2] & 0xc0) >> 6);
            buf[3] = tmp[2] & 0x3f;

            // allocate 4 new byts for `enc` and
            // then translate each encoded buffer
            // part by index from the base 64 index table
            // into `encbuf.ptr' unsigned char array

            for (i = 0; i < 4; ++i) {
                encbuf->ptr[size++] = b64_table[buf[i]];
            }

            // reset index
            i = 0;
        }
    }

    // remainder
    if (i > 0) {
        // fill `tmp' with `\0' at most 3 times
        for (j = i; j < 3; ++j) {
            tmp[j] = '\0';
        }

        // perform same codec as above
        buf[0] = (tmp[0] & 0xfc) >> 2;
        buf[1] = ((tmp[0] & 0x03) << 4) + ((tmp[1] & 0xf0) >> 4);
        buf[2] = ((tmp[1] & 0x0f) << 2) + ((tmp[2] & 0xc0) >> 6);
        buf[3] = tmp[2] & 0x3f;

        // perform same write to `encbuf->ptr` with new allocation
        for (j = 0; (j < i + 1); ++j) {
            encbuf->ptr[size++] = b64_table[buf[j]];
        }

        // while there is still a remainder
        // append `=' to `encbuf.ptr'
        while ((i++ < 3)) {
            encbuf->ptr[size++] = '=';
        }
    }

    // Make sure we have enough space to add '\0' character at end.
    encbuf->ptr[size] = '\0';

    return encbuf->ptr;
}



unsigned char *
b64_decode_ex (const char *src, size_t len, b64_buffer_t * decbuf, size_t *decsize) {
    int i = 0;
    int j = 0;
    int l = 0;
    size_t size = 0;
    unsigned char buf[3];
    unsigned char tmp[4];

    // alloc
    //if (b64_buf_malloc(&decbuf) == -1) { return NULL; }

    // parse until end of source
    while (len--) {
        // break if char is `=' or not base64 char
        if ('=' == src[j]) { break; }
        if (!(isalnum((unsigned char)src[j]) || '+' == src[j] || '/' == src[j])) { break; }

        // read up to 4 bytes at a time into `tmp'
        tmp[i++] = src[j++];

        // if 4 bytes read then decode into `buf'
        if (4 == i) {
            // translate values in `tmp' from table
            for (i = 0; i < 4; ++i) {
                // find translation char in `b64_table'
                for (l = 0; l < 64; ++l) {
                    if (tmp[i] == b64_table[l]) {
                        tmp[i] = l;
                        break;
                    }
                }
            }

            // decode
            buf[0] = (tmp[0] << 2) + ((tmp[1] & 0x30) >> 4);
            buf[1] = ((tmp[1] & 0xf) << 4) + ((tmp[2] & 0x3c) >> 2);
            buf[2] = ((tmp[2] & 0x3) << 6) + tmp[3];

            // write decoded buffer to `decbuf.ptr'
            //if (b64_buf_realloc(&decbuf, size + 3) == -1)  return NULL;
            for (i = 0; i < 3; ++i) {
                ((unsigned char*)decbuf->ptr)[size++] = buf[i];
            }

            // reset
            i = 0;
        }
    }

    // remainder
    if (i > 0) {
        // fill `tmp' with `\0' at most 4 times
        for (j = i; j < 4; ++j) {
            tmp[j] = '\0';
        }

        // translate remainder
        for (j = 0; j < 4; ++j) {
            // find translation char in `b64_table'
            for (l = 0; l < 64; ++l) {
                if (tmp[j] == b64_table[l]) {
                    tmp[j] = l;
                    break;
                }
            }
        }

        // decode remainder
        buf[0] = (tmp[0] << 2) + ((tmp[1] & 0x30) >> 4);
        buf[1] = ((tmp[1] & 0xf) << 4) + ((tmp[2] & 0x3c) >> 2);
        buf[2] = ((tmp[2] & 0x3) << 6) + tmp[3];

        // write remainer decoded buffer to `decbuf.ptr'
        //if (b64_buf_realloc(&decbuf, size + (i - 1)) == -1)  return NULL;
        for (j = 0; (j < i - 1); ++j) {
            ((unsigned char*)decbuf->ptr)[size++] = buf[j];
        }
    }

    // Make sure we have enough space to add '\0' character at end.
    //if (b64_buf_realloc(&decbuf, size + 1) == -1)  return NULL;
    ((unsigned char*)decbuf->ptr)[size] = '\0';

    // Return back the size of decoded string if demanded.
    if (decsize != NULL) {
        *decsize = size;
    }

    return (unsigned char*) decbuf->ptr;
}


void transfer_init() {
#if defined(_POSIX_VERSION) || defined(TULIP) || defined(AMYBOARD)
    amy_external_fopen_hook = external_fopen_hook;
    amy_external_fread_hook = external_fread_hook;
    amy_external_fwrite_hook = external_fwrite_hook;
    amy_external_fclose_hook = external_fclose_hook;
#endif
}

static uint16_t read_u16_le(const uint8_t *buf) {
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                  ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static int read_exact(uint32_t handle, uint8_t *buf, uint32_t len) {
    if (amy_external_fread_hook == NULL) {
        return 0;
    }
    uint32_t offset = 0;
    while (offset < len) {
        uint32_t got = amy_external_fread_hook(handle, buf + offset, len - offset);
        if (got == 0) {
            return 0;
        }
        offset += got;
    }
    return 1;
}

static int skip_bytes(uint32_t handle, uint32_t len) {
    uint8_t scratch[64];
    while (len > 0) {
        uint32_t chunk = len > sizeof(scratch) ? sizeof(scratch) : len;
        if (!read_exact(handle, scratch, chunk)) {
            return 0;
        }
        len -= chunk;
    }
    return 1;
}

int wave_parse_header(uint32_t handle, wave_info_t *info, uint32_t *data_bytes) {
    if (info == NULL || data_bytes == NULL) {
        return 0;
    }
    uint8_t riff_header[12];
    if (!read_exact(handle, riff_header, sizeof(riff_header))) {
        return 0;
    }
    if (memcmp(riff_header, "RIFF", 4) != 0 || memcmp(riff_header + 8, "WAVE", 4) != 0) {
        return 0;
    }
    uint8_t fmt_found = 0;
    wave_info_t tmp_info = {0};
    while (1) {
        uint8_t chunk_header[8];
        if (!read_exact(handle, chunk_header, sizeof(chunk_header))) {
            return 0;
        }
        uint32_t chunk_size = read_u32_le(chunk_header + 4);
        if (memcmp(chunk_header, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                return 0;
            }
            uint8_t fmt_chunk[16];
            if (!read_exact(handle, fmt_chunk, sizeof(fmt_chunk))) {
                return 0;
            }
            uint16_t audio_format = read_u16_le(fmt_chunk);
            tmp_info.channels = read_u16_le(fmt_chunk + 2);
            tmp_info.sample_rate = read_u32_le(fmt_chunk + 4);
            tmp_info.bits_per_sample = read_u16_le(fmt_chunk + 14);
            if (audio_format != 1 || tmp_info.channels == 0 || tmp_info.channels > 2 || tmp_info.bits_per_sample != 16) {
                return 0;
            }
            if (!skip_bytes(handle, chunk_size - sizeof(fmt_chunk))) {
                return 0;
            }
            if (chunk_size & 1) {
                if (!skip_bytes(handle, 1)) {
                    return 0;
                }
            }
            fmt_found = 1;
            continue;
        }
        if (memcmp(chunk_header, "data", 4) == 0) {
            if (!fmt_found) {
                return 0;
            }
            *info = tmp_info;
            *data_bytes = chunk_size;
            return 1;
        }
        if (!skip_bytes(handle, chunk_size)) {
            return 0;
        }
        if (chunk_size & 1) {
            if (!skip_bytes(handle, 1)) {
                return 0;
            }
        }
    }
    return 0;
}
uint32_t wave_read_pcm_frames_s16(uint32_t handle, uint16_t channels, uint16_t bits_per_sample,
        uint32_t *bytes_remaining, int16_t *dest, uint32_t max_frames) {
    if (dest == NULL || bytes_remaining == 0 || channels == 0 || channels > 2 || bits_per_sample != 16) {
        return 0;
    }
    uint32_t bytes_per_frame = channels * 2;
    if (*bytes_remaining < bytes_per_frame) {
        return 0;
    }
    uint32_t max_bytes = max_frames * bytes_per_frame;
    if (max_bytes > *bytes_remaining) {
        max_bytes = *bytes_remaining - (*bytes_remaining % bytes_per_frame);
    }
    uint32_t frames_to_read = max_bytes / bytes_per_frame;
    if (frames_to_read == 0) {
        return 0;
    }
    uint8_t raw_buf[(AMY_BLOCK_SIZE + 1) * 4];
    if (max_bytes > sizeof(raw_buf)) {
        max_bytes = sizeof(raw_buf) - (sizeof(raw_buf) % bytes_per_frame);
        frames_to_read = max_bytes / bytes_per_frame;
    }
    if (amy_external_fread_hook == NULL) {
        return 0;
    }
    uint32_t got = amy_external_fread_hook(handle, raw_buf, max_bytes);
    got -= got % bytes_per_frame;
    if (got == 0) {
        return 0;
    }
    frames_to_read = got / bytes_per_frame;
    *bytes_remaining -= got;
    if (channels == 1) {
        for (uint32_t i = 0; i < frames_to_read; i++) {
            dest[i] = (int16_t)read_u16_le(raw_buf + i * 2);
        }
    } else {
        for (uint32_t i = 0; i < frames_to_read; i++) {
            dest[i * 2] = (int16_t)read_u16_le(raw_buf + i * 4);
            dest[i * 2 + 1] = (int16_t)read_u16_le(raw_buf + i * 4 + 2);
        }
    }
    return frames_to_read;
}
