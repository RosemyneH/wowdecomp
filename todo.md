# Multithreading audit queue

Branch: `mt`; preserve unrelated working-tree changes. Historical investigation is
in `multithreading-audit/{todo,audit,benchmarks}.html` and `nested-dispatch.html`.
Current decisions/evidence: docs/multithreading-audit.md and
docs/multithreading-benchmarks.md. Hypotheses are not confirmed production defects.

## Active

`sideload.cpp` compiles. The full WowClient link and a live frame run are still
open. Hardware targets stay in the blocked list.

## Completed iterations

- [x] Alias `TYPEMASK_PLAYER` to `TYPE_PLAYER`. The sibling sideload calls
  `ClntObjMgrObjectPtr` with that name, and the client already passes
  `TYPE_PLAYER` at the same call. `sideload.cpp` compiles with the WowClient
  flags. The executable was not linked or run.
- [x] Trace lighting and draw callbacks. Lighting runs from `SetupLighting`
  after `ParallelFor` returns. Draw runs from `CM2SceneRender::DrawCallback`
  during scene draw. Neither is a job worker or an async poll. No lock added.
- [x] Hold the poll lock across `OnSequenceInterrupted` and `SetSequenceCallback`.
  A low-priority sequence load invokes the sequence callback on a scheduler
  thread. Event callbacks still run from `AdvanceTime` on the caller.
  `M2Model.cpp` typechecks with the Linux client flags.
- [x] Clear `m_loadedCallback` inside `CM2Model::Release` while holding the poll
  lock. `UpdateLoaded` can invoke that callback from skin completion on a
  scheduler thread. Units, effects, particles, and simple models all release
  through this function. `M2Model.cpp` typechecks with the Linux client flags.
- [x] Take the poll lock in `CDetailDoodad::DestroyModels` and clear the model
  loaded callback before release. `UpdateLoaded` can run that callback from
  skin-profile completion on a scheduler thread. `DetailDoodad.cpp` typechecks
  with the Linux client flags.
- [x] Take the poll lock in `CMapObj::Clear` and `CMapObjGroup::Clear`. Their
  post callbacks call `Load` and `Create` on a scheduler thread. Teardown now
  waits for that call before releasing the file buffer. `MapObj.cpp` and
  `MapObjGroup.cpp` typecheck with the Linux client flags.
- [x] Take the poll lock in `~CM2Shared` and `~CM2SequenceLoad`. Skin, model,
  and low-priority sequence callbacks use those objects on a scheduler thread.
  `M2Shared.cpp` typechecks with the Linux client flags.
- [x] Take the poll lock around an out-of-range area purge. The load callback
  runs on a scheduler thread and still uses the area. `m_isCurrent` is read
  under the queue lock. `CMapArea::Purge` takes the poll lock before `Cancel`.
  `Map.cpp` and `MapArea.cpp` typecheck with the Linux client flags.
- [x] Take the poll lock around texture staging and the deferred lists. Draw
  code and a scheduler-thread poll callback were both updating
  `s_asyncLoadBufferUsed`. The lock is acquired before the queue lock.
  `AsyncTextureWait` drops it before the file wait. `Texture.cpp` and
  `AsyncFile.cpp` typecheck with the Linux client flags.
- [x] Serialize `AsyncFileReadPollHandler` with a recursive section. Every
  scheduler thread synthesizes `EVENT_ID_POLL`, and map load calls the handler
  directly. `s_progressCount` stays a plain integer because those calls no
  longer overlap. A callback on the same thread can poll again. `AsyncFile.cpp`
  typechecks with the Linux client flags.
- [x] Unlink and copy the shutdown post callback under `g_queueLock`.
  `AsyncFileReadDestroy` joins the workers before that drain. Free-list
  alloc, cancel, and destroy already hold the lock. The free-list clear stays
  after the join. `AsyncFile.cpp` typechecks with the Linux client flags.
- [x] Take `g_queueLock` inside `AsyncFileReadLinkStaleObject`. Its two callers,
  the I/O worker and `AsyncFileReadObject`, already held the lock. The other
  `m_readList` writes already sit under that lock, and no other file writes
  the list. `AsyncFile.cpp` typechecks with the Linux client flags.
- [x] Return from `TextureIncreasePriority` when the texture has no async object,
  and take `g_queueLock` inside `AsyncFileReadLinkObject`. Loaded textures clear
  the async pointer, and model promotion relinked the file queue without the
  lock the worker holds. `Texture.cpp` and `AsyncFile.cpp` typecheck with the
  Linux client flags.
- [x] Keep a staging counter past 4 MiB from looking like free budget. Blocking
  `TextureGetGxTex` waits still submit a deferred texture, because draw callers
  use the result immediately. `TextureStagingRemaining` returns zero once the
  counter reaches the cap. `texture_staging_test` covers the cap and a counter
  past it. `Texture.cpp` typechecks with the Linux client flags.
