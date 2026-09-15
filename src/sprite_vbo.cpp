#include "sprite_vbo.h"

#include "hw_build.h"
#include "log.h"
#include "perf_control.h"
#include "world_vbo.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace spritevbo
{
namespace
{
// Exact GoldClient hw.dll RVAs for R_DrawSpriteModel's opaque GL_QUADS path.
constexpr std::uintptr_t kCurrentEntityRva          = 0x0078F470u;
constexpr std::uintptr_t kSpriteTextureMatrixRva    = 0x0078F944u;
constexpr std::uintptr_t kSpriteBlendRva            = 0x0333A008u;
constexpr std::uintptr_t kGoldBindRva               = 0x00064D40u;
constexpr std::uintptr_t kSpriteBindCallRva         = 0x0006FCD4u;
constexpr std::uintptr_t kSpriteColorCallRva        = 0x0006FB7Du;
constexpr std::uintptr_t kSpriteBeginCallRva        = 0x0006FD56u;
constexpr std::uintptr_t kSpriteTex0CallRva         = 0x0006FD6Eu;
constexpr std::uintptr_t kSpriteVertex0CallRva      = 0x0006FE11u;
constexpr std::uintptr_t kSpriteTex1CallRva         = 0x0006FE29u;
constexpr std::uintptr_t kSpriteVertex1CallRva      = 0x0006FECCu;
constexpr std::uintptr_t kSpriteTex2CallRva         = 0x0006FEE4u;
constexpr std::uintptr_t kSpriteVertex2CallRva      = 0x0006FF87u;
constexpr std::uintptr_t kSpriteTex3CallRva         = 0x0006FF9Fu;
constexpr std::uintptr_t kSpriteVertex3CallRva      = 0x00070042u;
constexpr std::uintptr_t kSpriteEndCallRva          = 0x00070048u;

constexpr std::uintptr_t kQglBeginRva               = 0x027E3DE0u;
constexpr std::uintptr_t kQglEndRva                 = 0x027E3DD8u;
constexpr std::uintptr_t kQglTexCoord2fRva          = 0x027E3778u;
constexpr std::uintptr_t kQglVertex3fvRva           = 0x027E37D0u;
constexpr std::uintptr_t kQglColor4fRva             = 0x027E3DD4u;
constexpr std::uintptr_t kQglColor4ubRva            = 0x027E37F4u;
constexpr std::uintptr_t kQglDrawArraysRva          = 0x027E3CECu;
constexpr std::uintptr_t kQglEnableClientStateRva   = 0x027E3CD4u;
constexpr std::uintptr_t kQglDisableClientStateRva  = 0x027E3CF0u;
constexpr std::uintptr_t kQglVertexPointerRva       = 0x027E3958u;
constexpr std::uintptr_t kQglTexCoordPointerRva     = 0x027E39E4u;
constexpr std::uintptr_t kQglColorPointerRva        = 0x027E3D0Cu;
constexpr std::uintptr_t kQglPushClientAttribRva    = 0x027E3B04u;
constexpr std::uintptr_t kQglPopClientAttribRva     = 0x027E3B14u;
constexpr std::uintptr_t kQglGetIntegervRva         = 0x027E3DD0u;
constexpr std::uintptr_t kQglGetFloatvRva           = 0x027E37DCu;
constexpr std::uintptr_t kQglIsEnabledRva           = 0x027E3DF4u;
constexpr std::uintptr_t kQglClientActiveTextureRva = 0x027E386Cu;
constexpr std::uintptr_t kQglActiveTextureRva       = 0x027E3784u;
constexpr std::uintptr_t kQglBindBufferRva          = 0x027E3DB8u;
constexpr std::uintptr_t kQglDeleteBuffersRva       = 0x027E3DC8u;
constexpr std::uintptr_t kQglGenBuffersRva          = 0x027E3DCCu;
constexpr std::uintptr_t kQglBufferDataRva          = 0x027E3DB4u;
constexpr std::uintptr_t kQglEnableRva              = 0x027E3DFCu;
constexpr std::uintptr_t kQglDisableRva             = 0x027E3DF8u;
constexpr std::uintptr_t kQglDepthMaskRva           = 0x027E376Cu;
constexpr std::uintptr_t kQglTexEnviRva             = 0x027E3744u;
constexpr std::uintptr_t kSdlGetProcAddressIatRva   = 0x001FA330u;

constexpr unsigned GL_QUADS                   = 0x0007u;
constexpr unsigned GL_FLOAT                   = 0x1406u;
constexpr unsigned GL_UNSIGNED_BYTE           = 0x1401u;
constexpr unsigned GL_TEXTURE_2D              = 0x0DE1u;
constexpr unsigned GL_DEPTH_TEST              = 0x0B71u;
constexpr unsigned GL_ALPHA_TEST              = 0x0BC0u;
constexpr unsigned GL_BLEND                   = 0x0BE2u;
constexpr unsigned GL_VERTEX_ARRAY            = 0x8074u;
constexpr unsigned GL_NORMAL_ARRAY            = 0x8075u;
constexpr unsigned GL_COLOR_ARRAY             = 0x8076u;
constexpr unsigned GL_INDEX_ARRAY             = 0x8077u;
constexpr unsigned GL_TEXTURE_COORD_ARRAY     = 0x8078u;
constexpr unsigned GL_EDGE_FLAG_ARRAY         = 0x8079u;
constexpr unsigned GL_FOG_COORDINATE_ARRAY    = 0x8457u;
constexpr unsigned GL_SECONDARY_COLOR_ARRAY   = 0x845Eu;
constexpr unsigned GL_TEXTURE0                = 0x84C0u;
constexpr unsigned GL_ACTIVE_TEXTURE          = 0x84E0u;
constexpr unsigned GL_CLIENT_ACTIVE_TEXTURE   = 0x84E1u;
constexpr unsigned GL_MAX_TEXTURE_UNITS       = 0x84E2u;
constexpr unsigned GL_TEXTURE_BINDING_2D      = 0x8069u;
constexpr unsigned GL_ARRAY_BUFFER            = 0x8892u;
constexpr unsigned GL_ARRAY_BUFFER_BINDING    = 0x8894u;
constexpr unsigned GL_STREAM_DRAW             = 0x88E0u;
constexpr unsigned GL_CLIENT_VERTEX_ARRAY_BIT = 0x00000002u;
constexpr unsigned GL_CURRENT_COLOR           = 0x0B00u;
constexpr unsigned GL_CURRENT_TEXTURE_COORDS  = 0x0B03u;
constexpr unsigned GL_CURRENT_PROGRAM         = 0x8B8Du;
constexpr unsigned GL_DEPTH_WRITEMASK         = 0x0B72u;
constexpr unsigned GL_TEXTURE_ENV             = 0x2300u;
constexpr unsigned GL_TEXTURE_ENV_MODE        = 0x2200u;
constexpr unsigned GL_MODULATE                = 0x2100u;

using GoldBindFn = void (__fastcall*)(int textureUnit, int textureId);
using GlBeginFn = void (WINAPI*)(unsigned mode);
using GlEndFn = void (WINAPI*)();
using GlTexCoord2fFn = void (WINAPI*)(float s, float t);
using GlVertex3fvFn = void (WINAPI*)(const float* vertex);
using GlColor4fFn = void (WINAPI*)(float r, float g, float b, float a);
using GlColor4ubFn = void (WINAPI*)(unsigned char r, unsigned char g,
                                    unsigned char b, unsigned char a);
using GlDrawArraysFn = void (WINAPI*)(unsigned mode, int first, int count);
using GlEnableClientStateFn = void (WINAPI*)(unsigned array);
using GlDisableClientStateFn = void (WINAPI*)(unsigned array);
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
using GlActiveTextureFn = void (WINAPI*)(unsigned texture);
using GlBindBufferFn = void (WINAPI*)(unsigned target, unsigned buffer);
using GlDeleteBuffersFn = void (WINAPI*)(int count, const unsigned* buffers);
using GlGenBuffersFn = void (WINAPI*)(int count, unsigned* buffers);
using GlBufferDataFn = void (WINAPI*)(unsigned target, std::ptrdiff_t size,
                                     const void* data, unsigned usage);
using GlBufferSubDataFn = void (WINAPI*)(unsigned target, std::ptrdiff_t offset,
                                        std::ptrdiff_t size, const void* data);
using GlEnableFn = void (WINAPI*)(unsigned cap);
using GlDisableFn = void (WINAPI*)(unsigned cap);
using GlDepthMaskFn = void (WINAPI*)(unsigned char flag);
using GlTexEnviFn = void (WINAPI*)(unsigned target, unsigned pname, int param);
using GlGetTexEnvivFn = void (WINAPI*)(unsigned target, unsigned pname, int* params);
using SdlGetProcAddressFn = void* (__cdecl*)(const char* name);

struct SpriteVertex
{
    float xyz[3];
    float st[2];
    unsigned char rgba[4];
};
static_assert(sizeof(SpriteVertex) == 24,
              "solid sprite stream vertex must remain tightly packed");

struct SpriteSpan
{
    unsigned texture = 0;
    std::size_t first = 0;
    std::size_t count = 0;
};

struct IndirectPatch
{
    std::uint8_t* address = nullptr;
    std::uint8_t original[6]{};
    void* replacement = nullptr;
    std::uintptr_t expectedSlotRva = 0;
};

struct DirectPatch
{
    std::uint8_t* address = nullptr;
    std::uint8_t original[5]{};
    void* replacement = nullptr;
    void* expectedTarget = nullptr;
};

std::uint8_t* g_hwBase = nullptr;
cl_enginefunc_t* g_engine = nullptr;
HMODULE g_selfModule = nullptr;
cvar_t* g_cvar = nullptr;
bool g_installed = false;
bool g_enabled = false;
bool g_collectStats = false;
int g_solidPassDepth = 0;

GoldBindFn g_goldBind = nullptr;
GlBeginFn g_begin = nullptr;
GlEndFn g_end = nullptr;
GlTexCoord2fFn g_texCoord2f = nullptr;
GlVertex3fvFn g_vertex3fv = nullptr;
GlColor4fFn g_color4f = nullptr;
GlColor4ubFn g_color4ub = nullptr;
GlDrawArraysFn g_drawArrays = nullptr;
GlEnableClientStateFn g_enableClientState = nullptr;
GlDisableClientStateFn g_disableClientState = nullptr;
GlVertexPointerFn g_vertexPointer = nullptr;
GlTexCoordPointerFn g_texCoordPointer = nullptr;
GlColorPointerFn g_colorPointer = nullptr;
GlPushClientAttribFn g_pushClientAttrib = nullptr;
GlPopClientAttribFn g_popClientAttrib = nullptr;
GlGetIntegervFn g_getIntegerv = nullptr;
GlGetFloatvFn g_getFloatv = nullptr;
GlIsEnabledFn g_isEnabled = nullptr;
GlClientActiveTextureFn g_clientActiveTexture = nullptr;
GlActiveTextureFn g_activeTexture = nullptr;
GlBindBufferFn g_bindBuffer = nullptr;
GlDeleteBuffersFn g_deleteBuffers = nullptr;
GlGenBuffersFn g_genBuffers = nullptr;
GlBufferDataFn g_bufferData = nullptr;
GlBufferSubDataFn g_bufferSubData = nullptr;
GlEnableFn g_enable = nullptr;
GlDisableFn g_disable = nullptr;
GlDepthMaskFn g_depthMask = nullptr;
GlTexEnviFn g_texEnvi = nullptr;
GlGetTexEnvivFn g_getTexEnviv = nullptr;

bool g_armCapture = false;
bool g_inCapture = false;
bool g_currentReady = false;
bool g_sawTextureBind = false;
bool g_sawColor = false;
unsigned g_currentTexture = 0;
float g_currentTex[2]{};
unsigned char g_currentColor[4]{255, 255, 255, 255};
std::array<SpriteVertex, 4> g_currentVertices{};
std::size_t g_currentVertexCount = 0;

std::vector<SpriteVertex> g_vertices;
std::vector<SpriteSpan> g_spans;
unsigned g_vbos[3]{};
unsigned g_vboFrame = 0;
std::size_t g_vboCapacity = 512u * 1024u;
std::uint32_t g_contextGeneration = 0;
ProfileStats g_stats{};

template <typename Fn>
Fn ReadQgl(std::uintptr_t rva)
{
    if (!g_hwBase)
        return nullptr;
    return *reinterpret_cast<Fn*>(g_hwBase + rva);
}

void* RelativeCallTarget(const std::uint8_t* callsite)
{
    if (!callsite || callsite[0] != 0xE8)
        return nullptr;
    const auto rel = *reinterpret_cast<const std::int32_t*>(callsite + 1);
    return const_cast<std::uint8_t*>(callsite) + 5 + rel;
}

bool VerifyIndirect(const IndirectPatch& patch)
{
    if (!patch.address || patch.address[0] != 0xFF ||
        patch.address[1] != 0x15)
        return false;
    const std::uintptr_t operand =
        static_cast<std::uintptr_t>(
            *reinterpret_cast<const std::uint32_t*>(patch.address + 2));
    return operand ==
        reinterpret_cast<std::uintptr_t>(g_hwBase + patch.expectedSlotRva);
}

bool WriteIndirect(IndirectPatch& patch)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(patch.address, 6, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    std::memcpy(patch.original, patch.address, 6);
    const auto delta =
        reinterpret_cast<std::intptr_t>(patch.replacement) -
        reinterpret_cast<std::intptr_t>(patch.address + 5);
    if (delta < std::numeric_limits<std::int32_t>::min() ||
        delta > std::numeric_limits<std::int32_t>::max())
    {
        DWORD ignored = 0;
        VirtualProtect(patch.address, 6, oldProtect, &ignored);
        return false;
    }
    patch.address[0] = 0xE8;
    *reinterpret_cast<std::int32_t*>(patch.address + 1) =
        static_cast<std::int32_t>(delta);
    patch.address[5] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), patch.address, 6);
    DWORD ignored = 0;
    VirtualProtect(patch.address, 6, oldProtect, &ignored);
    return true;
}

