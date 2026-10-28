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
artificial prefix and slightly slower on uniform. A later in-process study, below,
raised the production factor from four to sixteen after cheap and uniform medians
stayed flat at the higher factors. Serial output checks passed. This is still
synthetic item cost, not animation, skinning, or presented frame time. The earlier
identical-binary control still forbids treating a small cross-process gap as causal.

## Selective worker wakeup

Back-to-back runs on the same host, GCC 16.2.1 `-O2`, 10 workers, seed 335,
five samples. The first binary is `98066590` (four ranges per participant, every
worker woken). The second is the selective wakeup. Raw rows:
`../multithreading-audit/results/2026-10-28-wake-before.csv` and
`2026-10-28-wake-after.csv`. Public `ParallelFor` only, milliseconds per batch,
median [minimum, maximum]:

| Items | Distribution | All workers | Selective |
| --- | --- | --- | --- |
| 1 | uniform | 0.0425 [0.0386, 0.0521] | 0.0057 [0.0054, 0.0063] |
| 1 | uneven | 0.0411 [0.0391, 0.0474] | 0.0055 [0.0054, 0.0062] |
| 1 | skewed | 0.0422 [0.0390, 0.0560] | 0.0059 [0.0057, 0.0060] |
| 8 | uniform | 0.0427 [0.0404, 0.0441] | 0.0208 [0.0194, 0.0225] |
| 8 | uneven | 0.0398 [0.0387, 0.0475] | 0.0198 [0.0185, 0.0572] |
| 8 | skewed | 0.0407 [0.0378, 0.0479] | 0.0204 [0.0187, 0.0217] |
| 4096 | uniform | 0.1798 [0.1660, 0.1919] | 0.1522 [0.1459, 0.1540] |
| 4096 | uneven | 0.2721 [0.2539, 0.2916] | 0.2614 [0.2392, 0.3406] |
| 4096 | skewed | 1.1190 [1.0951, 1.1275] | 1.1004 [1.0932, 1.1272] |

The one-item intervals do not overlap. Eight-item uniform and skewed intervals
do not overlap; the uneven eight-item selective maximum reaches the all-worker
band. The 4096-item skewed and uneven intervals overlap, which matches both
paths still using every worker once the range count exceeds the pool. The
4096-item uniform gap is small and is not treated as a large-job speedup; the
earlier identical-binary control showed that cross-process gaps of this host
can be noise. These figures are synthetic dispatch cost, not frame time.

## Sixteen ranges and caller-owned ParallelFor

`job_policy_bench` forces Publish/Wait chunk counts of 1×, 4×, 8× and 16× workers
on one 10-worker process. Distributions: cheap (16 steps), uniform (200), skewed
(first 1/32 of items at 8000 steps). Sizes 32, 128, 512, 4096. Five samples, 20
batches, seed 335, order shuffled inside each sample. Two runs:
`../multithreading-audit/results/2026-10-28-policy.csv` and
`2026-10-28-policy-repeat.csv`. Median milliseconds per batch:

| Items | Distribution | ~1× | ~4× | ~8× | ~16× |
| --- | --- | --- | --- | --- | --- |
| 4096 | cheap, run 1 | 0.027 | 0.028 | 0.029 | 0.028 |
| 4096 | cheap, run 2 | 0.024 | 0.028 | 0.028 | 0.029 |
| 4096 | uniform, run 1 | 0.179 | 0.160 | 0.160 | 0.155 |
| 4096 | uniform, run 2 | 0.172 | 0.151 | 0.160 | 0.145 |
| 4096 | skewed, run 1 | 1.532 | 1.243 | 0.672 | 0.350 |
| 4096 | skewed, run 2 | 1.552 | 1.251 | 0.678 | 0.362 |
| 512 | skewed, run 1 | 0.203 | 0.163 | 0.107 | 0.066 |
| 512 | skewed, run 2 | 0.203 | 0.168 | 0.104 | 0.075 |

The 4096-item skewed bands do not overlap between 4×, 8× and 16× on either run.
Cheap 4096-item medians stay within a few microseconds. Uniform medians do not
move in a consistent direction; individual samples still spike into the 0.20 ms
range at every factor, including 1×.

Back-to-back public `ParallelFor` on the same host, current pool of 10 workers,
seed 335. Before is `a5af2ff2` (four ranges per participant, caller plus up to
one worker per range). After publishes sixteen ranges per participant and, for
`ParallelFor` only, leaves one runner for the caller. Raw rows:
`2026-10-28-x16-before.csv` and `2026-10-28-x16-after.csv`.

| Items | Distribution | Before | After |
| --- | --- | --- | --- |
| 1 | uniform | 0.0064 [0.0057, 0.0089] | 0.0003 [0.0003, 0.0003] |
| 1 | uneven | 0.0059 [0.0052, 0.0068] | 0.0001 [0.0001, 0.0001] |
| 1 | skewed | 0.0056 [0.0051, 0.0066] | 0.0001 [0.0001, 0.0001] |
| 4096 | uniform | 0.2225 [0.1744, 0.5980] | 0.1522 [0.1483, 0.2517] |
| 4096 | uneven | 0.3450 [0.2857, 0.3673] | 0.2484 [0.2333, 0.2762] |
| 4096 | skewed | 1.1353 [1.1055, 1.1767] | 0.3341 [0.3212, 0.3586] |

One-item intervals do not overlap: that call no longer wakes a worker. The
4096-item skewed and uneven intervals do not overlap. The uniform before-sample
contains a 0.598 ms spike, so that row is not used as a speedup claim. Eight-item
batches stayed near 0.02 ms on both binaries because the item count is already
below the chunk cap. Not frame time.

A follow-up in the same harness replaced the 16× slot with 32× workers (320
ranges at 4096 items). Skewed 4096-item median was 0.256 ms [0.219, 0.295],
below the 16× runs above. Cheap 4096-item median rose to 0.031 ms [0.030, 0.033],
above that run's 8× band of 0.027–0.028 ms, and cheap 512 items showed the same
split. Thirty-two claims per worker are not used: the extra skewed gain comes
with a repeatable cost on short tasks. Raw rows:
`2026-10-28-policy-32.csv`. The fourth column of that file is 32× workers, not 16×.

## Serial crossover

In-process serial call versus public `ParallelFor`, 10 workers, seed 335, five
samples, 20 batches. Item body is the same integer mix as the other benches.
`S` means the two ranges do not overlap and serial is faster. Full rows:
`2026-10-28-serial.csv` and `2026-10-28-serial-large.csv`. Median milliseconds:

| Items | 16 steps serial / parallel | 200 steps | 2000 steps |
| --- | --- | --- | --- |
| 8 | 0.0001 / 0.023 S | 0.0022 / 0.020 S | 0.023 / 0.021 |
| 32 | 0.0004 / 0.025 S | 0.0087 / 0.024 S | 0.092 / 0.036 P |
| 128 | 0.0016 / 0.025 S | 0.035 / 0.029 P | 0.371 / 0.074 P |
| 1024 | 0.012 / 0.029 S | 0.278 / 0.062 P | 2.929 / 0.375 P |
| 2048 | 0.024 / 0.029 S |  |  |
| 4096 | 0.049 / 0.039 P |  |  |

Exterior doodad culling is a sphere test plus a short clip-buffer scan, in the
neighborhood of the 16-step body rather than the 2000-step body. Its parallel
gate is now 4096, the first measured size where 16-step work beat serial.
Heavier call sites (animation, CPU skinning) stay on the pool below that size.
A strided index walk was slower than contiguous 16× ranges on the same host
(4096 uniform 0.184 ms versus 0.033 ms) and is not used.

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
