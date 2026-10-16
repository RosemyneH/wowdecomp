# Frame log lifetime

From the [log leak review](93bb34ff-c2a6-4462-ad07-edacdf5a7e00). The review's write did not reach the tree; this file records it.

`WOW_PERFLOG=1` allocates nothing after startup. Counters are BSS atomics. The one-second line is one fixed buffer filled from the world update, not a formatted string on the draw path.

## Do not use these for the hot line

- `ConsoleWrite` allocates the line under a lock (`WoW/Source/Console/ConsoleClient.cpp:873`, `SMemAlloc` at `:913`).
- A second format next to `DebugLog` in `CM2SceneRender.cpp` would allocate per batch.
- Do not hold `flockfile`, `ConsoleWrite`'s critsec, or any log mutex across `Job::ParallelFor` (`Engine/Source/Base/Job/JobSystem.cpp:158`) or from `IBufLock` through `CGxDeviceVulkan::Draw` (`CGxDeviceVulkan.cpp:1183`).

## Already resident, not the log's problem

`worlds`, `s_terrainBatch`, `s_visQueue`, `s_doodadCulls`, and `s_ownedFills` keep peak capacity. The log must not add another growable buffer on those paths.
