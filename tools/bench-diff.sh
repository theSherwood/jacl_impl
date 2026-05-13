#!/usr/bin/env bash
#
# bench-diff.sh — compare two JSONL files produced by test/test_perf.c
#
# Usage:
#   tools/bench-diff.sh BASELINE.jsonl NEW.jsonl
#
# Each input is a stream of one-JSON-per-line records, one per scenario.
# Output is a human-readable table of percentage deltas, baseline → new,
# grouped by scenario. Negative deltas are improvements for time/alloc
# metrics; positive for throughput-style metrics (not yet emitted).
#
# Designed to be cheap and dependency-free beyond python3.

set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 BASELINE.jsonl NEW.jsonl" >&2
    exit 2
fi

python3 - "$1" "$2" <<'PY'
import json, sys, os

baseline_path, new_path = sys.argv[1], sys.argv[2]

def load_jsonl(path):
    out = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            r = json.loads(line)
            out[r["scenario"]] = r
    return out

base = load_jsonl(baseline_path)
new  = load_jsonl(new_path)

# Fields to compare: top-level + delta.* + final.*.
# (metric_path_for_display, getter)
METRICS = [
    ("wall_ns_median",            lambda r: r["wall_ns_median"]),
    ("wall_ns_min",               lambda r: r["wall_ns_min"]),
    ("wall_ns_max",               lambda r: r["wall_ns_max"]),
    ("gc_cycles",                 lambda r: r["delta"]["gc_cycles"]),
    ("gc_total_ns",               lambda r: r["delta"]["gc_total_ns"]),
    ("gc_max_ns",                 lambda r: r["delta"]["gc_max_ns"]),
    ("gc_mark_rounds",            lambda r: r["delta"]["gc_mark_rounds"]),
    ("bytes_allocated",           lambda r: r["delta"]["bytes_allocated"]),
    ("allocs",                    lambda r: r["delta"]["allocs"]),
    ("slow_path_allocs",          lambda r: r["delta"]["slow_path_allocs"]),
    ("tasks_executed",            lambda r: r["delta"]["tasks_executed"]),
    ("steal_attempts",            lambda r: r["delta"]["steal_attempts"]),
    ("steal_successes",           lambda r: r["delta"]["steal_successes"]),
    ("idle_sleep_ns",             lambda r: r["delta"]["idle_sleep_ns"]),
    ("inbox_pops",                lambda r: r["delta"]["inbox_pops"]),
    ("pinned_inbox_pops",         lambda r: r["delta"]["pinned_inbox_pops"]),
    ("current_heap_blocks",       lambda r: r["final"]["current_heap_blocks"]),
]

def fmt_pct(b, n):
    if b == 0 and n == 0:
        return "    —"
    if b == 0:
        return "  +inf%"
    pct = 100.0 * (n - b) / b
    sign = "+" if pct > 0 else ""
    return f"{sign}{pct:6.1f}%"

def fmt_val(v):
    if isinstance(v, (int, float)):
        if abs(v) >= 1e9:
            return f"{v/1e9:.2f}G"
        if abs(v) >= 1e6:
            return f"{v/1e6:.2f}M"
        if abs(v) >= 1e3:
            return f"{v/1e3:.2f}K"
        return str(v)
    return str(v)

base_sha = next(iter(base.values()), {}).get("git_sha", "?")
new_sha  = next(iter(new.values()),  {}).get("git_sha", "?")

print(f"bench-diff: {base_sha} → {new_sha}")
print(f"  baseline: {baseline_path}")
print(f"  new:      {new_path}")
print()

all_scenarios = sorted(set(base) | set(new))
for name in all_scenarios:
    print(f"  [{name}]")
    if name not in base:
        print(f"    (not in baseline)")
        continue
    if name not in new:
        print(f"    (not in new)")
        continue
    b, n = base[name], new[name]
    print(f"    {'metric':<22} {'baseline':>14} {'new':>14}  {'delta':>9}")
    print(f"    {'-'*22} {'-'*14:>14} {'-'*14:>14}  {'-'*9:>9}")
    for label, get in METRICS:
        try:
            bv, nv = get(b), get(n)
        except KeyError:
            continue
        pct = fmt_pct(bv, nv)
        print(f"    {label:<22} {fmt_val(bv):>14} {fmt_val(nv):>14}  {pct:>9}")
    print()
PY
