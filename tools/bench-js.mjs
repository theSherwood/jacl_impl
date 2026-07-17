#!/usr/bin/env node
// bench-js.mjs — runtime perf harness for the JS ports of the JACL bench
// scenarios (Node / V8 JIT). Mirrors tools/bench-python.py: discovers
// *.mjs in a dir (default test/js/bench_scaled), each exporting run(),
// warms up + times, and emits one JSONL record per scenario.
//
// Env: BENCH_WARMUP_ITERS, BENCH_TIMED_ITERS, BENCH_JSON_OUT.
import { readdirSync, writeFileSync } from "node:fs";
import { fileURLToPath, pathToFileURL } from "node:url";
import { dirname, join, resolve } from "node:path";
import { execSync } from "node:child_process";

const here = dirname(fileURLToPath(import.meta.url));
const benchDir = process.argv[2]
  ? resolve(process.argv[2])
  : resolve(here, "..", "test", "js", "bench_scaled");
const warmup = parseInt(process.env.BENCH_WARMUP_ITERS ?? "2", 10);
const timed = parseInt(process.env.BENCH_TIMED_ITERS ?? "5", 10);

function gitSha() {
  try { return execSync("git rev-parse --short HEAD", { encoding: "utf8" }).trim() || "?"; }
  catch { return "?"; }
}
function median(a) { const s = [...a].sort((x, y) => (x < y ? -1 : x > y ? 1 : 0));
  return s[Math.floor(s.length / 2)]; }

const files = readdirSync(benchDir).filter((f) => f.endsWith(".mjs") && !f.startsWith("_")).sort();
const sha = gitSha();
const lines = [];
process.stderr.write(`bench-js: ${files.length} scenario(s), warmup=${warmup} timed=${timed}\n`);
for (const f of files) {
  const mod = await import(pathToFileURL(join(benchDir, f)).href);
  if (typeof mod.run !== "function") { process.stderr.write(`  ${f}: no run()\n`); continue; }
  for (let i = 0; i < warmup; i++) mod.run();
  const times = [];
  let result = null;
  for (let i = 0; i < timed; i++) {
    const t0 = process.hrtime.bigint();
    result = mod.run();
    times.push(Number(process.hrtime.bigint() - t0));
  }
  times.sort((a, b) => a - b);
  const rec = {
    scenario: f.replace(/\.mjs$/, ""), git_sha: sha, runtime: "node",
    node_version: process.versions.node, warmup_iters: warmup, timed_iters: timed,
    wall_ns_min: times[0], wall_ns_median: Math.round(median(times)),
    wall_ns_max: times[times.length - 1], result,
  };
  lines.push(JSON.stringify(rec));
  process.stderr.write(`  ${rec.scenario.padEnd(24)} median=${(rec.wall_ns_median / 1e6).toFixed(2)}ms\n`);
}
const outPath = process.env.BENCH_JSON_OUT;
if (outPath) writeFileSync(outPath, lines.join("\n") + "\n");
else process.stdout.write(lines.join("\n") + "\n");
