# Multithreading benchmark record

Historical synthetic samples and methodology are retained in
`../multithreading-audit/benchmarks.html`. They show substantial run-to-run variation
and do not justify adaptive chunking. The benchmark uses actual internal
Publish/Wait for chunk experiments, which differs from the public ParallelFor path.
No new performance improvement is claimed for exception-boundary handling.

Current iteration results and host/compiler details will be recorded after execution.
Representative client FPS, 1% lows, frame P95/P99, hybrid-core and NUMA measurements
remain outstanding; synthetic batch latency is not a substitute for frame timing.
