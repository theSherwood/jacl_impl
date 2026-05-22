#!/usr/bin/env python3
"""bench-python.py — runtime perf harness for the Python ports of
the JACL bench scenarios.

Discovers test/python/bench/*.py, imports each as a module, runs its
run() function with warmup + timed iterations, and emits one JSONL
record per scenario on stdout. Output schema mirrors test_perf.c
where possible:

  {"scenario": ..., "git_sha": ..., "runtime": "python",
   "python_version": ..., "warmup_iters": N, "timed_iters": N,
   "wall_ns_min": ..., "wall_ns_median": ..., "wall_ns_max": ...,
   "result": <run() return value>}

Env knobs (same names as test_perf.c):
  BENCH_WARMUP_ITERS, BENCH_TIMED_ITERS
  BENCH_JSON_OUT  — write to a file instead of stdout
"""

from __future__ import annotations

import importlib.util
import json
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path


BENCH_DIR_DEFAULT = Path(__file__).resolve().parent.parent / "test" / "python" / "bench"
WARMUP_DEFAULT = 2
TIMED_DEFAULT = 5


def _git_sha() -> str:
    try:
        r = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, check=False, timeout=2,
        )
        return r.stdout.strip() or "?"
    except Exception:
        return "?"


def _load(path: Path):
    spec = importlib.util.spec_from_file_location(path.stem, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {path}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    if not hasattr(mod, "run"):
        raise RuntimeError(f"{path} has no run() function")
    return mod


def _run_one(path: Path, warmup: int, timed: int, sha: str) -> dict:
    mod = _load(path)
    run = mod.run

    for _ in range(warmup):
        run()

    timings: list[int] = []
    last_result = None
    for _ in range(timed):
        t0 = time.perf_counter_ns()
        last_result = run()
        timings.append(time.perf_counter_ns() - t0)

    timings.sort()
    return {
        "scenario": path.stem,
        "git_sha": sha,
        "runtime": "python",
        "python_version": ".".join(str(x) for x in sys.version_info[:3]),
        "warmup_iters": warmup,
        "timed_iters": timed,
        "wall_ns_min": timings[0],
        "wall_ns_median": int(statistics.median(timings)),
        "wall_ns_max": timings[-1],
        "result": last_result,
    }


def main(argv: list[str]) -> int:
    bench_dir = Path(argv[1]) if len(argv) > 1 else BENCH_DIR_DEFAULT
    warmup = int(os.environ.get("BENCH_WARMUP_ITERS", WARMUP_DEFAULT))
    timed = int(os.environ.get("BENCH_TIMED_ITERS", TIMED_DEFAULT))
    out_path = os.environ.get("BENCH_JSON_OUT")
    out = open(out_path, "w") if out_path else sys.stdout

    sha = _git_sha()
    scripts = sorted(p for p in bench_dir.glob("*.py") if not p.name.startswith("_"))
    if not scripts:
        print(f"bench-python: no scenarios in {bench_dir}", file=sys.stderr)
        return 0

    print(f"bench-python: {len(scripts)} scenario(s), warmup={warmup} timed={timed}",
          file=sys.stderr)

    for path in scripts:
        try:
            rec = _run_one(path, warmup, timed, sha)
        except Exception as e:
            print(f"  {path.stem}: ERROR {e}", file=sys.stderr)
            continue
        print(json.dumps(rec), file=out)
        print(f"  {rec['scenario']:<24} median={rec['wall_ns_median']/1e6:7.2f}ms",
              file=sys.stderr)

    if out_path:
        out.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
