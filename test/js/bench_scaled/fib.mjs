// Scaled mirror of test/jacl/bench_scaled/fib.jacl — fib(34) = 5702887.
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
export function run() { return fib(34); }
