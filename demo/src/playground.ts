/**
 * JACL Playground — entry point.
 *
 * Wires together:
 *   - CodeMirror 6 editor in a #editor-host div (replaces the old
 *     textarea — index.html change required).
 *   - The JACL stream-parser language mode for syntax highlighting.
 *   - @replit/codemirror-vim, toggle-able via the #vim-toggle button.
 *   - The TEMEN backend: the wasm-safe bytecode engine (temen-browser cdylib,
 *     `TemenJaclRunner`) plus the in-browser LLVM-free frontend that compiles
 *     edited source to TEMEN IR (`jacl_emit.wasm`, `JaclFrontend`). This is the
 *     sole backend — the old Emscripten JACL VM has been removed.
 *   - The existing example picker (415 fixtures from test/jacl, plus
 *     the demo tours), seeded into the editor on load when the user
 *     hasn't typed anything yet.
 */

import { EditorState, Compartment } from "@codemirror/state";
import { EditorView, keymap, lineNumbers, highlightActiveLine } from "@codemirror/view";
import { defaultKeymap, history, historyKeymap, indentWithTab } from "@codemirror/commands";
import { bracketMatching, indentOnInput, syntaxHighlighting, defaultHighlightStyle, HighlightStyle } from "@codemirror/language";
import { tags as t } from "@lezer/highlight";
import { vim } from "@replit/codemirror-vim";

import { jacl as jaclMode } from "./jacl-mode";

// JACL-specific overlay style. Layered on top of defaultHighlightStyle
// (which handles keywords, strings, numbers, types, comments, ...).
//   - function(variableName) → call heads, painted distinct blue. The
//     wrapped tag comes via the JACL mode's tokenTable; see jacl-mode.
//   - variableName → `$var` refs. The default style doesn't give plain
//     variableName a color; teal keeps data flow traceable without
//     competing with calls (blue) or types (dark green).
//   - operator → arithmetic / comparison / arrow ops. The default
//     style also has no rule here. A muted magenta gives them visual
//     weight without overpowering identifiers.
const jaclHighlight = HighlightStyle.define([
  { tag: t.function(t.variableName), color: "#22d", fontWeight: "500" },
  { tag: t.variableName,             color: "#178" },
  { tag: t.operator,                 color: "#905" },
]);
import { TemenJaclRunner, JaclFrontend, RunResult, EmitResult } from "./temen-jacl-wasm";
import { initSplitter } from "./splitter";

// --- DOM refs ---
// The editor *host* is a plain div; CodeMirror mounts inside it.
const editorHost   = document.getElementById("editor-host") as HTMLDivElement;
const output       = document.getElementById("output")       as HTMLPreElement;
const runBtn       = document.getElementById("run-btn")      as HTMLButtonElement;
const clearBtn     = document.getElementById("clear-btn")    as HTMLButtonElement;
const vimToggleBtn = document.getElementById("vim-toggle")   as HTMLButtonElement;
const compileModeEl = document.getElementById("compile-mode") as HTMLSelectElement;
const statusEl     = document.getElementById("status")       as HTMLSpanElement;
const searchInput  = document.getElementById("example-search") as HTMLInputElement;
const dropdown     = document.getElementById("example-dropdown") as HTMLDivElement;

// --- Placeholder source (shown if examples.json fails to load) ---
const DEFAULT_CODE = `# Welcome to JACL!
# Edit this code and press Run (or Ctrl+Enter).

print "hello, world"

proc fact {n} {
  if [== $n 0] { 1 } else { [* $n [fact [- $n 1]]] }
}
print [fact 10]
`;

// --- Vim toggle state ---
// CodeMirror's Compartment lets us swap a slice of the editor config
// at runtime without rebuilding the entire EditorState. We use it for
// the vim extension because we want a single button to enable/disable
// vim mode without losing the doc, history, etc.
const vimCompartment = new Compartment();
const VIM_KEY = "jacl-playground:vim";
let vimEnabled = localStorage.getItem(VIM_KEY) === "1";

