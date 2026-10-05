#include "world_vbo.h"
#include "hw_build.h"
#include "inline_hook.h"
#include "log.h"
#include "perf_control.h"
#include "profile.h"
#include "studio_drawbatch.h"
#include "vis_cache.h"

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>
#include <xmmintrin.h>

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
constexpr std::uintptr_t kRecursiveWorldNodeRva = 0x00075CC0u;
constexpr std::uintptr_t kRecursiveWorldCallRva = 0x000760DDu; // R_DrawWorld top-level call
constexpr std::uintptr_t kTextureAnimationRva   = 0x00073B20u;
constexpr std::uintptr_t kRenderDynamicLightmapsRva = 0x00074640u;
constexpr std::uintptr_t kBindWorldProgramRva   = 0x0007BE60u;

constexpr std::uintptr_t kWorldModelRva        = 0x02F90E20u;
constexpr std::uintptr_t kCurrentEntityRva     = 0x0078F470u;
constexpr std::uintptr_t kFrameCountRva        = 0x0078F474u;
constexpr std::uintptr_t kLightStyleValuesRva  = 0x0078F478u;
constexpr std::uintptr_t kBrushPolysRva        = 0x0078F954u;
constexpr std::uintptr_t kLightmapPolysRva     = 0x027DD878u;
constexpr std::uintptr_t kTextureSortModeRva   = 0x0027268Cu;
constexpr std::uintptr_t kLightmapOverrideRva  = 0x00790630u;
constexpr std::uintptr_t kSequentialDebugDrawRva = 0x0333A3C8u;
// R_DrawSequentialPoly multitexture-path globals (see r_world_batch below).
constexpr std::uintptr_t kWorldSpecialPassRva   = 0x0078FA58u;
constexpr std::uintptr_t kMultitextureUnitsRva  = 0x0333A980u;
constexpr std::uintptr_t kMultitextureActiveRva = 0x0333A97Au; // byte
constexpr std::uintptr_t kDetailEnabledRva      = 0x0043EB9Eu; // byte
constexpr std::uintptr_t kDetailLoadedRva       = 0x0043EB9Fu; // byte
constexpr std::uintptr_t kDetailCvarRva         = 0x02FDFDC0u;
constexpr std::uintptr_t kWorldProgramRva       = 0x027E34A0u; // program table [3]
constexpr std::uintptr_t kBoundProgramRva       = 0x03C4E4A4u;
constexpr std::uintptr_t kLightmapRectRva       = 0x027DB078u;
constexpr std::uintptr_t kLightmapModifiedRva   = 0x027DD078u;
constexpr std::uintptr_t kLightmapTexturesRva   = 0x027DE078u;
constexpr std::uintptr_t kLightmapBytesRva      = 0x027DE878u;
constexpr std::uintptr_t kLightmapFormatRva     = 0x0027267Cu;
constexpr std::uintptr_t kLightmapDataRva       = 0x00797A48u;
// Inlined DecalSurfaceAdd + R_DrawDecals(true) tail of R_DrawSequentialPoly.
constexpr std::uintptr_t kDrawDecalsRva         = 0x00079260u;
constexpr std::uintptr_t kConPrintfRva          = 0x000BA840u;
constexpr std::uintptr_t kDecalOverflowTextRva  = 0x0021DA2Cu; // "Too many decal surfaces!\n"
constexpr std::uintptr_t kDecalSurfacesRva      = 0x027DEFA8u;
constexpr std::uintptr_t kDecalSurfaceCountRva  = 0x033C10F4u;
constexpr std::uintptr_t kDecalLimitOverrideRva = 0x03339C00u; // byte
constexpr std::uintptr_t kDecalLimitOverrideValueRva = 0x03339C04u;
constexpr std::uintptr_t kDecalLimitValueRva    = 0x03339BBCu;
constexpr int kMaxDecalSurfaces = 0x1000;

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
constexpr std::uintptr_t kQglTexEnviRva             = 0x027E3744u;
constexpr std::uintptr_t kQglUseProgramRva          = 0x027E381Cu;
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
constexpr unsigned GL_STREAM_DRAW             = 0x88E0u;
constexpr unsigned GL_TEXTURE_2D              = 0x0DE1u;
constexpr unsigned GL_UNSIGNED_BYTE           = 0x1401u;
constexpr unsigned GL_TEXTURE_ENV             = 0x2300u;
constexpr unsigned GL_TEXTURE_ENV_MODE        = 0x2200u;
constexpr int GL_MODULATE                     = 0x2100;
constexpr unsigned GL_CLIENT_VERTEX_ARRAY_BIT = 0x00000002u;
constexpr unsigned GL_POLYGON_MODE            = 0x0B40u;
constexpr unsigned GL_POLYGON_SMOOTH          = 0x0B41u;
constexpr unsigned GL_LIGHTING                = 0x0B50u;
constexpr int GL_FILL                         = 0x1B02;

constexpr int kSurfaceSize = 0x84;
constexpr int kVertexStride = 7 * sizeof(float);
constexpr int kMaxLightmaps = 64;
constexpr unsigned kAllowedSurfaceFlags = 0x00000802u; // PLANEBACK + proven harmless 0x800
// R_DrawSequentialPoly's multitexture branch reads only these surface flags:
// SKY/TURB/UNDERWATER select its early branch, 0x200 the scrolling loop.
// R_RenderDynamicLightmaps only tests SKY/TURB (a subset).
constexpr unsigned kSequentialSpecialFlags = 0x00000094u;
constexpr unsigned kSequentialScrollFlag   = 0x00000200u;
// R_DrawWorld clears lightmap_polys with 0x800 bytes, and the rect/modified/
// texture arrays are laid out back to back with the same stride: 512 blocks.
constexpr int kMaxLightmapBlocks = 512;
constexpr int kLightmapBlockSize = 128;
constexpr int kBatchSlotBits = 11;
constexpr int kBatchSlots = 1 << kBatchSlotBits;
constexpr int kMaxBatchBuckets = 1024;
constexpr std::size_t kBatchIboMinBytes = 256u * 1024u;

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
using GlBufferSubDataFn = void (WINAPI*)(unsigned target, std::ptrdiff_t offset,
                                         std::ptrdiff_t size, const void* data);
