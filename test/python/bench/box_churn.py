"""Mirror of test/jacl/bench/box_churn.jacl.

Allocates 2000 short-lived single-element lists (closest Python analog to
a JACL box), dereferences each, accumulates the sum. Validates: 1999000.
"""


def run() -> int:
    acc = 0
    for i in range(2000):
        b = [i]
        acc += b[0]
    return acc


if __name__ == "__main__":
    print(run())
