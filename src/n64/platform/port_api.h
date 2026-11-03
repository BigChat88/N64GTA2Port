// Internal interfaces between the port's platform, input and render layers.
#pragma once
#include <stdint.h>
#include <windows.h>

extern "C" {
// win32_shim.cpp
void gta2_port_post_message(UINT msg, WPARAM wp, LPARAM lp);

// exports.cpp: name -> renderer/video function, for GetProcAddress.
FARPROC gta2_port_find_export(const char* name);

// render: frames presented so far (the main loop yields after each one).
uint32_t gta2_port_present_count();
double gta2_port_now_ms();
}
