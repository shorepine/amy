// transfer.c
// data transfer in AMY:
// loading files from disk (VFS, stdio, arduino)
// base64 transfer decoding

#include <stdlib.h>
#include <ctype.h>
#include "transfer.h"
#include "amy.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#if (defined TULIP) || (defined AMYBOARD)
#define AMY_FILE mp_obj_t
#elif defined ARDUINO
// unclear
#else
#define AMY_FILE FILE
#endif


AMY_FILE* get_file_pointer(char* filename) {
    return (AMY_FILE*)fopen(filename, "rb");
}

void close_file(AMY_FILE * file_pointer) {
    fclose(file_pointer);
}


// wav parsing - wav_reader_t typedef is now in transfer.h

static uint32_t read_u32_le(AMY_FILE *fp) {
    uint8_t b[4];
    fread(b, 1, 4, fp);
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint16_t read_u16_le(AMY_FILE *fp) {
    uint8_t b[2];
    fread(b, 1, 2, fp);
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

wav_reader_t *wav_open(AMY_FILE *fp) {
    if (!fp) return NULL;

    char id[5] = {0};

    // RIFF header
    fread(id, 1, 4, fp);
    if (strncmp(id, "RIFF", 4) != 0) return NULL;
    fseek(fp, 4, SEEK_CUR); // skip chunk size
    fread(id, 1, 4, fp);
    if (strncmp(id, "WAVE", 4) != 0) return NULL;

    wav_reader_t *wav = calloc(1, sizeof(*wav));
    wav->fp = fp;

    // Parse chunks
    while (!feof(fp)) {
        if (fread(id, 1, 4, fp) != 4) break;
        uint32_t chunk_size = read_u32_le(fp);

        if (strncmp(id, "fmt ", 4) == 0) {
            uint16_t audio_format = read_u16_le(fp);
            wav->num_channels = read_u16_le(fp);
            wav->sample_rate = read_u32_le(fp);
            fseek(fp, 6, SEEK_CUR); // byte rate (4), block align (2)
            wav->bits_per_sample = read_u16_le(fp);

            // Skip any extra bytes
            if (chunk_size > 16) fseek(fp, chunk_size - 16, SEEK_CUR);

            if (audio_format != 1 || wav->bits_per_sample != 16) {
                free(wav);
                return NULL; // only PCM 16-bit supported
            }

        } else if (strncmp(id, "data", 4) == 0) {
            wav->data_offset = ftell(fp);
            wav->data_size = chunk_size;
            wav->num_samples = wav->data_size / (wav->num_channels * (wav->bits_per_sample / 8));
            fseek(fp, chunk_size, SEEK_CUR);
        } else {
            // skip unhandled chunk
            fseek(fp, chunk_size, SEEK_CUR);
        }
    }

    if (wav->data_offset == 0) {
        free(wav);
        return NULL; // no data chunk found
    }

    return wav;
}

size_t wav_read(wav_reader_t *wav, uint32_t start_frame, uint32_t num_frames, int16_t *samples) {
    if (!wav || !samples) return 0;

    uint32_t bytes_per_frame = wav->num_channels * (wav->bits_per_sample / 8);
    uint32_t start_byte = wav->data_offset + (start_frame * bytes_per_frame);

    if (start_frame >= wav->num_samples) return 0;

    if (start_frame + num_frames > wav->num_samples)
        num_frames = wav->num_samples - start_frame;

    fseek(wav->fp, start_byte, SEEK_SET);
    size_t read = fread(samples, bytes_per_frame, num_frames, wav->fp);
    return read;
}

void wav_close(wav_reader_t *wav) {
    if (wav) free(wav);
}


/// b64 transfer 

// Base64 encoding table
const char b64_table[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// We keep one decbuf around and reuse it during transfer
b64_buffer_t decbuf;

// signals to AMY that i'm now receiving a transfer of length (bytes!) into allocated storage
void start_receiving_transfer(uint32_t length, uint8_t * storage) {
    amy_global.transfer_flag = 1;
    amy_global.transfer_storage = storage;
    amy_global.transfer_length = length;
    amy_global.transfer_stored = 0;
    b64_buf_malloc(&decbuf);
}

// takes a wire message and adds it to storage after decoding it. stops transfer when it's done
void parse_transfer_message(char * message, uint16_t len) {
    size_t decoded = 0;
    uint8_t * block = b64_decode_ex (message, len, &decbuf, &decoded);
    for(uint16_t i=0;i<decoded;i++) {
        amy_global.transfer_storage[amy_global.transfer_stored++] = block[i];
    }
    if(amy_global.transfer_stored>=amy_global.transfer_length) { // we're done
        amy_global.transfer_flag = 0;
        free(decbuf.ptr);
    }
}

int b64_buf_malloc(b64_buffer_t * buf)
{
    buf->ptr = malloc(B64_BUFFER_SIZE);
    if(!buf->ptr) return -1;

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