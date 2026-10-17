# G1 GPU skin — Leak

Lifetime for the bone ring and the bind-pose vertex buffers. The ring follows `VkFrame::uniforms`. Owned mesh buffers follow `VkGarbage`.

`FramesInFlight` is 2 (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:141`). The slot that recorded a draw and the slot that later collects garbage are different turns of the same two fences.

## Bone ring

One host-visible storage buffer per `VkFrame`, 8 MiB, created once beside the uniform ring in `VkContext::CreateDeviceResources` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:308`). `CreateBuffer` allocates, binds, and maps for the life of the device (`VkContext.cpp:1047`). Usage is storage only. `Collect` recycles a retired buffer only when usage is `VERTEX | INDEX`, `mapped` is set, and `shadow` is clear (`VkContext.cpp:1518`). A storage ring does not take that spare path.

The cursor lives on the frame next to `uniformOffset`. `VkContext::PrepareFrame` sets `frame.uniformOffset = 0` after the fence wait (`VkContext.cpp:736`), along with `itemCursor`, `indirectCursor`, and the stream cursors (`VkContext.cpp:737`). The bone cursor resets at that same assignment. The `VkBuffer` and its `VkDeviceMemory` stay. `vkResetDescriptorPool` (`VkContext.cpp:733`) drops set handles. It does not free the buffer those sets named.

`GpuSkinBind` appends into the current slot. Alignment is `minStorageBufferOffsetAlignment`, the storage twin of the uniform align at `CGxDeviceVulkan.cpp:1502`. The descriptor range is `256 * sizeof(C44Matrix)` (16384 bytes). Each successful bind consumes that range, rounded up to the alignment, and stores only the dynamic offset. The next bind in this command buffer takes a later offset. A cursor that would pass the 8 MiB size returns 0 and leaves the ring untouched, the same refusal as a uniform that would pass `frame.uniforms.size` (`CGxDeviceVulkan.cpp:1505`). The caller keeps the CPU path. There is no wrap onto an earlier palette.

`Draw` reads the armed offset, writes it into `vkCmdBindDescriptorSets`, and clears the arm. Clearing the arm drops a CPU flag. The palette bytes stay in the ring until this slot’s cursor resets on a later `PrepareFrame`.

`DestroyDevice` is the free. It `vkDeviceWaitIdle`s, `Collect`s each frame, then `DestroyBuffer(frame.uniforms)` (`VkContext.cpp:664`, `VkContext.cpp:669`). The bone ring is destroyed in that same loop, after `Collect`. `DestroyBuffer` is `vkDestroyBuffer` plus `vkFreeMemory` on the spot (`VkContext.cpp:1182`). It is the teardown call for a ring that was never placed on `frame.garbage`.

The other slot’s ring stays mapped and bound to that slot’s command buffer until that slot’s fence signals. A draw on slot 0 does not reset, overwrite, or free slot 1.

## Bind-pose vertex buffers

The character cache is the upload slot on the model, not the shared mesh pool and not the stream ring.

`CM2Model::SetVertices` (`Engine/Source/Model2/M2Model.cpp:3152`) is the G1 upload: `!optimized && texCoord == 0` and `GpuSkinSupported()`, one `GxVBF_PBNT2` copy of the section, cached without comparing bones. That pool is `GxPoolUsage_Static` on `CM2Model::m_vertexUploads`. `IPoolBuffer` owns one `VkBufferAlloc` with `borrowed == 0` (`CGxDeviceVulkan.cpp:1815`). A bone change leaves that alloc in place. `M2PickUploadBuf` (`M2Model.cpp:287`) exists so a CPU rewrite can skip a buffer drawn in the last two frames (`M2Model.cpp:293`). Bind-pose does not rewrite, so it does not rotate `m_gpu[]` and it does not take `M2EnsureUploadBuf`’s destroy branch (`M2Model.cpp:313`) on a bone mismatch. The first stride change, 32-byte `GxVBF_PNT` to 48-byte `GxVBF_PBNT2`, may retire the old CPU pool once. The PBNT2 pool then stays for the life of the model.

A full bone ring returns 0 from `GpuSkinBind`. The CPU fallback writes the stream. It leaves the cached pool allocated.

**When the model dies.** `CM2Model::~CM2Model` calls `M2ReleaseVertexUploads` (`M2Model.cpp:7183`). That walks the slots and `M2DestroyUploadSlot` (`M2Model.cpp:76`).

