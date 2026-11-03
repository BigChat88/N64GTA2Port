/* _findfirst family over the in-memory game filesystem (src/n64/crt_n64.cpp). */
#pragma once
#include <stdint.h>
#include <time.h>
struct _finddata_t
{
    unsigned attrib;
    time_t time_create, time_access, time_write;
    unsigned long size;
    char name[260];
};
#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_SUBDIR 0x10
#ifdef __cplusplus
extern "C" {
#endif
intptr_t _findfirst(const char* pattern, struct _finddata_t* data);
int _findnext(intptr_t handle, struct _finddata_t* data);
int _findclose(intptr_t handle);
#ifdef __cplusplus
}
#endif
