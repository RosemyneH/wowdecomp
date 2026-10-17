# Surfaces

A WASM addon can draw, hook a global, and host a Wayland client in a frame.

| Kind | Call | Permission | What you get |
|---|---|---|---|
| Canvas | `ui_spawn` `"canvas"`, then `canvas_write` | `ui.create` | A frame whose texture is the RGBA you upload |
| Model | `ui_spawn` `"model"`, then `model_set` | `ui.create` | A `PlayerModel`. `unit:player`, a creature id, or a model path |
| Hook | `hook_bind("CastSpellByName")` | `ui.hook` | `hooksecurefunc`. The addon receives `WASM_HOOK` with the function name |
| Wayland | `surface_open(spec, width, height)` | `surface.host` | A draggable frame with a close button. The client is inside the frame |

`surface.host` and `ui.hook` stay denied until `wasmgrant`.

## Wayland frames

The game opens a private Wayland socket, `wowdecomp-N`, and the program connects to that instead of the desktop compositor. The pixels are a UI texture. Drag the frame, or close it, the same way you close a bag. It is not an overlay on the desktop.

| Spec | Launches |
|---|---|
| `https://…` or `http://…` | Chromium, software Wayland, app mode, that URL |
| `html:https://…` | The same |
| `app:foot` | That program, looked up on `PATH` as `/usr/bin/<name>` if you do not give a path |
| `app:/usr/bin/chromium --app=https://example.com` | The argv you wrote, with `WAYLAND_DISPLAY` set |

Chromium is started with `--ozone-platform=wayland` and `--disable-gpu`, so the page is a shared-memory buffer the frame can show. Keyboard input goes to the frame after you click it, and stops going to the game until you click away. The pointer follows the cursor over the page.

Linux is the host that has this. Other builds return an error from `WaylandFrame_Open`. The `WowdecompDesk` addon adds chat commands:

| Command | What it does |
|---|---|
| `/chrome` | YouTube, 960×540 |
| `/chrome example.com 800 500` | That page. A bare host gets `https://` |
| `/app foot` | That Wayland program, looked up on `PATH` |
| `/ping` | Toggle the ping readout and print KB/s |

The first world ping is about 30 seconds after you enter the world. Until a pong comes back the readout stays `ping --`. Lua can call the same function without the addon:

```lua
WaylandFrame_Open("https://www.youtube.com", 640, 400)
```

The character-screen description for a WASM package is a colored list, not one sentence. Gold permissions are the ones that need a grant. The panel appears when you hover the row.
