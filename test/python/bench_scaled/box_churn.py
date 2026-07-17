"""Scaled mirror of test/jacl/bench_scaled/box_churn.jacl — 3M small allocs = 21000000.
The 1-element list is the closest CPython analog to JACL's `[box 7]`."""
def run():
    acc = 0
    for _ in range(3000000):
        b = [7]
        acc += b[0]
    return acc
if __name__ == "__main__":
    print(run())
