// File system for saves/profiles (player\*). The DragonFS (rom:/) is read-only; the engine writes and reads back
// plyslot*.dat, plyslot*.svg, hiscores etc. through this layer. A file only stays in RAM while it is open or has changes
// not yet saved. Changes reach the FlashRAM (savefs_n64.c) once the engine stops writing: it builds a saved game out of
// several fopen/fwrite/fclose in a row, and each of those must not become a write to the chip.
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "savefs_n64.h"

namespace
{
struct RamFile
{
    std::vector<unsigned char> data;
    size_t savedSize = 0; // size of the version in the FlashRAM, while not loaded
    bool loaded = true;
    bool dirty = false;
    int openCount = 0;
};
std::map<std::string, std::shared_ptr<RamFile>> gFiles; // shared: a file removed while open stays valid until closed
bool gFlash = false;
int gQuietFrames = 0;
int gRetryFrames = 0;
int gStored = 0, gFailed = 0;
uint32_t gLastSize = 0, gLastPacked = 0;

constexpr int kFramesBeforeSaving = 30;
constexpr int kFramesBeforeRetry = 1800; // the save failed (no room or no RAM): keep it in RAM and try again later

void Unload(RamFile& f)
{
    if (gFlash && !f.dirty && f.openCount == 0)
    {
        f.savedSize = f.data.size();
        std::vector<unsigned char>().swap(f.data);
        f.loaded = false;
    }
}

struct Cookie
{
    std::shared_ptr<RamFile> file;
    size_t pos;
    bool canRead, canWrite, append;
};

int CReadImpl(void* c, char* buf, size_t n)
{
    Cookie* k = static_cast<Cookie*>(c);
    const size_t size = k->file->data.size();
    const size_t avail = k->pos < size ? size - k->pos : 0;
    const size_t m = n < avail ? n : avail;
    if (m)
    {
        memcpy(buf, &k->file->data[k->pos], m);
    }
    k->pos += m;
    return static_cast<int>(m);
}

int CWriteImpl(void* c, const char* buf, size_t n)
{
    Cookie* k = static_cast<Cookie*>(c);
    if (k->append)
    {
        k->pos = k->file->data.size();
    }
    if (k->pos + n > k->file->data.size())
    {
        k->file->data.resize(k->pos + n);
    }
    if (n)
    {
        memcpy(&k->file->data[k->pos], buf, n);
    }
    k->pos += n;
    return static_cast<int>(n);
}

// returns the new position or -1
long long CSeekImpl(void* c, long long off, int whence)
{
    Cookie* k = static_cast<Cookie*>(c);
    const long long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? static_cast<long long>(k->pos) : static_cast<long long>(k->file->data.size());
    const long long np = base + off;
    if (np < 0)
    {
        return -1;
    }
    k->pos = static_cast<size_t>(np);
    return np;
}

int CClose(void* c)
{
    Cookie* k = static_cast<Cookie*>(c);
    RamFile& f = *k->file;
    --f.openCount;
    if (k->canWrite)
    {
        f.dirty = true;
        gQuietFrames = 0;
    }
    Unload(f);
    delete k;
    return 0;
}

// funopen (BSD, newlib): callback types vary between newlib versions; deduced from the actual declaration.
template <class T> struct RdT;
template <class N> struct RdT<int (*)(void*, char*, N)>
{
    static int fn(void* c, char* b, N n) { return CReadImpl(c, b, static_cast<size_t>(n)); }
};
template <class T> struct WrT;
template <class N> struct WrT<int (*)(void*, const char*, N)>
{
    static int fn(void* c, const char* b, N n) { return CWriteImpl(c, b, static_cast<size_t>(n)); }
};
template <class T> struct SkT;
template <class O> struct SkT<O (*)(void*, O, int)>
{
    static O fn(void* c, O off, int w) { return static_cast<O>(CSeekImpl(c, static_cast<long long>(off), w)); }
};

template <class C, class RD, class WR, class SK, class CL>
FILE* OpenWith(FILE* (*fo)(C, RD, WR, SK, CL), Cookie* k)
{
    return fo(k, k->canRead ? RdT<RD>::fn : static_cast<RD>(nullptr), k->canWrite ? WrT<WR>::fn : static_cast<WR>(nullptr),
              SkT<SK>::fn, static_cast<CL>(CClose));
}

bool Load(const std::string& key, RamFile& f)
{
    if (f.loaded)
    {
        return true;
    }
    f.data.resize(f.savedSize);
    if (gta2_savefs_load(key.c_str(), f.data.data(), static_cast<uint32_t>(f.savedSize)) != 0)
    {
        std::vector<unsigned char>().swap(f.data);
        return false;
    }
    f.loaded = true;
    return true;
}
} // namespace