void RestoreIndirect(const IndirectPatch& patch)
{
    DWORD oldProtect = 0;
    if (!patch.address ||
        !VirtualProtect(patch.address, 6, PAGE_EXECUTE_READWRITE, &oldProtect))
        return;
    std::memcpy(patch.address, patch.original, 6);
    FlushInstructionCache(GetCurrentProcess(), patch.address, 6);
    DWORD ignored = 0;
    VirtualProtect(patch.address, 6, oldProtect, &ignored);
}

bool VerifyDirect(const DirectPatch& patch)
{
    return patch.address && patch.address[0] == 0xE8 &&
           RelativeCallTarget(patch.address) == patch.expectedTarget;
}

bool WriteDirect(DirectPatch& patch)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(patch.address, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    std::memcpy(patch.original, patch.address, 5);
    const auto delta =
        reinterpret_cast<std::intptr_t>(patch.replacement) -
        reinterpret_cast<std::intptr_t>(patch.address + 5);
    if (delta < std::numeric_limits<std::int32_t>::min() ||
        delta > std::numeric_limits<std::int32_t>::max())
    {
        DWORD ignored = 0;
        VirtualProtect(patch.address, 5, oldProtect, &ignored);
        return false;
    }
    patch.address[0] = 0xE8;
    *reinterpret_cast<std::int32_t*>(patch.address + 1) =
        static_cast<std::int32_t>(delta);
    FlushInstructionCache(GetCurrentProcess(), patch.address, 5);
    DWORD ignored = 0;
    VirtualProtect(patch.address, 5, oldProtect, &ignored);
    return true;
}

