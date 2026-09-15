# GCRender Experimental

Experimental renderer and optimization layer for GoldClient.

The project builds a 32-bit `gcrender.asi` module loaded by the Miles ASI provider in `Mss32.dll`. It captures the GoldSrc client engine table after `client.dll` is initialized, installs renderer hooks in memory, and keeps the original GoldClient renderer as the fallback path.

This repository contains the renderer and optimization layer for GoldClient.

## Target build

Internal renderer hooks are enabled only for the GoldClient `hw.dll` build identified by:

- PE timestamp: `0x6A49A361`
- `SizeOfImage`: `0x03CA7000`

If that build check fails, the affected optimizations stay disabled instead of applying offsets to an unknown renderer binary.

## Main controls

### `r_fastpath`

- `r_fastpath 1` enables the safe fast paths. This is the default.
- `r_fastpath 0` leaves the hooks installed in pass-through mode for A/B testing without restarting the client.

The current fast paths include renderer state filtering, visibility/cache work, world and effect VBO paths, and Studio renderer optimizations. Each subsystem keeps its own runtime guards and fallback behavior.

### `r_profile`

- `r_profile 1` enables renderer profiling.
- `r_profile 0` disables profiling. This is the default.

Reports are generated every 600 frames. While profiling is active, log lines stay in a fixed in-memory buffer, switching back to `r_profile 0` appends the buffered block to `gcrender.log` next to the ASI with a single file open/close. Detailed profiling hooks are installed or activated only when required.

### Automatic benchmark

Launch GoldClient with `-gcrenderbench` to run the built-in comparison. It raises `fps_max`, measures a baseline with `r_fastpath 0`, measures again with `r_fastpath 1`, buffers the reports during measurement, restores the runtime values, and flushes the result to `gcrender.log` after profiling stops.

## Runtime CVAR reference

The table below lists every CVAR registered by GCRender. The status reflects the current experimental configuration in this repository:

- **Positive/default**: enabled by default because this is the currently accepted optimization path for the target GoldClient build.
- **Not proven positive**: implemented and available for A/B testing, but left disabled by default because this repository does not contain a positive performance result that justifies enabling it globally yet.
- **Diagnostic**: measurement/control CVAR rather than a renderer optimization.

