# Interior UI loop

Campaign stays open. Both watch items are **NEXT SLICE**.

## Budget

- Nameplate marking is linear in the visible-unit count, and the `defer` bits match the set written by `MarkSerialNameplates`.
- Portal cover stays **NEXT SLICE** until the overlap predicate is proven on the same views that reach `PushPortalView`.

## Nameplates — NEXT SLICE

`MarkSerialNameplates` (`WoW/Source/Ui/WorldFrame.cpp:1532`) is the crowd mark. `PostAnimateVisibleUnits` calls it at `WoW/Source/Ui/WorldFrame.cpp:1576`.

Pass 1 is one walk. `WoW/Source/Ui/WorldFrame.cpp:1535-1538` sets `defer = 1` when `NameplateModelStaysSerial` (`WoW/Source/Ui/WorldFrame.cpp:1516-1529`) is true: a non-null model with an attach parent, an attach list, no scene, or `(m_fFlags & 1) == 0` while `IsLoaded(0, 0)`.

Pass 2 is the pair scan. `WoW/Source/Ui/WorldFrame.cpp:1541-1554` walks `i`, skips a row that is already deferred or has a null `m_worldModel` (`WoW/Source/Ui/WorldFrame.cpp:1544`), then compares `j` from `i + 1` (`WoW/Source/Ui/WorldFrame.cpp:1548`). A later unit with the same pointer and `defer == 0` sets both `defer` bits (`WoW/Source/Ui/WorldFrame.cpp:1549-1551`). Unique non-deferred models still compare every later slot, so the mark is quadratic in visible units.

The deferred set those two passes write:

- `defer` starts at 0 in `CollectVisibleUnit` (`WoW/Source/Ui/WorldFrame.cpp:1509`).
- Every unit `NameplateModelStaysSerial` accepts.
- Every other unit whose non-null `m_worldModel` is shared with another unit that pass 1 left at `defer == 0`.
- A null model stays 0 (`WoW/Source/Ui/WorldFrame.cpp:1517-1518`, `WoW/Source/Ui/WorldFrame.cpp:1544`). Pass 1 does not pull a serial unit’s siblings in by itself: a deferred `j` is skipped (`WoW/Source/Ui/WorldFrame.cpp:1549`).

A linear mark keyed by `CM2Model*` is absent. The budget needs that pass and the same `defer` bits. This item stays **NEXT SLICE**.

Open slice: one pass over the pass-2 candidates (not yet deferred, non-null model). Second sight of a pointer marks the stored first index and the new index. `NameplateModelStaysSerial` stays the first pass.

## Portal cover — NEXT SLICE

`CMapObj::RenderThruPortals` (`WoW/Source/WorldClient/MapObj.cpp:1165`) still tests pending views against covered rects.

- Scratch: `s_pendingPortalViews` and `s_coveredRects` (`WoW/Source/WorldClient/MapObj.cpp:1194-1199`).
- Depth 0, exterior pass, plane side &lt; 0 appends a covered rect of `[0,1] × [0,1]` (`WoW/Source/WorldClient/MapObj.cpp:1237-1245`).
- The same pass enqueues an interior view when `s_cullStateValid` and the dest flags miss `0x140` (`WoW/Source/WorldClient/MapObj.cpp:1289-1290`). `AddInteriorPortalView` stores the screen rect on `view.m_rect` (`WoW/Source/WorldClient/MapObj.cpp:897-907`).
- The cover walk runs only at depth 0 with a non-empty pending list (`WoW/Source/WorldClient/MapObj.cpp:1312-1314`).

The walk is `WoW/Source/WorldClient/MapObj.cpp:1316-1331`: each pending view against each covered rect. The predicate is `WoW/Source/WorldClient/MapObj.cpp:1323`:

`view.m_rect.maxX >= covered.minX && view.m_rect.maxY >= covered.minY && view.m_rect.minX <= covered.maxX && view.m_rect.minY <= covered.maxY`

A hit breaks. `j` still inside `s_coveredRects` drops the view. `j >= Count()` calls `CWorldScene::PushPortalView` (`WoW/Source/WorldClient/MapObj.cpp:1328-1329`). Touching edges count.

That predicate is the pushed set. The tree still uses the nested walk. No proof sits beside it that another structure pushes the same views. Portal work stays **NEXT SLICE**.

## Status

| Item | Call | Evidence |
| --- | --- | --- |
| Nameplates | NEXT SLICE | Pair scan `WoW/Source/Ui/WorldFrame.cpp:1548`. Budget is a linear mark with the same `defer` bits as `WoW/Source/Ui/WorldFrame.cpp:1535-1551`. |
| Portal cover | NEXT SLICE | Pending × covered `WoW/Source/WorldClient/MapObj.cpp:1316-1326`. Predicate `WoW/Source/WorldClient/MapObj.cpp:1323` gates `PushPortalView` at `WoW/Source/WorldClient/MapObj.cpp:1328-1329`. |

**CLOSED:** none.
