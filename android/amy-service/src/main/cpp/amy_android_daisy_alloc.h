#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *qspi_malloc(size_t size);
void qspi_free(void *ptr);

#ifdef __cplusplus
}
#endif
