"""Mirror of test/jacl/bench/fib_recursive.jacl.

Naive recursive Fibonacci. fib(25) = 75025; ~242 k calls, max stack
depth 25, no allocation in the hot path.
"""


def fib(n: int) -> int:
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)


def run() -> int:
    return fib(25)


if __name__ == "__main__":
    print(run())
