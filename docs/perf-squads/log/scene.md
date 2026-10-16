# Scene capture

Campaign 0 wants one steady one-second line from each scene below, with the same character position and camera on every later run. This script holds that pose. It does not rename log fields. Counter owns the compile-out flag (default off) and the line format. Until that flag is on, there is nothing to sample.

The line the plan prints, once a second, through the existing console (`ConsolePrintf` at `WoW/Source/Console/ConsoleClient.cpp:941`):

| Field | Where the count comes from |
| --- | --- |
| zone times | `OsGetAsyncTimeClocks` (`Engine/Source/Os/Linux/TimeManager.cpp:59`, `CLOCK_MONOTONIC`) |
| alloc counts | fresh vectors on the flush paths below |
| stream bytes / stream locks | `CGxDeviceVulkan::IBufLock` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1589`) |
| `vkAllocateMemory` count | `VkHeap::Alloc` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:51`) and `VkContext::CreateBuffer` (`VkContext.cpp:1003`) |
| uniform uploads | `memcmp` then memcpy of `VkDrawUniforms` (`CGxDeviceVulkan.cpp:1377`) |
| descriptor binds | `vkCmdBindDescriptorSets` (`CGxDeviceVulkan.cpp:1442`) |
| terrain batch size | `s_terrainBatch` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2461`) |
| draw count | `CGxDeviceVulkan::Draw` (`CGxDeviceVulkan.cpp:1183`) |
| visibility flushes | `CMapChunk::FlushQueuedVisibility` (`WoW/Source/WorldClient/MapChunk.cpp:514`) and `CWorldScene::CullDoodadsExterior` (`WoW/Source/WorldClient/World.cpp:3924`) |

`DrawInstanced` ends in `Draw` (`CGxDeviceVulkan.cpp:1499`), so a rigid batch still counts as a uniform upload, a descriptor bind, and a draw.

## What the operator supplies

This repository has no ADT, WDT, WDL, M2, WMO, or DBC. `README.md` says to copy a World of Warcraft **3.3.5a (12340)** `Data` folder next to the client. The capture also needs:

- That `Data` directory, including the locale MPQs and `realmlist.wtf`.
- A realm that applies `CMSG_WORLD_TELEPORT` and `MSG_MOVE_TELEPORT_CHEAT`. The client only sends those packets (`WoW/Source/Client_Console.cpp:215` and `Client_Console.cpp:117`). It does not move the character itself.
- A character already in the world. `worldport` and `port` return immediately when there is no active player (`Client_Console.cpp:143`, `Client_Console.cpp:76`).
- Four pose cards, written down by the operator. World coordinates and `WorldSafeLocsRec` names live in the operator's DBC, not in this tree. `worldport` can search `m_areaName` (`Client_Console.cpp:187`) and prints `Porting to %s` (`Client_Console.cpp:204`). That lands on the safe-loc point. The locked pose is the numeric form.
- For the crowd scene, other units in view. The client has no spawn list.

The client does not print world x, y, z. Read them from the realm. Read the camera from the console: a cvar name with no value prints the current string (`Engine/Source/ConsoleBase/ConsoleVar.cpp:87`).

## Frozen machine

Build `WowClient` and launch it the way `README.md` describes. Set these before the first pose and leave them for every later run. Typing a cvar name and a value is the console setter (`ConsoleVar.cpp:87`).

```
gxApi Vulkan
farclip 350
groundEffectDist 70
groundEffectDensity 16
cameraDistanceMax 15
cameraSavedDistance 15
camerasmooth 0
cameraBobbing 0
M2BatchDoodads 1
nameplateShowEnemies 0
nameplateShowFriends 0
```

`gxApi` is the string `Vulkan` (`Engine/Source/Gx/Gx.cpp:47`). Restart after changing it. The device log line is `GxApi: Vulkan` (`Gx.cpp:983`).

That API is what keeps the capture on the CPU-skin path. Vulkan reports every shader target as 0 (`CGxDeviceVulkan.cpp:489`). `M2Initialize` sets cache flag `0x8` only when those targets are non-zero (`Engine/Source/Model2/M2Cache.cpp:129`). `CWorld::Initialize` then calls `CShaderEffect::InitShaderSystem` with that flag (`WoW/Source/WorldClient/World.cpp:1724`). `GetM2CvarFlags` always ORs `0x8` (`WoW/Source/Client_CVar.cpp:728`), and the device gate is what drops it. D3D9 is a different capture.

`farclip` defaults to `350` (`WoW/Source/WorldClient/WorldParam_CVar.cpp:696`). `groundEffectDist` defaults to `70` and is capped at 220 yards (`WorldParam_CVar.cpp:712`, `World.cpp:1316`, `World.cpp:136`). `groundEffectDensity` defaults to `16` (`WorldParam_CVar.cpp:711`). `cameraDistanceMax` defaults to `15` (`WoW/Source/UIUtil/Camera_CVar.cpp:543`). `RestoreSavedDistancePitch` clamps distance to 50 (`Camera.cpp:4239`) and runs when account cvars load (`WoW/Source/Ui/GameUI.cpp:6200`). Set `cameraSavedDistance` and `cameraSavedPitch` before entering the world so that restore applies them. The client writes those two cvars from the live camera in `CGCamera::SaveCameraSettings` (`Camera.cpp:1798`).

Leave the default world enables from `CWorld::Initialize` (`World.cpp:1684`, mask `0x7104B73`). That mask already includes chunks, doodads, map objects, and detail doodads. Do not run `showDetailDoodads`, `showLowDetail`, or `maxLOD`. `showDetailDoodads` toggles `Enable_DetailDoodads` (`WoW/Source/WorldClient/World_Console.cpp:77`). `Enable_LowDetail` is already in the mask and only gates the horizon (`World.cpp:2146`).

Nameplates stay off for the first three scenes. The crowd scene turns them on, then the next outdoor run turns them off again.

Stand dismounted, on the ground, not swimming. `CWorldScene::RenderDetailDoodads` runs from the world frame (`WoW/Source/Ui/WorldFrame.cpp:1871`) after `CWorld::Render` (`WorldFrame.cpp:1842`).

## Pose card

One card per scene, kept with the log. Replay uses the numbers, not a fresh walk.

```
scene: outdoor | city | interior | crowd
continent: <uint from the realm>
x y z: <from the realm>
facing_deg: <degrees; worldport multiplies by 0.017453292 at Client_Console.cpp:182>
cameraSavedPitch: <console print>
```

`worldport` usage is `worldport <continentID> [x y z] [facing]` (`Client_Console.cpp:156`). Registered at `WoW/Source/Client.cpp:1886`.

After the teleport, wait until the area is loaded, then stand still. Hands off the mouse. `camerasmooth 0` and `cameraBobbing 0` keep the saved pitch and distance from drifting. Character facing is the yaw. Zoom is `cameraSavedDistance`.

## Take the line

The first seconds after a teleport allocate buffers, fill grass, and grow `CM2Shared` instance pools. Those are warmup, not the steady line.

1. Arrive with `worldport` and the pose card.
2. Confirm the scene checks in the section below (grass on screen, both M2 kinds, interior doorway, or the unit crowd).
3. Stand still for five seconds.
4. `clear`, then stand still for three more seconds.
5. `AppendLogToFile` (`WoW/Source/Console/ConsoleClient.cpp:52`). Default file is `ConsoleLogs/Log<Timestamp>.txt`. Pass a name: `AppendLogToFile ConsoleLogs/scene-outdoor.txt`.
6. Keep the middle one-second line of those three. The first line can still include the console command.

`vkAllocateMemory` may be zero on that steady line. Campaign 6 wants it flat, not climbing. A zero there is a pass. `UseStreamingBufs` (`MapRenderChunk.cpp:889`) and grass `FillVertices` (`DetailDoodad.cpp:340`) may also be zero once `m_chunkBuf` and the grass pool are warm (`MapRenderChunk.cpp:930`, `DetailDoodad.cpp:892`). Those zeros are a pass on the steady camera.

Run the four scenes in this order, with the same pauses, on every before and after. The client has no clock freeze. The realm clock moves the lights, and the lights are inside the uniform pack.

## Outdoor — terrain, doodads, grass

Elwynn or Westfall density means a view with terrain chunks, placed M2 doodads, and grass within `detailDoodadDist` (70 yards). Pick the spot in the operator's client. The WDT is theirs too.

**Must be non-zero**

| Counter | Why this scene |
| --- | --- |
| terrain batch size | chunks pushed into `s_terrainBatch` |
| visibility flushes | chunk queue flushed, exterior doodads refilled |
| stream locks | rigid doodad sections lock vertices and indices every batch |
| stream bytes | those locks |
| uniform uploads | each draw packs 768 bytes |
| descriptor binds | each draw binds |
| draw count | terrain, doodads, and grass |
| alloc counts | `FlushMultiPass` builds `used` and `draws` every flush |

Grass must be on screen. The grass draw is inside draw count. A line from a paved spot is not this scene.

**May be zero:** `vkAllocateMemory`, `UseStreamingBufs`, grass `FillVertices` after warmup.

**Map header gate.** `CMap::LoadWdt` copies the WDT header into `SMMapHeader` (`WoW/Source/WorldClient/Map.cpp:2907`, struct at `MapRenderChunk.h:207`). With terrain pixel shaders invalid, `SetShaders` uses `RenderMultiPassAlpha` only when flag bit `0x4` is clear (`MapRenderChunk.cpp:3191`). `RenderChunks` calls `QueueMultiPass` only for that function (`World.cpp:3710`). Vulkan leaves `Enable_PixelShaders` off because shader target 4 is 0 (`World.cpp:1690`), so `ValidateShaders` leaves `gTerrainPixelShadersValid` false (`Map.cpp:2499`). If the operator's WDT has bit `0x4` set, this path draws through `RenderMultiPassAdditive` and the terrain batch stays 0. Record the header flags next to the line. Do not retune the client to force the alpha batch.

**Workload**

- Areas in the chunk rect call `CMapArea::Update`, which calls `AddMapChunk` (`Map.cpp:1757`, `MapArea.cpp:259`, `MapChunk.cpp:598`).
- `AddMapChunk` enqueues visibility (`MapChunk.cpp:600`, `MapChunk.cpp:415`).
- `CWorldScene::Render` calls `CMapRenderChunk::UpdatePools` (`CWorldSceneRender.cpp:92`), which flushes that queue (`MapRenderChunk.cpp:806`, `MapChunk.cpp:514`). A non-empty flush allocates `jobs` and `liquids` (`MapChunk.cpp:524`, `MapChunk.cpp:549`).
- Exterior cull is the `else` branch (`CWorldSceneRender.cpp:138`), then `CullSortTable` (`World.cpp:4210`) and `CullChunks` (`World.cpp:4054`). Each surviving chunk calls `CullDoodadsExterior` (`World.cpp:4065`), which clears and refills `s_doodadCulls` (`World.cpp:3925`).
- `QueueMultiPass` copies a `TerrainCapture` and pushes `s_terrainBatch` (`MapRenderChunk.cpp:2526`, `MapRenderChunk.cpp:2556`). `FlushMultiPass` allocates `used` and `draws` (`MapRenderChunk.cpp:2559`, `MapRenderChunk.cpp:2572`).
- Hole-free index size used for the pool is `768 * count` (`MapRenderChunk.cpp:830`). `CreateIndices` writes the 8×8 pattern (`MapChunk.cpp:302`).
- Grass: a chunk inside `detailDoodadDist` calls `CreateDetailDoodads` (`MapChunk.cpp:1611`). `RenderDetailDoodads` walks the list (`World.cpp:3573`). `CDetailDoodadInstBatch::Draw` takes a pool slot or skips (`DetailDoodad.cpp:873`, `DetailDoodad.cpp:884`, `AcquireBuffers` at `DetailDoodad.cpp:117`).
- Doodad batches are M2 element type 2 (`CM2SceneRender.cpp:1125`) into `DrawBatchDoodad` (`CM2SceneRender.cpp:697`). With shaders off, a one-bone section takes `DrawRigidDoodads` (`CM2SceneRender.cpp:717`, `CM2SceneRender.cpp:608`) and stream-locks vertices and indices (`CM2SceneRender.cpp:644`, `CM2SceneRender.cpp:667`).

## City — dense M2s, rigid and skinned

A street or square in the operator's city data, outdoors, with many placed M2s. Buildings that are WMO groups do not satisfy this scene. The view has to contain both of these in the same second:

- a one-bone section, so `DrawRigidDoodads` returns 1
- a multi-bone section, so that function returns 0 and `FillDoodadSkin` runs

Creatures and characters are the skinned half. Props with a single bone are the rigid half. If the one-second line shows only one of the two stream paths, move the camera and record a new card. Keep that card.

**Must be non-zero**

| Counter | Why this scene |
| --- | --- |
| stream locks | both the rigid lock and the skinned lock |
| stream bytes | both locks rewrite mesh bytes |
| uniform uploads | each batch draws |
| descriptor binds | each batch draws |
| draw count | many M2 batches |

**May be zero:** terrain batch size, grass draws, visibility flushes, `vkAllocateMemory`. `DestroyBuffers` (`M2Shared.cpp:602`) must stay zero on the steady camera. It runs when `AllocInstances` grows (`M2Shared.cpp:771`, called from `CM2SceneRender.cpp:711`). Warmup may grow the pools. The steady line must not.

**Workload**

- Same type-2 entry as outdoor (`CM2SceneRender.cpp:1125`).
- Rigid: `DrawRigidDoodads` checks `m_boneCount == 1` and one shared bone (`CM2SceneRender.cpp:609`, `CM2SceneRender.cpp:620`), then `BufLock` on the vertex stream and the index stream (`CM2SceneRender.cpp:645`, `CM2SceneRender.cpp:669`), then `DrawInstanced` (`CM2SceneRender.cpp:691`).
- Skinned: the fixed-function fallback stream-locks a buffer of `instances * vertexCount` (`CM2SceneRender.cpp:776`) and calls `FillDoodadSkin` (`CM2SceneRender.cpp:795`, `CM2SceneRender.cpp:598`), which calls `CM2Model::s_transformVertices` (`M2Model.cpp:3101` is the C++ skin). `CM2Model::SetVertices` is the other stream-lock skin (`M2Model.cpp:2700`).
- `M2BatchDoodads` stays `1` so batches stay combined (`Client_CVar.cpp:700`, flag `0x20` at `Client_CVar.cpp:717`).

## Interior — portal-heavy WMO

Ironforge-style means a WMO the camera is inside, with several portal groups, not a doorway looked at from the road. The operator's WMO file supplies the portals. `worldport` by area name can put the character on a `WorldSafeLocs` point if their DBC has a matching `m_areaName` (`Client_Console.cpp:190`). Walk inside, face a doorway so more than one group is visible, then record the numeric card from that interior spot.

The interior branch is `CWorldScene::s_viewerMapObjDef` (`CWorldSceneRender.cpp:104`). `LocateViewer3` sets it (`World.cpp:2974`). `CMapObjDef::RenderWithCallback` calls `RenderInterior` (`WorldScene.cpp:175`, `MapObj.cpp:1699`).

**Must be non-zero**

| Counter | Why this scene |
| --- | --- |
| uniform uploads | group draws and interior doodads |
| descriptor binds | those draws |
| draw count | WMO groups plus interior doodads |
| stream locks | interior doodad sections still take the CPU path |

The portal cover loop must run. That loop is not a Campaign 0 field. Counter samples it at the site below if the line grows a field. Until then, the operator rejects a pose whose view is a single closed room: `RenderThruPortals` returns before the loop when `s_pendingPortalViews` is empty (`MapObj.cpp:1312`).

**May be zero:** terrain batch size, grass draws, `FlushQueuedVisibility`, `vkAllocateMemory`.

**Workload**

- `RenderInterior` walks the viewer's groups and calls `RenderThruPortals` (`MapObj.cpp:1728`, `MapObj.cpp:1165`).
- The pending-view × covered-rect loop is `MapObj.cpp:1316`.
- After the portal branch, `CullMapObjDefGroup` calls `CullDoodadsInterior` (`World.cpp:4171`, `World.cpp:4182`, function at `World.cpp:3996`).
- Those doodads still enter the M2 scene and hit `DrawBatchDoodad` on the shader-off path (`CM2SceneRender.cpp:717`).

## Crowd — nameplates

Same kind of place as the city, or any spot in the operator's world with many other units standing in view. Turn the plates on for this scene only:

```
nameplateShowEnemies 1
nameplateShowFriends 1
```

Defaults are `0` and `0` (`WoW/Source/Ui/GameUI_CVar.cpp:588`, `GameUI_CVar.cpp:592`). `ShouldShowNamePlate` returns before drawing a plate when the matching cvar is off (`WoW/Source/Object/ObjectClient/Unit_C.cpp:20344`). The camera target's own plate is not shown (`Unit_C.cpp:20318`). The crowd is everyone else.

Units idle here. A fight moves the count. Record the visible-unit count on the pose card and keep later runs in that band. The character position and camera still match exactly.

**Must be non-zero**

| Counter | Why this scene |
| --- | --- |
| nameplate units | `s_visibleUnits` after `CollectVisibleUnit` |
| uniform uploads | the world behind the plates still draws |
| descriptor binds | those draws |
| draw count | unit models plus the world |
| stream locks | skinned unit sections take `FillDoodadSkin` or `SetVertices` |

Nameplate units are the watch-list site, not one of the six campaign fields. The crowd scene exists so that count is stable when a later campaign measures `MarkSerialNameplates`. A line with one visible unit is not this scene. `PostAnimateVisibleUnits` only farms the tests out when more than one unit is ready (`WorldFrame.cpp:1584`).

Turn the two nameplate cvars back to `0` before the next outdoor line.

**May be zero:** terrain batch size, grass draws, `vkAllocateMemory`.

**Workload**

- `CGWorldFrame` calls `PostAnimateVisibleUnits` after the world render (`WorldFrame.cpp:1844`, `WorldFrame.cpp:1573`).
- `ClntObjMgrEnumVisibleObjects` fills `s_visibleUnits` through `CollectVisibleUnit` (`WorldFrame.cpp:1575`, `WorldFrame.cpp:1499`).
- `MarkSerialNameplates` is the double loop over that vector (`WorldFrame.cpp:1532`, `WorldFrame.cpp:1548`).
- Ready units call `ShouldShowNamePlate` (`WorldFrame.cpp:1568`) and then `NamePlateAddOrUpdate` (`WorldFrame.cpp:1602`).

## Reject the line

- Any pose card field differs from the previous run.
- Warmup line: `vkAllocateMemory` still climbing, or `DestroyBuffers` still running.
- Outdoor with no grass inside 70 yards, or terrain batch size 0 while the WDT flag bit `0x4` is clear.
- City with only rigid locks, or only skinned locks.
- Interior whose cover loop does not run.
- Crowd whose visible-unit count falls out of the recorded band, or whose nameplate cvars were left on for another scene.
- `gxApi` not Vulkan, or `showDetailDoodads` / `showLowDetail` / `maxLOD` / `farclip` / `groundEffectDist` touched.