extern "C" FILE* gta2_ramfs_open(const std::string& key, const char* mode)
{
    const bool plus = strchr(mode, '+') != nullptr;
    const char m = mode[0];
    auto it = gFiles.find(key);
    if (m == 'r' && it == gFiles.end())
    {
        errno = ENOENT;
        return nullptr;
    }
    std::shared_ptr<RamFile>& f = gFiles[key];
    if (!f)
    {
        f = std::make_shared<RamFile>();
    }
    if (m == 'w')
    {
        f->data.clear();
        f->loaded = true;
    }
    else if (!Load(key, *f))
    {
        errno = EIO;
        return nullptr;
    }
    Cookie* k = new Cookie{f, 0, m == 'r' || plus, m != 'r' || plus, m == 'a'};
    ++f->openCount;
    FILE* fp = OpenWith(&funopen, k);
    if (!fp)
    {
        --f->openCount;
        delete k;
        Unload(*f);
        return nullptr;
    }
    return fp;
}

// At boot, before the engine looks for its files. Without a FlashRAM chip the files just live in RAM until power-off.
extern "C" void gta2_ramfs_mount(void)
{
    gFlash = gta2_savefs_init() != 0;
    if (!gFlash)
    {
        return;
    }
    for (int i = 0; i < gta2_savefs_count(); ++i)
    {
        auto f = std::make_shared<RamFile>();
        f->loaded = false;
        f->savedSize = gta2_savefs_size(i);
        gFiles[gta2_savefs_name(i)] = f;
    }
}

// For the diagnostic overlay (N64_DEBUG_OVERLAY).
extern "C" void gta2_ramfs_status(int* flash, int* files, int* dirty, int* stored, int* failed, unsigned* lastSize, unsigned* lastPacked, unsigned* used)
{
    *lastSize = gLastSize;
    *lastPacked = gLastPacked;
    *used = gFlash ? gta2_savefs_used() : 0;
    *flash = gFlash;
    *files = static_cast<int>(gFiles.size());
    *dirty = 0;
    for (auto& e : gFiles)
    {
        *dirty += e.second->dirty;
    }
    *stored = gStored;
    *failed = gFailed;
}

// Once per game frame: saves what changed, once the engine has not closed a written file for a moment.
extern "C" void gta2_ramfs_tick(void)
{
    if (!gFlash)
    {
        return;
    }
    if (gRetryFrames > 0)
    {
        --gRetryFrames;
        return;
    }
    if (++gQuietFrames < kFramesBeforeSaving)
    {
        return;
    }
    gQuietFrames = kFramesBeforeSaving;
    for (auto& e : gFiles)
    {
        RamFile& f = *e.second;
        if (!f.dirty || f.openCount != 0)
        {
            continue;
        }
        const uint32_t usedBefore = gta2_savefs_used();
        if (gta2_savefs_store(e.first.c_str(), f.data.data(), static_cast<uint32_t>(f.data.size())) != 0)
        {
            ++gFailed;
            gRetryFrames = kFramesBeforeRetry;
            return;
        }
        ++gStored;
        gLastSize = static_cast<uint32_t>(f.data.size());
        gLastPacked = gta2_savefs_used() > usedBefore ? gta2_savefs_used() - usedBefore : 0;
        f.dirty = false;
        Unload(f);
    }
}
