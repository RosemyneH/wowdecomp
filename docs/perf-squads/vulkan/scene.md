# Campaign 6 scene — outdoor fly

Vulkan squad, buffer lifetime. Same outdoor character and camera every run. The pass window is the fly, not the login load.

## Pose

Map is the Eastern Kingdoms load that logged `Goldshire, Elwynn Forest` (`test/dev-335/Errors/2026-10-16 13.02.35 Crash.txt`). Login writes that spawn into the camera in `Packet_SMSG_LOGIN_VERIFY_WORLD` (`WoW/Source/Client.cpp:977`) and `LoadNewWorld` (`WoW/Source/Client.cpp:932`).

| | Value |
| --- | --- |
| Player | `(-9476.5, -5.569, 403.851)` |
| Camera | Third person, distance `15.0` (`WoW/Source/UIUtil/Camera_CVar.cpp:543`), pitch `0`, yaw along the velocity |
| Follow | `m_camera->m_model` clear, so the area of interest is the player (`WoW/Source/Ui/WorldFrame.cpp:2183`) |

`Z` stays `403.851` for the whole capture. That keeps the view outdoors, above Goldshire, and out of a WMO.

## Fly

Hover until two consecutive one-second log lines show the same `vkAllocateMemory` count per frame. That count is the baseline.

Then fly in a straight line, constant `CMovementShared::m_flightSpeed` (`WoW/Source/Object/MovementShared.h:54`), flying flag held (`WoW/Source/Ui/UIMacroOptions.cpp:467`, `m_flags & 0x2000000`). No stop, no turn, no pitch change.

- Start: `(-9476.5, -5.569, 403.851)`
- Travel: world `X` decreases by `2 * 533.33331` yards. `Y` and `Z` stay put.
- End: `(-9476.5 - 2 * 533.33331, -5.569, 403.851)`

`533.33331` is one ADT (`WoW/Source/WorldClient/Map.cpp:4180`). Two tiles move `chunkX` in `World::PrepareAreaOfInterest` (`WoW/Source/WorldClient/World.cpp:1215`) and slide `chunkRect` (`WoW/Source/WorldClient/World.cpp:1224`). New terrain, doodads, and grass enter at the leading edge. That is the outdoor load this campaign has to keep flat.

The window is the first `CWorld::frameCnt` tick after the player leaves the start through the tick that reaches the end. A frame is one increment of `CWorld::frameCnt` (`WoW/Source/WorldClient/World.cpp:1529`).

## Frozen

Do not retune these between the before and after runs. They are the Goldshire session plus the code defaults that session did not override.

| Key | Value | Where |
| --- | --- | --- |
| `GxApi` | Vulkan | crash log |
| `gxResolution` | `3440x1440` | crash log |
| `gxVSync` | `0` | crash log |
| `farclip` | `10000` | crash log; cap is `kFarclipMaxExtended` (`WoW/Source/WorldClient/WorldParam_CVarSlider.h:10`) with `extendedView` `1` |
| `groundEffectDist` | `62.8571434021` | crash log; still under the `220` cap (`WoW/Source/WorldClient/World.cpp:1316`) |
| `groundEffectDensity` | `64` | crash log |
| `environmentDetail` | `1.5` | crash log |
| `shadowLevel` | `0` | crash log |
| `CShaderEffect::s_enableShaders` | `0` | `Engine/Source/ShaderEffect/ShaderEffect.cpp:34` |

Leave these alone. They are not scene knobs.

- `StreamRingSize` is `64 << 20` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:135`). The old per-wrap `2.75` MiB pool stays closed (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1597`).
- Spare caps stay `MaxSpareBuffers` `512` and `MaxSpareBytes` `32` MiB (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:174`).
- Frame garbage still retires in `VkContext::Collect` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:1198`).

## Pass

Both gates, every frame of the fly. The hover baseline is the flat line. A step when a new ADT enters is a fail.

### `vkAllocateMemory` per frame is flat

Count only these two calls. Do not count `vkAllocateCommandBuffers` or `vkAllocateDescriptorSets`.

| Call | Site | What it allocates |
| --- | --- | --- |
| `VkHeap::Alloc` | `Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:51` | A new image block. Block size is `64 << 20` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:56`) when the freelist cannot satisfy the request (`VkContext.cpp:41`). |
| `VkContext::CreateBuffer` | `Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:1003` | One host-visible coherent allocation (`VkContext.cpp:1001`). |

`CreateBuffer` runs from `AcquireBuffer` on a spare miss (`VkContext.cpp:979`), from an oversized bump (`VkContext.cpp:1024`), and from a new ring chunk (`VkContext.cpp:1034`). Stream and staging both bump (`VkContext::StreamLock` at `VkContext.cpp:1086`, `BumpChunk` at `VkContext.cpp:1089`). A new static pool reaches the same `vkAllocateMemory` through `CGxDeviceVulkan::IPoolBuffer` (`CGxDeviceVulkan.cpp:1546`).

Each fly frame’s count equals the hover baseline. The series does not climb with distance.

### Stream locks do not call `operator new`

A stream lock is `CGxDeviceVulkan::IBufLock` when `pool->m_usage == GxPoolUsage_Stream` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1596`, `GxPoolUsage_Stream` at `Engine/Source/Gx/Types.h:256`). That branch calls `VkContext::StreamLock` (`CGxDeviceVulkan.cpp:1605`).

`operator new` is `SMem.cpp:115`. On this path it is the two `VkBufferAlloc` allocations:

- Borrowed ring view, first time the pool has no wrapper: `CGxDeviceVulkan.cpp:1619`
- Oversized lock (`slice.borrowed` clear, `BumpChunk` did not set `borrowed` at `VkContext.cpp:1046`): `CGxDeviceVulkan.cpp:1634`

Both stay at zero for every frame of the hover and the fly. The steady borrowed path writes through the existing wrapper (`CGxDeviceVulkan.cpp:1623`) and must not replace that with a heap allocation.

`IPoolBuffer`’s `new VkBufferAlloc` (`CGxDeviceVulkan.cpp:1544`) is a pool create, not a stream lock. It is not this gate. It still counts on the `vkAllocateMemory` gate when `AcquireBuffer` misses the spare list.

The outdoor stream pools are `0x2C0000` vertex bytes and `0x40000` index bytes (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:62`, `CGxDevice.cpp:1701`). Both are under `StreamRingSize`, so a steady fly takes the borrowed branch. An oversized lock still fails this gate if it appears.
