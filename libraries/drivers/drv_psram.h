#ifndef DRV_PSRAM_H
#define DRV_PSRAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int psram_init(void);
bool psram_is_ready(void);
uintptr_t psram_get_base(void);
size_t psram_get_size(void);
void psram_cache_sync(void);

#ifdef __cplusplus
}
#endif

#endif /* DRV_PSRAM_H */