// Which frontend compiles edited source to TEMEN IR. `emit` = jacl_emit.wasm (the Emscripten-built
// native-speed frontend, now macro-capable — it stages each macro body on the cdylib, ~30x faster
// than the guest on the tour); `guest` = the self-hosted compiler run as an TEMEN guest on the bytecode
// interpreter (slow, kept for comparison); `tierup` = the guest today (a pre-baked wasm tier-up is not
// wired). Persisted in localStorage.
type CompileMode = "emit" | "guest" | "tierup";
const MODE_KEY = "jacl-playground:compile-mode";
function loadCompileMode(): CompileMode {
  const v = localStorage.getItem(MODE_KEY);
  return v === "guest" || v === "tierup" ? v : "emit";
}
let compileMode: CompileMode = loadCompileMode();
// Memoize compiled IR by (mode, source) so re-running unchanged source skips the compile entirely.
const irCache = new Map<string, { ir: string; ran: CompileMode }>();

// --- Editor construction ---
function buildEditor(initial: string): EditorView {
  const state = EditorState.create({
    doc: initial,
    extensions: [
      lineNumbers(),
      highlightActiveLine(),
      // Soft-wrap long lines so the editor's column never overflows
      // the 50% panel split and we don't get a horizontal scrollbar.
      EditorView.lineWrapping,
      history(),
      bracketMatching(),
      indentOnInput(),
      // Both highlighters are registered as *main* (no `fallback: true`).
      // With fallback, defaultHighlightStyle was demoted to "only used
      // if no main highlighter is registered"; my jaclHighlight then
      // suppressed it for every tag it didn't itself style, killing
      // string/number/comment/keyword colors. Both as main → CM unions
      // the classes they emit (per syntaxHighlighting docstring).
      syntaxHighlighting(defaultHighlightStyle),
      syntaxHighlighting(jaclHighlight),
      jaclMode(),
      keymap.of([
        ...defaultKeymap,
        ...historyKeymap,
        indentWithTab,
        {
          key: "Mod-Enter",
          preventDefault: true,
          run: () => { handleRun(); return true; },
        },
        {
          // Suppress the browser's Save dialog when the user
          // muscle-memories Ctrl+S in the editor.
          key: "Mod-s",
          preventDefault: true,
          run: () => true,
        },
      ]),
      EditorView.theme({
        "&": { height: "100%", fontSize: "13px" },
        ".cm-scroller": { fontFamily: "var(--font-mono)", overflow: "auto" },
        ".cm-content": { padding: "8px 0" },
        ".cm-gutters": { backgroundColor: "var(--bg-editor)", borderRight: "1px solid var(--border)" },
      }),
      vimCompartment.of(vimEnabled ? vim() : []),
    ],
  });
  return new EditorView({ state, parent: editorHost });
}

function setVim(enabled: boolean, view: EditorView) {
  vimEnabled = enabled;
  localStorage.setItem(VIM_KEY, enabled ? "1" : "0");
  view.dispatch({
    effects: vimCompartment.reconfigure(enabled ? vim() : []),
  });
  vimToggleBtn.textContent = enabled ? "Vim: on" : "Vim: off";
  vimToggleBtn.classList.toggle("active", enabled);
}

function getSource(view: EditorView): string {
  return view.state.doc.toString();
}

function setSource(view: EditorView, text: string) {
  view.dispatch({
    changes: { from: 0, to: view.state.doc.length, insert: text },
  });
}

// --- TEMEN backend (the wasm-safe bytecode engine via the temen-browser cdylib) ---
// The sole run path. Unedited example programs run their **precompiled** `.temen` blobs (built by
// demo/temen/build_assets.sh) directly; edited source is compiled to TEMEN IR in the browser by the
// LLVM-free frontend (jacl_emit.wasm) and linked against the runtime (jaclrt.temen) per run.
let temenRunner: TemenJaclRunner | null = null;
let temenManifest: { name: string; temen: string }[] | null = null;
let temenFrontend: JaclFrontend | null = null;   // in-browser lexer+parser+codegen (jacl_emit.wasm)
let temenCompiler: Uint8Array | null = null;      // jacl_compiler.temen — the self-hosted frontend (stages macros)
let temenWarmCompiler: Uint8Array | null = null;  // jacl_compiler_snapshot.temen — two-phase warm card (tierup mode)
let temenRuntime: Uint8Array | null = null;       // jaclrt.temen — linked against per live run
let temenStagingRt: Uint8Array | null = null;     // jaclrt_staging.temeno — links macro-body modules (jacl_emit staging)
/** The example currently loaded verbatim in the editor (cleared once the user edits it). */
let currentExample: Example | null = null;

