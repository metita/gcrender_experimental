// gcrender.asi — loaded by mss32.dll's ASI provider scan. It captures GoldClient's
// engine table, then installs exact-build-gated in-memory render hooks and debug
// instrumentation. GoldClient files on disk are never modified by the loader.
#include <windows.h>
#include <cstdint>
#include "gl_state.h"
#include "perf_control.h"
#include "studio.h"
#include "studio_fastbones.h"
#include "studio_fastcache.h"
#include "studio_fastblend.h"
#include "studio_fastconcat.h"
#include "studio_fastlighting.h"
#include "studio_fastchrome.h"
#include "studio_fasttexture.h"
#include "studio_textureopt.h"
#include "studio_drawbatch.h"
#include "studio_renderer.h"
#include "studio_fastskin.h"
#include "studio_fastlight.h"
#include "vis_cache.h"
#include "world_vbo.h"
#include "particle_vbo.h"
#include "beam_vbo.h"
#include "sprite_vbo.h"
#include "profile.h"
#include "log.h"

namespace
{
LONG g_started = 0;

// The engine already ran client.dll's Initialize before we loaded, so we cannot
// hook it. Instead find the client's global gEngfuncs, which Initialize fills
// with: mov edi, <&gEngfuncs>, then rep movsd. The imm32 in the live (relocated)
// code is the runtime address of the struct.
cl_enginefunc_t* FindEngineFuncs(HMODULE client)
{
    auto init = reinterpret_cast<uint8_t*>(GetProcAddress(client, "Initialize"));
    if (!init) return nullptr;
    for (int i = 0; i < 0x40; ++i)
    {
        // BF imm32 (mov edi, imm32) followed shortly by F3 A5 (rep movsd).
        if (init[i] == 0xBF)
        {
            for (int j = i + 5; j < i + 12; ++j)
                if (init[j] == 0xF3 && init[j + 1] == 0xA5)
                    return *reinterpret_cast<cl_enginefunc_t**>(init + i + 1);
        }
    }
    return nullptr;
}

bool EngineTableReady(const cl_enginefunc_t* engine)
{
    return engine && engine->pfnRegisterVariable && engine->pfnGetCvarFloat;
}

void InitializeOnMainThread(HMODULE client, cl_enginefunc_t* engine)
{
    rendererlog::Line("main-thread init: installing client hooks");
    prof::InstallRendererProfile(GetModuleHandleA("hw.dll"));
    rendererperf::Init(engine);
    glstate::Install(engine);
    viscache::Install(GetModuleHandleA("hw.dll"));
    worldvbo::Install(GetModuleHandleA("hw.dll"), engine);
    const bool drawBatchReady = studio_drawbatch::Install(client, GetModuleHandleA("hw.dll"), engine);
    spritevbo::Install(GetModuleHandleA("hw.dll"), engine);
    studio_renderer::Install(client, GetModuleHandleA("hw.dll"), engine);
    studio_fastlighting::Install(GetModuleHandleA("hw.dll"), engine, drawBatchReady);
    studio_fastchrome::Install(GetModuleHandleA("hw.dll"), engine, drawBatchReady);
    studio_textureopt::Install(GetModuleHandleA("hw.dll"), engine);
    studio_fasttexture::Install(GetModuleHandleA("hw.dll"), engine);
    studio_fastskin::Install(GetModuleHandleA("hw.dll"), engine);
    studio_fastlight::Install(GetModuleHandleA("hw.dll"), engine);
    studio_fastbones::Install(client, engine);
    studio_fastcache::Install(client, engine);
    studio_fastblend::Install(client, engine);
    studio_fastconcat::Install(client, engine);
    particlevbo::Install(GetModuleHandleA("hw.dll"), engine);
    beamvbo::Install(GetModuleHandleA("hw.dll"), engine);
    studio::Install(client, engine);
}

DWORD WINAPI InitThread(LPVOID)
{
    HMODULE client = nullptr;
    for (int i = 0; i < 20000 && !client; ++i)
    {
        client = GetModuleHandleA("client.dll");
        if (!client) Sleep(1);
    }
    if (!client) { rendererlog::Line("InitThread: client.dll never appeared"); return 0; }

    cl_enginefunc_t* engine = nullptr;
    for (int i = 0; i < 20000 && !EngineTableReady(engine); ++i)
    {
        engine = FindEngineFuncs(client);
        if (!EngineTableReady(engine)) Sleep(1);
    }
    rendererlog::Line("InitThread: client.dll at %p, gEngfuncs=%p", client, engine);
    if (!EngineTableReady(engine))
    {
        rendererlog::Line("gEngfuncs was not ready, aborting");
        return 0;
    }

    if (!prof::Install(client, engine, &InitializeOnMainThread))
    {
        rendererlog::Line("InitThread: HUD_Redraw bootstrap was not installed");
        return 0;
    }
    rendererlog::Line("InitThread: bootstrap ready, engine calls deferred to HUD_Redraw");
    return 0;
}

// Pin ourselves and start the worker exactly once, whether we were activated via
// DllMain (LoadLibrary that resolves refs) or via the Miles RIB_Main probe.
void StartOnce(const char* via)
{
    if (InterlockedExchange(&g_started, 1) != 0) return;
    HMODULE pin = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN |
                       GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       reinterpret_cast<LPCSTR>(&StartOnce), &pin);
    rendererlog::Line("StartOnce via %s (pinned=%p)", via, pin);
    CloseHandle(CreateThread(nullptr, 0, &InitThread, nullptr, 0, nullptr));
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        rendererlog::Line("DllMain PROCESS_ATTACH");
        StartOnce("DllMain");
    }
    return TRUE;
}

// Miles recognises an .asi as a provider by its RIB_Main export. Exporting it
// (undecorated, via gcrender.def) makes mss32 load us as a real provider instead of
// probing and discarding us. We register no interface (return 0) so no audio is
// ever routed here, we only use the load as our entry point.
extern "C" int __stdcall RIB_Main(void* /*list*/, unsigned int /*count*/)
{
    rendererlog::Line("RIB_Main called by Miles");
    StartOnce("RIB_Main");
    return 0;
}

extern "C" int   __stdcall ASI_startup()       { StartOnce("ASI_startup"); return 1; }
extern "C" int   __stdcall ASI_shutdown()      { return 1; }
extern "C" const char* __stdcall ASI_error()   { return nullptr; }
