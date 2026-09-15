#pragma once
#include "sdk.h"

namespace rendererperf
{
void Init(cl_enginefunc_t* engine);
void UpdateFrame();
bool Enabled();
}
