#include "gl_state.h"
#include "hw_build.h"
#include "inline_hook.h"
#include "perf_control.h"
#include "profile.h"
#include "studio_renderer.h"
#include "world_vbo.h"
#include "log.h"
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace glstate
{
namespace
{
// Exact renderer dispatch slots in this GoldClient hw.dll. qgl calls dominate
// the renderer, the OPENGL32 IAT is only a small fallback path.
constexpr std::uintptr_t kQglDisableRva = 0x027E3DF8u;
constexpr std::uintptr_t kQglEnableRva  = 0x027E3DFCu;
constexpr std::uintptr_t kQglAlphaFuncRva = 0x027E3818u;
constexpr std::uintptr_t kQglBlendFuncRva = 0x027E3DE8u;
constexpr std::uintptr_t kQglDepthFuncRva = 0x027E37D8u;
constexpr std::uintptr_t kQglDepthMaskRva = 0x027E376Cu;
constexpr std::uintptr_t kQglTexEnviRva = 0x027E3744u;
constexpr std::uintptr_t kQglActiveTextureRva = 0x027E3784u;
constexpr std::uintptr_t kQglNewListRva = 0x027E3B70u;
constexpr std::uintptr_t kQglEndListRva = 0x027E3CD0u;
constexpr std::uintptr_t kQglCallListRva = 0x027E3DA0u;
constexpr std::uintptr_t kQglCallListsRva = 0x027E3D9Cu;
constexpr std::uintptr_t kQglPopAttribRva = 0x027E3B18u;
constexpr std::uintptr_t kIatDisableRva = 0x001FA2A8u;
constexpr std::uintptr_t kIatEnableRva  = 0x001FA2ACu;
constexpr std::uintptr_t kIatTexEnviRva = 0x001FA298u;

constexpr std::uintptr_t kStudioStateFnRva = 0x0009B920u;
constexpr std::uintptr_t kStudioStateCaller0Rva = 0x0009BD41u;
constexpr std::uintptr_t kStudioStateCaller1Rva = 0x0009BD6Du;
constexpr std::uintptr_t kStudioDrawPointsFnRva = 0x0009B570u;
constexpr std::uintptr_t kStudioDrawPointsCallerRva = 0x0009BB4Au;
constexpr std::uintptr_t kStudioDebugRva = 0x03339440u;
constexpr std::uintptr_t kGoldCurrentTmuRva = 0x027E35E0u;

constexpr unsigned GL_TEXTURE0 = 0x84C0u;
constexpr unsigned GL_TEXTURE_ENV = 0x2300u;
constexpr unsigned GL_TEXTURE_ENV_MODE = 0x2200u;

constexpr unsigned GL_ALPHA_TEST          = 0x0BC0;
constexpr unsigned GL_BLEND               = 0x0BE2;
constexpr unsigned GL_CULL_FACE           = 0x0B44;
constexpr unsigned GL_DEPTH_TEST          = 0x0B71;
constexpr unsigned GL_DITHER              = 0x0BD0;
constexpr unsigned GL_FOG                 = 0x0B60;
constexpr unsigned GL_SCISSOR_TEST        = 0x0C11;
constexpr unsigned GL_STENCIL_TEST        = 0x0B90;
constexpr unsigned GL_POLYGON_OFFSET_FILL = 0x8037;

using GlCapFn = void (WINAPI*)(unsigned cap);
using GlAlphaFuncFn = void (WINAPI*)(unsigned func, float ref);
using GlBlendFuncFn = void (WINAPI*)(unsigned sfactor, unsigned dfactor);
using GlDepthFuncFn = void (WINAPI*)(unsigned func);
using GlDepthMaskFn = void (WINAPI*)(unsigned char flag);
using GlTexEnviFn = void (WINAPI*)(unsigned target, unsigned pname, int param);
using GlActiveTextureFn = void (WINAPI*)(unsigned texture);
using GlNewListFn = void (WINAPI*)(unsigned list, unsigned mode);
using GlEndListFn = void (WINAPI*)();
using GlCallListFn = void (WINAPI*)(unsigned list);
using GlCallListsFn = void (WINAPI*)(int count, unsigned type, const void* lists);
using GlPopAttribFn = void (WINAPI*)();
using StudioStateFn = void (__cdecl*)();
using StudioDrawPointsFn = void (__cdecl*)();

GlCapFn g_enable  = nullptr;
GlCapFn g_disable = nullptr;
GlAlphaFuncFn g_alphaFunc = nullptr;
GlBlendFuncFn g_blendFunc = nullptr;
GlDepthFuncFn g_depthFunc = nullptr;
GlDepthMaskFn g_depthMask = nullptr;
GlTexEnviFn g_texEnvi = nullptr;
GlActiveTextureFn g_activeTexture = nullptr;
GlNewListFn g_newList = nullptr;
GlEndListFn g_endList = nullptr;
GlCallListFn g_callList = nullptr;
GlCallListsFn g_callLists = nullptr;
GlPopAttribFn g_popAttrib = nullptr;
HMODULE g_hw = nullptr;
using DrawWorldFn = void (__cdecl*)();
DrawWorldFn g_drawWorld = nullptr;
StudioStateFn g_studioStateFn = nullptr;
StudioDrawPointsFn g_studioDrawPointsFn = nullptr;

DWORD g_scopeThread = 0;
LONG  g_scopeDepth  = 0;
volatile LONG g_installed = 0;
LONG g_listCompileDepth = 0;

// 0 = unknown, 1 = disabled, 2 = enabled. We deliberately do not cache
// GL_TEXTURE_2D because its enable bit is per active texture unit.
unsigned char g_capState[9]{};
bool g_alphaKnown = false;
unsigned g_alphaFuncState = 0;
std::uint32_t g_alphaRefBits = 0;
bool g_blendKnown = false;
unsigned g_blendSrc = 0;
unsigned g_blendDst = 0;
bool g_depthKnown = false;
unsigned g_depthFuncState = 0;
bool g_activeTextureKnown = false;
unsigned g_activeTextureUnit = 0;
struct TexEnvModeState
{
    bool known;
    int value;
};
TexEnvModeState g_texEnvMode[32]{};
ProfileStats g_profileStats{};
bool g_collectProfileStats = false;

bool g_studioShadowInstalled = false;
bool g_studioShadowActive = false;
DWORD g_studioShadowThread = 0;
unsigned char g_studioBlendEnable = 0; // 0 unknown, 1 disabled, 2 enabled
bool g_studioBlendFuncKnown = false;
unsigned g_studioBlendSrc = 0;
unsigned g_studioBlendDst = 0;
bool g_studioDepthMaskKnown = false;
unsigned char g_studioDepthMask = 0;
TexEnvModeState g_studioTexEnvMode[32]{};

int CapIndex(unsigned cap)
{
    switch (cap)
    {
    case GL_ALPHA_TEST:          return 0;
    case GL_BLEND:               return 1;
    case GL_CULL_FACE:           return 2;
    case GL_DEPTH_TEST:          return 3;
    case GL_DITHER:              return 4;
    case GL_FOG:                 return 5;
    case GL_SCISSOR_TEST:        return 6;
    case GL_STENCIL_TEST:        return 7;
    case GL_POLYGON_OFFSET_FILL: return 8;
    default:                     return -1;
    }
}

bool InScope()
{
    return g_installed != 0 && rendererperf::Enabled() && g_scopeDepth > 0 &&
           g_listCompileDepth == 0 && g_scopeThread == GetCurrentThreadId();
}

void InvalidateCaps()
{
    std::memset(g_capState, 0, sizeof(g_capState));
    g_alphaKnown = false;
    g_blendKnown = false;
    g_depthKnown = false;
    std::memset(g_texEnvMode, 0, sizeof(g_texEnvMode));
}

void EnterScope()
{
    if (g_installed == 0 || !rendererperf::Enabled())
        return;

    const DWORD thread = GetCurrentThreadId();
    if (g_scopeDepth == 0)
    {
        g_scopeThread = thread;
        InvalidateCaps();
        g_scopeDepth = 1;
        return;
    }

    if (g_scopeThread == thread)
        ++g_scopeDepth;
}

void LeaveScope()
{
    if (g_installed == 0 || g_scopeDepth <= 0 ||
        g_scopeThread != GetCurrentThreadId())
        return;

    const LONG depth = --g_scopeDepth;
    if (depth <= 0)
    {
        g_scopeDepth = 0;
        g_scopeThread = 0;
        InvalidateCaps();
    }
}

void __cdecl DrawWorld_Hook()
{
    if (!g_drawWorld)
        return;
    const bool profiling = prof::Active();
    const long long start = profiling ? prof::Now() : 0;
    worldvbo::BeginWorldScope();
    EnterScope();
    g_drawWorld();
    LeaveScope();
    worldvbo::EndWorldScope();
    if (profiling)
    {
        ++g_profileStats.drawWorldCalls;
        g_profileStats.drawWorldTicks += prof::Now() - start;
    }
}

void WINAPI Enable_Hook(unsigned cap)
{
    if (!g_enable)
        return;

    if (InScope())
    {
        const int index = CapIndex(cap);
        if (index >= 0)
        {
            if (g_capState[index] == 2)
            {
                if (g_collectProfileStats) ++g_profileStats.enableDisableSkipped;
                return;
            }
            g_enable(cap);
            g_capState[index] = 2;
            return;
        }
    }
    g_enable(cap);
}

void WINAPI Disable_Hook(unsigned cap)
{
    if (!g_disable)
        return;

    if (InScope())
    {
        const int index = CapIndex(cap);
        if (index >= 0)
        {
            if (g_capState[index] == 1)
            {
                if (g_collectProfileStats) ++g_profileStats.enableDisableSkipped;
                return;
            }
            g_disable(cap);
            g_capState[index] = 1;
            return;
        }
    }
    g_disable(cap);
}

void WINAPI AlphaFunc_Hook(unsigned func, float ref)
{
    if (!g_alphaFunc)
        return;
    if (InScope())
    {
        std::uint32_t refBits = 0;
        std::memcpy(&refBits, &ref, sizeof(refBits));
        if (g_alphaKnown && g_alphaFuncState == func && g_alphaRefBits == refBits)
        {
            if (g_collectProfileStats) ++g_profileStats.alphaSkipped;
            return;
        }
        g_alphaFunc(func, ref);
        g_alphaKnown = true;
        g_alphaFuncState = func;
        g_alphaRefBits = refBits;
        return;
    }
    g_alphaFunc(func, ref);
}

void WINAPI BlendFunc_Hook(unsigned sfactor, unsigned dfactor)
{
    if (!g_blendFunc)
        return;
    if (InScope())
    {
        if (g_blendKnown && g_blendSrc == sfactor && g_blendDst == dfactor)
        {
            if (g_collectProfileStats) ++g_profileStats.blendSkipped;
            return;
        }
        g_blendFunc(sfactor, dfactor);
        g_blendKnown = true;
        g_blendSrc = sfactor;
        g_blendDst = dfactor;
        return;
    }
    g_blendFunc(sfactor, dfactor);
}

void WINAPI DepthFunc_Hook(unsigned func)
{
    if (!g_depthFunc)
        return;
    if (InScope())
    {
        if (g_depthKnown && g_depthFuncState == func)
        {
            if (g_collectProfileStats) ++g_profileStats.depthSkipped;
            return;
        }
        g_depthFunc(func);
        g_depthKnown = true;
        g_depthFuncState = func;
        return;
    }
    g_depthFunc(func);
}

void WINAPI ActiveTexture_Hook(unsigned texture)
{
    if (!g_activeTexture)
        return;
    g_activeTexture(texture);
    if (g_listCompileDepth > 0 || texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + 32)
    {
        g_activeTextureKnown = false;
        return;
    }
    g_activeTextureUnit = texture - GL_TEXTURE0;
    g_activeTextureKnown = true;
}

void WINAPI TexEnvi_Hook(unsigned target, unsigned pname, int param)
{
    if (!g_texEnvi)
        return;

    if (InScope() && g_activeTextureKnown &&
        target == GL_TEXTURE_ENV && pname == GL_TEXTURE_ENV_MODE &&
        g_activeTextureUnit < 32)
    {
        TexEnvModeState& state = g_texEnvMode[g_activeTextureUnit];
        if (state.known && state.value == param)
        {
            if (g_collectProfileStats) ++g_profileStats.texEnvSkipped;
            return;
        }
        g_texEnvi(target, pname, param);
        state.known = true;
        state.value = param;
        return;
    }
    g_texEnvi(target, pname, param);
}

template <typename Fn>
Fn ReadQgl(std::uintptr_t rva)
{
    if (!g_hw)
        return nullptr;
    auto* base = reinterpret_cast<std::uint8_t*>(g_hw);
    return *reinterpret_cast<Fn*>(base + rva);
}

void ResetStudioShadow()
{
    g_studioBlendEnable = 0;
    g_studioBlendFuncKnown = false;
    g_studioDepthMaskKnown = false;
    std::memset(g_studioTexEnvMode, 0, sizeof(g_studioTexEnvMode));
}

bool StudioDebugAllowsShadow()
{
    if (!g_hw)
        return false;
    __try
    {
        auto* base = reinterpret_cast<std::uint8_t*>(g_hw);
        return *reinterpret_cast<const int*>(base + kStudioDebugRva) < 2;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

template <typename Fn>
bool StudioProviderMatches(std::uintptr_t rva, Fn raw, Fn ours)
{
    const Fn live = ReadQgl<Fn>(rva);
    return live && (live == raw || live == ours);
}

bool StudioProvidersStable()
{
    return g_hw && g_enable && g_disable && g_blendFunc && g_depthMask &&
           g_texEnvi &&
           StudioProviderMatches(kQglEnableRva, g_enable,
                                 static_cast<GlCapFn>(&Enable_Hook)) &&
           StudioProviderMatches(kQglDisableRva, g_disable,
                                 static_cast<GlCapFn>(&Disable_Hook)) &&
           StudioProviderMatches(kQglBlendFuncRva, g_blendFunc,
                                 static_cast<GlBlendFuncFn>(&BlendFunc_Hook)) &&
           StudioProviderMatches(kQglTexEnviRva, g_texEnvi,
                                 static_cast<GlTexEnviFn>(&TexEnvi_Hook)) &&
           ReadQgl<GlDepthMaskFn>(kQglDepthMaskRva) == g_depthMask;
}

bool StudioShadowCanCache()
{
    if (!g_studioShadowInstalled || !g_studioShadowActive ||
        g_studioShadowThread != GetCurrentThreadId() ||
        !rendererperf::Enabled() || !StudioDebugAllowsShadow() ||
        !StudioProvidersStable())
    {
        if (g_studioShadowActive)
        {
            g_studioShadowActive = false;
            ResetStudioShadow();
        }
        return false;
    }
    return true;
}

template <typename Fn>
Fn StudioLiveOrRaw(std::uintptr_t rva, Fn raw, Fn ours)
{
    const Fn live = ReadQgl<Fn>(rva);
    if (live && live != raw && live != ours)
        return live;
    return raw;
}

void WINAPI StudioTexEnvi_Hook(unsigned target, unsigned pname, int param)
{
    if (!g_texEnvi)
        return;
    if (!StudioShadowCanCache() || target != GL_TEXTURE_ENV ||
        pname != GL_TEXTURE_ENV_MODE)
    {
        if (auto fn = StudioLiveOrRaw(kQglTexEnviRva, g_texEnvi,
                                     static_cast<GlTexEnviFn>(&TexEnvi_Hook)))
            fn(target, pname, param);
        return;
    }

    int unit = -1;
    __try
    {
        auto* base = reinterpret_cast<std::uint8_t*>(g_hw);
        unit = *reinterpret_cast<const int*>(base + kGoldCurrentTmuRva);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        unit = -1;
    }
    if (unit < 0 || unit >= 32)
    {
        g_texEnvi(target, pname, param);
        return;
    }

    TexEnvModeState& state = g_studioTexEnvMode[unit];
    if (state.known && state.value == param)
    {
        if (g_collectProfileStats)
            ++g_profileStats.texEnvSkipped;
        return;
    }
    g_texEnvi(target, pname, param);
    state.known = true;
    state.value = param;
}

void WINAPI StudioBlendFunc_Hook(unsigned sfactor, unsigned dfactor)
{
    if (!g_blendFunc)
        return;
    if (!StudioShadowCanCache())
    {
        if (auto fn = StudioLiveOrRaw(kQglBlendFuncRva, g_blendFunc,
                                     static_cast<GlBlendFuncFn>(&BlendFunc_Hook)))
            fn(sfactor, dfactor);
        return;
    }
    if (g_studioBlendFuncKnown && g_studioBlendSrc == sfactor &&
        g_studioBlendDst == dfactor)
    {
        if (g_collectProfileStats)
            ++g_profileStats.blendSkipped;
        return;
    }
    g_blendFunc(sfactor, dfactor);
    g_studioBlendFuncKnown = true;
    g_studioBlendSrc = sfactor;
    g_studioBlendDst = dfactor;
}

void WINAPI StudioDepthMask_Hook(unsigned char flag)
{
    if (!g_depthMask)
        return;
    if (!StudioShadowCanCache())
    {
        const GlDepthMaskFn live = ReadQgl<GlDepthMaskFn>(kQglDepthMaskRva);
        (live ? live : g_depthMask)(flag);
        return;
    }
    if (g_studioDepthMaskKnown && g_studioDepthMask == flag)
    {
        if (g_collectProfileStats)
            ++g_profileStats.depthMaskSkipped;
        return;
    }
    g_depthMask(flag);
    g_studioDepthMaskKnown = true;
    g_studioDepthMask = flag;
}

void WINAPI StudioBlendEnable_Hook(unsigned cap)
{
    if (!g_enable)
        return;
    if (!StudioShadowCanCache() || cap != GL_BLEND)
    {
        if (auto fn = StudioLiveOrRaw(kQglEnableRva, g_enable,
                                     static_cast<GlCapFn>(&Enable_Hook)))
            fn(cap);
        return;
    }
    if (g_studioBlendEnable == 2)
    {
        if (g_collectProfileStats)
            ++g_profileStats.enableDisableSkipped;
        return;
    }
    g_enable(cap);
    g_studioBlendEnable = 2;
}

void WINAPI StudioBlendDisable_Hook(unsigned cap)
{
    if (!g_disable)
        return;
    if (!StudioShadowCanCache() || cap != GL_BLEND)
    {
        if (auto fn = StudioLiveOrRaw(kQglDisableRva, g_disable,
                                     static_cast<GlCapFn>(&Disable_Hook)))
            fn(cap);
        return;
    }
    if (g_studioBlendEnable == 1)
    {
        if (g_collectProfileStats)
            ++g_profileStats.enableDisableSkipped;
        return;
    }
    g_disable(cap);
    g_studioBlendEnable = 1;
}

void StudioStateScopeCallImpl(bool flushGlowShellBase)
{
    if (!g_studioStateFn)
        return;

    ResetStudioShadow();
    g_studioShadowThread = 0;
    g_studioShadowActive = false;
    if (g_studioShadowInstalled && rendererperf::Enabled() &&
        StudioDebugAllowsShadow() && StudioProvidersStable())
    {
        g_studioShadowThread = GetCurrentThreadId();
        g_studioShadowActive = true;
    }

    g_studioStateFn();

    g_studioShadowActive = false;
    g_studioShadowThread = 0;
    ResetStudioShadow();

    if (flushGlowShellBase)
    {
        // hw+9BD41 is reached only by the GlowShell branch after Gold has
        // temporarily changed entity->renderfx 19 -> 0.  The retained Studio
        // kernel therefore sees an ordinary opaque player and may record that
        // base pass.  It must become framebuffer-visible here, while Gold's
        // first-pass state is still opaque and before the foreign callback at
        // hw+9BD55 / the second additive Studio pass.  Otherwise the later
        // renderfx=19 fallback flushes the opaque packet after ONE/ONE blend
        // has already been enabled, making the player look additive/transparent.
        studio_renderer::FlushDeferredStudioCommands();
    }
}

void __cdecl StudioGlowShellBaseScopeCall()
{
    StudioStateScopeCallImpl(true);
}

void __cdecl StudioStateScopeCall()
{
    StudioStateScopeCallImpl(false);
}

void __cdecl StudioDrawPointsBarrierCall()
{
    if (!g_studioDrawPointsFn)
        return;

    // R_StudioDrawPoints mutates Blend/BlendFunc/DepthMask at internal
    // callsites that are deliberately not part of the exact outer Studio
    // shadow.  Forget all cached outer state before and after crossing that
    // nested renderer so no later outer call can be skipped from stale data.
    ResetStudioShadow();
    g_studioDrawPointsFn();
    ResetStudioShadow();
}

void WINAPI NewList_Hook(unsigned list, unsigned mode)
{
    if (!g_newList)
        return;
    ++g_listCompileDepth;
    InvalidateCaps();
    g_newList(list, mode);
}

void WINAPI EndList_Hook()
{
    if (!g_endList)
        return;
    g_endList();
    if (g_listCompileDepth > 0)
        --g_listCompileDepth;
    InvalidateCaps();
}

void WINAPI CallList_Hook(unsigned list)
{
    if (!g_callList)
        return;
    g_callList(list);
    g_activeTextureKnown = false;
    InvalidateCaps();
}

void WINAPI CallLists_Hook(int count, unsigned type, const void* lists)
{
    if (!g_callLists)
        return;
    g_callLists(count, type, lists);
    g_activeTextureKnown = false;
    InvalidateCaps();
}

void WINAPI PopAttrib_Hook()
{
    if (!g_popAttrib)
        return;
    g_popAttrib();
    g_activeTextureKnown = false;
    InvalidateCaps();
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

struct RelativeCallPatch
{
    std::uint8_t* address = nullptr;
    std::uint8_t original[5]{};
    void* expectedTarget = nullptr;
    void* replacement = nullptr;
};

struct IndirectCallPatch
{
    std::uint8_t* address = nullptr;
    std::uint8_t original[6]{};
    std::uintptr_t expectedSlotRva = 0;
    void* replacement = nullptr;
};

bool VerifyRelativeCall(const RelativeCallPatch& patch)
{
    if (!patch.address || patch.address[0] != 0xE8 ||
        !patch.expectedTarget || !patch.replacement)
        return false;
    const std::int32_t rel =
        *reinterpret_cast<const std::int32_t*>(patch.address + 1);
    return patch.address + 5 + rel == patch.expectedTarget;
}

bool WriteRelativeCall(RelativeCallPatch& patch)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(patch.address, sizeof(patch.original),
                        PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    std::memcpy(patch.original, patch.address, sizeof(patch.original));
    patch.address[0] = 0xE8;
    *reinterpret_cast<std::int32_t*>(patch.address + 1) =
        static_cast<std::int32_t>(
            reinterpret_cast<std::uint8_t*>(patch.replacement) -
            (patch.address + 5));
    DWORD ignored = 0;
    VirtualProtect(patch.address, sizeof(patch.original), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), patch.address,
                          sizeof(patch.original));
    return true;
}

void RestoreRelativeCall(RelativeCallPatch& patch)
{
    if (!patch.address)
        return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(patch.address, sizeof(patch.original),
                        PAGE_EXECUTE_READWRITE, &oldProtect))
        return;
    std::memcpy(patch.address, patch.original, sizeof(patch.original));
    DWORD ignored = 0;
    VirtualProtect(patch.address, sizeof(patch.original), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), patch.address,
                          sizeof(patch.original));
}

bool VerifyIndirectCall(const IndirectCallPatch& patch)
{
    if (!patch.address || patch.address[0] != 0xFF ||
        patch.address[1] != 0x15 || !patch.replacement || !g_hw)
        return false;
    const std::uintptr_t operand =
        static_cast<std::uintptr_t>(
            *reinterpret_cast<const std::uint32_t*>(patch.address + 2));
    auto* base = reinterpret_cast<std::uint8_t*>(g_hw);
    return operand == reinterpret_cast<std::uintptr_t>(
                          base + patch.expectedSlotRva);
}

bool WriteIndirectCall(IndirectCallPatch& patch)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect(patch.address, sizeof(patch.original),
                        PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    std::memcpy(patch.original, patch.address, sizeof(patch.original));
    patch.address[0] = 0xE8;
    *reinterpret_cast<std::int32_t*>(patch.address + 1) =
        static_cast<std::int32_t>(
            reinterpret_cast<std::uint8_t*>(patch.replacement) -
            (patch.address + 5));
    patch.address[5] = 0x90;
    DWORD ignored = 0;
    VirtualProtect(patch.address, sizeof(patch.original), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), patch.address,
                          sizeof(patch.original));
    return true;
}

void RestoreIndirectCall(IndirectCallPatch& patch)
{
    if (!patch.address)
        return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(patch.address, sizeof(patch.original),
                        PAGE_EXECUTE_READWRITE, &oldProtect))
        return;
    std::memcpy(patch.address, patch.original, sizeof(patch.original));
    DWORD ignored = 0;
    VirtualProtect(patch.address, sizeof(patch.original), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), patch.address,
                          sizeof(patch.original));
}

