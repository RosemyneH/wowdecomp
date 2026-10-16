# Campaign 6 — Vulkan loop

**NEXT SLICE**

Cycle 1. The slice budget is stream locks do not call `operator new`. That is still open in `IBufLock`. The campaign stays open with it. No frame log is in tree, so this is a code reading, not a before/after sample.

The 64 MiB stream ring is already in place. Do not reopen it.

## This slice

| Check | Result | Code |
| --- | --- | --- |
| Stream locks do not call `operator new` | Open | `new VkBufferAlloc` is still on both stream branches |
| 64 MiB ring, no fresh pool on wrap | Already done | Leave `StreamRingSize` and the wrap path alone |

`CGxDeviceVulkan::IBufLock` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1589`) takes the stream branch at `1596`. `VkContext::StreamLock` (`VkContext.cpp:1086-1089`) bumps `StreamRingSize`.

When the slice fits, `BumpChunk` sets `borrowed = 1` (`VkContext.cpp:1043-1048`). `IBufLock` then heap-allocates a wrapper the first time that pool has no view, and again after `IReleasePool` has deleted it:

- `CGxDeviceVulkan.cpp:1618-1620` — `alloc = new VkBufferAlloc()`
- `CGxDeviceVulkan.cpp:1623-1625` — the ring slice is copied in, `memory` is cleared, `borrowed` stays 1, so a later destroy cannot `vkFree` the ring

A later borrowed lock of the same pool overwrites that wrapper and does not call `new`. The first lock, and every lock after a release, still does.

When `bytes > chunkSize`, `BumpChunk` calls `CreateBuffer` and returns with `borrowed` still 0 (`VkContext.cpp:1023-1028`). `CreateBuffer` leaves `borrowed` at 0 (`VkContext.cpp:983`). That is the oversized path. Every such lock deletes the previous wrapper and allocates another:

- `CGxDeviceVulkan.cpp:1627-1631` — `delete alloc`
- `CGxDeviceVulkan.cpp:1634-1635` — `alloc = new VkBufferAlloc(slice)`

`IReleasePool` (`CGxDeviceVulkan.cpp:1556-1565`) `delete`s whatever `m_apiSpecific` points at, including a stream view. Draw still reads that pointer as a `VkBufferAlloc` (`CGxDeviceVulkan.cpp:1256-1263`).

`EmergencyMem::Lock` (`Engine/Source/Gx/CGxDevice/EmergencyMem.cpp:15-23`) is the failure fallback. It grows a count, not a `VkBufferAlloc`. It is not this slice.

## Do not reopen

`StreamRingSize` is `64 << 20` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:135`). `StreamLock` passes that size into `BumpChunk` (`VkContext.cpp:1089`). The stream branch does not release the pool on wrap (`CGxDeviceVulkan.cpp:1597`). A full chunk advances `chunkIndex` and may `CreateBuffer` another 64 MiB chunk (`VkContext.cpp:1031-1037`). That second chunk is a later `vkAllocateMemory`, not the old 2.75 MiB wrap. The next writer does not change `StreamRingSize`, does not release the pool on wrap, and does not pull chunk growth into this slice.

## Later slices

These stay later. The next writer does not start them.

**Flat `vkAllocateMemory`.** Buffer memory is still one host-visible allocation per `CreateBuffer`. The type is `HOST_VISIBLE | HOST_COHERENT` and the call is `vkAllocateMemory` (`VkContext.cpp:1001-1003`). Callers that still hit that while the outdoor scene is live: a spare-buffer miss (`AcquireBuffer` at `970-979`), a new stream or staging chunk (`BumpChunk` at `1034`), and an oversized lock (`1024`). Frame rings are separate allocations at device setup (`290-298`), not a per-frame flat line. Images already take 64 MiB `VkHeap` blocks (`VkHeap::Alloc` at `VkContext.cpp:51`, device-local in `CreateImage` at `1112-1114`). The host suballocator is the slice after this one.

**Device-local static pools.** `GxPoolUsage_Static` does not take the stream branch. `IBufLock` calls `IPoolBuffer` (`CGxDeviceVulkan.cpp:1643-1649`) and returns a mapped host pointer. `IPoolBuffer` still `new VkBufferAlloc` once per pool (`1544-1547`) and `AcquireBuffer` lands in the same host-visible `CreateBuffer`. Nothing uploads a static pool into device-local memory and retires the staging copy. That slice is after the host suballocator.

`VkHeap::Block::free` is still a `std::map` (`VkContext.h:63`). The freelist slice stays after device-local static upload.

## Next writer task

**Embed the stream buffer view.** One concern. After this lands, a stream lock does not call `operator new`.

The only `GxPoolUsage_Stream` pools are `m_vertexPool` and `m_indexPool` (`CGxDevice::DeviceCreatePools`, `Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1701-1702`; fields at `CGxDevice.h:145-146`).

1. Add two `VkBufferAlloc` members on `CGxDeviceVulkan`, one per stream pool. `CGxDeviceVulkan` already holds `VkBufferAlloc` (`m_dummyVertices` at `CGxDeviceVulkan.h:99`). Do not add fields to `CGxPool` or `CGxBuf`. Their `/* 00xx */` offsets stay (`CGxPool.h:16-22`, `CGxBuf.h:10-18`). `PoolCreate` allocates `sizeof(CGxPool)` (`CGxDevice.cpp:1677`). Vulkan types stay out of `CGxPool.h`.
2. In the `GxPoolUsage_Stream` branch, point `pool->m_apiSpecific` at that member and assign the `StreamLock` slice into it. Remove `new VkBufferAlloc` at `1619` and `1634`, and the matching `delete`s at `1614` and `1631`. One view per pool, same as today. Do not add a view per `CGxBuf`.
3. Borrowed slice: keep `memory = VK_NULL_HANDLE` and `borrowed = 1` (`1623-1625`). `RetireBuffer` on a borrowed view only zeroes the struct (`VkContext.cpp:1166-1168`). Do not retire a borrowed view.
4. Oversized slice (`borrowed == 0`): retire the previous view only when it is owned, then assign the new slice, including its `memory`, into the same member. `RetireBuffer` (`1165-1183`) still hands an owned buffer to `VkGarbage`.
5. `IReleasePool` (`1563`) must not `delete` either member. Still `RetireBuffer` when `!borrowed`, then clear `m_apiSpecific`. The next stream lock points `m_apiSpecific` at the same member. `IPoolBuffer`'s `new` (`1544`) is the static and dynamic path. Leave it.

Leave `StreamRingSize`, `CreateBuffer`'s `vkAllocateMemory`, the host-visible memory type, the `VkHeap` map, the uniform overflow refusal (`CGxDeviceVulkan.cpp:1381-1383`), and the spare-buffer caps (`VkContext.h:174-175`).

After that lands, the next loop checks the stream branch for zero `new` / `delete` of `VkBufferAlloc`. The writer after that is the host suballocator, so `vkAllocateMemory` stays flat while flying. Device-local static upload is the slice after that.
