/**
 * Thin TypeScript wrapper around the Emscripten-built JACL VM.
 * The .wasm/.js pair is loaded the old-fashioned way via a <script>
 * tag in index.html (wasm/jacl.js), which puts `createJACL` on the
 * global. We declare its shape here so the rest of the TS code can
 * call into it with types.
 */

/** The Emscripten "Module" surface we actually use. The full shape
 *  is much larger but we only pin what we touch — keeps the surface
 *  honest and avoids importing Emscripten's own typings. */
export interface JaclModule {
  _malloc(size: number): number;
  _free(ptr: number): void;
  lengthBytesUTF8(s: string): number;
  stringToUTF8(s: string, ptr: number, max: number): void;
  UTF8ToString(ptr: number): string;
  _jacl_vm_new(): number;
  _jacl_vm_free(vm: number): void;
  _jacl_eval(vm: number, srcPtr: number): bigint;
  _jacl_is_error_val(v: bigint): number;
  _jacl_error_message_str(vm: number, err: bigint): number;
}

declare global {
  // wasm/jacl.js attaches createJACL to the window. The factory takes
  // an Emscripten init object — `print` / `printErr` hooks are what
  // we use to capture eval output.
  function createJACL(init: {
    print: (line: string) => void;
    printErr: (line: string) => void;
  }): Promise<JaclModule>;
}

export interface RunResult {
  output: string;
  error: string | null;
  isError: boolean;
}

export class JaclVM {
  private mod: JaclModule;
  private vm: number;
  private outputLines: string[] = [];

  private constructor(mod: JaclModule, vm: number) {
    this.mod = mod;
    this.vm = vm;
  }

  static async create(): Promise<JaclVM> {
    const lines: string[] = [];
    const mod = await createJACL({
      print: (line) => { lines.push(line); },
      printErr: (line) => { lines.push(line); },
    });
    const vm = mod._jacl_vm_new();
    const inst = new JaclVM(mod, vm);
    inst.outputLines = lines;
    return inst;
  }

  /** Run `source` against a *fresh* VM. The playground wants each
   *  Run click to be hermetic (so prior globals/structs/ctx don't
   *  bleed in), so we free the old VM and spin up a new one. The
   *  cross-eval persistence work from earlier this session
   *  intentionally only kicks in across `jacl_eval` calls on the
   *  same VM instance, which is what we want here. */
  runFresh(source: string): RunResult {
    this.mod._jacl_vm_free(this.vm);
    this.vm = this.mod._jacl_vm_new();
    this.outputLines.length = 0;

    const len = this.mod.lengthBytesUTF8(source) + 1;
    const ptr = this.mod._malloc(len);
    try {
      this.mod.stringToUTF8(source, ptr, len);
      const result = this.mod._jacl_eval(this.vm, ptr);
      if (this.mod._jacl_is_error_val(result)) {
        const msgPtr = this.mod._jacl_error_message_str(this.vm, result);
        const error = msgPtr
          ? this.mod.UTF8ToString(msgPtr)
          : "Unknown error";
        return { output: this.outputLines.join("\n"), error, isError: true };
      }
      return { output: this.outputLines.join("\n"), error: null, isError: false };
    } finally {
      this.mod._free(ptr);
    }
  }

  dispose() {
    this.mod._jacl_vm_free(this.vm);
  }
}