async function ensureTemen(): Promise<TemenJaclRunner | null> {
  if (temenRunner && temenManifest) return temenRunner;
  try {
    if (!temenManifest) {
      const resp = await fetch("temen/cards/manifest.json");
      temenManifest = resp.ok ? await resp.json() : [];
    }
    if (!temenRunner) temenRunner = await TemenJaclRunner.create("wasm/temen_browser.wasm");
    return temenRunner;
  } catch {
    return null; // assets not built / not shipped → caller shows an "assets missing" note
  }
}

/**
 * Load the live-editing pieces on demand; null if the runtime isn't shipped. The **frontend** is
 * either the self-hosted JACL compiler run as an TEMEN guest (`jacl_compiler.temen`, which expands
 * `defmacro`s in-guest via the §22 Jit cap — the macro-capable path) or, if that asset isn't shipped,
 * the Emscripten `jacl_emit.wasm` ({@link JaclFrontend}, no in-browser macro expansion).
 */
async function ensureLive(): Promise<{
  compiler: Uint8Array | null;
  warmCompiler: Uint8Array | null;
  frontend: JaclFrontend | null;
  runtime: Uint8Array;
} | null> {
  try {
    // Load BOTH frontends so the compiler dropdown can switch without a reload: the self-hosted
    // compiler-guest (`guest`/`tierup` modes) and the Emscripten `jacl_emit.wasm` (`emit` mode).
    // Each is best-effort — a missing asset just disables the modes that need it.
    if (!temenCompiler) {
      const resp = await fetch("temen/cards/jacl_compiler.temen");
      if (resp.ok) temenCompiler = new Uint8Array(await resp.arrayBuffer());
    }
    // The warm-snapshot two-phase card (TEMEN_WARM_COMPILER.md Slice 3): the `tierup` mode opens it
    // once and evals each compile over the restored warm image (~2× the plain guest). Best-effort —
    // a missing asset just leaves `tierup` falling back to the plain guest compile.
    if (!temenWarmCompiler) {
      const resp = await fetch("temen/cards/jacl_compiler_snapshot.temen");
      if (resp.ok) temenWarmCompiler = new Uint8Array(await resp.arrayBuffer());
    }
    // The staging runtime (jaclrt + syn_rt glue) lets jacl_emit.wasm stage macros: it codegens each
    // macro body and runs it on the cdylib against this module — so the fast AOT frontend is macro-capable.
    if (!temenStagingRt) {
      const resp = await fetch("temen/cards/jaclrt_staging.temeno");
      if (resp.ok) temenStagingRt = new Uint8Array(await resp.arrayBuffer());
    }
    if (!temenFrontend && typeof createJaclEmit === "function") {
      // Macro-body staging callback: run the codegen'd body on the cdylib, return the result wire.
      const stageRt = temenStagingRt;
      const stageRun =
        temenRunner && stageRt
          ? (mod: Uint8Array, arg: Uint8Array) => temenRunner!.linkRunRaw(mod, stageRt, arg)
          : undefined;
      try {
        temenFrontend = await JaclFrontend.create(stageRun);
      } catch { /* jacl_emit.wasm not shipped */ }
    }
    if (!temenRuntime) {
      const resp = await fetch("temen/jaclrt.temen");
      if (!resp.ok) return null;
      temenRuntime = new Uint8Array(await resp.arrayBuffer());
    }
    return { compiler: temenCompiler, warmCompiler: temenWarmCompiler, frontend: temenFrontend, runtime: temenRuntime };
  } catch {
    return null;
  }
}

