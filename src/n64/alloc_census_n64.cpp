// Debug ROMs only (N64_DEBUG_OVERLAY=1, the Makefile adds the --wrap options): who asks for memory while playing. Each place in the code that
// allocates is one line of the overlay, with what it asked for since the game started, to find what leaves the heap in pieces.
// The tables take about 34 KB of RAM, so a debug ROM has that much less free than a normal one.
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if N64_DEBUG_OVERLAY
extern "C" {
void* __real_malloc(size_t n);
void* __real_calloc(size_t a, size_t b);
void* __real_realloc(void* p, size_t n);
void* __real_memalign(size_t a, size_t n);
void __real_free(void* p);
void* __real__Znwj(size_t n);
void* __real__Znaj(size_t n);
}

namespace
{
struct Site
{
    void* ra;
    uint32_t calls, bytes, live, largest;
};
struct Block
{
    void* p;
    uint32_t size_site; // size << 8 | site
};
const int kSites = 64;
const uint32_t kBlocks = 4096;
void* const kTomb = reinterpret_cast<void*>(1);
Site gSites[kSites];
int gSiteCount;
Block gBlocks[kBlocks];
void* gNewCaller; // operator new ends up in calloc through crt_stubs: its own caller is the one that matters
void* gLastPtr;   // to tell a test (allocated and freed right away) from a real use
uint32_t gLastSize;
int gLastSite;

inline uint32_t Hash(void* p)
{
    return (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)) >> 4) * 2654435761u >> 20;
}

int SiteOf(void* ra)
{
    for (int i = 0; i < gSiteCount; ++i)
    {
        if (gSites[i].ra == ra)
        {
            return i;
        }
    }
    if (gSiteCount < kSites - 1)
    {
        gSites[gSiteCount].ra = ra;
        return gSiteCount++;
    }
    gSites[kSites - 1].ra = nullptr; // the rest, together
    return kSites - 1;
}

void* Note(void* p, size_t n, void* ra)
{
    gLastPtr = nullptr;
    if (!p)
    {
        gNewCaller = nullptr;
        return p;
    }
    if (gNewCaller)
    {
        ra = gNewCaller;
        gNewCaller = nullptr;
    }
    const int s = SiteOf(ra);
    Site& site = gSites[s];
    const uint32_t size = static_cast<uint32_t>(n);
    ++site.calls;
    site.bytes += size;
    if (size > site.largest)
    {
        site.largest = size;
    }
    gLastPtr = p;
    gLastSize = size;
    gLastSite = s;
    uint32_t h = Hash(p);
    for (int probe = 0; probe < 64; ++probe, h = (h + 1) & (kBlocks - 1))
    {
        if (!gBlocks[h].p || gBlocks[h].p == kTomb)
        {
            gBlocks[h].p = p;
            gBlocks[h].size_site = (size << 8) | static_cast<uint32_t>(s);
            site.live += size; // only what can be taken off when freed
            break;
        }
    }
    return p;
}

void Forget(void* p)
{
    if (!p)
    {
        return;
    }
    if (p == gLastPtr)
    {
        --gSites[gLastSite].calls;
        gSites[gLastSite].bytes -= gLastSize;
    }
    gLastPtr = nullptr;
    uint32_t h = Hash(p);
    for (int probe = 0; probe < 64 && gBlocks[h].p; ++probe, h = (h + 1) & (kBlocks - 1))
    {
        if (gBlocks[h].p == p)
        {
            gSites[gBlocks[h].size_site & 255u].live -= gBlocks[h].size_site >> 8;
            gBlocks[h].p = kTomb;
            return;
        }
    }
}
} // namespace

extern "C" {
void* __wrap_malloc(size_t n) { return Note(__real_malloc(n), n, __builtin_return_address(0)); }
void* __wrap_calloc(size_t a, size_t b) { return Note(__real_calloc(a, b), a * b, __builtin_return_address(0)); }
void* __wrap_memalign(size_t a, size_t n) { return Note(__real_memalign(a, n), n, __builtin_return_address(0)); }
void* __wrap_realloc(void* p, size_t n)
{
    Forget(p);
    return Note(__real_realloc(p, n), n, __builtin_return_address(0));
}
void __wrap_free(void* p)
{
    Forget(p);
    __real_free(p);
}
void* __wrap__Znwj(size_t n)
{
    gNewCaller = __builtin_return_address(0);
    return __real__Znwj(n);
}
void* __wrap__Znaj(size_t n)
{
    gNewCaller = __builtin_return_address(0);
    return __real__Znaj(n);
}

// What was asked for during the level load does not count: only what happens while playing
void gta2_alloc_census_restart(void)
{
    for (int i = 0; i < gSiteCount; ++i)
    {
        gSites[i].calls = gSites[i].bytes = gSites[i].largest = 0;
    }
}

// Writes the places that asked for the most memory, one per line: address, times, KB still held, largest KB asked for at once
void gta2_alloc_census_text(char* out, size_t size, int lines)
{
    bool shown[kSites] = {};
    out[0] = 0;
    size_t used = 0;
    for (int l = 0; l < lines; ++l)
    {
        int best = -1;
        for (int i = 0; i < gSiteCount; ++i)
        {
            if (!shown[i] && gSites[i].bytes && (best < 0 || gSites[i].bytes > gSites[best].bytes))
            {
                best = i;
            }
        }
        if (best < 0)
        {
            break;
        }
        shown[best] = true;
        const Site& s = gSites[best];
        used += static_cast<size_t>(snprintf(out + used, size - used, "%06lx n%lu L%lu M%lu\n", static_cast<unsigned long>(reinterpret_cast<uintptr_t>(s.ra) & 0xFFFFFFu),
                                             static_cast<unsigned long>(s.calls), static_cast<unsigned long>(s.live / 1024u),
                                             static_cast<unsigned long>((s.largest + 1023u) / 1024u)));
        if (used >= size)
        {
            break;
        }
    }
}
}
#endif
