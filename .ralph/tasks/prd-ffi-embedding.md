# PRD: FFI & Embedding API (M17)

## Introduction

Add a C embedding API to JACL so that host programs can create VMs, evaluate code, register native C functions, and marshal values across the C↔JACL boundary. This milestone also adds callback support via libffi closures, allowing JACL closures to be passed as C function pointers to native code. The embedding API is the foundation for all future C interop — dynamic FFI (dlopen/libffi calls from JACL scripts) is a separate future milestone.

The API targets macOS (arm64/x86_64), Linux (x86_64/aarch64), and WebAssembly via Emscripten (with the caveat that libffi closure trampolines are unavailable on WASM — callback support is desktop-only).

## Goals

- Provide a clean C embedding API: VM lifecycle, eval, call, value marshaling
- Allow host programs to register C functions callable from JACL as native procs
- Provide a GC handle API so C code can safely hold references to JACL values across GC cycles
- Support JACL closures as C function pointers via libffi closure trampolines (macOS/Linux)
- Struct interop: C code can read/write fields of JACL structs through the embedding API
- All existing M0–M13 tests continue to pass
- Builds cleanly on macOS, Linux, and Emscripten (WASM)

## User Stories

### US-001: Public header — `jacl.h`

**Description:** As a C developer embedding JACL, I need a single public header that declares the entire embedding API so I can include one file and use JACL from my application.

**Acceptance Criteria:**
- [ ] `include/jacl.h` exists with all public types and function declarations
- [ ] Header is self-contained (no transitive includes of internal headers)
- [ ] Header uses `JACL_API` annotation macro for export/visibility control
- [ ] All public types prefixed with `Jacl` or `jacl_` to avoid namespace collisions
- [ ] `#ifdef __cplusplus extern "C"` guards for C++ compatibility
- [ ] Header includes versioning macro: `JACL_VERSION_MAJOR`, `JACL_VERSION_MINOR`, `JACL_VERSION_PATCH`
- [ ] Build passes, all existing tests pass

### US-002: VM lifecycle API

**Description:** As a C developer, I need to create, configure, and destroy JACL VMs so I can run JACL code from my application.

**Acceptance Criteria:**
- [ ] `JaclVM* jacl_vm_new(void)` — allocates and initializes a new VM with default settings
- [ ] `void jacl_vm_free(JaclVM* vm)` — destroys VM, frees all associated memory (GC heap, intern table, struct registry, etc.)
- [ ] `JaclVM* jacl_vm_new_ex(const JaclConfig* config)` — create VM with custom configuration (initial heap size, max heap size, GC threshold)
- [ ] Multiple VMs can coexist in the same process without interfering
- [ ] `jacl_vm_free` on NULL is a safe no-op
- [ ] Valgrind/ASAN clean: no leaks or errors after create+destroy cycle
- [ ] Build passes, all existing tests pass

### US-003: Eval and script execution

**Description:** As a C developer, I need to evaluate JACL source code strings and get results so I can script my application.

**Acceptance Criteria:**
- [ ] `JaclVal jacl_eval(JaclVM* vm, const char* source)` — parse, compile, and execute a JACL source string, return the result value
- [ ] `JaclVal jacl_eval_file(JaclVM* vm, const char* path)` — read file, eval contents
- [ ] On parse/compile/runtime error, returns an error-flagged value (existing error system)
- [ ] `bool jacl_is_error(JaclVal val)` — check if a value has the error flag set
- [ ] `const char* jacl_error_message(JaclVM* vm, JaclVal err)` — extract error message string
- [ ] Multiple sequential `jacl_eval` calls share the same VM state (globals persist)
- [ ] Build passes, all existing tests pass

### US-004: Value creation and extraction

**Description:** As a C developer, I need to create JACL values from C types and extract C types from JACL values so I can marshal data across the boundary.

