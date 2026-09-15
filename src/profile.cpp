#include "profile.h"
#include "inline_hook.h"
#include "gl_state.h"
#include "perf_control.h"
#include "log.h"
#include "vis_cache.h"
#include "world_vbo.h"
#include "particle_vbo.h"
#include "beam_vbo.h"
#include "sprite_vbo.h"
#include "studio_drawbatch.h"
#include "studio_renderer.h"
#include "studio_textureopt.h"
#include "hw_build.h"
#include <cstdint>
#include <cstring>

namespace prof
{
namespace
{
using RedrawFn = int(__cdecl*)(float, int);
using FrameFn  = void(__cdecl*)(double);
using TrisFn   = void(__cdecl*)();
using RenderViewFn = void(__cdecl*)();
using RenderSceneFn = void(__cdecl*)();
using DrawEntitiesFn = void(__cdecl*)();
using DrawTEntitiesFn = void(__fastcall*)(int pass);
using DrawViewModelFn = void(__cdecl*)();
using RenderDlightsFn = void(__cdecl*)();
using DrawParticlesFn = void(__cdecl*)();

RedrawFn         g_origRedraw = nullptr;
FrameFn          g_origFrame  = nullptr;
TrisFn           g_origTris   = nullptr;
RenderViewFn      g_origRenderView = nullptr;
RenderSceneFn     g_origRenderScene = nullptr;
DrawEntitiesFn    g_origDrawEntities = nullptr;
DrawTEntitiesFn   g_origDrawTEntities = nullptr;
DrawViewModelFn   g_origDrawViewModel = nullptr;
RenderDlightsFn   g_origRenderDlights = nullptr;
DrawParticlesFn   g_origDrawParticles = nullptr;
cl_enginefunc_t* g_engine     = nullptr;
HMODULE          g_client     = nullptr;
MainThreadInitFn g_onFirstRedraw = nullptr;
volatile LONG    g_initState  = 0; // 0 = pending, 1 = running, 2 = complete
cvar_t*          g_cvar       = nullptr;
volatile LONG    g_active     = 0;

LARGE_INTEGER g_freq{};
long long     g_lastFrame  = 0;
long long     g_frameAcc   = 0;   // wall time between HUD_Redraw calls
long long     g_redrawAcc  = 0;   // time inside HUD_Redraw
long long     g_trisAcc    = 0;   // time inside HUD_DrawNormalTriangles
long long     g_studioAcc  = 0;   // time inside StudioDrawPlayer (all players)
long long     g_renderViewAcc = 0; // complete hw.dll renderer time
std::uint64_t g_renderViewCalls = 0;
long long     g_renderSceneAcc = 0;
long long     g_entitiesAcc = 0;
long long     g_tentitiesAcc = 0;
long long     g_viewModelAcc = 0;
long long     g_dlightsAcc = 0;
long long     g_particlesAcc = 0;
int           g_frames     = 0;
constexpr int kReport = 600;

bool  g_benchMode = false;
int   g_benchStage = -1; // 0=perf off, 1=perf on, 2=done
bool  g_benchConfigured = false;
float g_benchOldFpsMax = 144.0f;
double g_benchBaselineFrameMs = 0.0;
double g_benchBaselineWorldMs = 0.0;

double Ms(long long ticks)
{
    return g_freq.QuadPart ? (double)ticks * 1000.0 / (double)g_freq.QuadPart : 0.0;
}

void SetCvar(const char* name, float value)
{
    if (!g_engine || !g_engine->Cvar_SetValue)
        return;
    __try
    {
        g_engine->Cvar_SetValue(const_cast<char*>(name), value);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        rendererlog::Line("bench: failed setting %s", name);
    }
}
} // namespace

long long Now() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
bool Active()
{
    return g_active != 0;
}
void AddStudio(long long ticks) { if (Active()) g_studioAcc += ticks; }

void RefreshActive()
{
    LONG active = 0;
    if (g_cvar)
    {
        __try
        {
            active = g_cvar->value >= 1.0f ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            active = 0;
        }
    }
    g_active = active;
}

void EnsureCvar()
{
    if (g_cvar || !g_engine || !g_engine->pfnRegisterVariable)
        return;

    cvar_t* cvar = nullptr;
    __try
    {
        cvar = g_engine->pfnRegisterVariable("r_profile", "0", 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        rendererlog::Line("prof: cvar registration raised an exception");
        return;
    }
    g_cvar = cvar;
    RefreshActive();
    rendererlog::Line("prof: r_profile registration %s", cvar ? "ok" : "failed");

    if (g_benchMode && g_cvar && !g_benchConfigured && g_engine->Cvar_SetValue)
    {
        __try
        {
            g_benchOldFpsMax = g_engine->pfnGetCvarFloat("fps_max");
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_benchOldFpsMax = 144.0f;
        }
        SetCvar("fps_max", 1000.0f);
        SetCvar("r_profile", 1.0f);
        SetCvar("r_fastpath", 0.0f);
        RefreshActive();
        rendererperf::UpdateFrame();
        g_benchStage = 0;
        g_benchConfigured = true;
        rendererlog::Line("BENCH start: fps_max %.1f -> 1000, first %d frames perf=OFF",
                   g_benchOldFpsMax, kReport);
    }
}

namespace
{
void __cdecl Tris_Hook();
void __cdecl Frame_Hook(double time);
void __cdecl RenderView_Hook();
void __cdecl RenderScene_Hook();
void __cdecl DrawEntities_Hook();
void __fastcall DrawTEntities_Hook(int pass);
void __cdecl DrawViewModel_Hook();
void __cdecl RenderDlights_Hook();
void __cdecl DrawParticles_Hook();

void EnsureTrisHook()
{
    if (g_origTris || !g_client)
        return;

    void* tris = reinterpret_cast<void*>(
        GetProcAddress(g_client, "HUD_DrawNormalTriangles"));
    if (!tris)
    {
        rendererlog::Line("prof: HUD_DrawNormalTriangles export missing");
        return;
    }

    g_origTris = reinterpret_cast<TrisFn>(
        inl::Hook(tris, reinterpret_cast<void*>(&Tris_Hook)));
    if (!g_origTris)
        rendererlog::Line("prof: HUD_DrawNormalTriangles hook failed");
    else
        rendererlog::Line("prof: HUD_DrawNormalTriangles profiling hook installed on demand");
}

void InitializeOnMainThread()
{
    if (InterlockedCompareExchange(&g_initState, 1, 0) != 0)
        return;

    if (g_onFirstRedraw)
        g_onFirstRedraw(g_client, g_engine);

    InterlockedExchange(&g_initState, 2);
    rendererlog::Line("prof: main-thread initialization complete");
}

int __cdecl Redraw_Hook(float time, int intermission)
{
    InitializeOnMainThread();
    if (!g_origRedraw)
        return 0;

    const bool profiling = Active();
    long long a = 0;
    long long s = 0;
    if (profiling)
    {
        a = Now();
        if (g_lastFrame) g_frameAcc += a - g_lastFrame;
        g_lastFrame = a;
        s = Now();
    }
    int result = g_origRedraw(time, intermission);
    long long e = profiling ? Now() : 0;

    // All engine queries and cvar registration happen after the stock HUD
    // callback has returned. StudioDrawPlayer then consumes only the cache.
    EnsureCvar();
    RefreshActive();
    // Enter deferred logging before the first measured frame. Keep it active
    // through the final profiled frame so filesystem latency cannot perturb
    // either our timings or external frame counters such as cl_showfps.
    if (!profiling && Active())
        rendererlog::SetDeferred(true);
    if (Active())
        EnsureTrisHook();
    const bool collectDetailed = Active() && !g_benchMode;
    glstate::SetProfileCollection(collectDetailed);
    viscache::SetProfileCollection(collectDetailed);
    // VBO counters are only per-chain/per-surface (far cheaper than per-GL-call
    // counters), so keep them during the automatic benchmark as well.
    worldvbo::SetProfileCollection(Active());
    particlevbo::SetProfileCollection(Active());
    beamvbo::SetProfileCollection(Active());
    spritevbo::SetProfileCollection(Active());
    rendererperf::UpdateFrame();
    viscache::BeginFrame();
    glstate::UpdateFrame();
    worldvbo::UpdateFrame();
    particlevbo::UpdateFrame();
    beamvbo::UpdateFrame();
    spritevbo::UpdateFrame();
    studio_textureopt::UpdateFrame();
    studio_drawbatch::UpdateFrame();
    studio_renderer::UpdateFrame();

    if (profiling)
    {
        g_redrawAcc += e - s;
        if (++g_frames >= kReport)
        {
            const glstate::ProfileStats gl = glstate::ConsumeProfileStats();
            const viscache::ProfileStats vis = viscache::ConsumeProfileStats();
            const worldvbo::ProfileStats vbo = worldvbo::ConsumeProfileStats();
            const particlevbo::ProfileStats particles =
                particlevbo::ConsumeProfileStats();
            const beamvbo::ProfileStats beams =
                beamvbo::ConsumeProfileStats();
            const spritevbo::ProfileStats sprites =
                spritevbo::ConsumeProfileStats();
            const double frameMs = Ms(g_frameAcc) / g_frames;
            const double worldMs = gl.drawWorldCalls ? Ms(gl.drawWorldTicks) / gl.drawWorldCalls : 0.0;
            const char* mode = rendererperf::Enabled() ? "ON" : "OFF";
            rendererlog::Line("PROFILE[%s] over %d frames: frame=%.3f ms (%.0f fps)  "
                       "renderer=%.3f  redraw=%.3f  normal_tris=%.3f  studio=%.3f  world=%.3f",
                       mode, g_frames, frameMs,
                       1000.0 / frameMs,
                       Ms(g_renderViewAcc) / g_frames,
                       Ms(g_redrawAcc) / g_frames, Ms(g_trisAcc) / g_frames,
                       Ms(g_studioAcc) / g_frames,
                       worldMs);
            rendererlog::Line("PROFILE fastpaths: point=%llu hits=%llu (%.1f%%)  "
                       "mark=%llu fast=%llu fallback=%llu leaves=%llu mark_ms=%.4f/frame  "
                       "gl_skip en/dis=%llu alpha=%llu blend=%llu depth=%llu depthmask=%llu texenv=%llu",
                       static_cast<unsigned long long>(vis.pointCalls),
                       static_cast<unsigned long long>(vis.pointHits),
                       vis.pointCalls ? (100.0 * vis.pointHits / vis.pointCalls) : 0.0,
                       static_cast<unsigned long long>(vis.markCalls),
                       static_cast<unsigned long long>(vis.markFast),
                       static_cast<unsigned long long>(vis.markFallback),
                       static_cast<unsigned long long>(vis.visibleLeaves),
                       Ms(vis.markTicks) / g_frames,
                       static_cast<unsigned long long>(gl.enableDisableSkipped),
                       static_cast<unsigned long long>(gl.alphaSkipped),
                       static_cast<unsigned long long>(gl.blendSkipped),
                       static_cast<unsigned long long>(gl.depthSkipped),
                       static_cast<unsigned long long>(gl.depthMaskSkipped),
                       static_cast<unsigned long long>(gl.texEnvSkipped));
            rendererlog::Line("PROFILE vbo: chains=%llu surfaces=%llu legacy=%llu vertices=%llu "
                       "indices=%llu tris=%llu drawarrays=%llu multidraw=%llu "
                       "drawelements=%llu multielements=%llu seqruns=%llu seqsurf=%llu "
                       "seqbarrier=%llu brushScopes=%llu brushSurf=%llu brushVerts=%llu "
                       "brushBegin=%llu brushShapeRej=%llu brushIndexRej=%llu brushEligRej=%llu "
                       "detailTry=%llu detailDraw=%llu detailFallback=%llu detailVerts=%llu "
                       "chains_ms=%.4f/frame",
                       static_cast<unsigned long long>(vbo.chainsBatched),
                       static_cast<unsigned long long>(vbo.surfacesBatched),
                       static_cast<unsigned long long>(vbo.surfacesLegacy),
                       static_cast<unsigned long long>(vbo.verticesBatched),
                       static_cast<unsigned long long>(vbo.indicesBatched),
                       static_cast<unsigned long long>(vbo.trianglesBatched),
                       static_cast<unsigned long long>(vbo.drawArraysCalls),
                       static_cast<unsigned long long>(vbo.multiDrawCalls),
                       static_cast<unsigned long long>(vbo.drawElementsCalls),
                       static_cast<unsigned long long>(vbo.multiDrawElementsCalls),
                       static_cast<unsigned long long>(vbo.sequentialRuns),
                       static_cast<unsigned long long>(vbo.sequentialDeferredSurfaces),
                       static_cast<unsigned long long>(vbo.sequentialBarriers),
                       static_cast<unsigned long long>(vbo.brushScopes),
                       static_cast<unsigned long long>(vbo.brushSurfaces),
                       static_cast<unsigned long long>(vbo.brushVertices),
                       static_cast<unsigned long long>(vbo.brushBeginCalls),
                       static_cast<unsigned long long>(vbo.brushBeginShapeRejects),
                       static_cast<unsigned long long>(vbo.brushIndexRejects),
                       static_cast<unsigned long long>(vbo.brushEligibilityRejects),
                       static_cast<unsigned long long>(vbo.detailAttempts),
                       static_cast<unsigned long long>(vbo.detailDraws),
                       static_cast<unsigned long long>(vbo.detailFallbacks),
                       static_cast<unsigned long long>(vbo.detailVertices),
                       vbo.drawTextureChainsCalls
                           ? Ms(vbo.drawTextureChainsTicks) / g_frames : 0.0);
            rendererlog::Line("PROFILE phases: scene=%.3f entities=%.3f trans=%.3f "
                       "viewmodel=%.3f dlights=%.3f particles=%.3f",
                       Ms(g_renderSceneAcc) / g_frames,
                       Ms(g_entitiesAcc) / g_frames,
                       Ms(g_tentitiesAcc) / g_frames,
                       Ms(g_viewModelAcc) / g_frames,
                       Ms(g_dlightsAcc) / g_frames,
                       Ms(g_particlesAcc) / g_frames);
            rendererlog::Line(
                "PROFILE particle_vbo: captures=%llu vertices=%llu uploads=%llu empty=%llu "
                "fallback=%llu callback=%llu death=%llu list=%llu slots=%llu buffer=%llu array=%llu",
                static_cast<unsigned long long>(particles.captures),
                static_cast<unsigned long long>(particles.vertices),
                static_cast<unsigned long long>(particles.uploads),
                static_cast<unsigned long long>(particles.emptyCaptures),
                static_cast<unsigned long long>(particles.fallbacks),
                static_cast<unsigned long long>(particles.callbackFallbacks),
                static_cast<unsigned long long>(particles.deathFallbacks),
                static_cast<unsigned long long>(particles.listFallbacks),
                static_cast<unsigned long long>(particles.slotFallbacks),
                static_cast<unsigned long long>(particles.bufferFallbacks),
                static_cast<unsigned long long>(particles.arrayFallbacks));
            rendererlog::Line(
                "PROFILE beam_vbo: captures=%llu vertices=%llu uploads=%llu "
                "fallback=%llu slots=%llu buffer=%llu array=%llu replay=%llu modeSkip=%llu",
                static_cast<unsigned long long>(beams.captures),
                static_cast<unsigned long long>(beams.vertices),
                static_cast<unsigned long long>(beams.uploads),
                static_cast<unsigned long long>(beams.fallbacks),
                static_cast<unsigned long long>(beams.slotFallbacks),
                static_cast<unsigned long long>(beams.bufferFallbacks),
                static_cast<unsigned long long>(beams.arrayFallbacks),
                static_cast<unsigned long long>(beams.replays),
                static_cast<unsigned long long>(beams.renderModeSkips));
            rendererlog::Line(
                "PROFILE tracer_vbo: captures=%llu vertices=%llu uploads=%llu "
                "fallback=%llu slots=%llu buffer=%llu array=%llu replay=%llu",
                static_cast<unsigned long long>(beams.tracerCaptures),
                static_cast<unsigned long long>(beams.tracerVertices),
                static_cast<unsigned long long>(beams.tracerUploads),
                static_cast<unsigned long long>(beams.tracerFallbacks),
                static_cast<unsigned long long>(beams.tracerSlotFallbacks),
                static_cast<unsigned long long>(beams.tracerBufferFallbacks),
                static_cast<unsigned long long>(beams.tracerArrayFallbacks),
                static_cast<unsigned long long>(beams.tracerReplays));
            rendererlog::Line(
                "PROFILE sprite_vbo: attempts=%llu gate=%llu dis=%llu slotsG=%llu slotMask=0x%llX matrix=%llu blend=%llu entity=%llu model=%llu "
                "captures=%llu sprites=%llu vertices=%llu "
                "uploads=%llu draws=%llu fallback=%llu state=%llu slots=%llu replay=%llu",
                static_cast<unsigned long long>(sprites.attempts),
                static_cast<unsigned long long>(sprites.gateRejects),
                static_cast<unsigned long long>(sprites.gateDisabled),
                static_cast<unsigned long long>(sprites.gateSlots),
                static_cast<unsigned long long>(sprites.slotMismatchMask),
                static_cast<unsigned long long>(sprites.gateMatrix),
                static_cast<unsigned long long>(sprites.gateBlend),
                static_cast<unsigned long long>(sprites.gateEntity),
                static_cast<unsigned long long>(sprites.gateModel),
                static_cast<unsigned long long>(sprites.captures),
                static_cast<unsigned long long>(sprites.sprites),
                static_cast<unsigned long long>(sprites.vertices),
                static_cast<unsigned long long>(sprites.uploads),
                static_cast<unsigned long long>(sprites.drawCalls),
                static_cast<unsigned long long>(sprites.fallbacks),
                static_cast<unsigned long long>(sprites.stateFallbacks),
                static_cast<unsigned long long>(sprites.slotFallbacks),
                static_cast<unsigned long long>(sprites.replays));

            if (g_benchMode && g_benchStage == 0)
            {
                g_benchBaselineFrameMs = frameMs;
                g_benchBaselineWorldMs = worldMs;
                SetCvar("r_fastpath", 1.0f);
                rendererperf::UpdateFrame();
                g_benchStage = 1;
                rendererlog::Line("BENCH switch: next %d frames perf=ON", kReport);
            }
            else if (g_benchMode && g_benchStage == 1)
            {
                const double frameGain = g_benchBaselineFrameMs > 0.0
                    ? (g_benchBaselineFrameMs - frameMs) * 100.0 / g_benchBaselineFrameMs : 0.0;
                const double worldGain = g_benchBaselineWorldMs > 0.0
                    ? (g_benchBaselineWorldMs - worldMs) * 100.0 / g_benchBaselineWorldMs : 0.0;
                rendererlog::Line("BENCH result: frame OFF=%.4f ON=%.4f ms gain=%.2f%%, "
                           "world OFF=%.4f ON=%.4f ms gain=%.2f%%",
                           g_benchBaselineFrameMs, frameMs, frameGain,
                           g_benchBaselineWorldMs, worldMs, worldGain);
                SetCvar("fps_max", g_benchOldFpsMax);
                SetCvar("r_fastpath", 1.0f);
                SetCvar("r_profile", 0.0f);
                g_benchStage = 2;
                rendererlog::Line("BENCH done: restored fps_max %.1f, perf=ON, profile=OFF",
                           g_benchOldFpsMax);
            }
            g_frameAcc = g_redrawAcc = g_trisAcc = g_studioAcc = 0;
            g_renderViewAcc = 0;
            g_renderViewCalls = 0;
            g_renderSceneAcc = g_entitiesAcc = g_tentitiesAcc = 0;
            g_viewModelAcc = g_dlightsAcc = g_particlesAcc = 0;
            g_frames = 0;
        }
    }
    if (profiling && !Active())
        rendererlog::SetDeferred(false);
    return result;
}

void __cdecl Frame_Hook(double time)
{
    InitializeOnMainThread();
    if (g_origFrame)
        g_origFrame(time);
}

void __cdecl Tris_Hook()
{
    if (!g_origTris)
        return;

    if (!Active())
    {
        g_origTris();
        return;
    }

    long long s = Now();
    g_origTris();
    g_trisAcc += Now() - s;
}

void __cdecl RenderView_Hook()
{
    if (!g_origRenderView)
        return;
    if (!Active())
    {
        g_origRenderView();
        return;
    }

    const long long start = Now();
    g_origRenderView();
    g_renderViewAcc += Now() - start;
    ++g_renderViewCalls;
}

void __cdecl RenderScene_Hook()
{
    if (!g_origRenderScene) return;
    if (!Active()) { g_origRenderScene(); return; }
    const long long start = Now();
    g_origRenderScene();
    g_renderSceneAcc += Now() - start;
}

void __cdecl DrawEntities_Hook()
{
    if (!g_origDrawEntities) return;
    studio_renderer::BeginSolidEntityPass();
    if (!Active())
    {
        g_origDrawEntities();
        studio_renderer::EndSolidEntityPass();
        return;
    }
    const long long start = Now();
    g_origDrawEntities();
    studio_renderer::EndSolidEntityPass();
    g_entitiesAcc += Now() - start;
}

void __fastcall DrawTEntities_Hook(int pass)
{
    if (!g_origDrawTEntities) return;
    if (!Active()) { g_origDrawTEntities(pass); return; }
    const long long start = Now();
    g_origDrawTEntities(pass);
    g_tentitiesAcc += Now() - start;
}

void __cdecl DrawViewModel_Hook()
{
    if (!g_origDrawViewModel) return;
    if (!Active()) { g_origDrawViewModel(); return; }
    const long long start = Now();
    g_origDrawViewModel();
    g_viewModelAcc += Now() - start;
}

void __cdecl RenderDlights_Hook()
{
    if (!g_origRenderDlights) return;
    if (!Active()) { g_origRenderDlights(); return; }
    const long long start = Now();
    g_origRenderDlights();
    g_dlightsAcc += Now() - start;
}

void __cdecl DrawParticles_Hook()
{
    if (!g_origDrawParticles) return;
    if (!Active()) { g_origDrawParticles(); return; }
    const long long start = Now();
    g_origDrawParticles();
    g_particlesAcc += Now() - start;
}
} // namespace

bool InstallRendererProfile(HMODULE hw)
{
    if (g_origRenderView)
        return true;
    if (!hwbuild::MatchesTarget(hw))
        return false;

    auto* target = reinterpret_cast<std::uint8_t*>(hw) + 0x00071E70u;
    g_origRenderView = reinterpret_cast<RenderViewFn>(
        inl::Hook(target, reinterpret_cast<void*>(&RenderView_Hook)));
    auto* base = reinterpret_cast<std::uint8_t*>(hw);
    g_origRenderScene = reinterpret_cast<RenderSceneFn>(
        inl::Hook(base + 0x00071C40u, reinterpret_cast<void*>(&RenderScene_Hook)));
    g_origDrawEntities = reinterpret_cast<DrawEntitiesFn>(
        inl::Hook(base + 0x000700F0u, reinterpret_cast<void*>(&DrawEntities_Hook)));
    g_origDrawTEntities = reinterpret_cast<DrawTEntitiesFn>(
        inl::Hook(base + 0x0009DE10u, reinterpret_cast<void*>(&DrawTEntities_Hook)));
    g_origDrawViewModel = reinterpret_cast<DrawViewModelFn>(
        inl::Hook(base + 0x0006F6D0u, reinterpret_cast<void*>(&DrawViewModel_Hook)));
    g_origRenderDlights = reinterpret_cast<RenderDlightsFn>(
        inl::Hook(base + 0x0006E3B0u, reinterpret_cast<void*>(&RenderDlights_Hook)));
    g_origDrawParticles = reinterpret_cast<DrawParticlesFn>(
        inl::Hook(base + 0x0008AFC0u, reinterpret_cast<void*>(&DrawParticles_Hook)));

    rendererlog::Line("prof: renderer phase hooks view=%s scene=%s entities=%s trans=%s viewmodel=%s dlights=%s particles=%s",
               g_origRenderView ? "ok" : "fail",
               g_origRenderScene ? "ok" : "fail",
               g_origDrawEntities ? "ok" : "fail",
               g_origDrawTEntities ? "ok" : "fail",
               g_origDrawViewModel ? "ok" : "fail",
               g_origRenderDlights ? "ok" : "fail",
               g_origDrawParticles ? "ok" : "fail");
    return g_origRenderView != nullptr;
}

bool Install(HMODULE client, cl_enginefunc_t* engine,
             MainThreadInitFn on_first_redraw)
{
    if (!client || !engine || !on_first_redraw)
    {
        rendererlog::Line("prof: bootstrap install received invalid arguments");
        return false;
    }

    g_engine = engine;
    g_client = client;
    g_onFirstRedraw = on_first_redraw;
    g_benchMode = std::strstr(GetCommandLineA(), "-gcrenderbench") != nullptr;
    QueryPerformanceFrequency(&g_freq);

    void* redraw = reinterpret_cast<void*>(GetProcAddress(client, "HUD_Redraw"));
    void* frame  = reinterpret_cast<void*>(GetProcAddress(client, "HUD_Frame"));
    if (!redraw && !frame)
    {
        rendererlog::Line("prof: HUD_Redraw and HUD_Frame exports missing");
        return false;
    }

    if (redraw)
        g_origRedraw = reinterpret_cast<RedrawFn>(
            inl::Hook(redraw, reinterpret_cast<void*>(&Redraw_Hook)));
    if (frame)
        g_origFrame = reinterpret_cast<FrameFn>(
            inl::Hook(frame, reinterpret_cast<void*>(&Frame_Hook)));

    if (!g_origRedraw && !g_origFrame)
    {
        rendererlog::Line("prof: bootstrap hooks failed (HUD_Redraw/HUD_Frame)");
        return false;
    }

    rendererlog::Line("prof: bootstrap redraw=%p frame=%p (main init on first main-thread callback)",
               g_origRedraw, g_origFrame);
    return true;
}
}