void RestoreDirect(const DirectPatch& patch)
{
    DWORD oldProtect = 0;
    if (!patch.address ||
        !VirtualProtect(patch.address, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return;
    std::memcpy(patch.address, patch.original, 5);
    FlushInstructionCache(GetCurrentProcess(), patch.address, 5);
    DWORD ignored = 0;
    VirtualProtect(patch.address, 5, oldProtect, &ignored);
}

bool LoadFunctions()
{
    g_goldBind = reinterpret_cast<GoldBindFn>(g_hwBase + kGoldBindRva);
    g_begin = ReadQgl<GlBeginFn>(kQglBeginRva);
    g_end = ReadQgl<GlEndFn>(kQglEndRva);
    g_texCoord2f = ReadQgl<GlTexCoord2fFn>(kQglTexCoord2fRva);
    g_vertex3fv = ReadQgl<GlVertex3fvFn>(kQglVertex3fvRva);
    g_color4f = ReadQgl<GlColor4fFn>(kQglColor4fRva);
    g_color4ub = ReadQgl<GlColor4ubFn>(kQglColor4ubRva);
    g_drawArrays = ReadQgl<GlDrawArraysFn>(kQglDrawArraysRva);
    g_enableClientState =
        ReadQgl<GlEnableClientStateFn>(kQglEnableClientStateRva);
    g_disableClientState =
        ReadQgl<GlDisableClientStateFn>(kQglDisableClientStateRva);
    g_vertexPointer = ReadQgl<GlVertexPointerFn>(kQglVertexPointerRva);
    g_texCoordPointer =
        ReadQgl<GlTexCoordPointerFn>(kQglTexCoordPointerRva);
    g_colorPointer = ReadQgl<GlColorPointerFn>(kQglColorPointerRva);
    g_pushClientAttrib =
        ReadQgl<GlPushClientAttribFn>(kQglPushClientAttribRva);
    g_popClientAttrib =
        ReadQgl<GlPopClientAttribFn>(kQglPopClientAttribRva);
    g_getIntegerv = ReadQgl<GlGetIntegervFn>(kQglGetIntegervRva);
    g_getFloatv = ReadQgl<GlGetFloatvFn>(kQglGetFloatvRva);
    g_isEnabled = ReadQgl<GlIsEnabledFn>(kQglIsEnabledRva);
    g_clientActiveTexture =
        ReadQgl<GlClientActiveTextureFn>(kQglClientActiveTextureRva);
    g_activeTexture = ReadQgl<GlActiveTextureFn>(kQglActiveTextureRva);
    g_bindBuffer = ReadQgl<GlBindBufferFn>(kQglBindBufferRva);
    g_deleteBuffers = ReadQgl<GlDeleteBuffersFn>(kQglDeleteBuffersRva);
    g_genBuffers = ReadQgl<GlGenBuffersFn>(kQglGenBuffersRva);
    g_bufferData = ReadQgl<GlBufferDataFn>(kQglBufferDataRva);
    g_enable = ReadQgl<GlEnableFn>(kQglEnableRva);
    g_disable = ReadQgl<GlDisableFn>(kQglDisableRva);
    g_depthMask = ReadQgl<GlDepthMaskFn>(kQglDepthMaskRva);
    g_texEnvi = ReadQgl<GlTexEnviFn>(kQglTexEnviRva);

    const auto getProc = *reinterpret_cast<SdlGetProcAddressFn*>(
        g_hwBase + kSdlGetProcAddressIatRva);
    g_bufferSubData = getProc
        ? reinterpret_cast<GlBufferSubDataFn>(getProc("glBufferSubData"))
        : nullptr;
    g_getTexEnviv = getProc
        ? reinterpret_cast<GlGetTexEnvivFn>(getProc("glGetTexEnviv"))
        : nullptr;

    return g_goldBind && g_begin && g_end && g_texCoord2f &&
           g_vertex3fv && g_color4f && g_color4ub && g_drawArrays &&
           g_enableClientState && g_disableClientState &&
           g_vertexPointer && g_texCoordPointer && g_colorPointer &&
           g_pushClientAttrib && g_popClientAttrib && g_getIntegerv &&
           g_getFloatv && g_isEnabled && g_clientActiveTexture &&
           g_activeTexture && g_bindBuffer && g_deleteBuffers &&
           g_genBuffers && g_bufferData && g_bufferSubData &&
           g_enable && g_disable && g_depthMask && g_texEnvi &&
           g_getTexEnviv;
}

std::uint64_t SlotMismatchMask()
{
    std::uint64_t mask = 0;
    const auto selfOrExpected = [](const void* live, const void* expected) {
        if (live == expected)
            return true;
        if (!live || !g_selfModule)
            return false;
        MEMORY_BASIC_INFORMATION mbi{};
        return VirtualQuery(live, &mbi, sizeof(mbi)) == sizeof(mbi) &&
               mbi.AllocationBase == g_selfModule;
    };
#define SPRITE_SLOT_CHECK(bit, type, rva, expected) \
    do { if (ReadQgl<type>(rva) != (expected)) mask |= (1ull << (bit)); } while (0)
    SPRITE_SLOT_CHECK(0, GlBeginFn, kQglBeginRva, g_begin);
    SPRITE_SLOT_CHECK(1, GlEndFn, kQglEndRva, g_end);
    if (!selfOrExpected(
            reinterpret_cast<const void*>(ReadQgl<GlTexCoord2fFn>(kQglTexCoord2fRva)),
            reinterpret_cast<const void*>(g_texCoord2f)))
        mask |= (1ull << 2);
    SPRITE_SLOT_CHECK(3, GlVertex3fvFn, kQglVertex3fvRva, g_vertex3fv);
    if (!selfOrExpected(
            reinterpret_cast<const void*>(ReadQgl<GlColor4fFn>(kQglColor4fRva)),
            reinterpret_cast<const void*>(g_color4f)))
        mask |= (1ull << 4);
    SPRITE_SLOT_CHECK(5, GlColor4ubFn, kQglColor4ubRva, g_color4ub);
    SPRITE_SLOT_CHECK(6, GlDrawArraysFn, kQglDrawArraysRva, g_drawArrays);
    SPRITE_SLOT_CHECK(7, GlEnableClientStateFn, kQglEnableClientStateRva, g_enableClientState);
    SPRITE_SLOT_CHECK(8, GlDisableClientStateFn, kQglDisableClientStateRva, g_disableClientState);
    SPRITE_SLOT_CHECK(9, GlVertexPointerFn, kQglVertexPointerRva, g_vertexPointer);
    SPRITE_SLOT_CHECK(10, GlTexCoordPointerFn, kQglTexCoordPointerRva, g_texCoordPointer);
    SPRITE_SLOT_CHECK(11, GlColorPointerFn, kQglColorPointerRva, g_colorPointer);
    SPRITE_SLOT_CHECK(12, GlPushClientAttribFn, kQglPushClientAttribRva, g_pushClientAttrib);
    SPRITE_SLOT_CHECK(13, GlPopClientAttribFn, kQglPopClientAttribRva, g_popClientAttrib);
    SPRITE_SLOT_CHECK(14, GlGetIntegervFn, kQglGetIntegervRva, g_getIntegerv);
    SPRITE_SLOT_CHECK(15, GlGetFloatvFn, kQglGetFloatvRva, g_getFloatv);
    SPRITE_SLOT_CHECK(16, GlIsEnabledFn, kQglIsEnabledRva, g_isEnabled);
    SPRITE_SLOT_CHECK(17, GlClientActiveTextureFn, kQglClientActiveTextureRva, g_clientActiveTexture);
    SPRITE_SLOT_CHECK(18, GlActiveTextureFn, kQglActiveTextureRva, g_activeTexture);
    SPRITE_SLOT_CHECK(19, GlBindBufferFn, kQglBindBufferRva, g_bindBuffer);
    SPRITE_SLOT_CHECK(20, GlDeleteBuffersFn, kQglDeleteBuffersRva, g_deleteBuffers);
    SPRITE_SLOT_CHECK(21, GlGenBuffersFn, kQglGenBuffersRva, g_genBuffers);
    SPRITE_SLOT_CHECK(22, GlBufferDataFn, kQglBufferDataRva, g_bufferData);
    SPRITE_SLOT_CHECK(23, GlEnableFn, kQglEnableRva, g_enable);
    SPRITE_SLOT_CHECK(24, GlDisableFn, kQglDisableRva, g_disable);
    SPRITE_SLOT_CHECK(25, GlDepthMaskFn, kQglDepthMaskRva, g_depthMask);
    SPRITE_SLOT_CHECK(26, GlTexEnviFn, kQglTexEnviRva, g_texEnvi);
#undef SPRITE_SLOT_CHECK
    return mask;
}

bool SlotsStable()
{
    return SlotMismatchMask() == 0;
}

void ForgetBuffers(bool deleteBuffers)
{
    if (deleteBuffers && g_deleteBuffers &&
        (g_vbos[0] || g_vbos[1] || g_vbos[2]))
    {
        g_deleteBuffers(3, g_vbos);
    }
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

    int previous = 0;
    g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
    for (const unsigned id : ids)
    {
        g_bindBuffer(GL_ARRAY_BUFFER, id);
        g_bufferData(GL_ARRAY_BUFFER,
                     static_cast<std::ptrdiff_t>(g_vboCapacity),
                     nullptr, GL_STREAM_DRAW);
    }
    g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previous));
    std::memcpy(g_vbos, ids, sizeof(g_vbos));
    return true;
}

