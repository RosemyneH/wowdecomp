# G1 scene — city card

GPU skin is a picture check. The four scenes stay the performance-plan list, with the same character position and camera on every run:

- Outdoor: terrain, doodads, grass (Elwynn / Westfall density).
- City: many skinned and rigid M2s.
- Interior: portal-heavy WMO (Ironforge).
- Crowd: many nameplates.

G1 is read on the city card. The other three stay the last stills of those cards. This pass adds no harness, no log field, and no fifth pose.

`CShaderEffect::s_enableShaders` stays 0 (`Engine/Source/ShaderEffect/ShaderEffect.cpp:34`). Vulkan leaves every shader target at 0, so this is the fixed-function path. `M2BatchDoodads` stays 1.

## The frame

One city view, three objects, camera locked.

| Look at | Why it is in the frame | Where it draws |
| --- | --- | --- |
| The player, or an NPC with more than one bone that fails `IsBatchDoodadCompatible` | Element type 0. This is the GPU-skin subject. | `Engine/Source/Model2/M2Model.cpp:880`, `Engine/Source/Model2/M2Scene.cpp:2112-2113`, `Engine/Source/Model2/CM2SceneRender.cpp:1160` |
| A one-bone prop (lamp, crate, sign) | `DrawRigidDoodads` returns 1. G1 leaves that function. | `Engine/Source/Model2/CM2SceneRender.cpp:612-630`, `Engine/Source/Model2/CM2SceneRender.cpp:759` |
| A reflective piece whose batch shader has bit `0x4000` | Env texcoord. Sphere-map stage. Stays on the CPU. | `Engine/Source/Model2/CM2SceneRender.cpp:179-190`, `Engine/Source/Model2/CM2SceneRender.cpp:124-125` |

The GPU section on that character is `texCoord == 0`, not optimized, and not multi-sample. Optimized is `m_optGeo` / element flag `4` (`Engine/Source/Model2/M2Scene.cpp:2056`, `Engine/Source/Model2/M2Scene.cpp:2125`). Multi-sample is batch flag `0x40` (`Engine/Source/Model2/CM2SceneRender.cpp:187`). G1 uploads that section once as bind-pose `M2Vertex` / `GxVBF_PBNT2` (`Engine/Source/Model2/M2Data.h:119-126`, `Engine/Source/Gx/Buffer.cpp:90-96`) and blends it in `Ffp.vert` before the existing world matrix.

These stay on `TransformVertices_*` and still have to look right. They are not the proof that the GPU path is blending:

- Env batches, `texCoord != 0` (`Engine/Source/Model2/M2Model.cpp:3191`, texcoord write at `Engine/Source/Model2/M2Model.cpp:3625-3626`).
- Optimized multi-section and multi-sample batches.
- Multi-bone doodads, `FillDoodadSkin` (`Engine/Source/Model2/CM2SceneRender.cpp:598`).
- The other skin jobs in `M2Scene.cpp`.

A one-bone model takes the shared-vertex world matrix (`Engine/Source/Model2/CM2SceneRender.cpp:462`) and is not this character.

Stand still. Idle, or an emote that does not translate the root. Hands off the mouse.

## Skinned character animates

The body follows the idle through one cycle. A second body batch of the same model (shoulders, boots, hair) holds the same pose. `DrawBatch` on the shaders-off path draws with `m_minIndex == 0` (`Engine/Source/Model2/CM2SceneRender.cpp:489-492`). G1 calls `GpuSkinBind` on that draw even when `SetBatchVertices` skipped the upload, then clears the arm.

The stored mesh is bind pose. The pose on screen is the bone palette for this frame, then `u.world`. World at the start of the M2 pass is identity (`Engine/Source/Model2/CM2SceneRender.cpp:1110-1113`). Placement lives in `m_boneMatrices`. Blend order is `position * bone`, then that world matrix.

A frame that matches the D3D still of this card is a good blend. A body frozen in the rest pose is a bind-pose upload whose palette never ran.

## Rigid doodad is unchanged

The one-bone prop stays on `DrawRigidDoodads`. The vertex buffer is the untransformed section (`m_position`, `m_normal`, `m_texcoord[0]`, stride 32) and each copy is `m_boneMatrices[bone]` (`Engine/Source/Model2/CM2SceneRender.cpp:644-646`, `Engine/Source/Model2/CM2SceneRender.cpp:679-687`). Place, silhouette, and shade match the last city still. Copies do not shear on the frames the character changes pose.

## Env-mapped piece still matches

The `0x4000` batch is `SetVertices` with `texCoord != 0` (`Engine/Source/Model2/CM2SceneRender.cpp:190`). That write stays on the CPU and stores `m_texcoord[texCoord]`. The stage is sphere map (`Engine/Source/Model2/CM2SceneRender.cpp:125`). On the locked camera the highlight sits on the same texels as the last still, and it stays on the limb it belongs to.

## D3D build is untouched

G1 does not edit the D3D device. `GpuSkinSupported` is 1 only on Vulkan. D3D9 and D3D9Ex keep the default `GpuSkinBind` of 0, so `SetVertices` still calls `TransformVertices_*`. OpenGL uses that same default; its picture is the CPU picture.

Same city card, the existing client, `gxApi` `D3D9` or `D3D9Ex` (`Engine/Source/Gx/Gx.cpp:47`). Restart after the change. The device line is `GxApi: D3D9` or `GxApi: D3D9Ex` (`Engine/Source/Gx/Gx.cpp:983`). Character, prop, and highlight match the last D3D still of this card, and they match the Vulkan frame. Shader targets stay off. There is no new D3D target.

## Misses

| What you see | Where it broke |
| --- | --- |
| Body frozen in the rest pose while the env shell keeps moving | Bind-pose upload, palette not applied (`u.misc[2]` clear, or `GpuSkinBind` returned 0 and the CPU write was skipped) |
| Spikes, a collapsed mesh, or a sheared limb | Blend indices read as unorm, or the multiply is not row-major `position * bone` |
| First geoset posed, the next geoset of that model in the rest pose | `Draw` did not bind when `SetBatchVertices` skipped the upload |
| The rigid prop jumps or folds on the frame the character draws | The skin arm survived onto the next `Draw` |
| Highlight on the diffuse island, or the shell left in the rest pose | The `0x4000` batch took the GPU upload |
| D3D frame moved | Shared `SetVertices` / `DrawBatch` changed the CPU fallback |

## Manual check

1. Use the city card already written for this character and camera. Leave `farclip`, `groundEffectDist`, `M2BatchDoodads`, and the shader flag where that card left them.
2. Vulkan. The view contains the player or a type-0 NPC, one one-bone prop, and one `0x4000` piece.
3. Stand still through one idle cycle. The body and a second batch of that model stay in the anim.
4. The one-bone prop matches the previous city still.
5. The reflective piece matches that still and stays on its limb.
6. Outdoor, interior, and crowd cards: same picture as the last run (terrain and grass, a doorway, nameplates).
7. Existing D3D9 or D3D9Ex client, same city card. The three objects match steps 3–5 and the last D3D still.

Stop. No capture binary, no pixel diff, and no new log field for this pass.

## Other three cards

| Card | Still looks like |
| --- | --- |
| Outdoor | Terrain, grass, and rigid doodads |
| Interior | The WMO doorway and the interior doodads |
| Crowd | Nameplates over the same units |

Terrain, WMO, liquids, and UI stay on their current path. A change on those cards is a leaked skin arm or a vertex-format change.
