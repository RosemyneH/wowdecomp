# Campaign 6 — Leak

Reviewed the embed of the borrowed stream view against the current retire split. No writer diff. `Draw` keeps reading `CGxPool::m_apiSpecific` as a `VkBufferAlloc*` (`CGxDeviceVulkan.cpp:1256`, `CGxDeviceVulkan.cpp:1455`).

**Verdict: FAIL** if pool release retires that view, or if an owned buffer stops reaching `VkGarbage`.

## One record per pool

`IBufLock` (`CGxDeviceVulkan.cpp:1589`) stores a single `VkBufferAlloc` on the pool. Stream locks `new` one at `CGxDeviceVulkan.cpp:1619` and `CGxDeviceVulkan.cpp:1634`. `IPoolBuffer` `new`s the static and dynamic owner at `CGxDeviceVulkan.cpp:1544`. `IReleasePool` (`CGxDeviceVulkan.cpp:1556-1565`) is the free: `RetireBuffer` when `!borrowed`, then `delete` the wrapper.

The embed removes the per-lock `new` / `delete`. `m_apiSpecific` still points at a `VkBufferAlloc`. That object is the pool's member (or the buf's). `IReleasePool` must not `delete` it. `PoolDestroy` already `delete`s the pool after `IReleasePool` (`CGxDeviceVulkan.cpp:1581-1586`, `CGxDevice.cpp:1684-1688`). `IReleasePools` (`CGxDeviceVulkan.cpp:1568-1571`, from `IDestroyVulkan` at `CGxDeviceVulkan.cpp:378`) releases every pool while it is still on the list. `delete` of an interior `VkBufferAlloc` frees the pool there, and `PoolDestroy` frees it again.

`CGxPool::~CGxPool` is empty (`CGxPool.cpp:10-11`). `BufDestroy` unlinks and `delete`s the `CGxBuf` (`CGxDevice.cpp:1157-1168`) and does not touch the Vulkan buffer. GPU release stays in `IReleasePool`. A destructor on `VkBufferAlloc` that destroys `buffer` / `memory` runs from `delete pool` after `IReleasePool` already queued those handles.

One pool, one record. A second copy in `CGxBuf` that also `RetireBuffer`s frees the same `VkBuffer` the pool frees. Several bufs share the pool pointer today.

## Borrowed view — pool release must not enter VkGarbage

`BumpChunk` copies a frame chunk and sets `borrowed` on the copy (`VkContext.cpp:1045-1046`). The chunk stays in `frame.streamChunks` with `borrowed == 0`. `StreamLock` (`VkContext.cpp:1086-1089`) is one ring for every stream pool, so two pools can hold the same `VkBuffer`.

`IBufLock` on a borrowed slice (`CGxDeviceVulkan.cpp:1611-1625`):

- An owned predecessor is `RetireBuffer`'d (`CGxDeviceVulkan.cpp:1612-1613`).
- The stored view copies the slice, sets `memory = VK_NULL_HANDLE`, and sets `borrowed = 1` (`CGxDeviceVulkan.cpp:1623-1625`).

`IReleasePool` skips `RetireBuffer` when `borrowed` is set (`CGxDeviceVulkan.cpp:1560-1562`). The chunk is destroyed only from the frame: `DestroyDevice` `DestroyBuffer`s each `streamChunks` entry (`VkContext.cpp:655`) and each spare (`VkContext.cpp:662-664`).

`RetireBuffer` (`VkContext.cpp:1165-1178`) returns on `borrowed`. Otherwise it copies `buffer`, `memory`, `mapped`, `size`, and `usage` into a `VkGarbage` (`VkContext.h:35-45`) and `Retire` pushes that onto the frame (`VkContext.cpp:1160-1162`). `VkGarbage` has no borrowed field. `Collect` (`VkContext.cpp:1198-1236`) cannot tell a ring alias from an owner.

A borrowed view that reaches `Collect` is a double free:

