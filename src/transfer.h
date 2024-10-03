// transfer.h
// data transfer over AMY messages
// b64 stuff from https://github.com/jwerle/b64.c

// bytes -> b64 length: 4 * n / 3 gives unpadded length.
// ((4 * n / 3) + 3) & ~3 gives padded length 

// so if max AMY message is 255 chars, 252 bytes of b64 is max = 189 bytes of pcm 

#ifndef TRANSFER_H
#define TRANSFER_H 1
#include <stdint.h>

typedef struct b64_buffer {
    char * ptr;
    int bufc;
} b64_buffer_t;

 // How much memory to allocate per buffer
#define B64_BUFFER_SIZE     (256)

 // Start buffered memory
int b64_buf_malloc(b64_buffer_t * buffer);


/**
 * Base64 index table.
 */

static const char b64_table[] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
  'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
  'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
  'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
  'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
  'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
  'w', 'x', 'y', 'z', '0', '1', '2', '3',
  '4', '5', '6', '7', '8', '9', '+', '/'
};

void start_receiving_transfer(uint32_t length, uint8_t * storage);
void parse_transfer_message(char * message, uint16_t len) ;

/**
 * Encode `unsigned char *' source with `size_t' size.
 * Returns a `char *' base64 encoded string.
 */

char * b64_encode (const unsigned char *src, b64_buffer_t * encbuf, size_t len);


/**
 * Decode `char *' source with `size_t' size.
 * Returns a `unsigned char *' base64 decoded string + size of decoded string.
 */
unsigned char *
b64_decode_ex (const char *, size_t, b64_buffer_t * decbuf, size_t *);

#endif