bool ForeignArraysClear()
{
    if (g_isEnabled(GL_NORMAL_ARRAY) || g_isEnabled(GL_INDEX_ARRAY) ||
        g_isEnabled(GL_EDGE_FLAG_ARRAY) ||
        g_isEnabled(GL_FOG_COORDINATE_ARRAY) ||
        g_isEnabled(GL_SECONDARY_COLOR_ARRAY))
        return false;

    int previousClient = static_cast<int>(GL_TEXTURE0);
    int units = 0;
    g_getIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &previousClient);
    g_getIntegerv(GL_MAX_TEXTURE_UNITS, &units);
    if (units < 1 || units > 32)
        return false;
    for (int unit = 1; unit < units; ++unit)
    {
        g_clientActiveTexture(GL_TEXTURE0 + static_cast<unsigned>(unit));
        if (g_isEnabled(GL_TEXTURE_COORD_ARRAY))
        {
            g_clientActiveTexture(static_cast<unsigned>(previousClient));
            return false;
        }
    }
    g_clientActiveTexture(static_cast<unsigned>(previousClient));
    return true;
}

bool GeometryStateReady()
{
    int activeTexture = 0;
    int program = 0;
    int depthMask = 0;
    int texEnv = 0;
    int arrayBuffer = 0;
    g_getIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    g_getIntegerv(GL_CURRENT_PROGRAM, &program);
    g_getIntegerv(GL_DEPTH_WRITEMASK, &depthMask);
    g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
    g_getTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &texEnv);

    return activeTexture == static_cast<int>(GL_TEXTURE0) &&
           program == 0 && depthMask != 0 && arrayBuffer == 0 &&
           texEnv == static_cast<int>(GL_MODULATE) &&
           g_isEnabled(GL_TEXTURE_2D) &&
           g_isEnabled(GL_DEPTH_TEST) &&
           g_isEnabled(GL_ALPHA_TEST) &&
           !g_isEnabled(GL_BLEND) &&
           ForeignArraysClear();
}

