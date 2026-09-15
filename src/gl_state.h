#pragma once
#include "sdk.h"
#include <cstdint>

// Conservative OpenGL state filter used only inside renderer scopes that are
// known to be fully owned by hw.dll. Outside those scopes every call is passed
// through unchanged.
namespace glstate
{
struct ProfileStats
{
    std::uint64_t drawWorldCalls;
    long long drawWorldTicks;
    std::uint64_t enableDisableSkipped;
    std::uint64_t alphaSkipped;
    std::uint64_t blendSkipped;
    std::uint64_t depthSkipped;
    std::uint64_t depthMaskSkipped;
    std::uint64_t texEnvSkipped;
};

bool Install(cl_enginefunc_t* engine);
void UpdateFrame();
void SetProfileCollection(bool enabled);
ProfileStats ConsumeProfileStats();
}
