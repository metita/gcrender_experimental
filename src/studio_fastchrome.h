#pragma once
#include <windows.h>
#include "sdk.h"

namespace studio_fastchrome
{
bool Install(HMODULE hw, cl_enginefunc_t* engine, bool drawResetReady);
void BeginDraw();
}
