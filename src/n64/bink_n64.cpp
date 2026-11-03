// Bink video stand-in. Opening a movie fails, which makes the game skip the
// intro and cut-scene movies the same way it does when a .bik file is
// missing. The intro is played before the engine starts (intro_n64.c).
#define BUILD_BINK
#include "3rdParty/bink.h"

extern "C" {
BINK* __stdcall BinkOpen(const char*, s32)
{
    return nullptr;
}
void __stdcall BinkClose(BINK*)
{
}
void __stdcall BinkGetSummary(BINK*, void*)
{
}
s32 __stdcall BinkDoFrame(BINK*)
{
    return 0;
}
void __stdcall BinkNextFrame(BINK*)
{
}
s32 __stdcall BinkWait(BINK*)
{
    return 0;
}
s32 __stdcall BinkCopyToBuffer(BINK*, void*, s32, s32, s32, s32, s32)
{
    return 0;
}
void __stdcall BinkSetSoundSystem(void*, s32)
{
}
void __stdcall BinkSetIOSize(s32)
{
}
void* __stdcall BinkOpenMiles(s32)
{
    return nullptr;
}
BINKBUFFER* __stdcall BinkBufferOpen(HWND, s32, s32, s32)
{
    return nullptr;
}
void __stdcall BinkBufferClose(BINKBUFFER*)
{
}
void* __stdcall BinkBufferLock(BINKBUFFER*)
{
    return nullptr;
}
void __stdcall BinkBufferUnlock(BINKBUFFER*)
{
}
void __stdcall BinkBufferBlit(BINKBUFFER*, void*, s32)
{
}
void __stdcall BinkBufferSetDDPrimary(void*)
{
}
void __stdcall BinkBufferCheckWinPos(BINKBUFFER*, s32*, s32*)
{
}
void __stdcall BinkBufferSetOffset(BINKBUFFER*, s32, s32)
{
}
}
