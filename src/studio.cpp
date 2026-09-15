#include "studio.h"
#include "profile.h"
#include "studio_renderer.h"
#include "log.h"
#include <cstdint>
#include <intrin.h>

namespace studio
{
namespace
{
using StudioDrawPlayerFn = int(__cdecl*)(int flags, entity_state_t* player);
using StudioDrawModelFn  = int(__cdecl*)(int flags);

StudioDrawPlayerFn g_orig        = nullptr;
StudioDrawModelFn  g_origModel   = nullptr;
void**             g_rstudio     = nullptr;

int __cdecl DrawPlayer(int flags, entity_state_t* player)
{
    if (!g_orig) return 0;
    if (flags != 3)
        studio_renderer::FlushSolidEntityCommands();
    const bool profiling = prof::Active();
    const long long t0 = profiling ? prof::Now() : 0;
    int result = g_orig(flags, player);
    if (profiling)
        prof::AddStudio(prof::Now() - t0);
    return result;
}

int __cdecl DrawModel(int flags)
{
    if (!g_origModel)
        return 0;
    const bool batchScope =
        studio_renderer::BeginDrawModelBatchScope(
            flags, _ReturnAddress());
    if (!batchScope)
        studio_renderer::FlushSolidEntityCommands();
    const int result = g_origModel(flags);
    studio_renderer::EndDrawModelBatchScope(batchScope);
    return result;
}
} // namespace

bool HooksReady()
{
    if (!g_rstudio || !g_origModel || !g_orig)
        return false;
    __try
    {
        return g_rstudio[1] == reinterpret_cast<void*>(&DrawModel) &&
               g_rstudio[2] == reinterpret_cast<void*>(&DrawPlayer);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void Install(HMODULE client, cl_enginefunc_t* engine)
{
    if (!client || !engine)
    {
        rendererlog::Line("studio: install received invalid arguments");
        return;
    }

    auto fn = reinterpret_cast<uint8_t*>(
        GetProcAddress(client, "HUD_GetStudioModelInterface"));
    if (!fn) { rendererlog::Line("studio: interface export missing"); return; }

    void** rstudio = nullptr;
    for (int i = 0; i < 0x60; ++i)
        if (fn[i] == 0xC7 && fn[i + 1] == 0x00)
        {
            rstudio = reinterpret_cast<void**>(*reinterpret_cast<uint32_t*>(fn + i + 2));
            break;
        }
    if (!rstudio) { rendererlog::Line("studio: r_studio global not located"); return; }
    if (!rstudio[1]) { rendererlog::Line("studio: original StudioDrawModel missing"); return; }
    if (!rstudio[2]) { rendererlog::Line("studio: original StudioDrawPlayer missing"); return; }
    // Always make readiness inspect the table found by this install attempt,
    // never a table pointer left over from an earlier call.
    g_rstudio = rstudio;

    void* const drawModelHook =
        reinterpret_cast<void*>(&DrawModel);
    void* const drawPlayerHook =
        reinterpret_cast<void*>(&DrawPlayer);
    if (rstudio[1] == drawModelHook &&
        rstudio[2] == drawPlayerHook)
    {
        rendererlog::Line(
            "studio: StudioDrawModel/StudioDrawPlayer already hooked ready=%d",
            HooksReady() ? 1 : 0);
        return;
    }
    if (rstudio[1] == drawModelHook ||
        rstudio[2] == drawPlayerHook)
    {
        rendererlog::Line(
            "studio: partial Studio API hook state detected, refusing install");
        return;
    }

    StudioDrawModelFn const originalModel =
        reinterpret_cast<StudioDrawModelFn>(rstudio[1]);
    StudioDrawPlayerFn const originalPlayer =
        reinterpret_cast<StudioDrawPlayerFn>(rstudio[2]);
    g_origModel = originalModel;
    g_orig = originalPlayer;
    DWORD oldProtect = 0;
    if (!VirtualProtect(&rstudio[1], sizeof(void*) * 2,
                        PAGE_READWRITE, &oldProtect))
    {
        rendererlog::Line("studio: unable to make r_studio writable");
        g_origModel = nullptr;
        g_orig = nullptr;
        return;
    }

    bool installed = false;
    __try
    {
        rstudio[1] = drawModelHook;
        rstudio[2] = drawPlayerHook;
        installed =
            rstudio[1] == drawModelHook &&
            rstudio[2] == drawPlayerHook;
        if (!installed)
        {
            rstudio[1] = reinterpret_cast<void*>(originalModel);
            rstudio[2] = reinterpret_cast<void*>(originalPlayer);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        installed = false;
        __try
        {
            rstudio[1] = reinterpret_cast<void*>(originalModel);
            rstudio[2] = reinterpret_cast<void*>(originalPlayer);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    DWORD ignoredProtect = 0;
    VirtualProtect(&rstudio[1], sizeof(void*) * 2,
                   oldProtect, &ignoredProtect);

    if (!installed)
    {
        rendererlog::Line(
            "studio: Studio API hook install was incomplete, readiness disabled");
        return;
    }

    if (!HooksReady())
    {
        rendererlog::Line(
            "studio: Studio API hooks changed after install, readiness disabled");
        g_rstudio = nullptr;
        return;
    }

    rendererlog::Line("studio: hooked StudioDrawModel/StudioDrawPlayer (rstudio=%p model=%p player=%p ready=1)",
               rstudio, g_origModel, g_orig);
}
}