/** The manifest entry for the current source iff it is an *unedited* precompiled example. */
function temenEntryForCurrentSource(source: string): { name: string; temen: string } | null {
  if (!currentExample || !temenManifest) return null;
  if (source !== currentExample.code) return null; // edited → not precompiled
  return temenManifest.find((e) => e.name === currentExample!.name) ?? null;
}

async function initTemen(): Promise<void> {
  const runner = await ensureTemen();
  if (runner) {
    setStatus("Ready", "ok");
    runBtn.disabled = false;
    output.innerHTML = '<span class="placeholder">Press Run or Ctrl+Enter to execute</span>';
    void ensureLive(); // warm the frontend + runtime so the first edited-source Run is instant
  } else {
    setStatus("WASM load failed", "error");
    output.innerHTML =
      '<span class="error-line">Failed to load the TEMEN engine (wasm/temen_browser.wasm).\n\n' +
      "Make sure you ran: bash build_demo.sh (and demo/temen/build_assets.sh)\n" +
      'And are serving via HTTP (not file://)</span>';
  }
}

function handleRun() {
  if (runBtn.disabled) return;
  const source = getSource(view);
  if (!source.trim()) {
    output.innerHTML = '<span class="placeholder">(empty source)</span>';
    setStatus("Ready", "ok");
    return;
  }

  setStatus("Running...", "");
  void runOnTemen(source);
}

/** Timing breakdown for a run: `compileMs` (source → TEMEN IR) is absent for a precompiled example
 *  that runs its shipped `.temen` verbatim; `runMs` (link + execute) is absent if a compile error
 *  stopped us before running. */
interface RunTiming {
  compileMs?: number;
  runMs?: number;
  mode?: CompileMode;
  cached?: boolean;
  precompiled?: boolean; // ran a shipped .temen (unedited example) — no live compile at all
}

function displayResult(result: RunResult, timing: RunTiming) {
  output.textContent = "";
  if (result.output) {
    output.appendChild(document.createTextNode(result.output));
  }
  if (result.isError && result.error) {
    const span = document.createElement("span");
    span.className = "error-line";
    span.textContent = (result.output ? "\n" : "") + "Error: " + result.error;
    output.appendChild(span);
  }
  if (!result.output && !result.isError) {
    output.innerHTML = '<span class="placeholder">(no output)</span>';
  }

  // Build a compile/run breakdown for the status line and the console.
  const parts: string[] = [];
  if (timing.compileMs !== undefined) {
    parts.push(timing.cached ? "compile 0ms (cached)" : `compile ${timing.compileMs.toFixed(0)}ms`);
  }
  if (timing.runMs !== undefined) parts.push(`run ${timing.runMs.toFixed(0)}ms`);
  const total = (timing.compileMs ?? 0) + (timing.runMs ?? 0);
  const modeTag = timing.precompiled ? " [precompiled]" : timing.mode ? ` [${MODE_LABEL[timing.mode]}]` : "";
  const breakdown =
    parts.join(" · ") + (parts.length > 1 ? ` · total ${total.toFixed(0)}ms` : "") + modeTag;

  // Console log: a single grouped line so it's easy to watch across runs.
  console.log(
    `[temen] ${result.isError ? "error" : "ok"}${timing.mode ? " " + timing.mode : ""} — ` +
      (timing.compileMs !== undefined
        ? `compile=${timing.cached ? "0ms(cached)" : timing.compileMs.toFixed(1) + "ms"} `
        : "") +
      (timing.runMs !== undefined ? `run=${timing.runMs.toFixed(1)}ms ` : "") +
      `total=${total.toFixed(1)}ms`,
  );

  setStatus(
    result.isError ? `Error — ${breakdown}` : `Done — ${breakdown}`,
    result.isError ? "error" : "ok",
  );
}

type Live = { compiler: Uint8Array | null; warmCompiler: Uint8Array | null; frontend: JaclFrontend | null; runtime: Uint8Array };

