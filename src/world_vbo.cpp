#include "world_vbo.h"
#include "hw_build.h"
#include "inline_hook.h"
#include "log.h"
#include "perf_control.h"
#include "profile.h"
#include "studio_drawbatch.h"

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

namespace worldvbo
{
namespace
{
// Exact GoldClient RVAs for the inspected hw.dll build.
constexpr std::uintptr_t kDrawTextureChainsRva = 0x000752B0u;
constexpr std::uintptr_t kDrawSequentialPolyRva = 0x00073C50u;
constexpr std::uintptr_t kDrawSequentialCall1Rva = 0x00075B17u;
constexpr std::uintptr_t kDrawSequentialCall2Rva = 0x00075ED7u;
constexpr std::uintptr_t kRenderBrushPolyRva   = 0x00074FF0u;
constexpr std::uintptr_t kRenderBrushCallRva   = 0x00075387u;
constexpr std::uintptr_t kNewMapRva            = 0x00073080u;
constexpr std::uintptr_t kContextDestroyRva    = 0x000A26F0u;
constexpr std::uintptr_t kGlBindRva            = 0x00064D40u;
constexpr std::uintptr_t kSequentialResumeRva  = 0x00074103u;

constexpr std::uintptr_t kWorldModelRva        = 0x02F90E20u;
constexpr std::uintptr_t kCurrentEntityRva     = 0x0078F470u;
constexpr std::uintptr_t kFrameCountRva        = 0x0078F474u;
constexpr std::uintptr_t kLightStyleValuesRva  = 0x0078F478u;
constexpr std::uintptr_t kBrushPolysRva        = 0x0078F954u;
constexpr std::uintptr_t kLightmapPolysRva     = 0x027DD878u;
constexpr std::uintptr_t kTextureSortModeRva   = 0x0027268Cu;
constexpr std::uintptr_t kLightmapOverrideRva  = 0x00790630u;
constexpr std::uintptr_t kSequentialDebugDrawRva = 0x0333A3C8u;

constexpr std::uintptr_t kQglBeginRva               = 0x027E3DE0u;
constexpr std::uintptr_t kQglEndRva                 = 0x027E3DD8u;
constexpr std::uintptr_t kQglTexCoord2fRva          = 0x027E3778u;
constexpr std::uintptr_t kQglMTexCoord2fRva         = 0x027E3788u;
constexpr std::uintptr_t kQglVertex3fvRva           = 0x027E37D0u;
constexpr std::uintptr_t kQglBindBufferRva          = 0x027E3DB8u;
constexpr std::uintptr_t kQglDeleteBuffersRva       = 0x027E3DC8u;
constexpr std::uintptr_t kQglGenBuffersRva          = 0x027E3DCCu;
constexpr std::uintptr_t kQglBufferDataRva          = 0x027E3DB4u;
constexpr std::uintptr_t kQglDrawArraysRva          = 0x027E3CECu;
constexpr std::uintptr_t kQglEnableClientStateRva   = 0x027E3CD4u;
constexpr std::uintptr_t kQglDisableClientStateRva  = 0x027E3CF0u;
constexpr std::uintptr_t kQglVertexPointerRva       = 0x027E3958u;
constexpr std::uintptr_t kQglTexCoordPointerRva     = 0x027E39E4u;
constexpr std::uintptr_t kQglPushClientAttribRva    = 0x027E3B04u;
constexpr std::uintptr_t kQglPopClientAttribRva     = 0x027E3B14u;
constexpr std::uintptr_t kQglGetIntegervRva         = 0x027E3DD0u;
constexpr std::uintptr_t kQglIsEnabledRva           = 0x027E3DF4u;
constexpr std::uintptr_t kQglClientActiveTextureRva = 0x027E386Cu;
constexpr std::uintptr_t kQglTexSubImage2DRva       = 0x027E375Cu;
constexpr std::uintptr_t kSdlGetProcAddressIatRva   = 0x001FA330u;

constexpr unsigned GL_POLYGON                 = 0x0009u;
constexpr unsigned GL_TRIANGLE_FAN            = 0x0006u;
constexpr unsigned GL_TRIANGLES               = 0x0004u;
constexpr unsigned GL_FLOAT                   = 0x1406u;
constexpr unsigned GL_UNSIGNED_SHORT          = 0x1403u;
constexpr unsigned GL_UNSIGNED_INT            = 0x1405u;
constexpr unsigned GL_VERTEX_ARRAY            = 0x8074u;
constexpr unsigned GL_NORMAL_ARRAY            = 0x8075u;
constexpr unsigned GL_COLOR_ARRAY             = 0x8076u;
constexpr unsigned GL_INDEX_ARRAY             = 0x8077u;
constexpr unsigned GL_TEXTURE_COORD_ARRAY     = 0x8078u;
constexpr unsigned GL_EDGE_FLAG_ARRAY         = 0x8079u;
constexpr unsigned GL_FOG_COORDINATE_ARRAY    = 0x8457u;
constexpr unsigned GL_SECONDARY_COLOR_ARRAY   = 0x845Eu;
constexpr unsigned GL_TEXTURE0                = 0x84C0u;
constexpr unsigned GL_CLIENT_ACTIVE_TEXTURE   = 0x84E1u;
constexpr unsigned GL_MAX_TEXTURE_UNITS       = 0x84E2u;
constexpr unsigned GL_ARRAY_BUFFER            = 0x8892u;
constexpr unsigned GL_ELEMENT_ARRAY_BUFFER    = 0x8893u;
constexpr unsigned GL_ARRAY_BUFFER_BINDING    = 0x8894u;
constexpr unsigned GL_ELEMENT_ARRAY_BUFFER_BINDING = 0x8895u;
constexpr unsigned GL_STATIC_DRAW             = 0x88E4u;
constexpr unsigned GL_CLIENT_VERTEX_ARRAY_BIT = 0x00000002u;
constexpr unsigned GL_POLYGON_MODE            = 0x0B40u;
constexpr unsigned GL_POLYGON_SMOOTH          = 0x0B41u;
constexpr unsigned GL_LIGHTING                = 0x0B50u;
constexpr int GL_FILL                         = 0x1B02;

constexpr int kSurfaceSize = 0x84;
constexpr int kVertexStride = 7 * sizeof(float);
constexpr int kMaxLightmaps = 64;
constexpr unsigned kAllowedSurfaceFlags = 0x00000802u; // PLANEBACK + proven harmless 0x800

using DrawTextureChainsFn = void (__cdecl*)();
using RenderBrushPolyFn = void (__fastcall*)(std::uint8_t* surface, void* ignored);
using NewMapFn = void (__cdecl*)();
using ContextDestroyFn = bool (__fastcall*)(void* self, void* ignored);
using GlBindFn = void (__fastcall*)(int textureUnit, int textureId);

using GlBeginFn = void (WINAPI*)(unsigned mode);
using GlEndFn = void (WINAPI*)();
using GlTexCoord2fFn = void (WINAPI*)(float s, float t);
using GlMTexCoord2fFn = void (WINAPI*)(unsigned texture, float s, float t);
using GlVertex3fvFn = void (WINAPI*)(const float* vertex);
using GlBindBufferFn = void (WINAPI*)(unsigned target, unsigned buffer);
using GlDeleteBuffersFn = void (WINAPI*)(int count, const unsigned* buffers);
using GlGenBuffersFn = void (WINAPI*)(int count, unsigned* buffers);
using GlBufferDataFn = void (WINAPI*)(unsigned target, std::ptrdiff_t size,
                                      const void* data, unsigned usage);
using GlDrawArraysFn = void (WINAPI*)(unsigned mode, int first, int count);
using GlMultiDrawArraysFn = void (WINAPI*)(unsigned mode, const int* first,
                                           const int* count, int primcount);
using GlDrawElementsFn = void (WINAPI*)(unsigned mode, int count, unsigned type,
                                        const void* indices);
using GlMultiDrawElementsFn = void (WINAPI*)(unsigned mode, const int* count,
                                             unsigned type, const void* const* indices,
                                             int primcount);
using GlEnableClientStateFn = void (WINAPI*)(unsigned array);
using GlDisableClientStateFn = void (WINAPI*)(unsigned array);
using GlVertexPointerFn = void (WINAPI*)(int size, unsigned type, int stride,
                                        const void* pointer);
using GlTexCoordPointerFn = void (WINAPI*)(int size, unsigned type, int stride,
                                          const void* pointer);
using GlPushClientAttribFn = void (WINAPI*)(unsigned mask);
using GlPopClientAttribFn = void (WINAPI*)();
using GlGetIntegervFn = void (WINAPI*)(unsigned pname, int* params);
using GlIsEnabledFn = unsigned char (WINAPI*)(unsigned cap);
using GlClientActiveTextureFn = void (WINAPI*)(unsigned texture);
using GlTexSubImage2DFn = void (WINAPI*)(unsigned target, int level,
                                        int xoffset, int yoffset,
                                        int width, int height,
                                        unsigned format, unsigned type,
                                        const void* pixels);
using SdlGetProcAddressFn = void* (__cdecl*)(const char* name);

struct SurfaceInfo
{
    int first = -1;
    int count = 0;
    int indexFirst = -1;
    int indexCount = 0;
    std::uint8_t* poly = nullptr;
    std::uint32_t batchedGeneration = 0;
    std::uint32_t legacyGeneration = 0;
};

std::uint8_t* g_hwBase = nullptr;
cl_enginefunc_t* g_engine = nullptr;
cvar_t* g_cvar = nullptr;
cvar_t* g_brushCvar = nullptr;
bool g_enabled = true;
bool g_brushEnabled = false;
int g_mode = 0;
bool g_installed = false;
bool g_passReady = false;
bool g_dirty = true;
bool g_contextLost = false;
bool g_collectStats = false;

DrawTextureChainsFn g_drawTextureChains = nullptr;
RenderBrushPolyFn g_renderBrushPoly = nullptr;
NewMapFn g_newMap = nullptr;
ContextDestroyFn g_contextDestroy = nullptr;
GlBindFn g_glBind = nullptr;

GlBeginFn g_begin = nullptr;
GlEndFn g_end = nullptr;
GlTexCoord2fFn g_texCoord2f = nullptr;
GlMTexCoord2fFn g_mtexCoord2f = nullptr;
GlVertex3fvFn g_vertex3fv = nullptr;
GlBindBufferFn g_bindBuffer = nullptr;
GlDeleteBuffersFn g_deleteBuffers = nullptr;
GlGenBuffersFn g_genBuffers = nullptr;
GlBufferDataFn g_bufferData = nullptr;
GlDrawArraysFn g_drawArrays = nullptr;
GlMultiDrawArraysFn g_multiDrawArrays = nullptr;
GlDrawElementsFn g_drawElements = nullptr;
GlMultiDrawElementsFn g_multiDrawElements = nullptr;
GlEnableClientStateFn g_enableClientState = nullptr;
GlDisableClientStateFn g_disableClientState = nullptr;
GlVertexPointerFn g_vertexPointer = nullptr;
GlTexCoordPointerFn g_texCoordPointer = nullptr;
GlPushClientAttribFn g_pushClientAttrib = nullptr;
GlPopClientAttribFn g_popClientAttrib = nullptr;
GlGetIntegervFn g_getIntegerv = nullptr;
GlIsEnabledFn g_isEnabled = nullptr;
GlClientActiveTextureFn g_clientActiveTexture = nullptr;
GlTexSubImage2DFn g_texSubImage2D = nullptr;

unsigned g_vbo = 0;
unsigned g_ibo = 0;
std::uint8_t* g_world = nullptr;
std::uint8_t* g_surfaces = nullptr;
int g_numSurfaces = 0;
std::vector<SurfaceInfo> g_surfaceInfo;
std::vector<float> g_vertexData;
std::vector<std::uint32_t> g_indexData;
std::vector<std::uint16_t> g_indexData16;
bool g_index16 = false;
std::vector<std::uint8_t*> g_chainSurfaces;
std::vector<int> g_chainFirsts;
std::vector<int> g_chainCounts;
std::vector<int> g_chainIndexCounts;
std::vector<const void*> g_chainIndexOffsets;
std::vector<int> g_sequentialRunFirsts;
std::vector<int> g_sequentialRunCounts;
std::uint32_t g_passGeneration = 1;
std::uint32_t g_mapGeneration = 0;
std::uint32_t g_builtMapGeneration = 0;
std::uint32_t g_contextGeneration = 1;
ProfileStats g_stats{};

volatile LONG g_sequentialSuppress = 0;
std::uint8_t* g_sequentialSurface = nullptr;
int g_sequentialFirst = -1;
int g_sequentialCount = 0;
bool g_sequentialDetailActive = false;
void* g_sequentialBeginReturn = nullptr;
void* g_sequentialResume = nullptr;
void* g_sequentialResumeEdi = nullptr;
void* g_drawSequentialPoly = nullptr;
std::uint8_t* g_expectedSequentialSurface = nullptr;
int g_sequentialRunTextureId = 0;
int g_sequentialRunLightmap = -1;
bool g_sequentialCallsitesReady = false;
bool g_sequentialUploadBarrierReady = false;
bool g_worldScopeReady = false;
bool g_brushScopeReady = false;
bool g_studioBeginEndPairReady = false;
int g_worldPreviousArrayBuffer = 0;
int g_worldPreviousClientTexture = static_cast<int>(GL_TEXTURE0);
int g_brushPreviousArrayBuffer = 0;
int g_brushPreviousClientTexture = static_cast<int>(GL_TEXTURE0);

template <typename T>
T Read(std::uint8_t* base, std::size_t offset)
{
    return *reinterpret_cast<T*>(base + offset);
}

template <typename Fn>
Fn ReadQgl(std::uintptr_t rva)
{
    if (!g_hwBase) return nullptr;
    return *reinterpret_cast<Fn*>(g_hwBase + rva);
}

bool PatchCall(std::uint8_t* call, void* expectedTarget, void* replacement)
{
    if (!call || call[0] != 0xE8)
        return false;
    const std::int32_t oldRel = *reinterpret_cast<std::int32_t*>(call + 1);
    void* target = call + 5 + oldRel;
    if (target != expectedTarget)
        return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    *reinterpret_cast<std::int32_t*>(call + 1) =
        static_cast<std::int32_t>(reinterpret_cast<std::uint8_t*>(replacement) - (call + 5));
    DWORD ignored = 0;
    VirtualProtect(call, 5, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    return true;
}

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

void ForgetGpuBuffer(bool deleteIfPossible)
{
    if (deleteIfPossible && g_deleteBuffers && !g_contextLost)
    {
        unsigned ids[2]{};
        int count = 0;
        if (g_vbo) ids[count++] = g_vbo;
        if (g_ibo) ids[count++] = g_ibo;
        if (count)
            g_deleteBuffers(count, ids);
    }
    g_vbo = 0;
    g_ibo = 0;
    g_world = nullptr;
    g_surfaces = nullptr;
    g_numSurfaces = 0;
    g_builtMapGeneration = 0;
    g_surfaceInfo.clear();
    g_vertexData.clear();
    g_indexData.clear();
    g_indexData16.clear();
    g_index16 = false;
    g_sequentialRunFirsts.clear();
    g_sequentialRunCounts.clear();
    g_sequentialRunTextureId = 0;
    g_sequentialRunLightmap = -1;
    g_expectedSequentialSurface = nullptr;
    g_sequentialDetailActive = false;
    g_dirty = true;
}

bool RefreshGlFunctions()
{
    g_bindBuffer = ReadQgl<GlBindBufferFn>(kQglBindBufferRva);
    g_deleteBuffers = ReadQgl<GlDeleteBuffersFn>(kQglDeleteBuffersRva);
    g_genBuffers = ReadQgl<GlGenBuffersFn>(kQglGenBuffersRva);
    g_bufferData = ReadQgl<GlBufferDataFn>(kQglBufferDataRva);
    g_drawArrays = ReadQgl<GlDrawArraysFn>(kQglDrawArraysRva);
    g_enableClientState = ReadQgl<GlEnableClientStateFn>(kQglEnableClientStateRva);
    g_disableClientState = ReadQgl<GlDisableClientStateFn>(kQglDisableClientStateRva);
    g_vertexPointer = ReadQgl<GlVertexPointerFn>(kQglVertexPointerRva);
    g_texCoordPointer = ReadQgl<GlTexCoordPointerFn>(kQglTexCoordPointerRva);
    g_pushClientAttrib = ReadQgl<GlPushClientAttribFn>(kQglPushClientAttribRva);
    g_popClientAttrib = ReadQgl<GlPopClientAttribFn>(kQglPopClientAttribRva);
    g_getIntegerv = ReadQgl<GlGetIntegervFn>(kQglGetIntegervRva);
    g_isEnabled = ReadQgl<GlIsEnabledFn>(kQglIsEnabledRva);
    g_clientActiveTexture = ReadQgl<GlClientActiveTextureFn>(kQglClientActiveTextureRva);

    g_multiDrawArrays = nullptr;
    g_drawElements = nullptr;
    g_multiDrawElements = nullptr;
    auto getProc = *reinterpret_cast<SdlGetProcAddressFn*>(g_hwBase + kSdlGetProcAddressIatRva);
    if (getProc)
    {
        g_multiDrawArrays = reinterpret_cast<GlMultiDrawArraysFn>(getProc("glMultiDrawArrays"));
        if (!g_multiDrawArrays)
            g_multiDrawArrays = reinterpret_cast<GlMultiDrawArraysFn>(getProc("glMultiDrawArraysEXT"));
        g_drawElements = reinterpret_cast<GlDrawElementsFn>(getProc("glDrawElements"));
        g_multiDrawElements =
            reinterpret_cast<GlMultiDrawElementsFn>(getProc("glMultiDrawElements"));
        if (!g_multiDrawElements)
            g_multiDrawElements =
                reinterpret_cast<GlMultiDrawElementsFn>(getProc("glMultiDrawElementsEXT"));
    }

    return g_bindBuffer && g_deleteBuffers && g_genBuffers && g_bufferData &&
           g_drawArrays && g_drawElements && g_enableClientState && g_vertexPointer &&
           g_texCoordPointer && g_pushClientAttrib && g_popClientAttrib &&
           g_getIntegerv && g_isEnabled && g_clientActiveTexture;
}

unsigned WorldIndexType()
{
    return g_index16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
}

std::size_t WorldIndexBytes()
{
    return g_index16 ? sizeof(std::uint16_t) : sizeof(std::uint32_t);
}

bool ForeignClientArraysClear(int ownedTexcoordUnits)
{
    if (!g_isEnabled || !g_getIntegerv || !g_clientActiveTexture)
        return false;

    int previousClientTexture = static_cast<int>(GL_TEXTURE0);
    bool clientTextureKnown = false;
    __try
    {
        // Gold's immediate mode ignores all of these.  Retained DrawArrays /
        // DrawElements would consume them, so fail open instead of inheriting
        // foreign/plugin client-array state.
        if (g_isEnabled(GL_NORMAL_ARRAY) ||
            g_isEnabled(GL_COLOR_ARRAY) ||
            g_isEnabled(GL_INDEX_ARRAY) ||
            g_isEnabled(GL_EDGE_FLAG_ARRAY) ||
            g_isEnabled(GL_FOG_COORDINATE_ARRAY) ||
            g_isEnabled(GL_SECONDARY_COLOR_ARRAY))
            return false;

        int maxTextureUnits = 0;
        g_getIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &previousClientTexture);
        clientTextureKnown = true;
        g_getIntegerv(GL_MAX_TEXTURE_UNITS, &maxTextureUnits);
        if (maxTextureUnits < 1 || maxTextureUnits > 32)
            return false;

        if (ownedTexcoordUnits < 1 ||
            ownedTexcoordUnits > maxTextureUnits)
            return false;

        // Every texcoord unit not explicitly configured by this retained draw
        // is foreign state and would participate in DrawArrays/DrawElements.
        for (int unit = ownedTexcoordUnits;
             unit < maxTextureUnits; ++unit)
        {
            g_clientActiveTexture(
                GL_TEXTURE0 + static_cast<unsigned>(unit));
            if (g_isEnabled(GL_TEXTURE_COORD_ARRAY))
            {
                g_clientActiveTexture(
                    static_cast<unsigned>(previousClientTexture));
                return false;
            }
        }
        g_clientActiveTexture(
            static_cast<unsigned>(previousClientTexture));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (clientTextureKnown)
        {
            __try
            {
                g_clientActiveTexture(
                    static_cast<unsigned>(previousClientTexture));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        return false;
    }
}

bool PolygonFillMode()
{
    if (!g_getIntegerv || !g_isEnabled)
        return false;
    int modes[2]{};
    __try
    {
        g_getIntegerv(GL_POLYGON_MODE, modes);
        // GL_POLYGON and GL_TRIANGLE_FAN differ in provoking-vertex and
        // antialiased-edge semantics.  Gold's ordinary world path has lighting
        // and polygon smoothing disabled, fail open if a plugin/debug mode has
        // enabled either instead of exposing fan-internal triangle semantics.
        if (g_isEnabled(GL_LIGHTING) ||
            g_isEnabled(GL_POLYGON_SMOOTH))
            return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return modes[0] == GL_FILL && modes[1] == GL_FILL;
}

int SurfaceIndex(std::uint8_t* surface)
{
    if (!surface || !g_surfaces || g_numSurfaces <= 0)
        return -1;
    const std::ptrdiff_t delta = surface - g_surfaces;
    if (delta < 0 || (delta % kSurfaceSize) != 0)
        return -1;
    const int index = static_cast<int>(delta / kSurfaceSize);
    return index >= 0 && index < g_numSurfaces ? index : -1;
}

std::uint8_t* SurfaceTexture(std::uint8_t* surface)
{
    auto* texinfo = Read<std::uint8_t*>(surface, 0x2C);
    if (!texinfo || Read<int>(texinfo, 0x28) != 0)
        return nullptr;
    return Read<std::uint8_t*>(texinfo, 0x24);
}

bool SurfaceEligible(std::uint8_t* surface, const SurfaceInfo& info,
                     std::uint8_t*& textureOut)
{
    if (!surface || info.first < 0 || info.count < 3 || !info.poly)
        return false;

    const unsigned flags = Read<unsigned>(surface, 0x08);
    if ((flags & ~kAllowedSurfaceFlags) != 0)
        return false;
    if (Read<std::uint8_t*>(surface, 0x58) != nullptr) // decals
        return false;
    if (Read<std::uint8_t*>(surface, 0x24) != info.poly)
        return false;
    if (Read<std::uint8_t*>(info.poly, 0x00) != nullptr) // multi-poly/water-style surface
        return false;
    if (Read<int>(info.poly, 0x08) != info.count)
        return false;

    auto* currentEntity = *reinterpret_cast<std::uint8_t**>(g_hwBase + kCurrentEntityRva);
    if (!currentEntity || Read<int>(currentEntity, 0x2F8) != 0)
        return false;

    auto* texture = SurfaceTexture(surface);
    if (!texture)
        return false;
    if (Read<int>(texture, 0x24) != 0 ||
        Read<std::uint8_t*>(texture, 0x30) != nullptr ||
        Read<std::uint8_t*>(texture, 0x34) != nullptr ||
        Read<std::uintptr_t>(texture, 0x50) != 0)
        return false;

    const int rFrameCount = *reinterpret_cast<int*>(g_hwBase + kFrameCountRva);
    if (Read<int>(surface, 0x30) == rFrameCount || Read<int>(surface, 0x50) != 0)
        return false;

    const auto* styles = surface + 0x3C;
    for (int map = 0; map < 4; ++map)
    {
        const unsigned style = styles[map];
        if (style == 255)
            break;
        const int live = *reinterpret_cast<int*>(g_hwBase + kLightStyleValuesRva + style * 4u);
        const int cached = Read<int>(surface, 0x40 + map * 4);
        if (live != cached)
            return false;
    }

    const int lightmap = Read<int>(surface, 0x38);
    if (lightmap < 0 || lightmap >= kMaxLightmaps)
        return false;

    textureOut = texture;
    return true;
}

// R_DrawSequentialPoly has already performed all dynamic-lightmap work and
// decal bookkeeping that precedes its qglBegin. The direct sequential VBO
// replacement therefore needs only immutable glpoly geometry plus material
// cases whose vertex stream is exactly the ordinary TMU0/TMU1 layout. Keep
// animated/detail/scroll/special surfaces as barriers until their extra state
// is represented explicitly.
bool SequentialSurfaceEligible(std::uint8_t* surface, const SurfaceInfo& info,
                               bool detailActive)
{
    if (!surface || info.first < 0 || info.count < 3 || !info.poly)
        return false;

    const unsigned flags = Read<unsigned>(surface, 0x08);
    if ((flags & ~kAllowedSurfaceFlags) != 0 || (flags & 0x200u) != 0)
        return false;
    if (Read<std::uint8_t*>(surface, 0x24) != info.poly ||
        Read<std::uint8_t*>(info.poly, 0x00) != nullptr ||
        Read<int>(info.poly, 0x08) != info.count)
        return false;

    auto* currentEntity = *reinterpret_cast<std::uint8_t**>(g_hwBase + kCurrentEntityRva);
    if (!currentEntity || Read<int>(currentEntity, 0x2F8) != 0)
        return false;

    auto* texture = SurfaceTexture(surface);
    if (!texture)
        return false;
    // Animation/alternate textures still use caller-selected material state
    // that is not represented by this exact-point retained path. Detail state
    // is different: Gold has already selected/bound it before qglBegin and the
    // saved helper result tells us whether the vertex loop emits TMU2 coords.
    if (Read<int>(texture, 0x24) != 0 ||
        Read<std::uint8_t*>(texture, 0x30) != nullptr ||
        Read<std::uint8_t*>(texture, 0x34) != nullptr)
        return false;
    if (detailActive && Read<std::uintptr_t>(texture, 0x50) == 0)
        return false;

    const int lightmap = Read<int>(surface, 0x38);
    return lightmap >= 0 && lightmap < kMaxLightmaps;
}

bool SequentialRunBatchReady()
{
    // Do not defer sequential world polygons past their exact qglBegin point.
    // Gold performs observable post-End work (decals/render-state cleanup and
    // dynamic-light related transitions) before the next surface.  A run drawn
    // later can therefore inherit a different server texture/lightmap state and
    // produce white/incorrectly lit BSP polygons.  Keep the retained VBO path,
    // but submit each eligible surface immediately from PrepareSequentialBegin.
    //
    // Re-enable ordered runs only after the complete server-side texture state
    // (active TMU, enables/env/combine, bindings and post-End barriers) is part
    // of the recorded command key.
    return false;
}

void ClearSequentialRun()
{
    g_sequentialRunFirsts.clear();
    g_sequentialRunCounts.clear();
    g_sequentialRunTextureId = 0;
    g_sequentialRunLightmap = -1;
}

void FlushSequentialRun(bool barrier)
{
    if (g_sequentialRunFirsts.empty())
        return;

    const std::size_t count = g_sequentialRunFirsts.size();
    if (count == 1)
    {
        g_drawArrays(GL_POLYGON, g_sequentialRunFirsts[0],
                     g_sequentialRunCounts[0]);
        if (g_collectStats)
            ++g_stats.drawArraysCalls;
    }
    else
    {
        g_multiDrawArrays(GL_POLYGON, g_sequentialRunFirsts.data(),
                          g_sequentialRunCounts.data(),
                          static_cast<int>(count));
        if (g_collectStats)
            ++g_stats.multiDrawCalls;
    }

    if (g_collectStats)
    {
        ++g_stats.sequentialRuns;
        if (barrier)
            ++g_stats.sequentialBarriers;
    }
    ClearSequentialRun();
}

bool SequentialRunCandidate(std::uint8_t* surface, int& textureIdOut,
                            int& lightmapOut)
{
    // Exact hw.dll draws a per-surface debug/wireframe overlay after qglEnd
    // when this mode is active. Deferring the base polygon would reverse that
    // visible order, so keep the whole surface stock/immediate.
    if (*reinterpret_cast<int*>(g_hwBase + kSequentialDebugDrawRva) != 0)
        return false;

    const int index = SurfaceIndex(surface);
    if (index < 0)
        return false;
    const SurfaceInfo& info = g_surfaceInfo[static_cast<std::size_t>(index)];
    if (!SequentialSurfaceEligible(surface, info, false))
        return false;
    if (Read<std::uint8_t*>(surface, 0x58) != nullptr) // post-End decal work
        return false;
    if (*reinterpret_cast<int*>(g_hwBase + kLightmapOverrideRva) != 0)
        return false;

    std::uint8_t* texture = SurfaceTexture(surface);
    if (!texture)
        return false;
    // Ordered runs deliberately remain disabled. Keep their latent candidate
    // path conservative too: it has no exact qglBegin stack-local detail flag.
    if (Read<std::uintptr_t>(texture, 0x50) != 0)
        return false;
    const int textureId = Read<int>(texture, 0x1C);
    const int lightmap = Read<int>(surface, 0x38);
    if (textureId <= 0 || lightmap < 0 || lightmap >= kMaxLightmaps)
        return false;

    textureIdOut = textureId;
    lightmapOut = lightmap;
    return true;
}

int g_expectedSequentialTextureId = 0;
int g_expectedSequentialLightmap = -1;

void __cdecl BeforeSequentialCall(std::uint8_t* surface, int /*face*/,
                                  int callerFlag)
{
    g_expectedSequentialSurface = nullptr;
    g_expectedSequentialTextureId = 0;
    g_expectedSequentialLightmap = -1;

    // Exact R_DrawSequentialPoly consumes its caller-owned stack DWORD after
    // qglEnd. Nonzero can disable multitexture/select TMU0, so deferring that
    // surface would make the pending geometry execute under different state.
    if (callerFlag != 0 || !SequentialRunBatchReady())
    {
        FlushSequentialRun(true);
        return;
    }

    int textureId = 0;
    int lightmap = -1;
    if (!SequentialRunCandidate(surface, textureId, lightmap))
    {
        FlushSequentialRun(true);
        return;
    }

    if (!g_sequentialRunFirsts.empty() &&
        (textureId != g_sequentialRunTextureId ||
         lightmap != g_sequentialRunLightmap))
    {
        FlushSequentialRun(true);
    }

    g_expectedSequentialSurface = surface;
    g_expectedSequentialTextureId = textureId;
    g_expectedSequentialLightmap = lightmap;
}

void __declspec(naked) SequentialCall_Hook()
{
    __asm
    {
        // Exact hw.dll ABI: ECX=msurface_t*, EDX=face, plus one caller-owned
        // stack argument. Preserve the original stack verbatim and tail-jump
        // so the callee still observes that third argument at [ebp+8].
        pushfd
        pushad
        mov eax, dword ptr [esp + 40]
        push eax
        push edx
        push ecx
        call BeforeSequentialCall
        add esp, 12
        popad
        popfd
        jmp dword ptr [g_drawSequentialPoly]
    }
}

bool BuildCache()
{
    if (!RefreshGlFunctions())
        return false;

    auto* world = *reinterpret_cast<std::uint8_t**>(g_hwBase + kWorldModelRva);
    if (!world)
        return false;
    const int numSurfaces = Read<int>(world, 0xB0);
    auto* surfaces = Read<std::uint8_t*>(world, 0xB4);
    if (!surfaces || numSurfaces <= 0 || numSurfaces > 65536)
        return false;

    if (g_vbo)
        ForgetGpuBuffer(true);

    try
    {
        g_surfaceInfo.assign(static_cast<std::size_t>(numSurfaces), SurfaceInfo{});
        g_vertexData.clear();
        g_indexData.clear();
        g_indexData16.clear();
        g_index16 = false;
        g_chainSurfaces.clear();
        g_chainFirsts.clear();
        g_chainCounts.clear();
        g_chainIndexCounts.clear();
        g_chainIndexOffsets.clear();
        g_sequentialRunFirsts.clear();
        g_sequentialRunCounts.clear();
        g_chainSurfaces.reserve(static_cast<std::size_t>(numSurfaces));
        g_chainFirsts.reserve(static_cast<std::size_t>(numSurfaces));
        g_chainCounts.reserve(static_cast<std::size_t>(numSurfaces));
        g_chainIndexCounts.reserve(static_cast<std::size_t>(numSurfaces));
        g_chainIndexOffsets.reserve(static_cast<std::size_t>(numSurfaces));
        g_sequentialRunFirsts.reserve(static_cast<std::size_t>(numSurfaces));
        g_sequentialRunCounts.reserve(static_cast<std::size_t>(numSurfaces));

        for (int i = 0; i < numSurfaces; ++i)
        {
            auto* surface = surfaces + i * kSurfaceSize;
            auto* poly = Read<std::uint8_t*>(surface, 0x24);
            if (!poly)
                continue;
            const int count = Read<int>(poly, 0x08);
            if (count < 3 || count > 4096)
                continue;

            const std::size_t firstVertex = g_vertexData.size() / 7u;
            const std::size_t triIndexCount =
                static_cast<std::size_t>(count - 2) * 3u;
            if (firstVertex > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
                firstVertex + static_cast<std::size_t>(count) >
                    static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
                g_indexData.size() >
                    static_cast<std::size_t>(std::numeric_limits<int>::max()) - triIndexCount)
            {
                rendererlog::Line("worldvbo: BSP geometry exceeds indexed-cache limits, legacy path retained");
                ForgetGpuBuffer(false);
                return false;
            }

            SurfaceInfo& info = g_surfaceInfo[static_cast<std::size_t>(i)];
            info.first = static_cast<int>(firstVertex);
            info.count = count;
            info.indexFirst = static_cast<int>(g_indexData.size());
            info.indexCount = static_cast<int>(triIndexCount);
            info.poly = poly;

            const float* verts = reinterpret_cast<const float*>(poly + 0x10);
            g_vertexData.insert(g_vertexData.end(), verts, verts + count * 7);

            const std::uint32_t first = static_cast<std::uint32_t>(firstVertex);
            for (int vertex = 1; vertex < count - 1; ++vertex)
            {
                g_indexData.push_back(first);
                g_indexData.push_back(first + static_cast<std::uint32_t>(vertex));
                g_indexData.push_back(first + static_cast<std::uint32_t>(vertex + 1));
            }
        }
    }
    catch (...)
    {
        rendererlog::Line("worldvbo: CPU cache allocation failed, legacy path retained");
        ForgetGpuBuffer(false);
        return false;
    }

    if (g_vertexData.empty() || g_indexData.empty())
    {
        ForgetGpuBuffer(false);
        return false;
    }

    // One EBO covers the whole map. If the global vertex namespace fits in
    // uint16, mirror the generated fan indices once and keep SurfaceInfo's
    // indexFirst values as element offsets. Sequential DrawArrays is untouched.
    const std::size_t vertexCount = g_vertexData.size() / 7u;
    if (vertexCount <=
        static_cast<std::size_t>(
            (std::numeric_limits<std::uint16_t>::max)()) + 1u)
    {
        try
        {
            g_indexData16.reserve(g_indexData.size());
            for (std::uint32_t index : g_indexData)
            {
                if (index >
                    static_cast<std::uint32_t>(
                        (std::numeric_limits<std::uint16_t>::max)()))
                    throw 1;
                g_indexData16.push_back(
                    static_cast<std::uint16_t>(index));
            }
            g_index16 =
                g_indexData16.size() == g_indexData.size();
        }
        catch (...)
        {
            g_indexData16.clear();
            g_index16 = false;
        }
    }

    unsigned ids[2]{};
    g_genBuffers(2, ids);
    if (!ids[0] || !ids[1])
    {
        if (g_deleteBuffers)
            g_deleteBuffers(2, ids);
        ForgetGpuBuffer(false);
        return false;
    }

    int previousBuffer = 0;
    int previousIndexBuffer = 0;
    g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    g_getIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previousIndexBuffer);
    g_bindBuffer(GL_ARRAY_BUFFER, ids[0]);
    g_bufferData(GL_ARRAY_BUFFER,
                 static_cast<std::ptrdiff_t>(g_vertexData.size() * sizeof(float)),
                 g_vertexData.data(), GL_STATIC_DRAW);
    g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, ids[1]);
    g_bufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<std::ptrdiff_t>(
                     g_indexData.size() * WorldIndexBytes()),
                 g_index16
                     ? static_cast<const void*>(g_indexData16.data())
                     : static_cast<const void*>(g_indexData.data()),
                 GL_STATIC_DRAW);
    g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<unsigned>(previousIndexBuffer));
    g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previousBuffer));

    g_vbo = ids[0];
    g_ibo = ids[1];
    g_world = world;
    g_surfaces = surfaces;
    g_numSurfaces = numSurfaces;
    g_builtMapGeneration = g_mapGeneration;
    g_dirty = false;
    g_contextLost = false;

    rendererlog::Line("worldvbo: built static BSP VBO/EBO vbo=%u ebo=%u surfaces=%d vertices=%u indices=%u bytes=%u/%u index=%s multidraw_elements=%s",
               g_vbo, g_ibo, g_numSurfaces,
               static_cast<unsigned>(g_vertexData.size() / 7u),
               static_cast<unsigned>(g_indexData.size()),
               static_cast<unsigned>(g_vertexData.size() * sizeof(float)),
               static_cast<unsigned>(g_indexData.size() * WorldIndexBytes()),
               g_index16 ? "u16" : "u32",
               g_multiDrawElements ? "yes" : "no");
    return true;
}

