# Profile run — accountant input

Captured 2026-10-16T21:02Z UTC. Account **a:a** on `127.0.0.1`. Tree **b158d1a0**.

## Build

- Client: `/home/emma/Repos/wowdecomp/test/dev-335/WowClient64`
- Recent slices: `731e9985` (M2 skin overlaps terrain draw), `cab3ee2f` (ReBAR frame buffers).

## Machine (stderr)

```
vulkan memory: rebar=on heap=24564MiB uploadType=2 localType=1
no GxApi line
```

## Session

```
GRUNT: state: LOGIN_STATE_HANDSHAKING result: LOGIN_OK 
GRUNT: state: LOGIN_STATE_AUTHENTICATED result: LOGIN_OK 
GRUNT: state: LOGIN_STATE_DISCONNECTED result: LOGIN_OK 
weather-tick lines in log: 2
```

## Steady window (`8s` dismounted, post-cvar, `perf stat -p WowClient64`)

```
/home/emma/Repos/wowdecomp/Tools/perf-squad-profile.sh: line 75: perf: command not found
```

## Accountant notes (no `WOW_PERFLOG` yet)

- **ReBAR (`cab3ee2f`).** With `rebar=on`, non-bounce `CreateBuffer` prefers host-visible **device-local** when the type bit allows (`VkContext.cpp:1052-1054`). `vk_alloc` **count** per startup ring is unchanged (still six host-visible rings + stream chunks). **Traffic** may skip the upload heap for those buffers; textures and bounce staging stay on `localType` / `uploadType` (`VkContext.cpp:1047-1060`). Steady `vk_alloc=0` gate in `loop.md` still applies.
- **Skin overlap (`731e9985`).** `M2SkinPrepare` caps scratch at **64 MiB** (`M2Scene.cpp:1839`) and writes **`32 × verts`** per unique `(model, section, texCoord, optimized)` on workers (`M2Scene.cpp:1786`, `Job::Start` at `:1857`). `M2SkinFinish` joins before model draw (`M2Scene.cpp:2396`). Byte formulas in sections above are unchanged; wall time can hide skin under terrain until Campaign 0 prints `zone_m2_skin_ns` and `zone_terrain_ns` on the same line.
- **Next.** Land Campaign 0 (`WOW_PERFLOG=1`) and replace this `perf stat` block with one middle steady `frame scene=...` line per `scene.md`.