/** Human labels for the status line / console (kept in sync with the `<select>` options). */
const MODE_LABEL: Record<CompileMode, string> = {
  emit: "jacl_emit.wasm",
  guest: "self-hosted guest (interp)",
  tierup: "self-hosted guest (tier-up)",
};

/** The effective compile mode given which assets actually loaded — falls back so Run never dead-ends. */
function resolveCompileMode(live: Live): CompileMode {
  if (compileMode === "emit") return live.frontend ? "emit" : live.compiler ? "guest" : "emit";
  // guest / tierup both need the compiler-guest; without it, fall back to the Emscripten frontend.
  if (!live.compiler) return "emit";
  // `tierup` runs the warm-snapshot fast path when its two-phase card is shipped (compileWith falls
  // back to the plain guest otherwise); `guest` always runs the self-hosted guest on the interpreter.
  return compileMode === "tierup" ? "tierup" : "guest";
}

/** jacl_emit.wasm is an **emit-only** build with no TEMEN runtime, so it can't stage `defmacro`s — it
 *  returns an "emit-only build" / "staging hook not installed" error. Detect that so `emit` mode can
 *  fall back to the macro-capable self-hosted guest instead of dead-ending. */
function isMacroStagingError(e: string): boolean {
  return /macro|staging|emit-only/i.test(e);
}

/** Compile `source` to TEMEN IR with the chosen frontend, returning which engine actually ran (an
 *  `emit` that hits a macro falls back to the guest, so the reported mode reflects reality). */
async function compileWith(mode: CompileMode, live: Live, source: string): Promise<{ emitted: EmitResult; ran: CompileMode }> {
  if (mode === "emit") {
    const r = live.frontend!.emitIr(source);
    // Fast path succeeded, or failed for a non-macro reason (real syntax/type error) → report as-is.
    if (!("error" in r) || !isMacroStagingError(r.error) || !live.compiler) return { emitted: r, ran: "emit" };
    // Macro program → jacl_emit can't stage it; fall back to the self-hosted guest.
    return { emitted: temenRunner!.emitIrViaCompiler(live.compiler, source), ran: "guest" };
  }
  // `tierup`: open the two-phase warm-snapshot card once (init/prelude paid once, then restored per
  // compile), and run each compile on the **warm-coop** tier — cooperative tier-up over the warm image,
  // the fastest self-hosted path (TEMEN_WARM_COMPILER.md; ~3× warm-interp on the tour). The stable
  // cacheKey inside warmCoopEval compiles the emitted compiler module once per session. If the coop tier
  // declines/traps we fall back to warm-interp (temen_warm_eval); if the warm card isn't shipped at all,
  // to the plain self-hosted guest.
  if (mode === "tierup" && live.warmCompiler) {
    const r = temenRunner!;
    if (r.isWarmOpen || r.warmOpen(live.warmCompiler)) {
      const coop = await r.warmCoopEval(source);
      // Success, or a real compile error (not a tier decline) → report as-is; only a decline falls back.
      if (!("error" in coop) || !coop.error.startsWith("warm-coop declined:")) return { emitted: coop, ran: "tierup" };
      return { emitted: r.warmEval(source), ran: "tierup" };
    }
  }
  // `guest` (and the tierup fallback when no warm card): run the self-hosted compiler-guest verbatim.
  return { emitted: temenRunner!.emitIrViaCompiler(live.compiler!, source), ran: mode === "tierup" ? "guest" : mode };
}

