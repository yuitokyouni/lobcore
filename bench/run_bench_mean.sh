#!/usr/bin/env bash
# Release 3-run mean via Google Benchmark repetitions.
# Usage: ./bench/run_bench_mean.sh [binary] [min_time]
set -euo pipefail
BIN="${1:-./build-rel/bench/lobcore_bench}"
MIN="${2:-0.05s}"
"$BIN" --benchmark_min_time="$MIN" --benchmark_repetitions=3 \
  --benchmark_report_aggregates_only=true --benchmark_format=json |
  python3 -c '
import json, sys
data = json.load(sys.stdin)
print(f"{'\''Benchmark'\'' :<28} {'\''mean_ns'\'' :>12} {'\''reps'\'' :>4}")
for b in sorted(data["benchmarks"], key=lambda x: x["name"]):
    name = b["name"].split("/")[0]
    mean = b.get("cpu_time", b.get("real_time", 0))
    reps = b.get("repetitions", 3)
    print(f"{name:<28} {mean:12.0f} {reps:>4}")
'