using GlTexEnviFn = void (WINAPI*)(unsigned target, unsigned pname, int param);
using GlUseProgramFn = void (WINAPI*)(unsigned program);
using SdlGetProcAddressFn = void* (__cdecl*)(const char* name);
using RecursiveWorldNodeFn = void (__fastcall*)(std::uint8_t* node, int flag);
using TextureAnimationFn = std::uint8_t* (__fastcall*)(std::uint8_t* surface);
using RenderDynamicLightmapsFn = void (__fastcall*)(std::uint8_t* surface);
using BindWorldProgramFn = void (__fastcall*)(unsigned program);
using DrawDecalsFn = void (__fastcall*)(int multitexture);
using ConPrintfFn = void (__cdecl*)(const char* format, ...);

struct SurfaceInfo
{
    int first = -1;
    int count = 0;
    int indexFirst = -1;
    int indexCount = 0;
    std::uint8_t* poly = nullptr;
    std::uint32_t batchedGeneration = 0;
    std::uint32_t legacyGeneration = 0;
    std::uint32_t worldBatchFrame = 0;
};

struct BatchBucket
{
    int texnum;
    int lightmap;
    int nextInLightmap;
    int indexCount;
    int firstElement;
    int cursor;
};

// Collected in traversal order. The static index range is copied here so the
// flush scatters without touching SurfaceInfo again.
struct BatchEntry
{
    int indexFirst;
    int indexCount;
    int bucket;
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
int g_maxTextureUnits = 0;
std::uint32_t g_maxTextureUnitsGeneration = 0;

// r_world_batch state. Collection is live only inside the top-level
// R_RecursiveWorldNode call of R_DrawWorld.
cvar_t* g_batchCvar = nullptr;
int g_batchMode = 0;
bool g_batchCallsiteReady = false;
bool g_batchCollecting = false;
bool g_batchValidating = false;
bool g_batchFlushing = false;
bool g_batchFrameRejected = false;
int g_batchCallerFlag = 0;
int g_batchLastSurface = -1;
int g_batchLastBucket = -1;
bool g_batchLastDecal = false;
std::uint32_t g_batchFrame = 0;
int g_batchPreviousArrayBuffer = 0;
int g_batchPreviousIndexBuffer = 0;
int g_batchPreviousClientTexture = static_cast<int>(GL_TEXTURE0);
int g_batchTextureUnits = 0;
RecursiveWorldNodeFn g_recursiveWorldNode = nullptr;
TextureAnimationFn g_textureAnimation = nullptr;
RenderDynamicLightmapsFn g_renderDynamicLightmaps = nullptr;
BindWorldProgramFn g_bindWorldProgram = nullptr;
DrawDecalsFn g_drawDecals = nullptr;
ConPrintfFn g_conPrintf = nullptr;
GlBufferSubDataFn g_bufferSubData = nullptr;
std::vector<BatchBucket> g_batchBuckets;
std::vector<BatchEntry> g_batchEntries;
std::vector<int> g_batchDecalSurfaces;
std::vector<int> g_batchLightmapOrder;
std::vector<std::uint8_t> g_batchStaging;
std::uint32_t g_batchSlotStamp[kBatchSlots]{};
int g_batchSlotBucket[kBatchSlots]{};
std::uint32_t g_batchLightmapStamp[kMaxLightmapBlocks]{};
int g_batchLightmapHead[kMaxLightmapBlocks]{};
int g_batchLightmapTail[kMaxLightmapBlocks]{};
unsigned g_batchIbo = 0;
std::size_t g_batchIboCapacity = 0;
std::size_t g_batchIboOffset = 0;

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
        unsigned ids[3]{};
        int count = 0;
        if (g_vbo) ids[count++] = g_vbo;
        if (g_ibo) ids[count++] = g_ibo;
        if (g_batchIbo) ids[count++] = g_batchIbo;
        if (count)
            g_deleteBuffers(count, ids);
    }
    g_vbo = 0;
    g_ibo = 0;
    g_batchIbo = 0;
    g_batchIboCapacity = 0;
    g_batchIboOffset = 0;
    g_batchBuckets.clear();
    g_batchEntries.clear();
    g_batchDecalSurfaces.clear();
    g_batchLightmapOrder.clear();
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
    g_bufferSubData = nullptr;
    auto getProc = *reinterpret_cast<SdlGetProcAddressFn*>(g_hwBase + kSdlGetProcAddressIatRva);
    if (getProc)
    {
        g_bufferSubData = reinterpret_cast<GlBufferSubDataFn>(getProc("glBufferSubData"));
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

// GL_MAX_TEXTURE_UNITS is immutable for a context. Query it once per context
// generation instead of once per detail surface/scope.
int CachedMaxTextureUnits()
{
    if (g_maxTextureUnitsGeneration != g_contextGeneration ||
        g_maxTextureUnits <= 0)
    {
        int units = 0;
        g_getIntegerv(GL_MAX_TEXTURE_UNITS, &units);
        g_maxTextureUnits = units;
        g_maxTextureUnitsGeneration = g_contextGeneration;
    }
    return g_maxTextureUnits;
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

        g_getIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &previousClientTexture);
        clientTextureKnown = true;
        const int maxTextureUnits = CachedMaxTextureUnits();
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

bool CollectWorldBatchSurface(std::uint8_t* surface, int callerFlag);

// Returns nonzero when r_world_batch consumed the surface: the stock call is
// skipped and the caller's own `add esp, 4` releases the stack argument.
int __cdecl SequentialCallDispatch(std::uint8_t* surface, int face, int callerFlag)
{
    if (g_batchCollecting || g_batchValidating)
    {
        if (CollectWorldBatchSurface(surface, callerFlag))
            return 1;
    }
    else if (g_batchFrameRejected && g_collectStats)
    {
        ++g_stats.batchFallbackFrame;
    }
    BeforeSequentialCall(surface, face, callerFlag);
    return 0;
}

void __declspec(naked) SequentialCall_Hook()
{
    __asm
    {
        // Exact hw.dll ABI: ECX=msurface_t*, EDX=face, plus one caller-owned
        // stack argument that the caller pops. R_DrawSequentialPoly is an
        // ordinary callee (EBX/ESI/EDI/EBP preserved, EAX/ECX/EDX/flags
        // clobbered, no return value) and both callers reload everything they
        // use afterwards, so only ECX/EDX must survive for the tail-jump.
        push ecx
        push edx
        push dword ptr [esp + 12]
        push edx
        push ecx
        call SequentialCallDispatch
        add esp, 12
        pop edx
        pop ecx
        test eax, eax
        jne consumed
        jmp dword ptr [g_drawSequentialPoly]
    consumed:
        ret
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

    // A failed build would otherwise be retried (and logged) on every world
    // and brush scope. Retry the same map at most once every 5 seconds.
    static std::uint8_t* failedWorld = nullptr;
    static std::uint32_t failedGeneration = 0;
    static DWORD failedTick = 0;
    if (failedWorld == world && failedGeneration == g_mapGeneration &&
        !g_contextLost && GetTickCount() - failedTick < 5000u)
        return false;

    if (g_contextLost)
        ForgetGpuBuffer(false);
    else if (g_vbo)
        ForgetGpuBuffer(true);
    if (BuildCache())
    {
        failedWorld = nullptr;
        return true;
    }
    failedWorld = world;
    failedGeneration = g_mapGeneration;
    failedTick = GetTickCount();
    return false;
}

// ---------------------------------------------------------------------------
// r_world_batch: bucketed sequential (gl_texsort 0) world renderer.
//
// Builds on the static world VBO/EBO cache above, which is built on demand
// even when r_world_vbo is 0. The top-level R_RecursiveWorldNode call in
// R_DrawWorld is wrapped. While it runs, the R_DrawSequentialPoly callsite
// consumes ordinary surfaces of the exact multitexture path:
//
//   R_RenderDynamicLightmaps(s)            -> called here, same point/order
//   t = R_TextureAnimation(s)              -> called here (pure after init,
//                                             including '-' random tiling)
//   GL_Bind(0, t), TexEnvi MODULATE        -> replayed per bucket
//   GL_Bind(1, lightmap_textures[lm])      -> replayed per lightmap group
//   upload lightmap_rectchange[lm] if dirty -> replayed per lightmap, merged
//   detail helper / program[3] / Begin..End -> no-detail only, one draw
//   DecalSurfaceAdd(s), R_DrawDecals(true) -> replayed per surface after the
//                                             buckets, in traversal order
//
// That branch reads no surface flag other than SKY/TURB/UNDERWATER (early
// branch) and 0x200 (scrolling texcoords), so every other flag combination is
// the same code path and is batched.
//
// When recursion returns, and before R_DrawWorld disables multitexture and
// unbinds the world program, the collected surfaces are drawn as one
// glDrawElements per (animated texture, lightmap block) bucket from a
// streaming index buffer. Buckets are grouped by lightmap block in first-seen
// order, so TMU1 is bound and uploaded once per block and only TMU0 changes
// between the draws of a group. Every other surface keeps stock
// R_DrawSequentialPoly at its traversal point, so stock surfaces now draw
// before batched ones. The world is opaque and depth tested, decals are
// clipped to their own surface and drawn with depth writes off after it, and
// lightmap texels of distinct surfaces are disjoint, so the only observable
// difference is the order of exactly coplanar overlapping world faces (and of
// the decals on them).
// ---------------------------------------------------------------------------

template <typename T>
T& HwGlobal(std::uintptr_t rva)
{
    return *reinterpret_cast<T*>(g_hwBase + rva);
}

bool EnsureBatchStorage()
{
    try
    {
        const std::size_t surfaces = static_cast<std::size_t>(g_numSurfaces);
        if (g_batchEntries.capacity() < surfaces)
            g_batchEntries.reserve(surfaces);
        if (g_batchDecalSurfaces.capacity() < surfaces)
            g_batchDecalSurfaces.reserve(surfaces);
        if (g_batchBuckets.capacity() < static_cast<std::size_t>(kMaxBatchBuckets))
            g_batchBuckets.reserve(static_cast<std::size_t>(kMaxBatchBuckets));
        if (g_batchLightmapOrder.capacity() < static_cast<std::size_t>(kMaxLightmapBlocks))
            g_batchLightmapOrder.reserve(static_cast<std::size_t>(kMaxLightmapBlocks));
        // Every surface is collected at most once per pass, so the whole-map
        // index count bounds the per-pass staging size.
        const std::size_t bytes = g_indexData.size() * WorldIndexBytes();
        if (g_batchStaging.size() < bytes)
            g_batchStaging.resize(bytes);
    }
    catch (...)
    {
        return false;
    }
    return true;
}

bool RejectWorldBatch(std::uint64_t ProfileStats::*counter)
{
    g_batchFrameRejected = true;
    if (g_collectStats)
        ++(g_stats.*counter);
    return false;
}

// The only GL queries of a batched pass, issued back to back so a threaded
// driver synchronizes at most once. Fill mode is the exactness gate for
// drawing fans as GL_TRIANGLES. hw.dll never binds buffers or changes the
// client active texture (those qgl slots are only referenced by the loader),
// and the r_world_vbo sequential path restores both before returning, so the
// captured bindings are still live when the flush restores them.
bool CaptureWorldBatchClientState()
{
    if (!PolygonFillMode())
        return false;
    __try
    {
        const int units = CachedMaxTextureUnits();
        if (units < 2 || units > 32)
            return false;
        g_batchTextureUnits = units;
        g_batchPreviousArrayBuffer = 0;
        g_batchPreviousIndexBuffer = 0;
        g_batchPreviousClientTexture = static_cast<int>(GL_TEXTURE0);
        g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &g_batchPreviousArrayBuffer);
        g_getIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &g_batchPreviousIndexBuffer);
        g_getIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &g_batchPreviousClientTexture);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return g_batchPreviousClientTexture >= static_cast<int>(GL_TEXTURE0) &&
           g_batchPreviousClientTexture <
               static_cast<int>(GL_TEXTURE0) + g_batchTextureUnits;
}

