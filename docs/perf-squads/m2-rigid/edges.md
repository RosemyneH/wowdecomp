# Campaign 1 edges — rigid M2

Static bind (shared vertices, matrices only) is the `DrawRigidDoodads` success path. A `0` from that function means `DrawBatchDoodad` continues at `SetIndices` and skins. Keep that split. `s_enableShaders` and `CM2Cache::m_flags & 8` are different tests.

## Bone count not 1

`M2SkinSection::m_boneCount` is the combo length. `m_boneInfluences` is the skin function index. A static bind requires both: bone count 1 and influences 1.

| Branch | What runs |
| --- | --- |
| `Engine/Source/Model2/CM2SceneRender.cpp:609` | `m_boneCount != 1`, or a zero vertex or index count, returns `0`. Same test rejects a null `data`, `shared`, `cache`, or `section`. |
| `Engine/Source/Model2/CM2SceneRender.cpp:784` | Fallback sets `normalize` when `m_boneCount != 1`, then skips the per-instance scale scan at `:800`. |
| `Engine/Source/Model2/CM2SceneRender.cpp:792` | `skin.influences = m_boneInfluences`. `FillDoodadSkin` at `:603` calls `s_transformVertices[influences]`. |
| `Engine/Source/Model2/M2Model.cpp:3872` | Slot 1 is `TransformVerticesOneBone_*`. Slots 0 and 2–4 blend weights (`:3872`–`:3876`). |
| `Engine/Source/Model2/CM2SceneRender.cpp:325` | `SetupMaterial` forces `GxRs_NormalizeNormals` when `m_boneCount != 1`. The block is skipped for doodads: `:318` requires `m_curType != 2`. |
| `Engine/Source/Model2/CM2SceneRender.cpp:434` | Shader `DrawBatch` uploads `m_boneCount` matrices from `m_boneCombos[i + m_boneComboIndex]`. |
| `Engine/Source/Model2/CM2SceneRender.cpp:744` | Shader doodad path uploads that same combo, once per instance. |

`boneCount == 1` with `m_boneInfluences != 1` still returns success from `DrawRigidDoodads` today (`:609` does not read influences). That success skips the blend in `TransformVertices_*` (`M2Model.cpp:3115`–`:3146`). Static bind stays on the one-bone function only when influences is 1. Any other influence count stays on `FillDoodadSkin`.

## Mixed bone indices

Bone count 1 does not prove one matrix. The rigid scan reads `m_indices.b[0]` on the vertex span and uses that byte as an absolute `m_boneMatrices` index.

| Branch | What runs |
| --- | --- |
| `Engine/Source/Model2/CM2SceneRender.cpp:617` | First vertex's `m_indices.b[0]` is the candidate bone. |
| `Engine/Source/Model2/CM2SceneRender.cpp:620` | Any later vertex with a different `b[0]` returns `0`. |
| `Engine/Source/Model2/CM2SceneRender.cpp:626` | `bone >= m_bones.m_count` returns `0`. |
| `Engine/Source/Model2/CM2SceneRender.cpp:641` | On success every instance pushes `m_boneMatrices[bone]` for that one byte. |
| `Engine/Source/Model2/M2Shared.cpp:1198` | Shader-off load (`!(m_cache->m_flags & 8)`, `:1187`) rewrites `b[k]` to `m_boneCombos[m_boneComboIndex + skinBone]`. |
| `Engine/Source/Model2/M2Shared.cpp:1204` | That load then zeros every `m_boneComboIndex` and sets `m_boneCombos[i] = i` (`:1209`). |
| `Engine/Source/Model2/M2Model.cpp:3169` | Fallback one-bone skin indexes `m_boneMatrices[vertex->m_indices.b[0]]` per vertex. |
| `Engine/Source/Model2/CM2SceneRender.cpp:803` | Fallback normalize reads `m_boneCombos[m_boneComboIndex]`. After `:1204` that is bone 0, which is a different matrix from the rigid scan's absolute index. |

The mixed-index return at `:620` is what keeps those vertices on `:3169`. A static bind that picks `verts[0]` and ignores the rest of the span draws the wrong bone for the rest. Cache flag 8 skips the rewrite at `:1187`; those `b[0]` values stay combo-local. That mode is the shader-flag bail below, and the absolute-index scan must not run there.

## Missing shared buffers

`DrawRigidDoodads` returns before `SetIndices` (`CM2SceneRender.cpp:717` then `:721`). On the success path `m_vertexBuf` and `m_indexBuf` are often still null. A bind uses a buffer only after `SetVertices` / `SetIndices` have returned 1 for this batch's format.

