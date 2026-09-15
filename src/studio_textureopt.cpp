#include "studio_textureopt.h"

#include "hw_build.h"
#include "log.h"
#include "world_vbo.h"

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace studio_textureopt
{
namespace
{
constexpr std::uintptr_t kStudioTextureLoadCallRva = 0x000C67C0u;
constexpr std::uintptr_t kStudioTextureLoadRva = 0x00067CF0u;
constexpr std::uintptr_t kQglTexImage2DRva = 0x027E3768u;

constexpr unsigned GL_TEXTURE_2D = 0x0DE1u;
constexpr unsigned GL_RGB = 0x1907u;
constexpr unsigned GL_RGBA = 0x1908u;
constexpr unsigned GL_UNSIGNED_BYTE = 0x1401u;
constexpr unsigned GL_EXTENSIONS = 0x1F03u;
constexpr unsigned GL_VENDOR = 0x1F00u;
constexpr unsigned GL_RENDERER = 0x1F01u;
constexpr unsigned GL_VERSION = 0x1F02u;
constexpr unsigned GL_TEXTURE_COMPRESSED = 0x86A1u;
constexpr unsigned GL_TEXTURE_COMPRESSED_IMAGE_SIZE = 0x86A0u;
constexpr unsigned GL_TEXTURE_WIDTH = 0x1000u;
constexpr unsigned GL_UNPACK_ALIGNMENT = 0x0CF5u;
constexpr unsigned GL_UNPACK_ROW_LENGTH = 0x0CF2u;
constexpr unsigned GL_UNPACK_SKIP_ROWS = 0x0CF3u;
constexpr unsigned GL_UNPACK_SKIP_PIXELS = 0x0CF4u;
constexpr unsigned GL_PIXEL_UNPACK_BUFFER_BINDING = 0x88EFu;
constexpr unsigned GL_COMPRESSED_RGB_S3TC_DXT1_EXT = 0x83F0u;
constexpr unsigned GL_COMPRESSED_RGBA_S3TC_DXT5_EXT = 0x83F3u;

constexpr unsigned STUDIO_NF_CHROME = 0x0002u;
constexpr unsigned STUDIO_NF_ALPHA = 0x0010u;
constexpr unsigned STUDIO_NF_ADDITIVE = 0x0020u;
constexpr unsigned STUDIO_NF_MASKED = 0x0040u;

constexpr std::uint32_t kCacheMagic = 0x3143425Au; // ZBC1
constexpr std::uint16_t kCacheVersion = 2;
constexpr unsigned kKnownStudioFlags = 0x007Fu;

using GlTexImage2DFn = void (WINAPI*)(unsigned target, int level,
                                      int internalFormat, int width, int height,
                                      int border, unsigned format, unsigned type,
                                      const void* pixels);
using GlCompressedTexImage2DFn = void (WINAPI*)(unsigned target, int level,
                                                unsigned internalFormat,
                                                int width, int height,
                                                int border, int imageSize,
                                                const void* data);
using GlGetCompressedTexImageFn = void (WINAPI*)(unsigned target, int level,
                                                 void* image);
using GlGetTexLevelParameterivFn = void (WINAPI*)(unsigned target, int level,
                                                  unsigned pname, int* params);
using GlGetStringFn = const unsigned char* (WINAPI*)(unsigned name);
using GlGetIntegervFn = void (WINAPI*)(unsigned pname, int* params);
using WglGetProcAddressFn = PROC (WINAPI*)(LPCSTR name);

std::uint8_t* g_hwBase = nullptr;
void* g_originalStudioTextureLoad = nullptr;
GlTexImage2DFn g_texImage2D = nullptr;
GlCompressedTexImage2DFn g_compressedTexImage2D = nullptr;
GlGetCompressedTexImageFn g_getCompressedTexImage = nullptr;
GlGetTexLevelParameterivFn g_getTexLevelParameteriv = nullptr;
GlGetStringFn g_getString = nullptr;
GlGetIntegervFn g_getIntegerv = nullptr;
WglGetProcAddressFn g_wglGetProcAddress = nullptr;

cvar_t* g_forceMips = nullptr;
cvar_t* g_compress = nullptr;
cvar_t* g_cache = nullptr;
cvar_t* g_minSize = nullptr;

std::uint32_t g_capsGeneration = 0;
bool g_capsKnown = false;
bool g_s3tcReady = false;
bool g_pboSupported = false;
std::uint64_t g_driverFingerprint = 0;
bool g_cacheDirectoryReady = false;
char g_cacheDirectory[MAX_PATH]{};
bool g_installed = false;

struct UploadScope
{
    const std::uint8_t* descriptor = nullptr;
    unsigned flags = 0;
    int width = 0;
    int height = 0;
    bool ordinaryName = false;
};

thread_local UploadScope* g_uploadScope = nullptr;

struct CacheHeader
{
    std::uint32_t magic = kCacheMagic;
    std::uint16_t version = kCacheVersion;
    std::uint16_t reserved = 0;
    std::uint64_t key = 0;
    std::uint32_t internalFormat = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t level = 0;
    std::uint32_t dataSize = 0;
    std::uint64_t payloadHash = 0;
};

struct CacheSourceLayout
{
    std::size_t startOffset = 0;
    std::size_t rowStride = 0;
    std::size_t rowBytes = 0;
};

std::uint64_t g_uploadCalls = 0;
std::uint64_t g_compressedUploads = 0;
std::uint64_t g_cacheHits = 0;
std::uint64_t g_cacheMisses = 0;
std::uint64_t g_forcedMipLoads = 0;
std::uint64_t g_cachePboRejects = 0;
std::uint64_t g_cachePackingRejects = 0;

bool PatchPointer(void** slot, void* expected, void* replacement)
{
    if (!slot || !expected || !replacement || *slot != expected)
        return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect))
        return false;
    if (*slot == expected)
        *slot = replacement;
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(*slot), oldProtect, &ignored);
    return *slot == replacement;
}

