// MSVC C-runtime behaviour the game relies on, for the N64 build.
//
// wchar_t is 2 bytes here (-fshort-wchar) while the C library assumes 4, so
// the wide-string functions the game uses are reimplemented over UTF-16.
// File paths arrive Windows-style ("data\\Bil.sty"); the ROM holds the
// game files lowercased with '/' separators, and every path is normalised
// the same way before it reaches the filesystem.
#include <ctype.h>
#include <errno.h>
#include <io.h>
#include <map>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <string>
#include <vector>

#undef fopen
#undef fclose
#undef remove
#undef wcslen
#undef wcscpy
#undef wcsncpy
#undef wcscmp
#undef wcstombs
#undef mbstowcs
#undef swprintf
#undef vswprintf

#ifdef N64
extern "C" double gta2_n64_now_ms(void); // main_n64.c (libdragon stays isolated in C)
extern "C" const void* gta2_n64_rom_ptr(const char* romPath, unsigned offset);
extern "C" int gta2_n64_dir_find(const char* dirpath, const char* glob, char* out, int outsz);
#define GTA2_ROM_PREFIX std::string("rom:/")
static double NowMs() { return gta2_n64_now_ms(); }
#else
#include <time.h>
#define GTA2_ROM_PREFIX std::string("")
static double NowMs() { timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6; }
#endif

namespace
{
std::string NormalisePath(const char* path)
{
    std::string out;
    for (const char* p = path; *p; ++p)
    {
        char c = *p == '\\' ? '/' : *p;
        out.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
    }
    while (out.size() > 2 && out[0] == '.' && out[1] == '/')
    {
        out.erase(0, 2);
    }
    // Windows ignores trailing spaces in names ("data\\frontend\\DemoInfo.tga " in the menu screen table): so does this.
    while (!out.empty() && out.back() == ' ')
    {
        out.pop_back();
    }
    // The game looks for its music CD as "<drive>:\GTAUDIO\". The music files are
    // installed in data/gtaudio, so drive C stands in for the CD.
    if (out.compare(0, 10, "c:/gtaudio") == 0)
    {
        out = "data/gtaudio" + out.substr(10);
    }
    return GTA2_ROM_PREFIX + out;
}

void AppendWide(std::vector<wchar_t>& out, const char* s)
{
    while (*s)
    {
        out.push_back(static_cast<unsigned char>(*s++));
    }
}

void AppendWide(std::vector<wchar_t>& out, const wchar_t* s)
{
    while (*s)
    {
        out.push_back(*s++);
    }
}
} // namespace

extern "C" {

size_t gta2_wcslen(const wchar_t* s)
{
    size_t n = 0;
    while (s[n])
    {
        ++n;
    }
    return n;
}

wchar_t* gta2_wcscpy(wchar_t* d, const wchar_t* s)
{
    wchar_t* r = d;
    while ((*d++ = *s++))
    {
    }
    return r;
}

wchar_t* gta2_wcsncpy(wchar_t* d, const wchar_t* s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; ++i)
    {
        d[i] = s[i];
    }
    for (; i < n; ++i)
    {
        d[i] = 0;
    }
    return d;
}

int gta2_wcscmp(const wchar_t* a, const wchar_t* b)
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return static_cast<int>(static_cast<unsigned short>(*a)) - static_cast<int>(static_cast<unsigned short>(*b));
}

// MSVC wide printf: %s is a wide string and %S a narrow one, the reverse of C99.
int gta2_vswprintf(wchar_t* buf, const wchar_t* fmt, va_list ap)
{
    std::vector<wchar_t> out;
    for (const wchar_t* f = fmt; *f;)
    {
        if (*f != L'%')
        {
            out.push_back(*f++);
            continue;
        }
        std::string spec = "%";
        ++f;
        while (*f == L'-' || *f == L'+' || *f == L' ' || *f == L'#' || *f == L'0')
        {
            spec.push_back(static_cast<char>(*f++));
        }
        while (*f && (*f == L'*' || (*f >= L'0' && *f <= L'9') || *f == L'.'))
        {
            if (*f == L'*')
            {
                spec += std::to_string(va_arg(ap, int));
                ++f;
            }
            else
            {
                spec.push_back(static_cast<char>(*f++));
            }
        }
        bool isLong = false;
        bool isShort = false;
        while (*f == L'l' || *f == L'h' || *f == L'I' || *f == L'w')
        {
            isLong |= *f == L'l' || *f == L'w';
            isShort |= *f == L'h';
            ++f;
        }
        const wchar_t conv = *f ? *f++ : 0;
        char tmp[512];
        switch (conv)
        {
            case L'%':
                out.push_back(L'%');
                break;
            case L'c':
                out.push_back(static_cast<wchar_t>(va_arg(ap, int)));
                break;
            case L's':
            {
                if (isShort)
                {
                    AppendWide(out, va_arg(ap, const char*));
                    break;
                }
                const wchar_t* s = va_arg(ap, const wchar_t*);
                // Width/precision for wide strings: format a padded narrow placeholder
                // of the right length, then substitute the characters.
                std::vector<wchar_t> w;
                AppendWide(w, s ? s : L"(null)");
                std::string narrow(w.size(), 'x');
                spec += "s";
                snprintf(tmp, sizeof(tmp), spec.c_str(), narrow.c_str());
                size_t wi = 0;
                for (const char* t = tmp; *t; ++t)
                {
                    out.push_back(*t == 'x' && wi < w.size() ? w[wi++] : static_cast<wchar_t>(*t));
                }
                break;
            }
            case L'S':
            {
                const char* s = va_arg(ap, const char*);
                spec += "s";
                snprintf(tmp, sizeof(tmp), spec.c_str(), s ? s : "(null)");
                AppendWide(out, tmp);
                break;
            }
            case L'd':
            case L'i':
            case L'u':
            case L'x':
            case L'X':
            case L'o':
                spec.push_back(static_cast<char>(conv));
                snprintf(tmp, sizeof(tmp), spec.c_str(), va_arg(ap, int));
                AppendWide(out, tmp);
                break;
            case L'f':
            case L'g':
            case L'e':
                spec.push_back(static_cast<char>(conv));
                snprintf(tmp, sizeof(tmp), spec.c_str(), va_arg(ap, double));
                AppendWide(out, tmp);
                break;
            case L'p':
                snprintf(tmp, sizeof(tmp), "%p", va_arg(ap, void*));
                AppendWide(out, tmp);
                break;
            default:
                break;
        }
        (void)isLong;
    }
    for (size_t i = 0; i < out.size(); ++i)
    {
        buf[i] = out[i];
    }
    buf[out.size()] = 0;
    return static_cast<int>(out.size());
}