bool BeginWorldBatch()
{
    g_batchCollecting = false;
    g_batchValidating = false;
    g_batchFrameRejected = false;
    g_batchLastSurface = -1;
    g_batchLastBucket = -1;
    g_batchLastDecal = false;
    g_batchCallerFlag = 0;
    g_batchBuckets.clear();
    g_batchEntries.clear();
    g_batchDecalSurfaces.clear();
    g_batchLightmapOrder.clear();

    if (!g_installed || g_batchMode == 0 || !rendererperf::Enabled() ||
        !g_batchCallsiteReady || !g_sequentialCallsitesReady)
        return false;

    // Whole-pass gates: these select a different R_DrawSequentialPoly branch
    // or add per-surface work after qglEnd for every surface.
    auto* entity = HwGlobal<std::uint8_t*>(kCurrentEntityRva);
    if (HwGlobal<int>(kTextureSortModeRva) != 0 || !entity ||
        Read<int>(entity, 0x2F8) != 0 ||
        HwGlobal<int>(kWorldSpecialPassRva) != 0)
        return RejectWorldBatch(&ProfileStats::batchRejectState);
    if (HwGlobal<int>(kSequentialDebugDrawRva) != 0)
        return RejectWorldBatch(&ProfileStats::batchRejectWireframe);
    if (HwGlobal<int>(kLightmapOverrideRva) != 0)
        return RejectWorldBatch(&ProfileStats::batchRejectLightmap);
    if (HwGlobal<int>(kMultitextureUnitsRva) < 2 ||
        HwGlobal<std::uint8_t>(kMultitextureActiveRva) == 0 ||
        !ReadQgl<GlMTexCoord2fFn>(kQglMTexCoord2fRva))
        return RejectWorldBatch(&ProfileStats::batchRejectMultitexture);
    if (!EnsureCache() || !g_drawElements || !g_glBind || !g_disableClientState ||
        !g_drawDecals || !g_conPrintf ||
        !ReadQgl<GlTexEnviFn>(kQglTexEnviRva) ||
        !ReadQgl<GlTexSubImage2DFn>(kQglTexSubImage2DRva) ||
        !EnsureBatchStorage())
        return RejectWorldBatch(&ProfileStats::batchRejectCache);
    if (!CaptureWorldBatchClientState())
        return RejectWorldBatch(&ProfileStats::batchRejectClientState);

    if (++g_batchFrame == 0)
    {
        for (SurfaceInfo& info : g_surfaceInfo)
            info.worldBatchFrame = 0;
        std::memset(g_batchSlotStamp, 0, sizeof(g_batchSlotStamp));
        std::memset(g_batchLightmapStamp, 0, sizeof(g_batchLightmapStamp));
        g_batchFrame = 1;
    }

    if (g_batchMode == 2)
        g_batchValidating = true;
    else
        g_batchCollecting = true;
    if (g_collectStats)
        ++g_stats.batchFrames;
    return true;
}

