"""Scaled mirror of test/jacl/bench_scaled/fib.jacl — fib(34) = 5702887."""
def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)
def run():
    return fib(34)
if __name__ == "__main__":
    print(run())
