"""Mirror of test/jacl/bench/collection_churn.jacl.

Builds a 200-entry dict and a 200-element list, rebuilds each with new
values, then sums all values back. Python has no persistent collections
in the stdlib, so this uses functional-style copy-on-write to match
JACL's semantics — every update produces a new collection. This makes
the comparison apples-to-apples on work done (alloc + copy) rather than
"how fast can mutable update go."

For an idiomatic mutable comparison, see collection_churn_mut.py.
"""


def run() -> int:
    n = 200

    m: dict[int, int] = {}
    for i in range(n):
        m = {**m, i: i}
    for i in range(n):
        m = {**m, i: i * 2}
    ms = 0
    for i in range(n):
        ms += m[i]

    v: list[int] = []
    for i in range(n):
        v = [*v, i]
    for i in range(n):
        v = [*v[:i], i * 2, *v[i + 1 :]]
    vs = 0
    for i in range(n):
        vs += v[i]

    return ms + vs


if __name__ == "__main__":
    print(run())
