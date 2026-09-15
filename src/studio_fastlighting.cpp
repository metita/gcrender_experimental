#include "studio_fastlighting.h"
#include "hw_build.h"
#include "log.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

namespace studio_fastlighting
{
namespace
{
constexpr std::uintptr_t kLightingCallRva = 0x0009B459u;
constexpr std::uintptr_t kStudioLightingRva = 0x00096B50u;
struct FlatEntry
{
    std::uint32_t generation = 0;
    std::uint32_t result = 0;
};

std::uint8_t* g_hwBase = nullptr;
void* g_original = nullptr;
cvar_t* g_mode = nullptr;
FlatEntry g_flat[2]{};
std::uint32_t g_generation = 1;
bool g_installed = false;

std::uint64_t g_flatCalls = 0;
std::uint64_t g_stock = 0;
std::uint64_t g_hits = 0;
std::uint64_t g_misses = 0;
std::uint64_t g_validate = 0;
std::uint64_t g_mismatch = 0;

void InvokeStock(float* out, int bone, int flags, const float* normal)
{
    __asm
    {
        push normal
        push flags
        mov edx, bone
        mov ecx, out
        call dword ptr [g_original]
        add esp, 8
    }
}

void LogStats()
{
    if ((g_flatCalls & 0xFFFFFu) != 0)
        return;
    rendererlog::Line("fastlighting: flatCalls=%llu stock=%llu hit=%llu miss=%llu validate=%llu mismatch=%llu",
               static_cast<unsigned long long>(g_flatCalls),
               static_cast<unsigned long long>(g_stock),
               static_cast<unsigned long long>(g_hits),
               static_cast<unsigned long long>(g_misses),
               static_cast<unsigned long long>(g_validate),
               static_cast<unsigned long long>(g_mismatch));
}

void __cdecl Dispatch(float* out, int bone, int flags, const float* normal)
{
    ++g_flatCalls;
    int mode = 0;
    __try { if (g_mode) mode = static_cast<int>(g_mode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { mode = 0; }

    // Hook-side gating guarantees FLATSHADE. Bit 2 (FULLBRIGHT) remains part of
    // the key because Gold tests it before FLATSHADE and may return 1.0 early.
    FlatEntry* entry = &g_flat[(static_cast<unsigned>(flags) >> 2) & 1u];
    const bool hit = entry->generation == g_generation;

    if (mode == 1 && hit)
    {
        __try { std::memcpy(out, &entry->result, sizeof(entry->result)); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InvokeStock(out, bone, flags, normal);
            ++g_stock;
            ++g_misses;
            LogStats();
            return;
        }
        ++g_hits;
        LogStats();
        return;
    }

    InvokeStock(out, bone, flags, normal);
    ++g_stock;

    std::uint32_t bits = 0;
    __try { std::memcpy(&bits, out, sizeof(bits)); }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ++g_misses;
        LogStats();
        return;
    }

    if (mode == 2 && hit)
    {
        ++g_validate;
        if (entry->result != bits)
            ++g_mismatch;
        ++g_hits;
    }
    else if (hit)
    {
        ++g_hits;
    }
    else
    {
        ++g_misses;
        entry->result = bits;
        entry->generation = g_generation;
    }
    LogStats();
}

__declspec(naked) void LightingHook()
{
    __asm
    {
        // Production fast path only targets FLATSHADE. All normal-lighting
        // calls tail-jump straight to Gold and pay no C++/hash overhead.
        mov eax, dword ptr [g_mode]
        test eax, eax
        jz stock
        cmp dword ptr [eax + 0Ch], 03F800000h
        je check_flat
        cmp dword ptr [eax + 0Ch], 040000000h
        jne stock
    check_flat:
        test dword ptr [esp + 4], 1
        jz stock
        // Original LTCG ABI: ECX=out, EDX=bone, caller stack args
        // [esp+4]=flags and [esp+8]=normal. Keep those original args in place,
        // the Gold caller cleans them after we return.
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push edx
        push ecx
        call Dispatch
        add esp, 16
        ret
    stock:
        jmp dword ptr [g_original]
    }
}

bool PatchLightingCall(std::uint8_t* callsite)
{
    if (!callsite || callsite[0] != 0xE8)
        return false;
    const std::int32_t oldRel = *reinterpret_cast<const std::int32_t*>(callsite + 1);
    void* oldTarget = callsite + 5 + oldRel;
    if (oldTarget != g_original)
        return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect(callsite, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    *reinterpret_cast<std::int32_t*>(callsite + 1) =
        static_cast<std::int32_t>(reinterpret_cast<std::uint8_t*>(&LightingHook) - (callsite + 5));
    VirtualProtect(callsite, 5, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), callsite, 5);
    return true;
}
} // namespace

void BeginDraw()
{
    if (!g_installed)
        return;
    ++g_generation;
    if (g_generation == 0)
    {
        std::memset(g_flat, 0, sizeof(g_flat));
        g_generation = 1;
    }
}

bool Install(HMODULE hw, cl_enginefunc_t* engine, bool drawResetReady)
{
    if (!drawResetReady || !hwbuild::MatchesTarget(hw) ||
        !engine || !engine->pfnRegisterVariable)
    {
        rendererlog::Line("fastlighting: exact hw/draw reset unavailable, disabled");
        return false;
    }

    g_hwBase = reinterpret_cast<std::uint8_t*>(hw);
    g_original = g_hwBase + kStudioLightingRva;
    __try { g_mode = engine->pfnRegisterVariable("r_studio_lighting", "1", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_mode = nullptr; }
    if (!g_mode)
        return false;

    static const std::uint8_t expectedCall[] = {0xE8,0xF2,0xB6,0xFF,0xFF};
    __try
    {
        if (std::memcmp(g_hwBase + kLightingCallRva, expectedCall, sizeof(expectedCall)) != 0)
        {
            rendererlog::Line("fastlighting: R_StudioLighting callsite signature mismatch, disabled");
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    if (!PatchLightingCall(g_hwBase + kLightingCallRva))
    {
        rendererlog::Line("fastlighting: callsite patch failed, disabled");
        return false;
    }
    g_installed = true;
    rendererlog::Line("fastlighting: patched per-normal R_StudioLighting call (mode 0 stock, 1 cached, 2 validate)");
    return true;
}
} // namespace studio_fastlighting
