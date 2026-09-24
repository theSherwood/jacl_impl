// Gate: the playground's Run on the parallel Worker driver (jacl #152), end to end in Chromium.
//
//   node demo/temen/workers_gate.mjs [demo-dir]     (after demo/temen/build_assets.sh + demo/build_demo.sh)
//
// Serves the demo with the COOP/COEP headers the Worker driver needs (on Pages, coi-serviceworker.js
// adds them), drives the real page, and checks three Runs:
//   1. the unedited `tour` example — a precompiled card whose source uses `parallel`/`spawn`/`race`
//      — runs on the Workers and finishes clean (the tour is assert-only: no output on success);
//   2. an edited `parallel` program — compiled live in the page, linked (`linkEncode`), run on the
//      Workers — prints the four fold results, checked against the values computed here;
//   3. the `gc_stress_atom` example — 50 tasks `swap` one atom while forcing collections — prints
//      500 on the Workers: `swap` is a compare-and-swap (a get-then-set lost updates there);
//   4. a program that starts no pool stays on the single-threaded engine.
// The status line names the driver (`[N workers]`), which is what is asserted.
//
// Exit 0 iff all four pass. SKIPs (exit 0, says so) when the Worker driver's assets were not built.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const DEMO = path.resolve(process.argv[2] || path.join(path.dirname(fileURLToPath(import.meta.url)), '..'));
const ENGINE = path.join(DEMO, 'temen-web/target/wasm32-unknown-unknown/release/temen_browser.wasm');
if (!fs.existsSync(ENGINE)) {
  console.log(`SKIP: no Worker-driver engine at ${ENGINE} (build_assets.sh step 1a needs nightly + rust-src)`);
  process.exit(0);
}

async function loadChromium() {
  for (const spec of ['playwright', '/opt/node22/lib/node_modules/playwright/index.js']) {
    try {
      const m = await import(spec.startsWith('/') ? pathToFileURL(spec).href : spec);
      return (m.default ?? m).chromium;
    } catch {}
  }
  throw new Error('playwright not found (npm i playwright)');
}

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.json': 'application/json', '.wasm': 'application/wasm' };
const server = http.createServer((q, s) => {
  let f = path.join(DEMO, decodeURIComponent(new URL(q.url, 'http://x').pathname));
  if (!f.startsWith(DEMO)) { s.writeHead(403); return s.end(); }
  if (f.endsWith(path.sep)) f = path.join(f, 'index.html');
  fs.readFile(f, (e, b) => {
    if (e) { s.writeHead(404); return s.end(); }
    s.writeHead(200, {
      'Content-Type': TYPES[path.extname(f)] || 'application/octet-stream',
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    });
    s.end(b);
  });
});
await new Promise((r) => server.listen(0, r));
const port = server.address().port;

// The edited program: a 4-way fold, small enough to finish fast, with its answer computed here.
const fold = (lo, hi) => { let s = 0; for (let i = lo; i < hi; i++) s = (s + i) % 1000003; return s; };
const STEP = 200000;
const PAR_SRC = `proc fold-range {lo hi} {
  mut s 0
  mut i $lo
  while [< $i $hi] {
    set s [% [+ $s $i] 1000003]
    set i [+ $i 1]
  }
  $s
}
def r [parallel ${[0, 1, 2, 3].map((k) => `{ fold-range ${k * STEP} ${(k + 1) * STEP} }`).join(' ')}]
print [vec-get $r 0]
print [vec-get $r 1]
print [vec-get $r 2]
print [vec-get $r 3]
`;
const PAR_OUT = [0, 1, 2, 3].map((k) => fold(k * STEP, (k + 1) * STEP)).join('\n') + '\n';

const chromium = await loadChromium();
const browser = await chromium.launch({ args: process.env.CI ? ['--no-sandbox'] : [] });
const page = await browser.newPage();
const errors = [];
page.on('pageerror', (e) => errors.push(e.message));
await page.goto(`http://localhost:${port}/`);
await page.waitForFunction(() => !document.getElementById('run-btn').disabled, null, { timeout: 60000 });

const run = async () => {
  await page.click('#run-btn');
  await page.waitForFunction(() => /^(Done|Error)/.test(document.getElementById('status').textContent), null, { timeout: 120000 });
  return page.evaluate(() => ({ status: document.getElementById('status').textContent, output: document.getElementById('output').textContent }));
};
const setSource = (src) =>
  page.evaluate((text) => {
    const el = document.querySelector('.cm-content');
    el.focus();
    document.execCommand('selectAll');
    document.execCommand('insertText', false, text);
  }, src);

const results = [];
const check = (name, ok, got) => { results.push(ok); console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}: ${got.status}${ok ? '' : ` · output ${JSON.stringify(got.output)}`}`); };

// 1. The unedited tour: precompiled card, on the Workers.
await page.fill('#example-search', 'tour');
await page.click('.dropdown-item[data-name="tour"]');
let got = await run();
check('tour (precompiled) on Workers', /^Done/.test(got.status) && /\[precompiled\]/.test(got.status) && /\[\d+ workers\]/.test(got.status), got);

// 2. An edited parallel program: live compile + link, on the Workers.
await setSource(PAR_SRC);
got = await run();
check('edited parallel program on Workers', /^Done/.test(got.status) && /\[\d+ workers\]/.test(got.status) && got.output === PAR_OUT, got);

// 3. Contended atom swaps under GC, on the Workers: no lost update.
await page.fill('#example-search', 'gc_stress_atom');
await page.click('.dropdown-item[data-name="gc_stress_atom"]');
got = await run();
check('gc_stress_atom on Workers', /^Done/.test(got.status) && /\[\d+ workers\]/.test(got.status) && got.output === '500\n', got);

// 4. A program that starts no pool: single-threaded.
await setSource('print [+ 40 2]\n');
got = await run();
check('no pool, single-threaded', /^Done/.test(got.status) && !/workers\]/.test(got.status) && got.output === '42\n', got);

await browser.close();
server.close();
if (errors.length) console.log('page errors:', errors.slice(0, 5));
const ok = results.every(Boolean) && errors.length === 0;
console.log(ok ? 'PASS — the playground runs pool-starting programs on the Worker driver' : 'FAIL');
process.exit(ok ? 0 : 1);
