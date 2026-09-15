#include "studio_fasttexture.h"
#include "hw_build.h"
#include "log.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

namespace studio_fasttexture
{
namespace
{
constexpr std::uintptr_t kResolverCallRva = 0x0009AFCCu;
constexpr std::uintptr_t kResolverRva = 0x00098AE0u;
constexpr std::uintptr_t kGlBindRva = 0x00064D40u;
constexpr std::uintptr_t kForceFlagsRva = 0x029839B0u;
constexpr std::uintptr_t kCurrentEntityRva = 0x0078F470u;
constexpr std::uintptr_t kQglGetIntegervRva = 0x027E3DD0u;

constexpr unsigned GL_TEXTURE_BINDING_2D = 0x8069u;
constexpr unsigned GL_ACTIVE_TEXTURE = 0x84E0u;

using ResolverFn = void (__cdecl*)(void* studioHeader, int textureSlot);
using GlBindFn = void (__fastcall*)(int textureUnit, int textureId);
using GlGetIntegervFn = void (WINAPI*)(unsigned pname, int* params);

std::uint8_t* g_hwBase = nullptr;
ResolverFn g_original = nullptr;
GlBindFn g_glBind = nullptr;
cvar_t* g_mode = nullptr;

std::uint64_t g_calls = 0;
std::uint64_t g_fast = 0;
std::uint64_t g_forceFast = 0;
std::uint64_t g_simpleFast = 0;
std::uint64_t g_nameFast = 0;
std::uint64_t g_stock = 0;
std::uint64_t g_validate = 0;
std::uint64_t g_validateMismatch = 0;
std::uint64_t g_validateUnavailable = 0;

enum class FastKind : std::uint8_t
{
    None,
    ForceChrome,
    SimpleEntity,
    OrdinaryName,
};

struct FastDecision
{
    FastKind kind = FastKind::None;
    int textureId = 0;
};

void LogStats()
{
    if ((g_calls & 0x3FFFFu) != 0)
        return;
    rendererlog::Line("fasttexture: calls=%llu fast=%llu force=%llu simple=%llu name=%llu stock=%llu",
               static_cast<unsigned long long>(g_calls),
               static_cast<unsigned long long>(g_fast),
               static_cast<unsigned long long>(g_forceFast),
               static_cast<unsigned long long>(g_simpleFast),
               static_cast<unsigned long long>(g_nameFast),
               static_cast<unsigned long long>(g_stock));
}

FastDecision Classify(void* studioHeader, int textureSlot)
{
    const std::uint32_t forceFlags =
        *reinterpret_cast<const std::uint32_t*>(g_hwBase + kForceFlagsRva);
    if ((forceFlags & 2u) != 0)
        return {FastKind::ForceChrome, 0};

    if (!studioHeader || textureSlot < 0 || !g_glBind)
        return {};

    const auto* header = static_cast<const std::uint8_t*>(studioHeader);
    const int textureIndex = *reinterpret_cast<const int*>(header + 0xB8);
    if (textureIndex < 0)
        return {};
    const auto* desc = header + textureIndex + static_cast<std::size_t>(textureSlot) * 0x50u;
    const int textureId = *reinterpret_cast<const int*>(desc + 0x4C);

    const auto* current = *reinterpret_cast<const std::uint8_t* const*>(
        g_hwBase + kCurrentEntityRva);
    if (!current)
        return {};

    const int entityIndex = *reinterpret_cast<const int*>(current);
    if (entityIndex <= 0)
        return {FastKind::SimpleEntity, textureId};

    // DM_Base.bmp can only begin D/d and every accepted remap form begins R/r.
    // For every other first byte Gold jumps directly to GL_Bind(base texture)
    // without touching the remap cache.  This negative gate therefore avoids
    // duplicating Gold's locale-sensitive CRT string semantics entirely.
    const unsigned char first = *desc;
    if (first != 'D' && first != 'd' && first != 'R' && first != 'r')
        return {FastKind::OrdinaryName, textureId};

    return {};
}

// Returns true when the exact Gold resolver result has already been produced.
// Anything that could be DM_Base/Remap, or whose inputs are not the exact normal
// StudioDrawPoints shape, fails open to the original resolver.
bool __cdecl FastDispatch(void* studioHeader, int textureSlot)
{
    ++g_calls;
    const FastDecision d = Classify(studioHeader, textureSlot);
    if (d.kind == FastKind::ForceChrome)
    {
        ++g_fast;
        ++g_forceFast;
        LogStats();
        return true;
    }
    if (d.kind == FastKind::SimpleEntity || d.kind == FastKind::OrdinaryName)
    {
        g_glBind(0, d.textureId);
        ++g_fast;
        if (d.kind == FastKind::SimpleEntity) ++g_simpleFast;
        else ++g_nameFast;
        LogStats();
        return true;
    }

    ++g_stock;
    LogStats();
    return false;
}

GlGetIntegervFn ReadGetIntegerv()
{
    __try
    {
        return *reinterpret_cast<GlGetIntegervFn*>(g_hwBase + kQglGetIntegervRva);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

void LogValidationStats()
{
    if ((g_validate & 0xFFFu) != 0)
        return;
    rendererlog::Line("fasttexture_validate: validate=%llu mismatch=%llu unavailable=%llu",
               static_cast<unsigned long long>(g_validate),
               static_cast<unsigned long long>(g_validateMismatch),
               static_cast<unsigned long long>(g_validateUnavailable));
}

void __cdecl ValidationDispatch(void* studioHeader, int textureSlot)
{
    const FastDecision d = Classify(studioHeader, textureSlot);
    if (d.kind == FastKind::None)
    {
        g_original(studioHeader, textureSlot);
        return;
    }

    GlGetIntegervFn get = ReadGetIntegerv();
    if (!get)
    {
        ++g_validateUnavailable;
        g_original(studioHeader, textureSlot);
        return;
    }

    int activeBefore = 0;
    int bindingBefore = 0;
    get(GL_ACTIVE_TEXTURE, &activeBefore);
    get(GL_TEXTURE_BINDING_2D, &bindingBefore);

    g_original(studioHeader, textureSlot);

    int activeStock = 0;
    int bindingStock = 0;
    get(GL_ACTIVE_TEXTURE, &activeStock);
    get(GL_TEXTURE_BINDING_2D, &bindingStock);

    bool equal = false;
    if (d.kind == FastKind::ForceChrome)
    {
        equal = activeStock == activeBefore && bindingStock == bindingBefore;
    }
    else
    {
        // Re-issuing the predicted direct bind after Gold must be idempotent.
        // The second GL_Bind leaves Gold's final state authoritative if equal.
        g_glBind(0, d.textureId);
        int activeFast = 0;
        int bindingFast = 0;
        get(GL_ACTIVE_TEXTURE, &activeFast);
        get(GL_TEXTURE_BINDING_2D, &bindingFast);
        equal = activeFast == activeStock && bindingFast == bindingStock;
    }

    ++g_validate;
    if (!equal)
        ++g_validateMismatch;
    LogValidationStats();
}

__declspec(naked) void ResolverHook()
{
    __asm
    {
        mov eax, dword ptr [g_mode]
        test eax, eax
        jz stock
        // cvar_t::value is +0x0C in this exact engine build.  Only mode 1 is
        // active, every other value is a zero-overhead tail fallback to Gold.
        cmp dword ptr [eax + 0Ch], 03F800000h
        je active
        cmp dword ptr [eax + 0Ch], 040000000h
        je validate
        jmp stock

    active:
        // Initial stack: return, studioHeader, textureSlot.
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        call FastDispatch
        add esp, 8
        test al, al
        jnz handled
    stock:
        jmp dword ptr [g_original]
    validate:
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        call ValidationDispatch
        add esp, 8
        ret
    handled:
        ret
    }
}

bool PatchCall(std::uint8_t* callsite)
{
    if (!callsite || callsite[0] != 0xE8)
        return false;
    const std::int32_t rel = *reinterpret_cast<const std::int32_t*>(callsite + 1);
    if (callsite + 5 + rel != reinterpret_cast<std::uint8_t*>(g_original))
        return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect(callsite, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    *reinterpret_cast<std::int32_t*>(callsite + 1) =
        static_cast<std::int32_t>(reinterpret_cast<std::uint8_t*>(&ResolverHook) - (callsite + 5));
    VirtualProtect(callsite, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), callsite, 5);
    return true;
}
} // namespace

bool Install(HMODULE hw, cl_enginefunc_t* engine)
{
    if (!hwbuild::MatchesTarget(hw) || !engine || !engine->pfnRegisterVariable)
    {
        rendererlog::Line("fasttexture: exact hw/engine unavailable, disabled");
        return false;
    }
    g_hwBase = reinterpret_cast<std::uint8_t*>(hw);
    g_original = reinterpret_cast<ResolverFn>(g_hwBase + kResolverRva);
    g_glBind = reinterpret_cast<GlBindFn>(g_hwBase + kGlBindRva);
    __try { g_mode = engine->pfnRegisterVariable("r_studio_texture", "1", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_mode = nullptr; }
    if (!g_mode)
        return false;

    static const std::uint8_t expected[] = {0xE8,0x0F,0xDB,0xFF,0xFF};
    __try
    {
        if (std::memcmp(g_hwBase + kResolverCallRva, expected, sizeof(expected)) != 0)
        {
            rendererlog::Line("fasttexture: resolver callsite signature mismatch, disabled");
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    if (!PatchCall(g_hwBase + kResolverCallRva))
    {
        rendererlog::Line("fasttexture: resolver callsite patch failed, disabled");
        return false;
    }
    rendererlog::Line("fasttexture: exact resolver fast paths installed (mode 0 stock, 1 fast, 2 validate)");
    return true;
}
} // namespace studio_fasttexture


