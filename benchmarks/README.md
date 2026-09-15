# Benchmark notes

These files preserve the measurements summarized in the project README.

## Test machine

CPU: AMD Ryzen 5 5600G

GPU: NVIDIA GeForce GTX 1060 3 GB

GPU driver: 32.0.15.8253

OS: Windows 11 Pro build 26200

GoldClient renderer target: `hw.dll` timestamp `0x6A49A361`, image size `0x03CA7000`

GCRender binary SHA256: `4E377F7EB27D9E60EE625B0F75065A760DDFF5C08BABB75F3EF2D54A6102177F`

Test resolution: 1280x720 windowed

VSync: disabled

`fps_override`: `1`

`fps_max`: `1000`

Primary workload: `cstrike/zetest.dem`

## Files

`2026-09-15-screening.csv` contains one current-build screen for each runtime path that could be exercised without a dedicated map. The five baseline samples had a frame-time median of `3.056 ms`, a renderer median of `2.112 ms`, a Studio median of `0.857 ms`, and a world median of `0.377 ms`.

`2026-09-15-paired.csv` contains the balanced current-build A/B data that completed before the longer matrix was stopped. Each row matches the same temporal position from opposite A/B orderings. This is stronger evidence than the single-run screening data, but four matched positions are still not enough to call small differences definitive.

`2026-09-15-targeted.csv` preserves direct validation counters, texture-load evidence, and the dedicated tracer and sprite measurements used by the README.

GoldClient throttles the client to roughly 30 FPS when its window is not foreground. Any run affected by that throttle was discarded. This is why the raw profiler phase timings are preferred over frame time for small renderer changes.

## Interpreting screening data

The screening CSV is directional evidence, not a controlled per-feature benchmark. Different launches can reach different demo workload at the measured window. Large changes are only treated as meaningful when the feature counters confirm real coverage or when an independent targeted test agrees with the direction.

Examples of false attribution are intentionally preserved. `r_studio_instancing 1` produced a much lower frame time in its screening run, but `instDraw=0`, `instEnt=0`, and `instSaved=0`. The path did no instanced work, so the apparent gain came from scene variation and is not credited to instancing.

## Targeted evidence from development runs

Studio SaveBones validation accumulated more than nine million comparisons with `mismatch=0`. A representative late sample measured the stock call at `0.637 us` and the validation plus fast segment at `1.364 us`. Mode `2` intentionally executes extra validation work and is therefore not the performance mode.

Studio texture optimization produced a representative load with `14634` uploads, `5076` compressed textures, and `545` forced mip chains. The first persistent-cache pass recorded `1055` hits and `4021` misses. The next matching load recorded `5076` hits and `0` misses.

The dedicated tracer workload previously recorded `23273` captures, `93092` vertices, `311` uploads, and `0` fallbacks. The first tracer implementation increased renderer time from about `0.566 ms` to `1.148 ms`. A later pass reduced the ON result to about `0.637 ms`, but the full repeated visual and performance closure was not completed. The path therefore remains disabled by default.

The dedicated sprite test was pixel-identical in its controlled visual A/B, but renderer time changed from about `1.472 ms` with the path off to `1.663 ms` with it on. That targeted result is why the sprite VBO remains disabled even though a later non-paired screening run happened to look faster.