| Branch | What runs |
| --- | --- |
| `Engine/Source/Model2/M2Shared.cpp:1519` | Constructor leaves `m_indexPool`, `m_indexBuf`, `m_vertexPool`, `m_vertexBuf` null. |
| `Engine/Source/Model2/CM2SceneRender.cpp:711` | `AllocInstances` runs before the rigid call. |
| `Engine/Source/Model2/M2Shared.cpp:779` | A capacity step calls `DestroyBuffers`. |
| `Engine/Source/Model2/M2Shared.cpp:602` | `DestroyBuffers` nulls both bufs and both pools (`:610`, `:615`, `:625`, `:630`). |
| `Engine/Source/Model2/M2Shared.cpp:339` | `SetIndices` creates the index pool. `:343` returns `0` when the pool or buf is null. `:351` returns `0` when the lock fails. |
| `Engine/Source/Model2/M2Shared.cpp:389` | `SetVertices` creates the vertex pool. `:393` and `:402` return `0` on the same failures. The shader-on lock failure is `:433`. |
| `Engine/Source/Model2/M2Shared.cpp:398` | `s_enableShaders == 0` fills `M2VertexPNT2` and binds `GxVBF_PNT2` (`:425`). |
| `Engine/Source/Model2/M2Shared.cpp:430` | `s_enableShaders != 0` fills `M2Vertex` and binds `GxVBF_PBNT2` (`:457`). |
| `Engine/Source/Model2/CM2SceneRender.cpp:644` | Today's rigid path does not use those bufs. It stream-locks a 32-byte `GxVBF_PNT` span (`:665`). A null lock returns `0` at `:647`. The index lock returns `0` at `:670`. |
| `Engine/Source/Model2/CM2SceneRender.cpp:630` | `section - m_skinSections` is out of range for an opt-geo section (`M2Scene.cpp:1805` stores `&optGeo->m_skinSections[...]`). That returns `0`. Opt geo keeps its own index buf (`M2Model.cpp:2645`). |

`PNT` is 32 bytes (`Engine/Source/Gx/Buffer.cpp:24`). `PNT2` is 40 (`:39`) and carries two UVs. `PBNT2` is the shader-on record. Binding one format while the fixed-function batch still expects another is a missing buffer: fall through, same as a null lock.

`SetIndices` (`M2Shared.cpp:363`) replicates every section `m_instances` times. Shader-off `SetVertices` (`:406`) writes each vertex once, at its absolute index. Those two layouts match for instance 0 only. `DrawInstanced` replays one mesh (`CGxDevice.cpp:135`). The replicated index range is the chunked doodad draw (`CM2SceneRender.cpp:766`, `:817`), not the rigid matrix list.

## Zero instances

`DrawRigidDoodads` takes `total` (`m_curElement->m_count` at `CM2SceneRender.cpp:710`). The clamped `instances` value is unused on success.

| Branch | What runs |
| --- | --- |
| `Engine/Source/Model2/M2Shared.cpp:1525` | New shared objects start at `m_instances == 0` and `m_maxInstances == 0`. |
| `Engine/Source/Model2/M2Shared.cpp:771` | Grow runs only when `m_instances < count` and `m_instances < m_maxInstances`. Both zero skips the grow and returns `0`. |
| `Engine/Source/Model2/M2Shared.cpp:1150` | `InitializeSkinProfile` sets `m_instances = 1` after the `0x10000 / indexCount` cap (`:1136`). |
| `Engine/Source/Model2/CM2SceneRender.cpp:713` | `instances > total` clamps `instances` to `total`, including `total == 0`. |
| `Engine/Source/Model2/CM2SceneRender.cpp:641` | Rigid still walks `total` matrices and calls `DrawInstanced` with `total` (`:691`). |
| `Engine/Source/Model2/CM2SceneRender.cpp:771` | Fallback loop is `done < total; done += instances`. `instances == 0` and `total > 0` does not advance. |
| `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1479` | `DrawInstanced` returns when `count == 0` or `worlds` is null. |
| `Engine/Source/Gx/CGxDevice/CGxDevice.cpp:127` | The D3D/base path returns when `worlds` is null. `std::vector::data()` after `clear`/`reserve(0)` (`CM2SceneRender.cpp:637`) is that null. |

`total == 0` may return either value: the fallback loop does not enter. `instances == 0` with `total > 0` must not fall through to `:771`. Returning `0` from a missing-buffer check in that state is the infinite loop. Skin load that has not reached `:1150` is that state.

Rigid success also ignores the `AllocInstances` cap. `total` can exceed `m_instances`. That is safe for one streamed mesh plus a matrix list. It overruns a shared index buffer that was replicated only `m_instances` times (`M2Shared.cpp:363`). Chunk with the shader loop (`CM2SceneRender.cpp:733`) when the bound indices are the replicated range.

