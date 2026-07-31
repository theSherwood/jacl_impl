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
 * The cdylib entry is `svm_run_onramp`, which binds the module's manifest imports **by
 * name** (`write` → the stdout stream, `read`/`exit`/… likewise) — the ABI a
 * `synth_manifest_start` `_start` expects, where `print` reaches the host through
 * `call.import "write"`. (`svm_run_pb`'s arity-slot powerbox leaves that import unbound
 * and the module traps.)
 */

/** The subset of the svm-browser cdylib exports this runner touches. */
interface SvmBrowserExports {
  memory: WebAssembly.Memory;
  svm_abi_is64(): number;
  svm_alloc(len: number | bigint): number | bigint;
  svm_dealloc(ptr: number | bigint, len: number | bigint): void;
  svm_run_onramp(
    modPtr: number | bigint,
    modLen: number | bigint,
    stdinPtr: number | bigint,
    stdinLen: number | bigint,
  ): bigint;
  svm_link_run(
    progPtr: number | bigint,
    progLen: number | bigint,
    libPtr: number | bigint,
    libLen: number | bigint,
    entryPtr: number | bigint,
    entryLen: number | bigint,
    stdinPtr: number | bigint,
    stdinLen: number | bigint,
  ): bigint;
  svm_status(): number;
  svm_exit_code(): number;
  svm_stdout_ptr(): number | bigint;
  svm_stdout_len(): number | bigint;
  svm_stderr_ptr(): number | bigint;
  svm_stderr_len(): number | bigint;
}

/** The run-result shape the playground renders (stdout, optional error, error flag). */
export interface RunResult {
  output: string;
  error: string | null;
  isError: boolean;
}

/** `svm_status` codes (mirror `svm-browser`'s `STATUS_*`). */
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
      return `svm_status ${status}`;
  }
}

/**
 * The export the JACL frontend names its program entry (`jacl_emit_ir` emits the program body as
 * func 0; the native link path resolves it under this symbol). Passed to the generic `svm_link_run`
 * so nothing about this name lives in the language-agnostic cdylib.
 */
const JACL_ENTRY = "__jacl_entry";

export class SvmJaclRunner {
  private readonly ex: SvmBrowserExports;
  private readonly is64: boolean;

  private constructor(ex: SvmBrowserExports) {
    this.ex = ex;
    this.is64 = ex.svm_abi_is64() === 1;
  }

  /**
   * Instantiate the svm-browser cdylib. `wasmUrl` points at `svm_browser.wasm`
   * (built by `demo/svm/build_assets.sh`). The plain (non-threads) build needs only the
   * `svm_host.webgpu_op` import stub — no clock/console host functions.
   */
  static async create(wasmUrl: string): Promise<SvmJaclRunner> {
    const source = await fetch(wasmUrl);
    const imports = { svm_host: { webgpu_op: () => -1n } };
    const { instance } = await WebAssembly.instantiateStreaming(source, imports);
    return new SvmJaclRunner(instance.exports as unknown as SvmBrowserExports);
  }

  /** usize marshalling: `i32` (Number) on wasm32, `i64` (BigInt) on wasm64. */
  private usize(x: number): number | bigint {
    return this.is64 ? BigInt(x) : x;
  }

  /** Copy `bytes` into guest linear memory via `svm_alloc`; returns the pointer as a Number. */
  private load(bytes: Uint8Array): number {
    const ptr = this.ex.svm_alloc(this.usize(bytes.length));
    // Re-fetch the view: svm_alloc may have grown (and thus detached) the memory buffer.
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

    ex.svm_run_onramp(modPtr, modLen, inPtr, inLen);

    const status = ex.svm_status();
    const stdout = this.readCapture(ex.svm_stdout_ptr(), ex.svm_stdout_len());
    const stderr = this.readCapture(ex.svm_stderr_ptr(), ex.svm_stderr_len());

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
   * The generic cdylib entry (`svm_link_run`) is language-agnostic — it takes a program unit, a
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
    ex.svm_link_run(
      progPtr,
      this.usize(prog.length),
      rtPtr,
      this.usize(runtime.length),
      entryPtr,
      this.usize(entry.length),
      inPtr,
      inLen,
    );

    const status = ex.svm_status();
    const stdout = this.readCapture(ex.svm_stdout_ptr(), ex.svm_stdout_len());
    const stderr = this.readCapture(ex.svm_stderr_ptr(), ex.svm_stderr_len());
    if (status !== STATUS.OK) {
      const detail = stderr ? `${statusMessage(status)}: ${stderr}` : statusMessage(status);
      return { output: stdout, error: detail, isError: true };
    }
    return { output: stdout + stderr, error: null, isError: false };
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
  UTF8ToString(ptr: number): string;
  _free(ptr: number): void;
}

/** Frontend output: either the SVM IR text, or a compile diagnostic (syntax/type/codegen error). */
export type EmitResult = { ir: string } | { error: string };

export class JaclFrontend {
  private readonly emit: (src: string) => number;
  private readonly mod: JaclEmitModule;

  private constructor(mod: JaclEmitModule) {
    this.mod = mod;
    this.emit = (src: string) => mod.cwrap("jacl_emit_ir", "number", ["string"])(src);
  }

  static async create(): Promise<JaclFrontend> {
    const mod = await createJaclEmit();
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
