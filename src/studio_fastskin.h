#pragma once

#include <windows.h>
#include "sdk.h"

namespace studio_fastskin
{
bool Install(HMODULE hw, cl_enginefunc_t* engine);
bool DeferredSkinActive();
bool MaterializeDeferredSkin();
void ClearDeferredSkin();
}