- Usage is `VERTEX | INDEX` and `mapped` is set, so the garbage is recyclable (`VkContext.cpp:1213-1214`).
- `StreamRingSize` is 64 MiB (`VkContext.h:135`). `MaxSpareBytes` is 32 MiB (`VkContext.h:175`). The spare loop destroys every parked buffer (`VkContext.cpp:1219-1223`), then refuses the push (`VkContext.cpp:1224`).
- `Collect` `vkDestroyBuffer`s the ring (`VkContext.cpp:1230-1231`). `DestroyDevice` `DestroyBuffer`s the same chunk again (`VkContext.cpp:655`).

Nulling `memory`, which `IBufLock` does at `CGxDeviceVulkan.cpp:1624`, does not stop that. `Collect` destroys `garbage.buffer` when the spare push did not take it (`VkContext.cpp:1229-1236`), including a null `memory`. The `VkDeviceMemory` on the chunk is then freed a second time from `streamChunks`, or leaked if only the view's null handle was queued.

`RetireBuffer` on a borrowed record zeros the object it was passed (`VkContext.cpp:1166-1168`). That object is the pool's copy. `m_apiSpecific` must not point at the `streamChunks` element. The chunk's `borrowed` is 0, so `RetireBuffer` would queue the ring itself.

## Owned buffer — pool release must still RetireBuffer

`BumpChunk` returns a unique buffer when `bytes > chunkSize` (`VkContext.cpp:1023-1028`). `CreateBuffer` leaves `borrowed == 0`. That buffer is not in `streamChunks`. The pool record is the only owner. `IBufLock` stores it at `CGxDeviceVulkan.cpp:1626-1635`.

`IPoolBuffer` (`CGxDeviceVulkan.cpp:1542-1553`) is the same kind of owner for static and dynamic pools. `PoolSizeSet` (`CGxDeviceVulkan.cpp:1576-1578`), `PoolDestroy`, and `IReleasePools` all release through `IReleasePool`.

Those handles must be copied into `VkGarbage` before the record is cleared or overwritten. `RetireBuffer` nulls `buffer` and `memory` on the source after the copy (`VkContext.cpp:1181-1183`), so a second release of the same record is empty. A second copy that still holds the handles is a second free.

`Collect` runs only after `vkWaitForFences` on that frame slot (`VkContext.cpp:709-712`). `FramesInFlight` is 2 (`VkContext.h:131`). The comment at `VkContext.cpp:711` is the rule: retired buffers stay live until this fence signals. `DestroyBuffer` inside `IReleasePool` frees a `VkBuffer` the current command buffer still names. `DestroyDevice` collects garbage (`VkContext.cpp:650`) and does not walk pools. An owned buffer that never entered `VkGarbage` is dropped when the embed is cleared. `IDestroyVulkan` releases pools at `CGxDeviceVulkan.cpp:378` before that collect.

The `memory = VK_NULL_HANDLE` store is the borrowed path only (`CGxDeviceVulkan.cpp:1624`). An owned record keeps the `memory` from `CreateBuffer` so `VkGarbage.memory` is the allocation `Collect` frees (`VkContext.cpp:1234-1235`).

## Order in IBufLock

1. Predecessor owned (`!borrowed`): `RetireBuffer` before the assignment (`CGxDeviceVulkan.cpp:1613`, `CGxDeviceVulkan.cpp:1629`).
2. Predecessor borrowed: leave the ring in `streamChunks`. The guard is `!alloc->borrowed` at `CGxDeviceVulkan.cpp:1612` and `CGxDeviceVulkan.cpp:1628`.
3. New slice borrowed: store it with `borrowed = 1` and `memory = VK_NULL_HANDLE` (`CGxDeviceVulkan.cpp:1623-1625`).
4. New slice owned: that unique buffer becomes the only owner and stays `borrowed == 0` (`CGxDeviceVulkan.cpp:1634-1635`).

Assigning the view first drops an owned handle that never reaches `VkGarbage`. `RetireBuffer` after that assignment retires the ring.

`Stage` already uses this split: a non-borrowed chunk is `RetireBuffer`'d (`VkContext.cpp:1151-1154`); a borrowed view is returned with no retire (`VkContext.cpp:1157`). A helper shared with the embed keeps that split.
