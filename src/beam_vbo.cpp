#include "beam_vbo.h"

#include "hw_build.h"
#include "log.h"
#include "perf_control.h"
#include "world_vbo.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace beamvbo
{
namespace
{
// Exact GoldClient BEAMPOINTS/type-0 callsites.
constexpr std::uintptr_t kBeamBeginCallRva = 0x0008FAC8u;
constexpr std::uintptr_t kBeamEndCallRva   = 0x0008FB07u;
constexpr std::uintptr_t kBeamRenderModeCallRva = 0x0008F6BEu;
constexpr std::uintptr_t kBeamSpriteTextureCallRva = 0x0008F886u;
constexpr std::uintptr_t kBeamFinalDepthMaskCallRva = 0x0008FDD3u;
constexpr std::uintptr_t kBeamFinalRenderModeCallRva = 0x0008FDE3u;

constexpr std::uintptr_t kBrightnessCalls[] = {
    0x0008D4AEu, 0x0008D4E8u, 0x0008D979u, 0x0008D9AEu};
constexpr std::uintptr_t kTexCoordCalls[] = {
    0x0008D4CBu, 0x0008D500u, 0x0008D991u, 0x0008D9C6u};
constexpr std::uintptr_t kVertexCalls[] = {
    0x0008D4D5u, 0x0008D50Au, 0x0008D99Bu, 0x0008D9D0u};

// Exact GoldClient tracer callsites inside hw+0x8B750.  Tracer simulation,
// clipping, list iteration and state setup remain stock, only the emitted
// TRI_QUADS stream is captured.
constexpr std::uintptr_t kTracerSpriteTextureCallRva = 0x0008B7F1u;
constexpr std::uintptr_t kTracerRenderModeCallRva    = 0x0008B87Du;
constexpr std::uintptr_t kTracerBeginCallRva         = 0x0008BCCCu;
constexpr std::uintptr_t kTracerEndCallRva           = 0x0008BE6Fu;
constexpr std::uintptr_t kTracerFinalCullFaceCallRva = 0x0008BF3Fu;
constexpr std::uintptr_t kTracerFinalRenderModeCallRva = 0x0008BF4Au;
constexpr std::uintptr_t kTracerBrightnessCalls[] = {
    0x0008BCF6u, 0x0008BD56u, 0x0008BDB6u, 0x0008BE16u};
constexpr std::uintptr_t kTracerTexCoordCalls[] = {
    0x0008BD0Bu, 0x0008BD6Bu, 0x0008BDCBu, 0x0008BE2Bu};
constexpr std::uintptr_t kTracerVertexCalls[] = {
    0x0008BD46u, 0x0008BDA6u, 0x0008BE06u, 0x0008BE66u};

// TriangleAPI callback slots.
constexpr std::uintptr_t kTriRenderModeSlotRva = 0x0272BD4u;
constexpr std::uintptr_t kTriBeginSlotRva      = 0x0272BD8u;
constexpr std::uintptr_t kTriEndSlotRva        = 0x0272BDCu;
constexpr std::uintptr_t kTriColor4ubSlotRva   = 0x0272BE4u;
constexpr std::uintptr_t kTriTexCoordSlotRva   = 0x0272BE8u;
constexpr std::uintptr_t kTriVertex3fvSlotRva  = 0x0272BECu;
constexpr std::uintptr_t kTriVertex3fSlotRva   = 0x0272BF0u;
constexpr std::uintptr_t kTriBrightnessSlotRva = 0x0272BF4u;
constexpr std::uintptr_t kTriCullFaceSlotRva   = 0x0272BF8u;
constexpr std::uintptr_t kTriSpriteTextureSlotRva = 0x0272BFCu;

// Preserve TriangleAPI Begin's exact 0x9E423..0x9E44A preamble by patching only
// its eventual qglBegin FF15 call.
constexpr std::uintptr_t kTriBeginQglCallRva = 0x0009E45Bu;

constexpr std::uintptr_t kQglBeginRva               = 0x027E3DE0u;
constexpr std::uintptr_t kQglEndRva                 = 0x027E3DD8u;
constexpr std::uintptr_t kQglColor4fRva             = 0x027E3DD4u;
constexpr std::uintptr_t kQglDepthMaskRva           = 0x027E376Cu;
constexpr std::uintptr_t kQglTexEnviRva             = 0x027E3744u;
constexpr std::uintptr_t kQglBlendFuncRva            = 0x027E3DE8u;
constexpr std::uintptr_t kQglEnableRva               = 0x027E3DFCu;
constexpr std::uintptr_t kQglDisableRva              = 0x027E3DF8u;
constexpr std::uintptr_t kQglShadeModelRva           = 0x027E37CCu;
constexpr std::uintptr_t kQglTexCoord2fRva          = 0x027E3778u;
constexpr std::uintptr_t kQglVertex3fvRva           = 0x027E37D0u;
constexpr std::uintptr_t kQglVertex3fRva             = 0x027E3798u;
constexpr std::uintptr_t kQglDrawArraysRva          = 0x027E3CECu;
constexpr std::uintptr_t kQglEnableClientStateRva   = 0x027E3CD4u;
constexpr std::uintptr_t kQglVertexPointerRva       = 0x027E3958u;
constexpr std::uintptr_t kQglTexCoordPointerRva     = 0x027E39E4u;
constexpr std::uintptr_t kQglColorPointerRva        = 0x027E3D0Cu;
constexpr std::uintptr_t kQglPushClientAttribRva    = 0x027E3B04u;
constexpr std::uintptr_t kQglPopClientAttribRva     = 0x027E3B14u;
constexpr std::uintptr_t kQglGetIntegervRva         = 0x027E3DD0u;
constexpr std::uintptr_t kQglGetFloatvRva           = 0x027E37DCu;
constexpr std::uintptr_t kQglIsEnabledRva           = 0x027E3DF4u;
constexpr std::uintptr_t kQglClientActiveTextureRva = 0x027E386Cu;
constexpr std::uintptr_t kQglBindBufferRva          = 0x027E3DB8u;
constexpr std::uintptr_t kQglDeleteBuffersRva       = 0x027E3DC8u;
constexpr std::uintptr_t kQglGenBuffersRva          = 0x027E3DCCu;
constexpr std::uintptr_t kQglBufferDataRva          = 0x027E3DB4u;
constexpr std::uintptr_t kSdlGetProcAddressIatRva   = 0x001FA330u;

// TriangleAPI color globals used by exact Brightness().
constexpr std::uintptr_t kTriAlphaRva = 0x029F58A4u;
constexpr std::uintptr_t kTriBlueRva  = 0x029F58A8u;
constexpr std::uintptr_t kTriGreenRva = 0x029F58ACu;
constexpr std::uintptr_t kTriRedRva   = 0x029F58B0u;

constexpr unsigned GL_QUADS                   = 0x0007u;
constexpr unsigned GL_FLOAT                   = 0x1406u;
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
constexpr unsigned GL_ARRAY_BUFFER_BINDING    = 0x8894u;
constexpr unsigned GL_STREAM_DRAW             = 0x88E0u;
constexpr unsigned GL_CLIENT_VERTEX_ARRAY_BIT = 0x00000002u;
constexpr unsigned GL_CURRENT_COLOR           = 0x0B00u;
constexpr unsigned GL_CURRENT_TEXTURE_COORDS  = 0x0B03u;

using TriRenderModeFn = void (__cdecl*)(int mode);
using TriBeginFn = void (__cdecl*)(int primitive);
using TriEndFn = void (__cdecl*)();
using TriColor4ubFn = void (__cdecl*)(unsigned char r, unsigned char g,
                                     unsigned char b, unsigned char a);
using TriBrightnessFn = void (__cdecl*)(float brightness);
using TriTexCoord2fFn = void (__cdecl*)(float s, float t);
using TriVertex3fvFn = void (__cdecl*)(const float* vertex);
using TriVertex3fFn = void (__cdecl*)(float x, float y, float z);
using TriCullFaceFn = void (__cdecl*)(int style);
using TriSpriteTextureFn = int (__cdecl*)(void* spriteModel, int frame);

using GlBeginFn = void (WINAPI*)(unsigned mode);
using GlEndFn = void (WINAPI*)();
using GlColor4fFn = void (WINAPI*)(float r, float g, float b, float a);
using GlDepthMaskFn = void (WINAPI*)(unsigned char flag);
using GlTexEnviFn = void (WINAPI*)(unsigned target, unsigned pname, int param);
using GlBlendFuncFn = void (WINAPI*)(unsigned src, unsigned dst);
using GlEnableFn = void (WINAPI*)(unsigned cap);
using GlDisableFn = void (WINAPI*)(unsigned cap);
using GlShadeModelFn = void (WINAPI*)(unsigned mode);
using GlTexCoord2fFn = void (WINAPI*)(float s, float t);
using GlVertex3fvFn = void (WINAPI*)(const float* vertex);
using GlVertex3fFn = void (WINAPI*)(float x, float y, float z);
using GlDrawArraysFn = void (WINAPI*)(unsigned mode, int first, int count);
using GlEnableClientStateFn = void (WINAPI*)(unsigned array);
using GlVertexPointerFn = void (WINAPI*)(int size, unsigned type, int stride,
                                        const void* pointer);
using GlTexCoordPointerFn = void (WINAPI*)(int size, unsigned type, int stride,
                                          const void* pointer);
using GlColorPointerFn = void (WINAPI*)(int size, unsigned type, int stride,
                                       const void* pointer);
using GlPushClientAttribFn = void (WINAPI*)(unsigned mask);
using GlPopClientAttribFn = void (WINAPI*)();
using GlGetIntegervFn = void (WINAPI*)(unsigned pname, int* params);
using GlGetFloatvFn = void (WINAPI*)(unsigned pname, float* params);
using GlIsEnabledFn = unsigned char (WINAPI*)(unsigned cap);
using GlClientActiveTextureFn = void (WINAPI*)(unsigned texture);
using GlBindBufferFn = void (WINAPI*)(unsigned target, unsigned buffer);
using GlDeleteBuffersFn = void (WINAPI*)(int count, const unsigned* buffers);
using GlGenBuffersFn = void (WINAPI*)(int count, unsigned* buffers);
using GlBufferDataFn = void (WINAPI*)(unsigned target, std::ptrdiff_t size,
                                     const void* data, unsigned usage);
using GlBufferSubDataFn = void (WINAPI*)(unsigned target, std::ptrdiff_t offset,
                                        std::ptrdiff_t size, const void* data);
using SdlGetProcAddressFn = void* (__cdecl*)(const char* name);

struct BeamVertex
{
    float xyz[3];
    float st[2];
    float rgba[4];
};

struct CallPatch
{
    std::uint8_t* address = nullptr;
    std::uint8_t original[6]{};
    void* replacement = nullptr;
    std::uintptr_t expectedSlotRva = 0;
};

std::uint8_t* g_hwBase = nullptr;
HMODULE g_selfModule = nullptr;
cvar_t* g_cvar = nullptr;
cvar_t* g_tracerCvar = nullptr;
bool g_installed = false;
bool g_tracerInstalled = false;
bool g_enabled = false;
bool g_tracerEnabled = false;
bool g_armBegin = false;
bool g_capture = false;
bool g_collectStats = false;
bool g_lastArrayReject = false;

enum class CaptureKind : std::uint8_t
{
    None,
    Beam,
    Tracer
};

CaptureKind g_armKind = CaptureKind::None;
CaptureKind g_captureKind = CaptureKind::None;
CaptureKind g_batchKind = CaptureKind::None;

enum class TracerPassReject : std::uint8_t
{
    None,
    Slots,
    Buffer,
    Arrays
};

bool g_tracerPassValidated = false;
bool g_tracerPassReady = false;
bool g_tracerFinalAttrsValid = false;
TracerPassReject g_tracerPassReject = TracerPassReject::None;

TriRenderModeFn g_triRenderMode = nullptr;
TriBeginFn g_triBegin = nullptr;
TriEndFn g_triEnd = nullptr;
TriColor4ubFn g_triColor4ub = nullptr;
TriBrightnessFn g_triBrightness = nullptr;
TriTexCoord2fFn g_triTexCoord2f = nullptr;
TriVertex3fvFn g_triVertex3fv = nullptr;
TriVertex3fFn g_triVertex3f = nullptr;
TriCullFaceFn g_triCullFace = nullptr;
TriSpriteTextureFn g_triSpriteTexture = nullptr;

GlBeginFn g_qglBegin = nullptr;
GlEndFn g_qglEnd = nullptr;
GlColor4fFn g_color4f = nullptr;
GlDepthMaskFn g_qglDepthMaskOriginal = nullptr;
GlTexEnviFn g_texEnvi = nullptr;
GlBlendFuncFn g_blendFunc = nullptr;
GlEnableFn g_enable = nullptr;
GlDisableFn g_disable = nullptr;
GlShadeModelFn g_shadeModel = nullptr;
GlTexCoord2fFn g_texCoord2f = nullptr;
GlVertex3fvFn g_vertex3fv = nullptr;
GlVertex3fFn g_vertex3f = nullptr;
GlDrawArraysFn g_drawArrays = nullptr;
GlEnableClientStateFn g_enableClientState = nullptr;
GlVertexPointerFn g_vertexPointer = nullptr;
GlTexCoordPointerFn g_texCoordPointer = nullptr;
GlColorPointerFn g_colorPointer = nullptr;
GlPushClientAttribFn g_pushClientAttrib = nullptr;
GlPopClientAttribFn g_popClientAttrib = nullptr;
GlGetIntegervFn g_getIntegerv = nullptr;
GlGetFloatvFn g_getFloatv = nullptr;
GlIsEnabledFn g_isEnabled = nullptr;
GlClientActiveTextureFn g_clientActiveTexture = nullptr;
GlBindBufferFn g_bindBuffer = nullptr;
GlDeleteBuffersFn g_deleteBuffers = nullptr;
GlGenBuffersFn g_genBuffers = nullptr;
GlBufferDataFn g_bufferData = nullptr;
GlBufferSubDataFn g_bufferSubData = nullptr;

std::vector<BeamVertex> g_vertices;
float g_currentColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
float g_currentTex[2]{};
std::size_t g_currentBeamStart = 0;
bool g_batchPending = false;
int g_currentRenderMode = -1;
bool g_renderModeOwned = false;
void* g_currentSprite = nullptr;
int g_currentSpriteFrame = -1;
int g_batchRenderMode = -1;
void* g_batchSprite = nullptr;
int g_batchSpriteFrame = -1;
unsigned g_vbos[3]{};
unsigned g_vboFrame = 0;
std::size_t g_vboCapacity = 512u * 1024u;
std::uint32_t g_contextGeneration = 0;
ProfileStats g_stats{};

cvar_t* RegisterCvarSafe(cl_enginefunc_t* engine)
{
    if (!engine || !engine->pfnRegisterVariable)
        return nullptr;
    __try
    {
        return engine->pfnRegisterVariable("r_beam_vbo", "1", 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

cvar_t* RegisterTracerCvarSafe(cl_enginefunc_t* engine)
{
    if (!engine || !engine->pfnRegisterVariable)
        return nullptr;
    __try
    {
        return engine->pfnRegisterVariable("r_tracer_vbo", "0", 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

template <typename Fn>
Fn ReadPtr(std::uintptr_t rva)
{
    return g_hwBase ? *reinterpret_cast<Fn*>(g_hwBase + rva) : nullptr;
}

bool VerifyIndirectCall(const CallPatch& patch)
{
    if (!patch.address || patch.address[0] != 0xFF || patch.address[1] != 0x15)
        return false;
    const std::uintptr_t operand =
        static_cast<std::uintptr_t>(
            *reinterpret_cast<const std::uint32_t*>(patch.address + 2));
    return operand ==
        reinterpret_cast<std::uintptr_t>(g_hwBase + patch.expectedSlotRva);
}

bool WriteDirectCall(CallPatch& patch)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(patch.address, 6, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    std::memcpy(patch.original, patch.address, sizeof(patch.original));
    patch.address[0] = 0xE8;
    *reinterpret_cast<std::int32_t*>(patch.address + 1) =
        static_cast<std::int32_t>(
            reinterpret_cast<std::uint8_t*>(patch.replacement) -
            (patch.address + 5));
    patch.address[5] = 0x90;
    DWORD ignored = 0;
    VirtualProtect(patch.address, 6, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), patch.address, 6);
    return true;
}

void RestorePatch(CallPatch& patch)
{
    if (!patch.address)
        return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(patch.address, 6, PAGE_EXECUTE_READWRITE, &oldProtect))
        return;
    std::memcpy(patch.address, patch.original, sizeof(patch.original));
    DWORD ignored = 0;
    VirtualProtect(patch.address, 6, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), patch.address, 6);
}

bool LoadFunctions()
{
    g_triRenderMode = ReadPtr<TriRenderModeFn>(kTriRenderModeSlotRva);
    g_triBegin = ReadPtr<TriBeginFn>(kTriBeginSlotRva);
    g_triEnd = ReadPtr<TriEndFn>(kTriEndSlotRva);
    g_triColor4ub = ReadPtr<TriColor4ubFn>(kTriColor4ubSlotRva);
    g_triBrightness = ReadPtr<TriBrightnessFn>(kTriBrightnessSlotRva);
    g_triTexCoord2f = ReadPtr<TriTexCoord2fFn>(kTriTexCoordSlotRva);
    g_triVertex3fv = ReadPtr<TriVertex3fvFn>(kTriVertex3fvSlotRva);
    g_triVertex3f = ReadPtr<TriVertex3fFn>(kTriVertex3fSlotRva);
    g_triCullFace = ReadPtr<TriCullFaceFn>(kTriCullFaceSlotRva);
    g_triSpriteTexture =
        ReadPtr<TriSpriteTextureFn>(kTriSpriteTextureSlotRva);
    g_qglBegin = ReadPtr<GlBeginFn>(kQglBeginRva);
    g_qglEnd = ReadPtr<GlEndFn>(kQglEndRva);
    g_color4f = ReadPtr<GlColor4fFn>(kQglColor4fRva);
    g_qglDepthMaskOriginal = ReadPtr<GlDepthMaskFn>(kQglDepthMaskRva);
    g_texEnvi = ReadPtr<GlTexEnviFn>(kQglTexEnviRva);
    g_blendFunc = ReadPtr<GlBlendFuncFn>(kQglBlendFuncRva);
    g_enable = ReadPtr<GlEnableFn>(kQglEnableRva);
    g_disable = ReadPtr<GlDisableFn>(kQglDisableRva);
    g_shadeModel = ReadPtr<GlShadeModelFn>(kQglShadeModelRva);
    g_texCoord2f = ReadPtr<GlTexCoord2fFn>(kQglTexCoord2fRva);
    g_vertex3fv = ReadPtr<GlVertex3fvFn>(kQglVertex3fvRva);
    g_vertex3f = ReadPtr<GlVertex3fFn>(kQglVertex3fRva);
    g_drawArrays = ReadPtr<GlDrawArraysFn>(kQglDrawArraysRva);
    g_enableClientState =
        ReadPtr<GlEnableClientStateFn>(kQglEnableClientStateRva);
    g_vertexPointer = ReadPtr<GlVertexPointerFn>(kQglVertexPointerRva);
    g_texCoordPointer = ReadPtr<GlTexCoordPointerFn>(kQglTexCoordPointerRva);
    g_colorPointer = ReadPtr<GlColorPointerFn>(kQglColorPointerRva);
    g_pushClientAttrib =
        ReadPtr<GlPushClientAttribFn>(kQglPushClientAttribRva);
    g_popClientAttrib =
        ReadPtr<GlPopClientAttribFn>(kQglPopClientAttribRva);
    g_getIntegerv = ReadPtr<GlGetIntegervFn>(kQglGetIntegervRva);
    g_getFloatv = ReadPtr<GlGetFloatvFn>(kQglGetFloatvRva);
    g_isEnabled = ReadPtr<GlIsEnabledFn>(kQglIsEnabledRva);
    g_clientActiveTexture =
        ReadPtr<GlClientActiveTextureFn>(kQglClientActiveTextureRva);
    g_bindBuffer = ReadPtr<GlBindBufferFn>(kQglBindBufferRva);
    g_deleteBuffers = ReadPtr<GlDeleteBuffersFn>(kQglDeleteBuffersRva);
    g_genBuffers = ReadPtr<GlGenBuffersFn>(kQglGenBuffersRva);
    g_bufferData = ReadPtr<GlBufferDataFn>(kQglBufferDataRva);
    auto getProc =
        *reinterpret_cast<SdlGetProcAddressFn*>(
            g_hwBase + kSdlGetProcAddressIatRva);
    g_bufferSubData = getProc
        ? reinterpret_cast<GlBufferSubDataFn>(getProc("glBufferSubData"))
        : nullptr;

    return g_triRenderMode && g_triBegin && g_triEnd && g_triColor4ub &&
           g_triBrightness && g_triTexCoord2f && g_triVertex3fv &&
           g_triVertex3f && g_triCullFace && g_triSpriteTexture &&
           g_qglBegin && g_qglEnd && g_color4f && g_qglDepthMaskOriginal &&
           g_texEnvi && g_blendFunc && g_enable && g_disable && g_shadeModel &&
           g_texCoord2f && g_vertex3fv && g_vertex3f && g_drawArrays &&
           g_enableClientState &&
           g_vertexPointer && g_texCoordPointer && g_colorPointer &&
           g_pushClientAttrib && g_popClientAttrib && g_getIntegerv &&
           g_getFloatv && g_isEnabled && g_clientActiveTexture &&
           g_bindBuffer && g_deleteBuffers && g_genBuffers &&
           g_bufferData && g_bufferSubData;
}

bool SelfOrExpected(const void* live, const void* expected)
{
    if (live == expected)
        return true;
    if (!live || !g_selfModule)
        return false;
    MEMORY_BASIC_INFORMATION mbi{};
    return VirtualQuery(live, &mbi, sizeof(mbi)) == sizeof(mbi) &&
           mbi.AllocationBase == g_selfModule;
}

bool SlotsStable()
{
    // TriangleAPI slots themselves must remain exact. If an external TriAPI
    // provider replaces one, preserve it by using stock passthrough.
    if (ReadPtr<TriRenderModeFn>(kTriRenderModeSlotRva) != g_triRenderMode ||
        ReadPtr<TriBeginFn>(kTriBeginSlotRva) != g_triBegin ||
        ReadPtr<TriEndFn>(kTriEndSlotRva) != g_triEnd ||
        ReadPtr<TriBrightnessFn>(kTriBrightnessSlotRva) != g_triBrightness ||
        ReadPtr<TriTexCoord2fFn>(kTriTexCoordSlotRva) != g_triTexCoord2f ||
        ReadPtr<TriVertex3fvFn>(kTriVertex3fvSlotRva) != g_triVertex3fv ||
        ReadPtr<TriSpriteTextureFn>(kTriSpriteTextureSlotRva) !=
            g_triSpriteTexture)
        return false;

    // RenderMode(0/5) expands into these server-state dispatches.  Same-mode
    // dedupe is only valid while the exact providers captured at install remain
    // live, otherwise skipping RenderMode could bypass an external hook.
    if (ReadPtr<GlTexEnviFn>(kQglTexEnviRva) != g_texEnvi ||
        ReadPtr<GlBlendFuncFn>(kQglBlendFuncRva) != g_blendFunc ||
        ReadPtr<GlEnableFn>(kQglEnableRva) != g_enable ||
        ReadPtr<GlDisableFn>(kQglDisableRva) != g_disable ||
        ReadPtr<GlDepthMaskFn>(kQglDepthMaskRva) !=
            g_qglDepthMaskOriginal ||
        ReadPtr<GlShadeModelFn>(kQglShadeModelRva) != g_shadeModel)
        return false;

    // qgl dispatch may be wrapped later by another module inside this ASI.
    return SelfOrExpected(
               reinterpret_cast<const void*>(ReadPtr<GlBeginFn>(kQglBeginRva)),
               reinterpret_cast<const void*>(g_qglBegin)) &&
           SelfOrExpected(
               reinterpret_cast<const void*>(ReadPtr<GlEndFn>(kQglEndRva)),
               reinterpret_cast<const void*>(g_qglEnd)) &&
           SelfOrExpected(
               reinterpret_cast<const void*>(ReadPtr<GlColor4fFn>(kQglColor4fRva)),
               reinterpret_cast<const void*>(g_color4f)) &&
           SelfOrExpected(
               reinterpret_cast<const void*>(ReadPtr<GlTexCoord2fFn>(kQglTexCoord2fRva)),
               reinterpret_cast<const void*>(g_texCoord2f)) &&
           ReadPtr<GlVertex3fvFn>(kQglVertex3fvRva) == g_vertex3fv &&
           ReadPtr<GlDrawArraysFn>(kQglDrawArraysRva) == g_drawArrays &&
           ReadPtr<GlEnableClientStateFn>(kQglEnableClientStateRva) ==
               g_enableClientState &&
           ReadPtr<GlVertexPointerFn>(kQglVertexPointerRva) ==
               g_vertexPointer &&
           ReadPtr<GlTexCoordPointerFn>(kQglTexCoordPointerRva) ==
               g_texCoordPointer &&
           ReadPtr<GlColorPointerFn>(kQglColorPointerRva) ==
               g_colorPointer &&
           ReadPtr<GlPushClientAttribFn>(kQglPushClientAttribRva) ==
               g_pushClientAttrib &&
           ReadPtr<GlPopClientAttribFn>(kQglPopClientAttribRva) ==
               g_popClientAttrib &&
           ReadPtr<GlGetIntegervFn>(kQglGetIntegervRva) ==
               g_getIntegerv &&
           ReadPtr<GlGetFloatvFn>(kQglGetFloatvRva) == g_getFloatv &&
           ReadPtr<GlIsEnabledFn>(kQglIsEnabledRva) == g_isEnabled &&
           ReadPtr<GlClientActiveTextureFn>(kQglClientActiveTextureRva) ==
               g_clientActiveTexture &&
           ReadPtr<GlBindBufferFn>(kQglBindBufferRva) == g_bindBuffer &&
           ReadPtr<GlDeleteBuffersFn>(kQglDeleteBuffersRva) ==
               g_deleteBuffers &&
           ReadPtr<GlGenBuffersFn>(kQglGenBuffersRva) == g_genBuffers &&
           ReadPtr<GlBufferDataFn>(kQglBufferDataRva) == g_bufferData;
}

bool TracerSlotsStable()
{
    // Tracer capture suppresses these exact TriangleAPI calls. Any foreign
    // provider must continue to receive stock traffic, so fail open.
    if (!SlotsStable() ||
        ReadPtr<TriColor4ubFn>(kTriColor4ubSlotRva) != g_triColor4ub ||
        ReadPtr<TriVertex3fFn>(kTriVertex3fSlotRva) != g_triVertex3f ||
        ReadPtr<TriCullFaceFn>(kTriCullFaceSlotRva) != g_triCullFace)
    {
        return false;
    }

    // TriangleAPI Vertex3f ultimately reaches this qgl provider. Internal ASI
    // wrappers are compatible, external replacements are not bypassed.
    return SelfOrExpected(
        reinterpret_cast<const void*>(ReadPtr<GlVertex3fFn>(kQglVertex3fRva)),
        reinterpret_cast<const void*>(g_vertex3f));
}

void ForgetBuffers(bool deleteBuffers)
{
    if (deleteBuffers && g_deleteBuffers &&
        (g_vbos[0] || g_vbos[1] || g_vbos[2]))
        g_deleteBuffers(3, g_vbos);
    g_vbos[0] = g_vbos[1] = g_vbos[2] = 0;
    g_vboFrame = 0;
}

bool EnsureBuffers()
{
    if (!worldvbo::ContextGenerationReady())
        return false;
    const std::uint32_t generation = worldvbo::ContextGeneration();
    if (generation != g_contextGeneration)
    {
        ForgetBuffers(false);
        g_contextGeneration = generation;
    }
    if (g_vbos[0] && g_vbos[1] && g_vbos[2])
        return true;

    unsigned ids[3]{};
    g_genBuffers(3, ids);
    if (!ids[0] || !ids[1] || !ids[2])
    {
        g_deleteBuffers(3, ids);
        return false;
    }
    std::memcpy(g_vbos, ids, sizeof(g_vbos));

    int previous = 0;
    g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
    for (const unsigned id : g_vbos)
    {
        g_bindBuffer(GL_ARRAY_BUFFER, id);
        g_bufferData(GL_ARRAY_BUFFER,
                     static_cast<std::ptrdiff_t>(g_vboCapacity),
                     nullptr, GL_STREAM_DRAW);
    }
    g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previous));
    return true;
}

bool ForeignArraysClear()
{
    g_lastArrayReject = false;
    if (g_isEnabled(GL_NORMAL_ARRAY) || g_isEnabled(GL_INDEX_ARRAY) ||
        g_isEnabled(GL_EDGE_FLAG_ARRAY) ||
        g_isEnabled(GL_FOG_COORDINATE_ARRAY) ||
        g_isEnabled(GL_SECONDARY_COLOR_ARRAY))
    {
        g_lastArrayReject = true;
        return false;
    }

    int previousClientTexture = static_cast<int>(GL_TEXTURE0);
    int maxTextureUnits = 0;
    g_getIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &previousClientTexture);
    g_getIntegerv(GL_MAX_TEXTURE_UNITS, &maxTextureUnits);
    if (maxTextureUnits < 1 || maxTextureUnits > 32)
    {
        g_lastArrayReject = true;
        return false;
    }
    for (int unit = 1; unit < maxTextureUnits; ++unit)
    {
        g_clientActiveTexture(GL_TEXTURE0 + static_cast<unsigned>(unit));
        if (g_isEnabled(GL_TEXTURE_COORD_ARRAY))
        {
            g_clientActiveTexture(
                static_cast<unsigned>(previousClientTexture));
            g_lastArrayReject = true;
            return false;
        }
    }
    g_clientActiveTexture(static_cast<unsigned>(previousClientTexture));
    return true;
}

bool CaptureReady(CaptureKind kind)
{
    if (kind == CaptureKind::None || !rendererperf::Enabled())
        return false;
    const bool slotsStable =
        kind == CaptureKind::Tracer ? TracerSlotsStable() : SlotsStable();
    if (!slotsStable || !EnsureBuffers() || !ForeignArraysClear())
        return false;
    int arrayBuffer = 0;
    g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
    return arrayBuffer == 0;
}

void ResetTracerPass()
{
    g_tracerPassValidated = false;
    g_tracerPassReady = false;
    g_tracerFinalAttrsValid = false;
    g_tracerPassReject = TracerPassReject::None;
}

bool ValidateTracerPassOnce()
{
    if (g_tracerPassValidated)
        return g_tracerPassReady;

    g_tracerPassValidated = true;
    g_tracerPassReady = false;
    g_tracerPassReject = TracerPassReject::None;
    g_lastArrayReject = false;

    if (!TracerSlotsStable())
    {
        g_tracerPassReject = TracerPassReject::Slots;
        return false;
    }
    if (!EnsureBuffers())
    {
        g_tracerPassReject = TracerPassReject::Buffer;
        return false;
    }
    if (!ForeignArraysClear())
    {
        g_tracerPassReject = TracerPassReject::Arrays;
        return false;
    }

    int arrayBuffer = 0;
    g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
    if (arrayBuffer != 0)
    {
        g_tracerPassReject = TracerPassReject::Buffer;
        return false;
    }

    g_tracerPassReady = true;
    return true;
}

void AppendVertex(const float* xyz)
{
    if (!xyz)
        return;
    BeamVertex v{};
    std::memcpy(v.xyz, xyz, sizeof(v.xyz));
    std::memcpy(v.st, g_currentTex, sizeof(v.st));
    std::memcpy(v.rgba, g_currentColor, sizeof(v.rgba));
    g_vertices.push_back(v);
}

void RestoreCurrentAttributes(const float color[4], const float tex[2])
{
    if (auto liveColor = ReadPtr<GlColor4fFn>(kQglColor4fRva))
        liveColor(color[0], color[1], color[2], color[3]);
    if (auto liveTex = ReadPtr<GlTexCoord2fFn>(kQglTexCoord2fRva))
        liveTex(tex[0], tex[1]);
}

void ReplayPending()
{
    if (g_vertices.empty())
        return;

    auto begin = ReadPtr<GlBeginFn>(kQglBeginRva);
    auto color = ReadPtr<GlColor4fFn>(kQglColor4fRva);
    auto tex = ReadPtr<GlTexCoord2fFn>(kQglTexCoord2fRva);
    auto vertex = ReadPtr<GlVertex3fvFn>(kQglVertex3fvRva);
    if (!begin || !color || !tex || !vertex)
        return;

    const bool preserveCurrent = g_batchKind != CaptureKind::Tracer;
    float entryColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float entryTex4[4]{};
    if (preserveCurrent)
    {
        g_getFloatv(GL_CURRENT_COLOR, entryColor);
        g_getFloatv(GL_CURRENT_TEXTURE_COORDS, entryTex4);
    }

    begin(GL_QUADS);
    for (const BeamVertex& v : g_vertices)
    {
        color(v.rgba[0], v.rgba[1], v.rgba[2], v.rgba[3]);
        tex(v.st[0], v.st[1]);
        vertex(v.xyz);
    }
    if (auto end = ReadPtr<TriEndFn>(kTriEndSlotRva))
        end();
    if (preserveCurrent)
    {
        const float entryTex[2]{entryTex4[0], entryTex4[1]};
        RestoreCurrentAttributes(entryColor, entryTex);
    }
    if (g_collectStats)
    {
        if (g_batchKind == CaptureKind::Beam)
            ++g_stats.replays;
        else if (g_batchKind == CaptureKind::Tracer)
            ++g_stats.tracerReplays;
    }
}

bool DrawPending()
{
    if (g_vertices.empty())
        return true;

    const std::size_t bytes = g_vertices.size() * sizeof(BeamVertex);
    if (bytes > g_vboCapacity)
    {
        std::size_t newCapacity = g_vboCapacity;
        while (newCapacity < bytes &&
               newCapacity <= (static_cast<std::size_t>(-1) >> 1))
            newCapacity <<= 1;
        if (newCapacity < bytes)
            return false;

        int previous = 0;
        g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
        for (const unsigned id : g_vbos)
        {
            g_bindBuffer(GL_ARRAY_BUFFER, id);
            g_bufferData(GL_ARRAY_BUFFER,
                         static_cast<std::ptrdiff_t>(newCapacity),
                         nullptr, GL_STREAM_DRAW);
        }
        g_vboCapacity = newCapacity;
        g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previous));
    }

    int previousBuffer = 0;
    int previousClientTexture = static_cast<int>(GL_TEXTURE0);
    const bool preserveCurrent = g_batchKind != CaptureKind::Tracer;
    float entryColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float entryTex4[4]{};
    g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    g_getIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &previousClientTexture);
    if (preserveCurrent)
    {
        g_getFloatv(GL_CURRENT_COLOR, entryColor);
        g_getFloatv(GL_CURRENT_TEXTURE_COORDS, entryTex4);
    }
    const unsigned vbo = g_vbos[g_vboFrame++ % 3u];

    g_pushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    g_clientActiveTexture(GL_TEXTURE0);
    g_bindBuffer(GL_ARRAY_BUFFER, vbo);
    g_bufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<std::ptrdiff_t>(bytes), g_vertices.data());
    g_enableClientState(GL_VERTEX_ARRAY);
    g_enableClientState(GL_TEXTURE_COORD_ARRAY);
    g_enableClientState(GL_COLOR_ARRAY);
    g_vertexPointer(3, GL_FLOAT, sizeof(BeamVertex),
                    reinterpret_cast<const void*>(offsetof(BeamVertex, xyz)));
    g_texCoordPointer(2, GL_FLOAT, sizeof(BeamVertex),
                      reinterpret_cast<const void*>(offsetof(BeamVertex, st)));
    g_colorPointer(4, GL_FLOAT, sizeof(BeamVertex),
                   reinterpret_cast<const void*>(offsetof(BeamVertex, rgba)));
    g_drawArrays(GL_QUADS, 0, static_cast<int>(g_vertices.size()));
    g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previousBuffer));
    g_popClientAttrib();
    g_clientActiveTexture(static_cast<unsigned>(previousClientTexture));

    if (preserveCurrent)
    {
        const float entryTex[2]{entryTex4[0], entryTex4[1]};
        RestoreCurrentAttributes(entryColor, entryTex);
    }

    if (g_collectStats && g_batchKind == CaptureKind::Beam)
    {
        g_stats.vertices += g_vertices.size();
        ++g_stats.uploads;
    }
    else if (g_collectStats && g_batchKind == CaptureKind::Tracer)
    {
        g_stats.tracerVertices += g_vertices.size();
        ++g_stats.tracerUploads;
    }
    return true;
}