## Index bias overflow

Skin indices are absolute. The rigid stream is section-local, so it rebases with a `uint16` two's-complement bias. The wrap is the rebase. A signed subtract that is stored without the `uint16` truncation lands off the section.

| Branch | What runs |
| --- | --- |
| `Engine/Source/Model2/CM2SceneRender.cpp:674` | `indexStart` comes from `m_skinData->m_skinSections[sectionIndex].m_indexStart` (file order). |
| `Engine/Source/Model2/CM2SceneRender.cpp:675` | `bias = uint16(-int32(m_vertexStart))`. `m_vertexStart` is `uint16` (`M2Data.h:304`). |
| `Engine/Source/Model2/CM2SceneRender.cpp:678` | `indexOut[j] = uint16(bias + skinIndex)`. Both operands promote to `int`, then the store wraps. |
| `Engine/Source/Model2/CM2SceneRender.cpp:687` | Batch start is `0`, `m_minIndex` is `0`, `m_maxIndex` is `uint16(m_vertexCount - 1)` (`:690`). |
| `Engine/Source/Model2/M2Shared.cpp:361` | `SetIndices` uses the same bias when `sharedVertices` is false. `sharedVertices` true leaves offset `0` (`:355`). |
| `Engine/Source/Model2/M2Shared.cpp:369` | Each instance then does `offset += vertexCount` (or the full vertex count). `offset` is `uint16`, so the add wraps. |
| `Engine/Source/Model2/M2Shared.cpp:372` | The copy's `m_skinSections[i].m_indexStart` becomes the packed start. The file start stays on `m_skinData`. |
| `Engine/Source/Model2/M2Shared.cpp:1136` | `m_maxInstances = 0x10000 / m_indices.m_count` keeps the packed index buffer inside `uint16`. |
| `Engine/Source/Model2/CM2SceneRender.cpp:767` | Shader doodad `m_maxIndex = uint16(instances * vertexCount - 1)` truncates when that product exceeds `65535`. |
| `Engine/Source/Model2/CM2SceneRender.cpp:818` | The CPU doodad batch truncates the same way with `m_vertexCount`. |
| `Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.cpp:3299` | D3D `DrawIndexedPrimitive` consumes `m_minIndex` and `m_maxIndex - m_minIndex + 1`. |
| `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1470` | Vulkan `vkCmdDrawIndexed` uses the index buffer and a vertex offset of `0`. It does not apply `m_minIndex`. |

Apply the bias once, and only for a section-local vertex stream. `SetIndices` has already biased and packed; subtracting `m_vertexStart` again, or reading the packed `m_indexStart` from `m_skinSections` as if it were the file start at `:674`, walks off the section.

Absolute indices plus `m_minIndex == vertexStart` agree across D3D and Vulkan only while `m_maxIndex` still covers the highest index. The rigid batch's `m_maxIndex` is `vertexCount - 1` (`:690`). Pair that with rebased indices. Pair absolute indices with the shared-vertex range in `DrawBatch` (`CM2SceneRender.cpp:481`: `m_minIndex = m_vertexStart`, `m_maxIndex = m_vertexStart + m_vertexCount - 1`).

`m_maxInstances` limits index count, not `instances * m_vertexCount`. The `:369` offset can wrap while the index-count cap still holds. A wrapped offset aliases the next instance's indices.

## Env-map UVs

Two different bits. `m_shader & 0x4000` selects stored `texcoord[1]`. Combiner bit 8 (`(shader >> shift) & 8`) is sphere-map texgen. Static positions stay in model space so the bone matrix and the texgen matrix still compose.