- [x] Poll once at the start of `AsyncFileReadWaitAll`. Non-streaming map and
  game-UI load barriers were returning while deferred textures that fit the
  budget were still queued. Objects that still do not fit stay deferred.
  `AsyncFile.cpp` typechecks with the Linux client flags. No live load.
- [x] Copy the post callback and its argument under the queue lock before the
  poll invokes them. `Cancel` can recycle the object after the unlock. The
  copied argument is still the caller's lifetime. `AsyncFile.cpp` typechecks
  with the Linux client flags. No async-worker run.
- [x] Clear `s_waiting` when `AsyncFileReadWait` finds the object already
  processed. `WaitForLoad` calls it twice while the shared pointer is still
  set, and the old return left the fatal reentry guard stuck. A wait entered
  from inside a callback still fails that guard. `AsyncFile.cpp` typechecks
  with the Linux client flags. No async-worker run.
- [x] Trace area purge against a posted read. Out-of-range purge cancels when
  `m_isCurrent` is clear. Immediate cancel lets the area destructor free the
  file buffer. An in-flight read nulls that pointer and frees the buffer from
  `FreeAsyncObject`. Purge and poll are on the caller thread. No code change.
- [x] Trace exterior doodad culling. Workers read frustum planes and the clip
  buffer. Each index writes its own result. The clip buffer is painted in
  `UpdateOcclusion` after that bucket's culls return. Model flags and scene
  lists are written after `ParallelFor`. Interior doodads stay serial.
- [x] Take the queue lock around every `m_isCurrent` check in
  `AsyncFileReadDestroyObject`. The worker clears that byte under the same
  lock. The old spin read it after releasing the lock. `Cancel` still defers
  a current object to its post callback. No async-worker TSan run.
- [x] Stop deferred texture destruction from subtracting a staging reservation
  that was never added. `CreateBlpAsync` can queue an object with a null buffer.
  The destructor copied that buffer and size before `Cancel` and now changes
  `s_asyncLoadBufferUsed` only when the buffer was allocated.
  `texture_staging_test` covers the null release and a matched acquire/release.
  Not a live BLP load.
- [x] Trace animation helpers against the parallel roots. `AnimateMT` writes
  per-model state and animates attachments on the same worker.
  `FindKey` does not store into shared tracks. Sequence callbacks run in
  `AdvanceTime` before `ParallelFor`. Ribbon, particle-list, and lighting
  updates run in `AnimateST` after the join. World skin starts after `Animate`
  returns and `CM2Scene::Draw` joins it before the world frame returns.
  No new defect from this static pass. Live attachment trees and teardown
  remain blocked.
- [x] Spread large batches to about eight items per range. Two in-process runs
  moved 4096-item skewed medians from about 0.33 ms at 176 ranges to about
  0.24 ms at 512, with no overlap. Cheap 4096 stayed under its serial time.
  1024 ranges and one range per item were slower again. Lifecycle and TSan passed.
- [x] Cap runners at `max(4, items/4)`. Back-to-back 8- and 16-item ParallelFor
  intervals dropped with no overlap on uniform, heavy, and one-hot bodies.
  64-, 256-, and 4096-item intervals still overlap, so large batches are
  unchanged. Lifecycle and TSan passed, including two callbacks in flight.
- [x] Raise the exterior doodad parallel gate from 32 to 4096. In-process
  16-step work is faster serial through 2048 items (non-overlapping) and faster
  parallel at 4096. 200-step work crosses over near 128 and 2000-step work near
  16, so animation and skinning keep small parallel batches. Strided ranges were
  slower than contiguous 16× on uniform and skewed 4096 and were not adopted.
- [x] Reject 32× worker ranges. One run put the 4096-item skewed median at 0.256 ms,
  but cheap 512- and 4096-item bands moved up by about 0.004 ms with no overlap
  against that run's 8× samples. Sixteen remains the cap.
- [x] Raise the claim factor from 4 to 16 after two in-process runs showed the
  4096-item skewed median fall from about 1.25 ms to about 0.35 ms without moving
  cheap or uniform medians. `ParallelFor` no longer wakes a worker for a range it
  is about to run; one-item medians fell from about 0.006 ms to about 0.0003 ms
  with non-overlapping ranges. Lifecycle, TSan, init-failure injection and the
  caller-terminate path passed.
- [x] Wake only `min(ranges, workers)` workers per batch so a one-item dispatch
  does not disturb the rest of the pool. Lifecycle, init-failure injection,
  TSan, and ASan passed. Back-to-back one-item medians dropped from about
  0.042 ms to about 0.006 ms with non-overlapping ranges. Large skewed batches
  stayed in the same band.
- [x] Publish four ranges per participant so one heavy item is not sealed into
  the same claim as its neighbors. Coverage, terminate, and TSan lifecycle checks
  passed. Within-run 10-worker synthetic medians are recorded; no frame-time claim.
- [x] Fix forbidden pooled caller exceptions leaving a permanently incomplete batch.
  Fail fast on both worker and participating caller via noexcept; preserve no-pool
  exception propagation. Initial explicit catch revised after timing investigation.
