# Interior UI scene

Scene holds the captures still so a later frame matches this one. Same character position and same camera on every run of a given shot (`docs/perf-agent-plan.md:30`). This squad's pictures are the crowd shots below, and one Ironforge-style interior kept for the portal cover test (`docs/perf-agent-plan.md:34`, `docs/perf-agent-plan.md:167`).

A shot passes when the presented frame matches its baseline. `defer` bits are not on screen. A plate that appears, disappears, moves, recolors, or changes depth order fails the crowd shot. A doorway that gains or loses a room, or gains or loses its fog fill, fails the interior shot.

## Freeze

Every shot uses one settled frame.

| Lock | Value |
| --- | --- |
| Window | `gxWindow` 1, `gxMaximize` 0, `gxResolution` `1024x768`, `gxMultisample` 0 |
| UI scale | `uiScale` `1.0` |
| Shaders | `CShaderEffect::s_enableShaders == 0` |
| Clock | Same `FrameTime` sample, so anims and fog land together |
| Player | Same feet, same facing, same level. Camera target is the player |
| Camera | Same `m_position`, facing, FOV, near, far |
| Map | Instance type is not 4, so the 41-yard plate test stays on |
| Pointer | No locked target. No nameplate has mouse focus |

`gxResolution` is parsed as width, separator, height (`WoW/Source/ConsoleDevice/ConsoleDevice_CVar.cpp:464`). `1024x768` is one of the windowed sizes (`WoW/Source/ConsoleDevice/ConsoleDevice_CVar.cpp:499`). `uiScale` registers at `1.0` (`WoW/Source/Ui/GameUI_CVar.cpp:668`). `s_enableShaders` is the fixed-function switch (`Engine/Source/ShaderEffect/ShaderEffect.cpp:34`, `Engine/Source/ShaderEffect/ShaderEffect.cpp:184`). Plates read the camera and the viewport inside `CGWorldFrame::GetScreenCoordinates` (`WoW/Source/Ui/WorldFrame.cpp:383`).

The world frame for the shot is the one that has already run `CWorld::Render`, `PostAnimateVisibleUnits`, and `UnitNameplatesReposition` (`WoW/Source/Ui/WorldFrame.cpp:1842`, `WoW/Source/Ui/WorldFrame.cpp:1844`, `WoW/Source/Ui/WorldFrame.cpp:1851`).

## Crowd

Three shots, same feet and same camera. The only change between them is how many hostile units stand in the bubble. `nameplateShowEnemies` is `1`. `nameplateShowFriends` is `0`. `nameplateAllowOverlap` is `1`. `ShowClassColorInNameplate` is `0`.

Those cvars register as `0`, `0`, `1`, and `0` (`WoW/Source/Ui/GameUI_CVar.cpp:588`, `WoW/Source/Ui/GameUI_CVar.cpp:592`, `WoW/Source/Ui/GameUI_CVar.cpp:596`, `WoW/Source/Ui/GameUI_CVar.cpp:648`). The overlap value selects the anchor path (`WoW/Source/Object/ObjectClient/Unit_C.cpp:20140`).

A shown plate is a `CGNamePlateFrame` left on `s_namePlates` after `ShouldShowNamePlate` (`WoW/Source/Object/ObjectClient/Unit_C.cpp:20293`). The player is the camera target, so the player never gets a plate (`WoW/Source/Object/ObjectClient/Unit_C.cpp:20318`). Everyone else must pass that function: game UI visible (`WoW/Source/Object/ObjectClient/Unit_C.cpp:20306`), hostile so the enemies cvar admits them (`WoW/Source/Object/ObjectClient/Unit_C.cpp:20344`), creature type other than 8 or 12 (`WoW/Source/Object/ObjectClient/Unit_C.cpp:20352`), and inside 41 yards of the player. The distance check is `1681.0f` and it stays active because the map is not instance type 4 (`WoW/Source/Object/ObjectClient/Unit_C.cpp:20398`). The name point is `GetNamePosition` plus `0.6666667` on Z, then the viewport projection, and that projection has to succeed (`WoW/Source/Object/ObjectClient/Unit_C.cpp:20014`, `WoW/Source/Object/ObjectClient/Unit_C.cpp:20392`).

`MarkSerialNameplates` walks the visible-unit vector (`WoW/Source/Ui/WorldFrame.cpp:1532`). The parallel prepare runs only when more than one unit is non-deferred (`WoW/Source/Ui/WorldFrame.cpp:1584`). Deferred units, and every unit when that count is one or zero, go through `PostAnimate` (`WoW/Source/Ui/WorldFrame.cpp:1596`, `WoW/Source/Object/ObjectClient/Unit_C.cpp:799`).

### 0 plates

No other living unit is inside the bubble. `ClntObjMgrEnumVisibleObjects` collects an empty plate list (`WoW/Source/Ui/WorldFrame.cpp:1575`). The frame shows the world and the rest of the UI, and no nameplate widget.