void ClearPending()
{
    g_vertices.clear();
    g_batchPending = false;
    g_batchKind = CaptureKind::None;
    g_batchRenderMode = -1;
    g_batchSprite = nullptr;
    g_batchSpriteFrame = -1;
}

void FlushPending()
{
    if (!g_batchPending || g_vertices.empty())
    {
        ClearPending();
        return;
    }
    if (!DrawPending())
        ReplayPending();
    ClearPending();
}

void WINAPI TriBeginQglHook(unsigned mode)
{
    if (!g_armBegin)
    {
        FlushPending();
        if (auto live = ReadPtr<GlBeginFn>(kQglBeginRva))
            live(mode);
        return;
    }

    const CaptureKind armKind = g_armKind;
    const bool ready =
        armKind == CaptureKind::Tracer
            ? ValidateTracerPassOnce()
            : CaptureReady(armKind);
    if (g_armBegin && mode == GL_QUADS && ready)
    {
        if (g_batchPending &&
            (g_batchKind != armKind ||
             g_currentRenderMode != g_batchRenderMode ||
             g_currentSprite != g_batchSprite ||
             g_currentSpriteFrame != g_batchSpriteFrame))
        {
            // The state wrappers should have flushed before a key change. If
            // ownership was disturbed after that point, fail open rather than
            // drawing a previous batch under the wrong material state.
            FlushPending();
        }

        g_capture = true;
        g_captureKind = armKind;
        g_currentBeamStart = g_vertices.size();
        if (armKind == CaptureKind::Beam)
        {
            g_getFloatv(GL_CURRENT_COLOR, g_currentColor);
            float tex4[4]{};
            g_getFloatv(GL_CURRENT_TEXTURE_COORDS, tex4);
            g_currentTex[0] = tex4[0];
            g_currentTex[1] = tex4[1];
        }
        if (g_collectStats)
        {
            if (armKind == CaptureKind::Beam)
                ++g_stats.captures;
            else if (armKind == CaptureKind::Tracer)
                ++g_stats.tracerCaptures;
        }
        return;
    }

    if (g_armBegin && g_collectStats)
    {
        const bool tracer = armKind == CaptureKind::Tracer;
        if (tracer)
            ++g_stats.tracerFallbacks;
        else
            ++g_stats.fallbacks;

        if (tracer && g_tracerPassReject == TracerPassReject::Slots)
        {
            ++g_stats.tracerSlotFallbacks;
        }
        else if (tracer && g_tracerPassReject == TracerPassReject::Arrays)
        {
            ++g_stats.tracerArrayFallbacks;
        }
        else if (tracer)
        {
            ++g_stats.tracerBufferFallbacks;
        }
        else if (!SlotsStable())
        {
            ++g_stats.slotFallbacks;
        }
        else if (g_lastArrayReject)
        {
            ++g_stats.arrayFallbacks;
        }
        else
        {
            ++g_stats.bufferFallbacks;
        }
    }
    FlushPending();
    if (auto live = ReadPtr<GlBeginFn>(kQglBeginRva))
        live(mode);
}