bool BatchFallback(std::uint64_t ProfileStats::*counter)
{
    g_batchLastSurface = -1;
    if (g_collectStats)
        ++(g_stats.*counter);
    return false;
}

int FindOrAddBatchBucket(int texnum, int lightmap)
{
    // Consecutive traversal surfaces usually share texture and lightmap block.
    if (g_batchLastBucket >= 0)
    {
        const BatchBucket& last =
            g_batchBuckets[static_cast<std::size_t>(g_batchLastBucket)];
        if (last.texnum == texnum && last.lightmap == lightmap)
            return g_batchLastBucket;
    }

    std::uint32_t hash = static_cast<std::uint32_t>(texnum) * 0x9E3779B1u ^
                         static_cast<std::uint32_t>(lightmap) * 0x85EBCA6Bu;
    hash >>= 32 - kBatchSlotBits;
    for (int probe = 0; probe < kBatchSlots; ++probe)
    {
        const std::uint32_t slot = (hash + static_cast<std::uint32_t>(probe)) &
                                   static_cast<std::uint32_t>(kBatchSlots - 1);
        if (g_batchSlotStamp[slot] != g_batchFrame)
        {
            if (g_batchBuckets.size() >= static_cast<std::size_t>(kMaxBatchBuckets))
                return -1;
            const int bucket = static_cast<int>(g_batchBuckets.size());
            g_batchSlotStamp[slot] = g_batchFrame;
            g_batchSlotBucket[slot] = bucket;
            g_batchBuckets.push_back(BatchBucket{texnum, lightmap, -1, 0, 0, 0});
            // Link into its lightmap group, groups keep first-seen order.
            if (g_batchLightmapStamp[lightmap] != g_batchFrame)
            {
                g_batchLightmapStamp[lightmap] = g_batchFrame;
                g_batchLightmapHead[lightmap] = bucket;
                g_batchLightmapOrder.push_back(lightmap);
            }
            else
            {
                g_batchBuckets[static_cast<std::size_t>(
                    g_batchLightmapTail[lightmap])].nextInLightmap = bucket;
            }
            g_batchLightmapTail[lightmap] = bucket;
            g_batchLastBucket = bucket;
            return bucket;
        }
        const int bucket = g_batchSlotBucket[slot];
        const BatchBucket& candidate = g_batchBuckets[static_cast<std::size_t>(bucket)];
        if (candidate.texnum == texnum && candidate.lightmap == lightmap)
        {
            g_batchLastBucket = bucket;
            return bucket;
        }
    }
    return -1;
}