bool InstallStudioStateShadow()
{
    g_studioShadowInstalled = false;
    if (!g_hw || !StudioProvidersStable())
        return false;

    auto* base = reinterpret_cast<std::uint8_t*>(g_hw);
    g_studioStateFn = reinterpret_cast<StudioStateFn>(
        base + kStudioStateFnRva);
    g_studioDrawPointsFn = reinterpret_cast<StudioDrawPointsFn>(
        base + kStudioDrawPointsFnRva);

    RelativeCallPatch scopePatches[] = {
        {base + kStudioStateCaller0Rva, {},
         reinterpret_cast<void*>(g_studioStateFn),
         reinterpret_cast<void*>(&StudioGlowShellBaseScopeCall)},
        {base + kStudioStateCaller1Rva, {},
         reinterpret_cast<void*>(g_studioStateFn),
         reinterpret_cast<void*>(&StudioStateScopeCall)},
        {base + kStudioDrawPointsCallerRva, {},
         reinterpret_cast<void*>(g_studioDrawPointsFn),
         reinterpret_cast<void*>(&StudioDrawPointsBarrierCall)},
    };

    IndirectCallPatch statePatches[] = {
        {base + 0x0009B9E4u, {}, kQglTexEnviRva,
         reinterpret_cast<void*>(&StudioTexEnvi_Hook)},
        {base + 0x0009BA9Cu, {}, kQglTexEnviRva,
         reinterpret_cast<void*>(&StudioTexEnvi_Hook)},
        {base + 0x0009BAB6u, {}, kQglTexEnviRva,
         reinterpret_cast<void*>(&StudioTexEnvi_Hook)},
        {base + 0x0009BCC5u, {}, kQglTexEnviRva,
         reinterpret_cast<void*>(&StudioTexEnvi_Hook)},

        {base + 0x0009BA87u, {}, kQglBlendFuncRva,
         reinterpret_cast<void*>(&StudioBlendFunc_Hook)},
        {base + 0x0009BAC5u, {}, kQglBlendFuncRva,
         reinterpret_cast<void*>(&StudioBlendFunc_Hook)},
        {base + 0x0009BB03u, {}, kQglBlendFuncRva,
         reinterpret_cast<void*>(&StudioBlendFunc_Hook)},
        {base + 0x0009BBC2u, {}, kQglBlendFuncRva,
         reinterpret_cast<void*>(&StudioBlendFunc_Hook)},

        {base + 0x0009BB39u, {}, kQglDepthMaskRva,
         reinterpret_cast<void*>(&StudioDepthMask_Hook)},
        {base + 0x0009BB51u, {}, kQglDepthMaskRva,
         reinterpret_cast<void*>(&StudioDepthMask_Hook)},

        {base + 0x0009BB44u, {}, kQglEnableRva,
         reinterpret_cast<void*>(&StudioBlendEnable_Hook)},
        {base + 0x0009BBCDu, {}, kQglEnableRva,
         reinterpret_cast<void*>(&StudioBlendEnable_Hook)},
        {base + 0x0009BC31u, {}, kQglDisableRva,
         reinterpret_cast<void*>(&StudioBlendDisable_Hook)},
        {base + 0x0009BCB0u, {}, kQglDisableRva,
         reinterpret_cast<void*>(&StudioBlendDisable_Hook)},
    };

    for (const RelativeCallPatch& patch : scopePatches)
        if (!VerifyRelativeCall(patch))
            return false;
    for (const IndirectCallPatch& patch : statePatches)
        if (!VerifyIndirectCall(patch))
            return false;

    std::size_t scopeInstalled = 0;
    for (; scopeInstalled < sizeof(scopePatches) / sizeof(scopePatches[0]);
         ++scopeInstalled)
    {
        if (!WriteRelativeCall(scopePatches[scopeInstalled]))
            break;
    }
    if (scopeInstalled != sizeof(scopePatches) / sizeof(scopePatches[0]))
    {
        while (scopeInstalled > 0)
            RestoreRelativeCall(scopePatches[--scopeInstalled]);
        return false;
    }

    std::size_t stateInstalled = 0;
    for (; stateInstalled < sizeof(statePatches) / sizeof(statePatches[0]);
         ++stateInstalled)
    {
        if (!WriteIndirectCall(statePatches[stateInstalled]))
            break;
    }
    if (stateInstalled != sizeof(statePatches) / sizeof(statePatches[0]))
    {
        while (stateInstalled > 0)
            RestoreIndirectCall(statePatches[--stateInstalled]);
        while (scopeInstalled > 0)
            RestoreRelativeCall(scopePatches[--scopeInstalled]);
        return false;
    }

    ResetStudioShadow();
    g_studioShadowInstalled = true;
    return true;
}

