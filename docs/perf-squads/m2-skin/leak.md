# Campaign 1 leak review — M2 skin cache and instance pools

A per-frame skin cache and a grown `CM2Shared` instance pool each hold `VkBuffer`s that a submitted command buffer still names. Freeing those with `VkContext::DestroyBuffer` samples freed GPU memory. Leaving the cache on `CM2Shared` until the 10s idle sweep leaks one allocation per skinned section for every model that stays on screen.

`FramesInFlight` is 2 (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:131`). The frame that drew a buffer and the frame that retires it are different slots.

## Retire rule

Owned buffers are parked on the current `VkFrame::garbage` by `VkContext::RetireBuffer` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:1165`). `VkContext::Collect` (`VkContext.cpp:1198`) is the `vkDestroyBuffer` / `vkFreeMemory`. It runs only from `VkContext::PrepareFrame` after `vkWaitForFences` on that frame’s fence succeeds (`VkContext.cpp:709`).

`CGxDeviceVulkan::IReleasePool` is the device path into that list: a non-borrowed `VkBufferAlloc` is retired, then the wrapper is deleted (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1556`). `PoolDestroy` calls `IReleasePool` (`CGxDeviceVulkan.cpp:1581`).

`VkContext::DestroyBuffer` (`VkContext.cpp:1092`) destroys immediately. It is legal for device teardown and for spare-buffer overflow inside `Collect`. It is a use-after-free on any `VkBuffer` a command buffer in flight still names, including the other slot.

A fence timeout skips `Collect` and still rewinds the stream cursor (`VkContext.cpp:709` then `VkContext.cpp:722`). Garbage from that slot stays allocated. Stream bytes from that slot may still be sampled. A timeout is still in flight.

Borrowed stream views take the early return in `RetireBuffer` (`VkContext.cpp:1166`). That clears the view. It does not free the ring. Ring chunks live on the `VkFrame` until `VkContext::DestroyDevice` (`VkContext.cpp:655`).

## Who frees the skin cache

`CM2Scene` owns the cache and retires it.

`CM2Scene::Animate` advances `m_frameStamp` (`Engine/Source/Model2/M2Scene.cpp:1702`). That is the scene frame boundary. `CGxDeviceVulkan::ScenePresent` has already called `ISceneEnd` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:710`), and `VkContext::EndFrame` has submitted the command buffer that sampled the cache (`VkContext.cpp:906`). `Animate` then `RetireBuffer`s every owned cache entry from the previous stamp. `Collect` frees the GPU memory later, on the fence of the frame that received the retire.

`CM2Scene::~CM2Scene` (`M2Scene.cpp:1643`) retires any entries still held. Teardown has no later `Animate`.

The cache has one owner per `VkBuffer`. Two entries that retire the same alloc are a double destroy inside `Collect`.

These are the wrong freers:

| Site | Why it misses |
| --- | --- |
| `CM2SceneRender` | Stack object inside each `CM2Scene::Draw` (`M2Scene.cpp:2221`). `WorldFrame` draws pass 0, then pass 1 and pass 2 (`WoW/Source/Ui/WorldFrame.cpp:1863`). The destructor runs between passes. |
| `CM2Cache::UpdateShared` | `DestroyBuffers` after 10s off the update list (`Engine/Source/Model2/M2Cache.cpp:413`). Every drawn element relinks the shared (`Engine/Source/Model2/CM2SceneRender.cpp:1115`), so a visible model never reaches this sweep. |
| End of `CM2Scene::Draw` | Same frame still has later passes, and those passes reuse one skin of `(model, section, frame stamp)`. |

A single dynamic buffer overwritten every `Animate` is the in-flight case. Slot 0’s submission still reads it while slot 1 skins the next pose. The live cache is one buffer per in-flight frame, or a new owned buffer each stamp with the previous one retired. The high-water instance pools below are a different object. They stay put across frames.

## Stream lock lifetime

CPU skin writes the device stream singleton, then the next lock on that target replaces it.

| Fill | Lock |
| --- | --- |
| `CM2Model::SetVertices` | `Engine/Source/Model2/M2Model.cpp:2701` |
| `DrawRigidDoodads` vertex and index | `Engine/Source/Model2/CM2SceneRender.cpp:644`, `CM2SceneRender.cpp:667` |
| Doodad `FillDoodadSkin` | `CM2SceneRender.cpp:776` |

