#!/usr/bin/env bash
#
# bench-compare.sh — side-by-side comparison of two or three JSONL
# bench files from different runtimes (e.g. JACL test_perf vs
# tools/bench-python.py vs tools/bench-clojure.clj).
#
# Usage:
#   tools/bench-compare.sh BASELINE.jsonl OTHER.jsonl [THIRD.jsonl]
#
# Compares wall_ns_{min,median,max} across runtimes per scenario.
# Scenarios that exist in only one file are noted but not compared.
# Ratio columns show other/baseline — values >1 mean the named
# runtime is slower than the baseline.

set -euo pipefail

if [ $# -lt 2 ] || [ $# -gt 3 ]; then
    echo "usage: $0 BASELINE.jsonl OTHER.jsonl [THIRD.jsonl]" >&2
    exit 2
fi

python3 - "$@" <<'PY'
import json, sys

paths = sys.argv[1:]

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

def label(records, fallback):
    for r in records.values():
        rt = r.get("runtime")
        if rt:
            return rt
        # JACL test_perf doesn't emit "runtime" — infer from presence of "delta"
        if "delta" in r:
            return "jacl"
    return fallback

runtimes = []
for i, p in enumerate(paths):
    recs = load_jsonl(p)
    runtimes.append({
        "path": p,
        "label": label(recs, "rt%d" % i),
        "records": recs,
    })

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

labels = [rt["label"] for rt in runtimes]
print("bench-compare: " + " vs ".join(labels))
for rt in runtimes:
    print(f"  {rt['label']}: {rt['path']}")
print()

METRICS = ["wall_ns_min", "wall_ns_median", "wall_ns_max"]

# Scenarios present in ALL runtimes.
shared_sets = [set(rt["records"]) for rt in runtimes]
shared = sorted(set.intersection(*shared_sets))

for name in shared:
    print(f"  [{name}]")
    base = runtimes[0]["records"][name]
    # Header
    cols = [f"{rt['label']:>12}" for rt in runtimes]
    ratio_cols = [f"{('ratio['+rt['label']+'/'+runtimes[0]['label']+']'):>16}" for rt in runtimes[1:]]
    print("    " + f"{'metric':<18} " + " ".join(cols) + "  " + " ".join(ratio_cols))
    print("    " + "-"*18 + " " + " ".join(["-"*12 for _ in runtimes]) + "  " + " ".join(["-"*16 for _ in runtimes[1:]]))
    for m in METRICS:
        bv = base.get(m)
        vals = [fmt_ns(rt["records"][name].get(m)) for rt in runtimes]
        ratios = [fmt_ratio(bv, rt["records"][name].get(m)) for rt in runtimes[1:]]
        # right-justify ratio under header width (16 cols)
        ratios_padded = [f"{r:>16}" for r in ratios]
        vals_padded = [f"{v:>12}" for v in vals]
        print("    " + f"{m:<18} " + " ".join(vals_padded) + "  " + " ".join(ratios_padded))
    print()

# Report scenarios only in some runtimes.
all_scenarios = set().union(*shared_sets)
for rt in runtimes:
    only = sorted(set(rt["records"]) - set(shared))
    if only:
        print(f"  scenarios only in {rt['label']}: {', '.join(only)}")
PY
