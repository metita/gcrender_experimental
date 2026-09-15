#pragma once
#include <windows.h>
#include "sdk.h"

namespace prof
{
// Installs lightweight HUD_Redraw/HUD_Frame bootstrap hooks from the loader
// thread. The first callback performs cvar registration and the remaining
// hook installation on the game's main/render thread. HUD_Frame also runs
// while the client is sitting at the menu, so the ASI becomes ready before a map.
using MainThreadInitFn = void (*)(HMODULE client, cl_enginefunc_t* engine);
bool Install(HMODULE client, cl_enginefunc_t* engine,
             MainThreadInitFn on_first_redraw);
bool InstallRendererProfile(HMODULE hw);
void EnsureCvar();
// Studio time contributed by the StudioDrawPlayer wrapper (QPC ticks).
void AddStudio(long long ticks);
long long Now();
bool Active();
}