bool PatchRelativeCall(std::uint8_t* callsite, void* expected, void* replacement)
{
    if (!callsite || !expected || !replacement || callsite[0] != 0xE8)
        return false;
    const std::int32_t rel =
        *reinterpret_cast<const std::int32_t*>(callsite + 1);
    if (callsite + 5 + rel != reinterpret_cast<std::uint8_t*>(expected))
        return false;
    // Exact bytes following this build's Studio GL_LoadTexture call. This also
    // proves ESI still addresses the current texture descriptor (+0x48).
    if (callsite[5] != 0x89 || callsite[6] != 0x46 || callsite[7] != 0x04)
        return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect(callsite, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    *reinterpret_cast<std::int32_t*>(callsite + 1) =
        static_cast<std::int32_t>(
            reinterpret_cast<std::uint8_t*>(replacement) - (callsite + 5));
    DWORD ignored = 0;
    VirtualProtect(callsite, 5, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), callsite, 5);
    return true;
}

bool IsBadProc(PROC proc)
{
    const auto v = reinterpret_cast<std::uintptr_t>(proc);
    return !proc || v == 1u || v == 2u || v == 3u || v == ~std::uintptr_t{0};
}

void* ResolveExtension(const char* name)
{
    if (!g_wglGetProcAddress || !name)
        return nullptr;
    PROC proc = g_wglGetProcAddress(name);
    return IsBadProc(proc) ? nullptr : reinterpret_cast<void*>(proc);
}

bool HasExtension(const char* extensions, const char* needle)
{
    if (!extensions || !needle || !*needle || std::strchr(needle, ' '))
        return false;
    const std::size_t n = std::strlen(needle);
    const char* p = extensions;
    while ((p = std::strstr(p, needle)) != nullptr)
    {
        const bool left = p == extensions || p[-1] == ' ';
        const char rightChar = p[n];
        const bool right = rightChar == '\0' || rightChar == ' ';
        if (left && right)
            return true;
        p += n;
    }
    return false;
}

std::uint64_t HashBytes(std::uint64_t hash, const void* data, std::size_t size)
{
    constexpr std::uint64_t kPrime = 1099511628211ull;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= kPrime;
    }
    return hash;
}

template <typename T>
std::uint64_t HashValue(std::uint64_t hash, const T& value)
{
    return HashBytes(hash, &value, sizeof(value));
}

std::uint64_t HashString(std::uint64_t hash, const char* value)
{
    if (!value)
        return HashValue(hash, std::uint8_t{0});
    return HashBytes(hash, value, std::strlen(value) + 1u);
}