void __cdecl BeamRenderModeHook(int mode)
{
    if (g_batchPending && mode != g_batchRenderMode)
        FlushPending();

    // Inside R_DrawBeams Gold repeatedly asks TriAPI for the same render mode
    // for adjacent homogeneous beams.  RenderMode(0/5) expands to 4-5 GL state
    // calls each time.  Shadow it only while this exact beam-owned callsite and
    // captured TriAPI slot remain stable, never carry ownership across the final
    // pass barrier.
    const bool canOwn =
        g_enabled && rendererperf::Enabled() && SlotsStable();
    if (!canOwn)
    {
        // Never carry deferred geometry across a provider/cvar ownership loss,
        // even when the requested mode is numerically unchanged.
        if (g_batchPending)
            FlushPending();
        g_renderModeOwned = false;
    }
    if (canOwn && g_renderModeOwned && mode == g_currentRenderMode)
    {
        if (g_collectStats)
            ++g_stats.renderModeSkips;
        return;
    }

    if (auto live = ReadPtr<TriRenderModeFn>(kTriRenderModeSlotRva))
        live(mode);
    g_currentRenderMode = mode;
    g_renderModeOwned = canOwn;
}

int __cdecl BeamSpriteTextureHook(void* spriteModel, int frame)
{
    if (g_batchPending &&
        (spriteModel != g_batchSprite || frame != g_batchSpriteFrame))
        FlushPending();

    int result = 0;
    if (auto live = ReadPtr<TriSpriteTextureFn>(kTriSpriteTextureSlotRva))
        result = live(spriteModel, frame);
    if (result)
    {
        g_currentSprite = spriteModel;
        g_currentSpriteFrame = frame;
    }
    else
    {
        g_currentSprite = nullptr;
        g_currentSpriteFrame = -1;
    }
    return result;
}

