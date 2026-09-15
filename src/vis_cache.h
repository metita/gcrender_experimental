#pragma once
#include <windows.h>
#include <cstdint>

namespace viscache
{
struct ProfileStats
{
    std::uint64_t pointCalls;
    std::uint64_t pointHits;
    std::uint64_t markCalls;
    std::uint64_t markFast;
    std::uint64_t markFallback;
    std::uint64_t visibleLeaves;
    long long markTicks;
};

bool Install(HMODULE hw);
void BeginFrame();
void SetProfileCollection(bool enabled);
ProfileStats ConsumeProfileStats();
}
