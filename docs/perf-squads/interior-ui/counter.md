# Interior UI counters

Watch list in `docs/perf-agent-plan.md`: nameplates, portal cover test. Counters and `OsGetAsyncTimeClocks` zones only. `defer`, `PushPortalView`, and both loop orders stay as they are.

Compile out under the Campaign 0 flag, default off. Relaxed atomics on the calling thread. Same one-second console line. Field names below stay fixed. Zero each field after the line prints.

`OsGetAsyncTimeClocks` (`Engine/Source/Os/Linux/TimeManager.cpp:59`) is `CLOCK_MONOTONIC` nanoseconds when the timing method is `SystemMethod2`. Store the raw delta. Scale with `OsGetAsyncClocksPerSecond` once on the log line.

## Fields

| Field | What is added |
| --- | --- |
| `iu_visible_units` | `count` once per `MarkSerialNameplates` entry |
| `iu_visible_units_max` | Highest `count` in the second |
| `iu_nameplate_passes` | 1 per `MarkSerialNameplates` entry (divisor) |
| `iu_nameplate_compares` | 1 per inner-loop comparison |
| `iu_nameplate_clocks` | Clock delta around `MarkSerialNameplates` |
| `iu_portal_pending_x_covered` | Pending views × covered rects, once per cover pass |
| `iu_portal_cover_passes` | 1 per cover pass (divisor) |
| `iu_portal_cover_clocks` | Clock delta around the cover loops |

## Visible units

`s_visibleUnits` is filled, then passed straight into `MarkSerialNameplates`.

- `WoW/Source/Ui/WorldFrame.cpp:1497` — `s_visibleUnits`
- `WoW/Source/Ui/WorldFrame.cpp:1844` — frame calls `PostAnimateVisibleUnits`
- `WoW/Source/Ui/WorldFrame.cpp:1573` — `PostAnimateVisibleUnits`
- `WoW/Source/Ui/WorldFrame.cpp:1574` — `s_visibleUnits.clear()`
- `WoW/Source/Ui/WorldFrame.cpp:1575` — `ClntObjMgrEnumVisibleObjects`
- `WoW/Source/Ui/WorldFrame.cpp:1576` — `MarkSerialNameplates(s_visibleUnits)`
- `WoW/Source/Ui/WorldFrame.cpp:1533` — `count = units.size()`

`iu_visible_units` and `iu_visible_units_max` sample that `count`. Crowd scene (many nameplates).

## Comparisons inside `MarkSerialNameplates`

`WoW/Source/Ui/WorldFrame.cpp:1532`–`1555`.

- `WoW/Source/Ui/WorldFrame.cpp:1541` — outer index `i` over visible units
- `WoW/Source/Ui/WorldFrame.cpp:1544` — a deferred unit, or a null model, skips the inner loop
- `WoW/Source/Ui/WorldFrame.cpp:1548` — inner index `j` from `i + 1` to `count`
- `WoW/Source/Ui/WorldFrame.cpp:1549` — `!units[j].defer && units[j].unit->m_worldModel == model`

`iu_nameplate_compares` increments once each time execution reaches `WorldFrame.cpp:1549`. The serial-stay walk at `WoW/Source/Ui/WorldFrame.cpp:1535`–`1539` (`NameplateModelStaysSerial`) is its own pass and is left out of this counter.

Zone: `OsGetAsyncTimeClocks` on entry to `MarkSerialNameplates` (`WorldFrame.cpp:1532`) and at the closing brace (`WorldFrame.cpp:1555`). Add the raw delta to `iu_nameplate_clocks`.

The mark runs on the caller, before `Job::ParallelFor` at `WoW/Source/Ui/WorldFrame.cpp:1590`. The counter stays on that caller. `PrepareNameplateRange` (`WorldFrame.cpp:1557`) is outside the zone.

## Portal pending views × covered rects

`CMapObj::RenderThruPortals` at `WoW/Source/WorldClient/MapObj.cpp:1165`.

- `WoW/Source/WorldClient/MapObj.cpp:1194` — `s_pendingPortalViews` (function-static, shared by every `CMapObj`)
- `WoW/Source/WorldClient/MapObj.cpp:1195` — `s_coveredRects`
- `WoW/Source/WorldClient/MapObj.cpp:1197`–`1199` — both cleared when `!s_interiorPass && !depth`
- `WoW/Source/WorldClient/MapObj.cpp:1238`–`1244` — full-screen covered rect pushed when `side < 0` on that exterior depth-0 walk
- `WoW/Source/WorldClient/MapObj.cpp:1289`–`1290` — `AddInteriorPortalView` pushes a pending view when `!depth && s_cullStateValid && !(destFlags & 0x140)`
- `WoW/Source/WorldClient/MapObj.cpp:1312` — return unless `depth == 0` and the pending list is non-empty
- `WoW/Source/WorldClient/MapObj.cpp:1316` — pending-view loop
- `WoW/Source/WorldClient/MapObj.cpp:1320`–`1325` — covered-rect scan; overlap at `1323` breaks at `1324`
- `WoW/Source/WorldClient/MapObj.cpp:1333`–`1334` — both arrays cleared after the test

At `MapObj.cpp:1316`, before either loop, add `s_pendingPortalViews.Count() * s_coveredRects.Count()` to `iu_portal_pending_x_covered` and add 1 to `iu_portal_cover_passes`. The `break` at `MapObj.cpp:1324` can leave executed overlap tests below that product. The counter is the product.

The arrays fill from the exterior entry. `RenderThruPortalsExterior` sets `s_interiorPass = 0` at `WoW/Source/WorldClient/MapObj.cpp:1760` and calls depth 0 at `MapObj.cpp:1767`. `RenderInterior` sets `s_interiorPass = 1` at `WoW/Source/WorldClient/MapObj.cpp:1701` and calls depth 0 at `MapObj.cpp:1728`, which leaves both arrays unfilled. A non-zero product is the exterior portal pass into a portal-heavy WMO (Ironforge).

Zone: `OsGetAsyncTimeClocks` immediately before `MapObj.cpp:1316` and immediately after the pending loop closes at `MapObj.cpp:1331`. Add the raw delta to `iu_portal_cover_clocks`. One pair of atomics covers every WMO. The recursive calls at `MapObj.cpp:1308` return at `MapObj.cpp:1312` while `depth != 0`, so they stay out of this zone.