bool CollectWorldBatchSurface(std::uint8_t* surface, int callerFlag)
{
    const int index = SurfaceIndex(surface);
    if (index < 0)
        return BatchFallback(&ProfileStats::batchFallbackUncached);

    const unsigned flags = Read<unsigned>(surface, 0x08);
    if ((flags & kSequentialSpecialFlags) != 0)
        return BatchFallback(&ProfileStats::batchFallbackSpecial);
    if ((flags & kSequentialScrollFlag) != 0)
        return BatchFallback(&ProfileStats::batchFallbackScroll);

    SurfaceInfo& info = g_surfaceInfo[static_cast<std::size_t>(index)];
    if (info.first < 0 || info.count < 3 || !info.poly ||
        info.indexFirst < 0 || info.indexCount < 3 ||
        info.worldBatchFrame == g_batchFrame ||
        Read<std::uint8_t*>(surface, 0x24) != info.poly ||
        Read<std::uint8_t*>(info.poly, 0x00) != nullptr ||
        Read<int>(info.poly, 0x08) != info.count)
        return BatchFallback(&ProfileStats::batchFallbackUncached);

    const int lightmap = Read<int>(surface, 0x38);
    if (lightmap < 0 || lightmap >= kMaxLightmapBlocks)
        return BatchFallback(&ProfileStats::batchFallbackLightmap);

    auto* texinfo = Read<std::uint8_t*>(surface, 0x2C);
    if (!texinfo || Read<int>(texinfo, 0x28) != 0)
        return BatchFallback(&ProfileStats::batchFallbackSpecial);
    auto* base = Read<std::uint8_t*>(texinfo, 0x24);
    if (!base)
        return BatchFallback(&ProfileStats::batchFallbackSpecial);

    // R_TextureAnimation only reads cl.time/currententity->frame and, for '-'
    // random-tiled textures, the surface texturemins and its random table
    // after the one-time init, so calling it here (and again from stock on
    // fallback) is observably identical to the single stock call.
    auto* texture = g_textureAnimation(surface);
    if (!texture)
        return BatchFallback(&ProfileStats::batchFallbackSpecial);

    // Mirror the detail helper's gate: before its one-time load, or when the
    // animated texture owns a detail texture, it changes TMU2 state.
    if (HwGlobal<std::uint8_t>(kDetailEnabledRva) != 0 &&
        HwGlobal<int>(kDetailCvarRva) != 0 &&
        (HwGlobal<std::uint8_t>(kDetailLoadedRva) == 0 ||
         Read<std::uintptr_t>(texture, 0x50) != 0))
        return BatchFallback(&ProfileStats::batchFallbackDetail);

    // Decals are queued and drawn right after qglEnd by stock. The flush
    // replays that tail per surface after the buckets.
    const bool decal = Read<std::uint8_t*>(surface, 0x58) != nullptr;

    if (g_batchValidating)
    {
        // r_world_batch 2: classification only, stock still draws everything.
        g_batchLastSurface = -1;
        if (g_collectStats)
            ++g_stats.batchValidateSurfaces;
        return false;
    }

    if (g_batchEntries.size() >= g_batchEntries.capacity() ||
        (decal && g_batchDecalSurfaces.size() >= g_batchDecalSurfaces.capacity()))
        return BatchFallback(&ProfileStats::batchFallbackOverflow);
    const int texnum = Read<int>(texture, 0x1C);
    const int bucketIndex = FindOrAddBatchBucket(texnum, lightmap);
    if (bucketIndex < 0)
        return BatchFallback(&ProfileStats::batchFallbackOverflow);

    // Exact stock CPU work for this surface: c_brush_polys, lightmap_polys
    // chain, and the dynamic lightmap rebuild that marks the block modified.
    // The matching upload is deferred to the flush of this pass.
    g_renderDynamicLightmaps(surface);

    g_batchEntries.push_back(BatchEntry{info.indexFirst, info.indexCount, bucketIndex});
    g_batchBuckets[static_cast<std::size_t>(bucketIndex)].indexCount += info.indexCount;
    if (decal)
        g_batchDecalSurfaces.push_back(index);
    info.worldBatchFrame = g_batchFrame;
    g_batchLastSurface = index;
    g_batchLastDecal = decal;
    g_batchCallerFlag = callerFlag;
    if (g_collectStats)
    {
        ++g_stats.batchSurfaces;
        if (decal)
            ++g_stats.batchDecalSurfaces;
        if (base[0] == '-')
            ++g_stats.batchRandomSurfaces;
        if ((flags & ~kAllowedSurfaceFlags) != 0)
            ++g_stats.batchFlagSurfaces;
    }
    return true;
}

// Same upload as R_DrawSequentialPoly: full-width rows of the accumulated
// dirty rect, then reset the rect. Called with the block bound on TMU1.
void UploadBatchLightmap(int lightmap)
{
    int* modified = &HwGlobal<int>(kLightmapModifiedRva);
    if (modified[lightmap] == 0)
        return;
    modified[lightmap] = 0;
    int* rect = &HwGlobal<int>(kLightmapRectRva + static_cast<std::uintptr_t>(lightmap) * 16u);
    const int top = rect[1];
    const int height = rect[3];
    const int bytes = HwGlobal<int>(kLightmapBytesRva);
    const int offset = ((lightmap << 7) + top) * bytes << 7;
    auto texSubImage = ReadQgl<GlTexSubImage2DFn>(kQglTexSubImage2DRva);
    texSubImage(GL_TEXTURE_2D, 0, 0, top, kLightmapBlockSize, height,
                static_cast<unsigned>(HwGlobal<int>(kLightmapFormatRva)),
                GL_UNSIGNED_BYTE, g_hwBase + kLightmapDataRva + offset);
    rect[0] = kLightmapBlockSize;
    rect[1] = kLightmapBlockSize;
    rect[3] = 0;
    rect[2] = 0;
    if (g_collectStats)
        ++g_stats.batchLightmapUploads;
}

// Streams this pass's indices into a ring element buffer, orphaning on wrap.
// Returns the byte offset to use as the glDrawElements base, or a client
// pointer (with no element buffer bound) if no buffer object is available.
std::uintptr_t UploadBatchIndices(std::size_t bytes)
{
    if (!g_batchIbo)
    {
        unsigned id = 0;
        g_genBuffers(1, &id);
        g_batchIbo = id;
        g_batchIboCapacity = 0;
        g_batchIboOffset = 0;
    }
    if (!g_batchIbo)
    {
        g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        return reinterpret_cast<std::uintptr_t>(g_batchStaging.data());
    }

    g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_batchIbo);
    if (!g_bufferSubData)
    {
        g_bufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<std::ptrdiff_t>(bytes),
                     g_batchStaging.data(), GL_STREAM_DRAW);
        if (g_collectStats)
            ++g_stats.batchIndexOrphans;
        return 0;
    }

    if (bytes > g_batchIboCapacity)
    {
        std::size_t capacity = g_batchIboCapacity ? g_batchIboCapacity : kBatchIboMinBytes;
        while (capacity < bytes * 4u)
            capacity *= 2u;
        g_bufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<std::ptrdiff_t>(capacity),
                     nullptr, GL_STREAM_DRAW);
        g_batchIboCapacity = capacity;
        g_batchIboOffset = 0;
        if (g_collectStats)
            ++g_stats.batchIndexOrphans;
    }
    else if (g_batchIboOffset + bytes > g_batchIboCapacity)
    {
        g_bufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<std::ptrdiff_t>(g_batchIboCapacity),
                     nullptr, GL_STREAM_DRAW);
        g_batchIboOffset = 0;
        if (g_collectStats)
            ++g_stats.batchIndexOrphans;
    }
    const std::size_t offset = g_batchIboOffset;
    g_bufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<std::ptrdiff_t>(offset),
                    static_cast<std::ptrdiff_t>(bytes), g_batchStaging.data());
    g_batchIboOffset = (offset + bytes + 15u) & ~static_cast<std::size_t>(15u);
    return offset;
}