bool EnsureCache()
{
    auto* world = *reinterpret_cast<std::uint8_t**>(g_hwBase + kWorldModelRva);
    if (!world)
        return false;
    auto* surfaces = Read<std::uint8_t*>(world, 0xB4);
    const int count = Read<int>(world, 0xB0);

    if (!g_dirty && !g_contextLost && g_vbo && g_ibo &&
        g_builtMapGeneration == g_mapGeneration &&
        world == g_world && surfaces == g_surfaces && count == g_numSurfaces)
        return true;

    if (g_contextLost)
        ForgetGpuBuffer(false);
    else if (g_vbo)
        ForgetGpuBuffer(true);
    return BuildCache();
}

bool __cdecl DrawPreparedSequential();

bool DetailClientArrayProvidersReady()
{
    if (!g_disableClientState || !g_enableClientState || !g_texCoordPointer ||
        !g_clientActiveTexture || !g_getIntegerv || !g_isEnabled ||
        !g_pushClientAttrib || !g_popClientAttrib || !g_bindBuffer ||
        !g_drawArrays)
        return false;

    __try
    {
        return ReadQgl<GlDisableClientStateFn>(kQglDisableClientStateRva) ==
                   g_disableClientState &&
               ReadQgl<GlEnableClientStateFn>(kQglEnableClientStateRva) ==
                   g_enableClientState &&
               ReadQgl<GlTexCoordPointerFn>(kQglTexCoordPointerRva) ==
                   g_texCoordPointer &&
               ReadQgl<GlClientActiveTextureFn>(kQglClientActiveTextureRva) ==
                   g_clientActiveTexture &&
               ReadQgl<GlGetIntegervFn>(kQglGetIntegervRva) ==
                   g_getIntegerv &&
               ReadQgl<GlIsEnabledFn>(kQglIsEnabledRva) == g_isEnabled &&
               ReadQgl<GlPushClientAttribFn>(kQglPushClientAttribRva) ==
                   g_pushClientAttrib &&
               ReadQgl<GlPopClientAttribFn>(kQglPopClientAttribRva) ==
                   g_popClientAttrib &&
               ReadQgl<GlBindBufferFn>(kQglBindBufferRva) == g_bindBuffer &&
               ReadQgl<GlDrawArraysFn>(kQglDrawArraysRva) == g_drawArrays;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool DrawPreparedDetailSequential()
{
    if (!DetailClientArrayProvidersReady())
        return false;

    int maxTextureUnits = 0;
    int previousClientTexture = static_cast<int>(GL_TEXTURE0);
    int previousArrayBuffer = 0;
    bool clientTextureKnown = false;
    bool arrayBufferKnown = false;
    bool pushed = false;

    __try
    {
        g_getIntegerv(GL_MAX_TEXTURE_UNITS, &maxTextureUnits);
        if (maxTextureUnits < 3 || maxTextureUnits > 32)
            return false;

        // TMU2 is borrowed only for this exact draw. Any foreign enabled array
        // on TMU2 or above would participate in DrawArrays, unlike Gold's
        // immediate loop, so fail open before changing client state.
        if (!ForeignClientArraysClear(2))
            return false;

        g_getIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &previousClientTexture);
        clientTextureKnown = true;
        if (previousClientTexture < static_cast<int>(GL_TEXTURE0) ||
            previousClientTexture >= static_cast<int>(GL_TEXTURE0) + maxTextureUnits)
            return false;
        g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
        arrayBufferKnown = true;

        g_pushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        pushed = true;

        // glTexCoordPointer captures the VBO binding. The server active texture
        // is never touched here, Gold's detail helper has already left it in the
        // exact state expected by the code after qglEnd.
        g_bindBuffer(GL_ARRAY_BUFFER, g_vbo);
        g_clientActiveTexture(GL_TEXTURE0 + 2u);
        if (g_isEnabled(GL_TEXTURE_COORD_ARRAY))
        {
            g_clientActiveTexture(static_cast<unsigned>(previousClientTexture));
            g_bindBuffer(GL_ARRAY_BUFFER,
                         static_cast<unsigned>(previousArrayBuffer));
            g_popClientAttrib();
            return false;
        }
        g_enableClientState(GL_TEXTURE_COORD_ARRAY);
        g_texCoordPointer(2, GL_FLOAT, kVertexStride,
                          reinterpret_cast<const void*>(3 * sizeof(float)));

        // Keep Gold-visible client/buffer bindings unchanged during the draw,
        // the pointer retains the VBO captured above.
        g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previousArrayBuffer));
        g_clientActiveTexture(static_cast<unsigned>(previousClientTexture));

        g_drawArrays(GL_TRIANGLE_FAN, g_sequentialFirst, g_sequentialCount);

        // Disable the borrowed array before restoring the exact client snapshot.
        g_clientActiveTexture(GL_TEXTURE0 + 2u);
        g_disableClientState(GL_TEXTURE_COORD_ARRAY);
        g_clientActiveTexture(static_cast<unsigned>(previousClientTexture));
        g_popClientAttrib();
        pushed = false;
        g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previousArrayBuffer));
        g_clientActiveTexture(static_cast<unsigned>(previousClientTexture));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (pushed)
        {
            __try
            {
                g_popClientAttrib();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        __try
        {
            if (arrayBufferKnown)
                g_bindBuffer(GL_ARRAY_BUFFER,
                             static_cast<unsigned>(previousArrayBuffer));
            if (clientTextureKnown)
                g_clientActiveTexture(
                    static_cast<unsigned>(previousClientTexture));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return false;
    }
}

void ApplyFinalSequentialTexcoords(std::uint8_t* surface,
                                   const SurfaceInfo& info,
                                   bool detailActive)
{
    if (!surface || !g_mtexCoord2f || !info.poly || info.count < 1)
        return;
    const float* last = reinterpret_cast<const float*>(info.poly + 0x10) +
                        (info.count - 1) * 7;
    g_mtexCoord2f(GL_TEXTURE0, last[3], last[4]);
    g_mtexCoord2f(GL_TEXTURE0 + 1u, last[5], last[6]);
    if (detailActive)
        g_mtexCoord2f(GL_TEXTURE0 + 2u, last[3], last[4]);
}

int __cdecl PrepareSequentialBegin(std::uint8_t* surface, void* returnAddress,
                                   unsigned mode, int detailFlag)
{
    g_sequentialSurface = nullptr;
    g_sequentialFirst = -1;
    g_sequentialCount = 0;
    g_sequentialDetailActive = false;
    g_sequentialResumeEdi = nullptr;

    const bool brushScope = g_brushScopeReady;
    if (brushScope && g_collectStats)
        ++g_stats.brushBeginCalls;

    if (!g_installed || g_mode != 1 || !g_enabled || !rendererperf::Enabled() ||
        (!g_worldScopeReady && !g_brushScopeReady) || mode != GL_POLYGON ||
        returnAddress != g_sequentialBeginReturn)
    {
        if (brushScope && g_collectStats)
            ++g_stats.brushBeginShapeRejects;
        return 0;
    }

    // Only surfaces resident in the world cache are eligible. Inline brush
    // models share that storage, unrelated/model-private surfaces fail here.
    const int index = SurfaceIndex(surface);
    if (index < 0)
    {
        if (brushScope && g_collectStats)
            ++g_stats.brushIndexRejects;
        return 0;
    }

    if (detailFlag != 0 && detailFlag != 1)
    {
        if (brushScope && g_collectStats)
            ++g_stats.brushEligibilityRejects;
        return 0;
    }

    const bool detailActive = detailFlag != 0;
    if (detailActive && g_collectStats)
        ++g_stats.detailAttempts;
    SurfaceInfo& info = g_surfaceInfo[static_cast<std::size_t>(index)];
    if (!SequentialSurfaceEligible(surface, info, detailActive))
    {
        if (detailActive && g_collectStats)
            ++g_stats.detailFallbacks;
        if (brushScope && g_collectStats)
            ++g_stats.brushEligibilityRejects;
        return 0;
    }

    // R_DrawSequentialPoly already performed R_RenderDynamicLightmaps,
    // R_TextureAnimation, texture binds and lightmap upload before this Begin.
    // We replace only the non-scrolling immediate vertex loop.
    if (!g_clientActiveTexture)
    {
        if (detailActive && g_collectStats)
            ++g_stats.detailFallbacks;
        return 0;
    }

    g_sequentialSurface = surface;
    g_sequentialFirst = info.first;
    g_sequentialCount = info.count;
    g_sequentialDetailActive = detailActive;
    g_sequentialResumeEdi = info.poly + 0x10;

    if (g_expectedSequentialSurface == surface)
    {
        // The callsite wrapper classified this surface before Gold changed any
        // material state. Gold has now completed texture/lightmap setup and any
        // dynamic upload. Defer only the geometry, an upload hook flushes older
        // geometry before TexSubImage2D can mutate a lightmap beneath it.
        if (g_sequentialRunFirsts.empty())
        {
            g_sequentialRunTextureId = g_expectedSequentialTextureId;
            g_sequentialRunLightmap = g_expectedSequentialLightmap;
        }

        if (g_sequentialRunTextureId == g_expectedSequentialTextureId &&
            g_sequentialRunLightmap == g_expectedSequentialLightmap)
        {
            g_sequentialRunFirsts.push_back(info.first);
            g_sequentialRunCounts.push_back(info.count);
            ApplyFinalSequentialTexcoords(surface, info, detailActive);
            if (g_collectStats)
            {
                ++g_stats.surfacesBatched;
                ++g_stats.sequentialDeferredSurfaces;
                g_stats.verticesBatched += static_cast<std::uint64_t>(info.count);
            }
            g_expectedSequentialSurface = nullptr;
            g_expectedSequentialTextureId = 0;
            g_expectedSequentialLightmap = -1;
            g_sequentialSurface = nullptr;
            g_sequentialFirst = -1;
            g_sequentialCount = 0;
            return 1;
        }
    }

    g_expectedSequentialSurface = nullptr;
    g_expectedSequentialTextureId = 0;
    g_expectedSequentialLightmap = -1;

    // Everything before qglBegin remains stock GoldClient: texture animation,
    // dynamic-lightmap rebuild/upload, material binds and detail setup have
    // already happened. Emit only the immutable glpoly vertex range here, then
    // resume after Gold's immediate-mode vertex loop/qglEnd.
    if (!DrawPreparedSequential())
    {
        if (g_sequentialDetailActive && g_collectStats)
            ++g_stats.detailFallbacks;
        g_sequentialSurface = nullptr;
        g_sequentialFirst = -1;
        g_sequentialCount = 0;
        g_sequentialDetailActive = false;
        g_sequentialResumeEdi = nullptr;
        return 0;
    }
    return g_sequentialResumeEdi != nullptr;
}

bool __cdecl DrawPreparedSequential()
{
    if (!g_sequentialSurface || g_sequentialFirst < 0 ||
        g_sequentialCount < 3 || !g_vbo ||
        (!g_worldScopeReady && !g_brushScopeReady))
        return false;

    // BSP glpolys are convex fans.  In the ordinary filled renderer this is
    // topologically identical to Gold's legacy GL_POLYGON while avoiding the
    // legacy primitive path.  BeginWorldScope/BeginBrushScope fail open when a
    // debug/plugin polygon mode is not GL_FILL, where edge semantics differ.
    if (g_sequentialDetailActive)
    {
        if (!DrawPreparedDetailSequential())
            return false;
    }
    else
    {
        g_drawArrays(GL_TRIANGLE_FAN, g_sequentialFirst, g_sequentialCount);
    }

    // Immediate mode leaves the final texture coordinates as persistent GL
    // current state. glDrawArrays does not, so reproduce the two observable
    // unit-0/unit-1 values from the last legacy vertex exactly.
    const int index = SurfaceIndex(g_sequentialSurface);
    if (index >= 0)
    {
        const SurfaceInfo& info = g_surfaceInfo[static_cast<std::size_t>(index)];
        ApplyFinalSequentialTexcoords(g_sequentialSurface, info,
                                      g_sequentialDetailActive);
    }

    if (g_collectStats)
    {
        ++g_stats.surfacesBatched;
        ++g_stats.drawArraysCalls;
        g_stats.verticesBatched +=
            static_cast<std::uint64_t>(g_sequentialCount);
        if (g_sequentialDetailActive)
        {
            ++g_stats.detailDraws;
            g_stats.detailVertices +=
                static_cast<std::uint64_t>(g_sequentialCount);
        }
        if (g_brushScopeReady)
        {
            ++g_stats.brushSurfaces;
            g_stats.brushVertices +=
                static_cast<std::uint64_t>(g_sequentialCount);
        }
    }

    g_sequentialSurface = nullptr;
    g_sequentialFirst = -1;
    g_sequentialCount = 0;
    g_sequentialDetailActive = false;
    return true;
}

void WINAPI End_Hook();

bool __cdecl StudioEndHookLive()
{
    if (!g_hwBase)
        return false;
    __try
    {
        auto** endSlot = reinterpret_cast<void**>(g_hwBase + kQglEndRva);
        return *endSlot == reinterpret_cast<void*>(&End_Hook);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// R_DrawSequentialPoly's hot path is immediate mode. For a proven normal world
// GL_POLYGON, draw the resident VBO range at qglBegin and rewrite the caller
// return to the instruction immediately after Gold's vertex loop/qglEnd. Setup
// before qglBegin and all cleanup/decal logic after the loop remain stock.
void __declspec(naked) WINAPI Begin_Hook(unsigned /*mode*/)
{
    __asm
    {
        cmp byte ptr [g_studioBeginEndPairReady], 0
        je skipStudio
        // We are already executing through the live Begin hook. Re-check the
        // paired End slot immediately before suppression so a third-party qgl
        // rewrite cannot leave us with a suppressed Begin and a raw End.
        call StudioEndHookLive
        test al, al
        je skipStudio
        push dword ptr [esp + 4]
        call studio_drawbatch::TryBegin
        add esp, 4
        test al, al
        jne suppressed
    skipStudio:
        mov eax, [esp]
        cmp eax, dword ptr [g_sequentialBeginReturn]
        jne original
        cmp dword ptr [esp + 4], 9
        jne original
        pushad
        mov eax, [esp + 32]
        mov ecx, [esp + 36]
        // Exact R_DrawSequentialPoly local saved at [caller ESP+1C] after
        // DetailTexture setup. qglBegin's argument+return add 8 bytes, and
        // pushad adds 32 more, so the original local is [ESP+44h] here.
        mov edx, [esp + 68]
        push edx
        push ecx
        push eax
        push esi
        call PrepareSequentialBegin
        add esp, 16
        test eax, eax
        jz sequentialFallback
        // pushad layout: [esp+0] is saved EDI, [esp+32] is this qglBegin
        // call's return address. Stock leaves EDI=poly+0x10 when the skipped
        // loop reaches +0x74103, reproduce that exact observable register.
        mov eax, dword ptr [g_sequentialResumeEdi]
        test eax, eax
        jz sequentialFallback
        mov dword ptr [esp], eax
        mov eax, dword ptr [g_sequentialResume]
        mov dword ptr [esp + 32], eax
        popad
        ret 4
    sequentialFallback:
        popad
    original:
        jmp dword ptr [g_begin]
    suppressed:
        ret 4
    }
}

void __declspec(naked) WINAPI MTexCoord2f_Hook(unsigned /*texture*/, float /*s*/, float /*t*/)
{
    __asm
    {
        cmp dword ptr [g_sequentialSuppress], 0
        jne suppressed
        jmp dword ptr [g_mtexCoord2f]
    suppressed:
        ret 12
    }
}

void __declspec(naked) WINAPI Vertex3fv_Hook(const float* /*vertex*/)
{
    __asm
    {
        cmp dword ptr [g_sequentialSuppress], 0
        jne suppressed
        jmp dword ptr [g_vertex3fv]
    suppressed:
        ret 4
    }
}

void __declspec(naked) WINAPI End_Hook()
{
    __asm
    {
        call studio_drawbatch::TryEnd
        test al, al
        jne studioHandled
        jmp dword ptr [g_end]
    studioHandled:
        ret
    }
}

void WINAPI TexSubImage2D_Hook(unsigned target, int level,
                               int xoffset, int yoffset,
                               int width, int height,
                               unsigned format, unsigned type,
                               const void* pixels)
{
    if (g_worldScopeReady)
        FlushSequentialRun(true);
    if (g_texSubImage2D)
        g_texSubImage2D(target, level, xoffset, yoffset, width, height,
                        format, type, pixels);
}

bool RefreshSequentialHooks()
{
    g_studioBeginEndPairReady = false;
    g_sequentialUploadBarrierReady = false;
    if (!g_hwBase)
        return false;

    auto** beginSlot = reinterpret_cast<void**>(g_hwBase + kQglBeginRva);
    auto** endSlot = reinterpret_cast<void**>(g_hwBase + kQglEndRva);
    auto** mtexSlot = reinterpret_cast<void**>(g_hwBase + kQglMTexCoord2fRva);
    auto** subImageSlot =
        reinterpret_cast<void**>(g_hwBase + kQglTexSubImage2DRva);
    if (!*beginSlot || !*endSlot || !*mtexSlot)
        return false;

    if (*beginSlot != reinterpret_cast<void*>(&Begin_Hook))
    {
        // Capture/patch only the first pointer we own. If another renderer
        // replaces qglBegin later, fail open rather than wrapping a hook that
        // may already chain back to Begin_Hook.
        if (!g_begin)
        {
            g_begin = reinterpret_cast<GlBeginFn>(*beginSlot);
            if (!PatchPointer(beginSlot, reinterpret_cast<void*>(g_begin),
                              reinterpret_cast<void*>(&Begin_Hook)))
                return false;
        }
        else
        {
            return false;
        }
    }
    if (*endSlot != reinterpret_cast<void*>(&End_Hook))
    {
        if (!g_end)
        {
            g_end = reinterpret_cast<GlEndFn>(*endSlot);
            if (!PatchPointer(endSlot, reinterpret_cast<void*>(g_end),
                              reinterpret_cast<void*>(&End_Hook)))
                return false;
        }
        else
        {
            return false;
        }
    }
    // Studio capture suppresses Begin and depends on our paired End hook to
    // replay/draw the primitive. Never advertise readiness until both live
    // dispatch slots are verified together.
    if (*beginSlot != reinterpret_cast<void*>(&Begin_Hook) ||
        *endSlot != reinterpret_cast<void*>(&End_Hook))
        return false;
    g_studioBeginEndPairReady = true;
    // The direct sequential path jumps over Gold's entire per-vertex loop, so
    // there is no reason to hook every MultiTexCoord2f/Vertex3fv call anymore.
    // Keep only the real MultiTexCoord2f pointer to reproduce final current ST.
    if (*mtexSlot == reinterpret_cast<void*>(&MTexCoord2f_Hook))
    {
        if (!g_mtexCoord2f)
            return false;
    }
    else
    {
        g_mtexCoord2f = reinterpret_cast<GlMTexCoord2fFn>(*mtexSlot);
    }
    g_texCoord2f = ReadQgl<GlTexCoord2fFn>(kQglTexCoord2fRva);

    // Dynamic lightmaps can update a texture between two otherwise compatible
    // surfaces. A pending ordered run must be visible before that mutation.
    if (subImageSlot && *subImageSlot)
    {
        if (*subImageSlot != reinterpret_cast<void*>(&TexSubImage2D_Hook))
        {
            // Install only over the pointer we originally captured. If a mod
            // replaces qglTexSubImage2D later, fail open instead of wrapping
            // that hook and risking a callback cycle through our old pointer.
            if (!g_texSubImage2D)
                g_texSubImage2D = reinterpret_cast<GlTexSubImage2DFn>(*subImageSlot);
            if (*subImageSlot == reinterpret_cast<void*>(g_texSubImage2D))
            {
                PatchPointer(subImageSlot, reinterpret_cast<void*>(g_texSubImage2D),
                             reinterpret_cast<void*>(&TexSubImage2D_Hook));
            }
        }
        g_sequentialUploadBarrierReady =
            *subImageSlot == reinterpret_cast<void*>(&TexSubImage2D_Hook) &&
            g_texSubImage2D != nullptr;
    }
    return g_begin && g_end && g_texCoord2f && g_mtexCoord2f;
}

bool BeginPass()
{
    g_passReady = false;
    if (!g_installed || !g_enabled || !rendererperf::Enabled())
        return false;
    // Modern GoldClient normally uses R_DrawSequentialPoly when this is zero,
    // do not pay client-array setup cost around an empty DrawTextureChains pass.
    if (*reinterpret_cast<int*>(g_hwBase + kTextureSortModeRva) == 0)
        return false;
    if (!EnsureCache())
        return false;
    if (!ForeignClientArraysClear(1))
        return false;

    int arrayBuffer = 0;
    int indexBuffer = 0;
    g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
    g_getIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &indexBuffer);
    if (arrayBuffer != 0 || indexBuffer != 0)
        return false; // another renderer/mod owns client-array buffer state

    if (g_clientActiveTexture)
    {
        int clientTexture = static_cast<int>(GL_TEXTURE0);
        g_getIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &clientTexture);
        if (static_cast<unsigned>(clientTexture) != GL_TEXTURE0)
            return false;
    }

    if (++g_passGeneration == 0)
    {
        for (SurfaceInfo& info : g_surfaceInfo)
        {
            info.batchedGeneration = 0;
            info.legacyGeneration = 0;
        }
        g_passGeneration = 1;
    }

    g_pushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    if (g_clientActiveTexture)
        g_clientActiveTexture(GL_TEXTURE0);
    g_bindBuffer(GL_ARRAY_BUFFER, g_vbo);
    g_enableClientState(GL_VERTEX_ARRAY);
    g_enableClientState(GL_TEXTURE_COORD_ARRAY);
    g_vertexPointer(3, GL_FLOAT, kVertexStride, reinterpret_cast<const void*>(0));
    g_texCoordPointer(2, GL_FLOAT, kVertexStride, reinterpret_cast<const void*>(3 * sizeof(float)));
    g_passReady = true;
    return true;
}

void EndPass()
{
    if (!g_passReady)
        return;
    g_bindBuffer(GL_ARRAY_BUFFER, 0);
    g_popClientAttrib();
    g_passReady = false;
}

void MarkChainLegacy(std::uint8_t* surface)
{
    for (auto* s = surface; s; s = Read<std::uint8_t*>(s, 0x28))
    {
        const int index = SurfaceIndex(s);
        if (index >= 0)
            g_surfaceInfo[static_cast<std::size_t>(index)].legacyGeneration = g_passGeneration;
    }
}

bool TryBatchChain(std::uint8_t* surface)
{
    if (!g_passReady)
        return false;

    const int firstIndex = SurfaceIndex(surface);
    if (firstIndex < 0)
        return false;
    SurfaceInfo& firstInfo = g_surfaceInfo[static_cast<std::size_t>(firstIndex)];
    if (firstInfo.batchedGeneration == g_passGeneration)
        return true; // already emitted by the first surface in this texture chain
    if (firstInfo.legacyGeneration == g_passGeneration)
        return false;

    g_chainSurfaces.clear();
    g_chainFirsts.clear();
    g_chainCounts.clear();
    g_chainIndexCounts.clear();
    g_chainIndexOffsets.clear();

    std::uint8_t* chainTexture = nullptr;
    for (auto* s = surface; s; s = Read<std::uint8_t*>(s, 0x28))
    {
        const int index = SurfaceIndex(s);
        if (index < 0)
        {
            MarkChainLegacy(surface);
            return false;
        }
        SurfaceInfo& info = g_surfaceInfo[static_cast<std::size_t>(index)];
        std::uint8_t* texture = nullptr;
        if (!SurfaceEligible(s, info, texture) ||
            (chainTexture && texture != chainTexture))
        {
            MarkChainLegacy(surface);
            return false;
        }
        if (!chainTexture)
            chainTexture = texture;

        g_chainSurfaces.push_back(s);
        g_chainFirsts.push_back(info.first);
        g_chainCounts.push_back(info.count);
        if (info.indexFirst < 0 || info.indexCount < 3)
        {
            MarkChainLegacy(surface);
            return false;
        }
        g_chainIndexCounts.push_back(info.indexCount);
        g_chainIndexOffsets.push_back(reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(info.indexFirst) * WorldIndexBytes()));
    }

    if (g_chainSurfaces.empty() || !chainTexture)
    {
        MarkChainLegacy(surface);
        return false;
    }

    const int textureId = Read<int>(chainTexture, 0x1C);
    if (textureId <= 0)
    {
        MarkChainLegacy(surface);
        return false;
    }

    // Match R_RenderBrushPoly's texture bind, then emit fan-equivalent triangles
    // from immutable BSP geometry/index buffers. Gold has already built the
    // texture chain and all lightmap/material eligibility checks above keep
    // special state on the stock path.
    g_glBind(0, textureId);

    g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
    if (g_multiDrawElements && g_chainSurfaces.size() > 1)
    {
        g_multiDrawElements(GL_TRIANGLES, g_chainIndexCounts.data(), WorldIndexType(),
                            g_chainIndexOffsets.data(),
                            static_cast<int>(g_chainSurfaces.size()));
        if (g_collectStats) ++g_stats.multiDrawElementsCalls;
    }
    else
    {
        for (std::size_t i = 0; i < g_chainSurfaces.size(); ++i)
            g_drawElements(GL_TRIANGLES, g_chainIndexCounts[i], WorldIndexType(),
                           g_chainIndexOffsets[i]);
        if (g_collectStats) g_stats.drawElementsCalls += g_chainSurfaces.size();
    }
    g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Stock DrawGLPoly leaves unit-0's current texture coordinate equal to
    // the final vertex of the final surface in chain order. Indexed drawing
    // does not update current coordinates, so reproduce that persistent state.
    if (g_texCoord2f && !g_chainSurfaces.empty())
    {
        auto* lastSurface = g_chainSurfaces.back();
        const int lastIndex = SurfaceIndex(lastSurface);
        if (lastIndex >= 0)
        {
            const SurfaceInfo& lastInfo =
                g_surfaceInfo[static_cast<std::size_t>(lastIndex)];
            const float* last =
                reinterpret_cast<const float*>(lastInfo.poly + 0x10) +
                (lastInfo.count - 1) * 7;
            g_texCoord2f(last[3], last[4]);
        }
    }

    auto** lightmapPolys = reinterpret_cast<std::uint8_t**>(g_hwBase + kLightmapPolysRva);
    int& brushPolys = *reinterpret_cast<int*>(g_hwBase + kBrushPolysRva);
    for (std::size_t i = 0; i < g_chainSurfaces.size(); ++i)
    {
        auto* s = g_chainSurfaces[i];
        const int index = SurfaceIndex(s);
        SurfaceInfo& info = g_surfaceInfo[static_cast<std::size_t>(index)];
        ++brushPolys;
        const int lightmap = Read<int>(s, 0x38);
        *reinterpret_cast<std::uint8_t**>(info.poly + 0x04) = lightmapPolys[lightmap];
        lightmapPolys[lightmap] = info.poly;
        info.batchedGeneration = g_passGeneration;

        if (g_collectStats)
        {
            ++g_stats.surfacesBatched;
            g_stats.verticesBatched += static_cast<std::uint64_t>(info.count);
            g_stats.indicesBatched += static_cast<std::uint64_t>(info.indexCount);
            g_stats.trianglesBatched += static_cast<std::uint64_t>(info.indexCount / 3);
        }
    }

    if (g_collectStats) ++g_stats.chainsBatched;
    return true;
}

