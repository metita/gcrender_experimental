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

Launch GoldClient with `-gcrenderbench` to run the built-in comparison. It raises `fps_max`, measures a baseline with `r_fastpath 0`, measures again with `r_fastpath 1`, buffers the reports during measurement, restores the runtime values, and flushes the result to `gcrender.log` after profiling stops. Keep the GoldClient window in foreground. This mode is a coarse aggregate sanity check, not a per-feature certification.

## Runtime CVAR reference

The table below lists every CVAR registered by GCRender. The status describes the current runtime default, not a blanket performance claim:

- **Default/on**: enabled in the current experimental configuration.
- **Experimental/off**: implemented and available for A/B testing, but disabled by default.
- **Diagnostic**: measurement/control CVAR rather than a renderer optimization.
- **Parameter**: adjusts the scope of another optimization.

| CVAR | Default | Status | What it changes |
| --- | ---: | --- | --- |
| `r_fastpath` | `1` | **Default/on** | Master switch for the optimized renderer paths. `0` keeps hooks installed but routes them through pass-through behavior for direct A/B tests. |
| `r_profile` | `0` | **Diagnostic** | Enables frame/phase profiling and periodic reports. While enabled, log output is buffered in RAM and flushed to `gcrender.log` when profiling is disabled, avoiding filesystem I/O during the measured frames. |
| `r_world_vbo` | `1` | **Default/on** | Uses retained BSP world VBO/EBO rendering to reduce repeated immediate-mode geometry submission and GL call overhead. |
| `r_world_brush_vbo` | `1` | **Default/on** | Uses the VBO path for brush-model world geometry, reducing repeated CPU-side vertex submission. |
| `r_particle_vbo` | `1` | **Default/on** | Streams particle geometry through a VBO instead of issuing the original immediate-mode vertex sequence. |
| `r_beam_vbo` | `1` | **Default/on** | Batches supported beam geometry into a streaming VBO, reducing `glBegin`/per-vertex call overhead. |
| `r_tracer_vbo` | `0` | **Experimental/off** | Enables the tracer VBO capture path. It remains disabled because a repeatable positive result has not been established. |
| `r_sprite_vbo` | `0` | **Experimental/off** | Enables the solid sprite-model streaming VBO path. A controlled targeted test measured a regression, so the path remains disabled. |
| `r_studio_slerp` | `1` | **Default/on** | Uses the fast Studio animation blend/slerp path to reduce CPU work while interpolating animation data. |
| `r_studio_bones` | `1` | **Default/on** | Uses the optimized Studio bone/rotation calculation path. |
| `r_studio_posecache` | `1` | **Default/on** | Reuses compatible Studio pose results so repeated pose calculations can be avoided. Current measurements do not show a strong standalone gain. |
| `r_studio_savebones` | `2` | **Default/on** | Optimizes Studio saved-bone handling. Mode `2` deliberately runs stock work plus validation and therefore has diagnostic overhead. Mode `1` is the performance mode. |
| `r_studio_mergebones` | `1` | **Default/on** | Enables the optimized bone-merge path used by Studio attachments/merged models. |
| `r_studio_gait` | `1` | **Default/on** | Enables the optimized Studio gait-sequence calculation path. |
| `r_studio_concat` | `1` | **Default/on** | Replaces repeated Studio transform concatenation work with the optimized implementation. |
| `r_studio_lighting` | `1` | **Default/on** | Uses the optimized core Studio lighting calculation path to reduce CPU work per rendered model. |
| `r_studio_elight` | `1` | **Default/on** | Optimizes Studio entity/dynamic-light processing. |
| `r_studio_chrome` | `1` | **Default/on** | Uses the optimized Studio chrome/environment-coordinate calculation path. |
| `r_studio_skin` | `1` | **Default/on** | Uses the optimized Studio skin-selection/skin-family handling path. |
| `r_studio_texture` | `1` | **Default/on** | Uses the faster Studio texture resolver/bind path to reduce repeated texture lookup/bind work. |
| `r_studio_texmips` | `1` | **Default/on** | Generates mipmaps for eligible ordinary Studio textures that would otherwise be uploaded without them. |
| `r_studio_texcompress` | `2` | **Default/on** | Compresses eligible Studio textures. Mode `2` also permits masked textures with DXT5, while ordinary eligible textures use DXT1. |
| `r_studio_texcache` | `1` | **Default/on** | Enables the Studio texture upload/cache path so compatible texture work can be reused instead of rebuilt unnecessarily. |
| `r_studio_texcompress_min` | `64` | **Parameter** | Minimum width and height for Studio texture compression and mipmap eligibility. |
| `r_studio_batch` | `1` | **Default/on** | Enables Studio triangle-command batching to reduce immediate-mode GL submission overhead. |
| `r_studio_predecode` | `1` | **Default/on** | Predecodes reusable Studio triangle-command data so it does not need to be decoded again on every draw. |
| `r_studio_drawrange` | `1` | **Default/on** | Uses the draw-range path where supported to submit indexed Studio geometry with tighter vertex ranges. Mode `2` is accepted for experimental comparison. |
| `r_studio_indexbuffer` | `0` | **Experimental/off** | Enables the alternate Studio index-buffer path in mode `1` or `2`. Mode `1` showed a negative screening result. Mode `2` remains promising but is not fully paired. |
| `r_studio_vertexbuffer` | `0` | **Experimental/off** | Enables the alternate Studio vertex-buffer path. The current screen is not sufficient to certify a gain. |
| `r_studio_gpu` | `0` | **Experimental/off** | Enables the experimental GPU-oriented Studio path in mode `1` or `2`. Current screening is promising but full compatibility and repeated A/B closure are still required. |
| `r_studio_renderer` | `1` | **Default/on** | Enables the retained whole-model Studio renderer/cache path. The current screen shows a clear Studio phase reduction, while total frame impact remains scene dependent. |
| `r_studio_nonplayer` | `0` | **Experimental/off** | Extends the retained Studio renderer path to non-player models. Current screening exercises the path, but it remains disabled pending broader compatibility validation. |
| `r_meshoptimizer` | `1` | **Default/on** | Enables meshoptimizer-backed Studio mesh/index processing to improve geometry locality and submission efficiency. |
| `r_studio_instancing` | `0` | **Experimental/off** | Enables experimental Studio instancing. The current benchmark workload recorded no instanced draws, so no performance conclusion is claimed. |

