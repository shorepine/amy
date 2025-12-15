// transfer.c
// data transfer over AMY messages

#include <stdlib.h>
#include <ctype.h>
#include "transfer.h"
#include <stdio.h>
#include <stdint.h>

// per platform file reading / writing
// posix (linux, mac)
// micropython / littlefs - tulip & amybard
// (later) arduino / SPIFFS / SD
// if yours isnt' here, you just write your own amy_external_fopen_hook etc


// mac / linux / generic posix 

#if defined(_POSIX_VERSION) 

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
  uint32_t r = fread(buf, 1, n, f);
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
  if (!f) {
    return -1;
  }
  fclose(f);
  free_handle(h);
}

#elif (defined TULIP) or (defined AMYBOARD) 

// Use the stuff from `tulip_helpers.c` here



#endif



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


void transfer_init() {
#if defined(_POSIX_VERSION) || defined(TULIP) || defined(AMYBOARD)
  amy_external_fopen_hook = external_fopen_hook;
  amy_external_fread_hook = external_fread_hook;
  amy_external_fwrite_hook = external_fwrite_hook;
  amy_external_fclose_hook = external_fclose_hook;
  #endif
}