void __cdecl BeamFinalRenderModeHook(int mode)
{
    FlushPending();
    if (auto live = ReadPtr<TriRenderModeFn>(kTriRenderModeSlotRva))
        live(mode);
    g_currentRenderMode = mode;
    // The final stock restore is also our ownership barrier.  A plugin/engine
    // callback may freely mutate GL before the next beam pass, so the first
    // RenderMode call of every pass must always reach Gold.
    g_renderModeOwned = false;
}

void WINAPI BeamFinalDepthMaskHook(unsigned char flag)
{
    // R_DrawBeams restores depth-write/cull state immediately after the beam
    // entity bridge. Flush while the beam render state is still active.
    FlushPending();
    auto live = ReadPtr<GlDepthMaskFn>(kQglDepthMaskRva);
    if (live && live != &BeamFinalDepthMaskHook)
        live(flag);
    else if (g_qglDepthMaskOriginal &&
             g_qglDepthMaskOriginal != &BeamFinalDepthMaskHook)
        g_qglDepthMaskOriginal(flag);
}

void __cdecl BeamBeginHook(int primitive)
{
    const bool candidate =
        primitive == 2 && g_enabled && rendererperf::Enabled() &&
        SlotsStable();
    g_armBegin = candidate;
    g_armKind = candidate ? CaptureKind::Beam : CaptureKind::None;
    if (auto live = ReadPtr<TriBeginFn>(kTriBeginSlotRva))
        live(primitive);
    g_armBegin = false;
    g_armKind = CaptureKind::None;
}