/** Run the current source on the TEMEN backend: a precompiled example verbatim, else compile+link live. */
async function runOnTemen(source: string) {
  const runner = await ensureTemen();
  if (!runner) {
    temenError("TEMEN engine not available (run bash build_demo.sh + demo/temen/build_assets.sh).");
    return;
  }
  try {
    // Fast path: an *unedited* precompiled example runs its shipped .temen directly.
    const entry = temenEntryForCurrentSource(source);
    if (entry) {
      const tRun = performance.now();
      const bytes = new Uint8Array(await (await fetch(`temen/${entry.temen}`)).arrayBuffer());
      displayResult(runner.runTemen(bytes), { runMs: performance.now() - tRun, precompiled: true });
      return;
    }
    // Live path: compile edited source to IR in the browser, then link vs the runtime + run.
    const live = await ensureLive();
    if (!live || (!live.compiler && !live.frontend)) {
      temenError("Live compile needs jacl_compiler.temen (or jacl_emit.wasm) + jaclrt.temen (run demo/temen/build_assets.sh).");
      return;
    }
    // Resolve the requested compiler, falling back if its asset isn't shipped.
    const mode = resolveCompileMode(live);
    // Time the two halves separately: compiling the source to TEMEN IR vs linking + running.
    // A cache hit (same mode + source) reports compile time 0 — the compile was skipped.
    const cacheKey = mode + "\0" + source;
    const tCompile = performance.now();
    const cachedEntry = irCache.get(cacheKey);
    const cached = cachedEntry !== undefined;
    let ir: string;
    let ran: CompileMode = mode;
    if (cachedEntry !== undefined) {
      ({ ir, ran } = cachedEntry);
    } else {
      const out = await compileWith(mode, live, source);
      ran = out.ran;
      if ("error" in out.emitted) {
        displayResult({ output: "", error: out.emitted.error, isError: true }, { compileMs: performance.now() - tCompile, mode: ran });
        return;
      }
      ir = out.emitted.ir;
      irCache.set(cacheKey, { ir, ran });
    }
    const compileMs = cached ? 0 : performance.now() - tCompile;
    const tRun = performance.now();
    const result = runner.linkRun(ir, live.runtime);
    displayResult(result, { compileMs, runMs: performance.now() - tRun, mode: ran, cached });
  } catch (e) {
    output.textContent = "";
    const span = document.createElement("span");
    span.className = "error-line";
    span.textContent = "TEMEN run failed: " + (e instanceof Error ? e.message : String(e));
    output.appendChild(span);
    setStatus("Crashed", "error");
  }
}

/** Show an error note when a required TEMEN asset is missing. */
function temenError(note: string) {
  output.textContent = "";
  const span = document.createElement("span");
  span.className = "error-line";
  span.textContent = note;
  output.appendChild(span);
  setStatus("TEMEN assets missing", "error");
}

function setStatus(text: string, kind: "ok" | "error" | "") {
  statusEl.textContent = text;
  statusEl.className =
    kind === "error" ? "status-error" :
    kind === "ok"    ? "status-ok"    :
    "";
}

// --- Example picker ---
interface Example {
  name: string;
  category: string;
  code: string;
  concurrent?: boolean;
  description?: string;
}

let examples: Example[] = [];
let highlightedIndex = -1;

async function loadExamples() {
  try {
    const resp = await fetch("examples.json");
    examples = await resp.json();
    searchInput.placeholder = `Search ${examples.length} examples...`;
    // Seed the editor with the assert-based tour if the user hasn't
    // edited the default placeholder yet.
    const tour = examples.find((ex) => ex.name === "tour");
    if (tour && getSource(view) === DEFAULT_CODE) {
      setSource(view, tour.code);
    }
  } catch {
    searchInput.placeholder = "Examples unavailable";
    searchInput.disabled = true;
  }
}

function renderDropdown(filter: string) {
  dropdown.innerHTML = "";
  const query = filter.toLowerCase();
  const filtered = query
    ? examples.filter((ex) =>
        ex.name.toLowerCase().includes(query) ||
        ex.category.toLowerCase().includes(query) ||
        (ex.description?.toLowerCase().includes(query) ?? false))
    : examples;

  if (filtered.length === 0) {
    dropdown.innerHTML = '<div class="dropdown-item" style="color:var(--text-dim)">No matches</div>';
    return;
  }

  const groups = new Map<string, Example[]>();
  for (const ex of filtered) {
    if (!groups.has(ex.category)) groups.set(ex.category, []);
    groups.get(ex.category)!.push(ex);
  }

  let itemIndex = 0;
  for (const [cat, items] of groups) {
    const header = document.createElement("div");
    header.className = "dropdown-category";
    header.textContent = `${cat} (${items.length})`;
    dropdown.appendChild(header);

    for (const ex of items) {
      const item = document.createElement("div");
      item.className = "dropdown-item";
      item.dataset.index = String(itemIndex++);
      item.dataset.name = ex.name;
      let label = ex.name;
      if (ex.concurrent) label += ' <span class="badge badge-concurrent">async</span>';
      item.innerHTML = label;
      item.addEventListener("click", () => selectExample(ex));
      dropdown.appendChild(item);
    }
  }
}

