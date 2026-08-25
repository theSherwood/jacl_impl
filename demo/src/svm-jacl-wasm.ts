/**
 * SVM-backend run path for the playground.
 *
 * Runs a JACL program that has been compiled to an encoded SVM-IR module (`.svmb`:
 * frontend → codegen → link vs the translated runtime → powerbox `_start` → encode; see
 * `runtime/harness/src/bin/emit_svmb.rs`) through the **svm-browser cdylib** on `wasm32`.
 * The cdylib decodes the module and runs function 0 on the fail-closed **bytecode
 * interpreter** — the browser's only wasm-safe engine — capturing stdout/stderr.
 *
 * This is the run half of the SVM-in-browser migration (docs/SVM_BROWSER_PLAN.md), and the
 * playground's sole backend. It exposes a {@link RunResult} shape `playground.ts` consumes
 * directly. The compile half — turning *edited* source into IR in the browser — uses an
 * Emscripten build of the LLVM-free frontend+codegen (`jacl_emit.wasm`, {@link JaclFrontend});
 * unedited examples run **precompiled** `.svmb` blobs.
 *
 * The cdylib entry is `temen_run_onramp`, which binds the module's manifest imports **by
 * name** (`write` → the stdout stream, `read`/`exit`/… likewise) — the ABI a
 * `synth_manifest_start` `_start` expects, where `print` reaches the host through
 * `call.import "write"`. (`temen_run_pb`'s arity-slot powerbox leaves that import unbound
 * and the module traps.)
 */

/** The subset of the svm-browser cdylib exports this runner touches. */
interface SvmBrowserExports {
  memory: WebAssembly.Memory;
  temen_abi_is64(): number;
  temen_alloc(len: number | bigint): number | bigint;
  temen_dealloc(ptr: number | bigint, len: number | bigint): void;
  temen_run_onramp(
    modPtr: number | bigint,
    modLen: number | bigint,
    stdinPtr: number | bigint,
    stdinLen: number | bigint,
  ): bigint;
  temen_link_run(
    progPtr: number | bigint,
    progLen: number | bigint,
    libPtr: number | bigint,
    libLen: number | bigint,
    entryPtr: number | bigint,
    entryLen: number | bigint,
    stdinPtr: number | bigint,
    stdinLen: number | bigint,
  ): bigint;
  // Warm-runtime snapshot (SVM_WARM_COMPILER.md Slice 3): open a two-phase (`warmup`/`eval_run`)
  // guest once — the host runs `warmup` and snapshots the post-init window — then `eval_run` each
  // input over the restored warm image (skipping the guest init floor). `open` returns `-1` on
  // failure; `eval` reports via `temen_status`/`temen_stdout_*` like `temen_run_onramp`.
  temen_warm_open(modPtr: number | bigint, modLen: number | bigint): bigint;
  temen_warm_eval(stdinPtr: number | bigint, stdinLen: number | bigint): bigint;
  temen_warm_close(): void;
  temen_status(): number;
  temen_exit_code(): number;
  temen_stdout_ptr(): number | bigint;
  temen_stdout_len(): number | bigint;
  temen_stderr_ptr(): number | bigint;
  temen_stderr_len(): number | bigint;
}

/** The run-result shape the playground renders (stdout, optional error, error flag). */
export interface RunResult {
  output: string;
  error: string | null;
  isError: boolean;
}

/** `temen_status` codes (mirror `svm-browser`'s `STATUS_*`). */
const STATUS = {
  OK: 0,
  DECODE_ERR: 1,
  UNSUPPORTED: 2,
  TRAP: 3,
  BAD_RESULT: 4,
} as const;

function statusMessage(status: number): string {
  switch (status) {
    case STATUS.DECODE_ERR:
      return "could not decode the encoded module (.svmb)";
    case STATUS.UNSUPPORTED:
      return "a function is outside the bytecode engine's subset (STATUS_UNSUPPORTED)";
    case STATUS.TRAP:
      return "the guest trapped during execution";
    case STATUS.BAD_RESULT:
      return "the entry returned an unexpected result shape";
    default:
      return `temen_status ${status}`;
  }
}

/**
 * The export the JACL frontend names its program entry (`jacl_emit_ir` emits the program body as
 * func 0; the native link path resolves it under this symbol). Passed to the generic `temen_link_run`
 * so nothing about this name lives in the language-agnostic cdylib.
 */