### 40 plates

Forty hostile units, all idle, all loaded, all inside the bubble, all projecting inside the viewport.

| Units | Why they are in the shot |
| --- | --- |
| 36 | Unique `m_worldModel`. `NameplateModelStaysSerial` is false, so they take the parallel path (`WoW/Source/Ui/WorldFrame.cpp:1516`) |
| 2 | One shared `m_worldModel` pointer. The pair loop marks both deferred (`WoW/Source/Ui/WorldFrame.cpp:1548`). Both still plate through `PostAnimate` |
| 1 | `m_attachParent` or `m_attachList` is set, so it stays serial (`WoW/Source/Ui/WorldFrame.cpp:1521`). It still plates |
| 1 | Creature classification 1 or 2, unique model, so the elite icon is shown (`WoW/Source/Ui/NamePlateFrame.cpp:134`) |

The elite unit is one of the 36 unique models, not a fourth extra body. Names are distinct. Health fractions are distinct and frozen. Levels sit within 9 of the player, and none of these units is a boss, so the level widget is the number (`WoW/Source/Ui/NamePlateFrame.cpp:387`). At least two plates overlap in screen space.

### 100 plates

The same 40, plus 60 more unique loaded hostile units on the parallel path. Same feet, same camera, same bubble. The original 40 stay where they were. Overlaps get denser around the screen center used for sort, `(0.4, 0.3)` (`WoW/Source/Object/ObjectClient/Unit_C.cpp:20022`).

`ready` is well above 1, so `PrepareNameplateRange` runs (`WoW/Source/Ui/WorldFrame.cpp:1557`).

### What matches

Each plate is a frame `0.1` by `0.025` (`WoW/Source/Ui/NamePlateFrame.cpp:59`, `WoW/Source/Ui/NamePlateFrame.cpp:484`). With overlap on, the top of the plate is anchored to the world frame's bottom-left at the projected `(x, y)`, and the frame depth is `position.z - m_layoutDepth` (`WoW/Source/Object/ObjectClient/Unit_C.cpp:20142`). `position.z` is the projected depth from `GetScreenCoordinates` (`WoW/Source/Ui/WorldFrame.cpp:398`). Overlapping plates stack by that depth.

On each plate, against this freeze:

- Name string from `GetUnitName` (`WoW/Source/Ui/NamePlateFrame.cpp:519`).
- Health fill for that unit, and health color from `UpdateColorDisplay`. Class color is off, so a hostile plate uses `s_hostileColor` (`WoW/Source/Ui/NamePlateFrame.cpp:358`, `WoW/Source/Ui/NamePlateFrame.cpp:37`).
- Level number and its difficulty color (`WoW/Source/Ui/NamePlateFrame.cpp:378`).
- Elite icon on the one classification 1 or 2 unit, hidden on the others (`WoW/Source/Ui/NamePlateFrame.cpp:134`).
- Raid icon hidden. No unit has a raid index below 8 (`WoW/Source/Ui/NamePlateFrame.cpp:162`).
- Cast bar hidden. Nobody is casting, so `OnLayerUpdate` takes `HideCastBar` (`WoW/Source/Ui/NamePlateFrame.cpp:321`).
- Glow hidden. `m_damageFeedbackTime` stays 0 (`WoW/Source/Ui/NamePlateFrame.cpp:610`, `WoW/Source/Ui/NamePlateFrame.cpp:250`).
- Targeting flash hidden. Threat status stays at 1 (`WoW/Source/Ui/NamePlateFrame.cpp:282`).
- Frame alpha `0xFF`. There is no locked target (`WoW/Source/Ui/NamePlateFrame.cpp:263`).

The 0-plate frame has none of these widgets. The bodies those plates belong to match the baseline of that count, because the units are part of the shot.

`NamePlateSort` orders `s_namePlates` by distance to `(0.4, 0.3)`, inserting before the first closer node (`WoW/Source/Object/ObjectClient/Unit_C.cpp:20077`, `Engine/Source/Storm/H/stpl.h:493`). Reposition walks that list (`WoW/Source/Object/ObjectClient/Unit_C.cpp:20135`). The on-screen stack is the depth set during that walk.

## Interior

Used when the portal cover test is promoted. Not a nameplate shot.

One standing position inside a portal-heavy WMO, Ironforge density: the viewer resolves to a map-obj def (`WoW/Source/WorldClient/World.cpp:2974`), `Enable_MapObjs` is on (`WoW/Source/WorldClient/Types.h:50`), and the groups on screen are loaded (`WoW/Source/WorldClient/MapObj.cpp:1132`). `s_renderMode` stays 5, so interiors draw through `InteriorRender` (`WoW/Source/WorldClient/MapObj.cpp:104`, `WoW/Source/WorldClient/MapObj.cpp:1648`). `Enable_Portals` stays clear, so `RenderPortals` does not add the debug overlay (`WoW/Source/WorldClient/Types.h:54`, `WoW/Source/WorldClient/MapObj.cpp:1158`).

