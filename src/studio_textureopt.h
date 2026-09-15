#pragma once

#include <windows.h>
#include "sdk.h"

namespace studio_textureopt
{
bool Install(HMODULE hw, cl_enginefunc_t* engine);
void UpdateFrame();
}
