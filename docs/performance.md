# Performance

The decompiled client did most of a frame on one thread, and it rebuilt meshes it could have kept. This fork adds a job pool, keeps the meshes and the terrain plans that stay valid, and skins many models on the GPU. The [Graphics](graphics.md) page covers the GPU side. This page covers the CPU side and how to see the cost.

## Job pool

`Engine/Source/Base/Job/JobSystem.h` is a shared pool. The main thread starts the work and is the thread that waits for it. A job must not call back into the pool, and it must not touch the graphics device.

Two entry points:

| Call | When it returns |
|---|---|
| `Job::ParallelFor` | After every range has finished. The main thread runs a range too |
| `Job::Start` then `Job::Wait` | `Start` returns immediately. The main thread can draw, then `Wait` joins. A later `ParallelFor` also joins anything still running |

What runs there today:

| Work | Call |
|---|---|
| Filling terrain chunk vertices | `ParallelFor` while a chunk is being built |
| Culling doodads | `ParallelFor` over the visible set |
| Animating M2 roots | `ParallelFor` |
| Skinning models the GPU path did not take | `Start` at the beginning of the model scene, so workers skin while other scene work proceeds. `Wait` happens before those meshes are drawn |
| Filling detail-doodad (grass) vertices | `ParallelFor` |
| Multi-bone doodads that missed the GPU palette | `ParallelFor` inside the CPU fallback |

`Job.Join` in the profiler is the time the main thread spent blocked in those waits.

## What is reused

These are the caches that make a standing camera cheaper than a moving one:

- The terrain visibility set is kept while the camera stays put, and dropped on the next moving frame. The occlusion horizon is copied forward for that same still view, instead of projecting every chunk edge again.
- Layer groups are kept while the same chunks stay visible.
- Bindless terrain slots stay allocated. A new scene does not rewrite every terrain texture.
- Rigid doodad meshes and GPU bind-pose meshes stay in their buffers across frames. A bone change updates the palette, not the vertex buffer.
- Terrain texture-matrix generation is resolved once per flush.

Moving the camera, or changing which chunks are in range, rebuilds the visibility set and can rebuild grass. Grass distance is capped at 220 yards for that reason. See [World](world.md).

## Profiler

Seven zones are recorded for the world frame:

| Zone | What it wraps |
|---|---|
| `World.Render` | The world render entry |
| `Scene.Render` | The scene render |
| `Terrain.Flush` | Submitting terrain draws |
| `Vis.Flush` | Chunk visibility |
| `M2.Draw` | The model scene |
| `Gx.Draw` | The Vulkan draw call path |
| `Job.Join` | Main-thread waits on the pool |

`GetProfilerZones()` returns the mean frame time over the last 30 frames, then one record per zone: name, self time, total time, and call count. Self time is the zone minus the zones that opened inside it.

The `WowdecompProfile` addon (`Addons/WowdecompProfile`) hooks the latency bar. Hover that bar and the tooltip lists the frame and each zone that ran, slowest self time first.

`dev-station.sh` turns the sampler on for both `launch` and `release` (`WOWDECOMP_PROFILE=off` disables it). The zone memory is a Linux shared segment at `/dev/shm/wowdecomp-profile`; the `wowdecomp-profile` MCP server in `Tools/profile-mcp` reads it. For a long-running release benchmark loop, use `./Tools/dev-station.sh release-bench` (profiler on, `WOWDECOMP_AUTOLOGIN` default `a:a:Even`, samples via `Tools/bench-workspace.sh`).

`scope` and `build` no longer wipe `WTF/Account` unless you set `WOWDECOMP_RESET_WTF=1`. Server characters live in the AzerothCore database volume; `docker compose down -v` in `acore-docker` is what deletes them, not the client launch.