**Acceptance Criteria:**
- [ ] Constructors: `jacl_nil()`, `jacl_bool(bool)`, `jacl_i32(int32_t)`, `jacl_i64(JaclVM*, int64_t)`, `jacl_u32(uint32_t)`, `jacl_u64(JaclVM*, uint64_t)`, `jacl_f32(float)`, `jacl_f64(JaclVM*, double)`
- [ ] `JaclVal jacl_string(JaclVM* vm, const char* s, size_t len)` — create JACL string from C buffer (copies data, interns if short)
- [ ] `JaclVal jacl_string_cstr(JaclVM* vm, const char* s)` — convenience for null-terminated strings
- [ ] Extractors: `jacl_as_i32(JaclVal)`, `jacl_as_i64(JaclVal)`, `jacl_as_f32(JaclVal)`, `jacl_as_f64(JaclVal)`, `jacl_as_bool(JaclVal)`
- [ ] `const char* jacl_as_cstr(JaclVM* vm, JaclVal val, size_t* len_out)` — get pointer to string data (valid until next GC)
- [ ] Type query: `JaclType jacl_typeof(JaclVal val)` — returns the tag/type enum
- [ ] `bool jacl_is_nil(JaclVal)`, `jacl_is_bool(JaclVal)`, `jacl_is_i32(JaclVal)`, etc.
- [ ] Inline types (nil, bool, i32, f32, u32) require no VM pointer — zero allocation
- [ ] Build passes, all existing tests pass

### US-005: GC handle API

**Description:** As a C developer, I need to prevent the GC from collecting values I'm holding in C so that my references remain valid across GC cycles.

**Acceptance Criteria:**
- [ ] `JaclHandle jacl_handle_new(JaclVM* vm, JaclVal val)` — creates a GC root handle for the value
- [ ] `JaclVal jacl_handle_get(JaclHandle h)` — retrieves the current value from a handle
- [ ] `void jacl_handle_free(JaclVM* vm, JaclHandle h)` — releases the handle (value becomes collectible if no other refs)
- [ ] Handles are traced during GC mark phase as additional roots
- [ ] Handle storage is a simple growable array on the VM (no per-handle heap allocation)
- [ ] Stress test: create handles, trigger GC, verify values survive
- [ ] Stress test: free handles, trigger GC, verify values are collected
- [ ] Build passes, all existing tests pass

### US-006: Registering native C functions

**Description:** As a C developer, I need to register C functions that JACL code can call as procs so I can extend JACL with native functionality.

**Acceptance Criteria:**
- [ ] Native function signature: `typedef JaclVal (*JaclNativeFn)(JaclVM* vm, JaclVal* args, int argc)`
- [ ] `bool jacl_register_fn(JaclVM* vm, const char* name, JaclNativeFn fn, int arity)` — register a native function; arity=-1 for variadic
- [ ] Registered functions are callable from JACL: `[my-native-fn 1 2 3]`
- [ ] Arity mismatch produces a compile-time error (same as regular procs)
- [ ] Native function can return error values to propagate errors to JACL
- [ ] Add `JACL_TAG_NATIVE_FN` value tag (or embed in closure representation — implementation choice)
- [ ] `OP_CALL` dispatch handles native functions without pushing a bytecode call frame
- [ ] Native functions can call back into JACL via `jacl_call` (re-entrant)
- [ ] Build passes, all existing tests pass

### US-007: Calling JACL functions from C

**Description:** As a C developer, I need to call JACL-defined procs from C so I can invoke callbacks and scripted logic from my host application.

**Acceptance Criteria:**
- [ ] `JaclVal jacl_call(JaclVM* vm, JaclVal fn, JaclVal* args, int argc)` — call a JACL closure or native function with given arguments
- [ ] `JaclVal jacl_call_named(JaclVM* vm, const char* name, JaclVal* args, int argc)` — look up a global proc by name and call it
- [ ] Returns the function's return value, or error-flagged value on failure
- [ ] Handles re-entrant calls (C calls JACL which calls native C which calls JACL)
- [ ] Proper stack management: call frame pushed/popped correctly
- [ ] Build passes, all existing tests pass

