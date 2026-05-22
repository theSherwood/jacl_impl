"""Mirror of test/jacl/bench/box_literal_churn.jacl.

Same shape as box_churn but the boxed value is a literal-arithmetic
expression. In JACL this triggers OP_BOX_UNCHECKED (the typer proves
the operand cannot carry the error flag); Python has no such concept
but the bench is run for cross-runtime comparison parity.

Expected output: 2000 * (2*5) = 20_000.
"""


def run() -> int:
    acc = 0
    for _ in range(2000):
        b = [2 * 5]
        acc += b[0]
    return acc


if __name__ == "__main__":
    print(run())
