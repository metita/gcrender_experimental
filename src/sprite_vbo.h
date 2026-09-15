#pragma once

#include <cstdint>
#include <windows.h>

#include "sdk.h"

namespace spritevbo
{
struct ProfileStats
{
    std::uint64_t attempts = 0;
    std::uint64_t gateRejects = 0;
    std::uint64_t gateDisabled = 0;
    std::uint64_t gateSlots = 0;
    std::uint64_t slotMismatchMask = 0;
    std::uint64_t gateMatrix = 0;
    std::uint64_t gateBlend = 0;
    std::uint64_t gateEntity = 0;
    std::uint64_t gateModel = 0;
    std::uint64_t captures = 0;
    std::uint64_t sprites = 0;
    std::uint64_t vertices = 0;
    std::uint64_t uploads = 0;
    std::uint64_t drawCalls = 0;
    std::uint64_t fallbacks = 0;
    std::uint64_t stateFallbacks = 0;
    std::uint64_t slotFallbacks = 0;
    std::uint64_t replays = 0;
};

bool Install(HMODULE hw, cl_enginefunc_t* engine);
void UpdateFrame();

void BeginSolidPass();
void EndSolidPass();

// Arms capture for the exact solid R_DrawSpriteModel callsite.  Gold still
// executes the complete function, only its immediate-mode quad emission is
// replaced by a deferred stream when all conservative gates pass.
bool BeginCurrentSolidSprite();
void EndCurrentSolidSprite();
void Flush();

void SetProfileCollection(bool enabled);
ProfileStats ConsumeProfileStats();
} // namespace spritevbo
