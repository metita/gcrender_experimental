#include "perf_control.h"
#include "log.h"
#include <windows.h>

namespace rendererperf
{
namespace
{
cvar_t* g_cvar = nullptr;
volatile LONG g_enabled = 1;
}

void Init(cl_enginefunc_t* engine)
{
    if (g_cvar || !engine || !engine->pfnRegisterVariable)
        return;

    __try
    {
        g_cvar = engine->pfnRegisterVariable("r_fastpath", "1", 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_cvar = nullptr;
    }
    rendererlog::Line("perf: r_fastpath registration %s (default enabled)",
               g_cvar ? "ok" : "failed");
}

void UpdateFrame()
{
    LONG enabled = 1;
    if (g_cvar)
    {
        __try
        {
            enabled = g_cvar->value >= 1.0f ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            enabled = 0;
        }
    }
    g_enabled = enabled;
}

bool Enabled()
{
    return g_enabled != 0;
}
} // namespace rendererperf
