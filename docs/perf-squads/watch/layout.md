# Watch layout

Design only. Two watch items from `docs/perf-agent-plan.md:168` and `docs/perf-agent-plan.md:171`. They stay off the campaign list until Campaign 0 ranks them. A later writer lands them as two slices. This file does not change code.

Both slices keep the constants below. Bucket counts and cull thresholds do not move.

| Frozen | Value | Where |
| --- | --- | --- |
| Sort buckets | `64` | `WoW/Source/WorldClient/SortTable.h:17` |
| Bucket planes | `64` | `WoW/Source/WorldClient/SortTable.h:18` |
| Render-chunk lists | `24` | `WoW/Source/WorldClient/SortTable.h:21` |
| Exterior / liquid scale | `dist * 0.03f`, reject `bucket >= 64` | `WoW/Source/WorldClient/World.cpp:2254`, `WoW/Source/WorldClient/World.cpp:2312` |
| Chunk / doodad scale | `distance * 0.029999999f`, reject `bucket >= 64` | `WoW/Source/WorldClient/World.cpp:2300`, `WoW/Source/WorldClient/World.cpp:4096` |
| Grow quantum | `256 / sizeof(T)`, floor `8` | `Engine/Source/Storm/H/stpl.h:884` |
| Occlusion far | `World::s_farClip - 33.333332f` | `WoW/Source/WorldClient/CWorldSceneRender.cpp:85` |
| Occlusion near, portal | `frustumPortalView.m_maxViewDepth + 33.333332f` | `WoW/Source/WorldClient/CWorldSceneRender.cpp:132` |
| Occlusion near, outdoor | `-10000.0f` | `WoW/Source/WorldClient/CWorldSceneRender.cpp:146` |
| Pitch gate | `camVec.z` outside `[-0.89999998, 0.89999998]` skips the buffer | `WoW/Source/WorldClient/CWorldOcclusion.cpp:332` |
| Clip columns | `384` | `WoW/Source/WorldClient/World.h:177` |
| Column map | `x * 64.0f - 0.5f`, bias `192`, clamp `[0, 383]` | `WoW/Source/WorldClient/CWorldOcclusion.cpp:389` |
| Segment depth reject | post-multiply `z < 0.027777778f` | `WoW/Source/WorldClient/CWorldOcclusion.cpp:384` |
| Buffer clear / hole punch | `-1000000.0f` / `-1000001.0f` | `WoW/Source/WorldClient/CWorldSceneRender.cpp:98`, `WoW/Source/WorldClient/CWorldOcclusion.cpp:372` |

`TSGrowableArray::CalcChunkSize` stays the growth quantum. The realloc slice changes how a block moves, not how large the next block is.

## 1. `TSFixedArray` trivial realloc

### Today

`TSFixedArray<T>::ReallocData` (`Engine/Source/Storm/H/stpl.h:749`) always passes `SMEM_FLAG_NOREALLOC` (`Engine/Source/Storm/H/storm.h:58`) into `SMemReAlloc` (`Engine/Source/Storm/H/stpl.h:757`).

`SMemReAlloc` (`Engine/Source/Storm/Source/SMem.cpp:74`) returns `nullptr` when that flag is set and the old pointer is non-null (`Engine/Source/Storm/Source/SMem.cpp:85`). The caller then `SMemAlloc`s a new block, placement-copy-constructs the overlap, destroys the old elements, and `SMemFree`s the old block (`Engine/Source/Storm/H/stpl.h:759`). A null old pointer still allocates: the null check in `SMemReAlloc` runs before the flag check (`Engine/Source/Storm/Source/SMem.cpp:79`).

`m_count` is not updated inside `ReallocData`. Callers placement-new the new tail afterward (`Engine/Source/Storm/H/stpl.h:794`). That order stays.

`ReallocAndClearData` (`Engine/Source/Storm/H/stpl.h:732`) is a different contract. It destroys every element, then reallocates raw storage with flags `0` (`Engine/Source/Storm/H/stpl.h:741`). Leave it alone. Do not fold it into the fast path.

### Gate

The fast path is `if constexpr` on both:

- `std::is_trivially_copyable<T>::value`
- `std::is_trivially_destructible<T>::value`

The second trait is already implied by the first. Keep both so a non-trivial destructor cannot ride a bitwise move. The project is C++17 (`CMakeLists.txt:23`). Include `<type_traits>` from `Engine/Source/Storm/H/stpl.h`.

