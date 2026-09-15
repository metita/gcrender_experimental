#include "studio_fastlight.h"
#include "hw_build.h"
#include "inline_hook.h"
#include "log.h"

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <xmmintrin.h>

namespace studio_fastlight
{
namespace
{
// Exact Gold hw.dll RVAs.  Hook the caller loop instead of R_LightStrength
// itself: the latter uses an LTCG-internal ABI and an active C++ detour there
// proved unstable in the exact target build.
constexpr std::uintptr_t kLightLoopEntryRva = 0x0009B28Du;
constexpr std::uintptr_t kLightLoopExitRva = 0x0009B2B3u;
constexpr std::uintptr_t kLightStrengthRva = 0x00097C40u;
constexpr std::uintptr_t kLightPosRva = 0x02892B68u;
constexpr std::uintptr_t kCurrentLightStampRva = 0x0288D5B4u;
constexpr std::uintptr_t kBoneLightStampRva = 0x02891768u;
constexpr std::uintptr_t kBoneLightOriginRva = 0x02891968u;
constexpr std::uintptr_t kNumStudioLightsRva = 0x02952B98u;

constexpr int kMaxVerts = 0x4000;
constexpr int kMaxBones = 128;
constexpr int kMaxLights = 3;
constexpr std::size_t kBoneListCacheSlots = 1024;

std::uint8_t* g_hwBase = nullptr;
cvar_t* g_mode = nullptr;
void* g_loopTrampoline = nullptr;
void* g_lightStrength = nullptr;

alignas(16) float g_validationOutput[kMaxVerts * kMaxLights * 4]{};
int g_savedLightAge[kMaxBones]{};
float g_savedLightOrigin[kMaxBones * kMaxLights * 3]{};
int g_fastPostLightAge[kMaxBones]{};
float g_fastPostLightOrigin[kMaxBones * kMaxLights * 3]{};
volatile LONG g_validateBusy = 0;

struct BoneListCacheEntry
{
    const std::uint8_t* ptr = nullptr;
    int count = 0;
    std::vector<std::uint8_t> raw;
    std::vector<std::uint8_t> unique;
};

BoneListCacheEntry g_boneListCache[kBoneListCacheSlots];

std::uint64_t g_calls = 0;
std::uint64_t g_fast = 0;
std::uint64_t g_cacheMiss = 0;
std::uint64_t g_zeroLightCalls = 0;
std::uint64_t g_boneListBuild = 0;
std::uint64_t g_boneListHit = 0;
std::uint64_t g_fallback = 0;
std::uint64_t g_validate = 0;
std::uint64_t g_mismatch = 0;
std::uint64_t g_fastTicks = 0;
std::uint64_t g_stockTicks = 0;
std::uint64_t g_timed = 0;

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

void InvokeLightStrength(int bone, const float* vertex, float* dst)
{
    __asm
    {
        push dst
        mov edx, vertex
        mov ecx, bone
        call dword ptr [g_lightStrength]
        add esp, 4
    }
}

inline void StoreDelta(float* dst, const float* vertex, const float* cached)
{
    __m128 v = _mm_load_ss(vertex + 0);
    __m128 c = _mm_load_ss(cached + 0);
    v = _mm_sub_ss(v, c);
    _mm_store_ss(dst + 0, v);

    v = _mm_load_ss(vertex + 1);
    c = _mm_load_ss(cached + 1);
    v = _mm_sub_ss(v, c);
    _mm_store_ss(dst + 1, v);

    v = _mm_load_ss(vertex + 2);
    c = _mm_load_ss(cached + 2);
    v = _mm_sub_ss(v, c);
    _mm_store_ss(dst + 2, v);

    // Gold writes +0.0f to W for every active light before Lambert mutates it.
    *reinterpret_cast<std::uint32_t*>(dst + 3) = 0u;
}

bool SafeBytesEqual(const std::uint8_t* a, const std::uint8_t* b, std::size_t size)
{
    if (!a || !b)
        return false;
    __try
    {
        return std::memcmp(a, b, size) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SafeBytesCopy(std::uint8_t* dst, const std::uint8_t* src, std::size_t size)
{
    if (!dst || !src)
        return false;
    __try
    {
        std::memcpy(dst, src, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

const BoneListCacheEntry* GetBoneList(const std::uint8_t* bones, int count)
{
    if (!bones || count <= 0 || count > kMaxVerts)
        return nullptr;

    const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(bones);
    BoneListCacheEntry& entry = g_boneListCache[(key >> 4u) & (kBoneListCacheSlots - 1u)];
    if (entry.ptr == bones && entry.count == count &&
        entry.raw.size() == static_cast<std::size_t>(count) &&
        SafeBytesEqual(entry.raw.data(), bones, static_cast<std::size_t>(count)))
    {
        ++g_boneListHit;
        return &entry;
    }

    std::vector<std::uint8_t> raw;
    std::vector<std::uint8_t> unique;
    try
    {
        raw.resize(static_cast<std::size_t>(count));
        unique.reserve(128);
    }
    catch (...)
    {
        return nullptr;
    }
    if (!SafeBytesCopy(raw.data(), bones, raw.size()))
        return nullptr;

    bool seen[kMaxBones]{};
    for (const std::uint8_t bone : raw)
    {
        if (bone >= kMaxBones)
            return nullptr;
        if (!seen[bone])
        {
            seen[bone] = true;
            try
            {
                unique.push_back(bone);
            }
            catch (...)
            {
                return nullptr;
            }
        }
    }

    entry.ptr = bones;
    entry.count = count;
    entry.raw.swap(raw);
    entry.unique.swap(unique);
    ++g_boneListBuild;
    return &entry;
}

bool ReadInputs(void* frame,
                int& count,
                int& lights,
                const float*& verts,
                const std::uint8_t*& bones)
{
    if (!frame || !g_hwBase)
        return false;

    __try
    {
        count = ReadInt(frame, 0x14);
        verts = ReadPtr<const float*>(frame, 0x0C);
        bones = ReadPtr<const std::uint8_t*>(frame, 0x1C);
        lights = *reinterpret_cast<const int*>(g_hwBase + kNumStudioLightsRva);
        return count > 0 && count <= kMaxVerts && verts && bones &&
               lights >= 0 && lights <= kMaxLights;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool FastLoop(void* frame, float* output, std::uint64_t& misses)
{
    int count = 0;
    int lights = 0;
    const float* verts = nullptr;
    const std::uint8_t* bones = nullptr;
    if (!output || !ReadInputs(frame, count, lights, verts, bones))
        return false;

    __try
    {
        auto* ages = reinterpret_cast<int*>(g_hwBase + kBoneLightStampRva);
        const auto* cache = reinterpret_cast<const float*>(g_hwBase + kBoneLightOriginRva);
        const int current = *reinterpret_cast<const int*>(g_hwBase + kCurrentLightStampRva);

        if (lights == 0)
        {
            ++g_zeroLightCalls;
            // Exact zero-light R_LightStrength semantics: no lightpos or
            // lightbonepos writes occur, the first stale reference to each
            // valid bone only advances its light-age stamp to the current
            // Studio-light generation. Preserve vertex order and miss stats.
            const BoneListCacheEntry* boneList = GetBoneList(bones, count);
            if (!boneList)
                return false;
            for (const std::uint8_t bone : boneList->unique)
            {
                if (ages[bone] != current)
                {
                    ages[bone] = current;
                    ++misses;
                }
            }
            return true;
        }

        // Preserve Gold's first-use ordering: the first vertex referencing a
        // stale bone asks stock R_LightStrength to populate that bone's cache.
        // Subsequent vertices then use the exact cached values directly.
        alignas(16) float dummy[kMaxLights * 4]{};
        for (int i = 0; i < count; ++i)
        {
            const unsigned bone = bones[i];
            if (bone >= kMaxBones)
                return false;

            const float* vertex = verts + i * 3;
            if (ages[bone] != current)
            {
                InvokeLightStrength(static_cast<int>(bone), vertex, dummy);
                ++misses;
                if (ages[bone] != current)
                    return false;
            }

            const float* boneCache = cache + bone * (kMaxLights * 3);
            float* dst = output + i * (kMaxLights * 4);
            for (int light = 0; light < lights; ++light)
                StoreDelta(dst + light * 4, vertex, boneCache + light * 3);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool StockLoop(void* frame, float* output)
{
    int count = 0;
    int lights = 0;
    const float* verts = nullptr;
    const std::uint8_t* bones = nullptr;
    if (!output || !ReadInputs(frame, count, lights, verts, bones))
        return false;

    __try
    {
        for (int i = 0; i < count; ++i)
        {
            const unsigned bone = bones[i];
            if (bone >= kMaxBones)
                return false;
            InvokeLightStrength(static_cast<int>(bone),
                                verts + i * 3,
                                output + i * (kMaxLights * 4));
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
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
    rendererlog::Line("fastlight: calls=%llu fast=%llu cachemiss=%llu zero=%llu boneBuild=%llu boneHit=%llu fallback=%llu validate=%llu mismatch=%llu stock_us=%.3f fast_us=%.3f",
               static_cast<unsigned long long>(g_calls),
               static_cast<unsigned long long>(g_fast),
               static_cast<unsigned long long>(g_cacheMiss),
               static_cast<unsigned long long>(g_zeroLightCalls),
               static_cast<unsigned long long>(g_boneListBuild),
               static_cast<unsigned long long>(g_boneListHit),
               static_cast<unsigned long long>(g_fallback),
               static_cast<unsigned long long>(g_validate),
               static_cast<unsigned long long>(g_mismatch),
               stockUs, fastUs);
}

// 0 => run original caller loop, 1 => loop fully handled here.
int __cdecl Dispatch(void* frame)
{
    ++g_calls;
    int mode = 0;
    __try { if (g_mode) mode = static_cast<int>(g_mode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { mode = 0; }

    if (mode == 1)
    {
        std::uint64_t misses = 0;
        float* lightPos = reinterpret_cast<float*>(g_hwBase + kLightPosRva);
        if (!FastLoop(frame, lightPos, misses))
        {
            ++g_fallback;
            LogStats();
            return 0;
        }
        g_cacheMiss += misses;
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

        int count = 0;
        int lights = 0;
        const float* verts = nullptr;
        const std::uint8_t* bones = nullptr;
        if (!ReadInputs(frame, count, lights, verts, bones))
        {
            InterlockedExchange(&g_validateBusy, 0);
            ++g_fallback;
            LogStats();
            return 0;
        }

        auto* ages = reinterpret_cast<int*>(g_hwBase + kBoneLightStampRva);
        auto* origins = reinterpret_cast<float*>(g_hwBase + kBoneLightOriginRva);
        __try
        {
            std::memcpy(g_savedLightAge, ages, sizeof(g_savedLightAge));
            std::memcpy(g_savedLightOrigin, origins, sizeof(g_savedLightOrigin));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedExchange(&g_validateBusy, 0);
            ++g_fallback;
            LogStats();
            return 0;
        }

        std::uint64_t misses = 0;
        LARGE_INTEGER t0{}, t1{}, t2{};
        QueryPerformanceCounter(&t0);
        const bool fastOk = FastLoop(frame, g_validationOutput, misses);
        QueryPerformanceCounter(&t1);

        bool fastStateOk = fastOk;
        if (fastOk)
        {
            __try
            {
                std::memcpy(g_fastPostLightAge, ages, sizeof(g_fastPostLightAge));
                std::memcpy(g_fastPostLightOrigin, origins, sizeof(g_fastPostLightOrigin));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                fastStateOk = false;
            }
        }

        // Restore the exact pre-fast cache state, then let the stock loop own
        // the real output and final cache state.
        __try
        {
            std::memcpy(ages, g_savedLightAge, sizeof(g_savedLightAge));
            std::memcpy(origins, g_savedLightOrigin, sizeof(g_savedLightOrigin));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedExchange(&g_validateBusy, 0);
            ++g_fallback;
            return 0;
        }

        float* lightPos = reinterpret_cast<float*>(g_hwBase + kLightPosRva);
        const bool stockOk = StockLoop(frame, lightPos);
        QueryPerformanceCounter(&t2);

        if (!fastOk || !fastStateOk || !stockOk)
        {
            InterlockedExchange(&g_validateBusy, 0);
            ++g_fallback;
            LogStats();
            return stockOk ? 1 : 0;
        }

        g_fastTicks += static_cast<std::uint64_t>(t1.QuadPart - t0.QuadPart);
        g_stockTicks += static_cast<std::uint64_t>(t2.QuadPart - t1.QuadPart);
        ++g_timed;
        ++g_validate;
        g_cacheMiss += misses;

        bool equal = true;
        __try
        {
            equal = std::memcmp(ages, g_fastPostLightAge, sizeof(g_fastPostLightAge)) == 0 &&
                    std::memcmp(origins, g_fastPostLightOrigin, sizeof(g_fastPostLightOrigin)) == 0;
            const std::size_t activeBytes = static_cast<std::size_t>(lights) * 4 * sizeof(float);
            const std::size_t stride = static_cast<std::size_t>(kMaxLights) * 4;
            for (int i = 0; i < count && equal; ++i)
            {
                equal = std::memcmp(lightPos + i * stride,
                                    g_validationOutput + i * stride,
                                    activeBytes) == 0;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            equal = false;
        }

        if (!equal)
            ++g_mismatch;
        InterlockedExchange(&g_validateBusy, 0);
        LogStats();
        return 1;
    }

    return 0;
}

__declspec(naked) void LightLoopHook()
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
        jmp dword ptr [g_loopTrampoline]
    active:
        // C++ validation/fast code must not leak any FP/SSE state into the
        // surrounding LTCG StudioDrawPoints function.
        mov esi, esp
        and esp, -16
        sub esp, 512
        fxsave [esp]
        push ebp
        call Dispatch
        add esp, 4
        mov edx, eax
        fxrstor [esp]
        mov esp, esi
        test edx, edx
        jz stock

        // The stock loop's only downstream GPR side effect that matters is the
        // EBX restore at 0x9B2B3.  EDI is overwritten at 0x9B2BB and ESI is
        // reloaded before its next use.
        mov eax, dword ptr [g_hwBase]
        add eax, kLightLoopExitRva
        jmp eax
    }
}
} // namespace

bool Install(HMODULE hw, cl_enginefunc_t* engine)
{
    if (!hwbuild::MatchesTarget(hw) || !engine || !engine->pfnRegisterVariable)
    {
        rendererlog::Line("fastlight: exact hw/engine unavailable, disabled");
        return false;
    }

    g_hwBase = reinterpret_cast<std::uint8_t*>(hw);
    g_lightStrength = g_hwBase + kLightStrengthRva;

    // Register before patching executable code, failure leaves hw.dll stock.
    __try { g_mode = engine->pfnRegisterVariable("r_studio_elight", "1", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_mode = nullptr; }
    if (!g_mode)
    {
        rendererlog::Line("fastlight: cvar registration failed, hw left untouched");
        return false;
    }

    __try
    {
        const std::uint8_t expectedEntryPrefix[4] = {0x8B,0x75,0xF4,0xBB};
        const std::uint32_t expectedLightPos = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(g_hwBase + kLightPosRva));
        std::uint32_t liveLightPos = 0;
        std::memcpy(&liveLightPos, g_hwBase + kLightLoopEntryRva + 4, sizeof(liveLightPos));

        const std::uint8_t expectedExit[3] = {0x8B,0x5D,0xF8};
        const std::uint8_t expectedStrength[4] = {0x55,0x8B,0xEC,0x51};
        if (std::memcmp(g_hwBase + kLightLoopEntryRva,
                        expectedEntryPrefix, sizeof(expectedEntryPrefix)) != 0 ||
            liveLightPos != expectedLightPos ||
            std::memcmp(g_hwBase + kLightLoopExitRva,
                        expectedExit, sizeof(expectedExit)) != 0 ||
            std::memcmp(g_hwBase + kLightStrengthRva,
                        expectedStrength, sizeof(expectedStrength)) != 0)
        {
            rendererlog::Line("fastlight: exact caller/function signature mismatch, disabled");
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    g_loopTrampoline = inl::Hook(g_hwBase + kLightLoopEntryRva,
                                 reinterpret_cast<void*>(&LightLoopHook));
    if (!g_loopTrampoline)
    {
        rendererlog::Line("fastlight: caller-loop hook failed, disabled");
        return false;
    }

    rendererlog::Line("fastlight: hooked StudioDrawPoints light loop (mode 0 stock, 1 fast per-bone cache, 2 validate)");
    return true;
}
} // namespace studio_fastlight