const JACL_ENTRY = "__jacl_entry";

export class SvmJaclRunner {
  private readonly ex: SvmBrowserExports;
  private readonly is64: boolean;

  private constructor(ex: SvmBrowserExports) {
    this.ex = ex;
    this.is64 = ex.temen_abi_is64() === 1;
  }

  /**
   * Instantiate the svm-browser cdylib. `wasmUrl` points at `temen_browser.wasm`
   * (built by `demo/svm/build_assets.sh`). The plain (non-threads) build needs only the
   * `temen_host.webgpu_op` import stub — no clock/console host functions.
   */
  static async create(wasmUrl: string): Promise<SvmJaclRunner> {
    const source = await fetch(wasmUrl);
    const imports = { temen_host: { webgpu_op: () => -1n } };
    const { instance } = await WebAssembly.instantiateStreaming(source, imports);
    return new SvmJaclRunner(instance.exports as unknown as SvmBrowserExports);
  }

  /** usize marshalling: `i32` (Number) on wasm32, `i64` (BigInt) on wasm64. */
  private usize(x: number): number | bigint {
    return this.is64 ? BigInt(x) : x;
  }

  /** Copy `bytes` into guest linear memory via `temen_alloc`; returns the pointer as a Number. */
  private load(bytes: Uint8Array): number {
    const ptr = this.ex.temen_alloc(this.usize(bytes.length));
    // Re-fetch the view: temen_alloc may have grown (and thus detached) the memory buffer.
    new Uint8Array(this.ex.memory.buffer).set(bytes, Number(ptr));
    return Number(ptr);
  }

  private readCapture(ptr: number | bigint, len: number | bigint): string {
    const p = Number(ptr);
    const n = Number(len);
    if (n === 0) return "";
    // `.slice()` copies out before the next call can reuse/detach the buffer.
    return new TextDecoder().decode(new Uint8Array(this.ex.memory.buffer, p, n).slice());
  }

  /**
   * Run a precompiled `.svmb` module. `stdin` is optional. Returns the captured stdout, and
   * on a non-OK status an error string — the same {@link RunResult} the old backend produced.
   */
  runSvmb(svmb: Uint8Array, stdin?: Uint8Array): RunResult {
    const ex = this.ex;
    const modPtr = this.load(svmb);
    const modLen = this.usize(svmb.length);

    let inPtr: number | bigint = this.usize(0);
    let inLen: number | bigint = this.usize(0);
    if (stdin && stdin.length > 0) {
      inPtr = this.load(stdin);
      inLen = this.usize(stdin.length);
    }

    ex.temen_run_onramp(modPtr, modLen, inPtr, inLen);

    const status = ex.temen_status();
    const stdout = this.readCapture(ex.temen_stdout_ptr(), ex.temen_stdout_len());
    const stderr = this.readCapture(ex.temen_stderr_ptr(), ex.temen_stderr_len());

    if (status !== STATUS.OK) {
      const detail = stderr ? `${statusMessage(status)}: ${stderr}` : statusMessage(status);
      return { output: stdout, error: detail, isError: true };
    }
    return { output: stdout + stderr, error: null, isError: false };
  }

  /**
   * The **live-editing** path: link the SVM IR the JACL frontend emitted (`jacl_emit_ir`; see
   * {@link JaclFrontend}) against the JACL runtime (`jaclrt.svm` bytes) and run it — no precompiled
   * `.svmb` needed. `programIr` is the frontend's raw output: self-contained svm-text (wire v9 —
   * own-data addresses are inline `data.self` instructions the linker resolves, so there is no
   * separate relocation buffer).
   *
   * The generic cdylib entry (`temen_link_run`) is language-agnostic — it takes a program unit, a
   * library unit (each text or a `.svmo` binary object, sniffed by magic), and an entry-export name.
   * The only JACL-frontend specific here is the `__jacl_entry` entry name.
   */
  linkRun(programIr: string, runtime: Uint8Array, stdin?: Uint8Array): RunResult {
    const ex = this.ex;
    const prog = new TextEncoder().encode(programIr);
    const entry = new TextEncoder().encode(JACL_ENTRY);
    const progPtr = this.load(prog);
    const rtPtr = this.load(runtime);
    const entryPtr = this.load(entry);
    let inPtr: number | bigint = this.usize(0);
    let inLen: number | bigint = this.usize(0);
    if (stdin && stdin.length > 0) {
      inPtr = this.load(stdin);
      inLen = this.usize(stdin.length);
    }
    ex.temen_link_run(
      progPtr,
      this.usize(prog.length),
      rtPtr,
      this.usize(runtime.length),
      entryPtr,
      this.usize(entry.length),
      inPtr,
      inLen,
    );

    const status = ex.temen_status();
    const stdout = this.readCapture(ex.temen_stdout_ptr(), ex.temen_stdout_len());
    const stderr = this.readCapture(ex.temen_stderr_ptr(), ex.temen_stderr_len());
    if (status !== STATUS.OK) {
      const detail = stderr ? `${statusMessage(status)}: ${stderr}` : statusMessage(status);
      return { output: stdout, error: detail, isError: true };
    }
    return { output: stdout + stderr, error: null, isError: false };
  }