// Assigns each bucket its element range in draw order (lightmap groups in
// first-seen order, buckets in first-seen order inside a group), then copies
// every collected surface's static fan range in one sequential pass. Returns
// the total element count.
std::size_t BuildBatchIndices()
{
    std::size_t total = 0;
    for (const int lightmap : g_batchLightmapOrder)
    {
        for (int b = g_batchLightmapHead[lightmap]; b >= 0;
             b = g_batchBuckets[static_cast<std::size_t>(b)].nextInLightmap)
        {
            BatchBucket& bucket = g_batchBuckets[static_cast<std::size_t>(b)];
            bucket.firstElement = static_cast<int>(total);
            bucket.cursor = bucket.firstElement;
            total += static_cast<std::size_t>(bucket.indexCount);
        }
    }
    // Every surface is collected once per pass, so this cannot exceed the
    // whole-map index count the staging buffer was sized for.
    if (total * WorldIndexBytes() > g_batchStaging.size())
        return 0;

    BatchBucket* buckets = g_batchBuckets.data();
    if (g_index16)
    {
        const std::uint16_t* source = g_indexData16.data();
        auto* staging = reinterpret_cast<std::uint16_t*>(g_batchStaging.data());
        for (const BatchEntry& entry : g_batchEntries)
        {
            BatchBucket& bucket = buckets[entry.bucket];
            std::memcpy(staging + bucket.cursor, source + entry.indexFirst,
                        static_cast<std::size_t>(entry.indexCount) * sizeof(std::uint16_t));
            bucket.cursor += entry.indexCount;
        }
    }
    else
    {
        const std::uint32_t* source = g_indexData.data();
        auto* staging = reinterpret_cast<std::uint32_t*>(g_batchStaging.data());
        for (const BatchEntry& entry : g_batchEntries)
        {
            BatchBucket& bucket = buckets[entry.bucket];
            std::memcpy(staging + bucket.cursor, source + entry.indexFirst,
                        static_cast<std::size_t>(entry.indexCount) * sizeof(std::uint32_t));
            bucket.cursor += entry.indexCount;
        }
    }
    return total;
}

void ApplyBatchFinalTexcoords(int surfaceIndex)
{
    const SurfaceInfo& info = g_surfaceInfo[static_cast<std::size_t>(surfaceIndex)];
    const float* v = reinterpret_cast<const float*>(info.poly + 0x10) +
                     (info.count - 1) * 7;
    auto mtexCoord = ReadQgl<GlMTexCoord2fFn>(kQglMTexCoord2fRva);
    mtexCoord(GL_TEXTURE0, v[3], v[4]);
    mtexCoord(GL_TEXTURE0 + 1u, v[5], v[6]);
}

void UnbindProgramForCallerFlag()
{
    if (g_batchCallerFlag != 0 && HwGlobal<int>(kBoundProgramRva) != 0)
    {
        HwGlobal<int>(kBoundProgramRva) = 0;
        if (auto useProgram = ReadQgl<GlUseProgramFn>(kQglUseProgramRva))
            useProgram(0);
    }
}

// Exact post-qglEnd decal tail of R_DrawSequentialPoly for each batched decal
// surface, in traversal order: the world program is still bound there, the
// surface is queued (DecalSurfaceAdd, inlined by hw), R_DrawDecals(true) runs
// because the gated rendermode is normal, then the caller-flag program unbind.
// R_DrawDecals binds its own textures, the surface lightmap block is already
// uploaded, and it empties the queue again, so the queue is empty here exactly
// as it is after every stock decal surface.
void ReplayBatchDecals(unsigned worldProgram)
{
    int& count = HwGlobal<int>(kDecalSurfaceCountRva);
    auto** queue = &HwGlobal<std::uint8_t*>(kDecalSurfacesRva);
    for (const int index : g_batchDecalSurfaces)
    {
        g_bindWorldProgram(worldProgram);
        // Immediate mode leaves the surface's last texcoords current before
        // its decals, the replay of the final traversal surface reproduces it.
        if (index == g_batchLastSurface)
            ApplyBatchFinalTexcoords(index);

        if (count >= kMaxDecalSurfaces)
        {
            g_conPrintf(reinterpret_cast<const char*>(g_hwBase + kDecalOverflowTextRva));
        }
        else
        {
            const float limit = HwGlobal<std::uint8_t>(kDecalLimitOverrideRva) != 0
                ? HwGlobal<float>(kDecalLimitOverrideValueRva)
                : HwGlobal<float>(kDecalLimitValueRva);
            if (count < _mm_cvtt_ss2si(_mm_set_ss(limit)))
                queue[count++] = g_surfaces + static_cast<std::ptrdiff_t>(index) * kSurfaceSize;
        }
        g_drawDecals(1);
        UnbindProgramForCallerFlag();
    }
}