bool CaptureReady()
{
    return g_enabled && rendererperf::Enabled() &&
           g_solidPassDepth > 0 && SlotsStable() &&
           EnsureBuffers() && GeometryStateReady();
}

struct ServerState
{
    int activeTexture = static_cast<int>(GL_TEXTURE0);
    int clientTexture = static_cast<int>(GL_TEXTURE0);
    int arrayBuffer = 0;
    int texture = 0;
    int depthMask = 1;
    int texEnv = static_cast<int>(GL_MODULATE);
    bool texture2D = true;
    bool depthTest = true;
    bool alphaTest = false;
    bool blend = false;
    float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float texcoord[4]{};
};

void SetEnabled(unsigned cap, bool enabled)
{
    if (enabled)
        g_enable(cap);
    else
        g_disable(cap);
}

bool CaptureServerState(ServerState& state)
{
    __try
    {
        g_getIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);
        g_getIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &state.clientTexture);
        g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &state.arrayBuffer);
        g_getIntegerv(GL_DEPTH_WRITEMASK, &state.depthMask);
        state.texture2D = g_isEnabled(GL_TEXTURE_2D) != 0;
        state.depthTest = g_isEnabled(GL_DEPTH_TEST) != 0;
        state.alphaTest = g_isEnabled(GL_ALPHA_TEST) != 0;
        state.blend = g_isEnabled(GL_BLEND) != 0;
        g_getFloatv(GL_CURRENT_COLOR, state.color);

        if (state.activeTexture != static_cast<int>(GL_TEXTURE0))
            g_activeTexture(GL_TEXTURE0);
        g_getIntegerv(GL_TEXTURE_BINDING_2D, &state.texture);
        g_getTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &state.texEnv);
        g_getFloatv(GL_CURRENT_TEXTURE_COORDS, state.texcoord);
        if (state.activeTexture != static_cast<int>(GL_TEXTURE0))
            g_activeTexture(static_cast<unsigned>(state.activeTexture));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void RestoreServerState(const ServerState& state)
{
    __try
    {
        g_activeTexture(GL_TEXTURE0);
        if (g_goldBind)
            g_goldBind(0, state.texture);
        g_texEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, state.texEnv);
        SetEnabled(GL_TEXTURE_2D, state.texture2D);
        SetEnabled(GL_DEPTH_TEST, state.depthTest);
        SetEnabled(GL_ALPHA_TEST, state.alphaTest);
        SetEnabled(GL_BLEND, state.blend);
        g_depthMask(state.depthMask ? 1 : 0);
        if (auto liveColor = ReadQgl<GlColor4fFn>(kQglColor4fRva))
            liveColor(state.color[0], state.color[1],
                      state.color[2], state.color[3]);
        if (auto liveTex = ReadQgl<GlTexCoord2fFn>(kQglTexCoord2fRva))
            liveTex(state.texcoord[0], state.texcoord[1]);
        g_activeTexture(static_cast<unsigned>(state.activeTexture));
        g_clientActiveTexture(static_cast<unsigned>(state.clientTexture));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void PrepareSpriteDrawState()
{
    g_activeTexture(GL_TEXTURE0);
    g_texEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
              static_cast<int>(GL_MODULATE));
    g_enable(GL_TEXTURE_2D);
    g_enable(GL_DEPTH_TEST);
    g_enable(GL_ALPHA_TEST);
    g_disable(GL_BLEND);
    g_depthMask(1);
}