Use the copyable trait, not `std::is_trivial`. `C3Vector` has a user-provided default constructor that zeroes (`Engine/Source/Tempest/C3Vector.h:56`) and no user-declared copy constructor (`Engine/Source/Tempest/C3Vector.h:11`). It is trivially copyable and not trivial. `TSGrowableArray<C3Vector>` is on the `ReallocData` instantiation list (`Engine/Source/Storm/H/stpl.h:747`). Those elements must take the fast path. New slots still run `T()` through the existing placement new (`Engine/Source/Storm/H/stpl.h:796`). The fast path only moves the prefix that is already alive.

Anything that is not trivially copyable stays on today's copy-construct path. Two cases that must not become bitwise:

- `CParticle2_Model` declares its own copy constructor and destructor (`Engine/Source/Services/ParticleSystem2.h:42`). Its `ReallocData` instantiation is `0x0097BBE0<CParticle2_Model>` (`Engine/Source/Storm/H/stpl.h:747`).
- `TSFixedArray`'s copy constructor is user-declared (`Engine/Source/Storm/H/stpl.h:107`) and allocates through `Set` (`Engine/Source/Storm/H/stpl.h:699`). A nested array such as `TSFixedArray<TSGrowableArray<TAXIROUTELINE>>` (`WoW/Source/Ui/TaxiMapFrame.h:51`) would alias `m_data` if it were `realloc`'d. The trait rejects it.

A pointer member with an implicit trivial copy (`CSimpleBatchedMesh`, `Engine/Source/Frame/CSimpleBatchedMesh.h:15`) is trivially copyable. Bitwise move matches the copy constructor. Do not add an ownership heuristic on top of the trait.

### Fast path

Still inside `ReallocData`, after the existing tail-destructor loop (`Engine/Source/Storm/H/stpl.h:752`) and after `m_alloc = count` (`Engine/Source/Storm/H/stpl.h:756`).

1. Call `SMemReAlloc(olddata, sizeof(T) * count, file, line, 0)`. Flags `0`, not `SMEM_FLAG_NOREALLOC`. `SMemReAlloc` already rounds to 8 bytes and calls `realloc` (`Engine/Source/Storm/Source/SMem.cpp:89`). No new allocator.
2. Return that pointer. Do not placement-copy, do not destroy the prefix, do not `SMemFree` the old pointer. `realloc` owns it, including when the address does not change.
3. Do not write `m_count`.

`realloc` preserves the bytes of the live prefix `[0, min(count, m_count))`. For a trivially copyable `T` that is the copy constructor. The tail `[count, m_count)` was already trivially destroyed; a shrinking `realloc` drops those bytes. Spare slots between `m_count` and the old `m_alloc` stay uninitialized storage, which is what the copy path leaves behind too. Callers that read only `[0, m_count)` are unchanged.

Zero-size request (`count == 0`): `SMemReAlloc` asks `realloc` for 0 bytes (`Engine/Source/Storm/Source/SMem.cpp:89`). On this platform that frees the old block and returns `nullptr`, and `SMemReAlloc` returns `nullptr` without `FatalError` because the rounded size is 0 (`Engine/Source/Storm/Source/SMem.cpp:92`). Store `nullptr` and return. Do not fall into the copy-construct arm.

Non-zero request that returns `nullptr`: `realloc` failed and left the old block allocated, then `FatalError` ran (`Engine/Source/Storm/Source/SMem.cpp:93`). If `FatalError` returns, write `olddata` back into `m_data`. Do not allocate a second block and do not copy-construct.

### Slow path

The `else` of the `if constexpr` is the function as it is today:

- `SMemReAlloc(..., SMEM_FLAG_NOREALLOC)` (`Engine/Source/Storm/H/stpl.h:757`)
- on `nullptr`, `SMemAlloc`, placement `new (p) T(olddata[i])`, destroy the old prefix, `SMemFree` (`Engine/Source/Storm/H/stpl.h:759`)

That arm is also the null-old-pointer success of `SMemReAlloc`: a null source allocates before the flag is tested (`Engine/Source/Storm/Source/SMem.cpp:79`), `m_data` is non-null, and the copy loop does not run. Keep that. Do not "clean up" the success branch.