| CVAR | Default | Status | What it changes / intended positive effect |
| --- | ---: | --- | --- |
| `r_fastpath` | `1` | **Positive/default** | Master switch for the optimized renderer paths. `0` keeps hooks installed but routes them through pass-through behavior for direct A/B tests. |
| `r_profile` | `0` | **Diagnostic** | Enables frame/phase profiling and periodic reports. While enabled, log output is buffered in RAM and flushed to `gcrender.log` when profiling is disabled, avoiding filesystem I/O during the measured frames. |
| `r_world_vbo` | `1` | **Positive/default** | Uses retained BSP world VBO/EBO rendering to reduce repeated immediate-mode geometry submission and GL call overhead. |
| `r_world_brush_vbo` | `1` | **Positive/default** | Uses the VBO path for brush-model world geometry, reducing repeated CPU-side vertex submission. |
| `r_particle_vbo` | `1` | **Positive/default** | Streams particle geometry through a VBO instead of issuing the original immediate-mode vertex sequence. |
| `r_beam_vbo` | `1` | **Positive/default** | Batches supported beam geometry into a streaming VBO, reducing `glBegin`/per-vertex call overhead. |
| `r_tracer_vbo` | `0` | **Not proven positive** | Enables the tracer VBO capture path. It is available for comparison but remains disabled until it shows a repeatable gain. |
| `r_sprite_vbo` | `0` | **Not proven positive** | Enables the solid sprite-model streaming VBO path. It remains off because a positive result has not been established. |
| `r_studio_slerp` | `1` | **Positive/default** | Uses the fast Studio animation blend/slerp path to reduce CPU work while interpolating animation data. |
| `r_studio_bones` | `1` | **Positive/default** | Uses the optimized Studio bone/rotation calculation path. |
| `r_studio_posecache` | `1` | **Positive/default** | Reuses compatible Studio pose results so repeated pose calculations can be avoided. |
| `r_studio_savebones` | `2` | **Positive/default** | Optimizes Studio saved-bone handling. Mode `2` also runs the stock path for validation/comparison before using the optimized result where valid. |
| `r_studio_mergebones` | `1` | **Positive/default** | Enables the optimized bone-merge path used by Studio attachments/merged models. |
| `r_studio_gait` | `1` | **Positive/default** | Enables the optimized Studio gait-sequence calculation path. |
| `r_studio_concat` | `1` | **Positive/default** | Replaces repeated Studio transform concatenation work with the optimized implementation. |
| `r_studio_lighting` | `1` | **Positive/default** | Uses the optimized core Studio lighting calculation path to reduce CPU work per rendered model. |
| `r_studio_elight` | `1` | **Positive/default** | Optimizes Studio entity/dynamic-light processing. |
| `r_studio_chrome` | `1` | **Positive/default** | Uses the optimized Studio chrome/environment-coordinate calculation path. |
| `r_studio_skin` | `1` | **Positive/default** | Uses the optimized Studio skin-selection/skin-family handling path. |
| `r_studio_texture` | `1` | **Positive/default** | Uses the faster Studio texture resolver/bind path to reduce repeated texture lookup/bind work. |
| `r_studio_texmips` | `1` | **Positive/default** | Generates mipmaps for eligible ordinary Studio textures that would otherwise be uploaded without them, improving minification behavior and reducing texture sampling pressure at distance. |
| `r_studio_texcompress` | `2` | **Positive/default** | Compresses eligible Studio textures, mode `2` also permits masked textures (DXT5), while ordinary eligible textures use DXT1. This reduces GPU texture memory/bandwidth use. |
| `r_studio_texcache` | `1` | **Positive/default** | Enables the Studio texture upload/cache path so compatible texture work can be reused instead of rebuilt unnecessarily. |
| `r_studio_texcompress_min` | `64` | **Positive/default** | Minimum width and height for Studio texture compression/mipmap eligibility. Raising it limits optimization to larger textures, lowering it includes smaller ones. |
| `r_studio_batch` | `1` | **Positive/default** | Enables Studio triangle-command batching to reduce immediate-mode GL submission overhead. |
| `r_studio_predecode` | `1` | **Positive/default** | Predecodes reusable Studio triangle-command data so it does not need to be decoded again on every draw. |
| `r_studio_drawrange` | `1` | **Positive/default** | Uses the draw-range path where supported to submit indexed Studio geometry with tighter vertex ranges. Mode `2` is also accepted for experimental comparison. |
| `r_studio_indexbuffer` | `0` | **Not proven positive** | Enables the alternate Studio index-buffer path (`1`/`2`). It remains disabled because no positive result is established for the default configuration. |
| `r_studio_vertexbuffer` | `0` | **Not proven positive** | Enables the alternate Studio vertex-buffer path. It remains disabled pending evidence of a repeatable benefit. |
| `r_studio_gpu` | `0` | **Not proven positive** | Enables the experimental GPU-oriented Studio path (`1`/`2`). It is kept off until its performance and compatibility benefit is demonstrated. |
| `r_studio_renderer` | `1` | **Positive/default** | Enables the retained whole-model Studio renderer/cache path, avoiding repeated rebuild work when the model state can be reused safely. |
| `r_studio_nonplayer` | `0` | **Not proven positive** | Extends the retained Studio renderer path to non-player models. It remains disabled because the broader path is still experimental. |
| `r_meshoptimizer` | `1` | **Positive/default** | Enables meshoptimizer-backed Studio mesh/index processing to improve geometry locality and submission efficiency. |
| `r_studio_instancing` | `0` | **Not proven positive** | Enables experimental Studio instancing. It remains disabled because a positive result has not been established. |

