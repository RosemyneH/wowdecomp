# Multithreading benchmarks

## Host and method

2026-10-28: Linux x86_64, AMD Ryzen 5 5600H, 6 cores/12 logical CPUs,
one NUMA node, GCC 16.2.1. Standalone builds use `-std=c++17 -O2 -pthread -I.`.
The benchmark stubs Profiler scopes. Hardware affinity, CPU frequency and external
background activity are uncontrolled; these are synthetic throughput observations,
not engine frame-time evidence.

`job_benchmark` is now an optional CMake target. It checks deterministic integer-chain
output against a serial reference after warmup and every sample. Sizes: 0, 1, 8,
4096. Distributions: uniform 200 steps; uneven 50–650 periodic; skewed first 1/32
8000 steps, rest 50. Twenty batches per timed sample, five samples. Public
ParallelFor is compared with internal Publish/Wait using 1x/2x/4x/8x worker chunks;
these paths differ, so the results do not isolate chunk count alone. Empty jobs use
only public dispatch. Chunk order is shuffled per sample with seed 335.

On Linux/GNU, a benchmark-only linker wrapper supplies hardware_concurrency from
`WOW_JOB_BENCH_WORKERS + 2`, allowing the actual pool to create 1/2/4/8/10/16 workers.
It changes neither affinity nor host CPU availability. It is absent from engine
builds. Sixteen workers oversubscribe this host; heterogeneous cores and NUMA are
not simulated. The matrix runner rejects an absent override or unexpected output.

Reproduction (repository root):

```sh
git show cb54f581:Engine/Source/Base/Job/JobSystem.cpp > /tmp/JobSystem-baseline.cpp
c++ -std=c++17 -O2 -pthread -I. multithreading-audit/job_benchmark.cpp multithreading-audit/job_benchmark_hardware.cpp /tmp/JobSystem-baseline.cpp -Wl,--wrap=_ZNSt6thread20hardware_concurrencyEv -o /tmp/benchmark-baseline
c++ -std=c++17 -O2 -pthread -I. multithreading-audit/job_benchmark.cpp multithreading-audit/job_benchmark_hardware.cpp Engine/Source/Base/Job/JobSystem.cpp -Wl,--wrap=_ZNSt6thread20hardware_concurrencyEv -o /tmp/benchmark-current
python3 multithreading-audit/run_job_benchmarks.py --baseline /tmp/benchmark-baseline --current /tmp/benchmark-current --output /tmp/measurements.csv
```

## Results and regression investigation

Raw CSVs in `../multithreading-audit/results/` each contain 2880 timed samples,
including serial-reference checks. `2026-10-28-chunks.csv` and
`2026-10-28-chunks-clean-repeat.csv` compare cb54f581 with explicit-catch commit
5bf61799; `2026-10-28-chunks-noexcept.csv` compares cb54f581 with the revised
noexcept invocation boundary. Another run overlapped compiler/Wine/QEMU setup and
was discarded; it is not included as evidence.

The explicit catch showed a repeatable slower uniform case at four workers:
public ParallelFor medians baseline/current were 0.244/0.275 ms, then
0.239/0.283 ms. Other configurations varied in both directions. Inspection found
changed register allocation in RunRanges. Replacing the explicit catch with a
noexcept function boundary retains fail-fast correctness and on this GCC build
produces byte-identical 145-byte RunRanges normal-path machine code to baseline.
This removes that normal-path code-generation difference; it does not prove
identical frame-time performance on every platform.

Revised run, public ParallelFor over 4096 items, milliseconds per batch;
median [minimum, maximum] of five samples:

| Workers | Distribution | Baseline | noexcept |
| --- | --- | --- | --- |
| 1 | uniform | 0.576 [0.561, 0.598] | 0.582 [0.566, 0.618] |
| 1 | uneven | 1.039 [1.023, 1.071] | 1.064 [1.047, 1.110] |
| 1 | skewed | 1.569 [1.562, 1.580] | 1.566 [1.565, 1.613] |
| 2 | uniform | 0.382 [0.376, 0.390] | 0.393 [0.386, 0.439] |
| 2 | uneven | 0.712 [0.674, 0.842] | 0.739 [0.726, 0.786] |
| 2 | skewed | 1.524 [1.520, 1.533] | 1.529 [1.523, 1.533] |
| 4 | uniform | 0.266 [0.237, 0.284] | 0.272 [0.238, 0.425] |
| 4 | uneven | 0.421 [0.418, 0.603] | 0.524 [0.420, 0.660] |
| 4 | skewed | 1.519 [1.499, 1.555] | 1.523 [1.498, 1.548] |
| 8 | uniform | 0.248 [0.239, 0.263] | 0.253 [0.240, 0.263] |
| 8 | uneven | 0.446 [0.398, 0.461] | 0.439 [0.413, 0.460] |
| 8 | skewed | 1.478 [1.475, 1.490] | 1.482 [1.478, 1.500] |
| 10 | uniform | 0.216 [0.209, 0.230] | 0.268 [0.209, 0.301] |
| 10 | uneven | 0.365 [0.348, 0.381] | 0.462 [0.358, 1.031] |
| 10 | skewed | 1.550 [1.492, 1.575] | 1.552 [1.494, 1.559] |
| 16 | uniform | 0.199 [0.192, 0.201] | 0.204 [0.195, 0.223] |
| 16 | uneven | 0.307 [0.306, 0.322] | 0.314 [0.304, 0.367] |
| 16 | skewed | 1.514 [1.498, 1.534] | 1.546 [1.522, 1.601] |

