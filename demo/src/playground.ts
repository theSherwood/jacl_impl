/**
 * JACL Playground — entry point.
 *
 * Wires together:
 *   - CodeMirror 6 editor in a #editor-host div (replaces the old
 *     textarea — index.html change required).
 *   - The JACL stream-parser language mode for syntax highlighting.
 *   - @replit/codemirror-vim, toggle-able via the #vim-toggle button.
 *   - The SVM backend: the wasm-safe bytecode engine (svm-browser cdylib,
 *     `SvmJaclRunner`) plus the in-browser LLVM-free frontend that compiles
 *     edited source to SVM IR (`jacl_emit.wasm`, `JaclFrontend`). This is the
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
import { SvmJaclRunner, JaclFrontend, RunResult } from "./svm-jacl-wasm";
import { initSplitter } from "./splitter";

// --- DOM refs ---
// The editor *host* is a plain div; CodeMirror mounts inside it.
const editorHost   = document.getElementById("editor-host") as HTMLDivElement;
const output       = document.getElementById("output")       as HTMLPreElement;
const runBtn       = document.getElementById("run-btn")      as HTMLButtonElement;
const clearBtn     = document.getElementById("clear-btn")    as HTMLButtonElement;
const vimToggleBtn = document.getElementById("vim-toggle")   as HTMLButtonElement;
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

// --- SVM backend (the wasm-safe bytecode engine via the svm-browser cdylib) ---
// The sole run path. Unedited example programs run their **precompiled** `.svmb` blobs (built by
// demo/svm/build_assets.sh) directly; edited source is compiled to SVM IR in the browser by the
// LLVM-free frontend (jacl_emit.wasm) and linked against the runtime (jaclrt.svm) per run.
let svmRunner: SvmJaclRunner | null = null;
let svmManifest: { name: string; svmb: string }[] | null = null;
let svmFrontend: JaclFrontend | null = null;   // in-browser lexer+parser+codegen (jacl_emit.wasm) — fallback
let svmCompiler: Uint8Array | null = null;      // jacl_compiler.svmb — the self-hosted frontend (stages macros)
let svmRuntime: Uint8Array | null = null;       // jaclrt.svm — linked against per live run
/** The example currently loaded verbatim in the editor (cleared once the user edits it). */
let currentExample: Example | null = null;

async function ensureSvm(): Promise<SvmJaclRunner | null> {
  if (svmRunner && svmManifest) return svmRunner;
  try {
    if (!svmManifest) {
      const resp = await fetch("svm/svmb/manifest.json");
      svmManifest = resp.ok ? await resp.json() : [];
    }
    if (!svmRunner) svmRunner = await SvmJaclRunner.create("wasm/svm_browser.wasm");
    return svmRunner;
  } catch {
    return null; // assets not built / not shipped → caller shows an "assets missing" note
  }
}

/**
 * Load the live-editing pieces on demand; null if the runtime isn't shipped. The **frontend** is
 * either the self-hosted JACL compiler run as an SVM guest (`jacl_compiler.svmb`, which expands
 * `defmacro`s in-guest via the §22 Jit cap — the macro-capable path) or, if that asset isn't shipped,
 * the Emscripten `jacl_emit.wasm` ({@link JaclFrontend}, no in-browser macro expansion).
 */
async function ensureLive(): Promise<{
  compiler: Uint8Array | null;
  frontend: JaclFrontend | null;
  runtime: Uint8Array;
} | null> {
  try {
    if (!svmCompiler) {
      const resp = await fetch("svm/svmb/jacl_compiler.svmb");
      if (resp.ok) svmCompiler = new Uint8Array(await resp.arrayBuffer());
    }
    // Fall back to the Emscripten frontend only when the self-hosted compiler-guest isn't shipped.
    if (!svmCompiler && !svmFrontend) svmFrontend = await JaclFrontend.create();
    if (!svmRuntime) {
      const resp = await fetch("svm/jaclrt.svm");
      if (!resp.ok) return null;
      svmRuntime = new Uint8Array(await resp.arrayBuffer());
    }
    return { compiler: svmCompiler, frontend: svmFrontend, runtime: svmRuntime };
  } catch {
    return null;
  }
}

/** The manifest entry for the current source iff it is an *unedited* precompiled example. */
function svmEntryForCurrentSource(source: string): { name: string; svmb: string } | null {
  if (!currentExample || !svmManifest) return null;
  if (source !== currentExample.code) return null; // edited → not precompiled
  return svmManifest.find((e) => e.name === currentExample!.name) ?? null;
}

