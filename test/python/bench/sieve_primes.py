"""Mirror of test/jacl/bench/sieve_primes.jacl.

Count primes below N=2000 via trial division. Pure arithmetic, no
allocation. Expected output: 303.

NB: Python doesn't have a JACL-style early break here, so we use
`break` once we've found a divisor, matching the early-exit shape
JACL would have if it used break too. To keep the work apples-to-
apples we still scan i*i <= k either way.
"""


def run() -> int:
    n = 2000
    count = 0
    k = 2
    while k < n:
        is_p = True
        i = 2
        while i * i <= k:
            if k % i == 0:
                is_p = False
            i += 1
        if is_p:
            count += 1
        k += 1
    return count


if __name__ == "__main__":
    print(run())