The public ParallelFor columns above still use one range per participant, so they
do not show the chunk-count effect. Within those same runs, forced 4× and 8×
ranges did. That comparison is what the later section acts on. Small jobs still
pay publication and all-worker acknowledgment overhead. No work stealing,
independent handles, or completion replacement is justified here.

## Four ranges per participant

2026-10-28, same host, GCC 16.2.1 `-O2`, actual pool of 10 workers
(`hardware_concurrency` 12, no `WOW_JOB_BENCH_WORKERS` override). One process,
seed 335, five samples, 20 batches each. Raw rows:
`../multithreading-audit/results/2026-10-28-chunks-x4.csv`.
`parallel_for` is the production path after the change (44 ranges on 4096 items).
`publish_wait` still forces 1×/2×/4×/8× worker ranges in that same process.
Median [minimum, maximum] milliseconds per batch, 4096 items:

| Distribution | parallel_for (44) | 10 ranges | 20 ranges | 40 ranges | 80 ranges |
| --- | --- | --- | --- | --- | --- |
| uniform | 0.176 [0.159, 0.187] | 0.200 [0.179, 0.251] | 0.178 [0.176, 0.187] | 0.186 [0.165, 0.200] | 0.204 [0.170, 0.233] |
| uneven | 0.271 [0.264, 0.285] | 0.319 [0.282, 0.381] | 0.289 [0.274, 0.347] | 0.270 [0.266, 0.296] | 0.263 [0.252, 0.336] |
| skewed | 1.111 [1.101, 1.142] | 1.551 [1.508, 1.560] | 1.495 [1.488, 1.533] | 1.220 [1.203, 1.266] | 0.650 [0.643, 0.675] |

On this run the skewed 44-range and 10-range intervals do not overlap. Uniform
44-range median is below the 10-range median. Eighty ranges are faster on the
artificial prefix and slightly slower on uniform, which is why production stays
at four ranges per participant. Serial output checks passed (nonzero exit would
have stopped the run). This is still synthetic item cost, not animation, skinning,
or presented frame time. The earlier identical-binary control still forbids
treating a later cross-process rerun as a causal speedup.

## Measurement limits

Historical results remain in `../multithreading-audit/benchmarks.html`. Do not
compare absolute timings across those earlier runs as controlled regressions.
Worker utilization, per-worker range counts, queue delay/wakeup latency, whole-frame
P95/P99, average FPS and 1% lows were not measured by this harness. Existing
Profiler Job.Join excludes caller work before Join, and WorldRender is a scoped
render interval, not full presented frame time. Live client measurements are
blocked by the current unrelated sideload build failure; native target hardware,
Android NDK and representative scenes are also required for remaining validation.

## Reordered repeat and identical-binary control

The noexcept repeat (`2026-10-28-chunks-noexcept-repeat.csv`, matrix-order seed
336) reversed the earlier uniform four-worker result: baseline/current medians
0.279/0.246 ms. Ten-worker uniform was 0.217/0.209 ms; four-worker uneven was
0.447/0.505 ms, which remains a slower observation requiring caution.

To check the environment, `2026-10-28-chunks-aa-control.csv` uses the **same
baseline executable for both revision labels**, matrix-order seed 337. Even
identical binaries diverged dramatically: four-worker uniform medians 1.410/0.333
ms and eight-worker uniform 0.252/1.491 ms. No compiler, client build, Wine or QEMU
commands were intentionally run concurrently with this control. Uncontrolled host
activity/frequency/scheduling makes this environment unsuitable for attributing
these differences to code. This is a failed comparability control, not evidence
that the revised code is faster or slower. Further performance acceptance needs
controlled representative measurements; the change is retained for deterministic
correctness with no added normal-path operations in RunRanges. No scheduler
optimization is shipped from these noisy data.
