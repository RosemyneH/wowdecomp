# Multithreading audit

## Architecture and invariants

`Engine/Source/Base/Job/JobSystem.cpp` owns one static pool and one active batch.
Publication holds `Pool::mu`; workers copy callback metadata under that mutex.
An atomic index dynamically assigns contiguous ranges. Both `Start` and
`ParallelFor` publish at least sixteen ranges per participant. Batches large
enough that eight items per range is finer than that use the finer split, up to
1024 ranges. `ParallelFor`
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

## Poll progress count

`SchedulerThreadProcProcess` calls `SynthesizePoll` for whichever context that scheduler thread is running (`EvtSched.cpp:631`). `AsyncFileReadPollHandler` is the global `EVENT_ID_POLL` handler, and map loading calls it directly too. Two of those calls can overlap. `s_progressCount` was a plain integer decremented after the queue lock was released.

The handler now takes a recursive `s_pollLock` for the whole call, including the callback. A callback on that same thread may poll again. A second thread waits. The counter stays a plain integer because only one thread is inside the handler. The queue lock is still dropped before the callback so an I/O worker can post. The texture staging counter is still touched from draw code outside this lock.

## Post list and free list

`AsyncFileReadPollHandler` and the I/O worker already edit `s_asyncFileReadPostList` under `g_queueLock`. `AsyncFileReadDestroyObject`, `AsyncFile::Cancel`, and `AsyncFileReadAllocObject` edit the free list under that lock. `AllocObject` initializes the object's fields after releasing the lock, once the node is unlinked and no other allocator can see it.

`AsyncFileReadDestroy` joins every worker before `AsyncFileReadUnloadPostList` and the free-list `Clear`. The drain now unlinks and copies the unload callback under the lock, then calls it after releasing the lock, matching the poll handler. The free-list `Clear` stays after that join.

## File queue list edits

Every `m_readList` write in the tree is in `AsyncFile.cpp`. `NetQueuePromoteLocalFiles`, `AsyncFileReadWait`, `AsyncFileReadObject`, and the I/O worker already hold `g_queueLock` around those writes. `AsyncFileReadIsReading` only reads the list, under the same lock. `AsyncFileReadLinkObject` takes the lock itself.

`AsyncFileReadLinkStaleObject` is called from the worker (`AsyncFile.cpp`, frame-limited head) and from `AsyncFileReadObject`, both of which already hold the lock. It now takes the lock too, and it leaves a null `m_queue` alone. The stale list is the frame-limited side queue. No other translation unit writes `m_readList` or `m_staleList`.

## Texture priority list

`s_asyncLoadList` is not the file queue. `AsyncTextureHandler` and `AsyncTextureStatus` walk it from the poll handler, which runs on the caller. `TextureIncreasePriority` is reached from `TextureGetGxTex`, `CM2Model::LinkToAsyncHeadRecursive` (`M2Model.cpp:1277`, map and world-object promotion), and liquid blob warmup. Those are caller-thread draws and prepares. The queue lock stays on the path that bumps a read already in the file queue. The null-buffer link does not take that lock.

A finished texture clears `m_asyncObject` in `AsyncTextureSuccessCallback`. `LinkToAsyncHead(1)` still visits those textures. The priority function now returns when the object is already gone, instead of reading a null pointer in streaming mode.

`CM2Model::LinkToAsyncHeadRecursive` also calls `AsyncFileReadLinkObject` with no lock held (`M2Model.cpp:1271`). The I/O worker unlinks that same read list under `g_queueLock`. `AsyncFileReadLinkObject` now takes the lock itself and reads `m_queue` under it, so a read that has already been posted is not pulled off the post list. Callers that already hold the lock re-enter it. Linux `SCritSect` is a recursive pthread mutex, and the Windows critical section is recursive too.

## Blocking texture wait

`TextureGetGxTex` with `wait == 1` calls `AsyncTextureWait` (`Texture.cpp`). Terrain draws, fonts, shadows, and render-target updates use that texture in the same call. A deferred object has a null buffer, so the wait submits it immediately and blocks. Refusing that submit would leave those callers without a GPU texture. The staging counter can therefore pass 4 MiB.

`0x400000 - s_asyncLoadBufferUsed` used to wrap once the counter passed the cap. `AsyncTextureHandler` and `CreateBlpAsync` then treated the wrap as free space and admitted more reads. `TextureStagingRemaining` returns zero in that case. The urgent submit stays. `texture_staging_test` covers a counter at the cap and past it.

## Wait-all and the texture budget

`AsyncFileReadWaitAll` is the non-streaming load barrier. `CMap::PrepareUpdate` and `CMap::Preload` call it between prepare phases (`Map.cpp:2329`, `Map.cpp:2549`). `CGGameUI` calls it after cinematic and teleport preloads when the client is not streaming (`GameUI.cpp:5593`). The only poll handler is `AsyncTextureHandler`, which submits deferred textures that fit in the 4 MiB staging budget.

The old loop polled only while a file read was already visible. A deferred texture that fit the budget, with an empty file queue, never started. The loop now polls once up front. Textures that fit become file reads and are joined. Textures that still do not fit stay on `s_asyncTextureList`, because the wait stops when no file read is visible. That is the budget contract. Forcing those objects through would wrap or ignore the 4 MiB cap.

## Area purge and wait reentry

