# Interior-ui accountant

Static comparison counts for the two watch-list sites. Campaign 0 has no frame log yet, so bytes, allocs, and draws stay blank until that line exists.

## Nameplates — crowd

`MarkSerialNameplates` (`WoW/Source/Ui/WorldFrame.cpp:1532`) is **O(n²)** over visible units. `n` is `s_visibleUnits.size()` after `ClntObjMgrEnumVisibleObjects` (`WoW/Source/Ui/WorldFrame.cpp:1575`). The call is `WoW/Source/Ui/WorldFrame.cpp:1576`.

The quadratic work is the pair loop at `WoW/Source/Ui/WorldFrame.cpp:1541`. The inner scan at `WoW/Source/Ui/WorldFrame.cpp:1548` compares `m_worldModel` with every later unit. Worst case is `n(n−1)/2` pointer compares: no unit already deferred, every model distinct. The serial-model pass at `WoW/Source/Ui/WorldFrame.cpp:1535` is linear.

This is the **crowd** term. A crowd of nameplates makes `n` large. An interior camera keeps the visible-unit list small, so the same function stays cheap there.

## Portal cover — interior

The cover test in `CMapObj::RenderThruPortals` (`WoW/Source/WorldClient/MapObj.cpp:1165`) costs **pending views × covered rects**.

That product is the nested loop at `WoW/Source/WorldClient/MapObj.cpp:1316`. The outer count is `s_pendingPortalViews.Count()`. The inner count is `s_coveredRects.Count()` (`WoW/Source/WorldClient/MapObj.cpp:1320`). Each step is the overlap test at `WoW/Source/WorldClient/MapObj.cpp:1323`. The inner loop breaks on the first hit (`WoW/Source/WorldClient/MapObj.cpp:1324`), so the product is the worst-case number of tests.

Pending views are recorded at `WoW/Source/WorldClient/MapObj.cpp:1290`. Covered rects are recorded at `WoW/Source/WorldClient/MapObj.cpp:1239`. Both loops run only at depth 0 (`WoW/Source/WorldClient/MapObj.cpp:1312`).

This is the **interior** term. A portal-heavy WMO (Ironforge) fills both lists. The crowd scene pays the nameplate term; this product stays empty when the portal walk does not run.

## Which scene pays

| Scene | Cost | Site |
| --- | --- | --- |
| Crowd | O(n²) over visible units | `WoW/Source/Ui/WorldFrame.cpp:1548` |
| Interior | pending views × covered rects | `WoW/Source/WorldClient/MapObj.cpp:1316` |
