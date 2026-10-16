# Interior UI — Faith

Watch list in `docs/perf-agent-plan.md`: nameplates (`MarkSerialNameplates`) and the portal cover test (pending views × covered rects in `MapObj.cpp`). Placement math, hole tests, cull thresholds, `VkDrawUniforms`, and `GpuDrawItem` are untouched.

## Nameplates — fail

Units that share a non-null `m_worldModel` must all leave `MarkSerialNameplates` with `defer == 1`.

`WoW/Source/Ui/WorldFrame.cpp:1532`

The first loop (`1536-1538`) sets `defer` when `NameplateModelStaysSerial` returns `1` (`1516-1529`). The share pass is the second loop (`1541-1554`). It skips a unit that is already deferred or has a null model (`1544`). The inner test pairs a later unit only while that unit is still clear (`1549`), then sets both bits (`1550-1551`).

One clear unit beside deferred sharers of the same pointer stays clear:

- The earlier unit was deferred at `1537`. `1544` skips it. The later unit only scans forward, so it never sees that pointer. Its `defer` stays `0`.
- The later unit was deferred at `1537`. `1549` requires `!units[j].defer`, so the pair is ignored. The earlier unit stays `0`.

That unit still runs `ShouldShowNamePlate` in `PrepareNameplateRange` (`1563-1568`) after `PostAnimateVisibleUnits` marks the list (`1576`).

Two units that are both still clear and share a pointer both become deferred (`1550-1551`). A null `m_worldModel` stays clear on every such unit: `NameplateModelStaysSerial` returns `0` (`1517-1518`) and the share pass skips it (`1544`).

Rejected. This scan stops. A quieter variant of the same two loops is out for this cycle.

## Portal cover — fail

A later portal change must skip a pending view only when some covered rect contains that view:

`view.minX >= covered.minX && view.maxX <= covered.maxX && view.minY >= covered.minY && view.maxY <= covered.maxY`

`CMapObj::RenderThruPortals` is the `// OFFSET: 0x007AC060` body (`WoW/Source/WorldClient/MapObj.cpp:1164-1165`). The skip in tree is an overlap test (`1323`):

`view.maxX >= covered.minX && view.maxY >= covered.minY && view.minX <= covered.maxX && view.minY <= covered.maxY`

The inner loop breaks on the first hit (`1323-1325`). `PushPortalView` runs only when every covered rect missed (`1328-1329`). A view that crosses a covered rect and extends past it is dropped.

Rects stored on the back-face path are the unit square (`1239-1244`). A view with `minX < 0` and the rest of the rect inside that square overlaps it (`1323`) and is skipped. Containment fails: `view.minX < covered.minX`. `TransformAndClipVerts` divides by `w` with no NDC clamp (`666-668`), and `ClipVerts` keeps a plane epsilon of `0.0001` (`325-330`), so a projected corner can sit outside `[-1, 1]` before `AddInteriorPortalView` remaps into `view.m_rect` (`900-904`).

Rejected as the predicate to promote. An empty `s_coveredRects` pushes every pending view (`1328`), which is the contain rule with no containing rect.