bool GrowBuffers(std::size_t bytes, int previousBuffer)
{
    if (bytes <= g_vboCapacity)
        return true;
    std::size_t capacity = g_vboCapacity;
    while (capacity < bytes &&
           capacity <= (std::numeric_limits<std::size_t>::max() >> 1))
        capacity <<= 1;
    if (capacity < bytes)
        return false;

    for (const unsigned id : g_vbos)
    {
        g_bindBuffer(GL_ARRAY_BUFFER, id);
        g_bufferData(GL_ARRAY_BUFFER,
                     static_cast<std::ptrdiff_t>(capacity),
                     nullptr, GL_STREAM_DRAW);
    }
    g_vboCapacity = capacity;
    g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previousBuffer));
    return true;
}

bool DrawPending()
{
    if (g_vertices.empty())
        return true;
    if (!SlotsStable() || !EnsureBuffers())
        return false;

    ServerState state{};
    if (!CaptureServerState(state))
        return false;

    const std::size_t bytes = g_vertices.size() * sizeof(SpriteVertex);
    if (!GrowBuffers(bytes, state.arrayBuffer))
        return false;

    bool pushed = false;
    bool ok = false;
    __try
    {
        PrepareSpriteDrawState();
        const unsigned vbo = g_vbos[g_vboFrame++ % 3u];
        g_pushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        pushed = true;
        g_clientActiveTexture(GL_TEXTURE0);
        g_disableClientState(GL_NORMAL_ARRAY);
        g_disableClientState(GL_INDEX_ARRAY);
        g_disableClientState(GL_EDGE_FLAG_ARRAY);
        g_disableClientState(GL_FOG_COORDINATE_ARRAY);
        g_disableClientState(GL_SECONDARY_COLOR_ARRAY);
        g_bindBuffer(GL_ARRAY_BUFFER, vbo);
        g_bufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<std::ptrdiff_t>(bytes),
                        g_vertices.data());
        g_enableClientState(GL_VERTEX_ARRAY);
        g_enableClientState(GL_TEXTURE_COORD_ARRAY);
        g_enableClientState(GL_COLOR_ARRAY);
        g_vertexPointer(3, GL_FLOAT, sizeof(SpriteVertex),
                        reinterpret_cast<const void*>(
                            offsetof(SpriteVertex, xyz)));
        g_texCoordPointer(2, GL_FLOAT, sizeof(SpriteVertex),
                          reinterpret_cast<const void*>(
                              offsetof(SpriteVertex, st)));
        g_colorPointer(4, GL_UNSIGNED_BYTE, sizeof(SpriteVertex),
                       reinterpret_cast<const void*>(
                           offsetof(SpriteVertex, rgba)));

        for (const SpriteSpan& span : g_spans)
        {
            if (!span.texture || span.count == 0 ||
                span.first > static_cast<std::size_t>(INT_MAX) ||
                span.count > static_cast<std::size_t>(INT_MAX))
                __leave;
            g_goldBind(0, static_cast<int>(span.texture));
            g_drawArrays(GL_QUADS,
                         static_cast<int>(span.first),
                         static_cast<int>(span.count));
            if (g_collectStats)
                ++g_stats.drawCalls;
        }
        ok = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }

    __try
    {
        g_bindBuffer(GL_ARRAY_BUFFER,
                     static_cast<unsigned>(state.arrayBuffer));
        if (pushed)
            g_popClientAttrib();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    RestoreServerState(state);

    if (ok && g_collectStats)
    {
        g_stats.vertices += g_vertices.size();
        ++g_stats.uploads;
    }
    return ok;
}