### US-008: Struct interop from C

**Description:** As a C developer, I need to create, read, and modify JACL structs from C so I can work with structured data across the embedding boundary.

**Acceptance Criteria:**
- [ ] `JaclVal jacl_struct_new(JaclVM* vm, const char* type_name, JaclVal* fields, int field_count)` — instantiate a struct by type name
- [ ] `JaclVal jacl_struct_get(JaclVM* vm, JaclVal s, const char* field_name)` — read a field by name
- [ ] `bool jacl_struct_set(JaclVM* vm, JaclVal s, const char* field_name, JaclVal value)` — write a field by name
- [ ] `const char* jacl_struct_type_name(JaclVM* vm, JaclVal s)` — get the struct's type name
- [ ] Type checking on set: returns false and sets VM error if value type doesn't match field type
- [ ] Field access uses the existing StructTypeRegistry for offset/type lookup
- [ ] GC write barrier triggered on struct field mutation of heap-typed fields
- [ ] Build passes, all existing tests pass

### US-009: Closure-to-function-pointer trampolines (libffi)

**Description:** As a C developer, I need to obtain a C function pointer from a JACL closure so I can pass JACL callbacks to C libraries that expect function pointers (e.g., qsort, signal handlers, GUI callbacks).

**Acceptance Criteria:**
- [ ] `JaclTrampoline* jacl_trampoline_new(JaclVM* vm, JaclVal closure, const char* sig)` — create a C-callable function pointer from a JACL closure
- [ ] Signature string specifies C types: `"i32(i32,i32)"` means returns i32, takes two i32 args. Supported types: `void`, `i32`, `i64`, `u32`, `u64`, `f32`, `f64`, `ptr`
- [ ] `void* jacl_trampoline_ptr(JaclTrampoline* t)` — get the raw C function pointer
- [ ] `void jacl_trampoline_free(JaclVM* vm, JaclTrampoline* t)` — release the trampoline and its libffi resources
- [ ] The trampoline pins the closure via a GC handle (prevents collection while trampoline is alive)
- [ ] When the function pointer is called, it marshals C args → JaclVal, calls the closure via `jacl_call`, and marshals the return value back
- [ ] Uses `ffi_closure_alloc` / `ffi_prep_closure_loc` from libffi
- [ ] Works on macOS (arm64, x86_64) and Linux (x86_64, aarch64)
- [ ] Compile-time `#ifdef JACL_HAS_LIBFFI` guard — trampoline API is unavailable when libffi is not present (e.g., WASM builds)
- [ ] Build passes, all existing tests pass

### US-010: Build system — libffi integration

**Description:** As a developer building JACL, I need the build system to detect and link libffi so that trampoline support is available on supported platforms.

**Acceptance Criteria:**
- [ ] Makefile/build script detects libffi via `pkg-config --cflags --libs libffi` (or fallback to standard paths)
- [ ] If libffi is found: defines `JACL_HAS_LIBFFI=1`, links `-lffi`
- [ ] If libffi is not found: builds without trampoline support, no error (graceful degradation)
- [ ] Emscripten build: explicitly disables libffi (no detection attempt)
- [ ] `jacl_has_trampolines()` runtime query returns true/false based on compile-time flag
- [ ] Build passes on macOS, Linux, and Emscripten

### US-011: Emscripten / WASM build target

**Description:** As a developer, I need JACL to compile to WebAssembly via Emscripten so that the embedding API works in browser and WASI contexts.

**Acceptance Criteria:**
- [ ] `emcc` compiles all of JACL to a `.wasm` + `.js` module
- [ ] Embedding API functions are exported via `EMSCRIPTEN_KEEPALIVE` or `-s EXPORTED_FUNCTIONS`
- [ ] Platform abstraction: threading primitives (`platform.h`) compile as no-ops or single-threaded stubs under Emscripten
- [ ] GC works in single-threaded mode (no epoch atomics needed)
- [ ] Trampoline API stubs return error when called (not a compile error, just runtime "unsupported")
- [ ] Basic test: create VM, eval expression, extract result — works in Node.js via Emscripten
- [ ] Build passes with `emcc`

