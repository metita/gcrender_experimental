#pragma once

#include <windows.h>
#include "sdk.h"

namespace studio_renderer
{
bool Install(HMODULE client, HMODULE hw, cl_enginefunc_t* engine);
void UpdateFrame();
bool BeginDrawModelBatchScope(int flags, void* caller);
void EndDrawModelBatchScope(bool entered);
void BeginSolidEntityPass();
void EndSolidEntityPass();
void FlushDeferredStudioCommands();
void FlushSolidEntityCommands();
}