void __fastcall RenderBrushPoly_Hook(std::uint8_t* surface, void* ignored)
{
    if (TryBatchChain(surface))
        return;
    if (g_collectStats) ++g_stats.surfacesLegacy;
    if (g_renderBrushPoly)
        g_renderBrushPoly(surface, ignored);
}

void __cdecl DrawTextureChains_Hook()
{
    if (!g_drawTextureChains)
        return;
    const bool profiling = prof::Active();
    const long long start = profiling ? prof::Now() : 0;
    // World recursion is complete. Preserve Gold's ordering by making every
    // deferred ordinary surface visible before sky/water/texture-chain work.
    FlushSequentialRun(true);
    g_expectedSequentialSurface = nullptr;
    BeginPass();
    g_drawTextureChains();
    EndPass();
    if (profiling)
    {
        ++g_stats.drawTextureChainsCalls;
        g_stats.drawTextureChainsTicks += prof::Now() - start;
    }
}

void __cdecl NewMap_Hook()
{
    if (!g_newMap)
        return;
    ClearSequentialRun();
    g_expectedSequentialSurface = nullptr;
    if (g_vbo && !g_contextLost)
        ForgetGpuBuffer(true);
    ++g_mapGeneration;
    if (g_mapGeneration == 0)
        g_mapGeneration = 1;
    g_dirty = true;
    g_newMap();
}