### US-012: E2E tests — embedding basics

**Description:** As a developer, I need end-to-end tests for the core embedding API to verify correctness.

**Acceptance Criteria:**
- [ ] Test: create VM, eval `[+ 1 2]`, extract i32 result, verify == 3, free VM
- [ ] Test: eval with syntax error returns error-flagged value
- [ ] Test: eval with runtime error returns error-flagged value with message
- [ ] Test: create all value types from C, round-trip through eval, extract back
- [ ] Test: string creation, extraction, and GC survival
- [ ] Test: multiple sequential evals share global state
- [ ] Test: multiple independent VMs don't interfere
- [ ] All tests pass, no memory leaks (ASAN clean)

### US-013: E2E tests — native functions

**Description:** As a developer, I need tests for native function registration and calling conventions.

**Acceptance Criteria:**
- [ ] Test: register native function, call from JACL, verify args and return value
- [ ] Test: variadic native function receives correct argc/argv
- [ ] Test: native function returns error value, JACL propagates error flag
- [ ] Test: arity mismatch on native function call → compile error
- [ ] Test: native function calls `jacl_call` back into JACL (re-entrant)
- [ ] Test: register multiple native functions, call them in sequence
- [ ] All tests pass, no memory leaks

### US-014: E2E tests — GC handles

**Description:** As a developer, I need tests proving the handle API keeps values alive across GC cycles.

**Acceptance Criteria:**
- [ ] Test: create handle, force GC, verify value survives
- [ ] Test: free handle, force GC, verify value is collected (use allocation counting)
- [ ] Test: create many handles (100+), GC stress, verify all values survive
- [ ] Test: handle to a struct — struct fields survive GC
- [ ] Test: handle to a closure — closure and its upvalues survive GC
- [ ] All tests pass, no memory leaks

### US-015: E2E tests — struct interop and trampolines

**Description:** As a developer, I need tests for struct manipulation from C and closure-to-function-pointer trampolines.

**Acceptance Criteria:**
- [ ] Test: create struct from C, read fields, verify values
- [ ] Test: mutate struct field from C, read back from JACL, verify change
- [ ] Test: type mismatch on struct set returns error
- [ ] Test: create trampoline from JACL closure, call the C function pointer, verify closure executes with correct args
- [ ] Test: trampoline with various signatures (void return, multiple args, different types)
- [ ] Test: free trampoline, verify closure becomes collectible
- [ ] Test: trampoline used as callback to C `qsort` — sort an array using JACL comparator
- [ ] All tests pass, no memory leaks (trampoline tests skipped on WASM)

## Functional Requirements

- FR-1: `jacl.h` is the single public header for the embedding API
- FR-2: `jacl_vm_new()` / `jacl_vm_free()` manage VM lifecycle; multiple VMs can coexist
- FR-3: `jacl_eval()` parses, compiles, and executes JACL source, returning the result as a `JaclVal`
- FR-4: Value constructors (`jacl_i32`, `jacl_string`, etc.) create JACL values from C types; extractors (`jacl_as_i32`, `jacl_as_cstr`, etc.) go the other direction
- FR-5: `jacl_handle_new()` / `jacl_handle_free()` create GC root handles that prevent collection of values held by C code
- FR-6: `jacl_register_fn()` registers a C function callable from JACL as a named proc with arity checking
- FR-7: `jacl_call()` invokes a JACL closure or native function from C, supporting re-entrant calls
- FR-8: `jacl_struct_new()` / `jacl_struct_get()` / `jacl_struct_set()` allow C code to create and manipulate JACL structs
- FR-9: `jacl_trampoline_new()` uses libffi closures to produce a C function pointer that, when called, invokes a JACL closure with automatic argument marshaling
- FR-10: Trampoline API is conditionally compiled (`JACL_HAS_LIBFFI`) and unavailable on WASM
- FR-11: JACL compiles to WebAssembly via Emscripten with embedding API functional (minus trampolines)
- FR-12: Native functions use a `JACL_TAG_NATIVE_FN` tag (or equivalent) and are dispatched by `OP_CALL` without a bytecode call frame

