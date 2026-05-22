"""Mirror of test/jacl/bench/parallel_map_reduce.jacl.

4-way parallel sum of integer ranges. Python's GIL means real OS-thread
parallelism on pure-Python integer work is bounded by the GIL acquire
cadence; this is still the closest analog and is what a Python user
would write before reaching for multiprocessing.

Uses concurrent.futures.ThreadPoolExecutor with 4 workers so the API
shape matches JACL's `parallel` (submit N tasks, await all). On
multiprocessing the picture would change — but multiprocessing adds
fork+IPC costs that make a single-iter bench unrepresentative.

Expected output: sum 0..9999 = 49_995_000.
"""

from concurrent.futures import ThreadPoolExecutor


def sum_range(lo: int, hi: int) -> int:
    s = 0
    for i in range(lo, hi):
        s += i
    return s


def run() -> int:
    with ThreadPoolExecutor(max_workers=4) as pool:
        futs = [
            pool.submit(sum_range, 0, 2500),
            pool.submit(sum_range, 2500, 5000),
            pool.submit(sum_range, 5000, 7500),
            pool.submit(sum_range, 7500, 10000),
        ]
        return sum(f.result() for f in futs)


if __name__ == "__main__":
    print(run())
