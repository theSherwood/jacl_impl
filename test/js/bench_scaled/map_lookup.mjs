// Scaled mirror of test/jacl/bench_scaled/map_lookup.jacl — 3M lookups = 493733.
export function run() {
  const m = new Map();
  for (let i = 0; i < 200; i++) m.set(i, i * 7);
  let acc = 0;
  for (let j = 0; j < 3000000; j++) {
    const k = (j * 73 + 11) % 200;
    acc = (acc + m.get(k)) % 1000003;
  }
  return acc;
}