function selectExample(ex: Example) {
  setSource(view, ex.code);
  currentExample = ex; // remember it so the TEMEN backend can run its precompiled .temen
  searchInput.value = "";
  closeDropdown();
  view.focus();
}

function openDropdown() {
  renderDropdown(searchInput.value);
  dropdown.classList.remove("hidden");
}

function closeDropdown() {
  dropdown.classList.add("hidden");
  highlightedIndex = -1;
}

function highlightItem(index: number) {
  const items = dropdown.querySelectorAll<HTMLElement>(".dropdown-item");
  items.forEach((el) => el.classList.remove("highlighted"));
  if (index >= 0 && index < items.length) {
    items[index].classList.add("highlighted");
    items[index].scrollIntoView({ block: "nearest" });
    highlightedIndex = index;
  }
}

// --- Wire it up ---
const view = buildEditor(DEFAULT_CODE);
setVim(vimEnabled, view);

const panelsEl   = document.querySelector(".panels") as HTMLElement;
const splitterEl = document.getElementById("splitter") as HTMLElement;
if (panelsEl && splitterEl) initSplitter(panelsEl, splitterEl);

runBtn.addEventListener("click", handleRun);
clearBtn.addEventListener("click", () => {
  output.innerHTML = '<span class="placeholder">Press Run or Ctrl+Enter to execute</span>';
  setStatus("Ready", "ok");
});
vimToggleBtn.addEventListener("click", () => setVim(!vimEnabled, view));

// Compile-mode dropdown: reflect the persisted choice, and persist + re-run on change.
compileModeEl.value = compileMode;
compileModeEl.addEventListener("change", () => {
  compileMode = (compileModeEl.value as CompileMode) ?? "emit";
  localStorage.setItem(MODE_KEY, compileMode);
  // Re-run the current source through the newly selected compiler so the timing updates immediately.
  if (!runBtn.disabled) void runOnTemen(getSource(view));
});

searchInput.addEventListener("focus", openDropdown);
searchInput.addEventListener("input", () => {
  renderDropdown(searchInput.value);
  dropdown.classList.remove("hidden");
  highlightedIndex = -1;
});
searchInput.addEventListener("keydown", (e) => {
  const items = dropdown.querySelectorAll<HTMLElement>(".dropdown-item");
  const count = items.length;
  if (e.key === "ArrowDown") {
    e.preventDefault();
    highlightItem(highlightedIndex < count - 1 ? highlightedIndex + 1 : 0);
  } else if (e.key === "ArrowUp") {
    e.preventDefault();
    highlightItem(highlightedIndex > 0 ? highlightedIndex - 1 : count - 1);
  } else if (e.key === "Enter") {
    e.preventDefault();
    if (highlightedIndex >= 0 && highlightedIndex < count) {
      const name = items[highlightedIndex].dataset.name!;
      const ex = examples.find((x) => x.name === name);
      if (ex) selectExample(ex);
    }
  } else if (e.key === "Escape") {
    closeDropdown();
    view.focus();
  }
});

document.addEventListener("click", (e) => {
  const target = e.target as HTMLElement | null;
  if (!target?.closest(".example-picker")) closeDropdown();
});

// Global Ctrl+Enter (fires even when the editor isn't focused).
document.addEventListener("keydown", (e) => {
  if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) {
    const active = document.activeElement;
    const inEditor = active && editorHost.contains(active);
    if (!inEditor) {
      e.preventDefault();
      handleRun();
    }
  }
});

// Kick off WASM load + example fetch in parallel.
void Promise.all([initTemen(), loadExamples()]);
