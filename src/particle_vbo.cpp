#include "particle_vbo.h"

#include "hw_build.h"
#include "log.h"
#include "perf_control.h"
#include "world_vbo.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace particlevbo
{
namespace
{
// Exact inspected GoldClient hw.dll R_DrawParticles callsites.
constexpr std::uintptr_t kParticleBeginCallRva      = 0x0008B081u;
constexpr std::uintptr_t kParticleColorCallRva      = 0x0008B2E7u;
constexpr std::uintptr_t kParticleTex0CallRva       = 0x0008B2FFu;
constexpr std::uintptr_t kParticleVertex0CallRva    = 0x0008B306u;
constexpr std::uintptr_t kParticleTex1CallRva       = 0x0008B31Eu;
constexpr std::uintptr_t kParticleVertex1CallRva    = 0x0008B366u;
constexpr std::uintptr_t kParticleTex2CallRva       = 0x0008B37Eu;
constexpr std::uintptr_t kParticleVertex2CallRva    = 0x0008B3C6u;
constexpr std::uintptr_t kParticleEndCallRva        = 0x0008B6B8u;

constexpr std::uintptr_t kQglBeginRva               = 0x027E3DE0u;
constexpr std::uintptr_t kQglEndRva                 = 0x027E3DD8u;
constexpr std::uintptr_t kQglColor4ubvRva           = 0x027E3D5Cu;
constexpr std::uintptr_t kQglColor4fRva             = 0x027E3DD4u;
constexpr std::uintptr_t kQglTexCoord2fRva          = 0x027E3778u;
constexpr std::uintptr_t kQglVertex3fvRva           = 0x027E37D0u;
constexpr std::uintptr_t kQglVertex3fRva            = 0x027E3798u;
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

constexpr unsigned GL_TRIANGLES               = 0x0004u;
constexpr unsigned GL_FLOAT                   = 0x1406u;
constexpr unsigned GL_UNSIGNED_BYTE           = 0x1401u;
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

constexpr std::uintptr_t kActiveParticlesRva = 0x027EF518u;
constexpr std::uintptr_t kClientTimeDoubleRva = 0x02E16EB0u;

using GlBeginFn = void (WINAPI*)(unsigned mode);
using GlEndFn = void (WINAPI*)();
using GlColor4ubvFn = void (WINAPI*)(const unsigned char* rgba);
using GlColor4fFn = void (WINAPI*)(float r, float g, float b, float a);
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

struct ParticleVertex
{
    float xyz[3];
    float st[2];
    unsigned char rgba[4];
};
static_assert(sizeof(ParticleVertex) == 24,
              "particle stream vertex must remain tightly packed");

struct CallPatch
{
    std::uint8_t* address = nullptr;
    std::uint8_t original[6]{};
    void* replacement = nullptr;
    std::uintptr_t expectedSlotRva = 0;
};

std::uint8_t* g_hwBase = nullptr;
cl_enginefunc_t* g_engine = nullptr;
HMODULE g_selfModule = nullptr;
cvar_t* g_cvar = nullptr;
bool g_installed = false;
bool g_enabled = false;
bool g_capture = false;
bool g_collectStats = false;

GlBeginFn g_begin = nullptr;
GlEndFn g_end = nullptr;
GlColor4ubvFn g_color4ubv = nullptr;
GlColor4fFn g_color4f = nullptr;
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

std::vector<ParticleVertex> g_vertices;
float g_currentColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
unsigned char g_currentColorBytes[4]{255, 255, 255, 255};
float g_currentTex[2]{};
bool g_touchedColor = false;
bool g_touchedTex = false;
bool g_lastArrayReject = false;
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

bool VerifyIndirectCall(const CallPatch& patch)
{
    if (!patch.address || patch.address[0] != 0xFF || patch.address[1] != 0x15)
        return false;
    const std::uintptr_t operand =
        static_cast<std::uintptr_t>(*reinterpret_cast<const std::uint32_t*>(
            patch.address + 2));
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

bool GlReady()
{
    g_begin = ReadQgl<GlBeginFn>(kQglBeginRva);
    g_end = ReadQgl<GlEndFn>(kQglEndRva);
    g_color4ubv = ReadQgl<GlColor4ubvFn>(kQglColor4ubvRva);
    g_color4f = ReadQgl<GlColor4fFn>(kQglColor4fRva);
    g_texCoord2f = ReadQgl<GlTexCoord2fFn>(kQglTexCoord2fRva);
    g_vertex3fv = ReadQgl<GlVertex3fvFn>(kQglVertex3fvRva);
    g_vertex3f = ReadQgl<GlVertex3fFn>(kQglVertex3fRva);
    g_drawArrays = ReadQgl<GlDrawArraysFn>(kQglDrawArraysRva);
    g_enableClientState =
        ReadQgl<GlEnableClientStateFn>(kQglEnableClientStateRva);
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
    g_bindBuffer = ReadQgl<GlBindBufferFn>(kQglBindBufferRva);
    g_deleteBuffers = ReadQgl<GlDeleteBuffersFn>(kQglDeleteBuffersRva);
    g_genBuffers = ReadQgl<GlGenBuffersFn>(kQglGenBuffersRva);
    g_bufferData = ReadQgl<GlBufferDataFn>(kQglBufferDataRva);
    auto getProc =
        *reinterpret_cast<SdlGetProcAddressFn*>(
            g_hwBase + kSdlGetProcAddressIatRva);
    g_bufferSubData = getProc
        ? reinterpret_cast<GlBufferSubDataFn>(
              getProc("glBufferSubData"))
        : nullptr;
    return g_begin && g_end && g_color4ubv && g_color4f &&
           g_texCoord2f && g_vertex3fv && g_vertex3f && g_drawArrays &&
           g_enableClientState && g_vertexPointer && g_texCoordPointer &&
           g_colorPointer && g_pushClientAttrib && g_popClientAttrib &&
           g_getIntegerv && g_getFloatv && g_isEnabled &&
           g_clientActiveTexture &&
           g_bindBuffer && g_deleteBuffers && g_genBuffers && g_bufferData &&
           g_bufferSubData;
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
        // The previous objects belonged to a dead context, never delete them in
        // the new one.
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
    g_vbos[0] = ids[0];
    g_vbos[1] = ids[1];
    g_vbos[2] = ids[2];

    int previousBuffer = 0;
    g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    for (const unsigned id : g_vbos)
    {
        g_bindBuffer(GL_ARRAY_BUFFER, id);
        g_bufferData(GL_ARRAY_BUFFER,
                     static_cast<std::ptrdiff_t>(g_vboCapacity),
                     nullptr, GL_STREAM_DRAW);
    }
    g_bindBuffer(GL_ARRAY_BUFFER,
                 static_cast<unsigned>(previousBuffer));
    return true;
}

bool GlSlotsStable()
{
    // Direct callsite capture is only valid while the qgl dispatch pointers
    // match the functions captured at installation. If another renderer/mod
    // installs a later qgl hook, fail open so that hook still observes the
    // exact particle immediate-mode traffic.
    auto selfOrExpected = [](const void* live, const void* expected) {
        if (live == expected)
            return true;
        if (!live || !g_selfModule)
            return false;
        MEMORY_BASIC_INFORMATION mbi{};
        return VirtualQuery(live, &mbi, sizeof(mbi)) == sizeof(mbi) &&
               mbi.AllocationBase == g_selfModule;
    };

    const auto liveTexCoord = ReadQgl<GlTexCoord2fFn>(kQglTexCoord2fRva);
    const auto liveColor4f = ReadQgl<GlColor4fFn>(kQglColor4fRva);
    const auto liveVertex3f = ReadQgl<GlVertex3fFn>(kQglVertex3fRva);

    return ReadQgl<GlBeginFn>(kQglBeginRva) == g_begin &&
           ReadQgl<GlEndFn>(kQglEndRva) == g_end &&
           ReadQgl<GlColor4ubvFn>(kQglColor4ubvRva) == g_color4ubv &&
           selfOrExpected(reinterpret_cast<const void*>(liveColor4f),
                          reinterpret_cast<const void*>(g_color4f)) &&
           selfOrExpected(reinterpret_cast<const void*>(liveTexCoord),
                          reinterpret_cast<const void*>(g_texCoord2f)) &&
           ReadQgl<GlVertex3fvFn>(kQglVertex3fvRva) == g_vertex3fv &&
           selfOrExpected(reinterpret_cast<const void*>(liveVertex3f),
                          reinterpret_cast<const void*>(g_vertex3f)) &&
           ReadQgl<GlDrawArraysFn>(kQglDrawArraysRva) == g_drawArrays &&
           ReadQgl<GlEnableClientStateFn>(kQglEnableClientStateRva) ==
               g_enableClientState &&
           ReadQgl<GlVertexPointerFn>(kQglVertexPointerRva) ==
               g_vertexPointer &&
           ReadQgl<GlTexCoordPointerFn>(kQglTexCoordPointerRva) ==
               g_texCoordPointer &&
           ReadQgl<GlColorPointerFn>(kQglColorPointerRva) ==
               g_colorPointer &&
           ReadQgl<GlPushClientAttribFn>(kQglPushClientAttribRva) ==
               g_pushClientAttrib &&
           ReadQgl<GlPopClientAttribFn>(kQglPopClientAttribRva) ==
               g_popClientAttrib &&
           ReadQgl<GlGetIntegervFn>(kQglGetIntegervRva) ==
               g_getIntegerv &&
           ReadQgl<GlGetFloatvFn>(kQglGetFloatvRva) == g_getFloatv &&
           ReadQgl<GlIsEnabledFn>(kQglIsEnabledRva) == g_isEnabled &&
           ReadQgl<GlClientActiveTextureFn>(kQglClientActiveTextureRva) ==
               g_clientActiveTexture &&
           ReadQgl<GlBindBufferFn>(kQglBindBufferRva) == g_bindBuffer &&
           ReadQgl<GlDeleteBuffersFn>(kQglDeleteBuffersRva) ==
               g_deleteBuffers &&
           ReadQgl<GlGenBuffersFn>(kQglGenBuffersRva) == g_genBuffers &&
           ReadQgl<GlBufferDataFn>(kQglBufferDataRva) == g_bufferData;
}

bool CaptureReady()
{
    g_lastArrayReject = false;
    if (!g_enabled || !rendererperf::Enabled() || !GlSlotsStable() ||
        !EnsureBuffers())
        return false;

    // Immediate mode ignores client arrays, glDrawArrays consumes them. Use
    // the same conservative gate as the validated Studio array path and leave
    // any foreign state completely untouched.
    if (g_isEnabled(GL_NORMAL_ARRAY) || g_isEnabled(GL_INDEX_ARRAY) ||
        g_isEnabled(GL_EDGE_FLAG_ARRAY) ||
        g_isEnabled(GL_FOG_COORDINATE_ARRAY) ||
        g_isEnabled(GL_SECONDARY_COLOR_ARRAY))
    {
        g_lastArrayReject = true;
        return false;
    }

    int checkClientTexture = static_cast<int>(GL_TEXTURE0);
    int maxTextureUnits = 0;
    g_getIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &checkClientTexture);
    g_getIntegerv(GL_MAX_TEXTURE_UNITS, &maxTextureUnits);
    if (maxTextureUnits < 1 || maxTextureUnits > 32)
    {
        g_lastArrayReject = true;
        return false;
    }
    for (int unit = 1; unit < maxTextureUnits; ++unit)
    {
        g_clientActiveTexture(
            GL_TEXTURE0 + static_cast<unsigned>(unit));
        if (g_isEnabled(GL_TEXTURE_COORD_ARRAY))
        {
            g_clientActiveTexture(
                static_cast<unsigned>(checkClientTexture));
            g_lastArrayReject = true;
            return false;
        }
    }
    g_clientActiveTexture(
        static_cast<unsigned>(checkClientTexture));

    int arrayBuffer = 0;
    g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
    // Gold's immediate particle path owns no VBO. If another renderer/mod does,
    // leave the exact stock calls untouched.
    return arrayBuffer == 0;
}

enum class CallbackGuardResult
{
    Safe,
    CustomCallback,
    ExpiredDeathCallback,
    InvalidList
};

CallbackGuardResult ParticleCallbacksRequireStock()
{
    // R_DrawParticles runs R_FreeDeadParticles after qglBegin, and type 10
    // invokes an external callback from inside the particle loop. Either
    // callback may depend on stock immediate-mode Begin/End nesting, so any
    // callback-bearing list is conservatively left entirely stock.
    __try
    {
        // R_FreeDeadParticles at hw+0x86E70 compares the engine's double
        // current time against particle_t::die promoted from float. Use that
        // exact value/criterion so a death callback can never run after we
        // suppressed the stock glBegin.
        const double clientTime =
            *reinterpret_cast<const double*>(
                g_hwBase + kClientTimeDoubleRva);

        auto* particle =
            *reinterpret_cast<std::uint8_t**>(g_hwBase + kActiveParticlesRva);
        int guard = 0;
        while (particle && guard++ < 65536)
        {
            const int type = *reinterpret_cast<int*>(particle + 0x28);
            void* deathfunc = *reinterpret_cast<void**>(particle + 0x2C);
            void* callback = *reinterpret_cast<void**>(particle + 0x30);
            const float die = *reinterpret_cast<float*>(particle + 0x24);
            if (type == 10 && callback)
                return CallbackGuardResult::CustomCallback;
            if (deathfunc &&
                clientTime > static_cast<double>(die))
                return CallbackGuardResult::ExpiredDeathCallback;
            particle = *reinterpret_cast<std::uint8_t**>(particle + 0x10);
        }
        return guard >= 65536 ? CallbackGuardResult::InvalidList
                              : CallbackGuardResult::Safe;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return CallbackGuardResult::InvalidList;
    }
}

void AppendVertex(float x, float y, float z)
{
    ParticleVertex v{};
    v.xyz[0] = x;
    v.xyz[1] = y;
    v.xyz[2] = z;
    v.st[0] = g_currentTex[0];
    v.st[1] = g_currentTex[1];
    std::memcpy(v.rgba, g_currentColorBytes, sizeof(v.rgba));
    g_vertices.push_back(v);
}

unsigned char ColorByte(float value)
{
    if (value <= 0.0f)
        return 0;
    if (value >= 1.0f)
        return 255;
    return static_cast<unsigned char>(value * 255.0f + 0.5f);
}

void WINAPI ParticleBegin(unsigned mode)
{
    const CallbackGuardResult guard = ParticleCallbacksRequireStock();
    const bool slotsStable = GlSlotsStable();
    bool buffersReady = false;
    if (mode == GL_TRIANGLES && guard == CallbackGuardResult::Safe &&
        g_enabled && rendererperf::Enabled() && slotsStable)
    {
        buffersReady = CaptureReady();
    }

    if (mode == GL_TRIANGLES && guard == CallbackGuardResult::Safe &&
        buffersReady)
    {
        g_capture = true;
        g_vertices.clear();
        g_touchedColor = false;
        g_touchedTex = false;
        g_getFloatv(GL_CURRENT_COLOR, g_currentColor);
        for (int i = 0; i < 4; ++i)
            g_currentColorBytes[i] = ColorByte(g_currentColor[i]);
        if (g_collectStats)
            ++g_stats.captures;
        return;
    }
    if (g_collectStats)
    {
        ++g_stats.fallbacks;
        if (guard == CallbackGuardResult::CustomCallback)
            ++g_stats.callbackFallbacks;
        else if (guard == CallbackGuardResult::ExpiredDeathCallback)
            ++g_stats.deathFallbacks;
        else if (guard == CallbackGuardResult::InvalidList)
            ++g_stats.listFallbacks;
        else if (!slotsStable)
            ++g_stats.slotFallbacks;
        else if (g_lastArrayReject)
            ++g_stats.arrayFallbacks;
        else if (g_enabled && rendererperf::Enabled() && !buffersReady)
            ++g_stats.bufferFallbacks;
    }
    if (auto live = ReadQgl<GlBeginFn>(kQglBeginRva))
        live(mode);
}

void WINAPI ParticleColor4ubv(const unsigned char* rgba)
{
    if (!g_capture)
    {
        if (auto live = ReadQgl<GlColor4ubvFn>(kQglColor4ubvRva))
            live(rgba);
        return;
    }
    if (!rgba)
        return;
    constexpr float inv255 = 1.0f / 255.0f;
    g_currentColor[0] = static_cast<float>(rgba[0]) * inv255;
    g_currentColor[1] = static_cast<float>(rgba[1]) * inv255;
    g_currentColor[2] = static_cast<float>(rgba[2]) * inv255;
    g_currentColor[3] = static_cast<float>(rgba[3]) * inv255;
    std::memcpy(g_currentColorBytes, rgba, sizeof(g_currentColorBytes));
    g_touchedColor = true;
}

void WINAPI ParticleTexCoord2f(float s, float t)
{
    if (!g_capture)
    {
        if (auto live = ReadQgl<GlTexCoord2fFn>(kQglTexCoord2fRva))
            live(s, t);
        return;
    }
    g_currentTex[0] = s;
    g_currentTex[1] = t;
    g_touchedTex = true;
}

void WINAPI ParticleVertex3fv(const float* vertex)
{
    if (!g_capture)
    {
        if (auto live = ReadQgl<GlVertex3fvFn>(kQglVertex3fvRva))
            live(vertex);
        return;
    }
    if (vertex)
        AppendVertex(vertex[0], vertex[1], vertex[2]);
}

void WINAPI ParticleVertex3f(float x, float y, float z)
{
    if (!g_capture)
    {
        if (auto live = ReadQgl<GlVertex3fFn>(kQglVertex3fRva))
            live(x, y, z);
        return;
    }
    AppendVertex(x, y, z);
}

void DrawCaptured()
{
    if (g_vertices.empty())
    {
        if (g_collectStats)
            ++g_stats.emptyCaptures;
        return;
    }

    int previousBuffer = 0;
    int previousClientTexture = static_cast<int>(GL_TEXTURE0);
    g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    g_getIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &previousClientTexture);
    const std::size_t bytes =
        g_vertices.size() * sizeof(ParticleVertex);

    if (bytes > g_vboCapacity)
    {
        std::size_t newCapacity = g_vboCapacity;
        while (newCapacity < bytes &&
               newCapacity <= (static_cast<std::size_t>(-1) >> 1))
        {
            newCapacity <<= 1;
        }
        if (newCapacity < bytes)
            return;

        for (const unsigned id : g_vbos)
        {
            g_bindBuffer(GL_ARRAY_BUFFER, id);
            g_bufferData(GL_ARRAY_BUFFER,
                         static_cast<std::ptrdiff_t>(newCapacity),
                         nullptr, GL_STREAM_DRAW);
        }
        g_vboCapacity = newCapacity;
        g_bindBuffer(GL_ARRAY_BUFFER,
                     static_cast<unsigned>(previousBuffer));
    }

    const unsigned vbo = g_vbos[g_vboFrame++ % 3u];
    g_pushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    g_clientActiveTexture(GL_TEXTURE0);
    g_bindBuffer(GL_ARRAY_BUFFER, vbo);
    g_bufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<std::ptrdiff_t>(bytes),
                    g_vertices.data());
    g_enableClientState(GL_VERTEX_ARRAY);
    g_enableClientState(GL_TEXTURE_COORD_ARRAY);
    g_enableClientState(GL_COLOR_ARRAY);
    g_vertexPointer(3, GL_FLOAT, sizeof(ParticleVertex),
                    reinterpret_cast<const void*>(
                        offsetof(ParticleVertex, xyz)));
    g_texCoordPointer(2, GL_FLOAT, sizeof(ParticleVertex),
                      reinterpret_cast<const void*>(
                          offsetof(ParticleVertex, st)));
    g_colorPointer(4, GL_UNSIGNED_BYTE, sizeof(ParticleVertex),
                   reinterpret_cast<const void*>(
                       offsetof(ParticleVertex, rgba)));
    g_drawArrays(GL_TRIANGLES, 0, static_cast<int>(g_vertices.size()));
    g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previousBuffer));
    g_popClientAttrib();
    g_clientActiveTexture(static_cast<unsigned>(previousClientTexture));

    // Immediate mode leaves current color/texcoord persistent. Client arrays
    // make current color indeterminate, so explicitly reproduce Gold's final
    // observable values.
    if (g_touchedColor)
    {
        if (auto live = ReadQgl<GlColor4fFn>(kQglColor4fRva))
            live(g_currentColor[0], g_currentColor[1],
                 g_currentColor[2], g_currentColor[3]);
    }
    if (g_touchedTex)
    {
        if (auto live = ReadQgl<GlTexCoord2fFn>(kQglTexCoord2fRva))
            live(g_currentTex[0], g_currentTex[1]);
    }

    if (g_collectStats)
    {
        g_stats.vertices += g_vertices.size();
        ++g_stats.uploads;
    }
}

