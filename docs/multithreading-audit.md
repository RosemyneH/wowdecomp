# Multithreading audit

## Architecture and invariants

`Engine/Source/Base/Job/JobSystem.cpp` owns one static pool and one active batch.
Publication holds `Pool::mu`; workers copy callback metadata under that mutex.
An atomic index dynamically assigns contiguous ranges. Both `Start` and
`ParallelFor` publish sixteen ranges per participant (`Start` uses the workers;
`ParallelFor` also counts the caller), clamped to the item count. `ParallelFor`
keeps one of those runners for the caller, so a one-item parallel call does not
wake a worker. `Wait` also claims ranges, then waits for both zero remaining
ranges and an acknowledgment
from every worker that was released into the batch. Idle workers are not released.
That acknowledgment protects metadata reuse: a worker still inside the callback
must not observe the next batch's function pointer.

Dispatch/wait are documented main-thread-only; callbacks cannot call Job APIs or
use the graphics device. Lifecycle concurrency is not established as safe. Empty
or null dispatches do not join prior work. Nonempty dispatches join prior work;
shutdown waits, signals termination, and joins all threads. Callback user data and
all referenced inputs/outputs must survive until that join.

## Confirmed caller exception failure

Location: `JobSystem.cpp`, `RunRanges` callback invocation, remaining decrement and
`Join` predicate; reproduction: `multithreading-audit/job_exit_repro.cpp`,
`caller_exception` mode. A callback throwing on the caller formerly skipped the
remaining decrement and escaped, leaving subsequent Wait permanently blocked.
Reachable through a throwing callback in a pooled ParallelFor; callbacks are
contractually forbidden to throw. Production callback throw reachability is not
proven. Worker exceptions already terminate the process.

Decision: make the pooled invocation function noexcept, terminating on
both caller and worker threads. Partial output cannot safely be treated as a
successful batch. No-pool synchronous fallback behavior is preserved. Validate
with isolated caller/worker terminate-handler tests, fallback control and lifecycle
regressions. This is deterministic failure handling, not callback exception recovery.

## Remaining scope

Historical transitive call-site notes and hypotheses are in
`../multithreading-audit/audit.html` and `nested-dispatch.html`. Do not treat their
hypotheses as reachable defects. Four dispatch sites currently remain: World.cpp
(culling), M2Scene.cpp (animation and async skinning), CM2SceneRender.cpp (doodad
skinning). Terrain and grass initialize the pool but currently fill mapped buffers
serially. Client teardown/input reentrancy and renderer staging lifetimes require
further validation. No graphics calls were moved onto workers. Claim size is now
four ranges per participant; wakeup, completion, and single-batch publication
are unchanged.

Validation: Linux Release targets rebuilt and all 15 job/emergency-memory CTests
passed three repetitions each, including caller and worker terminate-handler code
42 and no-pool exception propagation. No native other-platform verification yet.

## Dispatch ownership inventory (current source inspection)

| Location | Inputs and writes | Barrier and remaining validation |
| --- | --- | --- |
| `World.cpp:4198`, `CullDoodadRange` | Stable vector of definitions; each index writes its own result. Sphere QueryBuffer (`CWorldOcclusion.cpp:487`) reads clip-buffer/matrices only; sphere FrustumCull (`World.cpp:2174`) reads frustum state. | Synchronous barrier before flags/list mutation. Scene globals must remain immutable during dispatch; no engine runtime race trace yet. |
| `M2Scene.cpp:2110`, `AnimateModelRange` | Stack batch points to root vector and scene view; roots exclude attached models. AnimateMT/Simple mutate model state and recursively visit children. | Synchronous barrier before AnimateST particles/list/lighting updates. Ownership assumes a proper attachment tree and immutable shared animation data; full helper audit remains open. |
| `M2Scene.cpp:1985`, `SkinRange` | Global vector of raw model/section pointers; each writes a separate aligned byte interval prepared before Start. | Prepare joins before resizing; Finish joins before consumption. Object lifetime and main-thread mutations across the render overlap still need real-scene validation. |
| `CM2SceneRender.cpp:999`, `FillDoodadSkin` | Stack batch, mapped vertex buffer and immutable indices; instance writes at `n * stride`. Cache reads or per-model transforms. | ParallelFor returns before BufUnlock and drawing; fewer than 8 instances run serially. Backend lock/unlock stay on caller. Valid asset counts and no overlapping model mutation required. |

Terrain `MapRenderChunk.cpp:946` locks at most one vertex and one index buffer,
fills, then unlocks. Grass `DetailDoodad.cpp:601,618` locks/fills/unlocks each
buffer serially. Backend state is per target: `CGxDevice.cpp:274` changes a shared
locked-buffer slot and Vulkan `CGxDeviceVulkan.cpp:2739,2804` can use shared
EmergencyMem then consume it at unlock. D3D `CGxDeviceD3d.cpp:136` also falls back
to shared EmergencyMem. Keeping backend calls on the main thread is necessary;
serial fill avoids outstanding same-target staging pointers. The existing
emergency_mem_test exercises aliasing and simulated upload ordering, not these
actual terrain/grass functions or live backends. Do not claim that integration
coverage from its passing result.

