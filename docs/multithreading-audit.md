# Multithreading audit

## Architecture and invariants

`Engine/Source/Base/Job/JobSystem.cpp` owns one static pool and one active batch.
Publication holds `Pool::mu`; workers copy callback metadata under that mutex.
An atomic index dynamically assigns contiguous ranges. `Start` chooses up to one
range per worker; `ParallelFor` includes one additional caller range. `Wait` also
claims ranges, then waits for both zero remaining ranges and every worker acknowledgment.
The latter protects metadata reuse even after the last callback finishes. Removing
acknowledgments alone would permit old workers to claim from a new batch.

Dispatch/wait are documented main-thread-only; callbacks cannot call Job APIs or
use the graphics device. Lifecycle concurrency is not established as safe. Empty
or null dispatches do not join prior work. Nonempty dispatches join prior work;
shutdown waits, signals termination, and joins all threads. Callback user data and
all referenced inputs/outputs must survive until that join.

## Confirmed caller exception failure

Location: `JobSystem.cpp`, `RunRanges` callback invocation, remaining decrement and
`Join` predicate; reproduction: `multithreading-audit/job_exit_repro.cpp`,
`caller_exception` mode. A callback throwing on the caller formerly skipped the
remaining decrement and escaped, leaving subsequent Wait permanently blocked.
Reachable through a throwing callback in a pooled ParallelFor; callbacks are
contractually forbidden to throw. Production callback throw reachability is not
proven. Worker exceptions already terminate the process.

Decision: catch exceptions at the pooled invocation boundary and terminate on
both caller and worker threads. Partial output cannot safely be treated as a
successful batch. No-pool synchronous fallback behavior is preserved. Validate
with isolated caller/worker terminate-handler tests, fallback control and lifecycle
regressions. This is deterministic failure handling, not callback exception recovery.

## Remaining scope

Historical transitive call-site notes and hypotheses are in
`../multithreading-audit/audit.html` and `nested-dispatch.html`. Do not treat their
hypotheses as reachable defects. Four dispatch sites currently remain: World.cpp
(culling), M2Scene.cpp (animation and async skinning), CM2SceneRender.cpp (doodad
skinning). Terrain and grass initialize the pool but currently fill mapped buffers
serially. Client teardown/input reentrancy and renderer staging lifetimes require
further validation. No new graphics-thread work or scheduler policy change is made.

Validation: Linux Release targets rebuilt and all 15 job/emergency-memory CTests
passed three repetitions each, including caller and worker terminate-handler code
42 and no-pool exception propagation. No native other-platform verification yet.