void WINAPI ParticleEnd()
{
    if (!g_capture)
    {
        if (auto live = ReadQgl<GlEndFn>(kQglEndRva))
            live();
        return;
    }
    g_capture = false;
    DrawCaptured();
}
} // namespace

bool Install(HMODULE hw, cl_enginefunc_t* engine)
{
    if (!hwbuild::MatchesTarget(hw) || !engine ||
        !engine->pfnRegisterVariable)
    {
        rendererlog::Line(
            "particlevbo: exact hw.dll/engine unavailable, disabled");
        return false;
    }
    g_hwBase = reinterpret_cast<std::uint8_t*>(hw);
    g_engine = engine;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&Install), &g_selfModule);
    if (!GlReady())
    {
        rendererlog::Line("particlevbo: required GL entrypoints unavailable");
        return false;
    }

    __try
    {
        g_cvar = engine->pfnRegisterVariable("r_particle_vbo", "1", 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_cvar = nullptr;
    }

    g_vertices.reserve(8192);

    CallPatch patches[] = {
        {g_hwBase + kParticleBeginCallRva, {}, reinterpret_cast<void*>(&ParticleBegin), kQglBeginRva},
        {g_hwBase + kParticleColorCallRva, {}, reinterpret_cast<void*>(&ParticleColor4ubv), kQglColor4ubvRva},
        {g_hwBase + kParticleTex0CallRva, {}, reinterpret_cast<void*>(&ParticleTexCoord2f), kQglTexCoord2fRva},
        {g_hwBase + kParticleVertex0CallRva, {}, reinterpret_cast<void*>(&ParticleVertex3fv), kQglVertex3fvRva},
        {g_hwBase + kParticleTex1CallRva, {}, reinterpret_cast<void*>(&ParticleTexCoord2f), kQglTexCoord2fRva},
        {g_hwBase + kParticleVertex1CallRva, {}, reinterpret_cast<void*>(&ParticleVertex3f), kQglVertex3fRva},
        {g_hwBase + kParticleTex2CallRva, {}, reinterpret_cast<void*>(&ParticleTexCoord2f), kQglTexCoord2fRva},
        {g_hwBase + kParticleVertex2CallRva, {}, reinterpret_cast<void*>(&ParticleVertex3f), kQglVertex3fRva},
        {g_hwBase + kParticleEndCallRva, {}, reinterpret_cast<void*>(&ParticleEnd), kQglEndRva},
    };

    for (const CallPatch& patch : patches)
    {
        if (!VerifyIndirectCall(patch))
        {
            rendererlog::Line(
                "particlevbo: R_DrawParticles callsite verification failed at hw+0x%X",
                static_cast<unsigned>(patch.address - g_hwBase));
            return false;
        }
    }

    std::size_t installed = 0;
    for (; installed < sizeof(patches) / sizeof(patches[0]); ++installed)
    {
        if (!WriteDirectCall(patches[installed]))
            break;
    }
    if (installed != sizeof(patches) / sizeof(patches[0]))
    {
        while (installed > 0)
            RestorePatch(patches[--installed]);
        rendererlog::Line(
            "particlevbo: transactional callsite patch failed, disabled");
        return false;
    }

    g_contextGeneration = worldvbo::ContextGeneration();
    g_installed = true;
    rendererlog::Line(
        "particlevbo: exact R_DrawParticles streaming capture installed (r_particle_vbo default 1)");
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
} // namespace particlevbo