## Non-Goals

- No dynamic FFI from JACL scripts (no `dlopen`/`dlsym`/`ffi-call` builtins — that's a future milestone)
- No automatic C header parsing or bindgen
- No Windows support in this milestone
- No async/concurrent native functions (native fns run synchronously on the calling worker)
- No JIT or inline assembly — all marshaling is through libffi or manual C code
- No automatic struct layout matching between C `struct` definitions and JACL `defstruct` (C code uses the field access API, not raw memory pointers)
- No sandboxing of native functions (they have full access to the process)

## Technical Considerations

- **Value representation:** Inline types (i32, f32, u32, bool, nil) are trivially marshaled — the `JaclVal` encoding is already the API. Heap types (strings, i64, f64, closures, structs) require VM access for allocation.
- **Native function dispatch:** Extend `OP_CALL` to check for native function tag before assuming closure. Native calls skip the bytecode call frame — push args, call C function directly, push result. This avoids bytecode overhead for native calls.
- **GC handles:** A simple `JaclVal` array on the VM struct, scanned as additional roots during mark phase. Handles are indices into this array. Free handles go on a free list for reuse. No per-handle heap allocation.
- **Trampoline implementation:** Each trampoline allocates a `ffi_closure` via `ffi_closure_alloc`, prepares a `ffi_cif` for the C signature, and uses `ffi_prep_closure_loc` to bind a generic dispatch function. The dispatch function receives the JACL closure (via userdata), marshals `ffi_arg` values to `JaclVal`, calls `jacl_call`, and marshals the return value back. The closure is pinned via a GC handle.
- **Emscripten:** libffi closure trampolines are fundamentally impossible in pure WASM (can't generate executable code at runtime). The basic embedding API works fine. Threading primitives in `platform.h` need `#ifdef __EMSCRIPTEN__` stubs for single-threaded mode. The NxM scheduler degrades to a single-worker event loop.
- **String lifetime:** `jacl_as_cstr` returns a pointer into JACL's string storage. This pointer is invalidated by GC compaction (JACL doesn't compact, so this is safe today — but document the contract). For safety, the caller should copy the string or hold a handle.
- **Struct write barriers:** `jacl_struct_set` must trigger the GC write barrier when mutating a heap-typed field on an old-generation struct, to maintain the generational GC invariant.
- **Unity build:** The embedding API source (`ffi.c` or similar) will be `#include`d into `jacl.c` like all other source files. `jacl.h` is the only file users include directly.
- **libffi dependency:** Optional, detected at build time. On macOS, available via Homebrew (`brew install libffi`). On Linux, available via package manager (`apt install libffi-dev`). Not available on Emscripten.

## Success Metrics

- A C program can embed JACL, register native functions, eval scripts, and exchange values with zero memory leaks
- Closure trampolines work correctly as `qsort`-style comparator callbacks on macOS and Linux
- All existing M0–M13 tests pass unchanged
- JACL compiles and runs basic eval tests under Emscripten/Node.js
- API is small: ≤30 public functions in `jacl.h`

## Resolved Questions

- **Thread safety of `JaclVM*`:** Single-threaded access per VM. The NxM scheduler owns threading internally — external callers must not call the embedding API from multiple OS threads on the same VM. Document this contract.
- **Trampoline lifetime and VM destruction:** `jacl_vm_free` silently frees all outstanding trampolines. Calling a trampoline's function pointer after its VM is freed is undefined behavior.
- **Max handles:** Default cap of 1024 simultaneous GC handles, configurable via `JaclConfig`. Exceeding the limit returns a null/invalid handle.
- **Error recovery in native functions:** No `setjmp`/`longjmp` guard. Native code is trusted — if it crashes, the process crashes.
