# Scene — Campaign 2, outdoor

Capture is the Campaign 0 outdoor camera: Elwynn / Westfall density, terrain, doodads, and grass. Same character position and same camera every run. Do not move, and do not retune fog distances, light selection, or which chunks enter a batch.

The check is the long run of draws that reuse one fog record and one light set. Upload and bind counts have to follow those changes. The picture has to match the baseline frame at that camera.

## Shared fog and lights

Terrain fog is one device record for a whole batch. `CWorldScene::RenderChunks` writes day-night fog once (`WoW/Source/WorldClient/World.cpp:3765`), then each chunk’s `RenderSetup` calls `SetupGxLights` and `SetupGxFog` (`WoW/Source/WorldClient/MapRenderChunk.cpp:923`). `QueueMultiPass` copies that fog into the capture (`MapRenderChunk.cpp:2549`). `FlushMultiPass` groups chunks only when fog enable, color, start, and end match (`MapRenderChunk.cpp:2596`), calls `ApplyChunkFog` once (`MapRenderChunk.cpp:2621`), and submits the group with one `DrawTerrainBatch` (`MapRenderChunk.cpp:2625`).

That draw packs fog into the 768-byte uniform (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1036`) and the shader fogs every chunk in the group from it (`Engine/Source/Gx/CGxDeviceVulkan/Shaders/Ffp.vert:124`, `Engine/Source/Gx/CGxDeviceVulkan/Shaders/Ffp.frag:48`). Terrain lights do not come from that uniform. `gpuMode == 2` reads the per-chunk block in `GpuDrawItem` (`Ffp.vert:70`). `FillTerrainDraw` and `DrawTerrainBatch` copy that block (`MapRenderChunk.cpp:2507`, `CGxDeviceVulkan.cpp:1521`).

Doodads go the other way. `SetupLighting` writes fog from `m_curLighting` (`Engine/Source/Model2/CM2SceneRender.cpp:222`) through `SetFogParams` / `SetFogEnabled` (`Engine/Source/ShaderEffect/ShaderEffect.cpp:333`, `Engine/Source/ShaderEffect/ShaderEffect.cpp:352`). `SetLocalLighting` calls `SetupGxLights` (`ShaderEffect.cpp:542`, `Engine/Source/Model2/M2Light.cpp:328`). A rigid batch is one `DrawInstanced` (`CM2SceneRender.cpp:691`). `gpuMode == 1` replaces only the world matrix from the item (`Ffp.vert:66`). Lights and fog stay in the uniform (`Ffp.vert:61`, `Ffp.vert:124`).

Grass uses the same device slots. Each detail instance calls `SetupGxLights` and `SetupGxFog`, then draws (`World.cpp:3598`).

`Draw` builds that uniform on every call (`CGxDeviceVulkan.cpp:1358`). A `memcmp` of the 768 bytes decides the memcpy into the 32 MiB ring (`CGxDeviceVulkan.cpp:1377`, `Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:132`). `vkCmdBindDescriptorSets` runs on every draw after that, including when the set and the dynamic offset did not change (`CGxDeviceVulkan.cpp:1442`).

## What must look the same

Compare against the baseline frame at this camera.

- **Fog color and falloff** on terrain chunks that shared a group, and on doodads and grass that shared that `CM2Lighting` fog. Start, end, color, and enable stay the record `ApplyChunkFog` or `SetupLighting` wrote. A chunk that failed the fog compare (`MapRenderChunk.cpp:2596`) keeps its own fog. The shadow draws after the batches re-apply that chunk’s fog (`MapRenderChunk.cpp:2642`) and stay on it.
- **Sun and point lights** on doodads and grass: direction, diffuse, ambient, and attenuation from `m_lights` (`CGxDeviceVulkan.cpp:1002`, `M2Light.cpp:328`). Batches that kept the same lighting shade the same. A material that turns lighting or fog off (`CM2SceneRender.cpp:204`, `CM2SceneRender.cpp:214`) changes that draw.
- **Terrain shading** stays the per-chunk light block in the 448-byte item (`Engine/Source/Gx/CGxDevice/CGxDevice.h:72`, `Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:89`). `misc[1]` stays `2` for that batch (`CGxDeviceVulkan.cpp:1045`, `CGxDeviceVulkan.cpp:1532`). A skipped uniform upload must not move terrain onto the uniform light arrays.
- **Placement.** Chunk worlds and doodad bone worlds stay in the item (`CGxDeviceVulkan.cpp:1518`, `CGxDeviceVulkan.cpp:1491`). The fallback chunk draw and the shadow draw still take the world they set (`MapRenderChunk.cpp:2518`, `MapRenderChunk.cpp:2643`). View and projection stay this camera (`CGxDeviceVulkan.cpp:978`).
- **Textures and the bound set.** A bind skip is legal only when the image views, samplers, and dynamic uniform offset are unchanged (`CGxDeviceVulkan.cpp:1399`, `CGxDeviceVulkan.cpp:1441`). The offset must not alias another draw’s matrices (`CGxDeviceVulkan.cpp:1381`).
- **The 768-byte record.** `VkDrawUniforms` stays 768 bytes, field order matching `FfpUniforms.glsl` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h:22`, `Engine/Source/Gx/CGxDeviceVulkan/Shaders/FfpUniforms.glsl:1`).

## Pass

On this capture, one second of the Campaign 0 log:

| Counter | Counts |
| --- | --- |
| Uniform uploads | Memcpy into the ring (`CGxDeviceVulkan.cpp:1377`) |
| Descriptor binds | `vkCmdBindDescriptorSets` (`CGxDeviceVulkan.cpp:1442`) |
| Draws | `Draw` (`CGxDeviceVulkan.cpp:1183`), including the one inside each `DrawTerrainBatch` (`CGxDeviceVulkan.cpp:1538`) and each `DrawInstanced` (`CGxDeviceVulkan.cpp:1499`) |

**Pass** when uploads and binds scale with state changes, not with draw count.

An upload state change is a difference in the 768 bytes `IBuildUniforms` fills (`CGxDeviceVulkan.cpp:971`), or an invalid cached record (`CGxDeviceVulkan.cpp:1377`). A repeated fog record or a repeated light set is not a change. One terrain group that shares fog is one draw and at most one upload. One rigid doodad batch that shares lights and fog is one draw and at most one upload. The next draw with the same matrices, stages, lights, fog, and `misc` does not upload again.

A bind state change is a new descriptor set or a new dynamic offset (`CGxDeviceVulkan.cpp:1399`, `CGxDeviceVulkan.cpp:1441`). The same set at the same offset does not bind again.

**Fail** the picture if fog color, fog falloff, sun direction, or a chunk or doodad placement differs from the baseline. **Fail** the budget if uploads or binds stay proportional to draw count across those shared-fog terrain groups and shared-light doodad batches.