void RefreshDispatchSlots()
{
    if (!g_hw || !g_enable || !g_disable)
        return;

    auto* base = reinterpret_cast<std::uint8_t*>(g_hw);
    auto** qEnable  = reinterpret_cast<void**>(base + kQglEnableRva);
    auto** qDisable = reinterpret_cast<void**>(base + kQglDisableRva);
    auto** qAlphaFunc = reinterpret_cast<void**>(base + kQglAlphaFuncRva);
    auto** qBlendFunc = reinterpret_cast<void**>(base + kQglBlendFuncRva);
    auto** qDepthFunc = reinterpret_cast<void**>(base + kQglDepthFuncRva);
    auto** qTexEnvi = reinterpret_cast<void**>(base + kQglTexEnviRva);
    auto** qActiveTexture = reinterpret_cast<void**>(base + kQglActiveTextureRva);
    auto** qNewList = reinterpret_cast<void**>(base + kQglNewListRva);
    auto** qEndList = reinterpret_cast<void**>(base + kQglEndListRva);
    auto** qCallList = reinterpret_cast<void**>(base + kQglCallListRva);
    auto** qCallLists = reinterpret_cast<void**>(base + kQglCallListsRva);
    auto** qPopAttrib = reinterpret_cast<void**>(base + kQglPopAttribRva);
    auto** iEnable  = reinterpret_cast<void**>(base + kIatEnableRva);
    auto** iDisable = reinterpret_cast<void**>(base + kIatDisableRva);
    auto** iTexEnvi = reinterpret_cast<void**>(base + kIatTexEnviRva);

    // Context recreation may repopulate qgl with the same driver pointers.
    // Reinstall only when a slot contains the exact original we observed.
    if (*qEnable == reinterpret_cast<void*>(g_enable))
        PatchPointer(qEnable, reinterpret_cast<void*>(g_enable), reinterpret_cast<void*>(&Enable_Hook));
    if (*qDisable == reinterpret_cast<void*>(g_disable))
        PatchPointer(qDisable, reinterpret_cast<void*>(g_disable), reinterpret_cast<void*>(&Disable_Hook));
    if (*qAlphaFunc == reinterpret_cast<void*>(g_alphaFunc))
        PatchPointer(qAlphaFunc, reinterpret_cast<void*>(g_alphaFunc), reinterpret_cast<void*>(&AlphaFunc_Hook));
    if (*qBlendFunc == reinterpret_cast<void*>(g_blendFunc))
        PatchPointer(qBlendFunc, reinterpret_cast<void*>(g_blendFunc), reinterpret_cast<void*>(&BlendFunc_Hook));
    if (*qDepthFunc == reinterpret_cast<void*>(g_depthFunc))
        PatchPointer(qDepthFunc, reinterpret_cast<void*>(g_depthFunc), reinterpret_cast<void*>(&DepthFunc_Hook));
    if (g_texEnvi && *qTexEnvi == reinterpret_cast<void*>(g_texEnvi))
        PatchPointer(qTexEnvi, reinterpret_cast<void*>(g_texEnvi), reinterpret_cast<void*>(&TexEnvi_Hook));
    if (g_activeTexture && *qActiveTexture == reinterpret_cast<void*>(g_activeTexture))
        PatchPointer(qActiveTexture, reinterpret_cast<void*>(g_activeTexture), reinterpret_cast<void*>(&ActiveTexture_Hook));
    if (*qNewList == reinterpret_cast<void*>(g_newList))
        PatchPointer(qNewList, reinterpret_cast<void*>(g_newList), reinterpret_cast<void*>(&NewList_Hook));
    if (*qEndList == reinterpret_cast<void*>(g_endList))
        PatchPointer(qEndList, reinterpret_cast<void*>(g_endList), reinterpret_cast<void*>(&EndList_Hook));
    if (*qCallList == reinterpret_cast<void*>(g_callList))
        PatchPointer(qCallList, reinterpret_cast<void*>(g_callList), reinterpret_cast<void*>(&CallList_Hook));
    if (*qCallLists == reinterpret_cast<void*>(g_callLists))
        PatchPointer(qCallLists, reinterpret_cast<void*>(g_callLists), reinterpret_cast<void*>(&CallLists_Hook));
    if (*qPopAttrib == reinterpret_cast<void*>(g_popAttrib))
        PatchPointer(qPopAttrib, reinterpret_cast<void*>(g_popAttrib), reinterpret_cast<void*>(&PopAttrib_Hook));

    HMODULE gl = GetModuleHandleA("opengl32.dll");
    if (gl)
    {
        void* coreEnable = reinterpret_cast<void*>(GetProcAddress(gl, "glEnable"));
        void* coreDisable = reinterpret_cast<void*>(GetProcAddress(gl, "glDisable"));
        void* coreTexEnvi = reinterpret_cast<void*>(GetProcAddress(gl, "glTexEnvi"));
        if (*iEnable == coreEnable)
            PatchPointer(iEnable, coreEnable, reinterpret_cast<void*>(&Enable_Hook));
        if (*iDisable == coreDisable)
            PatchPointer(iDisable, coreDisable, reinterpret_cast<void*>(&Disable_Hook));
        if (g_texEnvi && *iTexEnvi == coreTexEnvi)
            PatchPointer(iTexEnvi, coreTexEnvi, reinterpret_cast<void*>(&TexEnvi_Hook));
    }
}
} // namespace