`CGxDevice::BufStream` returns `m_streamBufs[target]` and writes `m_itemSize`, `m_size`, `m_itemCount`, and `m_hasData = 0` (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1062`).

`CGxDeviceVulkan::IBufLock` on `GxPoolUsage_Stream` calls `VkContext::StreamLock` (`CGxDeviceVulkan.cpp:1596`, `VkContext.cpp:1086`). `BumpChunk` appends into `frame.streamChunks`. A slice that fits is `borrowed = 1` and the cursor moves forward (`VkContext.cpp:1046`). The same frame does not rewind. `PrepareFrame` sets `streamChunk` and `streamOffset` back to 0 after the fence wait (`VkContext.cpp:722`). That rewind is the end of every borrowed slice from that slot.

`IBufLock` stores the new byte offset on the singleton (`CGxDeviceVulkan.cpp:1638`). `CGxDeviceVulkan::Draw` reads `m_index` at bind time (`CGxDeviceVulkan.cpp:1263`, `CGxDeviceVulkan.cpp:1445`). A cache that keeps the `CGxBuf*` and draws after another `BufStream` on that target binds the later slice. The skinned bytes are still in the ring until the rewind. The singleton no longer points at them.

A lock larger than `StreamRingSize` (64 MiB, `VkContext.h:135`) takes `BumpChunk`’s owned `CreateBuffer` (`VkContext.cpp:1023`, `borrowed` stays 0). The next stream `IBufLock` retires that alloc (`CGxDeviceVulkan.cpp:1613`, `CGxDeviceVulkan.cpp:1629`) and `delete`s the wrapper (`CGxDeviceVulkan.cpp:1630`). A cache that kept `pool->m_apiSpecific` then uses a freed `VkBufferAlloc`. Copying the `VkBuffer` handle out before that retire is the legal handle, and it stays live until `Collect` on the frame that retired it.

`RetireBuffer` on a borrowed view does not keep the slice alive (`VkContext.cpp:1166`). Holding `mapped + index` across `PrepareFrame`’s rewind is the next occupant of the ring.

The skin cache stores an owned dynamic buffer per `(model, section, frame stamp)`, with the `VkBuffer` and offset captured at fill. It does not store `m_streamBufs[]`.

## `DestroyBuffers` and the grown instance pool

`CM2Shared::DestroyBuffers` (`Engine/Source/Model2/M2Shared.cpp:602`) drops `m_indexBuf` / `m_vertexBuf` through `BufDestroy`, then `PoolDestroy`s `m_indexPool` / `m_vertexPool`. `BufDestroy` frees the CPU `CGxBuf` only (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1157`). The GPU free is `PoolDestroy` → `IReleasePool` → `RetireBuffer`, as in the retire rule.

Callers today:

| Caller | Line | When |
| --- | --- | --- |
| `CM2Shared::AllocInstances` | `M2Shared.cpp:779` | `m_instances` grows |
| `CM2Cache::UpdateShared` | `M2Cache.cpp:413` | unused for 10s |
| `CM2Shared::~CM2Shared` | `M2Shared.cpp:1882` | last release |

`AllocInstances` (`M2Shared.cpp:771`) rounds the request up to a multiple of 16, clamps to `m_maxInstances`, and calls `DestroyBuffers` on the way up. The pools are then recreated at the new `m_instances` by `SetIndices` (`M2Shared.cpp:340`) and `SetVertices` (`M2Shared.cpp:390`, `GxPoolUsage_Static`). Each step up retires the buffer the previous size still has bound on this frame and on the other in-flight slot. `RetireBuffer` covers that. `DestroyBuffer` on that path does not.

Doubling in place retires the previous `VkBufferAlloc` once, on the step up, through `IReleasePool`. `m_instances` stays at the high-water mark. A later smaller batch uses the existing pools. `DestroyBuffers` does not run on a steady camera.

A shrink every frame is the same retire as today’s grow, repeated while the steady camera still samples the old size. Capacity stays at the peak. `DestroyBuffers` does not clear `m_instances`, so a shrink has to be an explicit drop. Leaving the drop out is the leak-safe behavior.

`DestroyBuffers` identifies the stream singleton with `BufStream(target, 0, 0)` (`M2Shared.cpp:606`, `M2Shared.cpp:621`). That call still writes the singleton’s size fields and clears `m_hasData` (`CGxDevice.cpp:1062`). Every grow and every 10s idle destroy clobbers a skin cache that aliased `m_streamBufs[]`.

Same-frame order inside `DrawBatchDoodad` is `AllocInstances`, then the rigid or skin draws (`CM2SceneRender.cpp:711`). A second batch of the same shared can bind the old buffer, then grow, then bind the new one. Both handles are in one command buffer. The old handle is retired onto that frame and collected only after that frame’s fence.
