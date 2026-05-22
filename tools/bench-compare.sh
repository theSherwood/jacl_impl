#!/usr/bin/env bash
#
# bench-compare.sh — side-by-side comparison of two JSONL bench files
# from different runtimes (e.g. JACL test_perf vs tools/bench-python.py).
#
# Usage:
#   tools/bench-compare.sh JACL.jsonl PYTHON.jsonl
#
# Compares wall_ns_{min,median,max} across runtimes per scenario.
# Scenarios that exist in only one file are noted but not compared.
# A "ratio" column shows new/baseline on wall_ns_median — values >1
# mean the second runtime is slower.

set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 BASELINE.jsonl OTHER.jsonl" >&2
    exit 2
fi

python3 - "$1" "$2" <<'PY'
import json, sys

base_path, other_path = sys.argv[1], sys.argv[2]

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

base = load_jsonl(base_path)
other = load_jsonl(other_path)

def label(records, fallback):
    for r in records.values():
        rt = r.get("runtime")
        if rt:
            return rt
        # JACL test_perf doesn't emit "runtime" — infer from presence of "delta"
        if "delta" in r:
            return "jacl"
    return fallback

base_label = label(base, "baseline")
other_label = label(other, "other")

def fmt_ns(v):
    if v is None:
        return "—"
    if v >= 1e9: return f"{v/1e9:7.2f}s"
    if v >= 1e6: return f"{v/1e6:7.2f}ms"
    if v >= 1e3: return f"{v/1e3:7.2f}µs"
    return f"{v:6d}ns"

def fmt_ratio(b, n):
    if b == 0 or b is None or n is None:
        return "    —"
    r = n / b
    return f"{r:6.2f}x"

print(f"bench-compare: {base_label} vs {other_label}")
print(f"  baseline ({base_label}): {base_path}")
print(f"  other    ({other_label}): {other_path}")
print()

METRICS = ["wall_ns_min", "wall_ns_median", "wall_ns_max"]

shared = sorted(set(base) & set(other))
only_base = sorted(set(base) - set(other))
only_other = sorted(set(other) - set(base))

for name in shared:
    print(f"  [{name}]")
    b, n = base[name], other[name]
    hdr = f"    {'metric':<18} {base_label:>12} {other_label:>12}  {'ratio':>8}"
    print(hdr)
    print(f"    {'-'*18} {'-'*12} {'-'*12}  {'-'*8}")
    for m in METRICS:
        bv = b.get(m); nv = n.get(m)
        print(f"    {m:<18} {fmt_ns(bv):>12} {fmt_ns(nv):>12}  {fmt_ratio(bv, nv):>8}")
    print()

if only_base:
    print(f"  scenarios only in {base_label}: {', '.join(only_base)}")
if only_other:
    print(f"  scenarios only in {other_label}: {', '.join(only_other)}")
PY