## API concurrency and scheduling decisions

Lifecycle and WorkerCount now explicitly share the serialized engine-main-thread
contract already required by dispatch. Concurrent callers can otherwise race on
workerCount, Shutdown reset and active-batch ownership. No in-tree job call found
on an I/O worker; unsupported concurrent API use and nested callback dispatch are
not made supported through partial mutex additions. A callback waiting on its own
batch would deadlock. Future concurrency requires a separate ownership/API design.

A batch wakes `min(ranges, workers)` workers and waits for those workers only.
Idle workers keep the previous callback pointer out of their registers because
their per-worker run flag stays false; a spurious wakeup checks that flag under
the pool mutex and goes back to sleep. The next publish cannot reuse the batch
until every woken worker has returned to its wait. One-item work therefore does
not wake the rest of the pool. Start and the next nonempty ParallelFor still
synchronize unrelated prior work. Wait already does useful range processing.
No multiple-in-flight change is justified
without representative overlap measurements. The only asynchronous in-tree dispatch
is CPU skinning, already overlapped with terrain drawing. Hybrid/NUMA tuning cannot
be inferred from this single-node, symmetric 6-core/12-thread host.

## Chunk granularity

`ChunkCount` publishes `16 * participants` ranges, never more than the item count.
Counts at or below that cap are one range per item. The factor comes from an
in-process Publish/Wait study at 10 workers: 16× worker ranges cut the 4096-item
skewed median from about 1.25 ms to about 0.35 ms on two runs, while cheap and
uniform medians stayed in the same band. It is not an adaptive controller and it
is not a frame-time result. Details are in the benchmark record.

Other threading sources found: Services/AsyncFile queue workers; Storm SThread/SLock
platform wrappers; detached Unix/Darwin URL downloads; BlizzardCore process/thread
wrappers; sound mixer/ALSA threads; Windows HID/crash workers; Android client pthread.
These consume CPU alongside the pool. Their transitive resource ownership is not
proven by the Job tests; prior AsyncFile hypotheses remain in the historical audit.

## Current platform evidence

Linux GCC 16.2.1 lifecycle test: ASan/UBSan (`-O1 -g -fno-omit-frame-pointer`)
passed once outside the sandbox after LeakSanitizer rejected ptrace; TSan passed
once inside the sandbox. Both exercise serialized lifecycle paths only. GNU
Linux ARM64 and MinGW Windows x64 isolated lifecycle binaries cross-compiled;
ARM64 passed once under qemu-aarch64-static using `/usr/aarch64-linux-gnu`.
No native ARM scheduling claim follows from emulation. Linux WowClient build
failed in unrelated auto-detected sibling sideload code, at
`/home/emma/Repos/synastria-sideload/src/sideload.cpp:548` (undeclared TYPEMASK_PLAYER).
No edits made to that integration. Android SDK here contains platform-tools but
no NDK; Windows ARM64/MSVC and native target runtimes remain unverified.

Windows x64 MinGW lifecycle also passed once under Wine with an isolated temporary
prefix after the sandbox rejected wineserver's local socket. Wine is not native
Windows or MSVC verification. The isolated Windows exception executable compiled;
its runtime cases remain to be checked.

The existing Profiler has shared global stack/zone accumulators (`Profiler.cpp`,
Scope constructor/destructor) and is not suitable for worker callbacks. Job.Join
measures the final Join interval, excluding the calling thread's preceding range
execution in Wait. Its WorldRender-rooted frameTicks describe that scoped render
interval, not total presented frame time; deriving client FPS/1% lows from those
records would be misleading. Do not add worker scopes without separate thread-local
or synchronized accounting. Shared-memory profile format is left unchanged.

The initial explicit catch was revised after benchmark regression investigation:
RunRanges is now noexcept, preserving the same fail-fast policy with baseline's
normal-path RunRanges machine code under GCC 16.2.1. Final isolated Linux tests
passed 15 tests three repetitions. Affected Linux Base, Model2, WorldClient and Gx
libraries rebuilt successfully. See the benchmark record for noisy timing controls;
there is no optimization claim. Shutdown omission and direct-exit tests remain
negative controls, not evidence that arbitrary client exits are now safe.

Final noexcept verification repeated lifecycle under Linux ASan/UBSan and TSan,
and GNU ARM64/QEMU, once each, all passed. MinGW pooled caller/worker exception
cases under Wine returned terminate code 42; synchronous fallback returned 0.
These are isolated tests, not live engine or native-platform safety proofs.
