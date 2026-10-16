# Uniforms integrator

Campaign 2, group B in `docs/perf-agent-plan.md:46`. This squad closes, then the vulkan squad (Campaign 6) starts. Both squads edit `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp`. The splice is by function.

## Merge order

1. **Dirty-bit pack.** `IBuildUniforms`, and inside `Draw` the `IRsSync` call plus the uniform upload.
2. **Conditional descriptor bind.** The descriptor-set block in `Draw`. Lands on top of the pack slice.
3. **Pin check.** `VkDrawUniforms` stays the 768-byte record. No field edit.
4. **Reviews, in this order when they touch the same lines:** Faith, then Edges, then Leak. One concern each. The tree builds after each fix.
5. **Close Campaign 2.** Then vulkan rebases onto this tree and edits `IBufLock` and `VkContext` only.

A uniforms hunk is the two bodies below. A vulkan hunk is `IBufLock` plus the `VkContext` / `VkHeap` functions below. `CGxDeviceVulkan.h` stays as it stands: `Draw` at `CGxDeviceVulkan.h:158`, `IBuildUniforms` at `CGxDeviceVulkan.h:224`, `IBufLock` at `CGxDeviceVulkan.h:212`.

## Functions this squad may edit

| Function | Body |
| --- | --- |
| `CGxDeviceVulkan::IBuildUniforms` | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:971-1046` |
| `CGxDeviceVulkan::Draw` | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1183-1476` |

Slice lines inside those bodies:

| Slice | Lines |
| --- | --- |
| Dirty-bit pack | `IBuildUniforms` `971-1046`. In `Draw`, `IRsSync` at `1198` and the upload at `1358-1390`. |
| Conditional descriptor bind | `Draw` `1399-1442` (`CachedSet` through `vkCmdBindDescriptorSets`). |
| Pin check | `VkDrawUniforms` stays `CGxDeviceVulkan.h:22-46` with the `static_assert` at `CGxDeviceVulkan.h:46`. |