void FlushWorldBatch()
{
    if (g_batchBuckets.empty())
        return;

    const std::size_t indexBytes = WorldIndexBytes();
    const std::size_t total = BuildBatchIndices();
    if (total == 0)
        return; // unreachable, see BuildBatchIndices

    // Client arrays for the retained draw. Immediate mode ignores every client
    // array, so foreign arrays are disabled inside the push instead of being
    // queried, and the pop restores them.
    g_pushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    g_disableClientState(GL_NORMAL_ARRAY);
    g_disableClientState(GL_COLOR_ARRAY);
    g_disableClientState(GL_INDEX_ARRAY);
    g_disableClientState(GL_EDGE_FLAG_ARRAY);
    g_disableClientState(GL_FOG_COORDINATE_ARRAY);
    g_disableClientState(GL_SECONDARY_COLOR_ARRAY);
    for (int unit = 2; unit < g_batchTextureUnits; ++unit)
    {
        g_clientActiveTexture(GL_TEXTURE0 + static_cast<unsigned>(unit));
        g_disableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    g_bindBuffer(GL_ARRAY_BUFFER, g_vbo);
    g_enableClientState(GL_VERTEX_ARRAY);
    g_vertexPointer(3, GL_FLOAT, kVertexStride, reinterpret_cast<const void*>(0));
    g_clientActiveTexture(GL_TEXTURE0);
    g_enableClientState(GL_TEXTURE_COORD_ARRAY);
    g_texCoordPointer(2, GL_FLOAT, kVertexStride,
                      reinterpret_cast<const void*>(3 * sizeof(float)));
    g_clientActiveTexture(GL_TEXTURE0 + 1u);
    g_enableClientState(GL_TEXTURE_COORD_ARRAY);
    g_texCoordPointer(2, GL_FLOAT, kVertexStride,
                      reinterpret_cast<const void*>(5 * sizeof(float)));
    g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(g_batchPreviousArrayBuffer));

    const std::uintptr_t indexBase = UploadBatchIndices(total * indexBytes);

    // Material sequence of the stock multitexture branch. GL_Bind keeps hw's
    // per-unit bind/enable/active-unit caches coherent (redundant binds issue
    // no GL call) and the qgl slots keep gl_state's filters coherent, so code
    // after the pass observes consistent state. Per lightmap group TMU1 is
    // bound and uploaded once, then only TMU0 changes between draws.
    g_batchFlushing = true;
    auto texEnvi = ReadQgl<GlTexEnviFn>(kQglTexEnviRva);
    const int* lightmapTextures = &HwGlobal<int>(kLightmapTexturesRva);
    const unsigned worldProgram = HwGlobal<unsigned>(kWorldProgramRva);
    const unsigned indexType = WorldIndexType();
    bool first = true;
    for (const int lightmap : g_batchLightmapOrder)
    {
        g_glBind(1, lightmapTextures[lightmap]);
        UploadBatchLightmap(lightmap);
        if (g_collectStats)
            ++g_stats.batchLightmapGroups;
        for (int b = g_batchLightmapHead[lightmap]; b >= 0;
             b = g_batchBuckets[static_cast<std::size_t>(b)].nextInLightmap)
        {
            const BatchBucket& bucket = g_batchBuckets[static_cast<std::size_t>(b)];
            if (bucket.indexCount <= 0)
                continue;
            g_glBind(0, bucket.texnum);
            if (first)
            {
                // TMU0 env is not touched between buckets, set it once.
                texEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                g_bindWorldProgram(worldProgram);
                first = false;
            }
            const std::uintptr_t start =
                indexBase + static_cast<std::uintptr_t>(bucket.firstElement) * indexBytes;
            g_drawElements(GL_TRIANGLES, bucket.indexCount, indexType,
                           reinterpret_cast<const void*>(start));
            if (g_collectStats)
                ++g_stats.batchDraws;
        }
    }

    g_popClientAttrib();
    g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<unsigned>(g_batchPreviousIndexBuffer));
    g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(g_batchPreviousArrayBuffer));
    g_clientActiveTexture(static_cast<unsigned>(g_batchPreviousClientTexture));

    ReplayBatchDecals(worldProgram);

    // When the final traversal surface was batched without decals, reproduce
    // its stock end state: TMU0/TMU1 bindings with TMU1 active, the world
    // program, and the last emitted texcoords that immediate mode leaves
    // current. GL_Bind and the program bind are cached, so this is free when
    // nothing changed after its bucket. With decals, the replay above already
    // ended exactly as stock does.
    if (g_batchLastSurface >= 0 && !g_batchLastDecal && g_batchLastBucket >= 0)
    {
        const BatchBucket& last = g_batchBuckets[static_cast<std::size_t>(g_batchLastBucket)];
        g_glBind(0, last.texnum);
        g_glBind(1, lightmapTextures[last.lightmap]);
        g_bindWorldProgram(worldProgram);
        ApplyBatchFinalTexcoords(g_batchLastSurface);
    }
    // R_DrawSequentialPoly's post-qglEnd program unbind for a nonzero caller
    // flag (constant for the whole recursion).
    UnbindProgramForCallerFlag();
    g_batchFlushing = false;

    if (g_collectStats)
    {
        ++g_stats.batchFlushes;
        g_stats.batchIndices += static_cast<std::uint64_t>(total);
    }
}

void EndWorldBatch()
{
    if (g_batchCollecting)
        FlushWorldBatch();
    g_batchCollecting = false;
    g_batchValidating = false;
    g_batchFrameRejected = false;
    g_batchBuckets.clear();
    g_batchEntries.clear();
    g_batchDecalSurfaces.clear();
    g_batchLightmapOrder.clear();
}

