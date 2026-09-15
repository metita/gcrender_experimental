#include "log.h"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace rendererlog
{
namespace
{
constexpr std::size_t kDeferredCapacity = 1024u * 1024u;
constexpr std::size_t kLineCapacity = 4096u;

SRWLOCK g_lock = SRWLOCK_INIT;
bool g_deferred = false;
bool g_deferredTruncated = false;
std::size_t g_deferredSize = 0;
char g_deferredBuffer[kDeferredCapacity];

void PathNextToUs(char* out, int n)
{
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&PathNextToUs), &self);
    char dir[MAX_PATH]{};
    GetModuleFileNameA(self, dir, MAX_PATH);
    char* slash = strrchr(dir, '\\');
    if (slash) *(slash + 1) = 0;
    _snprintf(out, n, "%sgcrender.log", dir);
    out[n - 1] = 0;
}

void WriteBlock(const char* data, std::size_t size, bool truncated)
{
    char path[MAX_PATH];
    PathNextToUs(path, sizeof(path));
    FILE* f = fopen(path, "ab");
    if (!f)
        return;
    if (size)
        fwrite(data, 1, size, f);
    if (truncated)
    {
        static const char marker[] =
            "[gcrender] deferred log buffer full, additional lines were dropped\n";
        fwrite(marker, 1, sizeof(marker) - 1, f);
    }
    fclose(f);
}
}

void SetDeferred(bool deferred)
{
    AcquireSRWLockExclusive(&g_lock);
    if (deferred)
    {
        if (!g_deferred)
        {
            g_deferred = true;
            g_deferredSize = 0;
            g_deferredTruncated = false;
        }
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }

    if (!g_deferred)
    {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }

    g_deferred = false;
    WriteBlock(g_deferredBuffer, g_deferredSize, g_deferredTruncated);
    g_deferredSize = 0;
    g_deferredTruncated = false;
    ReleaseSRWLockExclusive(&g_lock);
}

void Line(const char* fmt, ...)
{
    char line[kLineCapacity]{};
    int prefix = _snprintf(line, sizeof(line), "[%8lu] ", GetTickCount());
    if (prefix < 0 || static_cast<std::size_t>(prefix) >= sizeof(line))
        prefix = 0;

    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(line + prefix, sizeof(line) - static_cast<std::size_t>(prefix) - 2,
               fmt, ap);
    va_end(ap);
    line[sizeof(line) - 2] = 0;

    std::size_t length = std::strlen(line);
    line[length++] = '\n';

    AcquireSRWLockExclusive(&g_lock);
    if (g_deferred)
    {
        if (length <= kDeferredCapacity - g_deferredSize)
        {
            std::memcpy(g_deferredBuffer + g_deferredSize, line, length);
            g_deferredSize += length;
        }
        else
        {
            g_deferredTruncated = true;
        }
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    WriteBlock(line, length, false);
    ReleaseSRWLockExclusive(&g_lock);
}
}