void ReplayPending()
{
    if (g_vertices.empty())
        return;

    ServerState state{};
    if (!CaptureServerState(state))
        return;
    __try
    {
        PrepareSpriteDrawState();
        for (const SpriteSpan& span : g_spans)
        {
            if (!span.texture || !span.count)
                continue;
            g_goldBind(0, static_cast<int>(span.texture));
            g_begin(GL_QUADS);
            const std::size_t end = span.first + span.count;
            for (std::size_t i = span.first;
                 i < end && i < g_vertices.size(); ++i)
            {
                const SpriteVertex& v = g_vertices[i];
                if (auto liveColor = ReadQgl<GlColor4ubFn>(kQglColor4ubRva))
                    liveColor(v.rgba[0], v.rgba[1],
                              v.rgba[2], v.rgba[3]);
                if (auto liveTex = ReadQgl<GlTexCoord2fFn>(kQglTexCoord2fRva))
                    liveTex(v.st[0], v.st[1]);
                g_vertex3fv(v.xyz);
            }
            g_end();
        }
        if (g_collectStats)
            ++g_stats.replays;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    RestoreServerState(state);
}

void ClearPending()
{
    g_vertices.clear();
    g_spans.clear();
}

void ReplayCurrentQuad()
{
    if (!g_currentTexture || g_currentVertexCount == 0)
        return;
    const std::size_t replayCount =
        g_currentVertexCount < g_currentVertices.size()
            ? g_currentVertexCount
            : g_currentVertices.size();
    const std::size_t oldVertices = g_vertices.size();
    const std::size_t oldSpans = g_spans.size();
    try
    {
        g_vertices.insert(g_vertices.end(),
                          g_currentVertices.begin(),
                          g_currentVertices.begin() +
                              static_cast<std::ptrdiff_t>(replayCount));
        g_spans.push_back(
            {g_currentTexture, oldVertices, replayCount});
        ReplayPending();
    }
    catch (...)
    {
    }
    g_vertices.resize(oldVertices);
    g_spans.resize(oldSpans);
}

void WINAPI SpriteBeginHook(unsigned mode)
{
    if (!g_armCapture || mode != GL_QUADS || !g_sawTextureBind ||
        !g_sawColor || !g_currentTexture || !CaptureReady())
    {
        if (g_armCapture)
        {
            if (g_collectStats)
            {
                ++g_stats.fallbacks;
                if (!SlotsStable())
                    ++g_stats.slotFallbacks;
                else
                    ++g_stats.stateFallbacks;
            }
            Flush();
        }
        g_inCapture = false;
        if (auto live = ReadQgl<GlBeginFn>(kQglBeginRva))
            live(mode);
        return;
    }

    g_inCapture = true;
    g_currentReady = false;
    g_currentVertexCount = 0;
    g_currentTex[0] = g_currentTex[1] = 0.0f;
    if (g_collectStats)
        ++g_stats.captures;
}

void WINAPI SpriteColor4ubHook(unsigned char r, unsigned char g,
                               unsigned char b, unsigned char a)
{
    if (auto live = ReadQgl<GlColor4ubFn>(kQglColor4ubRva))
        live(r, g, b, a);
    if (g_armCapture)
    {
        g_currentColor[0] = r;
        g_currentColor[1] = g;
        g_currentColor[2] = b;
        g_currentColor[3] = a;
        g_sawColor = true;
    }
}

void WINAPI SpriteTexCoordHook(float s, float t)
{
    if (!g_inCapture)
    {
        if (auto live = ReadQgl<GlTexCoord2fFn>(kQglTexCoord2fRva))
            live(s, t);
        return;
    }
    g_currentTex[0] = s;
    g_currentTex[1] = t;
}

void WINAPI SpriteVertexHook(const float* xyz)
{
    if (!g_inCapture)
    {
        if (auto live = ReadQgl<GlVertex3fvFn>(kQglVertex3fvRva))
            live(xyz);
        return;
    }
    if (!xyz || g_currentVertexCount >= g_currentVertices.size())
    {
        g_currentVertexCount = g_currentVertices.size() + 1;
        return;
    }
    SpriteVertex& v = g_currentVertices[g_currentVertexCount++];
    std::memcpy(v.xyz, xyz, sizeof(v.xyz));
    std::memcpy(v.st, g_currentTex, sizeof(v.st));
    std::memcpy(v.rgba, g_currentColor, sizeof(v.rgba));
}

void WINAPI SpriteEndHook()
{
    if (!g_inCapture)
    {
        if (auto live = ReadQgl<GlEndFn>(kQglEndRva))
            live();
        return;
    }

    g_inCapture = false;
    if (g_currentVertexCount == g_currentVertices.size())
    {
        g_currentReady = true;
        // Stock immediate mode leaves the final sprite texcoord persistent.
        if (auto live = ReadQgl<GlTexCoord2fFn>(kQglTexCoord2fRva))
            live(g_currentTex[0], g_currentTex[1]);
        return;
    }

    // Exact stock always emits four vertices.  If ownership was disturbed,
    // preserve ordering by materialising previous deferred sprites first, then
    // replay whatever was captured while the current sprite state is still live.
    Flush();
    ReplayCurrentQuad();
    g_currentReady = false;
    if (g_collectStats)
    {
        ++g_stats.fallbacks;
        ++g_stats.stateFallbacks;
    }
}

void __fastcall SpriteBindHook(int textureUnit, int textureId)
{
    if (g_goldBind)
        g_goldBind(textureUnit, textureId);
    if (g_armCapture && textureUnit == 0 && textureId > 0)
    {
        g_currentTexture = static_cast<unsigned>(textureId);
        g_sawTextureBind = true;
    }
}

bool CommitCurrent()
{
    if (!g_currentReady || !g_currentTexture ||
        g_currentVertexCount != g_currentVertices.size())
        return true;

    const std::size_t oldVertices = g_vertices.size();
    const std::size_t oldSpans = g_spans.size();
    try
    {
        g_vertices.insert(g_vertices.end(),
                          g_currentVertices.begin(),
                          g_currentVertices.end());
        if (!g_spans.empty() &&
            g_spans.back().texture == g_currentTexture &&
            g_spans.back().first + g_spans.back().count == oldVertices)
        {
            g_spans.back().count += g_currentVertices.size();
        }
        else
        {
            g_spans.push_back(
                {g_currentTexture, oldVertices, g_currentVertices.size()});
        }
    }
    catch (...)
    {
        g_vertices.resize(oldVertices);
        g_spans.resize(oldSpans);
        return false;
    }
    if (g_collectStats)
        ++g_stats.sprites;
    return true;
}
} // namespace

