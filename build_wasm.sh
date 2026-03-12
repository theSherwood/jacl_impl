#!/bin/bash
#
# Build JACL as a WebAssembly module via Emscripten.
# Produces build_wasm/jacl.js + build_wasm/jacl.wasm
#
# Usage: bash build_wasm.sh
#

set -e

EMCC="${EMCC:-emcc}"

# Verify emcc is available
if ! command -v "$EMCC" &>/dev/null; then
  echo "error: emcc not found. Install Emscripten SDK first."
  exit 1
fi

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

OUT_DIR="$DIR/build_wasm"
mkdir -p "$OUT_DIR"

# Exported embedding API functions (C names from embed.c with _val suffixes)
EXPORTED_FUNCTIONS="[
  '_jacl_vm_new',
  '_jacl_vm_new_ex',
  '_jacl_vm_free',
  '_jacl_eval',
  '_jacl_eval_file',
  '_jacl_is_error_val',
  '_jacl_error_message_str',
  '_jacl_nil_val',
  '_jacl_bool_val',
  '_jacl_i32_val',
  '_jacl_i64_val',
  '_jacl_u32_val',
  '_jacl_u64_val',
  '_jacl_f32_val',
  '_jacl_f64_val',
  '_jacl_string_val',
  '_jacl_string_cstr_val',
  '_jacl_as_i32_val',
  '_jacl_as_i64_val',
  '_jacl_as_u32_val',
  '_jacl_as_u64_val',
  '_jacl_as_f32_val',
  '_jacl_as_f64_val',
  '_jacl_as_bool_val',
  '_jacl_as_cstr_val',
  '_jacl_typeof_val',
  '_jacl_handle_new_val',
  '_jacl_handle_get_val',
  '_jacl_handle_free_val',
  '_jacl_register_fn_val',
  '_jacl_call_val',
  '_jacl_call_named_val',
  '_jacl_struct_new_val',
  '_jacl_struct_get_val',
  '_jacl_struct_set_val',
  '_jacl_struct_type_name_val',
  '_jacl_has_trampolines',
  '_jacl_trampoline_new_val',
  '_jacl_trampoline_ptr_val',
  '_jacl_trampoline_free_val',
  '_malloc',
  '_free'
]"

echo "=== Building JACL WASM module ==="

$EMCC \
  -std=c99 \
  -O2 \
  -D_DEFAULT_SOURCE \
  -D_POSIX_C_SOURCE=200809L \
  -Wno-implicit-function-declaration \
  src/jacl.c \
  -o "$OUT_DIR/jacl.js" \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="createJACL" \
  -s EXPORTED_FUNCTIONS="$EXPORTED_FUNCTIONS" \
  -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8','getValue','setValue']" \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=16777216 \
  -s STACK_SIZE=1048576 \
  -s NO_EXIT_RUNTIME=1 \
  -s WASM_BIGINT=1

echo "=== WASM build complete ==="
echo "  $OUT_DIR/jacl.js"
echo "  $OUT_DIR/jacl.wasm"
