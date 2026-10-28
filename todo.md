# Multithreading audit queue

Branch: `mt`; preserve unrelated working-tree changes. Historical investigation is
in `multithreading-audit/{todo,audit,benchmarks}.html` and `nested-dispatch.html`.
Current decisions/evidence: docs/multithreading-audit.md and
docs/multithreading-benchmarks.md. Hypotheses are not confirmed production defects.

## Active

Claim size is sixteen ranges per participant, and `ParallelFor` keeps one runner
for the caller. Engine/runtime validation that needs a client or other hardware
remains blocked below.

## Completed iterations

- [x] Reject 32× worker ranges. One run put the 4096-item skewed median at 0.256 ms,
  but cheap 512- and 4096-item bands moved up by about 0.004 ms with no overlap
  against that run's 8× samples. Sixteen remains the cap.
- [x] Raise the claim factor from 4 to 16 after two in-process runs showed the
  4096-item skewed median fall from about 1.25 ms to about 0.35 ms without moving
  cheap or uniform medians. `ParallelFor` no longer wakes a worker for a range it
  is about to run; one-item medians fell from about 0.006 ms to about 0.0003 ms
  with non-overlapping ranges. Lifecycle, TSan, init-failure injection and the
  caller-terminate path passed.
- [x] Wake only `min(ranges, workers)` workers per batch so a one-item dispatch
  does not disturb the rest of the pool. Lifecycle, init-failure injection,
  TSan, and ASan passed. Back-to-back one-item medians dropped from about
  0.042 ms to about 0.006 ms with non-overlapping ranges. Large skewed batches
  stayed in the same band.
- [x] Publish four ranges per participant so one heavy item is not sealed into
  the same claim as its neighbors. Coverage, terminate, and TSan lifecycle checks
  passed. Within-run 10-worker synthetic medians are recorded; no frame-time claim.
- [x] Fix forbidden pooled caller exceptions leaving a permanently incomplete batch.
  Fail fast on both worker and participating caller via noexcept; preserve no-pool
  exception propagation. Initial explicit catch revised after timing investigation.
- [x] Inventory every Job dispatch/wait/init/shutdown call; document ownership and
  barriers for animation, asynchronous skinning, doodad culling and mapped skinning.
  Transitive runtime ownership validation remains blocked, not proven race-free.
- [x] Audit scheduler publication, memory visibility, completion and lifecycle;
  document serialized main-thread lifecycle/WorkerCount contract and forbidden
  callback reentry. Concurrent API use is unsupported, not made thread-safe.
- [x] Inspect terrain/grass serialized fills and Vulkan/D3D/OpenGL lock contracts;
  rerun shared EmergencyMem regression. This is not actual terrain/backend coverage.
- [x] Add optional reproducible microbenchmark, randomized chunk/order experiments,
  sizes 0/1/8/4096, uniform/uneven/skewed costs, workers 1/2/4/8/10/16, serial checks.
  Record five 2880-sample matrices, including reordered/noexcept and A/A controls.
- [x] Evaluate completion acknowledgments and multiple-in-flight design: retain
  current protocol because acknowledgments protect batch reuse; no representative
  benefit established for handles or work stealing. Claim size was revisited later.
- [x] Build affected Linux libraries and focused targets; run focused tests,
  sanitizers and available cross-compiler/emulation checks. Record platform limits.
- [x] Inspect existing profiler: global worker-unsafe scope state; Job.Join excludes
  prior caller range work and WorldRender interval is not whole-frame timing.

## Verification evidence

- 2026-10-28 chunk-size change, final sources: one `-O2` lifecycle run and one
  TSan lifecycle run passed with no diagnostics. The same claim formula had
  already passed three `-O2` lifecycle runs and the exit reproducer (0 for
  shutdown and fallback, 42 for terminate paths). Default 10-worker benchmark
  wrote 240 samples to `multithreading-audit/results/2026-10-28-chunks-x4.csv`
  with matching serial output. Not a client or multi-worker-count rerun.
- Linux Release Base, Model2, WorldClient and Gx libraries and optional benchmark
  target build successfully. Final focused suite: 15 tests, three repetitions each,
  all pass. Exit omission tests deliberately reproduce termination (not fixed).
- Final noexcept lifecycle: Linux GCC 16.2.1 ASan/UBSan and TSan pass once each.
  Initial LSan sandbox ptrace failure retried successfully outside sandbox.
- GNU Linux ARM64 lifecycle cross-build and QEMU/sysroot run pass once; emulation
  is not native scheduler validation. MinGW Windows x64 lifecycle cross-build and
  Wine lifecycle pass once. Final noexcept MinGW caller/worker exception tests
  return 42 under Wine, fallback control returns 0. Native Windows/MSVC unverified.
- Benchmarks compare cb54f581 baseline, 5bf61799 explicit catch and revised noexcept.
  Explicit-catch uniform four-worker slowdown triggered revision. Revised GCC
  RunRanges normal-path bytes match baseline. Reordered results vary; identical-
  binary control also diverges dramatically, invalidating causal timing conclusions.
  No speedup claimed and no adaptive scheduler change accepted from these results.

## Blocked tasks / prerequisites

- [ ] Live asynchronous skin input/model lifetimes, teardown reentrancy, animation
  attachment/particle ownership, and actual mapped-buffer backend tests require a
  working integrated client and representative model/map workloads. Current
  WowClient build fails in pre-existing local sibling integration at
  `/home/emma/Repos/synastria-sideload/src/sideload.cpp:548`: TYPEMASK_PLAYER undeclared.
  Its uncommitted CMake integration auto-detects the sibling and has no disable
  switch. Left user source unchanged; affected engine libraries do build.
- [ ] Actual startup-fatal/active-resource teardown and production exception
  reachability remain unverified. Isolated exit reproductions do not establish safe
  client resource ordering; do not add a late exit join without that evidence.
- [ ] Representative frame CPU utilization, per-worker tasks/durations, scheduling
  overhead, queue/wakeup latency, total frame time/FPS/1% low/P95/P99 require the
  integrated workload above and suitable instrumentation. Existing profiler cannot
  supply those numbers safely from workers. Do not infer them from microbenchmarks.
- [ ] Controlled performance acceptance/tuning requires a quieter/controlled host:
  identical-binary A/A control failed comparability. No causal regression or speedup
  conclusion is justified from current timing spread.
- [ ] Native Windows x64/MSVC, Windows ARM64, Linux ARM64 and Android ARM64 runtime
  verification requires target hardware/toolchains. Host has MinGW and GNU ARM64
  cross-compilers, Wine/QEMU, but no native target runners; Android SDK only has
  platform-tools, no NDK. Cross-build/emulation results are explicitly limited.
- [ ] Historical AsyncFile/texture and lazy model-table race hypotheses need
  controlled real-resource reproductions and ownership evidence before fixes.

## Git

Created `mt` from cb54f581, preserving main history and unrelated modifications.
Each verified iteration is committed/pushed to origin/mt. No unrelated files staged.

CSV portability follow-up: store raw samples with LF endings and have the runner
write LF explicitly; numeric data unchanged. Diff whitespace check passes.
