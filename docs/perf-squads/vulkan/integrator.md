# Vulkan integrator — merge with uniforms

Campaign 2 (uniforms) closes before Campaign 6. Merge **uniforms first**, then this branch. Resolve by function. Do not interleave the two edits inside one function.

Uniforms owns `Draw` and `IBuildUniforms`. This squad owns `IBufLock`, `VkHeap`, and `CreateBuffer`.

## Resolve in this order

1. **`VkContext.h` structs, then `VkHeap`.** Take this branch for `VkHeap` (`VkContext.h:53-71`, `VkContext.cpp:7-91`). Keep `VkBufferAlloc` field order through `borrowed` (`VkContext.h:18-26`). Append suballocation fields after `borrowed`. Leave `VkFrame::uniforms` and `uniformOffset` (`VkContext.h:107-108`) and `CreateBuffer`'s signature (`VkContext.h:206`).

2. **`CreateBuffer`.** Take this branch (`VkContext.cpp:982-1016`). The uniform-ring call (`VkContext.cpp:290`) must still come back host-visible, host-coherent, and mapped, with `size == UniformRingSize` (`VkContext.h:131-132`) and `buffer` covering that ring from byte 0. `PrepareFrame` still sets `frame.uniformOffset = 0` (`VkContext.cpp:717`). `DestroyDevice` still retires that ring with `DestroyBuffer(frame.uniforms)` (`VkContext.cpp:651`). A suballocated ring frees its `VkBuffer` there and does not `vkFreeMemory` a block other rings still use (`VkContext.cpp:1092-1098`).

3. **Device fields.** Take uniforms' dirty bits beside `m_uniforms`, `m_uniformOffset`, and `m_uniformsValid` (`CGxDeviceVulkan.h:116-118`). This squad does not edit that cluster. `VkDrawUniforms` stays 768 bytes (`CGxDeviceVulkan.h:46`).

4. **`IBuildUniforms`, then `Draw`.** Take the uniforms text for `IBuildUniforms` (`CGxDeviceVulkan.cpp:971-1046`) and `Draw` (`CGxDeviceVulkan.cpp:1183-1476`), including the pack (`CGxDeviceVulkan.cpp:1358-1389`) and the descriptor bind (`CGxDeviceVulkan.cpp:1399-1442`). This squad does not edit those functions. If both sides touched `IInvalidateCmdState`, keep the uniforms clears and keep `m_uniformsValid = 0` (`CGxDeviceVulkan.cpp:535`).

5. **`IBufLock` last.** Take this branch (`CGxDeviceVulkan.cpp:1589-1650`). `IPoolBuffer` and `IReleasePool` (`CGxDeviceVulkan.cpp:1542-1565`) sit between the two owners. Uniforms does not edit them. On a conflict there, take this squad. They are how `CreateBuffer` reaches a mesh pool (`VkContext.cpp:970-979`).

## What Draw still reads after the merge

`Draw` is not updated for the new lifetime. `IBufLock` and `CreateBuffer` keep these shapes:

| `Draw` site | Still true after this branch |
| --- | --- |
| `CGxDeviceVulkan.cpp:1256-1264` | `CGxPool::m_apiSpecific` (`CGxPool.h:18`) is a `VkBufferAlloc*` with a non-null `buffer`. Vertex offset is `CGxBuf::m_index` (`CGxBuf.h:14`) unless the draw is indirect. |
| `CGxDeviceVulkan.cpp:1455-1470` | The index `VkBufferAlloc*` is the same kind of pointer. The index bind stays at buffer offset 0. `firstIndex` stays `m_index >> 1`. |
| `CGxDeviceVulkan.cpp:1377-1389` | `frame.uniforms.mapped` is writable for 768 bytes at the bumped offset. `frame.uniforms.size` is the ring capacity. The overflow `return` (`CGxDeviceVulkan.cpp:1381-1383`) stays: a full ring does not reuse another draw's matrices. |
| `CGxDeviceVulkan.cpp:1409` and `:1441-1442` | The dynamic uniform descriptor is `{ frame.uniforms.buffer, 0, sizeof(VkDrawUniforms) }` plus `m_uniformOffset`. The ring `VkBuffer` is created with the device (`VkContext.cpp:290`) and is not replaced mid-frame. |

Embedding the borrowed stream view drops the per-lock `new` / `delete` (`CGxDeviceVulkan.cpp:1618-1634`). The pointer stored in `m_apiSpecific` is still a `VkBufferAlloc*`, and `IBufLock` still writes `buf->m_index` to the byte offset inside that buffer (`CGxDeviceVulkan.cpp:1638`). `Draw` does not branch on `borrowed`.

`GxPoolUsage_Static` may be device-local and unmapped. `Draw` only reads `buffer` and the buf offset for those pools. Host mapping stays on the rings `Draw` writes: the uniform ring above, and the same `CreateBuffer` contract for `frame.items` and `frame.indirect` (`VkContext.cpp:294-298`).

`StreamRingSize` stays 64 MiB (`VkContext.h:134-135`). Do not reopen the per-wrap pool allocation.
