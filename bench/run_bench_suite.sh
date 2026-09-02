#!/usr/bin/env bash
# Per-benchmark 3-repetition mean (JSON aggregate). Faster than full-suite long runs.
set -euo pipefail
BIN="${1:-./build-rel/bench/lobcore_bench}"
MIN="${2:-0.01s}"
OUT="${3:-/tmp/bench_results.txt}"
: >"$OUT"
mapfile -t NAMES < <("$BIN" --benchmark_list_tests)
for name in "${NAMES[@]}"; do
  echo "== $name ==" | tee -a "$OUT"
  "$BIN" --benchmark_filter="^${name}$" --benchmark_min_time="$MIN" \
    --benchmark_repetitions=3 --benchmark_report_aggregates_only=true \
    --benchmark_format=console 2>&1 | tee -a "$OUT" | tail -3
done
python3 - "$OUT" <<'PY'
import re, sys
text = open(sys.argv[1]).read()
rows = []
for m in re.finditer(r"== (BM_\w+) ==.*?mean\s+([\d.]+)\s+(\w+)", text, re.S):
    rows.append((m.group(1), float(m.group(2)), m.group(3)))
print(f"{'Benchmark':<28} {'mean':>12} {'unit':>4}")
for name, val, unit in rows:
    scale = {"ns": 1, "us": 1e3, "ms": 1e6, "s": 1e9}.get(unit, 1)
    print(f"{name:<28} {val*scale:12.0f} {'ns':>4}")
PY
