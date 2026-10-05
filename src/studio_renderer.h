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
// Deferred-run barriers in front of a StudioDrawModel the recorder does not
// own and of a StudioDrawPlayer with flags other than 3. Both skip the flush
// when the call is order-independent of the pending opaque run.
void FlushBeforeStockDrawModel(int flags, void* caller);
void FlushBeforeDrawPlayer(int flags);
}
