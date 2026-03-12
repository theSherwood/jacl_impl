/**
 * Basic end-to-end test for JACL WASM module.
 * Runs in Node.js: node test/test_wasm.mjs
 *
 * Tests: create VM, eval expression, extract result, free VM.
 * JaclVal is uint64_t — passed as BigInt with WASM_BIGINT.
 */

import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const wasmDir = join(__dirname, '..', 'build_wasm');

// Dynamic import of the Emscripten-generated module
const createJACL = (await import(join(wasmDir, 'jacl.js'))).default;

let passed = 0;
let failed = 0;

function assert(cond, msg) {
  if (cond) {
    console.log(`  PASS: ${msg}`);
    passed++;
  } else {
    console.log(`  FAIL: ${msg}`);
    failed++;
  }
}

async function run() {
  console.log('=== JACL WASM Test ===\n');

  // Initialize the WASM module
  const jacl = await createJACL();

  // Access exported functions directly (BigInt-aware).
  // With WASM_BIGINT, i64 params/returns are BigInt.
  const _vm_new = jacl._jacl_vm_new;
  const _vm_free = jacl._jacl_vm_free;
  const _eval = jacl._jacl_eval;
  const _is_error = jacl._jacl_is_error_val;
  const _as_i32 = jacl._jacl_as_i32_val;
  const _has_trampolines = jacl._jacl_has_trampolines;
  const _nil_val = jacl._jacl_nil_val;
  const _bool_val = jacl._jacl_bool_val;
  const _i32_val = jacl._jacl_i32_val;
  const _typeof_val = jacl._jacl_typeof_val;

  // Helper: allocate a C string from JS, call fn, free
  function withCString(str, fn) {
    const len = jacl.lengthBytesUTF8(str) + 1;
    const ptr = jacl._malloc(len);
    jacl.stringToUTF8(str, ptr, len);
    const result = fn(ptr);
    jacl._free(ptr);
    return result;
  }

  // Test 1: Create VM
  console.log('Test 1: VM lifecycle');
  const vm = _vm_new();
  assert(vm !== 0, 'jacl_vm_new returns non-null');

  // Test 2: Eval [+ 1 2] => 3
  console.log('Test 2: Eval [+ 1 2]');
  const result = withCString('[+ 1 2]', (s) => _eval(vm, s));
  assert(!_is_error(result), 'result is not an error');
  const val = _as_i32(result);
  assert(val === 3, `[+ 1 2] == 3 (got ${val})`);

  // Test 3: Eval with syntax error
  console.log('Test 3: Syntax error');
  const err_result = withCString('[+ 1', (s) => _eval(vm, s));
  assert(_is_error(err_result), 'incomplete expression returns error');

  // Test 4: Trampoline API stubs return false/NULL in WASM
  console.log('Test 4: Trampolines unavailable');
  assert(!_has_trampolines(), 'jacl_has_trampolines returns false in WASM');

  // Test 5: Value constructors
  console.log('Test 5: Value constructors');
  const nil = _nil_val();
  assert(_typeof_val(nil) === 2, 'nil has type JACL_TYPE_NIL (2)');
  const t = _bool_val(1);
  assert(_typeof_val(t) === 1, 'true has type JACL_TYPE_BOOL (1)');
  const n = _i32_val(42);
  assert(_as_i32(n) === 42, 'i32_val(42) round-trips to 42');

  // Test 6: Multiple evals share global state
  console.log('Test 6: Globals persist across evals');
  withCString('def x 10', (s) => _eval(vm, s));
  const x_result = withCString('[+ $x 5]', (s) => _eval(vm, s));
  assert(!_is_error(x_result), 'second eval succeeds');
  assert(_as_i32(x_result) === 15, 'global x persists: [+ $x 5] == 15');

  // Test 7: Free VM (should not crash)
  console.log('Test 7: VM free');
  _vm_free(vm);
  assert(true, 'jacl_vm_free completes without crash');

  // Test 8: Multiple independent VMs
  console.log('Test 8: Independent VMs');
  const vm1 = _vm_new();
  const vm2 = _vm_new();
  withCString('def a 100', (s) => _eval(vm1, s));
  withCString('def a 200', (s) => _eval(vm2, s));
  const r1 = _as_i32(withCString('$a', (s) => _eval(vm1, s)));
  const r2 = _as_i32(withCString('$a', (s) => _eval(vm2, s)));
  assert(r1 === 100 && r2 === 200, `VMs independent: vm1.a=${r1}, vm2.a=${r2}`);
  _vm_free(vm1);
  _vm_free(vm2);

  // Summary
  console.log(`\n=== Results: ${passed} passed, ${failed} failed ===`);
  process.exit(failed > 0 ? 1 : 0);
}

run().catch(err => {
  console.error('Test runner error:', err);
  process.exit(1);
});
