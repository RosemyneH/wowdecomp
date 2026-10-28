#!/usr/bin/env python3
"""Linux/GNU wrapped benchmark matrix; run with otherwise idle CPUs."""
import argparse
import csv
import os
from pathlib import Path
import random
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--baseline", required=True)
parser.add_argument("--current", required=True)
parser.add_argument("--output", required=True)
parser.add_argument("--seed", type=int, default=335)
args = parser.parse_args()
cases = [(revision, workers) for revision in ("baseline", "current")
         for workers in (1, 2, 4, 8, 10, 16)]
random.Random(args.seed).shuffle(cases)
with Path(args.output).open("w", newline="") as output:
    writer = csv.writer(output)
    writer.writerow(("revision", "workers", "items", "distribution", "path",
                     "chunks", "sample", "ms_per_batch"))
    for revision, workers in cases:
        executable = str(Path(getattr(args, revision)).resolve())
        result = subprocess.run([executable], check=True, capture_output=True,
                                text=True, timeout=60,
                                env={**os.environ, "WOW_JOB_BENCH_WORKERS": str(workers)})
        lines = result.stdout.splitlines()
        if not lines or not lines[0].startswith(f"workers={workers} "):
            raise RuntimeError("worker override unavailable or incorrect")
        rows = list(csv.reader(lines[2:]))
        if len(rows) != 240 or any(len(row) != 6 for row in rows):
            raise RuntimeError("unexpected benchmark schema/sample count")
        writer.writerows([revision, workers, *row] for row in rows)
        output.flush()
        print(f"{revision} workers={workers}: 240 samples verified", flush=True)