`Draw` already calls `IRsSync(0)` at `CGxDeviceVulkan.cpp:1198` before `IBuildUniforms` at `1359`. `CGxDevice::IRsSync` (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1245-1266`) clears `m_dirty` and `m_dirtyStates`. The pack slice samples render-state dirtiness at that call, then consumes it in `IBuildUniforms`.

Producers the pack reads. Their bodies stay as they are:

| Producer | Body | What the pack reads |
| --- | --- | --- |
| `CGxDevice::IRsDirty` | `CGxDevice.cpp:1188-1203` | `m_dirty` / `m_dirtyStates` |
| `CGxDevice::RsSet` (`void*`) | `CGxDevice.cpp:1344-1365` | render state |
| `CGxDevice::RsSet` (`int32_t`) | `CGxDevice.cpp:1953-1964` | render state |
| `CGxDevice::RsSet` (`float`) | `CGxDevice.cpp:1967-1978` | render state |
| `CGxDevice::RsSet` (`C3Vector`) | `CGxDevice.cpp:1981-1994` | render state |
| `CGxDevice::RsPop` | `CGxDevice.cpp:1369-1387` | render state |
| `CGxDevice::MasterEnableSet` | `CGxDevice.cpp:1309-1340` | master enables |
| `CGxDevice::XformPush` | `CGxDevice.cpp:641-652` | `CGxMatrixStack::m_dirty` |
| `CGxDevice::XformPushAndSet` | `CGxDevice.cpp:704-711` | `m_dirty` |
| `CGxDevice::XformSetView` | `CGxDevice.cpp:1446-1457` | `m_dirty` |
| `GxXformPop` | `Engine/Source/Gx/Gx.cpp:387-394` | `m_dirty` |
| `GxXformTranslate` | `Gx.cpp:398-403` | `m_dirty` |
| `GxXformScale` | `Gx.cpp:407-412` | `m_dirty` |
| `GxXformSet` | `Gx.cpp:1019-1024` | `m_dirty` |
| `GxXformPush` (matrix) | `Gx.cpp:627-629` | calls `XformPushAndSet` |
| `CGxMatrixStack::Identity` | `Engine/Source/Gx/CGxMatrixStack.cpp:6-31` | `m_dirty` at `29` |
| `CGxDeviceVulkan::XformSetProjection` | `CGxDeviceVulkan.cpp:780-812` | `m_dirty` at `810`, `m_projNative` |
| `CGxDeviceVulkan::ISetTexGen` | `CGxDeviceVulkan.cpp:898-927` | tex-gen `m_dirty` at `924` |
| `CGxDevice::LightEnable` | `CGxDevice.cpp:798-805` | `m_flags` |
| `CGxDevice::LightSet` | `CGxDevice.cpp:1048-1058` | replaces `m_lights[index]` |

`IXformTex` (`CGxDeviceVulkan.cpp:934-969`) clears the tex stacks at `967-968` when the pack calls it. The pack slice keeps that call on the path that writes `texMatrix`. `IXformTex` itself stays.

Cache and ring, already in `Draw`:

- `m_uniformsValid` is cleared in `IInvalidateCmdState` (`CGxDeviceVulkan.cpp:527-536`, assignment at `535`), which `ISceneBegin` calls at `589`. The skip in `Draw` at `1376` uploads when that flag is clear.
- `VkContext::PrepareFrame` sets `frame.uniformOffset = 0` at `VkContext.cpp:717`. The upload at `Draw` `1378-1387` is the writer of the next offset.
- The full-ring return stays at `Draw` `1381-1384`. Campaign 6 relies on that guard (`docs/perf-agent-plan.md:155`).

## Functions the vulkan squad may edit

`CGxDeviceVulkan::IBufLock` — `CGxDeviceVulkan.cpp:1589-1650` (declaration `CGxDeviceVulkan.h:212`).

`VkHeap` and `VkContext` in `Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp`:

| Function | Body |
| --- | --- |
| `VkHeap::Alloc` | `7-62` |
| `VkHeap::Free` | `64-83` |
| `VkHeap::Destroy` | `85-91` |
| `VkContext::CreateInstance` | `93-139` |
| `VkContext::CreateDevice` | `141-247` |
| `VkContext::CreateDeviceResources` | `249-306` |
| `VkContext::DestroySurface` | `308-318` |
| `VkContext::SetSurface` | `320-327` |
| `VkContext::CreateRenderPass` | `329-385` |
| `VkContext::CreateSwapchain` | `387-607` |
| `VkContext::DestroySwapchain` | `609-639` |
| `VkContext::DestroyDevice` | `641-684` |
| `VkContext::DestroyInstance` | `686-694` |
| `VkContext::Frame` | `696-698` |
| `VkContext::PrepareFrame` | `700-727` |
| `VkContext::CreateDescriptorPool` | `729-744` |
| `VkContext::AllocateSet` | `746-765` |
| `VkContext::CachedSet` | `767-778` |
| `VkContext::FlushUploads` | `780-787` |
| `VkContext::BeginFrame` | `789-823` |
| `VkContext::UploadCmd` | `825-841` |
| `VkContext::Submit` | `843-904` |
| `VkContext::EndFrame` | `906-930` |
| `VkContext::AbandonFrame` | `932-942` |
| `VkContext::FlushFrame` | `944-958` |
| `VkContext::FindMemoryType` | `960-968` |
| `VkContext::AcquireBuffer` | `970-980` |
| `VkContext::CreateBuffer` | `982-1016` |
| `VkContext::BumpChunk` | `1018-1054` |
| `VkContext::AllocDrawItems` | `1056-1069` |
| `VkContext::AllocIndirect` | `1071-1084` |
| `VkContext::StreamLock` | `1086-1090` |
| `VkContext::DestroyBuffer` | `1092-1099` |
| `VkContext::CreateImage` | `1101-1136` |
| `VkContext::Stage` | `1138-1158` |
| `VkContext::Retire` | `1160-1163` |
| `VkContext::RetireBuffer` | `1165-1184` |
| `VkContext::RetireImage` | `1186-1196` |
| `VkContext::Collect` | `1198-1245` |
| `VkContext::Barrier` | `1247-1334` |

Declarations: `VkHeap` at `VkContext.h:68-70`, `VkContext` at `VkContext.h:180-221`. `VkSetKey::operator==` is `VkContext.h:76`. `VkSetKeyHash::operator()` is `VkContext.h:80-85`.

`GpuDrawItem` stays the 448-byte record at `VkContext.h:89-97`. `VkBufferAlloc::buffer` stays at `VkContext.h:19`. `Draw` binds that handle at `CGxDeviceVulkan.cpp:1257-1262` and `1455-1462`. `IBufLock` writes it at `1609-1636` and sets `buf->m_index` at `1638`, which `Draw` uses at `1263`.

`Draw` calls `VkContext::CachedSet` (`VkContext.cpp:767-778`, declaration `VkContext.h:196`) at `CGxDeviceVulkan.cpp:1402`. The call site is this squad's. The function body is vulkan's. The signature stays `(const VkSetKey&, VkDescriptorSetLayout, int32_t&)`.

Callers of the two owned entry points stay as they are: `DrawInstanced` `1478-1500`, `DrawTerrainBatch` `1502-1540`, `BufLock` `1674-1677`, `BufData` `1685-1702`. Pool helpers `IPoolBuffer` through `PoolDestroy` stay at `1542-1587`. `IBufUnlock` stays at `1652-1672`. D3D locks stay at `Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.cpp:132` and `Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.cpp:130`.