async function initSvm(): Promise<void> {
  const runner = await ensureSvm();
  if (runner) {
    setStatus("Ready", "ok");
    runBtn.disabled = false;
    output.innerHTML = '<span class="placeholder">Press Run or Ctrl+Enter to execute</span>';
    void ensureLive(); // warm the frontend + runtime so the first edited-source Run is instant
  } else {
    setStatus("WASM load failed", "error");
    output.innerHTML =
      '<span class="error-line">Failed to load the SVM engine (wasm/svm_browser.wasm).\n\n' +
      "Make sure you ran: bash build_demo.sh (and demo/svm/build_assets.sh)\n" +
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
  void runOnSvm(source);
}

/** Timing breakdown for a run: `compileMs` (source → SVM IR) is absent for a precompiled example
 *  that runs its shipped `.svmb` verbatim; `runMs` (link + execute) is absent if a compile error
 *  stopped us before running. */
interface RunTiming {
  compileMs?: number;
  runMs?: number;
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
  if (timing.compileMs !== undefined) parts.push(`compile ${timing.compileMs.toFixed(0)}ms`);
  if (timing.runMs !== undefined) parts.push(`run ${timing.runMs.toFixed(0)}ms`);
  const total = (timing.compileMs ?? 0) + (timing.runMs ?? 0);
  const breakdown = parts.join(" · ") + (parts.length > 1 ? ` · total ${total.toFixed(0)}ms` : "");

  // Console log: a single grouped line so it's easy to watch across runs.
  console.log(
    `[svm] ${result.isError ? "error" : "ok"} — ` +
      (timing.compileMs !== undefined ? `compile=${timing.compileMs.toFixed(1)}ms ` : "") +
      (timing.runMs !== undefined ? `run=${timing.runMs.toFixed(1)}ms ` : "") +
      `total=${total.toFixed(1)}ms`,
  );

  setStatus(
    result.isError ? `Error — ${breakdown}` : `Done — ${breakdown}`,
    result.isError ? "error" : "ok",
  );
}

/** Run the current source on the SVM backend: a precompiled example verbatim, else compile+link live. */
async function runOnSvm(source: string) {
  const runner = await ensureSvm();
  if (!runner) {
    svmError("SVM engine not available (run bash build_demo.sh + demo/svm/build_assets.sh).");
    return;
  }
  try {
    // Fast path: an *unedited* precompiled example runs its shipped .svmb directly.
    const entry = svmEntryForCurrentSource(source);
    if (entry) {
      const tRun = performance.now();
      const bytes = new Uint8Array(await (await fetch(`svm/${entry.svmb}`)).arrayBuffer());
      displayResult(runner.runSvmb(bytes), { runMs: performance.now() - tRun });
      return;
    }
    // Live path: compile edited source to IR in the browser, then link vs the runtime + run.
    const live = await ensureLive();
    if (!live || (!live.compiler && !live.frontend)) {
      svmError("Live compile needs jacl_compiler.svmb (or jacl_emit.wasm) + jaclrt.svm (run demo/svm/build_assets.sh).");
      return;
    }
    // Time the two halves separately: compiling the source to SVM IR (via the self-hosted
    // compiler-guest, or the Emscripten frontend) vs linking + running the program.
    const tCompile = performance.now();
    // Prefer the self-hosted compiler-guest (expands macros in-guest); else the Emscripten frontend.
    const emitted = live.compiler
      ? runner.emitIrViaCompiler(live.compiler, source)
      : live.frontend!.emitIr(source);
    const compileMs = performance.now() - tCompile;
    if ("error" in emitted) {
      displayResult({ output: "", error: emitted.error, isError: true }, { compileMs });
      return;
    }
    const tRun = performance.now();
    const result = runner.linkRun(emitted.ir, live.runtime);
    displayResult(result, { compileMs, runMs: performance.now() - tRun });
  } catch (e) {
    output.textContent = "";
    const span = document.createElement("span");
    span.className = "error-line";
    span.textContent = "SVM run failed: " + (e instanceof Error ? e.message : String(e));
    output.appendChild(span);
    setStatus("Crashed", "error");
  }
}

/** Show an error note when a required SVM asset is missing. */
function svmError(note: string) {
  output.textContent = "";
  const span = document.createElement("span");
  span.className = "error-line";
  span.textContent = note;
  output.appendChild(span);
  setStatus("SVM assets missing", "error");
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
  currentExample = ex; // remember it so the SVM backend can run its precompiled .svmb
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
void Promise.all([initSvm(), loadExamples()]);
