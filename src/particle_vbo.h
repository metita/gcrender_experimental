#pragma once

#include <cstdint>
#include <windows.h>

#include "sdk.h"

namespace particlevbo
{
struct ProfileStats
{
    std::uint64_t captures;
    std::uint64_t vertices;
    std::uint64_t uploads;
    std::uint64_t emptyCaptures;
    std::uint64_t fallbacks;
    std::uint64_t callbackFallbacks;
    std::uint64_t deathFallbacks;
    std::uint64_t listFallbacks;
    std::uint64_t slotFallbacks;
    std::uint64_t bufferFallbacks;
    std::uint64_t arrayFallbacks;
};

bool Install(HMODULE hw, cl_enginefunc_t* engine);
void UpdateFrame();
void SetProfileCollection(bool enabled);
ProfileStats ConsumeProfileStats();
}
