#include "studio_fastskin.h"
#include "hw_build.h"
#include "inline_hook.h"
#include "log.h"
#include "studio_drawbatch.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <xmmintrin.h>

namespace studio_fastskin
{
namespace
{
// Exact Gold hw.dll RVAs for StudioDrawPoints' unique-vertex skin loop.
constexpr std::uintptr_t kSkinEntryRva = 0x0009B188u;
constexpr std::uintptr_t kSkinExitRva = 0x0009B284u;
constexpr std::uintptr_t kBoneTransformRva = 0x029F3A58u;
constexpr std::uintptr_t kAuxVertsRva = 0x029BE248u;

constexpr int kMaxVerts = 0x4000;
constexpr int kMaxBones = 128;

std::uint8_t* g_hwBase = nullptr;
cvar_t* g_mode = nullptr;
void* g_entryTrampoline = nullptr;
void* g_stockLoopCopy = nullptr;

alignas(16) float g_scratch[kMaxVerts * 3]{};
alignas(16) __m128 g_columns[kMaxBones][4]{};
std::uint32_t g_columnStamp[kMaxBones]{};
std::uint32_t g_skinStamp = 1;

volatile LONG g_validateBusy = 0;
bool g_validationPending = false;
int g_validationVerts = 0;
LONGLONG g_stockStart = 0;

std::uint64_t g_calls = 0;
std::uint64_t g_fast = 0;
std::uint64_t g_fallback = 0;
std::uint64_t g_validate = 0;
std::uint64_t g_mismatch = 0;
std::uint64_t g_fastTicks = 0;
std::uint64_t g_stockTicks = 0;
std::uint64_t g_timed = 0;
std::uint64_t g_deferred = 0;
std::uint64_t g_materialized = 0;
std::uint64_t g_deferFallback = 0;

struct DeferredSkinState
{
    const float* verts = nullptr;
    const std::uint8_t* vertBones = nullptr;
    int count = 0;
    bool active = false;
};

DeferredSkinState g_deferredSkin{};

inline int ReadInt(const void* frame, int negativeOffset)
{
    return *reinterpret_cast<const int*>(
        static_cast<const std::uint8_t*>(frame) - negativeOffset);
}

template <typename T>
inline T ReadPtr(const void* frame, int negativeOffset)
{
    return *reinterpret_cast<T const*>(
        static_cast<const std::uint8_t*>(frame) - negativeOffset);
}

void ResetColumnStamps()
{
    std::memset(g_columnStamp, 0, sizeof(g_columnStamp));
    g_skinStamp = 1;
}

bool FastSkinRaw(int count,
                 const float* verts,
                 const std::uint8_t* vertBones,
                 float* output)
{
    if (!output || !g_hwBase || count <= 0 || count > kMaxVerts ||
        !verts || !vertBones)
        return false;

    __try
    {
        if (++g_skinStamp == 0)
            ResetColumnStamps();

        const float* palette = reinterpret_cast<const float*>(g_hwBase + kBoneTransformRva);
        unsigned lastBone = UINT32_MAX;
        __m128 c0 = _mm_setzero_ps();
        __m128 c1 = _mm_setzero_ps();
        __m128 c2 = _mm_setzero_ps();
        __m128 c3 = _mm_setzero_ps();
        const float* src = verts;
        float* dst = output;

        for (int i = 0; i < count; ++i)
        {
            const unsigned bone = vertBones[i];
            if (bone >= kMaxBones)
                return false;

            if (bone != lastBone)
            {
                if (g_columnStamp[bone] != g_skinStamp)
                {
                    const float* m = palette + bone * 12;
                    __m128 r0 = _mm_loadu_ps(m + 0);
                    __m128 r1 = _mm_loadu_ps(m + 4);
                    __m128 r2 = _mm_loadu_ps(m + 8);
                    __m128 r3 = _mm_setzero_ps();
                    _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
                    g_columns[bone][0] = r0;
                    g_columns[bone][1] = r1;
                    g_columns[bone][2] = r2;
                    g_columns[bone][3] = r3;
                    g_columnStamp[bone] = g_skinStamp;
                }
                c0 = g_columns[bone][0];
                c1 = g_columns[bone][1];
                c2 = g_columns[bone][2];
                c3 = g_columns[bone][3];
                lastBone = bone;
            }

            const __m128 vx = _mm_set1_ps(src[0]);
            const __m128 vy = _mm_set1_ps(src[1]);
            const __m128 vz = _mm_set1_ps(src[2]);

            // Keep Gold's scalar operation order per component:
            // (y*m01 + x*m00) + z*m02 + m03, and likewise for rows 1/2.
            __m128 value = _mm_add_ps(
                _mm_mul_ps(c0, vx),
                _mm_mul_ps(c1, vy));
            value = _mm_add_ps(value, _mm_mul_ps(c2, vz));
            value = _mm_add_ps(value, c3);

            _mm_store_ss(dst + 0, value);
            __m128 y = _mm_shuffle_ps(value, value, _MM_SHUFFLE(1, 1, 1, 1));
            __m128 z = _mm_shuffle_ps(value, value, _MM_SHUFFLE(2, 2, 2, 2));
            _mm_store_ss(dst + 1, y);
            _mm_store_ss(dst + 2, z);
            src += 3;
            dst += 3;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool FastSkin(void* frame, float* output)
{
    if (!frame)
        return false;
    __try
    {
        const int count = ReadInt(frame, 0x14);
        const auto* verts = ReadPtr<const float*>(frame, 0x0C);
        const auto* vertBones = ReadPtr<const std::uint8_t*>(frame, 0x1C);
        return FastSkinRaw(count, verts, vertBones, output);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool CaptureDeferredSkin(void* frame)
{
    if (!frame)
        return false;
    __try
    {
        const int count = ReadInt(frame, 0x14);
        const auto* verts = ReadPtr<const float*>(frame, 0x0C);
        const auto* vertBones = ReadPtr<const std::uint8_t*>(frame, 0x1C);
        if (count <= 0 || count > kMaxVerts || !verts || !vertBones)
            return false;
        bool seenBones[kMaxBones]{};
        const float* palette =
            reinterpret_cast<const float*>(g_hwBase + kBoneTransformRva);
        for (int i = 0; i < count; ++i)
        {
            // Validate every model-space component that a later FastSkinRaw
            // materialization would consume.  The deferred lifetime never
            // crosses this StudioDrawPoints call, so a successful full preflight
            // removes the partial-page/fault hole from fallback materialization.
            const float* src = verts + static_cast<std::size_t>(i) * 3u;
            volatile float vx = src[0];
            volatile float vy = src[1];
            volatile float vz = src[2];
            (void)vx;
            (void)vy;
            (void)vz;

            const unsigned bone = vertBones[i];
            if (bone >= kMaxBones)
                return false;
            if (!seenBones[bone])
            {
                const float* m = palette + static_cast<std::size_t>(bone) * 12u;
                for (int component = 0; component < 12; ++component)
                {
                    volatile float value = m[component];
                    (void)value;
                }
                seenBones[bone] = true;
            }
        }
        g_deferredSkin.verts = verts;
        g_deferredSkin.vertBones = vertBones;
        g_deferredSkin.count = count;
        g_deferredSkin.active = true;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_deferredSkin = {};
        return false;
    }
}

void LogStats()
{
    if ((g_calls & 0xFFFu) != 0)
        return;
    LARGE_INTEGER fq{};
    QueryPerformanceFrequency(&fq);
    const double stockUs = (g_timed && fq.QuadPart)
        ? 1.0e6 * static_cast<double>(g_stockTicks) /
          static_cast<double>(fq.QuadPart) / static_cast<double>(g_timed) : 0.0;
    const double fastUs = (g_timed && fq.QuadPart)
        ? 1.0e6 * static_cast<double>(g_fastTicks) /
          static_cast<double>(fq.QuadPart) / static_cast<double>(g_timed) : 0.0;
    rendererlog::Line("fastskin: calls=%llu fast=%llu fallback=%llu validate=%llu mismatch=%llu deferred=%llu materialized=%llu deferFallback=%llu stock_us=%.3f fast_us=%.3f",
               static_cast<unsigned long long>(g_calls),
               static_cast<unsigned long long>(g_fast),
               static_cast<unsigned long long>(g_fallback),
               static_cast<unsigned long long>(g_validate),
               static_cast<unsigned long long>(g_mismatch),
               static_cast<unsigned long long>(g_deferred),
               static_cast<unsigned long long>(g_materialized),
               static_cast<unsigned long long>(g_deferFallback),
               stockUs, fastUs);
}

// Returns 1 to skip the stock loop, 2 to run stock and validate at kSkinExit,
// 0 for an exact stock fallback.
int __cdecl EntryDispatch(void* frame)
{
    ++g_calls;
    int mode = 0;
    __try { if (g_mode) mode = static_cast<int>(g_mode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { mode = 0; }

    const int count = ReadInt(frame, 0x14);
    if (count <= 0 || count > kMaxVerts)
    {
        ++g_fallback;
        LogStats();
        return 0;
    }

    if (mode == 1)
    {
        if (studio_drawbatch::GpuSkinDeferAllowed() &&
            CaptureDeferredSkin(frame))
        {
            ++g_deferred;
            LogStats();
            return 1;
        }

        float* aux = reinterpret_cast<float*>(g_hwBase + kAuxVertsRva);
        if (!FastSkin(frame, aux))
        {
            ++g_fallback;
            LogStats();
            return 0;
        }
        ++g_fast;
        LogStats();
        return 1;
    }

    if (mode == 2)
    {
        if (InterlockedCompareExchange(&g_validateBusy, 1, 0) != 0)
        {
            ++g_fallback;
            LogStats();
            return 0;
        }

        LARGE_INTEGER t0{}, t1{};
        QueryPerformanceCounter(&t0);
        const bool ok = FastSkin(frame, g_scratch);
        QueryPerformanceCounter(&t1);
        if (!ok)
        {
            InterlockedExchange(&g_validateBusy, 0);
            ++g_fallback;
            LogStats();
            return 0;
        }

        g_fastTicks += static_cast<std::uint64_t>(t1.QuadPart - t0.QuadPart);
        g_stockStart = t1.QuadPart;
        g_validationVerts = count;
        g_validationPending = true;
        return 2;
    }

    return 0;
}

void __cdecl CompleteValidation(void* frame)
{
    if (!g_validationPending)
        return;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (now.QuadPart >= g_stockStart)
    {
        g_stockTicks += static_cast<std::uint64_t>(now.QuadPart - g_stockStart);
        ++g_timed;
    }

    bool equal = false;
    __try
    {
        const int count = ReadInt(frame, 0x14);
        const float* aux = reinterpret_cast<const float*>(g_hwBase + kAuxVertsRva);
        equal = count == g_validationVerts && count > 0 && count <= kMaxVerts &&
                std::memcmp(aux, g_scratch, static_cast<std::size_t>(count) * 3 * sizeof(float)) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        equal = false;
    }

    ++g_validate;
    if (!equal)
        ++g_mismatch;
    g_validationPending = false;
    g_validationVerts = 0;
    InterlockedExchange(&g_validateBusy, 0);
    LogStats();
}

__declspec(naked) void SkinEntryHook()
{
    __asm
    {
        mov eax, dword ptr [g_mode]
        test eax, eax
        jz stock
        cmp dword ptr [eax + 0Ch], 03F800000h
        je active
        cmp dword ptr [eax + 0Ch], 040000000h
        je active
    stock:
        jmp dword ptr [g_entryTrampoline]
    active:
        // Preserve the complete FP/SSE environment because the stock loop only
        // owns xmm0/xmm1, later Studio code may still carry values in xmm2+.
        mov esi, esp
        and esp, -16
        sub esp, 512
        fxsave [esp]
        push ebp
        call EntryDispatch
        add esp, 4
        mov edx, eax
        fxrstor [esp]
        mov esp, esi
        cmp edx, 1
        je fastDone
        cmp edx, 2
        je validateStock
        jmp dword ptr [g_entryTrampoline]
    validateStock:
        // Execute an exact relocated copy of Gold's original skin loop while
        // EBP/ESP still belong to StudioDrawPoints.  This avoids a second
        // detour at the shared post-loop address, which was the unstable part
        // of the old mode-2 validator.
        call dword ptr [g_stockLoopCopy]

        // Compare/tally without leaking helper FP/SSE state into Gold.
        mov esi, esp
        and esp, -16
        sub esp, 512
        fxsave [esp]
        push ebp
        call CompleteValidation
        add esp, 4
        fxrstor [esp]
        mov esp, esi

        mov ecx, dword ptr [ebp - 14h]
        mov eax, dword ptr [g_hwBase]
        add eax, kSkinExitRva
        jmp eax
    fastDone:
        // The next stock block only needs ECX=numverts. It immediately reloads
        // EBX/EDI/ESI/EDX before consuming them.
        mov ecx, dword ptr [ebp - 14h]
        mov eax, dword ptr [g_hwBase]
        add eax, kSkinExitRva
        jmp eax
    }
}
} // namespace

bool DeferredSkinActive()
{
    return g_deferredSkin.active;
}

bool MaterializeDeferredSkin()
{
    if (!g_deferredSkin.active)
        return true;
    float* aux = reinterpret_cast<float*>(g_hwBase + kAuxVertsRva);
    if (!FastSkinRaw(g_deferredSkin.count,
                     g_deferredSkin.verts,
                     g_deferredSkin.vertBones,
                     aux))
    {
        ++g_deferFallback;
        return false;
    }
    g_deferredSkin = {};
    ++g_materialized;
    return true;
}

void ClearDeferredSkin()
{
    g_deferredSkin = {};
}

bool Install(HMODULE hw, cl_enginefunc_t* engine)
{
    if (!hwbuild::MatchesTarget(hw) || !engine || !engine->pfnRegisterVariable)
    {
        rendererlog::Line("fastskin: exact hw/engine unavailable, disabled");
        return false;
    }

    g_hwBase = reinterpret_cast<std::uint8_t*>(hw);

    // Register control state before touching executable code.  If Gold's cvar
    // registry is not available/healthy, fail closed and leave hw.dll stock.
    __try { g_mode = engine->pfnRegisterVariable("r_studio_skin", "1", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_mode = nullptr; }
    if (!g_mode)
    {
        rendererlog::Line("fastskin: cvar registration failed, hw left untouched");
        return false;
    }
    rendererlog::Line("fastskin: cvar registered, validating exact loop before patch");

    const std::uint8_t expectedExit[5] = {0x8B,0x5D,0xF8,0x33,0xFF};
    __try
    {
        const auto* entry = g_hwBase + kSkinEntryRva;
        const std::uint8_t expectedPrefix[4] = {0x8B,0x45,0xF4,0xBE};
        const std::uint32_t relocatedAux = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(g_hwBase + kAuxVertsRva));
        std::uint32_t liveAux = 0;
        std::memcpy(&liveAux, entry + 4, sizeof(liveAux));
        if (std::memcmp(entry, expectedPrefix, sizeof(expectedPrefix)) != 0 ||
            liveAux != relocatedAux ||
            std::memcmp(g_hwBase + kSkinExitRva, expectedExit, sizeof(expectedExit)) != 0)
        {
            rendererlog::Line("fastskin: exact loop byte signature mismatch, disabled");
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    // The loop is self-contained: its only branch is the backward JL to an
    // address inside this same range, and all absolute Gold addresses are
    // already loader-relocated in the live image.  Therefore a byte-for-byte
    // copy plus RET is a safe callable stock oracle for mode 2.
    constexpr std::size_t kStockLoopSize = kSkinExitRva - kSkinEntryRva;
    auto* stockCopy = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, kStockLoopSize + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!stockCopy)
    {
        rendererlog::Line("fastskin: unable to allocate stock validation loop, hw left untouched");
        return false;
    }
    std::memcpy(stockCopy, g_hwBase + kSkinEntryRva, kStockLoopSize);
    stockCopy[kStockLoopSize] = 0xC3; // ret
    DWORD oldProtect = 0;
    if (!VirtualProtect(stockCopy, kStockLoopSize + 1, PAGE_EXECUTE_READ, &oldProtect))
    {
        VirtualFree(stockCopy, 0, MEM_RELEASE);
        rendererlog::Line("fastskin: unable to protect stock validation loop, hw left untouched");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), stockCopy, kStockLoopSize + 1);
    g_stockLoopCopy = stockCopy;

    g_entryTrampoline = inl::Hook(g_hwBase + kSkinEntryRva,
                                  reinterpret_cast<void*>(&SkinEntryHook));
    if (!g_entryTrampoline)
    {
        VirtualFree(g_stockLoopCopy, 0, MEM_RELEASE);
        g_stockLoopCopy = nullptr;
        rendererlog::Line("fastskin: entry hook failed");
        return false;
    }

    rendererlog::Line("fastskin: hooked StudioDrawPoints vertex skin loop (mode 0 stock, 1 fast SIMD, 2 validate, single-entry validator)");
    return true;
}
} // namespace studio_fastskin

