// Scaled mirror of test/jacl/bench_scaled/string_concat.jacl — 400 rounds = 160400.
function round() {
  let s = "x";
  for (let i = 0; i < 400; i++) s = s + "y";
  return s.length;
}
export function run() {
  let acc = 0;
  for (let r = 0; r < 400; r++) acc += round();
  return acc;
}
