#pragma once
#include <windows.h>
#include "sdk.h"

namespace studio
{
// Swap the client's StudioDrawPlayer slot for a wrapper that redraws enemies
// through walls when enabled. Teammates (same team as the local player) are
// left to render normally. No code bytes are patched, only a data pointer.
void Install(HMODULE client, cl_enginefunc_t* engine);
bool HooksReady();
}
