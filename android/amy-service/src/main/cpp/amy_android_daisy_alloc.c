#include "amy_android_daisy_alloc.h"

#include <stdlib.h>

void *qspi_malloc(size_t size) {
    return malloc(size);
}

void qspi_free(void *ptr) {
    free(ptr);
}