- [x] Inventory every Job dispatch/wait/init/shutdown call; document ownership and
  barriers for animation, asynchronous skinning, doodad culling and mapped skinning.
  Transitive runtime ownership validation remains blocked, not proven race-free.
- [x] Audit scheduler publication, memory visibility, completion and lifecycle;
  document serialized main-thread lifecycle/WorkerCount contract and forbidden
  callback reentry. Concurrent API use is unsupported, not made thread-safe.
- [x] Inspect terrain/grass serialized fills and Vulkan/D3D/OpenGL lock contracts;
  rerun shared EmergencyMem regression. This is not actual terrain/backend coverage.
- [x] Add optional reproducible microbenchmark, randomized chunk/order experiments,
  sizes 0/1/8/4096, uniform/uneven/skewed costs, workers 1/2/4/8/10/16, serial checks.
  Record five 2880-sample matrices, including reordered/noexcept and A/A controls.
- [x] Evaluate completion acknowledgments and multiple-in-flight design: retain
  current protocol because acknowledgments protect batch reuse; no representative
  benefit established for handles or work stealing. Claim size was revisited later.
- [x] Build affected Linux libraries and focused targets; run focused tests,
  sanitizers and available cross-compiler/emulation checks. Record platform limits.
- [x] Inspect existing profiler: global worker-unsafe scope state; Job.Join excludes
  prior caller range work and WorldRender interval is not whole-frame timing.

## Verification evidence

- 2026-10-28 chunk-size change, final sources: one `-O2` lifecycle run and one
  TSan lifecycle run passed with no diagnostics. The same claim formula had
  already passed three `-O2` lifecycle runs and the exit reproducer (0 for
  shutdown and fallback, 42 for terminate paths). Default 10-worker benchmark
  wrote 240 samples to `multithreading-audit/results/2026-10-28-chunks-x4.csv`
  with matching serial output. Not a client or multi-worker-count rerun.
- Linux Release Base, Model2, WorldClient and Gx libraries and optional benchmark
  target build successfully. Final focused suite: 15 tests, three repetitions each,
  all pass. Exit omission tests deliberately reproduce termination (not fixed).
- Final noexcept lifecycle: Linux GCC 16.2.1 ASan/UBSan and TSan pass once each.
  Initial LSan sandbox ptrace failure retried successfully outside sandbox.
- GNU Linux ARM64 lifecycle cross-build and QEMU/sysroot run pass once; emulation
  is not native scheduler validation. MinGW Windows x64 lifecycle cross-build and
  Wine lifecycle pass once. Final noexcept MinGW caller/worker exception tests
  return 42 under Wine, fallback control returns 0. Native Windows/MSVC unverified.
- Benchmarks compare cb54f581 baseline, 5bf61799 explicit catch and revised noexcept.
  Explicit-catch uniform four-worker slowdown triggered revision. Revised GCC
  RunRanges normal-path bytes match baseline. Reordered results vary; identical-
  binary control also diverges dramatically, invalidating causal timing conclusions.
  No speedup claimed and no adaptive scheduler change accepted from these results.

## Blocked tasks / prerequisites

- [ ] Live asynchronous skin input/model lifetimes, teardown reentrancy, animation
  attachment/particle ownership, and actual mapped-buffer backend tests require a
  working integrated client and representative model/map workloads. The previous
  `TYPEMASK_PLAYER` compile error in the sibling sideload is cleared and that
  file compiles. The full WowClient link and a live run are still open. Affected
  engine libraries do build.
- [ ] Actual startup-fatal/active-resource teardown and production exception
  reachability remain unverified. Isolated exit reproductions do not establish safe
  client resource ordering; do not add a late exit join without that evidence.
- [ ] Representative frame CPU utilization, per-worker tasks/durations, scheduling
  overhead, queue/wakeup latency, total frame time/FPS/1% low/P95/P99 require the
  integrated workload above and suitable instrumentation. Existing profiler cannot
  supply those numbers safely from workers. Do not infer them from microbenchmarks.
- [ ] Controlled performance acceptance/tuning requires a quieter/controlled host:
  identical-binary A/A control failed comparability. No causal regression or speedup
  conclusion is justified from current timing spread.
- [ ] Native Windows x64/MSVC, Windows ARM64, Linux ARM64 and Android ARM64 runtime
  verification requires target hardware/toolchains. Host has MinGW and GNU ARM64
  cross-compilers, Wine/QEMU, but no native target runners; Android SDK only has
  platform-tools, no NDK. Cross-build/emulation results are explicitly limited.
- [ ] Historical AsyncFile/texture and lazy model-table race hypotheses need
  controlled real-resource reproductions and ownership evidence before fixes.

## Git

Created `mt` from cb54f581, preserving main history and unrelated modifications.
Each verified iteration is committed/pushed to origin/mt. No unrelated files staged.

CSV portability follow-up: store raw samples with LF endings and have the runner
write LF explicitly; numeric data unchanged. Diff whitespace check passes.