## Benchmark and profiling evidence

The raw measurements are kept in [`benchmarks/`](benchmarks/README.md). The current benchmark build has SHA256 `4E377F7EB27D9E60EE625B0F75065A760DDFF5C08BABB75F3EF2D54A6102177F`.

### Test system

| Item | Value |
| --- | --- |
| CPU | AMD Ryzen 5 5600G |
| GPU | NVIDIA GeForce GTX 1060 3 GB |
| GPU driver | `32.0.15.8253` |
| OS | Windows 11 Pro build 26200 |
| Resolution | 1280x720 windowed |
| VSync | Off |
| `fps_override` | `1` |
| `fps_max` | `1000` |
| Primary workload | `cstrike/zetest.dem` |

GoldClient throttles the client to about 30 FPS when its window is not foreground. Those runs were rejected. `r_profile` also defers log writes until profiling stops, so filesystem latency is not inside the measured 600-frame windows.

### Current baseline

Five uncapped default-configuration samples produced the following distribution:

| Metric | Median | Range |
| --- | ---: | ---: |
| Frame | `3.056 ms` | `3.034 to 3.134 ms` |
| Renderer | `2.112 ms` | `2.082 to 2.122 ms` |
| Studio | `0.857 ms` | `0.851 to 0.884 ms` |
| World | `0.377 ms` | `0.371 to 0.380 ms` |

Small deltas inside this range should not be presented as proven gains from a single launch.

### Strongest current-build signals

The table below compares one feature change against the default baseline median. These are screening results unless explicitly marked as paired. Phase timings are more useful than total frame time because different demo launches can contain different scene load.

| Path | Test | Measured signal | Interpretation |
| --- | --- | --- | --- |
| `r_studio_bones` | `1 -> 0` | Studio `0.857 -> 1.033 ms`, `+0.176 ms` | Strong positive signal for the optimized bone path. |
| `r_studio_mergebones` | `1 -> 0` | Studio `0.857 -> 0.945 ms`, `+0.088 ms` | Positive Studio phase signal. |
| `r_studio_batch` | `1 -> 0` | Renderer `2.112 -> 2.263 ms`, Studio `0.857 -> 0.952 ms` | Strong positive signal for batching. |
| `r_studio_chrome` | `1 -> 0` | Studio `0.857 -> 0.907 ms`, `+0.050 ms` | Positive phase signal in this workload. |
| `r_studio_drawrange` | `1 -> 0` | Studio `0.857 -> 0.904 ms`, `+0.047 ms` | Positive phase signal. |
| `r_meshoptimizer` | `1 -> 0` | Frame `3.056 -> 3.208 ms`, Studio `0.857 -> 0.902 ms` | Positive screening signal. |
| `r_studio_renderer` | `1 -> 0` | Studio `0.857 -> 1.096 ms`, `+0.239 ms` | The retained renderer clearly reduces measured Studio phase time. Total frame time is scene dependent. |
| `r_studio_indexbuffer` mode `1` | `0 -> 1` | Studio `0.857 -> 0.973 ms` | Negative screening result, consistent with leaving mode `1` disabled. |

