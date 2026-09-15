#include "vis_cache.h"
#include "hw_build.h"
#include "inline_hook.h"
#include "perf_control.h"
#include "profile.h"
#include "log.h"
#include <cstdint>
#include <cstring>
#include <intrin.h>

namespace viscache
{
namespace
{
constexpr std::uintptr_t kPointInLeafRva = 0x00069D80u;
constexpr std::uintptr_t kMarkLeavesRva  = 0x00076200u;
constexpr std::uintptr_t kViewLeafRva    = 0x0078F94Cu;
constexpr std::uintptr_t kOldViewLeafRva = 0x0078FA64u;
constexpr std::uintptr_t kVisFrameRva    = 0x0078F9A8u;
constexpr std::uintptr_t kNoVisRva       = 0x033394B8u;
constexpr std::uintptr_t kMirrorRva      = 0x033392D8u;
constexpr std::uintptr_t kMarkGuardRva   = 0x0079062Cu;
constexpr std::uintptr_t kWorldModelRva  = 0x02F90E20u;
constexpr std::uintptr_t kLeafPvsRva     = 0x000B7CB0u;
constexpr std::uintptr_t kCallRvas[] = {
    0x000279B4u, 0x0003FB70u, 0x00070B1Bu, 0x000B8189u,
    0x000EC1E3u, 0x000F4A0Eu, 0x000F9337u, 0x000FE58Eu
};

using PointInLeafFn = void* (__fastcall*)(const float* point, void* model);
using MarkLeavesFn = void (__cdecl*)();
using LeafPvsFn = unsigned char* (__fastcall*)(void* leaf, void* model);
PointInLeafFn g_original = nullptr;
MarkLeavesFn g_markLeaves = nullptr;
LeafPvsFn g_leafPvs = nullptr;
std::uint8_t* g_hwBase = nullptr;
DWORD g_renderThread = 0;
std::uint32_t g_generation = 1;

struct CacheEntry
{
    std::uint32_t generation;
    std::uintptr_t model;
    std::uint32_t point[3];
    void* leaf;
};

constexpr std::size_t kCacheSize = 256;
CacheEntry g_cache[kCacheSize]{};
ProfileStats g_profileStats{};
bool g_collectProfileStats = false;

std::uint32_t FloatBits(float value)
{
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

std::size_t Hash(std::uintptr_t model, const std::uint32_t point[3])
{
    std::uint32_t h = static_cast<std::uint32_t>(model) * 0x9E3779B1u;
    h ^= point[0] * 0x85EBCA6Bu;
    h ^= point[1] * 0xC2B2AE35u;
    h ^= point[2] * 0x27D4EB2Fu;
    h ^= h >> 16;
    return h & (kCacheSize - 1);
}

void* __fastcall PointInLeaf_Hook(const float* point, void* model)
{
    const bool profiling = g_collectProfileStats;
    if (profiling) ++g_profileStats.pointCalls;
    if (!g_original || !rendererperf::Enabled() || !point || !model ||
        GetCurrentThreadId() != g_renderThread)
        return g_original ? g_original(point, model) : nullptr;

    std::uint32_t bits[3] = {
        FloatBits(point[0]), FloatBits(point[1]), FloatBits(point[2])
    };
    CacheEntry& entry = g_cache[Hash(reinterpret_cast<std::uintptr_t>(model), bits)];
    if (entry.generation == g_generation &&
        entry.model == reinterpret_cast<std::uintptr_t>(model) &&
        entry.point[0] == bits[0] && entry.point[1] == bits[1] &&
        entry.point[2] == bits[2])
    {
        if (profiling) ++g_profileStats.pointHits;
        return entry.leaf;
    }

    void* leaf = g_original(point, model);
    entry.generation = g_generation;
    entry.model = reinterpret_cast<std::uintptr_t>(model);
    entry.point[0] = bits[0];
    entry.point[1] = bits[1];
    entry.point[2] = bits[2];
    entry.leaf = leaf;
    return leaf;
}

void __cdecl MarkLeaves_Hook()
{
    const bool profiling = g_collectProfileStats;
    const long long start = profiling ? prof::Now() : 0;
    if (profiling) ++g_profileStats.markCalls;
    if (!g_markLeaves || !g_hwBase || !g_leafPvs || !rendererperf::Enabled())
    {
        if (g_markLeaves) g_markLeaves();
        if (profiling)
        {
            ++g_profileStats.markFallback;
            g_profileStats.markTicks += prof::Now() - start;
        }
        return;
    }

    auto*& viewLeaf = *reinterpret_cast<void**>(g_hwBase + kViewLeafRva);
    auto*& oldViewLeaf = *reinterpret_cast<void**>(g_hwBase + kOldViewLeafRva);
    auto& visFrame = *reinterpret_cast<int*>(g_hwBase + kVisFrameRva);
    const std::uint32_t noVis = *reinterpret_cast<std::uint32_t*>(g_hwBase + kNoVisRva);
    const int mirror = *reinterpret_cast<int*>(g_hwBase + kMirrorRva);
    const int guard = *reinterpret_cast<int*>(g_hwBase + kMarkGuardRva);

    // Match Gold's early-outs exactly. Keep r_novis on the original path because
    // Gold uses its own all-visible buffer there.
    if (oldViewLeaf == viewLeaf && noVis == 0)
        return;
    if (mirror || guard)
        return;
    if (noVis != 0)
    {
        g_markLeaves();
        if (profiling)
        {
            ++g_profileStats.markFallback;
            g_profileStats.markTicks += prof::Now() - start;
        }
        return;
    }

    void* world = *reinterpret_cast<void**>(g_hwBase + kWorldModelRva);
    if (!viewLeaf || !world)
    {
        g_markLeaves();
        if (profiling)
        {
            ++g_profileStats.markFallback;
            g_profileStats.markTicks += prof::Now() - start;
        }
        return;
    }

    auto* model = reinterpret_cast<std::uint8_t*>(world);
    const int numLeafs = *reinterpret_cast<int*>(model + 0x88);
    auto* leafs = *reinterpret_cast<std::uint8_t**>(model + 0x8C);
    if (numLeafs <= 0 || !leafs)
    {
        g_markLeaves();
        if (profiling)
        {
            ++g_profileStats.markFallback;
            g_profileStats.markTicks += prof::Now() - start;
        }
        return;
    }

    const int frame = ++visFrame;
    oldViewLeaf = viewLeaf;
    unsigned char* vis = g_leafPvs(viewLeaf, world);
    if (!vis)
    {
        if (profiling) g_profileStats.markTicks += prof::Now() - start;
        return;
    }

    if (profiling) ++g_profileStats.markFast;

    // Gold's mleaf/mnode stride is 0x3C in this build. Iterate only set PVS bits
    // instead of testing every leaf, parent stamping remains byte-for-byte in the
    // same ascending leaf order and stops at an already-stamped ancestor.
    const int bytes = (numLeafs + 7) >> 3;
    for (int byteIndex = 0; byteIndex < bytes; ++byteIndex)
    {
        unsigned bits = vis[byteIndex];
        while (bits)
        {
            unsigned long bit = 0;
            _BitScanForward(&bit, bits);
            const int leafIndex = (byteIndex << 3) + static_cast<int>(bit);
            if (leafIndex >= numLeafs)
                break;

            if (profiling) ++g_profileStats.visibleLeaves;

            std::uint8_t* node = leafs + (leafIndex + 1) * 0x3C;
            for (;;)
            {
                int& nodeVisFrame = *reinterpret_cast<int*>(node + 0x04);
                if (nodeVisFrame == frame)
                    break;
                nodeVisFrame = frame;
                node = *reinterpret_cast<std::uint8_t**>(node + 0x20);
                if (!node)
                    break;
            }
            bits &= bits - 1;
        }
    }
    if (profiling)
        g_profileStats.markTicks += prof::Now() - start;
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
} // namespace

bool Install(HMODULE hw)
{
    if (!hwbuild::MatchesTarget(hw))
    {
        rendererlog::Line("viscache: hw.dll build mismatch, PointInLeaf cache disabled");
        return false;
    }
    auto* base = reinterpret_cast<std::uint8_t*>(hw);
    g_hwBase = base;
    g_original = reinterpret_cast<PointInLeafFn>(base + kPointInLeafRva);
    g_leafPvs = reinterpret_cast<LeafPvsFn>(base + kLeafPvsRva);

    int patched = 0;
    for (std::uintptr_t rva : kCallRvas)
    {
        if (PatchCall(base + rva, reinterpret_cast<void*>(g_original),
                      reinterpret_cast<void*>(&PointInLeaf_Hook)))
            ++patched;
    }
    rendererlog::Line("viscache: PointInLeaf direct calls patched %d/%u",
               patched, static_cast<unsigned>(sizeof(kCallRvas) / sizeof(kCallRvas[0])));

    g_markLeaves = reinterpret_cast<MarkLeavesFn>(
        inl::Hook(base + kMarkLeavesRva, reinterpret_cast<void*>(&MarkLeaves_Hook)));
    if (g_markLeaves)
        rendererlog::Line("viscache: R_MarkLeaves sparse-PVS fast path installed");
    else
        rendererlog::Line("viscache: R_MarkLeaves hook unavailable, original path retained");

    return patched > 0 || g_markLeaves != nullptr;
}

void BeginFrame()
{
    g_renderThread = GetCurrentThreadId();
    if (++g_generation == 0)
    {
        std::memset(g_cache, 0, sizeof(g_cache));
        g_generation = 1;
    }
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
} // namespace viscache
