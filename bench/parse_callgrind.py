#!/usr/bin/env python3
"""Bucket exclusive callgrind Ir (instrumented CrossSweep region)."""

from __future__ import annotations

import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

LINE_RE = re.compile(r"^\s*([0-9,]+)\s+\(\s*[0-9.]+%\)\s+(.+)$")


def categorize(name: str) -> str:
    n = name.lower()
    if "hashtable" in n or "unordered_map" in n or "_map_base" in n:
        return "locations_"
    if "_rb_tree" in n:
        return "price_map"
    if "deque" in n:
        return "deque"
    if "vector" in n:
        return "trades_vector"
    if "add_limit" in n:
        return "add_limit_body"
    if any(
        k in n
        for k in (
            "malloc",
            "free",
            "_int_malloc",
            "_int_free",
            "malloc_consolidate",
            "operator new",
            "operator delete",
            "pool_resource",
            "memory_resource",
            "do_allocate",
            "do_deallocate",
            "polymorphic_allocator",
        )
    ):
        return "allocator"
    if "memset" in n or "memcpy" in n or "memmove" in n:
        return "memops"
    if any(
        k in n
        for k in (
            "ld-linux",
            "dl-",
            "_dl_",
            "libc_start",
            "below main",
            ":main",
            "rtld.c",
        )
    ):
        return "startup_noise"
    if "libstdc++.so" in n and "???:0x" in n:
        return "allocator"
    return "other"


def parse_annotate(path: Path) -> tuple[int, dict[str, int]]:
    ann = subprocess.check_output(
        ["callgrind_annotate", "--auto=yes", str(path)],
        text=True,
        errors="replace",
    )
    program_total = 0
    buckets: dict[str, int] = defaultdict(int)
    in_fn_section = False
    fn_section_pending = False

    for line in ann.splitlines():
        if "PROGRAM TOTALS" in line:
            m = re.search(r"([0-9,]+)", line)
            if m:
                program_total = int(m.group(1).replace(",", ""))
            continue
        if "file:function" in line:
            fn_section_pending = True
            continue
        if fn_section_pending:
            if line.startswith("---"):
                in_fn_section = True
                fn_section_pending = False
            continue
        if in_fn_section and line.startswith("---"):
            in_fn_section = False
            continue
        if not in_fn_section:
            continue
        m = LINE_RE.match(line)
        if not m:
            continue
        ir = int(m.group(1).replace(",", ""))
        buckets[categorize(m.group(2))] += ir

    return program_total, dict(buckets)


def main() -> None:
    path = Path(sys.argv[1])
    program_total, buckets = parse_annotate(path)
    measured = sum(buckets.values())
    print(f"file: {path.name}")
    print(f"program_total_ir: {program_total:,}")
    print(f"classified_ir: {measured:,} ({100 * measured / program_total:.1f}% of program)")
    print()
    for k in sorted(buckets, key=lambda x: buckets[x], reverse=True):
        v = buckets[k]
        print(f"  {k:16s} {v:12,}  ({100 * v / program_total:5.1f}% of program)")

    alloc = buckets.get("allocator", 0)
    loc = buckets.get("locations_", 0)
    body = buckets.get("add_limit_body", 0)
    print()
    print(f"allocator: {100 * alloc / program_total:.1f}% of program")
    print(f"locations_: {100 * loc / program_total:.1f}% of program")
    print(f"add_limit_body (exclusive): {100 * body / program_total:.1f}% of program")

    import subprocess as sp

    ann_inc = sp.check_output(
        ["callgrind_annotate", "--auto=yes", "--inclusive=yes", str(path)],
        text=True,
        errors="replace",
    )
    add_limit_inc = 0
    for line in ann_inc.splitlines():
        if "lobcore::OrderBook::add_limit" in line and "file:function" not in line:
            m = LINE_RE.match(line)
            if m:
                add_limit_inc = int(m.group(1).replace(",", ""))
                break
    if add_limit_inc:
        print()
        print(f"add_limit_inclusive_ir: {add_limit_inc:,}")
        print(f"allocator as % of add_limit inclusive: {100 * alloc / add_limit_inc:.1f}%")
        print(f"locations_ as % of add_limit inclusive: {100 * loc / add_limit_inc:.1f}%")


if __name__ == "__main__":
    main()