bool Install(HMODULE hw, cl_enginefunc_t* engine)
{
    if (!hwbuild::MatchesTarget(hw) || !engine ||
        !engine->pfnRegisterVariable)
    {
        rendererlog::Line(
            "spritevbo: exact hw.dll/engine unavailable, disabled");
        return false;
    }

    g_hwBase = reinterpret_cast<std::uint8_t*>(hw);
    g_engine = engine;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&Install), &g_selfModule);
    if (!LoadFunctions())
    {
        rendererlog::Line("spritevbo: required GL entrypoints unavailable");
        return false;
    }

    __try
    {
        g_cvar = engine->pfnRegisterVariable("r_sprite_vbo", "0", 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_cvar = nullptr;
    }

    g_vertices.reserve(4096);
    g_spans.reserve(256);

    std::array<IndirectPatch, 11> patches{{
        {g_hwBase + kSpriteColorCallRva, {}, reinterpret_cast<void*>(&SpriteColor4ubHook), kQglColor4ubRva},
        {g_hwBase + kSpriteBeginCallRva, {}, reinterpret_cast<void*>(&SpriteBeginHook), kQglBeginRva},
        {g_hwBase + kSpriteTex0CallRva, {}, reinterpret_cast<void*>(&SpriteTexCoordHook), kQglTexCoord2fRva},
        {g_hwBase + kSpriteVertex0CallRva, {}, reinterpret_cast<void*>(&SpriteVertexHook), kQglVertex3fvRva},
        {g_hwBase + kSpriteTex1CallRva, {}, reinterpret_cast<void*>(&SpriteTexCoordHook), kQglTexCoord2fRva},
        {g_hwBase + kSpriteVertex1CallRva, {}, reinterpret_cast<void*>(&SpriteVertexHook), kQglVertex3fvRva},
        {g_hwBase + kSpriteTex2CallRva, {}, reinterpret_cast<void*>(&SpriteTexCoordHook), kQglTexCoord2fRva},
        {g_hwBase + kSpriteVertex2CallRva, {}, reinterpret_cast<void*>(&SpriteVertexHook), kQglVertex3fvRva},
        {g_hwBase + kSpriteTex3CallRva, {}, reinterpret_cast<void*>(&SpriteTexCoordHook), kQglTexCoord2fRva},
        {g_hwBase + kSpriteVertex3CallRva, {}, reinterpret_cast<void*>(&SpriteVertexHook), kQglVertex3fvRva},
        {g_hwBase + kSpriteEndCallRva, {}, reinterpret_cast<void*>(&SpriteEndHook), kQglEndRva},
    }};
    DirectPatch bindPatch{
        g_hwBase + kSpriteBindCallRva, {},
        reinterpret_cast<void*>(&SpriteBindHook),
        reinterpret_cast<void*>(g_goldBind)};

    for (const IndirectPatch& patch : patches)
    {
        if (!VerifyIndirect(patch))
        {
            rendererlog::Line(
                "spritevbo: qgl callsite verification failed at hw+0x%X",
                static_cast<unsigned>(patch.address - g_hwBase));
            return false;
        }
    }
    if (!VerifyDirect(bindPatch))
    {
        rendererlog::Line(
            "spritevbo: GL_Bind callsite verification failed at hw+0x%X",
            static_cast<unsigned>(bindPatch.address - g_hwBase));
        return false;
    }

    std::size_t installed = 0;
    for (; installed < patches.size(); ++installed)
    {
        if (!WriteIndirect(patches[installed]))
            break;
    }
    if (installed != patches.size() || !WriteDirect(bindPatch))
    {
        while (installed > 0)
            RestoreIndirect(patches[--installed]);
        rendererlog::Line(
            "spritevbo: transactional callsite patch failed, disabled");
        return false;
    }

    g_contextGeneration = worldvbo::ContextGeneration();
    g_installed = true;
    rendererlog::Line(
        "spritevbo: exact solid R_DrawSpriteModel streaming capture installed "
        "(r_sprite_vbo default 0)");
    return true;
}

void UpdateFrame()
{
    g_enabled = false;
    if (!g_installed || !g_cvar)
        return;
    __try
    {
        g_enabled = g_cvar->value == 1.0f;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_enabled = false;
    }
}

void BeginSolidPass()
{
    if (g_solidPassDepth == 0 && !g_vertices.empty())
        Flush();
    ++g_solidPassDepth;
}

void EndSolidPass()
{
    if (g_solidPassDepth <= 0)
    {
        Flush();
        return;
    }
    if (g_solidPassDepth == 1)
        Flush();
    --g_solidPassDepth;
}

bool BeginCurrentSolidSprite()
{
    if (g_collectStats)
        ++g_stats.attempts;
    g_armCapture = false;
    g_inCapture = false;
    g_currentReady = false;
    g_sawTextureBind = false;
    g_sawColor = false;
    g_currentTexture = 0;
    g_currentVertexCount = 0;

    if (!g_installed || !g_enabled || !rendererperf::Enabled() ||
        g_solidPassDepth <= 0)
    {
        if (g_collectStats)
        {
            ++g_stats.gateRejects;
            ++g_stats.gateDisabled;
        }
        return false;
    }
    const std::uint64_t slotMismatch = SlotMismatchMask();
    if (slotMismatch != 0)
    {
        if (g_collectStats)
        {
            ++g_stats.gateRejects;
            ++g_stats.gateSlots;
            g_stats.slotMismatchMask |= slotMismatch;
        }
        return false;
    }

    __try
    {
        if (*reinterpret_cast<const int*>(
                g_hwBase + kSpriteTextureMatrixRva) != 0)
        {
            if (g_collectStats)
            {
                ++g_stats.gateRejects;
                ++g_stats.gateMatrix;
            }
            return false;
        }
        if (*reinterpret_cast<const int*>(
                g_hwBase + kSpriteBlendRva) != 0)
        {
            if (g_collectStats)
            {
                ++g_stats.gateRejects;
                ++g_stats.gateBlend;
            }
            return false;
        }

        cl_entity_t* entity =
            *reinterpret_cast<cl_entity_t**>(
                g_hwBase + kCurrentEntityRva);
        if (!entity || !entity->model ||
            entity->curstate.rendermode != 0)
        {
            if (g_collectStats)
            {
                ++g_stats.gateRejects;
                ++g_stats.gateEntity;
            }
            return false;
        }
        const auto* model =
            reinterpret_cast<const std::uint8_t*>(entity->model);
        if (*reinterpret_cast<const int*>(model + 0x44) != 1)
        {
            if (g_collectStats)
            {
                ++g_stats.gateRejects;
                ++g_stats.gateModel;
            }
            return false;
        }

        // The exact color is captured from Gold's qglColor4ub call.  This keeps
        // Gold's optional color-correction helper authoritative.
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (g_collectStats)
            ++g_stats.gateRejects;
        return false;
    }

    g_armCapture = true;
    return true;
}

void EndCurrentSolidSprite()
{
    if (!g_armCapture)
        return;
    if (g_currentReady && !CommitCurrent())
    {
        Flush();
        ReplayCurrentQuad();
        if (g_collectStats)
        {
            ++g_stats.fallbacks;
            ++g_stats.stateFallbacks;
        }
    }
    g_armCapture = false;
    g_inCapture = false;
    g_currentReady = false;
    g_sawTextureBind = false;
    g_sawColor = false;
    g_currentTexture = 0;
    g_currentVertexCount = 0;
}

void Flush()
{
    if (g_vertices.empty())
    {
        ClearPending();
        return;
    }
    if (!DrawPending())
        ReplayPending();
    ClearPending();
}

void SetProfileCollection(bool enabled)
{
    g_collectStats = enabled;
}

ProfileStats ConsumeProfileStats()
{
    const ProfileStats stats = g_stats;
    g_stats = {};
    return stats;
}
} // namespace spritevbo