void __cdecl TracerRenderModeHook(int mode)
{
    // Tracers are a separate pass. Never inherit beam render-mode ownership
    // across this boundary, Gold must authoritatively establish additive state.
    FlushPending();
    ResetTracerPass();
    if (auto live = ReadPtr<TriRenderModeFn>(kTriRenderModeSlotRva))
        live(mode);
    g_currentRenderMode = mode;
    g_renderModeOwned = false;
}

void __cdecl TracerBeginHook(int primitive)
{
    const bool candidate =
        primitive == 2 && g_tracerEnabled && rendererperf::Enabled();
    g_armBegin = candidate;
    g_armKind = candidate ? CaptureKind::Tracer : CaptureKind::None;
    if (auto live = ReadPtr<TriBeginFn>(kTriBeginSlotRva))
        live(primitive);
    g_armBegin = false;
    g_armKind = CaptureKind::None;
}

void __cdecl TracerVertexHook(float x, float y, float z)
{
    if (!g_capture)
    {
        if (auto live = ReadPtr<TriVertex3fFn>(kTriVertex3fSlotRva))
            live(x, y, z);
        return;
    }

    const float xyz[3]{x, y, z};
    AppendVertex(xyz);
}

void __cdecl TracerFinalCullFaceHook(int style)
{
    // Critical ordering barrier: the deferred tracer quads were generated with
    // TRI_NONE/culling disabled. Draw them before Gold restores TRI_FRONT.
    FlushPending();

    // While tracing we deliberately leave current color/texcoord untouched at
    // each End.  No callback or other GL consumer exists inside the exact
    // R_DrawTracers loop, and every emitted vertex sets both attributes before
    // use.  Restore the one externally observable stock result exactly once,
    // still before Gold's CullFace restore.
    if (g_tracerFinalAttrsValid)
        RestoreCurrentAttributes(g_currentColor, g_currentTex);
    ResetTracerPass();

    if (auto live = ReadPtr<TriCullFaceFn>(kTriCullFaceSlotRva))
        live(style);
}

