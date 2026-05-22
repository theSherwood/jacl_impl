"""Mirror of test/jacl/bench/map_lookup_hot.jacl.

Build a 200-entry dict (i -> i*7), then do 10 k lookups against the
deterministic scattered key sequence ((j * 73 + 11) mod 200). Targets
the dict read hot path — directly comparable to JACL's HAMT-get.

Expected output: 6_965_000.
"""


def run() -> int:
    m: dict[int, int] = {}
    for i in range(200):
        m[i] = i * 7

    acc = 0
    for j in range(10000):
        k = (j * 73 + 11) % 200
        acc += m[k]
    return acc


if __name__ == "__main__":
    print(run())