void __fastcall RecursiveWorldNode_Hook(std::uint8_t* node, int flag)
{
    const bool batching = BeginWorldBatch();
    g_recursiveWorldNode(node, flag);
    if (batching || g_batchFrameRejected)
        EndWorldBatch();
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

    // Per-scope work is hoisted into Begin{World,Brush}Scope, which already
    // ran ForeignClientArraysClear(2), saved the client-active texture and
    // ARRAY_BUFFER binding, and pushed GL_CLIENT_VERTEX_ARRAY_BIT. hw.dll never
    // calls qglClientActiveTexture/qglBindBuffer (their qgl slots are only
    // referenced by the loader table), so those saved values are still the
    // live ones here, and the scope pop restores the borrowed TMU2 pointer.
    int previousClientTexture = static_cast<int>(GL_TEXTURE0);
    int previousArrayBuffer = 0;
    if (g_brushScopeReady)
    {
        previousClientTexture = g_brushPreviousClientTexture;
        previousArrayBuffer = g_brushPreviousArrayBuffer;
    }
    else if (g_worldScopeReady)
    {
        previousClientTexture = g_worldPreviousClientTexture;
        previousArrayBuffer = g_worldPreviousArrayBuffer;
    }
    else
    {
        return false;
    }

    bool borrowed = false;
    __try
    {
        const int maxTextureUnits = CachedMaxTextureUnits();
        if (maxTextureUnits < 3 || maxTextureUnits > 32)
            return false;
        if (previousClientTexture < static_cast<int>(GL_TEXTURE0) ||
            previousClientTexture >= static_cast<int>(GL_TEXTURE0) + maxTextureUnits)
            return false;

        // glTexCoordPointer captures the VBO binding. The server active texture
        // is never touched here, Gold's detail helper has already left it in the
        // exact state expected by the code after qglEnd.
        g_bindBuffer(GL_ARRAY_BUFFER, g_vbo);
        g_clientActiveTexture(GL_TEXTURE0 + 2u);
        borrowed = true;
        if (g_isEnabled(GL_TEXTURE_COORD_ARRAY))
        {
            g_clientActiveTexture(static_cast<unsigned>(previousClientTexture));
            g_bindBuffer(GL_ARRAY_BUFFER,
                         static_cast<unsigned>(previousArrayBuffer));
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

        // Disable the borrowed array so ordinary surfaces never consume it.
        g_clientActiveTexture(GL_TEXTURE0 + 2u);
        g_disableClientState(GL_TEXTURE_COORD_ARRAY);
        g_clientActiveTexture(static_cast<unsigned>(previousClientTexture));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        __try
        {
            if (borrowed)
            {
                g_clientActiveTexture(GL_TEXTURE0 + 2u);
                g_disableClientState(GL_TEXTURE_COORD_ARRAY);
            }
            g_bindBuffer(GL_ARRAY_BUFFER,
                         static_cast<unsigned>(previousArrayBuffer));
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
    if (g_collectStats && g_batchCollecting && !g_batchFlushing)
        ++g_stats.batchStockLightmapUploads;
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
    // The new world can reuse the old model pointer; drop cached leaves.
    viscache::BeginFrame();
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
    g_batchIbo = 0;
    g_batchIboCapacity = 0;
    g_batchIboOffset = 0;
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
    g_recursiveWorldNode =
        reinterpret_cast<RecursiveWorldNodeFn>(g_hwBase + kRecursiveWorldNodeRva);
    g_textureAnimation =
        reinterpret_cast<TextureAnimationFn>(g_hwBase + kTextureAnimationRva);
    g_renderDynamicLightmaps =
        reinterpret_cast<RenderDynamicLightmapsFn>(g_hwBase + kRenderDynamicLightmapsRva);
    g_bindWorldProgram =
        reinterpret_cast<BindWorldProgramFn>(g_hwBase + kBindWorldProgramRva);
    g_drawDecals = reinterpret_cast<DrawDecalsFn>(g_hwBase + kDrawDecalsRva);
    g_conPrintf = reinterpret_cast<ConPrintfFn>(g_hwBase + kConPrintfRva);

    __try
    {
        g_cvar = engine->pfnRegisterVariable("r_world_vbo", "0", 0);
        g_brushCvar =
            engine->pfnRegisterVariable("r_world_brush_vbo", "1", 0);
        g_batchCvar = engine->pfnRegisterVariable("r_world_batch", "0", 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_cvar = nullptr;
        g_brushCvar = nullptr;
        g_batchCvar = nullptr;
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

    // r_world_batch flushes right after the top-level world recursion, before
    // R_DrawWorld disables multitexture. Without this callsite it stays off.
    g_batchCallsiteReady =
        g_sequentialCallsitesReady &&
        PatchCall(g_hwBase + kRecursiveWorldCallRva,
                  reinterpret_cast<void*>(g_recursiveWorldNode),
                  reinterpret_cast<void*>(&RecursiveWorldNode_Hook));
    rendererlog::Line("worldbatch: %s (recursion callsite=%s, buffer_subdata=%s)",
                      g_batchCallsiteReady ? "available" : "unavailable",
                      g_batchCallsiteReady ? "yes" : "no",
                      g_bufferSubData ? "yes" : "no");

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
    int batchMode = 0;
    if (g_batchCvar)
    {
        __try
        {
            const float value = g_batchCvar->value;
            if (value == 1.0f) batchMode = 1;
            else if (value == 2.0f) batchMode = 2;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            batchMode = 0;
        }
    }
    g_batchMode = batchMode;
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
    if (!EnsureCache() || !g_clientActiveTexture)
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
    // GL state queries last: they are the expensive checks.
    if (!PolygonFillMode() || !ForeignClientArraysClear(2))
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
    // Called once per profile report. The shared report line predates these
    // counters, so emit them here, only while periodic stats are enabled.
    if (rendererlog::StatsEnabled() &&
        (out.batchFrames || out.batchFallbackFrame ||
         out.batchRejectState || out.batchRejectWireframe ||
         out.batchRejectLightmap || out.batchRejectMultitexture ||
         out.batchRejectCache || out.batchRejectClientState))
    {
        rendererlog::Line("PROFILE worldbatch: mode=%d frames=%llu flushes=%llu surfaces=%llu "
                   "draws=%llu indices=%llu orphans=%llu lm_merged=%llu lm_stock=%llu "
                   "validate=%llu fb_uncached=%llu fb_special=%llu fb_scroll=%llu "
                   "fb_flags=%llu fb_decal=%llu fb_detail=%llu fb_random=%llu "
                   "fb_lightmap=%llu fb_overflow=%llu fb_frame=%llu rej_state=%llu "
                   "rej_wire=%llu rej_lmoverride=%llu rej_mtex=%llu rej_cache=%llu "
                   "rej_client=%llu lm_groups=%llu decal_surfs=%llu random_surfs=%llu "
                   "flag_surfs=%llu",
                   g_batchMode,
                   static_cast<unsigned long long>(out.batchFrames),
                   static_cast<unsigned long long>(out.batchFlushes),
                   static_cast<unsigned long long>(out.batchSurfaces),
                   static_cast<unsigned long long>(out.batchDraws),
                   static_cast<unsigned long long>(out.batchIndices),
                   static_cast<unsigned long long>(out.batchIndexOrphans),
                   static_cast<unsigned long long>(out.batchLightmapUploads),
                   static_cast<unsigned long long>(out.batchStockLightmapUploads),
                   static_cast<unsigned long long>(out.batchValidateSurfaces),
                   static_cast<unsigned long long>(out.batchFallbackUncached),
                   static_cast<unsigned long long>(out.batchFallbackSpecial),
                   static_cast<unsigned long long>(out.batchFallbackScroll),
                   static_cast<unsigned long long>(out.batchFallbackFlags),
                   static_cast<unsigned long long>(out.batchFallbackDecal),
                   static_cast<unsigned long long>(out.batchFallbackDetail),
                   static_cast<unsigned long long>(out.batchFallbackRandom),
                   static_cast<unsigned long long>(out.batchFallbackLightmap),
                   static_cast<unsigned long long>(out.batchFallbackOverflow),
                   static_cast<unsigned long long>(out.batchFallbackFrame),
                   static_cast<unsigned long long>(out.batchRejectState),
                   static_cast<unsigned long long>(out.batchRejectWireframe),
                   static_cast<unsigned long long>(out.batchRejectLightmap),
                   static_cast<unsigned long long>(out.batchRejectMultitexture),
                   static_cast<unsigned long long>(out.batchRejectCache),
                   static_cast<unsigned long long>(out.batchRejectClientState),
                   static_cast<unsigned long long>(out.batchLightmapGroups),
                   static_cast<unsigned long long>(out.batchDecalSurfaces),
                   static_cast<unsigned long long>(out.batchRandomSurfaces),
                   static_cast<unsigned long long>(out.batchFlagSurfaces));
    }
    return out;
}
} // namespace worldvbo
