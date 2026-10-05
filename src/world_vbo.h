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
    // r_world_batch. Fallback counters are surfaces left on stock
    // R_DrawSequentialPoly, frame rejects are world passes kept fully stock.
    std::uint64_t batchFrames;
    std::uint64_t batchFlushes;
    std::uint64_t batchSurfaces;
    std::uint64_t batchDraws;
    std::uint64_t batchIndices;
    std::uint64_t batchIndexOrphans;
    std::uint64_t batchLightmapUploads;
    std::uint64_t batchStockLightmapUploads;
    std::uint64_t batchValidateSurfaces;
    std::uint64_t batchFallbackUncached;
    std::uint64_t batchFallbackSpecial;
    std::uint64_t batchFallbackScroll;
    std::uint64_t batchFallbackFlags;
    std::uint64_t batchFallbackDecal;
    std::uint64_t batchFallbackDetail;
    std::uint64_t batchFallbackRandom;
    std::uint64_t batchFallbackLightmap;
    std::uint64_t batchFallbackOverflow;
    std::uint64_t batchFallbackFrame;
    std::uint64_t batchRejectState;
    std::uint64_t batchRejectWireframe;
    std::uint64_t batchRejectLightmap;
    std::uint64_t batchRejectMultitexture;
    std::uint64_t batchRejectCache;
    std::uint64_t batchRejectClientState;
    // Coverage of batched surfaces that used to fall back, and the number of
    // lightmap groups (one TMU1 bind and at most one upload each).
    std::uint64_t batchDecalSurfaces;
    std::uint64_t batchRandomSurfaces;
    std::uint64_t batchFlagSurfaces;
    std::uint64_t batchLightmapGroups;
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