| Step | What it frees |
| --- | --- |
| `BufDestroy` (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1157`) | The `CGxBuf`. It unlinks the buf and clears `m_primVertexBuf` if `Draw` still holds it (`CGxDevice.cpp:1168`). No `vkDestroyBuffer`, no `vkFreeMemory`. |
| `PoolDestroy` (`CGxDeviceVulkan.cpp:1868`) | Calls `IReleasePool`, then the base `delete`. `CGxPool::~CGxPool` is empty (`Engine/Source/Gx/CGxPool.cpp:10`). |
| `IReleasePool` (`CGxDeviceVulkan.cpp:1833`) | `RetireBuffer` when `!borrowed`, then `delete` the wrapper. |

`RetireBuffer` copies `buffer`, `memory`, `mapped`, `size`, `usage`, and `shadow` onto the current frame and nulls the source (`VkContext.cpp:1469`). `Retire` calls `PrepareFrame`, then `push_back`s (`VkContext.cpp:1464`). A second release of the same record is empty. A second copy that still holds the handles is a second free inside `Collect`.

`UnoptimizeVisibleGeometry` also calls `M2ReleaseVertexUploads` (`M2Model.cpp:1486`) because opt sections are about to be freed. The G1 buffer is `!optimized`. A bone change does not enter that function.

These are the wrong freers:

| Site | Why |
| --- | --- |
| `GpuSkinBind` / `Draw` / a bone change in `SetVertices` | The palette is new. The mesh is not. |
| `CM2SceneRender` | Stack object inside each `CM2Scene::Draw`. Its destructor runs between passes. |
| `CM2Cache::UpdateShared` → `CM2Shared::DestroyBuffers` (`Engine/Source/Model2/M2Cache.cpp:413`, `Engine/Source/Model2/M2Shared.cpp:604`) | Drops `m_vertexPool` / `m_indexPool` on the shared after 10s idle, and `AllocInstances` drops them on grow. A live model’s bind-pose buffer is not on that list. |
| `VkContext::DestroyBuffer` from `~CM2Model` | Frees a `VkBuffer` the other slot’s command buffer still names. |

Device teardown already covers a model that is still alive. `IDestroyVulkan` `vkDeviceWaitIdle`s, then `IReleasePools` walks `m_poolList` (`CGxDeviceVulkan.cpp:393`, `CGxDeviceVulkan.cpp:414`). Upload pools are on that list. `DestroyInstance` then `DestroyDevice`, which `Collect`s every frame before it destroys the rings (`VkContext.cpp:705`, `VkContext.cpp:669`).

## While a frame fence is pending

`PrepareFrame` calls `Collect` only after `vkWaitForFences` on **this** slot returns `VK_SUCCESS` (`VkContext.cpp:728`). The comment at `VkContext.cpp:730` is the rule: retired buffers stay live until this fence signals. `Collect` is the `vkDestroyBuffer` / `vkFreeMemory` for garbage (`VkContext.cpp:1540`). A heap span is `VkHeap::Free` there (`VkContext.cpp:1549`), not `vkFreeMemory`. `VkHeap::Destroy` `vkFreeMemory`s the blocks (`VkContext.cpp:87`) from `DestroyDevice`, after that `Collect`.

A fence timeout skips `Collect` and still zeros the cursors (`VkContext.cpp:728`, then `VkContext.cpp:736`). Garbage already queued stays allocated. The bone ring object stays allocated. The cursor reset is the same assignment as `uniformOffset`. It is not a free, and it is not permission to `vkFree` the previous palettes. Those bytes may still be sampled, which is the existing uniform-ring hole. New code does not paper over it by destroying the ring.

These objects stay until the wait that covers the submit that sampled them:

| Object | Legal free |
| --- | --- |
| This slot’s bone ring, and the other slot’s bone ring | `DestroyBuffer` in `DestroyDevice`, after `vkDeviceWaitIdle` and `Collect` |
| `frame.uniforms`, `frame.items`, `frame.indirect` | Same `DestroyDevice` loop (`VkContext.cpp:670`) |
| `streamChunks` / `stagingChunks` | `DestroyDevice` (`VkContext.cpp:673`). A borrowed view never enters `VkGarbage`. `RetireBuffer` returns on `borrowed` and zeros only the view (`VkContext.cpp:1470`). |
| Anything already on `frame.garbage`, including a bind-pose pool retired by model death | `Collect` after this slot’s fence succeeds |
| The other slot’s `garbage` | That slot’s `Collect` |

`DestroyBuffer` inside `GpuSkinBind`, `Draw`, `SetVertices`, or `~CM2Model` frees a buffer a recorded command still names. `RetireBuffer` on the frame’s bone `VkBufferAlloc` either queues the ring for `Collect` or, if `borrowed` is set, drops the handles and leaks the allocation. The ring is a member of `VkFrame`, same as `uniforms`. It is not garbage and it is not a pool.