  /**
   * Run a **binary program object** against `runtime` with raw byte I/O and return raw stdout — the
   * macro-body staging primitive. The AOT frontend codegens each macro body to an svm-encode object
   * and hands it here (linked at `__jacl_entry` against jaclrt_staging.svm, arg wire on stdin); the
   * returned bytes are the result wire it decodes back to an AST. `null` on a non-OK run.
   */
  linkRunRaw(programBytes: Uint8Array, runtime: Uint8Array, stdin: Uint8Array): Uint8Array | null {
    const ex = this.ex;
    const progPtr = this.load(programBytes);
    const rtPtr = this.load(runtime);
    const entry = new TextEncoder().encode(JACL_ENTRY);
    const entryPtr = this.load(entry);
    let inPtr: number | bigint = this.usize(0);
    let inLen: number | bigint = this.usize(0);
    if (stdin.length > 0) {
      inPtr = this.load(stdin);
      inLen = this.usize(stdin.length);
    }
    ex.temen_link_run(
      progPtr,
      this.usize(programBytes.length),
      rtPtr,
      this.usize(runtime.length),
      entryPtr,
      this.usize(entry.length),
      inPtr,
      inLen,
    );
    if (ex.temen_status() !== STATUS.OK) return null;
    const n = Number(ex.temen_stdout_len());
    if (n === 0) return new Uint8Array(0);
    return new Uint8Array(this.ex.memory.buffer, Number(ex.temen_stdout_ptr()), n).slice();
  }

  /**
   * The **self-hosted frontend** path: compile JACL `source` to SVM IR by running the JACL compiler
   * *as an SVM guest* (`jacl_compiler.svmb` — the LLVM-free frontend+codegen translated to SVM). The
   * compiler reads the source on stdin and writes the IR on stdout; its `_start` is the emit driver.
   *
   * Unlike the Emscripten {@link JaclFrontend} (`jacl_emit.wasm`), this expands `defmacro`s **in-guest**
   * via the §22 `Jit` capability the on-ramp grants a `vm_jit_*`-importing guest (`onramp_exec` runs it
   * on the tree-walker so its import-bound `invoke`/`install` reach the driver) — so macro-bearing tour
   * programs compile instead of trapping. The emitted IR is the same wire form {@link JaclFrontend}
   * produces (it is the same `jacl_emit_ir` codegen), so {@link linkRun} consumes it unchanged.
   */
  emitIrViaCompiler(compilerSvmb: Uint8Array, source: string): EmitResult {
    const out = this.runSvmb(compilerSvmb, new TextEncoder().encode(source));
    if (out.isError) return { error: out.error ?? "compiler-guest run failed" };
    const MARK = "%%ERROR%%\n";
    if (out.output.startsWith(MARK)) return { error: out.output.slice(MARK.length) };
    return { ir: out.output };
  }

  private warmOpened = false;

  /**
   * Open a **warm-runtime snapshot** session over the two-phase compiler card
   * (`jacl_compiler_snapshot.svmb`): the host runs its `warmup` (the prelude init) once and snapshots
   * the window. Call once, then {@link warmEval} per compile — each restores the warm image and JITs
   * `eval_run`, skipping the ~450 ms/compile init+prelude floor (SVM_WARM_COMPILER.md Slice 3; ~2×
   * over `emitIrViaCompiler`). Returns `false` if the engine refuses the card (then fall back).
   */
  warmOpen(snapshotSvmb: Uint8Array): boolean {
    const modPtr = this.load(snapshotSvmb);
    const opened = this.ex.temen_warm_open(modPtr, this.usize(snapshotSvmb.length));
    this.warmOpened = opened !== -1n && this.ex.temen_status() === STATUS.OK;
    return this.warmOpened;
  }

