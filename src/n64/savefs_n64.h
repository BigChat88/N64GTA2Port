// Save files in the cartridge FlashRAM: see savefs_n64.c. All functions return 0 (or a count) on success, negative on failure.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int gta2_savefs_init(void); // 1 if a FlashRAM chip was found; without one, nothing else may be called
int gta2_savefs_count(void);
const char *gta2_savefs_name(int i);
uint32_t gta2_savefs_size(int i);
int gta2_savefs_load(const char *name, void *dst, uint32_t len);
int gta2_savefs_store(const char *name, const void *data, uint32_t len);
int gta2_savefs_remove(const char *name);
uint32_t gta2_savefs_used(void); // bytes of the log, older versions included

#ifdef __cplusplus
}
#endif
