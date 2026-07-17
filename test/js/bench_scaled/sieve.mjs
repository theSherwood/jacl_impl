// Scaled mirror of test/jacl/bench_scaled/sieve.jacl — primes < 60000 = 6057.
export function run() {
  const n = 60000;
  let count = 0, k = 2;
  while (k < n) {
    let is_p = 1, i = 2;
    while (i * i <= k) { if (k % i === 0) is_p = 0; i += 1; }
    if (is_p === 1) count += 1;
    k += 1;
  }
  return count;
}
