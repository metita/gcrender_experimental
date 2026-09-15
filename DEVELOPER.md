# GCRender developer notes

## Purpose

GCRender is the renderer and optimization layer for the 32 bit GoldClient build. It is loaded as `gcrender.asi` by the Miles provider scan and installs in process memory after the GoldSrc client is ready.

GoldClient files on disk are not replaced or patched by the module.

## Compatibility gate

Internal address based hooks are enabled only for the following `hw.dll` build:

- PE timestamp: `0x6A49A361`
- `SizeOfImage`: `0x03CA7000`

If either value differs, the affected path stays disabled. The original renderer remains available as the runtime fallback.

## Initialization sequence

1. The provider loader enters `DllMain` or `RIB_Main`.
2. The module pins itself and starts a worker thread.
3. The worker waits for `client.dll` and locates the live `gEngfuncs` table through `Initialize`.
4. The worker installs the `HUD_Redraw` bootstrap.
5. The first redraw performs main thread initialization and installs the renderer hooks.
6. Each subsystem validates its own prerequisites before using an optimized path.

The delayed initialization is required because the engine table is not usable during provider load.

## Runtime controls

`r_fastpath 1` enables the optimized paths. This is the default.

`r_fastpath 0` keeps the hooks installed but routes the affected work through pass-through behavior for comparison.

`r_profile 1` enables frame and phase profiling. Reports are written to `gcrender.log` beside the module every 600 frames.

The `-gcrenderbench` launch option runs the built-in comparison, records the result in `gcrender.log`, and restores the runtime values when it finishes.

## Source map

- `src/dllmain.cpp`: provider entry points and initialization bootstrap.
- `src/profile.cpp`: frame hooks, profiling, and benchmark control.
- `src/perf_control.cpp`: runtime fast path switch.
- `src/gl_state.cpp`: OpenGL state filtering.
- `src/vis_cache.cpp`: visibility cache paths.
- `src/world_vbo.cpp`: world and brush geometry paths.
- `src/particle_vbo.cpp`, `src/beam_vbo.cpp`, `src/sprite_vbo.cpp`: effect geometry paths.
- `src/studio*.cpp`: Studio model batching, cache, texture, lighting, skinning, and fallback paths.
- `src/hw_build.h`: exact target build validation.

## Build and handoff

Requirements are Windows, Visual Studio 2022 Build Tools with the C++ toolchain, and CMake 3.21 or newer.

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The expected artifact is:

```text
build\Release\gcrender.asi
```

For a GoldClient test, copy only `gcrender.asi` beside `hl.exe` and `Mss32.dll`. Review `gcrender.log` after launch and confirm that initialization reaches the main thread before evaluating renderer behavior.

Keep GCRender independent from the separate ZgAsi tree. Do not copy generated build directories, runtime captures, logs, or debug symbols into a release commit.

## Acceptance checklist

- Configure and build the Win32 Release target successfully.
- Confirm that the output name is `gcrender.asi`.
- Confirm that `RIB_Main`, `ASI_startup`, `ASI_shutdown`, and `ASI_error` are exported.
- Confirm that the exact `hw.dll` gate is active in the target installation.
- Confirm that the module loads without replacing GoldClient files.
- Confirm that `r_fastpath` and `r_profile` are reported in the runtime log.
- Compare optimized and pass-through captures only after a clean client restart.