`Add`, `New`, `Reserve`, `GrowToFit`, and `SetCount` keep calling `ReallocData` (`Engine/Source/Storm/H/stpl.h:836`, `Engine/Source/Storm/H/stpl.h:939`, `Engine/Source/Storm/H/stpl.h:985`, `Engine/Source/Storm/H/stpl.h:915`, `Engine/Source/Storm/H/stpl.h:794`). They do not grow a second way.

## 2. Occlusion silhouette cache

### What is rebuilt

`CMapChunk::ClipBufferUpdate` (`WoW/Source/WorldClient/MapChunk.cpp:1302`) builds two 9-vertex outlines and passes each to `CWorldOcclusion::UpdateBufferChunk` (`WoW/Source/WorldClient/MapChunk.cpp:1314`, `WoW/Source/WorldClient/MapChunk.cpp:1327`).

The grid is 9 outer rows. Every row except the last is 9 outer vertices plus 8 inner vertices, so an outer row starts at `row * 17` (`WoW/Source/WorldClient/MapChunk.cpp:259`). The inner 8 are not part of either outline.

| Edge | Predicate | Indices |
| --- | --- | --- |
| Row | `s_activeWorldView.x > camTarget.x` → base `136`, else `0` | `base + i` for `i` in `0..8` (`WoW/Source/WorldClient/MapChunk.cpp:1306`) |
| Column | `s_activeWorldView.y > camTarget.y` → base `8`, else `0` | `base + 17 * i` for `i` in `0..8` (`WoW/Source/WorldClient/MapChunk.cpp:1318`) |

Both compares are strict `>`. Equality stays on base `0`. Row 8 is index `136` (`8 * 17`). Column 8 is the last outer vertex of each row.

`UpdateBufferChunk` does not read `vertexList[i].z`. World position is `vertexList.xy + m_topLeftCoords.xy` and `m_height[index] + m_topLeftCoords.z` (`WoW/Source/WorldClient/CWorldOcclusion.cpp:339`). `Intersect` writes `vertexList.z` from the height array (`WoW/Source/WorldClient/MapChunk.cpp:1690`). The cache copies `x` and `y` from `vertexList` at fill time and never reads `vertexList.z`.

`CWorldOccluder::ClipBufferUpdate` is a 2-point `UpdateBuffer` (`WoW/Source/WorldClient/CWorldOcclusion.cpp:606`). It is not a chunk silhouette. Do not cache it.

### Key

Per chunk, both edges share one key:

| Field | Source |
| --- | --- |
| Quadrant | bit 0 = `s_activeWorldView.x > camTarget.x`, bit 1 = `s_activeWorldView.y > camTarget.y` |
| Height pointer | `CMapChunk::m_height`, the MCVT alias set at token `0x4D435654` (`WoW/Source/WorldClient/MapChunk.cpp:208`) |
| Origin | `m_topLeftCoords` (`WoW/Source/WorldClient/MapChunk.h:83`) |
| 18 height samples | `m_height` at the 9 row indices and the 9 column indices for that quadrant |

Quadrant is its own field. A flat chunk has the same height on every edge, so a height compare alone would reuse the other edge's `xy` after the camera crosses an axis. The pointer is a fast reject. The 18 floats catch an MCVT blob freed and reallocated at the same address. Origin is in the key because it is added into every world position (`WoW/Source/WorldClient/CWorldOcclusion.cpp:339`).

Evaluate the two compares in `ClipBufferUpdate`, where they already live. Do not read `CMapChunk::nearCornerIndex`. That index is packed from the swapped compares `camTarget < s_activeWorldView` (`WoW/Source/WorldClient/World.cpp:2907`) and is also the corner selector at `WoW/Source/WorldClient/World.cpp:4091`. Same booleans today, different owner. The silhouette key stays next to the index bases.

### Record

Owner is `CMapChunk`, inline, next to `m_height` (`WoW/Source/WorldClient/MapChunk.h:102`). Not a `TSFixedArray`, not a heap map, not a new bucket.

```text
C3Vector m_origin;             // 12
float    m_heightKey[2][9];    // 72
C3Vector m_world[2][9];        // 216   pre-multiply world positions
C3Vector m_projected[2][9];    // 216   post-multiply z, post-divide x/y
uint32_t m_matrixGen;          // 4
uint8_t  m_quadrant;           // 1
uint8_t  m_valid;              // 1
```

`524` bytes with the trailing pad to 4. Fixed. No grow, no shrink.

