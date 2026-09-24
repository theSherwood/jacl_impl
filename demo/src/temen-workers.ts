/**
 * The **parallel Worker driver** for a JACL Run (jacl #152): the program's vCPUs — the scheduler's
 * pool workers — each on their own Web Worker over one shared `WebAssembly.Memory`, through temen's
 * own orchestration (`par.js` + `worker.js`, shipped under `temen-web/` in the layout they resolve
 * their engine from) and its **threads** build of the engine. The single-threaded on-ramp
 * ({@link TemenJaclRunner.runTemen}) multiplexes those workers on one thread, so a `parallel` block
 * is concurrent there but not parallel.
 *
 * The runtime learns how many workers to start from the run's §3e environment (`JACL_WORKERS=N`,
 * read once per run by `sched_init`); a run with no entry gets 1, which is why the same `.temen`
 * serves both drivers.
 *
 * Needs cross-origin isolation (shared memory); {@link WorkerDriver.create} returns `null` without it,
 * and the playground stays on the single-threaded path.
 */
import type { RunResult } from "./temen-jacl-wasm";

/** A loaded threads engine, as temen's `par.js` returns it (opaque here beyond what we pass back). */
interface Engine {
  module: WebAssembly.Module;
  memory: WebAssembly.Memory;
}

/** What temen's `par.js` exports (the subset used here). */
interface ParModule {
  loadEngine(prev?: Engine | null): Promise<Engine>;
  makeRunner(engine: Engine): (
    guest: Uint8Array,
    opts: { onramp: boolean; env: string[]; stdin?: Uint8Array | null; winSize: number },
  ) => Promise<{ value: bigint | null; exit: number | null; started: number }>;
  readParStdout(engine: Engine): string;
  readParStderr(engine: Engine): string;
}

/**
 * The window a module declares, from its encoded header: the 16-byte container header, then the memory
 * descriptor — a presence flag, then `size_log2` (temen WIRE.md). The Worker driver's root reserves
 * exactly the window it is given, so it must be the module's own.
 */
function declaredWindow(module: Uint8Array): number {
  const magic = "TEMEN\0\0\0";
  for (let i = 0; i < magic.length; i++) {
    if (module[i] !== magic.charCodeAt(i)) throw new Error("not a .temen module");
  }
  if (module[16] !== 1) throw new Error("the module declares no memory");
  return 2 ** module[17];
}

export class WorkerDriver {
  private constructor(
    private readonly par: ParModule,
    /** The first engine: its compiled module is reused for every run's fresh one. */
    private readonly compiled: Engine,
    /** How many pool workers a run asks for. */
    readonly workers: number,
  ) {}

  /**
   * Load temen's Worker orchestration and threads engine from `base` (the `temen-web/` directory), or
   * `null` when this page cannot run it (not cross-origin isolated, or a single core, where Workers
   * buy nothing).
   */
  static async create(base: string): Promise<WorkerDriver | null> {
    if (!self.crossOriginIsolated || typeof SharedArrayBuffer === "undefined") return null;
    const cores = navigator.hardwareConcurrency || 1;
    if (cores < 2) return null;
    // A runtime URL, so the bundler leaves it alone: par.js resolves worker.js and the engine
    // relative to itself.
    const url = new URL(`${base}/web/par.js`, document.baseURI).href;
    const par = (await import(/* @vite-ignore */ url)) as ParModule;
    // The runtime caps its pool at 16; beyond the core count Workers only contend.
    return new WorkerDriver(par, await par.loadEngine(), Math.min(cores, 16));
  }

  /** Run a linked `.temen` (a precompiled card, or {@link TemenJaclRunner.linkEncode}'s output). */
  async run(module: Uint8Array, stdin?: Uint8Array): Promise<RunResult> {
    // A fresh engine per run (~0.5 ms: the compiled module is reused): a run never frees what it
    // allocates in the shared memory — the 64 MiB window, every vCPU's stack — so reusing one engine
    // would grow it by that much on every Run. The previous run's memory goes with its Workers.
    const engine = await this.par.loadEngine(this.compiled);
    let exit: number | null;
    try {
      ({ exit } = await this.par.makeRunner(engine)(module, {
        onramp: true,
        env: [`JACL_WORKERS=${this.workers}`],
        stdin: stdin ?? null,
        winSize: declaredWindow(module),
      }));
    } catch (e) {
      const why = e instanceof Error ? e.message : String(e);
      return { output: this.par.readParStdout(engine), error: `the program trapped on the Worker driver: ${why}`, isError: true };
    }
    const out = this.par.readParStdout(engine);
    const err = this.par.readParStderr(engine);
    if (exit !== null) {
      // The single-threaded path reports any `exit` as a non-OK status too.
      return { output: out, error: `the program exited with code ${exit}${err ? `: ${err}` : ""}`, isError: true };
    }
    return { output: out + err, error: null, isError: false };
  }
}
