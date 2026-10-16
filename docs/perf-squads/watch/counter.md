# Watch squad — Counter

Counters only. No behavior change. One-second console line, same dump as Campaign 0, compile-out flag default off. Timers are `OsGetAsyncTimeClocks` (`Engine/Source/Os/Linux/TimeManager.cpp:59`). On Linux `SystemMethod2` those ticks are `CLOCK_MONOTONIC` nanoseconds; `SystemMethod1` is `CLOCK_MONOTONIC_COARSE` milliseconds. Sum the raw deltas. The Accountant divides by `OsGetAsyncClocksPerSecond`.

Leave `Engine/Source/Storm/H/stpl.h` unchanged in this pass. The trivial-type `realloc` stays a later campaign.

Field names below are frozen.

## Fields

| Field | Unit | Wired this pass |
| --- | --- | --- |
| `parallel_for_wait_clocks` | Sum of `OsGetAsyncTimeClocks` deltas across the barrier | Yes |
| `clip_buffer_updates` | Calls | Yes |
| `tsfixed_grow_allocs` | Grow allocs that copy an existing block | Name only |

Reset each field when the one-second line is printed. Relaxed atomics. Flag off: the two wired sites keep today's control flow, with no clock reads and no extra increments.

Printed line, this pass:

```text
watch parallel_for_wait_clocks=<clocks> clip_buffer_updates=<calls>
```

`tsfixed_grow_allocs` stays off that line until a later pass wires it. A printed zero would read as "no grows."

## `parallel_for_wait_clocks`

Zone is the caller blocked after its own range, through the barrier that waits for every worker.

`Engine/Source/Base/Job/JobSystem.cpp:132` `Job::ParallelFor`

- `JobSystem.cpp:133` returns when `count == 0`. That path does not wait and adds nothing.
- `JobSystem.cpp:156` `RunRanges` is the caller's share of the work. Leave it outside the zone.
- `JobSystem.cpp:158` takes `g_pool.mu`.
- `JobSystem.cpp:160` `cv.wait` returns only when `remaining == 0` and `finished == workerCount` (`JobSystem.cpp:160-162`).

Sample `OsGetAsyncTimeClocks` immediately before the lock at `JobSystem.cpp:158`, and again when `cv.wait` returns. Add the delta. One zone covers every caller; do not split timers per call site. The watch list's small doodad jobs enter here from `Engine/Source/Model2/CM2SceneRender.cpp:795` (`instances >= 8`). The same barrier also serves `MapRenderChunk.cpp:333`, `World.cpp:3966`, `MapChunk.cpp:578`, `WorldFrame.cpp:1590`, `DetailDoodad.cpp:732`, and `M2Scene.cpp:1728`.

## `clip_buffer_updates`

Increment once on entry to `WoW/Source/WorldClient/MapChunk.cpp:1302` `CMapChunk::ClipBufferUpdate`.

Each call rebuilds two 9-vertex outlines:

- `MapChunk.cpp:1310-1315` — indices `i + base`, `base` 0 or 136
- `MapChunk.cpp:1322-1327` — `base` 0 or 8, then `base += 17`

Outline count for the Accountant is `clip_buffer_updates * 2`. Do not increment inside either loop.

Callers, both on the holed-chunk list in `CWorldScene::UpdateOcclusion`:

- `WoW/Source/WorldClient/World.cpp:2616` — hole-free chunk inside the occlusion depth band (unlinked before the second walk)
- `WoW/Source/WorldClient/World.cpp:2628` — chunk that still has holes

`CWorldOccluder::ClipBufferUpdate` (`WoW/Source/WorldClient/CWorldOcclusion.cpp:606`) is a different function. Leave it out of this field.

## `tsfixed_grow_allocs`

Cite only. Do not edit `stpl.h` to increment it.

`Engine/Source/Storm/H/stpl.h:749` `TSFixedArray<T>::ReallocData`

A grow alloc is one fresh allocation that copy-constructs an existing block because the new count is larger than the old capacity:

1. Read `m_alloc` before `stpl.h:756` overwrites it. Call that `oldAlloc`. `olddata` is `stpl.h:750`.
2. `stpl.h:757` always passes `SMEM_FLAG_NOREALLOC`. For a non-null `olddata`, `SMemReAlloc` returns null (`Engine/Source/Storm/Source/SMem.cpp:85-86`).
3. `stpl.h:760` `SMemAlloc` then copy-constructs `olddata` (`stpl.h:762-770`) and `SMemFree`s it.

Count the call only when `olddata` is non-null, `stpl.h:760` runs, and `count > oldAlloc`. That is a capacity grow. A first allocation (`olddata` null) is served inside `SMemReAlloc` (`SMem.cpp:79-80`) and does not take the copy loop. A shrink or a same-size realloc still fresh-allocs under `SMEM_FLAG_NOREALLOC`; those stay out of this field.

`SMem.cpp:85` is not a stand-in counter. It fires for every existing block, and `_msize` is rounded up to 8 bytes, so a byte compare misses real element grows. `ReallocAndClearData` (`stpl.h:732`) passes flag 0 and is a different path.

When a later pass wires the atomic, it still leaves allocation, copy, and free as they are. This pass does not.
