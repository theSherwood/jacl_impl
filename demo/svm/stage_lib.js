// Emscripten JS-library: provides `jacl_svm_stage` to jacl_emit.wasm so the AOT-compiled frontend can
// stage macros by bouncing just the macro-body module to the SVM engine (the svm-browser cdylib).
// The C bridge (codegen/selfhost/macro_staging/stage_bridge.c) calls this once per macro invocation:
// it hands us the codegen'd macro-body object + the arg wire, and expects the result wire back.
//
// The actual SVM run is provided by the host via `Module.svmStageRun(moduleBytes, argWire) ->
// Uint8Array | null` — wired up in the playground to link the body against jaclrt_staging.svm and run
// it on the cdylib. Everything crossing this boundary is raw bytes; there is no AST/memory marshalling.
addToLibrary({
  jacl_svm_stage__deps: ['malloc', 'free'],
  jacl_svm_stage: function (modPtr, modLen, argPtr, argLen, outPtrPtr, outLenPtr) {
    // Copy inputs out of the Emscripten heap first — a later _malloc may grow (and detach) it.
    const mod = HEAPU8.slice(modPtr, modPtr + modLen);
    const arg = argLen ? HEAPU8.slice(argPtr, argPtr + argLen) : new Uint8Array(0);
    let res = null;
    try {
      res = typeof Module.svmStageRun === 'function' ? Module.svmStageRun(mod, arg) : null;
    } catch (e) {
      res = null;
    }
    if (!res || res.length === undefined) return 3; // non-zero → the bridge reports a staging error
    const p = _malloc(res.length || 1);
    HEAPU8.set(res, p);
    setValue(outPtrPtr, p, '*'); // *out_ptr = the result-wire buffer
    setValue(outLenPtr, res.length, 'i32'); // *out_len (size_t = i32 on wasm32)
    return 0;
  },
  jacl_svm_stage_free__deps: ['free'],
  jacl_svm_stage_free: function (ptr, _len) {
    _free(ptr);
  },
});