bool Install(cl_enginefunc_t* /*engine*/)
{
    HMODULE hw = GetModuleHandleA("hw.dll");
    if (!hwbuild::MatchesTarget(hw))
    {
        rendererlog::Line("glstate: hw.dll build mismatch, state cache disabled");
        return false;
    }

    g_hw = hw;
    auto* base = reinterpret_cast<std::uint8_t*>(hw);
    auto** qEnable  = reinterpret_cast<void**>(base + kQglEnableRva);
    auto** qDisable = reinterpret_cast<void**>(base + kQglDisableRva);
    auto** qAlphaFunc = reinterpret_cast<void**>(base + kQglAlphaFuncRva);
    auto** qBlendFunc = reinterpret_cast<void**>(base + kQglBlendFuncRva);
    auto** qDepthFunc = reinterpret_cast<void**>(base + kQglDepthFuncRva);
    auto** qDepthMask = reinterpret_cast<void**>(base + kQglDepthMaskRva);
    auto** qTexEnvi = reinterpret_cast<void**>(base + kQglTexEnviRva);
    auto** qActiveTexture = reinterpret_cast<void**>(base + kQglActiveTextureRva);
    auto** qNewList = reinterpret_cast<void**>(base + kQglNewListRva);
    auto** qEndList = reinterpret_cast<void**>(base + kQglEndListRva);
    auto** qCallList = reinterpret_cast<void**>(base + kQglCallListRva);
    auto** qCallLists = reinterpret_cast<void**>(base + kQglCallListsRva);
    auto** qPopAttrib = reinterpret_cast<void**>(base + kQglPopAttribRva);
    if (!*qEnable || !*qDisable || !*qAlphaFunc || !*qBlendFunc || !*qDepthFunc ||
        !*qNewList || !*qEndList ||
        !*qCallList || !*qCallLists || !*qPopAttrib)
    {
        rendererlog::Line("glstate: qgl dispatch table not ready");
        return false;
    }

    g_enable = reinterpret_cast<GlCapFn>(*qEnable);
    g_disable = reinterpret_cast<GlCapFn>(*qDisable);
    g_alphaFunc = reinterpret_cast<GlAlphaFuncFn>(*qAlphaFunc);
    g_blendFunc = reinterpret_cast<GlBlendFuncFn>(*qBlendFunc);
    g_depthFunc = reinterpret_cast<GlDepthFuncFn>(*qDepthFunc);
    g_depthMask = reinterpret_cast<GlDepthMaskFn>(*qDepthMask);
    g_texEnvi = reinterpret_cast<GlTexEnviFn>(*qTexEnvi);
    g_activeTexture = reinterpret_cast<GlActiveTextureFn>(*qActiveTexture);
    g_newList = reinterpret_cast<GlNewListFn>(*qNewList);
    g_endList = reinterpret_cast<GlEndListFn>(*qEndList);
    g_callList = reinterpret_cast<GlCallListFn>(*qCallList);
    g_callLists = reinterpret_cast<GlCallListsFn>(*qCallLists);
    g_popAttrib = reinterpret_cast<GlPopAttribFn>(*qPopAttrib);
    if (!PatchPointer(qEnable, reinterpret_cast<void*>(g_enable), reinterpret_cast<void*>(&Enable_Hook)) ||
        !PatchPointer(qDisable, reinterpret_cast<void*>(g_disable), reinterpret_cast<void*>(&Disable_Hook)) ||
        !PatchPointer(qAlphaFunc, reinterpret_cast<void*>(g_alphaFunc), reinterpret_cast<void*>(&AlphaFunc_Hook)) ||
        !PatchPointer(qBlendFunc, reinterpret_cast<void*>(g_blendFunc), reinterpret_cast<void*>(&BlendFunc_Hook)) ||
        !PatchPointer(qDepthFunc, reinterpret_cast<void*>(g_depthFunc), reinterpret_cast<void*>(&DepthFunc_Hook)) ||
        !PatchPointer(qNewList, reinterpret_cast<void*>(g_newList), reinterpret_cast<void*>(&NewList_Hook)) ||
        !PatchPointer(qEndList, reinterpret_cast<void*>(g_endList), reinterpret_cast<void*>(&EndList_Hook)) ||
        !PatchPointer(qCallList, reinterpret_cast<void*>(g_callList), reinterpret_cast<void*>(&CallList_Hook)) ||
        !PatchPointer(qCallLists, reinterpret_cast<void*>(g_callLists), reinterpret_cast<void*>(&CallLists_Hook)) ||
        !PatchPointer(qPopAttrib, reinterpret_cast<void*>(g_popAttrib), reinterpret_cast<void*>(&PopAttrib_Hook)))
    {
        rendererlog::Line("glstate: qgl state/safety slot install failed, cache disabled");
        return false;
    }

    // Optional TMU-aware TexEnv cache. It becomes active only if both extension
    // dispatch slots were resolved by GoldClient.
    if (g_texEnvi && g_activeTexture)
    {
        PatchPointer(qTexEnvi, reinterpret_cast<void*>(g_texEnvi), reinterpret_cast<void*>(&TexEnvi_Hook));
        PatchPointer(qActiveTexture, reinterpret_cast<void*>(g_activeTexture), reinterpret_cast<void*>(&ActiveTexture_Hook));
    }

    g_installed = 1;
    RefreshDispatchSlots();

    if (InstallStudioStateShadow())
        rendererlog::Line(
            "glstate: exact Studio hw+0x9B920 callsite state shadow installed (2 scopes, 14 state sites)");
    else
        rendererlog::Line(
            "glstate: Studio callsite state shadow unavailable, Studio state remains stock");

    // Exact GoldClient build: R_DrawWorld = 0x10075F60 at preferred base
    // 0x10000000. ASLR-safe by resolving the RVA from the loaded module.
    auto* drawWorld = reinterpret_cast<std::uint8_t*>(hw) + 0x00075F60u;
    g_drawWorld = reinterpret_cast<DrawWorldFn>(
        inl::Hook(drawWorld, reinterpret_cast<void*>(&DrawWorld_Hook)));
    if (!g_drawWorld)
        rendererlog::Line("glstate: R_DrawWorld hook unavailable, world cache disabled");
    else
        rendererlog::Line("glstate: R_DrawWorld state-cache scope installed");

    rendererlog::Line("glstate: exact qgl state hooks installed (r_fastpath default 1)");
    return true;
}

void UpdateFrame()
{
    if (!g_installed)
        return;
    RefreshDispatchSlots();
}

void SetProfileCollection(bool enabled)
{
    g_collectProfileStats = enabled;
}

ProfileStats ConsumeProfileStats()
{
    ProfileStats out = g_profileStats;
    g_profileStats = {};
    return out;
}
} // namespace glstate
