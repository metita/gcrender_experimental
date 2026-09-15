#pragma once
#include "sdk.h"
#include <windows.h>
#include <cstdint>

namespace worldvbo
{
struct ProfileStats
{
    std::uint64_t chainsBatched;
    std::uint64_t surfacesBatched;
    std::uint64_t surfacesLegacy;
    std::uint64_t drawArraysCalls;
    std::uint64_t multiDrawCalls;
    std::uint64_t drawElementsCalls;
    std::uint64_t multiDrawElementsCalls;
    std::uint64_t verticesBatched;
    std::uint64_t indicesBatched;
    std::uint64_t trianglesBatched;
    std::uint64_t sequentialRuns;
    std::uint64_t sequentialDeferredSurfaces;
    std::uint64_t sequentialBarriers;
    std::uint64_t brushScopes;
    std::uint64_t brushSurfaces;
    std::uint64_t brushVertices;
    std::uint64_t brushBeginCalls;
    std::uint64_t brushBeginShapeRejects;
    std::uint64_t brushIndexRejects;
    std::uint64_t brushEligibilityRejects;
    std::uint64_t detailAttempts;
    std::uint64_t detailDraws;
    std::uint64_t detailFallbacks;
    std::uint64_t detailVertices;
    long long drawTextureChainsTicks;
    std::uint64_t drawTextureChainsCalls;
};

bool Install(HMODULE hw, cl_enginefunc_t* engine);
void UpdateFrame();
void BeginWorldScope();
void EndWorldScope();
void BeginBrushScope();
void EndBrushScope();
bool StudioImmediateReady();
void StudioImmediateBegin(unsigned mode);
void StudioImmediateEnd();
std::uint32_t ContextGeneration();
bool ContextGenerationReady();
void SetProfileCollection(bool enabled);
ProfileStats ConsumeProfileStats();
}
