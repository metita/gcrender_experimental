#pragma once

#include <windows.h>
#include "sdk.h"

namespace studio_drawbatch
{
bool Install(HMODULE client, HMODULE hw, cl_enginefunc_t* engine);
void UpdateFrame();
bool GpuSkinDeferAllowed();
bool QueryModelIdentity(const void* studioHeader, const void** ownerModel,
                        int* ownerIndex, std::uint32_t* generation);
bool ValidateModelIdentity(const void* studioHeader, const void* ownerModel,
                           int ownerIndex, std::uint32_t generation);
bool ShadowWillBeSkipped();
void SetRetainedRendererActive(bool active);
bool RetainedDirectScopeAllowed();
// studio_renderer owns the barrier-guarded solid entity pass. Inside it, the
// client-array baseline proven by the first Studio draw is reused until the
// next brush/sprite/shadow barrier invalidates it.
void SetClientArrayProofScope(bool active);
void InvalidateClientArrayProof();

// Called by world_vbo's already-installed qglBegin/qglEnd wrappers so both
// optimizers can coexist without chaining the same qgl dispatch slots.
bool TryBegin(unsigned mode);
bool TryEnd();
}
