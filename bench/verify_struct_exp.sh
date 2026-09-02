#!/usr/bin/env bash
# 構造変更実験: map 基準 vs 案 A を同一セッション・同一スクリプトで再計測。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
MIN="${1:-0.01s}"
OUT_DIR="${2:-/tmp/struct_exp_verify}"
BASE_REF="${3:-1a100ac}"
SRC="/tmp/lobcore-baseline-src"
mkdir -p "$OUT_DIR"

log() { echo "[verify] $*" | tee -a "$OUT_DIR/session.log"; }

log "=== session ==="
log "date: $(date -Iseconds)"
log "min_time: $MIN"
log "suite: bench/run_bench_suite.sh (repetitions=3, aggregates_only)"
log "cmake: Release -DLOBCORE_BUILD_BENCH=ON"
log "baseline git: $BASE_REF (map+deque @ PR #13)"
log "option A git: $(git rev-parse --short HEAD)"

CMAKE=(cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release -DLOBCORE_BUILD_BENCH=ON -DCMAKE_CXX_COMPILER=g++)

log "=== map baseline (git archive) ==="
rm -rf "$SRC"
mkdir -p "$SRC"
git archive "$BASE_REF" | tar -x -C "$SRC"
cp "$ROOT/bench/run_bench_suite.sh" "$SRC/bench/run_bench_suite.sh"
chmod +x "$SRC/bench/run_bench_suite.sh"
(
  cd "$SRC"
  "${CMAKE[@]}"
  cmake --build build-rel --target lobcore_bench
  ./bench/run_bench_suite.sh ./build-rel/bench/lobcore_bench "$MIN" "$OUT_DIR/map_baseline.txt"
) 2>&1 | tee -a "$OUT_DIR/map_baseline_run.log"

log "=== option A (current tree) ==="
(
  cd "$ROOT"
  "${CMAKE[@]}"
  cmake --build build-rel --target lobcore_bench
  ./bench/run_bench_suite.sh ./build-rel/bench/lobcore_bench "$MIN" "$OUT_DIR/option_a.txt"
) 2>&1 | tee -a "$OUT_DIR/option_a_run.log"

python3 - "$OUT_DIR" <<'PY' | tee -a "$OUT_DIR/session.log"
import re, sys
from pathlib import Path
out = Path(sys.argv[1])

def parse(path):
    text = path.read_text()
    return {m.group(1): float(m.group(2))
            for m in re.finditer(r"(BM_\w+)_mean\s+([\d.]+) ns", text)}

base, opta = parse(out / "map_baseline.txt"), parse(out / "option_a.txt")
print(f"\n{'Benchmark':<28} {'map':>12} {'optA':>12} {'delta':>8}")
for k in sorted(set(base) | set(opta)):
    b, o = base.get(k), opta.get(k)
    if b and o:
        print(f"{k:<28} {b:12.0f} {o:12.0f} {(o/b-1)*100:+7.1f}%")
PY

log "done: $OUT_DIR"