void RefreshCapabilities()
{
    if (!worldvbo::ContextGenerationReady())
        return;
    const std::uint32_t generation = worldvbo::ContextGeneration();
    if (g_capsKnown && generation == g_capsGeneration)
        return;

    g_s3tcReady = false;
    g_pboSupported = false;
    g_driverFingerprint = 1469598103934665603ull;
    g_compressedTexImage2D = nullptr;
    g_getCompressedTexImage = nullptr;

    if (!g_getString)
        return;
    const char* extensions = reinterpret_cast<const char*>(g_getString(GL_EXTENSIONS));
    const char* vendor = reinterpret_cast<const char*>(g_getString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(g_getString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(g_getString(GL_VERSION));
    if (!extensions || !vendor || !renderer || !version)
    {
        // A context may be between destruction and recreation while the engine
        // is pumping a frame. Retry instead of pinning this generation as
        // unsupported from a transient null glGetString result.
        return;
    }
    g_capsGeneration = generation;
    g_capsKnown = true;
    g_driverFingerprint = HashString(g_driverFingerprint, vendor);
    g_driverFingerprint = HashString(g_driverFingerprint, renderer);
    g_driverFingerprint = HashString(g_driverFingerprint, version);

    g_pboSupported =
        HasExtension(extensions, "GL_ARB_pixel_buffer_object") ||
        HasExtension(extensions, "GL_EXT_pixel_buffer_object");
    int glMajor = 0;
    int glMinor = 0;
    if (std::sscanf(version, "%d.%d", &glMajor, &glMinor) == 2 &&
        (glMajor > 2 || (glMajor == 2 && glMinor >= 1)))
    {
        g_pboSupported = true;
    }
    const bool hasS3tc =
        HasExtension(extensions, "GL_EXT_texture_compression_s3tc");
    if (!hasS3tc)
        return;

    g_compressedTexImage2D =
        reinterpret_cast<GlCompressedTexImage2DFn>(
            ResolveExtension("glCompressedTexImage2D"));
    g_getCompressedTexImage =
        reinterpret_cast<GlGetCompressedTexImageFn>(
            ResolveExtension("glGetCompressedTexImage"));
    g_s3tcReady = true;
    rendererlog::Line(
        "studio texture: S3TC ready for context generation %u (cache=%s)",
        generation,
        (g_compressedTexImage2D && g_getCompressedTexImage &&
         g_getTexLevelParameteriv) ? "ready" : "upload-only");
}

bool OrdinaryTextureName(const std::uint8_t* descriptor)
{
    if (!descriptor)
        return false;
    const unsigned char first = descriptor[0];
    return first != 'D' && first != 'd' &&
           first != 'R' && first != 'r';
}

int MinimumTextureSize()
{
    if (!g_minSize)
        return 64;
    const int value = static_cast<int>(g_minSize->value);
    return (std::max)(4, (std::min)(value, 4096));
}

bool BaseEligibility(const UploadScope& scope)
{
    if (!scope.descriptor || !scope.ordinaryName ||
        scope.width <= 0 || scope.height <= 0)
        return false;
    if ((scope.flags & (STUDIO_NF_CHROME | STUDIO_NF_ALPHA |
                        STUDIO_NF_ADDITIVE)) != 0u)
        return false;
    if ((scope.flags & ~kKnownStudioFlags) != 0u)
        return false;
    const int minSize = MinimumTextureSize();
    return scope.width >= minSize && scope.height >= minSize;
}

bool ShouldForceMips(const UploadScope& scope, int stockMipmap)
{
    if (stockMipmap != 0 || !g_forceMips || g_forceMips->value < 1.0f)
        return false;
    return BaseEligibility(scope) && (scope.flags & STUDIO_NF_MASKED) == 0u;
}

int CompressionMode()
{
    if (!g_compress)
        return 0;
    const int mode = static_cast<int>(g_compress->value);
    return (std::max)(0, (std::min)(mode, 2));
}

bool CompressionFormat(const UploadScope& scope, unsigned& internalFormat)
{
    const int mode = CompressionMode();
    if (mode <= 0 || !BaseEligibility(scope))
        return false;
    if ((scope.flags & STUDIO_NF_MASKED) != 0u)
    {
        if (mode < 2)
            return false;
        internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        return true;
    }
    internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
    return true;
}

bool EnsureCacheDirectory()
{
    if (g_cacheDirectoryReady)
        return g_cacheDirectory[0] != '\0';
    g_cacheDirectoryReady = true;

    HMODULE self = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&EnsureCacheDirectory), &self) ||
        !self)
        return false;

    char path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(self, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return false;
    char* slash = std::strrchr(path, '\\');
    if (!slash)
        slash = std::strrchr(path, '/');
    if (!slash)
        return false;
    *slash = '\0';

    char root[MAX_PATH]{};
    if (std::snprintf(root, sizeof(root), "%s\\renderer_cache", path) <= 0)
        return false;
    CreateDirectoryA(root, nullptr);
    if (std::snprintf(g_cacheDirectory, sizeof(g_cacheDirectory),
                      "%s\\studio", root) <= 0)
    {
        g_cacheDirectory[0] = '\0';
        return false;
    }
    CreateDirectoryA(g_cacheDirectory, nullptr);
    const DWORD attr = GetFileAttributesA(g_cacheDirectory);
    if (attr == INVALID_FILE_ATTRIBUTES ||
        (attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        g_cacheDirectory[0] = '\0';
        return false;
    }
    return true;
}

bool BuildCachePath(std::uint64_t key, char (&path)[MAX_PATH])
{
    if (!EnsureCacheDirectory())
        return false;
    const int written = std::snprintf(
        path, sizeof(path), "%s\\%016llx.zbc",
        g_cacheDirectory, static_cast<unsigned long long>(key));
    return written > 0 && written < static_cast<int>(sizeof(path));
}

void DeleteCache(std::uint64_t key)
{
    char path[MAX_PATH]{};
    if (BuildCachePath(key, path))
        DeleteFileA(path);
}

std::size_t ExpectedCompressedBytes(unsigned internalFormat,
                                    int width, int height)
{
    if (width <= 0 || height <= 0)
        return 0;
    const std::size_t blocksWide =
        (static_cast<std::size_t>(width) + 3u) / 4u;
    const std::size_t blocksHigh =
        (static_cast<std::size_t>(height) + 3u) / 4u;
    const std::size_t blockBytes =
        internalFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ? 8u :
        internalFormat == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT ? 16u : 0u;
    if (!blockBytes || blocksWide > SIZE_MAX / blocksHigh)
        return 0;
    const std::size_t blocks = blocksWide * blocksHigh;
    if (blocks > SIZE_MAX / blockBytes)
        return 0;
    return blocks * blockBytes;
}

bool LoadCache(std::uint64_t key, unsigned internalFormat,
               int width, int height, int level,
               std::vector<std::uint8_t>& data)
{
    char path[MAX_PATH]{};
    if (!BuildCachePath(key, path))
        return false;
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    CacheHeader header{};
    DWORD read = 0;
    LARGE_INTEGER fileSize{};
    const bool sizeKnown = GetFileSizeEx(file, &fileSize) != 0;
    const std::size_t expectedSize =
        ExpectedCompressedBytes(internalFormat, width, height);
    bool ok = ReadFile(file, &header, sizeof(header), &read, nullptr) != 0 &&
              read == sizeof(header) && header.magic == kCacheMagic &&
              header.version == kCacheVersion && header.key == key &&
              header.internalFormat == internalFormat &&
              header.width == width && header.height == height &&
              header.level == level && expectedSize > 0 &&
              header.dataSize == expectedSize && sizeKnown &&
              fileSize.QuadPart ==
                  static_cast<LONGLONG>(sizeof(CacheHeader) + expectedSize);
    if (ok)
    {
        try
        {
            data.resize(header.dataSize);
        }
        catch (...)
        {
            ok = false;
        }
    }
    if (ok)
    {
        read = 0;
        ok = ReadFile(file, data.data(), header.dataSize, &read, nullptr) != 0 &&
             read == header.dataSize;
        if (ok)
        {
            const std::uint64_t payloadHash =
                HashBytes(1469598103934665603ull, data.data(), data.size());
            ok = payloadHash == header.payloadHash;
        }
    }
    CloseHandle(file);
    if (!ok)
    {
        data.clear();
        DeleteFileA(path);
    }
    return ok;
}

void SaveCache(std::uint64_t key, unsigned internalFormat,
               int width, int height, int level,
               const std::vector<std::uint8_t>& data)
{
    if (data.empty() || data.size() > 64u * 1024u * 1024u)
        return;
    char path[MAX_PATH]{};
    if (!BuildCachePath(key, path))
        return;

    char temporary[MAX_PATH]{};
    const int written = std::snprintf(
        temporary, sizeof(temporary), "%s.%lu.tmp", path,
        static_cast<unsigned long>(GetCurrentThreadId()));
    if (written <= 0 || written >= static_cast<int>(sizeof(temporary)))
        return;

    HANDLE file = CreateFileA(temporary, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;

    CacheHeader header{};
    header.key = key;
    header.internalFormat = internalFormat;
    header.width = width;
    header.height = height;
    header.level = level;
    header.dataSize = static_cast<std::uint32_t>(data.size());
    header.payloadHash =
        HashBytes(1469598103934665603ull, data.data(), data.size());

    DWORD count = 0;
    bool ok = WriteFile(file, &header, sizeof(header), &count, nullptr) != 0 &&
              count == sizeof(header);
    if (ok)
    {
        count = 0;
        ok = WriteFile(file, data.data(), header.dataSize, &count, nullptr) != 0 &&
             count == header.dataSize;
    }
    CloseHandle(file);
    if (ok)
        MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING);
    else
        DeleteFileA(temporary);
}

bool CacheKey(const UploadScope& scope,
              int level, int width, int height,
              int stockInternalFormat,
              unsigned format, unsigned type,
              unsigned compressedInternalFormat,
              const void* pixels,
              const CacheSourceLayout& layout,
              std::uint64_t& outKey)
{
    std::uint64_t hash = 1469598103934665603ull;
    hash = HashValue(hash, kCacheVersion);
    hash = HashValue(hash, g_driverFingerprint);
    hash = HashValue(hash, scope.flags);
    hash = HashValue(hash, level);
    hash = HashValue(hash, width);
    hash = HashValue(hash, height);
    hash = HashValue(hash, stockInternalFormat);
    hash = HashValue(hash, format);
    hash = HashValue(hash, type);
    hash = HashValue(hash, compressedInternalFormat);
    hash = HashValue(hash, scope.width);
    hash = HashValue(hash, scope.height);
    const auto* base = static_cast<const std::uint8_t*>(pixels);
    __try
    {
        const std::uint8_t* row = base + layout.startOffset;
        for (int y = 0; y < height; ++y)
        {
            hash = HashBytes(hash, row, layout.rowBytes);
            row += layout.rowStride;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        outKey = 0;
        return false;
    }
    outKey = hash;
    return true;
}

bool CacheEnabled()
{
    return g_cache && g_cache->value >= 1.0f &&
           g_compressedTexImage2D && g_getCompressedTexImage &&
           g_getTexLevelParameteriv;
}

bool CacheSourceEligible(int width, int height, CacheSourceLayout& layout)
{
    layout = {};
    if (!g_getIntegerv)
        return false;
    int alignment = 0;
    int rowLength = -1;
    int skipRows = -1;
    int skipPixels = -1;
    __try
    {
        g_getIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        g_getIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
        g_getIntegerv(GL_UNPACK_SKIP_ROWS, &skipRows);
        g_getIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);
        if (alignment != 1 && alignment != 2 &&
            alignment != 4 && alignment != 8)
        {
            ++g_cachePackingRejects;
            return false;
        }
        if (rowLength < 0 || skipRows < 0 || skipPixels < 0)
        {
            ++g_cachePackingRejects;
            return false;
        }
        if (g_pboSupported)
        {
            int unpackBuffer = -1;
            g_getIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
            if (unpackBuffer != 0)
            {
                ++g_cachePboRejects;
                return false;
            }
        }

        constexpr std::size_t bytesPerPixel = 4u;
        const std::size_t maxSize =
            (std::numeric_limits<std::size_t>::max)();
        const std::size_t sourcePixelsPerRow =
            static_cast<std::size_t>(rowLength > 0 ? rowLength : width);
        const std::size_t logicalWidth = static_cast<std::size_t>(width);
        const std::size_t logicalHeight = static_cast<std::size_t>(height);
        if (sourcePixelsPerRow > maxSize / bytesPerPixel ||
            logicalWidth > maxSize / bytesPerPixel)
        {
            ++g_cachePackingRejects;
            return false;
        }
        const std::size_t unpackRowBytes =
            sourcePixelsPerRow * bytesPerPixel;
        const std::size_t alignmentSize =
            static_cast<std::size_t>(alignment);
        if (unpackRowBytes > maxSize - (alignmentSize - 1u))
        {
            ++g_cachePackingRejects;
            return false;
        }
        const std::size_t rowStride =
            (unpackRowBytes + alignmentSize - 1u) &
            ~(alignmentSize - 1u);
        const std::size_t rowBytes = logicalWidth * bytesPerPixel;

        const std::size_t skipRowsSize =
            static_cast<std::size_t>(skipRows);
        const std::size_t skipPixelsSize =
            static_cast<std::size_t>(skipPixels);
        if ((rowStride != 0 && skipRowsSize > maxSize / rowStride) ||
            skipPixelsSize > maxSize / bytesPerPixel)
        {
            ++g_cachePackingRejects;
            return false;
        }
        const std::size_t rowsOffset = skipRowsSize * rowStride;
        const std::size_t pixelsOffset = skipPixelsSize * bytesPerPixel;
        if (rowsOffset > maxSize - pixelsOffset)
        {
            ++g_cachePackingRejects;
            return false;
        }
        const std::size_t startOffset = rowsOffset + pixelsOffset;

        if (logicalHeight > 0)
        {
            const std::size_t lastRow = logicalHeight - 1u;
            if ((rowStride != 0 && lastRow > maxSize / rowStride) ||
                startOffset > maxSize - lastRow * rowStride)
            {
                ++g_cachePackingRejects;
                return false;
            }
            const std::size_t lastRowOffset =
                startOffset + lastRow * rowStride;
            if (rowBytes > maxSize - lastRowOffset)
            {
                ++g_cachePackingRejects;
                return false;
            }
        }

        layout.startOffset = startOffset;
        layout.rowStride = rowStride;
        layout.rowBytes = rowBytes;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void WINAPI TexImage2D_Hook(unsigned target, int level, int internalFormat,
                            int width, int height, int border,
                            unsigned format, unsigned type, const void* pixels)
{
    if (!g_texImage2D)
        return;
    ++g_uploadCalls;

    UploadScope* scope = g_uploadScope;
    if (!scope || target != GL_TEXTURE_2D || level < 0 || border != 0 ||
        width <= 0 || height <= 0 || !pixels ||
        type != GL_UNSIGNED_BYTE || format != GL_RGBA)
    {
        g_texImage2D(target, level, internalFormat, width, height, border,
                     format, type, pixels);
        return;
    }

    unsigned compressedInternalFormat = 0;
    if (!CompressionFormat(*scope, compressedInternalFormat))
    {
        g_texImage2D(target, level, internalFormat, width, height, border,
                     format, type, pixels);
        return;
    }

    RefreshCapabilities();
    if (!g_s3tcReady || !g_getTexLevelParameteriv)
    {
        g_texImage2D(target, level, internalFormat, width, height, border,
                     format, type, pixels);
        return;
    }

    constexpr int channels = 4;
    const std::size_t widthSize = static_cast<std::size_t>(width);
    const std::size_t heightSize = static_cast<std::size_t>(height);
    if (heightSize != 0 &&
        widthSize > std::numeric_limits<std::size_t>::max() / heightSize)
    {
        g_texImage2D(target, level, internalFormat, width, height, border,
                     format, type, pixels);
        return;
    }
    const std::size_t pixelCount =
        widthSize * heightSize;
    if (pixelCount > (64u * 1024u * 1024u) / static_cast<std::size_t>(channels))
    {
        g_texImage2D(target, level, internalFormat, width, height, border,
                     format, type, pixels);
        return;
    }
    const std::size_t byteCount = pixelCount * static_cast<std::size_t>(channels);
    CacheSourceLayout cacheLayout{};
    bool useCache =
        CacheEnabled() && CacheSourceEligible(width, height, cacheLayout);
    std::uint64_t key = 0;
    if (useCache)
    {
        useCache = CacheKey(
            *scope, level, width, height, internalFormat, format, type,
            compressedInternalFormat, pixels, cacheLayout, key);
    }

    if (useCache)
    {
        std::vector<std::uint8_t> cached;
        if (LoadCache(key, compressedInternalFormat, width, height, level, cached))
        {
            g_compressedTexImage2D(
                target, level, compressedInternalFormat, width, height, border,
                static_cast<int>(cached.size()), cached.data());
            int uploadedWidth = 0;
            int compressed = 0;
            g_getTexLevelParameteriv(target, level, GL_TEXTURE_WIDTH,
                                     &uploadedWidth);
            if (uploadedWidth == width)
                g_getTexLevelParameteriv(target, level, GL_TEXTURE_COMPRESSED,
                                         &compressed);
            if (uploadedWidth == width && compressed)
            {
                ++g_cacheHits;
                ++g_compressedUploads;
                return;
            }
            // The cache payload passed all structural checks but the driver did
            // not define the requested compressed level. Restore exact stock
            // content while the original source pixels are still available.
            g_texImage2D(target, level, internalFormat, width, height, border,
                         format, type, pixels);
            DeleteCache(key);
            return;
        }
        ++g_cacheMisses;
    }

    // The driver accepts ordinary RGB/RGBA bytes and produces the requested
    // S3TC representation in the existing Gold texture object. Texture IDs,
    // filtering, wrap state and every Gold-generated mip level remain intact.
    g_texImage2D(target, level, static_cast<int>(compressedInternalFormat),
                 width, height, border, format, type, pixels);
    int uploadedWidth = 0;
    int compressed = 0;
    g_getTexLevelParameteriv(target, level, GL_TEXTURE_WIDTH, &uploadedWidth);
    if (uploadedWidth == width)
        g_getTexLevelParameteriv(target, level, GL_TEXTURE_COMPRESSED,
                                 &compressed);
    if (uploadedWidth != width || !compressed)
    {
        g_texImage2D(target, level, internalFormat, width, height, border,
                     format, type, pixels);
        return;
    }
    ++g_compressedUploads;

    if (useCache)
    {
        int compressedBytes = 0;
        g_getTexLevelParameteriv(target, level,
                                 GL_TEXTURE_COMPRESSED_IMAGE_SIZE,
                                 &compressedBytes);
        const std::size_t expectedSize =
            ExpectedCompressedBytes(compressedInternalFormat, width, height);
        if (compressedBytes > 0 && expectedSize > 0 &&
            static_cast<std::size_t>(compressedBytes) == expectedSize)
        {
            try
            {
                std::vector<std::uint8_t> encoded(
                    static_cast<std::size_t>(compressedBytes));
                g_getCompressedTexImage(target, level, encoded.data());
                SaveCache(key, compressedInternalFormat, width, height, level,
                          encoded);
            }
            catch (...)
            {
                // Cache population is optional, the already uploaded texture is
                // still valid and authoritative.
            }
        }
    }
}

int CallOriginalStudioTextureLoad(void* identifier,
                                  int width, int height, std::uint8_t* data,
                                  int mipmap, int iType, std::uint8_t* palette,
                                  void* arg7, int filter)
{
    int result = 0;
    void* fn = g_originalStudioTextureLoad;
    __asm
    {
        mov ecx, identifier
        mov edx, 4
        push filter
        push arg7
        push palette
        push iType
        push mipmap
        push data
        push height
        push width
        call fn
        add esp, 20h
        mov result, eax
    }
    return result;
}

extern "C" int __cdecl StudioTextureLoadScoped(
    void* identifier, const std::uint8_t* descriptorPlus48,
    int width, int height, std::uint8_t* data, int mipmap, int iType,
    std::uint8_t* palette, void* arg7, int filter)
{
    UploadScope scope{};
    if (descriptorPlus48)
    {
        scope.descriptor = descriptorPlus48 - 0x48;
        __try
        {
            scope.flags = *reinterpret_cast<const unsigned*>(
                scope.descriptor + 0x40);
            scope.width = *reinterpret_cast<const int*>(
                scope.descriptor + 0x44);
            scope.height = *reinterpret_cast<const int*>(
                scope.descriptor + 0x48);
            scope.ordinaryName = OrdinaryTextureName(scope.descriptor);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            scope = {};
        }
    }

    int effectiveMipmap = mipmap;
    if (ShouldForceMips(scope, mipmap))
    {
        effectiveMipmap = 1;
        ++g_forcedMipLoads;
    }

    UploadScope* previous = g_uploadScope;
    g_uploadScope = &scope;
    const int result = CallOriginalStudioTextureLoad(
        identifier, width, height, data, effectiveMipmap, iType, palette,
        arg7, filter);
    g_uploadScope = previous;
    return result;
}

__declspec(naked) void StudioTextureLoadCallsiteHook()
{
    __asm
    {
        // Original ABI at hw+0xC67C0: ECX carries Gold's identifier object,
        // ESI points at mstudiotexture_t+0x48, and eight caller-clean stack
        // arguments follow the return address.
        mov eax, esp
        push dword ptr [eax + 20h]
        push dword ptr [eax + 1Ch]
        push dword ptr [eax + 18h]
        push dword ptr [eax + 14h]
        push dword ptr [eax + 10h]
        push dword ptr [eax + 0Ch]
        push dword ptr [eax + 08h]
        push dword ptr [eax + 04h]
        push esi
        push ecx
        call StudioTextureLoadScoped
        add esp, 28h
        ret
    }
}

void RefreshTexImageHook()
{
    if (!g_installed || !g_hwBase)
        return;
    auto** slot = reinterpret_cast<void**>(g_hwBase + kQglTexImage2DRva);
    if (*slot == reinterpret_cast<void*>(&TexImage2D_Hook))
        return;

    HMODULE gl = GetModuleHandleA("opengl32.dll");
    void* core = gl ? reinterpret_cast<void*>(GetProcAddress(gl, "glTexImage2D"))
                    : nullptr;
    if (*slot == reinterpret_cast<void*>(g_texImage2D) ||
        (core && *slot == core))
    {
        g_texImage2D = reinterpret_cast<GlTexImage2DFn>(*slot);
        (void)PatchPointer(slot, *slot, reinterpret_cast<void*>(&TexImage2D_Hook));
    }
}
} // namespace

bool Install(HMODULE hw, cl_enginefunc_t* engine)
{
    if (!hwbuild::MatchesTarget(hw) || !engine || !engine->pfnRegisterVariable)
    {
        rendererlog::Line(
            "studio texture: exact hw/engine unavailable, optimization disabled");
        return false;
    }

    g_hwBase = reinterpret_cast<std::uint8_t*>(hw);
    g_originalStudioTextureLoad = g_hwBase + kStudioTextureLoadRva;

    auto** texImageSlot =
        reinterpret_cast<void**>(g_hwBase + kQglTexImage2DRva);
    if (!*texImageSlot)
    {
        rendererlog::Line("studio texture: qglTexImage2D not ready, disabled");
        return false;
    }
    g_texImage2D = reinterpret_cast<GlTexImage2DFn>(*texImageSlot);

    HMODULE gl = GetModuleHandleA("opengl32.dll");
    if (gl)
    {
        g_getString = reinterpret_cast<GlGetStringFn>(
            GetProcAddress(gl, "glGetString"));
        g_getIntegerv = reinterpret_cast<GlGetIntegervFn>(
            GetProcAddress(gl, "glGetIntegerv"));
        g_getTexLevelParameteriv = reinterpret_cast<GlGetTexLevelParameterivFn>(
            GetProcAddress(gl, "glGetTexLevelParameteriv"));
        g_wglGetProcAddress = reinterpret_cast<WglGetProcAddressFn>(
            GetProcAddress(gl, "wglGetProcAddress"));
    }

    __try
    {
        g_forceMips = engine->pfnRegisterVariable(
            "r_studio_texmips", "1", 0);
        g_compress = engine->pfnRegisterVariable(
            "r_studio_texcompress", "2", 0);
        g_cache = engine->pfnRegisterVariable(
            "r_studio_texcache", "1", 0);
        g_minSize = engine->pfnRegisterVariable(
            "r_studio_texcompress_min", "64", 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_forceMips = g_compress = g_cache = g_minSize = nullptr;
    }
    if (!g_forceMips || !g_compress || !g_cache || !g_minSize)
        return false;

    if (!PatchPointer(texImageSlot, reinterpret_cast<void*>(g_texImage2D),
                      reinterpret_cast<void*>(&TexImage2D_Hook)))
    {
        rendererlog::Line("studio texture: qglTexImage2D hook failed, disabled");
        return false;
    }
    if (!PatchRelativeCall(g_hwBase + kStudioTextureLoadCallRva,
                           g_originalStudioTextureLoad,
                           reinterpret_cast<void*>(&StudioTextureLoadCallsiteHook)))
    {
        (void)PatchPointer(
            texImageSlot, reinterpret_cast<void*>(&TexImage2D_Hook),
            reinterpret_cast<void*>(g_texImage2D));
        g_uploadScope = nullptr;
        g_hwBase = nullptr;
        rendererlog::Line(
            "studio texture: Studio loader callsite signature mismatch, scoped optimization disabled");
        return false;
    }

    g_installed = true;
    RefreshCapabilities();
    rendererlog::Line(
        "studio texture: scoped loader ready (defaults mips=1, compression=2, cache=1)");
    return true;
}

void UpdateFrame()
{
    if (!g_installed || !g_hwBase)
        return;
    RefreshTexImageHook();
    RefreshCapabilities();

    static std::uint64_t lastLogged = 0;
    if (g_uploadCalls - lastLogged >= 4096u)
    {
        lastLogged = g_uploadCalls;
        rendererlog::Line(
            "studio texture: uploads=%llu compressed=%llu cache_hit=%llu cache_miss=%llu forced_mips=%llu cache_pbo_reject=%llu cache_pack_reject=%llu",
            static_cast<unsigned long long>(g_uploadCalls),
            static_cast<unsigned long long>(g_compressedUploads),
            static_cast<unsigned long long>(g_cacheHits),
            static_cast<unsigned long long>(g_cacheMisses),
            static_cast<unsigned long long>(g_forcedMipLoads),
            static_cast<unsigned long long>(g_cachePboRejects),
            static_cast<unsigned long long>(g_cachePackingRejects));
    }
}
} // namespace studio_textureopt
