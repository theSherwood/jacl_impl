"""Scaled mirror of test/jacl/bench_scaled/string_concat.jacl — 400 rounds = 160400.
`s = s + "y"` allocates a fresh string each step (O(N^2) per round), matching the
JACL rope-concat workload."""
def _round():
    s = "x"
    for _ in range(400):
        s = s + "y"
    return len(s)
def run():
    acc = 0
    for _ in range(400):
        acc += _round()
    return acc
if __name__ == "__main__":
    print(run())