`CMap::PreUpdateAreas` (`Map.cpp:2027`) purges an out-of-range area when its async object is missing or no longer current. A finished read clears `m_isCurrent` before the post callback (`AsyncFile.cpp:464`). `CMapArea::Purge` then `Cancel`s. A current read returns 0, nulls `m_fileBuffer`, and frees that buffer from `FreeAsyncObject`. Any other state returns 1, unlinks the object, and the area destructor frees `m_fileBuffer` (`MapArea.cpp:606`). Map update and the poll handler share the caller thread, so purge does not run in the gap between the poll's unlock and its callback. The poll copies `m_userPostloadCallback` and `m_userArg` before that unlock (`AsyncFile.cpp`). A later `Cancel` can recycle the object without changing the callback this dequeue already took. The argument's lifetime is still the caller's: a second thread that cancels and frees that argument before the call returns is not closed here.

`AsyncFileReadWait` increments `s_waiting` and, if `m_isProcessed` is already set, used to return without clearing it. The next wait then hits the fatal reentry guard. `CM2Model::WaitForLoad` (`M2Model.cpp:731`) calls wait twice while `m_asyncObject` can still be set. The processed return now decrements the guard. A nested wait from inside a callback still trips that guard on purpose.

## Async destroy flag

`AsyncFileReadDestroyObject` (`AsyncFile.cpp`) used to drop `g_queueLock` and spin on plain `m_isCurrent`. The I/O worker stores that byte only while holding the same lock (`AsyncFile.cpp:464`). Those accesses were a data race if destruction observed an in-flight read. The wait now re-acquires the lock for every check. `Cancel` still avoids this wait for a current object by deferring cleanup to the post callback. No TSan run of the async file workers was taken.

## Deferred texture staging

`CreateBlpAsync` (`Texture.cpp`) links an async object onto the deferred list when the file does not fit the 4 MiB staging budget. That path leaves `m_buffer` null and does not add `m_size` to `s_asyncLoadBufferUsed`. `AsyncTextureProcess` is the only store of `m_buffer`, and it is the add. `CTexture::~CTexture` used to call `AsyncFile::Cancel` and then subtract `m_size` after the object had been recycled, including when no buffer had been reserved. An unsigned subtract from a smaller used count wraps the budget and later admits more reads.

`TextureStagingUsed` changes the counter only when `m_buffer` is non-null. The destructor copies the buffer and size before `Cancel`. A failed `SMemAlloc` no longer reserves staging either, so a null buffer stays a zero change on both acquire and release. `multithreading-audit/texture_staging_test.cpp` checks the null-buffer release and a matched acquire/release. This is not a live texture-load run.

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
further validation. No graphics calls were moved onto workers. A batch publishes
at least sixteen ranges per participant, and about eight items per range when
that is finer, capped at 1024 ranges. Wakeup, completion, and single-batch
publication are unchanged.

Validation: Linux Release targets rebuilt and all 15 job/emergency-memory CTests
passed three repetitions each, including caller and worker terminate-handler code
42 and no-pool exception propagation. No native other-platform verification yet.

## Dispatch ownership inventory (current source inspection)

| Location | Inputs and writes | Barrier and remaining validation |
| --- | --- | --- |
| `World.cpp:4198`, `CullDoodadRange` | Stable vector of definitions; each index writes its own result. `QueryBuffer` (`CWorldOcclusion.cpp:487`) reads `s_clipBuffer` and the clip matrix. `FrustumCull` (`World.cpp:2174`) reads `CFrustum::planes` (`CFrustum.cpp:68`). | `CullSortTable` (`World.cpp:4469`) runs each chunk cull to completion before `UpdateOcclusion` (`World.cpp:2772`) paints the clip buffer. Flag, list, and `nDoodadsRendered` writes stay after `ParallelFor` returns. Interior doodads stay on the caller. |
| `M2Scene.cpp:2110`, `AnimateModelRange` | Stack batch points to root vector and scene view; roots exclude attached models. `AnimateMT` / `AnimateMTSimple` write that model's bones, tracks, lights, and texture matrices, then recurse through `AnimateAttachmentsMT` on the same worker. Shared `M2Data` tracks are read through const `FindKey` (`M2Model.cpp:2966`); key indices live on the model. | `ParallelFor` returns before `AnimateST` (`M2Model.cpp:3120`) links draw/particle lists, simulates ribbons, and updates cameras. `AdvanceTime` (`M2Scene.cpp:58`) drains `s_sequenceCallbacks` on the caller before that dispatch. Emitter simulation stays in `AnimateParticleST`, after the join. |
| `M2Scene.cpp:1985`, `SkinRange` | Global vector of raw model/section pointers; each writes a separate aligned byte interval prepared before Start. Duplicate model/section/texcoord keys are skipped. | `M2SkinPrepare` joins, resizes, then `Start`s. The world frame starts that batch in `CWorldSceneRender.cpp:193` and joins it in `CGWorldFrame::OnWorldRender` via `Draw` (`WorldFrame.cpp:1810`, `M2Scene.cpp:2604`) before the frame function returns. The empty-viewport return is before `CWorld::Render`. Bone writes from the next `Animate` wait for this batch because `ParallelFor` joins first. Live teardown during the terrain overlap is still untraced. |
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

A batch wakes at most four runners through 16 items, then one more runner per four
items, and never more runners than ranges or the pool. It waits only for the
workers it released.
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

`ChunkCount` publishes at least `16 * participants` ranges. When `count / 8`
is larger, it uses that many ranges instead, capped at 1024. The cap exists so a
`uint32_t` count cannot publish hundreds of millions of ranges. The 4096-item
measurement that chose eight items per range is in the benchmark record.

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
Windows or MSVC verification. The isolated Windows exception executable's pooled
caller and worker cases returned terminate code 42 under Wine; the synchronous
fallback returned 0. Those results are recorded again with the final noexcept
check below.

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