int gta2_swprintf(wchar_t* buf, const wchar_t* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = gta2_vswprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

char* gta2_itoa(int value, char* buf, int radix)
{
    if (radix == 10)
    {
        sprintf(buf, "%d", value);
        return buf;
    }
    unsigned v = static_cast<unsigned>(value);
    char tmp[40];
    int n = 0;
    do
    {
        const unsigned d = v % radix;
        tmp[n++] = static_cast<char>(d < 10 ? '0' + d : 'a' + d - 10);
        v /= radix;
    } while (v);
    for (int i = 0; i < n; ++i)
    {
        buf[i] = tmp[n - 1 - i];
    }
    buf[n] = 0;
    return buf;
}

wchar_t* gta2_itow(int value, wchar_t* buf, int radix)
{
    char tmp[40];
    gta2_itoa(value, tmp, radix);
    int i = 0;
    for (; tmp[i]; ++i)
    {
        buf[i] = static_cast<unsigned char>(tmp[i]);
    }
    buf[i] = 0;
    return buf;
}

char* gta2_strupr(char* s)
{
    for (char* p = s; *p; ++p)
    {
        *p = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
    }
    return s;
}

// Files under player/ (profiles, saves) live in a RAM file system (ramfs_n64.cpp):
// the DragonFS is read-only.
FILE* gta2_ramfs_open(const std::string& key, const char* mode);

static bool IsPersistent(const std::string& norm)
{
    return norm.find("player/") != std::string::npos;
}

static FILE* OpenFile(const std::string& norm, const char* mode)
{
    return IsPersistent(norm) ? gta2_ramfs_open(norm, mode) : fopen(norm.c_str(), mode);
}

FILE* gta2_fopen(const char* path, const char* mode)
{
    const std::string norm = NormalisePath(path);
    return OpenFile(norm, mode);
}

int gta2_fclose(FILE* f)
{
    return fclose(f);
}

// Path normalisation for other port layers (e.g. audio streams).
const char* gta2_port_normalise_path(const char* path)
{
    static std::string last;
    last = NormalisePath(path);
    return last.c_str();
}

// Monotonic milliseconds: since page load, or continuing from a loaded quick save's clock
// (gta2_port_set_clock), so the game never sees time jump or run backwards.
static double gClockOffset = 0;
double gta2_port_now_ms()
{
    return NowMs() + gClockOffset;
}
// The game reads the x86 time-stamp counter for its profiler and CPU-speed
// estimate. A 1 GHz counter derived from the monotonic clock serves both.
unsigned long long gta2_port_rdtsc()
{
    return static_cast<unsigned long long>(gta2_port_now_ms() * 1.0e6);
}

} // extern "C"

int _stat(const char* path, struct _stat* st)
{
    // No stat on DragonFS: try to open and measure.
    FILE* f = OpenFile(NormalisePath(path), "rb");
    if (!f)
    {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    memset(st, 0, sizeof(*st));
    st->st_size = ftell(f);
    fclose(f);
    return 0;
}

// _findfirst: the engine uses it to test whether a file exists (attract replays, .bik, saves).
// No wildcards: probes the file on DragonFS. With wildcard (e.g.: data\\*.seq): no results.
extern "C" intptr_t _findfirst(const char* pattern, struct _finddata_t* data)
{
    const std::string norm = NormalisePath(pattern);
    if (norm.find_first_of("*?") != std::string::npos)
    {
#ifdef N64
        const size_t sl = norm.rfind('/');
        if (sl == std::string::npos)
        {
            return -1;
        }
        char found[260];
        if (!gta2_n64_dir_find(norm.substr(0, sl).c_str(), norm.c_str() + sl + 1, found, sizeof(found)))
        {
            return -1;
        }
        memset(data, 0, sizeof(*data));
        strncpy(data->name, found, sizeof(data->name) - 1);
        data->attrib = _A_NORMAL;
        return 1;
#else
        return -1;
#endif
    }
    FILE* f = OpenFile(norm, "rb");
    if (!f)
    {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fclose(f);
    memset(data, 0, sizeof(*data));
    const size_t slash = norm.rfind('/');
    strncpy(data->name, norm.c_str() + (slash == std::string::npos ? 0 : slash + 1), sizeof(data->name) - 1);
    data->size = static_cast<unsigned long>(size);
    data->attrib = _A_NORMAL;
    return 1; // stateless handle
}
extern "C" int _findnext(intptr_t, struct _finddata_t*) { return -1; }
extern "C" int _findclose(intptr_t) { return 0; }

#ifdef N64
extern "C" const void* gta2_n64_map_file(const char* path, long offset)
{
    return gta2_n64_rom_ptr(NormalisePath(path).c_str(), static_cast<unsigned>(offset));
}
#endif