### Paired A/B results

Two A/B orderings were completed for `r_fastpath` and `r_world_vbo`. Each reported value below averages four matched temporal positions from opposite orderings.

| Path | Enabled | Comparison | Mean difference | Result |
| --- | ---: | ---: | ---: | --- |
| `r_fastpath` frame | `2.220 ms` | `2.176 ms` with `r_fastpath 0` | `+0.044 ms` | Mixed. The four paired deltas ranged from `-0.533` to `+0.482 ms`, so no aggregate gain is claimed. |
| `r_fastpath` Studio | `0.403 ms` | `0.407 ms` with `r_fastpath 0` | `-0.005 ms` | Essentially neutral in this paired capture. |
| `r_world_vbo` world phase | `0.374 ms` | `0.321 ms` with `r_world_vbo 0` | `+0.054 ms` | No positive result in this workload. The paired samples vary enough that this is not treated as a universal regression. |

These paired results are deliberately kept even when they are neutral or negative. The purpose of the benchmark data is to show what was actually measured, not to make every enabled path look faster.

### Experimental paths

| Path | Coverage and result | Current conclusion |
| --- | --- | --- |
| `r_tracer_vbo 1` | Current screen captured `83,497` tracers, `333,988` vertices, `600` uploads, and `0` fallbacks. A dedicated earlier test measured renderer time around `0.566 -> 1.148 ms`. A later implementation pass reduced the ON sample to about `0.637 ms`. | Improved substantially, but no repeated positive closure. Remains off. |
| `r_sprite_vbo 1` | Current screen captured `11,430` sprites with `0` fallbacks. A dedicated controlled visual A/B was pixel-identical, but renderer time changed from about `1.472 -> 1.663 ms`. | Remains off because the targeted performance result was negative. |
| `r_studio_indexbuffer 2` | Validation counters were active with no reported validation mismatch. The current separate-scene screen looked faster, but it was not completed as a balanced paired A/B. | Promising, not certified. Remains off. |
| `r_studio_gpu 1` | GPU path counters show real usage. The current screen reduced Studio time from the baseline median `0.857` to `0.748 ms`. | Promising, needs repeated A/B and compatibility closure. Remains off. |
| `r_studio_gpu 2` | GPU validation path was active with no reported validation mismatch. Studio measured `0.788 ms` in the screen. | Promising validation result, not a final performance certification. |
| `r_studio_nonplayer 1` | `19,152` non-player attempts and `2,619` deferred draws were recorded. Studio measured `0.759 ms` in the screen. | Real coverage, promising result, broader compatibility validation still required. |
| `r_studio_instancing 1` | `instDraw=0`, `instEnt=0`, `instSaved=0`. | No benchmark coverage. No performance claim. |
| `r_beam_vbo` | The `zetest` measurement window had no beam captures. | Requires a dedicated beam workload for a current per-feature number. |

### Studio SaveBones validation

Mode `2` is a validation mode, not the fastest mode. Development logs accumulated more than nine million comparisons with `mismatch=0`. A representative late sample measured stock at `0.637 us` per call and the validation plus fast segment at `1.364 us` per call. The extra work in mode `2` is intentional. Mode `1` is the performance path.

### Texture optimization evidence

Texture CVARs primarily change load-time work, GPU storage, and cache reuse, so a steady-state frame-time A/B is not the right measurement.

One representative load recorded `14,634` Studio texture uploads, `5,076` compressed textures, and `545` forced mip chains. The first persistent-cache pass recorded `1,055` hits and `4,021` misses. The next matching load recorded `5,076` hits and `0` misses. No PBO or unpack-layout rejects were reported in that validated pass.

`r_studio_texcompress_min` is a scope parameter rather than an independent optimization and therefore does not have a standalone FPS result.

### Reading the full per-CVAR data

[`benchmarks/2026-09-15-screening.csv`](benchmarks/2026-09-15-screening.csv) contains the current-build screen for the runtime paths. [`benchmarks/2026-09-15-paired.csv`](benchmarks/2026-09-15-paired.csv) contains the balanced paired samples that completed. [`benchmarks/2026-09-15-targeted.csv`](benchmarks/2026-09-15-targeted.csv) contains validation, texture-load, tracer, sprite, and pose-cache evidence. The benchmark notes explain coverage and known limitations.

For future performance work, use the same scene, keep GoldClient in foreground, compare one CVAR at a time, and prefer phase timing plus feature counters over a single FPS number.

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