bool __fastcall ContextDestroy_Hook(void* self, void* ignored)
{
    ClearSequentialRun();
    g_expectedSequentialSurface = nullptr;
    ++g_contextGeneration;
    if (g_contextGeneration == 0)
        g_contextGeneration = 1;
    g_contextLost = true;
    g_vbo = 0; // context deletion owns the old GPU object, never delete it later
    g_ibo = 0;
    g_dirty = true;
    return g_contextDestroy ? g_contextDestroy(self, ignored) : true;
}
} // namespace

bool StudioImmediateReady()
{
    if (!g_studioBeginEndPairReady || !g_begin || !g_end || !g_hwBase)
        return false;
    __try
    {
        auto** beginSlot = reinterpret_cast<void**>(g_hwBase + kQglBeginRva);
        auto** endSlot = reinterpret_cast<void**>(g_hwBase + kQglEndRva);
        return *beginSlot == reinterpret_cast<void*>(&Begin_Hook) &&
               *endSlot == reinterpret_cast<void*>(&End_Hook);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void StudioImmediateBegin(unsigned mode)
{
    if (g_begin) g_begin(mode);
}

void StudioImmediateEnd()
{
    if (g_end) g_end();
}

std::uint32_t ContextGeneration()
{
    return g_contextGeneration;
}

bool ContextGenerationReady()
{
    // The trampoline is non-null only after the exact hw ContextDestroy entry
    // was successfully hooked.  Studio GPU objects must not trust generation
    // numbers unless this lifecycle hook is actually present.
    return g_contextDestroy != nullptr;
}

bool Install(HMODULE hw, cl_enginefunc_t* engine)
{
    if (!hwbuild::MatchesTarget(hw) || !engine || !engine->pfnRegisterVariable)
    {
        rendererlog::Line("worldvbo: target hw.dll/engine unavailable, disabled");
        return false;
    }

    g_hwBase = reinterpret_cast<std::uint8_t*>(hw);
    g_engine = engine;
    g_sequentialBeginReturn = g_hwBase + 0x00073E6Eu;
    g_sequentialResume = g_hwBase + kSequentialResumeRva;
    g_drawSequentialPoly = g_hwBase + kDrawSequentialPolyRva;
    g_renderBrushPoly = reinterpret_cast<RenderBrushPolyFn>(g_hwBase + kRenderBrushPolyRva);
    g_glBind = reinterpret_cast<GlBindFn>(g_hwBase + kGlBindRva);

    __try
    {
        g_cvar = engine->pfnRegisterVariable("r_world_vbo", "1", 0);
        g_brushCvar =
            engine->pfnRegisterVariable("r_world_brush_vbo", "1", 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_cvar = nullptr;
        g_brushCvar = nullptr;
    }

    if (!RefreshGlFunctions())
    {
        rendererlog::Line("worldvbo: required OpenGL VBO/client-array entrypoints unavailable");
        return false;
    }
    if (!RefreshSequentialHooks())
    {
        rendererlog::Line("worldvbo: sequential qgl hooks unavailable, disabled");
        return false;
    }

    const bool sequentialCall1 =
        PatchCall(g_hwBase + kDrawSequentialCall1Rva, g_drawSequentialPoly,
                  reinterpret_cast<void*>(&SequentialCall_Hook));
    const bool sequentialCall2 =
        PatchCall(g_hwBase + kDrawSequentialCall2Rva, g_drawSequentialPoly,
                  reinterpret_cast<void*>(&SequentialCall_Hook));
    g_sequentialCallsitesReady = sequentialCall1 && sequentialCall2;
    if (!g_sequentialCallsitesReady)
    {
        rendererlog::Line(
            "worldvbo: sequential run callsites incomplete (%d/%d), per-surface VBO path retained",
            sequentialCall1 ? 1 : 0, sequentialCall2 ? 1 : 0);
    }

    if (!PatchCall(g_hwBase + kRenderBrushCallRva,
                   reinterpret_cast<void*>(g_renderBrushPoly),
                   reinterpret_cast<void*>(&RenderBrushPoly_Hook)))
    {
        rendererlog::Line("worldvbo: R_RenderBrushPoly world callsite mismatch, disabled");
        return false;
    }

    g_drawTextureChains = reinterpret_cast<DrawTextureChainsFn>(
        inl::Hook(g_hwBase + kDrawTextureChainsRva,
                  reinterpret_cast<void*>(&DrawTextureChains_Hook)));
    g_newMap = reinterpret_cast<NewMapFn>(
        inl::Hook(g_hwBase + kNewMapRva,
                  reinterpret_cast<void*>(&NewMap_Hook)));
    g_contextDestroy = reinterpret_cast<ContextDestroyFn>(
        inl::Hook(g_hwBase + kContextDestroyRva,
                  reinterpret_cast<void*>(&ContextDestroy_Hook)));

    if (!g_drawTextureChains || !g_newMap || !g_contextDestroy)
    {
        rendererlog::Line("worldvbo: lifecycle hook incomplete, VBO path stays fail-open");
        g_installed = false;
        return false;
    }

    g_installed = true;
    g_dirty = true;
    rendererlog::Line("worldvbo: installed retained BSP VBO/EBO + immediate sequential VBO safety path (r_world_vbo=1 r_world_brush_vbo=1 defaults, multidraw=%s, multidraw_elements=%s, seq_calls=%s, lm_barrier=%s)",
               g_multiDrawArrays ? "yes" : "no",
               g_multiDrawElements ? "yes" : "no",
               g_sequentialCallsitesReady ? "yes" : "no",
               g_sequentialUploadBarrierReady ? "yes" : "no");
    return true;
}

void UpdateFrame()
{
    if (!g_installed)
        return;
    // The validated path is default-on. If cvar registration failed or the
    // pointer is unavailable, stay on stock rendering rather than assuming it.
    int mode = 0;
    if (g_cvar)
    {
        __try
        {
            const float value = g_cvar->value;
            if (value == 1.0f) mode = 1;
            else if (value == 2.0f) mode = 2;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            mode = 0;
        }
    }
    g_mode = mode;
    g_enabled = mode == 1;
    g_brushEnabled = false;
    if (g_brushCvar)
    {
        __try
        {
            g_brushEnabled = g_brushCvar->value == 1.0f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_brushEnabled = false;
        }
    }
    if (!RefreshSequentialHooks())
    {
        g_mode = 0;
        g_enabled = false;
    }
}

void BeginWorldScope()
{
    g_worldScopeReady = false;
    ClearSequentialRun();
    g_expectedSequentialSurface = nullptr;
    if (!g_installed || g_mode != 1 || !g_enabled ||
        !rendererperf::Enabled() ||
        *reinterpret_cast<int*>(g_hwBase + kTextureSortModeRva) != 0)
        return;
    if (!EnsureCache() || !g_clientActiveTexture ||
        !PolygonFillMode() ||
        !ForeignClientArraysClear(2))
        return;

    g_worldPreviousArrayBuffer = 0;
    g_worldPreviousClientTexture = static_cast<int>(GL_TEXTURE0);
    g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &g_worldPreviousArrayBuffer);
    g_getIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &g_worldPreviousClientTexture);

    g_pushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    g_bindBuffer(GL_ARRAY_BUFFER, g_vbo);

    // Vertex coordinates are shared by both texture units.
    g_enableClientState(GL_VERTEX_ARRAY);
    g_vertexPointer(3, GL_FLOAT, kVertexStride,
                    reinterpret_cast<const void*>(0));

    // Base texture UVs: v[3], v[4].
    g_clientActiveTexture(GL_TEXTURE0);
    g_enableClientState(GL_TEXTURE_COORD_ARRAY);
    g_texCoordPointer(2, GL_FLOAT, kVertexStride,
                      reinterpret_cast<const void*>(3 * sizeof(float)));

    // Lightmap UVs: v[5], v[6].
    g_clientActiveTexture(GL_TEXTURE0 + 1u);
    g_enableClientState(GL_TEXTURE_COORD_ARRAY);
    g_texCoordPointer(2, GL_FLOAT, kVertexStride,
                      reinterpret_cast<const void*>(5 * sizeof(float)));

    // gl*Pointer captures the VBO binding, restore the server-visible binding
    // now so untouched GoldClient code still observes its original state.
    g_bindBuffer(GL_ARRAY_BUFFER,
                 static_cast<unsigned>(g_worldPreviousArrayBuffer));
    g_clientActiveTexture(
        static_cast<unsigned>(g_worldPreviousClientTexture));
    g_worldScopeReady = true;
}

void EndWorldScope()
{
    if (!g_worldScopeReady)
        return;
    FlushSequentialRun(true);
    g_popClientAttrib();
    g_clientActiveTexture(
        static_cast<unsigned>(g_worldPreviousClientTexture));
    g_worldScopeReady = false;
    g_sequentialSuppress = 0;
    g_sequentialSurface = nullptr;
    g_sequentialResumeEdi = nullptr;
    g_sequentialDetailActive = false;
    g_expectedSequentialSurface = nullptr;
}

void BeginBrushScope()
{
    g_brushScopeReady = false;
    g_expectedSequentialSurface = nullptr;

    if (!g_installed || g_mode != 1 || !g_enabled || !g_brushEnabled ||
        !rendererperf::Enabled() ||
        *reinterpret_cast<int*>(g_hwBase + kTextureSortModeRva) != 0)
        return;
    if (!EnsureCache() || !g_clientActiveTexture ||
        !PolygonFillMode() ||
        !ForeignClientArraysClear(2))
        return;

    // The exact solid brush caller has already assigned currententity. Restrict
    // the retained path to opaque inline BSP models whose surface array is the
    // same storage captured from cl.worldmodel.
    auto* entity =
        *reinterpret_cast<std::uint8_t**>(g_hwBase + kCurrentEntityRva);
    if (!entity || Read<int>(entity, 0x2F8) != 0)
        return;
    auto* model = Read<std::uint8_t*>(entity, 0xB94);
    if (!model || Read<std::uint8_t*>(model, 0xB4) != g_surfaces)
        return;
    const int firstSurface = Read<int>(model, 0x70);
    const int surfaceCount = Read<int>(model, 0x74);
    if (firstSurface < 0 || surfaceCount <= 0 ||
        firstSurface > g_numSurfaces ||
        surfaceCount > g_numSurfaces - firstSurface)
        return;

    g_brushPreviousArrayBuffer = 0;
    g_brushPreviousClientTexture = static_cast<int>(GL_TEXTURE0);
    g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &g_brushPreviousArrayBuffer);
    g_getIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &g_brushPreviousClientTexture);

    g_pushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    g_bindBuffer(GL_ARRAY_BUFFER, g_vbo);
    g_enableClientState(GL_VERTEX_ARRAY);
    g_vertexPointer(3, GL_FLOAT, kVertexStride,
                    reinterpret_cast<const void*>(0));

    g_clientActiveTexture(GL_TEXTURE0);
    g_enableClientState(GL_TEXTURE_COORD_ARRAY);
    g_texCoordPointer(2, GL_FLOAT, kVertexStride,
                      reinterpret_cast<const void*>(3 * sizeof(float)));

    g_clientActiveTexture(GL_TEXTURE0 + 1u);
    g_enableClientState(GL_TEXTURE_COORD_ARRAY);
    g_texCoordPointer(2, GL_FLOAT, kVertexStride,
                      reinterpret_cast<const void*>(5 * sizeof(float)));

    g_bindBuffer(GL_ARRAY_BUFFER,
                 static_cast<unsigned>(g_brushPreviousArrayBuffer));
    g_clientActiveTexture(
        static_cast<unsigned>(g_brushPreviousClientTexture));
    g_brushScopeReady = true;
    if (g_collectStats)
        ++g_stats.brushScopes;
}

void EndBrushScope()
{
    if (!g_brushScopeReady)
        return;

    // Brush geometry is intentionally immediate: R_DrawBrushModel owns a
    // per-entity modelview that is already popped before this wrapper returns.
    // Never carry an ordered run across that matrix boundary.
    FlushSequentialRun(true);
    g_popClientAttrib();
    g_clientActiveTexture(
        static_cast<unsigned>(g_brushPreviousClientTexture));
    g_brushScopeReady = false;
    g_sequentialSurface = nullptr;
    g_sequentialResumeEdi = nullptr;
    g_sequentialDetailActive = false;
    g_expectedSequentialSurface = nullptr;
}

void SetProfileCollection(bool enabled)
{
    g_collectStats = enabled;
}

ProfileStats ConsumeProfileStats()
{
    ProfileStats out = g_stats;
    g_stats = {};
    return out;
}
} // namespace worldvbo