  /** Whether a warm session is currently open (via {@link warmOpen}). */
  get isWarmOpen(): boolean {
    return this.warmOpened;
  }

  /** Compile `source` over the open warm session — the fast path (see {@link warmOpen}). */
  warmEval(source: string): EmitResult {
    if (!this.warmOpened) return { error: "warm session not open" };
    const inBytes = new TextEncoder().encode(source);
    const inPtr = this.load(inBytes);
    this.ex.temen_warm_eval(inPtr, this.usize(inBytes.length));
    const status = this.ex.temen_status();
    const stdout = this.readCapture(this.ex.temen_stdout_ptr(), this.ex.temen_stdout_len());
    const stderr = this.readCapture(this.ex.temen_stderr_ptr(), this.ex.temen_stderr_len());
    if (status !== STATUS.OK) return { error: stderr ? `${statusMessage(status)}: ${stderr}` : statusMessage(status) };
    const MARK = "%%ERROR%%\n";
    if (stdout.startsWith(MARK)) return { error: stdout.slice(MARK.length) };
    return { ir: stdout };
  }

  /** Tear down the warm session and free its owned window. */
  warmClose(): void {
    this.ex.temen_warm_close();
    this.warmOpened = false;
  }
}

/**
 * The in-browser JACL **frontend**: the LLVM-free lexer+parser+codegen (`src/jacl_emit.c` +
 * `codegen/*.c`) compiled to wasm by Emscripten (`demo/svm/build_emit_wasm.sh`), exposing
 * `jacl_emit_ir(source) -> SVM IR text`. Loaded via a `<script src="wasm/jacl_emit.js">` tag, which
 * puts `createJaclEmit` on the global.
 */
declare global {
  function createJaclEmit(init?: {
    locateFile?: (path: string) => string;
  }): Promise<JaclEmitModule>;
}
interface JaclEmitModule {
  cwrap(name: string, ret: string, args: string[]): (...a: unknown[]) => number;
  ccall(name: string, ret: string | null, argTypes: string[], args: unknown[]): unknown;
  UTF8ToString(ptr: number): string;
  _free(ptr: number): void;
  /** Set by us: the macro-body staging callback the `jacl_svm_stage` JS-library import calls. */
  svmStageRun?: (moduleBytes: Uint8Array, argWire: Uint8Array) => Uint8Array | null;
}

/** Runs a codegen'd macro-body module on SVM and returns the result wire — bytes in, bytes out. */
export type MacroStageRun = (moduleBytes: Uint8Array, argWire: Uint8Array) => Uint8Array | null;

/** Frontend output: either the SVM IR text, or a compile diagnostic (syntax/type/codegen error). */
export type EmitResult = { ir: string } | { error: string };

export class JaclFrontend {
  private readonly emit: (src: string) => number;
  private readonly mod: JaclEmitModule;

  private constructor(mod: JaclEmitModule) {
    this.mod = mod;
    this.emit = (src: string) => mod.cwrap("jacl_emit_ir", "number", ["string"])(src);
  }

  /**
   * Load the AOT-compiled JACL frontend. Pass `stageRun` to make it **macro-capable**: the frontend
   * codegens each macro body to an SVM object and hands it to `stageRun` (which runs it on the
   * cdylib against jaclrt_staging.svm) via the `jacl_svm_stage` bridge — so 99% of compilation runs
   * at native wasm speed and only tiny macro bodies touch SVM. Without it, macros error (emit-only).
   */
  static async create(stageRun?: MacroStageRun): Promise<JaclFrontend> {
    const mod = await createJaclEmit();
    if (stageRun) {
      mod.svmStageRun = stageRun;
      mod.ccall("jacl_install_svm_stage_hook", null, [], []); // arm the staging hook
    }
    return new JaclFrontend(mod);
  }

  /** Compile a single JACL source string to SVM IR text, or return its compile diagnostic. */
  emitIr(source: string): EmitResult {
    const ptr = this.emit(source);
    const out = this.mod.UTF8ToString(ptr);
    this.mod._free(ptr);
    const MARK = "%%ERROR%%\n";
    if (out.startsWith(MARK)) return { error: out.slice(MARK.length) };
    return { ir: out };
  }
}
