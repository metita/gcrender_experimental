#pragma once

#include <cstdint>
#include <windows.h>

#include "sdk.h"

namespace beamvbo
{
struct ProfileStats
{
    std::uint64_t captures;
    std::uint64_t vertices;
    std::uint64_t uploads;
    std::uint64_t fallbacks;
    std::uint64_t slotFallbacks;
    std::uint64_t bufferFallbacks;
    std::uint64_t arrayFallbacks;
    std::uint64_t replays;
    std::uint64_t renderModeSkips;
    std::uint64_t tracerCaptures;
    std::uint64_t tracerVertices;
    std::uint64_t tracerUploads;
    std::uint64_t tracerFallbacks;
    std::uint64_t tracerSlotFallbacks;
    std::uint64_t tracerBufferFallbacks;
    std::uint64_t tracerArrayFallbacks;
    std::uint64_t tracerReplays;
};

bool Install(HMODULE hw, cl_enginefunc_t* engine);
void UpdateFrame();
void SetProfileCollection(bool enabled);
ProfileStats ConsumeProfileStats();
}
