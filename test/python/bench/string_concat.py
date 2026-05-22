"""Mirror of test/jacl/bench/string_concat.jacl.

500 concatenations of "y" onto an accumulating string. Python strings
are immutable, so each `s + "y"` allocates a fresh string — matches
JACL's `concat` semantics. CPython has an optimization for `s += x`
when `s` has refcount 1, so we avoid `+=` to keep this honest.
"""


def run() -> int:
    s = "x"
    for _ in range(500):
        s = s + "y"
    return len(s)


if __name__ == "__main__":
    print(run())