Same feet. Two frozen cameras.

| Look | What the exterior depth-0 walk does |
| --- | --- |
| Cover | At least one portal has `side < 0`, so the covered rect is the unit square `(0,0)-(1,1)` (`WoW/Source/WorldClient/MapObj.cpp:1237`). At least one on-screen pending view overlaps it and is dropped (`WoW/Source/WorldClient/MapObj.cpp:1320`) |
| Open | No depth-0 portal has `side < 0`, so no covered rect is recorded. At least one pending view is pushed (`WoW/Source/WorldClient/MapObj.cpp:1328`) |

The exterior walk is `RenderThruPortalsExterior` on a group whose flags include `0x8` (`WoW/Source/WorldClient/MapObj.cpp:1757`, `WoW/Source/WorldClient/MapObj.cpp:2613`). `SetupMapObjDefCull` has set `s_cullStateValid` (`WoW/Source/WorldClient/MapObj.cpp:270`). Pending views are added only from that exterior pass, at depth 0, when the dest flags pass the mask (`WoW/Source/WorldClient/MapObj.cpp:1289`, `WoW/Source/WorldClient/MapObj.cpp:878`). The viewer's own room is the interior pass, `s_interiorPass = 1` (`WoW/Source/WorldClient/WorldScene.cpp:175`, `WoW/Source/WorldClient/MapObj.cpp:1701`), and that pass does not fill covered rects (`WoW/Source/WorldClient/MapObj.cpp:1238`).

One sightline from these cameras recurses through more than `s_maxPortalDepth` portals. That cap is 10 (`WoW/Source/WorldClient/MapObj.cpp:137`, `WoW/Source/WorldClient/MapObj.cpp:1166`).

### What matches

The rooms you can see. `RenderThruPortals` submits a group through `gRenderCallback`, then builds the child frustum from the clipped portal rect (`WoW/Source/WorldClient/MapObj.cpp:1186`, `WoW/Source/WorldClient/MapObj.cpp:1254`). The parent rect clamps `minX`, `maxX`, and `minY`. `maxY` stays the portal's own `maxY`. A degenerate clip is `fequalz` at `0.001` (`WoW/Source/WorldClient/MapObj.cpp:1272`). Groups with flag `0x10000` are skipped in the recursion (`WoW/Source/WorldClient/MapObj.cpp:1172`) and can still be submitted by the post pass when the box survives `FrustumCull` (`WoW/Source/WorldClient/MapObj.cpp:1740`). The room past depth 10 is absent.

Exterior openings. `AddExteriorPortalView` pushes a rect view with `m_vertCount` still 0 (`WoW/Source/WorldClient/MapObj.cpp:812`, `WoW/Source/WorldClient/CPortalView.h:27`). `CreateInversePortalList` subtracts those rects from the full `(0,0)-(1,1)` square (`WoW/Source/WorldClient/World.cpp:3105`). `RenderPortalList` draws the leftovers with interior fog (`WoW/Source/WorldClient/CWorldSceneRender.cpp:202`, `WoW/Source/WorldClient/World.cpp:3209`).

Interior openings that survived the cover test. `PushPortalView` stores them (`WoW/Source/WorldClient/World.cpp:2938`). `RenderPortalList` draws them after the WMO, with outdoor fog, depth test and depth write on, vertices at `z = 1` (`WoW/Source/WorldClient/CWorldSceneRender.cpp:207`, `WoW/Source/WorldClient/World.cpp:3205`, `WoW/Source/WorldClient/World.cpp:3253`). A view with verts is a triangle fan. A view with `m_vertCount == 0` is the rect quad (`WoW/Source/WorldClient/World.cpp:3268`). On the Cover look, a pending view that overlaps the unit square contributes no triangles. On the Open look, those pushed views are present.

Clear and sky. The clear color follows `frustumPortalView.m_maxViewDepth`: interior fog when that depth is negative, otherwise the outdoor fog, the liquid fog, or black (`WoW/Source/WorldClient/CWorldSceneRender.cpp:163`). An interior skybox set during the interior pass is the group with flag `0x40000` (`WoW/Source/WorldClient/MapObj.cpp:1182`, `WoW/Source/WorldClient/CWorldSceneRender.cpp:211`). Sky draws from `s_unk_ADF570` when the merged depth is non-negative and the camera is not in liquid (`WoW/Source/WorldClient/CWorldSceneRender.cpp:217`).

Doodads and entities culled with the groups that made it through (`WoW/Source/WorldClient/World.cpp:4139`). Fog colors come from `DayNight::GetInfo` (`WoW/Source/WorldClient/DayNight__DNInfo.h:17`), so the frozen clock is part of this match.