void __cdecl BeamBrightnessHook(float brightness)
{
    if (!g_capture)
    {
        if (auto live = ReadPtr<TriBrightnessFn>(kTriBrightnessSlotRva))
            live(brightness);
        return;
    }
    const float alpha = *reinterpret_cast<float*>(g_hwBase + kTriAlphaRva);
    g_currentColor[0] =
        *reinterpret_cast<float*>(g_hwBase + kTriRedRva) * alpha * brightness;
    g_currentColor[1] =
        *reinterpret_cast<float*>(g_hwBase + kTriGreenRva) * alpha * brightness;
    g_currentColor[2] =
        *reinterpret_cast<float*>(g_hwBase + kTriBlueRva) * alpha * brightness;
    g_currentColor[3] = 1.0f;
}

void __cdecl BeamTexCoordHook(float s, float t)
{
    if (!g_capture)
    {
        if (auto live = ReadPtr<TriTexCoord2fFn>(kTriTexCoordSlotRva))
            live(s, t);
        return;
    }
    g_currentTex[0] = s;
    g_currentTex[1] = t;
}

void __cdecl BeamVertexHook(const float* xyz)
{
    if (!g_capture)
    {
        if (auto live = ReadPtr<TriVertex3fvFn>(kTriVertex3fvSlotRva))
            live(xyz);
        return;
    }
    AppendVertex(xyz);
}

