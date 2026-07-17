"""Scaled mirror of test/jacl/bench_scaled/map_lookup.jacl — 3M lookups = 493733."""
def run():
    m = {}
    for i in range(200):
        m[i] = i * 7
    acc = 0
    for j in range(3000000):
        k = (j * 73 + 11) % 200
        acc = (acc + m[k]) % 1000003
    return acc
if __name__ == "__main__":
    print(run())
