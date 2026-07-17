// Scaled mirror of test/jacl/bench_scaled/box_churn.jacl — 3M small allocs = 21000000.
// A fresh 1-field object is the closest V8 analog to JACL's `[box 7]`.
export function run() {
  let acc = 0;
  for (let i = 0; i < 3000000; i++) { const b = { v: 7 }; acc += b.v; }
  return acc;
}
