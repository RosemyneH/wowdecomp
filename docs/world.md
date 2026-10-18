# World

The world is still the 3.3.5 scene: ADT chunks, WMOs, M2s, liquids, and the low-detail horizon. This fork changes how far that scene is allowed to extend, and how a few targeted spells pick a point on it.

## Full view

Video Options → Effects has a **Full view** checkbox. It writes the `extendedView` CVar (`0` or `1`). You can set the same CVar from the console.

While the checkbox is on, the world is drawn at **50,000** yards. The stored `farclip` is left alone, and the view-distance slider is disabled. Turning the checkbox off returns the view to that stored distance.

It also changes the related limits:

| Setting | Full view off | Full view on |
|---|---|---|
| `farclip` slider maximum | 1,277 | 50,000 |
| `groundEffectDist` slider maximum | 140 | 220 |
| `horizonFarClip` scale | 4 | 48 |
| `distanceFog` | left on | turned off |
| Horizon Z clip (`extendedHorizonZClip`) | off | on |

Grass and ground clutter still stop at 220 yards. Past that, every chunk inside the far clip would rebuild detail doodads on the CPU.

`farclip` itself still defaults to 350, the retail value. Full view is what unlocks the long range. The horizon mesh is drawn out to `horizonFarClip` times the effective far clip, so the continent continues past the ring of full-detail chunks.

With full view off, the old per-map caps remain. Kalimdor and Eastern Kingdoms stay near 1,277 unless `farClipOverride` is set. Outland and Northrend can go further, and a machine with 1 GB of RAM or less is held to the shorter cap.

## Fog

`distanceFog` is a master enable on the graphics device. The Vulkan shader fogs from the near and far distances currently stored on the device, so a change applies on the next draw. Full view forces this CVar off, because a 50,000-yard linear fog would hide the horizon the checkbox exists to show. Set `distanceFog` back to `1` if you want the fade at long range.

## Ground-targeted spells

`/cast [@cursor]` and `/cast [@playerlocation]` are recognized by the AwesomeWotLK layer. The next cast finishes on the terrain under the mouse, or at the player's feet, through the same terrain-click path a manual click would use. The marker is only for that following cast. If the cast does not happen on the next service tick, the marker is dropped.

The Lua and CVar surface around this lives on the [AwesomeWotLK](awesomewotlk.md) page.

## Camera

Two AwesomeWotLK CVars change the camera, and both apply:

- `cameraIndirectVisibility` `1` skips the collision that normally pushes the camera out of walls and terrain, so the camera can pass through world objects.
- `cameraFov` is the field of view. The value is 1–200. **100 is a right angle** (90°), and that is the default when the cvar is missing or `0`. Values above that widen toward a half turn and are clamped just short of 180°.

`cameraIndirectAlpha` is stored and readable. It does not fade the geometry the camera intersects.

`showPlayer` `0` sets the active player's model alpha to 0. Setting it back to `1` restores alpha 255.
