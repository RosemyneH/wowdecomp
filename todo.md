# Multithreading audit queue

Branch: `mt`. Preserve unrelated working-tree changes. Historical source evidence
and measurements remain in `multithreading-audit/{todo,audit,benchmarks}.html`;
those records are prior observations, not fresh verification.

## Active

- [ ] Complete reproducible scheduler measurements and platform verification.

## Completed iterations

- [x] Verify consistent fail-fast behavior for forbidden pooled callback exceptions;
  previously caller exceptions escaped with a permanently incomplete batch.

## Ordered backlog

- [ ] Finish call-site ownership/lifetime audit: animation, asynchronous skinning,
  doodad culling and mapped-buffer skinning; verify teardown before resource release.
- [x] Audit scheduler lifecycle/API concurrency contract, nested dispatch, publication
  and completion invariants; explicitly document serialized lifecycle/WorkerCount access.
  Call-site transitive lifetime verification remains open.
- [ ] Validate terrain/grass mapped-buffer serialization with existing regressions
  and inspect renderer backend contracts.
- [ ] Improve reproducible chunk benchmarks: randomized configurations, worker counts
  1/2/4/8/16 and available CPUs, small/uniform/uneven/skewed loads, repeated samples.
- [ ] Evaluate completion acknowledgments, wakeups, scheduling and multiple in-flight
  batches using measurements; avoid speculative scheduler replacement.
- [ ] Run available client builds, isolated sanitizer tests and platform checks.
- [ ] Document unavailable platform/runtime/frame-time measurements as explicit blockers.

## Completed / evidence

- Existing work includes initialization-failure rollback, serialized lifecycle tests,
  process-exit reproductions and mapped-buffer fixes. Revalidate before final completion.
- Created `mt` from cb54f581; main history unchanged.

## Limitations

- Linux Release isolated targets rebuilt; 15 job/emergency-memory CTests passed
  three repetitions each (45 executions). Caller and worker exceptions returned
  terminate-handler code 42 promptly. Exit negative controls still reproduce omitted-shutdown
  termination; those are not fixes. Full audit remains incomplete.

- Current static inventory covers all four dispatch sites and both skin waits;
  integration ownership caveats are in docs/multithreading-audit.md.
- Linux WowClient build reached the unrelated sibling sideload source and failed
  at sideload.cpp:548 (`TYPEMASK_PLAYER` undeclared). Its local CMake integration
  auto-detects the sibling with no disable switch; left user work untouched.
- Linux lifecycle passed ASan/UBSan outside ptrace sandbox and TSan inside sandbox,
  once each. Initial LeakSanitizer run was blocked by ptrace, not a passing run.
- MinGW Windows x64 and GNU Linux ARM64 lifecycle binaries cross-compiled.
  ARM64 isolated lifecycle passed once under qemu-aarch64-static with GNU sysroot;
  emulation is not native ARM64 scheduler verification.