| Branch | What runs |
| --- | --- |
| `Engine/Source/Model2/M2Shared.cpp:646` | `SubstituteSimpleShaders`, when `!(m_data->m_flags & 8)`, sets bit 8 in the high nibble when the coord combo is `> 2` (`:650`). |
| `Engine/Source/Model2/M2Shared.cpp:656` | Coord combo `== 1` sets `m_shader \|= 0x4000`. The flag-8 material path sets the same bit at `:678`. |
| `Engine/Source/Model2/CM2SceneRender.cpp:658` | Rigid stream writes `m_texcoord[0]` only, into one `GxVBF_PNT` slot. |
| `Engine/Source/Model2/CM2SceneRender.cpp:603` | `FillDoodadSkin` calls the transform with texCoord `0`. |
| `Engine/Source/Model2/CM2SceneRender.cpp:179` | `SetBatchVertices` (non-doodad) reads `m_shader & 0x4000`. `!sameEnv` forces `SetVertices(env != 0)` (`:184`) or `CM2Model::SetVertices(..., env != 0)` (`:190`). |
| `Engine/Source/Model2/M2Model.cpp:3155` | `TransformVertices_*` writes `m_texcoord[texCoord]` into the single stream slot. |
| `Engine/Source/Model2/M2Shared.cpp:415` | Shared `PNT2` fill stores `m_texcoord[texCoordIndex]` in slot 0 and the other channel in slot 1. |
| `Engine/Source/Model2/M2Shared.cpp:399` | That fill runs only when `!m_hasData \|\| !m_valid`. A later `SetVertices` with the other channel leaves the first channel in the buffer. |
| `Engine/Source/Model2/CM2SceneRender.cpp:83` | `SetupTextures` forces `envSecond = 0` when shaders are off (`:88`). Shaders on set `envSecond` when `0x4000` and `textureCount == 1` (`:90`), and the texture is bound at stage 1 (`:115`). |
| `Engine/Source/Model2/CM2SceneRender.cpp:118` | Bit 8 calls `SetTexMtx_SphereMap`. Otherwise the stage gets the texture matrix or identity (`:120`, `:122`). This runs before the rigid return (`DrawBatchDoodad` calls `SetupTextures` at `:708`). |
| `Engine/Source/Model2/CM2SceneRender.cpp:187` | `m_flags & 0x40` on the batch selects `SetVerticesMultiSample`. |
| `Engine/Source/Model2/M2Model.cpp:3094` | `TransformVerticesNoUVSelect_cpp` copies both `m_texcoord[0]` and `[1]` into `PNT2`. |
| `Engine/Source/Model2/M2Shared.cpp:1240` | Shader-off load sets batch flag `0x40` when `m_textureCount > 1`. |

`IsBatchDoodadCompatible` (`M2Model.cpp:434`) keys off batch flag `0x10`, so a one-bone env or multi-texture batch still reaches `DrawRigidDoodads`. Success there skips `:190` and `:187`. A static `PNT` fill of `texcoord[0]` drops `0x4000` and drops the second UV that multi-sample keeps. Sphere-map batches can stay on the rigid mesh: texgen is already set at `:125`. Stored-env batches (`0x4000`) and flag-`0x40` batches need the UV path those branches already take.

`m_hasData` is not a channel match. The first `SetVertices` call wins until the buf is invalid.

## Shader flag on

`CShaderEffect::s_enableShaders` already binds shared vertices and uploads bones. The rigid function does not run. Leave that branch on `SetVertices(0)` and the constant upload.

| Branch | What runs |
| --- | --- |
| `Engine/Source/Model2/CM2SceneRender.cpp:717` | `DrawRigidDoodads` is called only when `s_enableShaders` is 0. |
| `Engine/Source/Model2/CM2SceneRender.cpp:730` | Shader doodads call `SetVertices(0)` (`:731`) and upload `m_boneCount` matrices per instance from `m_boneCombos` (`:744`). |
| `Engine/Source/Model2/CM2SceneRender.cpp:763` | Unlock count is `3 * registers` (three constants per matrix). |
| `Engine/Source/Model2/CM2SceneRender.cpp:171` | `SetBatchVertices` returns after `SetVertices(0)` when shaders are on. The env / multi-sample arm at `:179` is the shader-off arm. |
| `Engine/Source/Model2/CM2SceneRender.cpp:431` | `DrawBatch` re-uploads the combo when the model or `m_boneComboIndex` changes. |
| `Engine/Source/Model2/CM2SceneRender.cpp:471` | The draw uses `m_minIndex = m_vertexStart` and the shared index range. |
| `Engine/Source/Model2/M2Shared.cpp:398` | Vertex fill splits on `s_enableShaders`, not on the cache flag. Shader-on writes per-instance bone indices at `:440`. |
| `Engine/Source/Model2/CM2SceneRender.cpp:613` | Inside the rigid function, `m_cache->m_flags & 8` returns `0`. That flag is set from shader targets (`M2Cache.cpp:129`–`:134`), independent of `s_enableShaders`. |
| `Engine/Source/Model2/CM2SceneRender.cpp:613` | `(m_bones.m_count == 1 && m_cache->m_flags & 0x40)` also returns `0`. Flag `0x40` is set when targets are off and SSE is absent (`M2Cache.cpp:138`). |
| `Engine/Source/Model2/CM2SceneRender.cpp:462` | `DrawBatch` treats that same single-bone `0x40` case as shared vertices and loads `m_boneMatrices[0]` (`:479`). |

Turning shaders on must keep `:730` and `:471`. Turning them off must keep the `:613` bails so a one-bone `0x40` model stays on `:462`, and a flag-8 cache stays on the shader vertex layout. Folding `s_enableShaders` into the cache-flag test, or the reverse, sends absolute bones down the combo path or combo-local indices down `m_boneMatrices[bone]`.