`m_world` is the outline `ClipBufferUpdate` rebuilds: the nine positions each edge would hand to the matrix. `m_projected` matches `s_clipBufferVerts` after the divide (`WoW/Source/WorldClient/CWorldOcclusion.cpp:343`). `z` stays the post-multiply depth. The span test reads that `z` (`WoW/Source/WorldClient/CWorldOcclusion.cpp:384`) and the divided `x`. `s_clipBufferVerts` is a 9-wide scratch (`WoW/Source/WorldClient/CWorldOcclusion.cpp:43`). The cache copies out of it. It does not keep a pointer into it.

`m_valid` starts clear in `CMapChunk::CMapChunk`, where `m_height` is set to null (`WoW/Source/WorldClient/MapChunk.cpp:1160`). `Purge` clears `m_valid` (`WoW/Source/WorldClient/MapChunk.cpp:94`). `Purge` does not null `m_height`; the flag is what stops a stale outline.

### Matrix stamp

The outline key has no matrix in it. Screen position still depends on `s_clipBufferMatrix` (`WoW/Source/WorldClient/World.cpp:2922`). That matrix is rewritten every `CWorldScene::Update` (`WoW/Source/WorldClient/World.cpp:2918`).

Add `CWorldScene::s_clipBufferMatrixGen`, a `uint32_t` next to `s_clipBufferMatrix` (`WoW/Source/WorldClient/World.h:176`), starting at `1`. After both assignments (`WoW/Source/WorldClient/World.cpp:2922` and `WoW/Source/WorldClient/World.cpp:2924`), `memcmp` the new 16 floats against the stored matrix. Bump the generation only when the bytes differ, then store. One compare per frame, not per chunk. A steady camera keeps the generation. A pan inside the same quadrant changes it. This is not a cull threshold.

### Hit and miss

Lookup runs only inside `ClipBufferUpdate`, after the existing pitch gate inside `UpdateBufferChunk` (`WoW/Source/WorldClient/CWorldOcclusion.cpp:332`). Chunks that never enter `ClipBufferUpdate` do not consult the cache.

Hit when `m_valid` is set and quadrant, `m_height`, origin, and all 18 samples match.

| Hit kind | Extra test | Work skipped | Work that still runs |
| --- | --- | --- | --- |
| World | key matches, `m_matrixGen` does not | Index walk and height / `vertexList` gather | Multiply the cached world verts, then splat |
| Projected | key matches and `m_matrixGen == s_clipBufferMatrixGen` | Gather and the 18 matrix multiplies | Splat from `m_projected` |

Miss fills `m_heightKey`, `m_origin`, `m_quadrant`, `m_world`, then projects, copies the projected scratch into `m_projected`, stores the generation, and sets `m_valid`.

The span loop stays one function used by the miss path and both hits. Hole chunks and solid chunks share it. `m_header->m_holes` is read live (`WoW/Source/WorldClient/CWorldOcclusion.cpp:351`). Holes are not part of the key: the outline does not depend on them, and a hole-flag change still takes the punch path (`WoW/Source/WorldClient/CWorldOcclusion.cpp:370`).

`s_clipBuffer` is cleared to `-1000000.0f` every frame (`WoW/Source/WorldClient/CWorldSceneRender.cpp:97`) and composited with `max` (`WoW/Source/WorldClient/CWorldOcclusion.cpp:407`). Do not cache column spans. The 384-wide write still happens on every eligible chunk.

### Who still calls `ClipBufferUpdate`

`CWorldScene::UpdateOcclusion` (`WoW/Source/WorldClient/World.cpp:2592`) is unchanged:

- Hole-free chunks update only when `s_occlusionNearDepth < m_distToCamera < s_occlusionFarDepth` (`WoW/Source/WorldClient/World.cpp:2615`), then unlink.
- The second walk is the chunks that stayed on the list (holes non-zero). Those always call `ClipBufferUpdate` (`WoW/Source/WorldClient/World.cpp:2628`).
- A hole-free chunk is updated at most once. The cache must not add a second call.

The depth test, the `64` sort buckets, and the `0.03f` / `0.029999999f` scales stay at the sites in the table above.

## Writer

Two slices, if Campaign 0 promotes them. `ReallocData` first, silhouette second. Neither slice retunes a number in the frozen table.

`// OFFSET:` behavior stays: trivial elements compare equal to the copy-constructed prefix, non-trivial elements still run their copy constructor and destructor, and a cached outline splats the same columns `UpdateBufferChunk` would have written.

New comments use the repo TextEmoji form.