## Measured runtime evidence

These are historical A/B captures from the same renderer lineage and target GoldClient environment. They are useful evidence for direction and relative cost, but they are not universal guarantees: scene content, driver state, hardware, and build revisions can change the result. The older captures also predate deferred `r_profile` log flushing, so `frame` time may contain a small amount of filesystem noise. Renderer/Studio phase timings are the more useful values here. Current GCRender should be re-benchmarked before treating these numbers as final certification.

| Path | Comparison | Measured result | Interpretation |
| --- | --- | --- | --- |
| Deferred/GPU Studio skin path | OFF: renderer `1.908 ms`, Studio `0.976 ms`, ON: renderer `1.430 ms`, Studio `0.666 ms` | Renderer `-0.478 ms` (`-25.1%`), Studio `-0.310 ms` (`-31.8%`) | **Positive in this capture.** The full frame result was `33.461 -> 33.340 ms`, but that run was capped near 30 FPS, so the phase deltas are more meaningful than total frame time. |
| Studio pose cache | OFF: renderer `1.720 ms`, Studio `0.869 ms`, ON: renderer `1.750 ms`, Studio `0.884 ms` | Renderer `+0.030 ms` (`+1.7%`), Studio `+0.015 ms` (`+1.7%`) | **Neutral/slightly negative in this capture.** A representative sample hit `3,634 / 20,480` lookups (`17.7%`), a longer run hit `43,416 / 278,528` (`15.6%`). This path needs a fresh uncapped A/B before calling it a proven gain. |
| `r_studio_savebones 2` validation mode | Stock call `0.601 us`, validation/fast segment `1.162 us` | Validation segment `+0.561 us` (`+93.3%` versus the measured stock call) | **Expected negative overhead for validation mode.** Mode `2` deliberately executes stock work plus validation/comparison and should not be interpreted as the performance result of mode `1`. The capture reported `mismatch=0`. |

The retained Studio renderer also has useful absolute timing diagnostics even when no stock A/B is available. In representative runs with roughly 54-55% direct coverage, direct retained draws were around `15-22 us/call`, with the log breaking this down into state, uniform/UBO, material, draw, and restore cost. Those absolute numbers are diagnostic only, without a matched stock capture they are not listed above as a claimed speedup.

For performance work, compare one experimental CVAR at a time against the same scene and client state. `-gcrenderbench` currently measures the aggregate `r_fastpath 0` versus `r_fastpath 1` result, it does not independently certify every subsystem CVAR in this table.

## Build

Requirements:

- Windows
- Visual Studio 2022 Build Tools with the C++ toolchain
- CMake 3.21 or newer

Build from PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

Output:

```text
build\Release\gcrender.asi
```

Copy `gcrender.asi` to the GoldClient game directory next to `hl.exe` and `Mss32.dll`.

## Code layout

- `src/dllmain.cpp`: ASI entry points, GoldClient engine-table discovery, and main-thread initialization bootstrap.
- `src/profile.cpp`: profiling, frame updates, renderer phase hooks, and automatic benchmark.
- `src/perf_control.cpp`: global `r_fastpath` runtime control.
- `src/gl_state.cpp`: redundant OpenGL state filtering and related profiling.
- `src/vis_cache.cpp`: visibility-related fast paths and frame cache.
- `src/world_vbo.cpp`: world and brush VBO paths.
- `src/particle_vbo.cpp`, `src/beam_vbo.cpp`, `src/sprite_vbo.cpp`: effect rendering paths.
- `src/studio*.cpp`: Studio model renderer optimizations, batching, caches, texture paths, lighting, skinning, and related fallbacks.
- `src/hw_build.h`: exact `hw.dll` build gate used by internal-offset hooks.
- `third_party/meshoptimizer`: vendored meshoptimizer dependency.