void __cdecl BeamEndHook()
{
    if (!g_capture)
    {
        if (auto live = ReadPtr<TriEndFn>(kTriEndSlotRva))
            live();
        return;
    }
    const CaptureKind finishedKind = g_captureKind;
    g_capture = false;
    g_captureKind = CaptureKind::None;

    // Beam capture can cross callbacks/state consumers, so preserve its current
    // attributes exactly at every End as before. Tracer capture has no callback
    // boundary in the exact loop, defer its final externally observable current
    // state until the CullFace pass barrier.
    if (finishedKind == CaptureKind::Beam)
        RestoreCurrentAttributes(g_currentColor, g_currentTex);

    if (g_vertices.size() > g_currentBeamStart)
    {
        if (finishedKind == CaptureKind::Tracer)
            g_tracerFinalAttrsValid = true;
        if (!g_batchPending)
        {
            g_batchPending = true;
            g_batchKind = finishedKind;
            g_batchRenderMode = g_currentRenderMode;
            g_batchSprite = g_currentSprite;
            g_batchSpriteFrame = g_currentSpriteFrame;
        }
    }
}
} // namespace

bool Install(HMODULE hw, cl_enginefunc_t* engine)
{
    if (!hwbuild::MatchesTarget(hw) || !engine ||
        !engine->pfnRegisterVariable)
    {
        rendererlog::Line("beamvbo: exact hw.dll/engine unavailable, disabled");
        return false;
    }

    g_hwBase = reinterpret_cast<std::uint8_t*>(hw);
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&Install), &g_selfModule);
    if (!LoadFunctions())
    {
        rendererlog::Line("beamvbo: required TriAPI/GL entrypoints unavailable");
        return false;
    }

    g_cvar = RegisterCvarSafe(engine);
    g_tracerCvar = RegisterTracerCvarSafe(engine);

    g_vertices.reserve(1024);

    std::vector<CallPatch> beamPatches;
    beamPatches.reserve(19);
    beamPatches.push_back(
        {g_hwBase + kTriBeginQglCallRva, {},
         reinterpret_cast<void*>(&TriBeginQglHook), kQglBeginRva});
    beamPatches.push_back(
        {g_hwBase + kBeamRenderModeCallRva, {},
         reinterpret_cast<void*>(&BeamRenderModeHook), kTriRenderModeSlotRva});
    beamPatches.push_back(
        {g_hwBase + kBeamSpriteTextureCallRva, {},
         reinterpret_cast<void*>(&BeamSpriteTextureHook),
         kTriSpriteTextureSlotRva});
    beamPatches.push_back(
        {g_hwBase + kBeamBeginCallRva, {},
         reinterpret_cast<void*>(&BeamBeginHook), kTriBeginSlotRva});
    beamPatches.push_back(
        {g_hwBase + kBeamEndCallRva, {},
         reinterpret_cast<void*>(&BeamEndHook), kTriEndSlotRva});
    beamPatches.push_back(
        {g_hwBase + kBeamFinalDepthMaskCallRva, {},
         reinterpret_cast<void*>(&BeamFinalDepthMaskHook), kQglDepthMaskRva});
    beamPatches.push_back(
        {g_hwBase + kBeamFinalRenderModeCallRva, {},
         reinterpret_cast<void*>(&BeamFinalRenderModeHook),
         kTriRenderModeSlotRva});
    for (const auto rva : kBrightnessCalls)
        beamPatches.push_back(
            {g_hwBase + rva, {}, reinterpret_cast<void*>(&BeamBrightnessHook),
             kTriBrightnessSlotRva});
    for (const auto rva : kTexCoordCalls)
        beamPatches.push_back(
            {g_hwBase + rva, {}, reinterpret_cast<void*>(&BeamTexCoordHook),
             kTriTexCoordSlotRva});
    for (const auto rva : kVertexCalls)
        beamPatches.push_back(
            {g_hwBase + rva, {}, reinterpret_cast<void*>(&BeamVertexHook),
             kTriVertex3fvSlotRva});

    for (const CallPatch& patch : beamPatches)
    {
        if (!VerifyIndirectCall(patch))
        {
            rendererlog::Line(
                "beamvbo: beam callsite verification failed at hw+0x%X",
                static_cast<unsigned>(patch.address - g_hwBase));
            return false;
        }
    }

    std::size_t beamInstalled = 0;
    for (; beamInstalled < beamPatches.size(); ++beamInstalled)
    {
        if (!WriteDirectCall(beamPatches[beamInstalled]))
            break;
    }
    if (beamInstalled != beamPatches.size())
    {
        while (beamInstalled > 0)
            RestorePatch(beamPatches[--beamInstalled]);
        rendererlog::Line("beamvbo: beam transactional patch failed, disabled");
        return false;
    }

    // Tracer phase is optional and transactional on its own.  A tracer build
    // mismatch must never take down the already validated BEAMPOINTS path.
    std::vector<CallPatch> tracerPatches;
    tracerPatches.reserve(21);
    tracerPatches.push_back(
        {g_hwBase + kTracerSpriteTextureCallRva, {},
         reinterpret_cast<void*>(&BeamSpriteTextureHook),
         kTriSpriteTextureSlotRva});
    tracerPatches.push_back(
        {g_hwBase + kTracerRenderModeCallRva, {},
         reinterpret_cast<void*>(&TracerRenderModeHook),
         kTriRenderModeSlotRva});
    tracerPatches.push_back(
        {g_hwBase + kTracerBeginCallRva, {},
         reinterpret_cast<void*>(&TracerBeginHook), kTriBeginSlotRva});
    tracerPatches.push_back(
        {g_hwBase + kTracerEndCallRva, {},
         reinterpret_cast<void*>(&BeamEndHook), kTriEndSlotRva});
    tracerPatches.push_back(
        {g_hwBase + kTracerFinalCullFaceCallRva, {},
         reinterpret_cast<void*>(&TracerFinalCullFaceHook),
         kTriCullFaceSlotRva});
    tracerPatches.push_back(
        {g_hwBase + kTracerFinalRenderModeCallRva, {},
         reinterpret_cast<void*>(&BeamFinalRenderModeHook),
         kTriRenderModeSlotRva});
    for (const auto rva : kTracerBrightnessCalls)
        tracerPatches.push_back(
            {g_hwBase + rva, {}, reinterpret_cast<void*>(&BeamBrightnessHook),
             kTriBrightnessSlotRva});
    for (const auto rva : kTracerTexCoordCalls)
        tracerPatches.push_back(
            {g_hwBase + rva, {}, reinterpret_cast<void*>(&BeamTexCoordHook),
             kTriTexCoordSlotRva});
    for (const auto rva : kTracerVertexCalls)
        tracerPatches.push_back(
            {g_hwBase + rva, {}, reinterpret_cast<void*>(&TracerVertexHook),
             kTriVertex3fSlotRva});

    bool tracerVerified = true;
    for (const CallPatch& patch : tracerPatches)
    {
        if (!VerifyIndirectCall(patch))
        {
            rendererlog::Line(
                "beamvbo: tracer callsite verification failed at hw+0x%X, "
                "tracer disabled, beam remains active",
                static_cast<unsigned>(patch.address - g_hwBase));
            tracerVerified = false;
            break;
        }
    }

    std::size_t tracerInstalled = 0;
    if (tracerVerified)
    {
        for (; tracerInstalled < tracerPatches.size(); ++tracerInstalled)
        {
            if (!WriteDirectCall(tracerPatches[tracerInstalled]))
                break;
        }
    }
    if (tracerVerified && tracerInstalled != tracerPatches.size())
    {
        while (tracerInstalled > 0)
            RestorePatch(tracerPatches[--tracerInstalled]);
        rendererlog::Line(
            "beamvbo: tracer transactional patch failed, tracer disabled, "
            "beam remains active");
    }
    g_tracerInstalled =
        tracerVerified && tracerInstalled == tracerPatches.size();

    g_contextGeneration = worldvbo::ContextGeneration();
    g_installed = true;
    if (g_tracerInstalled)
        rendererlog::Line(
            "beamvbo: exact BEAMPOINTS/type0 streaming capture installed "
            "(r_beam_vbo default 1), tracer capture installed "
            "(r_tracer_vbo default 0)");
    else
        rendererlog::Line(
            "beamvbo: exact BEAMPOINTS/type0 streaming capture installed "
            "(r_beam_vbo default 1), tracer capture unavailable");
    return true;
}

void UpdateFrame()
{
    g_enabled = false;
    g_tracerEnabled = false;
    if (!g_installed)
        return;
    __try
    {
        if (g_cvar)
            g_enabled = g_cvar->value == 1.0f;
        if (g_tracerInstalled && g_tracerCvar)
            g_tracerEnabled = g_tracerCvar->value == 1.0f;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_enabled = false;
        g_tracerEnabled = false;
    }
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
} // namespace beamvbo
