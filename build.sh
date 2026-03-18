#!/bin/bash

CC="${CC:-gcc}"
CFLAGS="-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE"
PASS=0
FAIL=0
FAILED_TESTS=""

# Detect libffi for trampoline support
LIBFFI_FLAGS=""
if pkg-config --exists libffi 2>/dev/null; then
  LIBFFI_CFLAGS="$(pkg-config --cflags libffi 2>/dev/null)"
  LIBFFI_LIBS="$(pkg-config --libs libffi 2>/dev/null)"
  LIBFFI_FLAGS="-DJACL_HAS_LIBFFI=1 ${LIBFFI_CFLAGS} ${LIBFFI_LIBS}"
  echo "libffi: found via pkg-config"
elif [ -f "/usr/include/ffi.h" ] || [ -f "/usr/local/include/ffi.h" ] || \
     [ -f "/usr/include/x86_64-linux-gnu/ffi.h" ]; then
  LIBFFI_FLAGS="-DJACL_HAS_LIBFFI=1 -lffi"
  echo "libffi: found in standard paths"
else
  echo "libffi: not found (trampolines disabled)"
fi

LIB_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --lib) LIB_ONLY=1 ;;
  esac
done

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

BUILD_DIR=$(mktemp -d "${DIR}/.build_XXXXXX")
trap "rm -rf '$BUILD_DIR'" EXIT

# Define tests: src|name|flags
TESTS=(
    "lib/arena/test_arena.c|arena|"
    "lib/rc/test_rc.c|rc|"
    "lib/hamt/test_hamt.c|hamt|"
    "lib/hamt/test_hamt_types.c|hamt_types|"
    "lib/hamt/test_hamt_transient.c|hamt_transient|"
    "lib/hamt/test_hamt_hash.c|hamt_hash|"
    "lib/hamt/test_hamt_ownership.c|hamt_ownership|-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE -DNDEBUG -lpthread"
    "lib/hamt/test_hamt_concurrent.c|hamt_concurrent|-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE -lpthread"
    "lib/chase_lev/test_chase_lev.c|chase_lev|"
    "lib/chase_lev/test_chase_lev_stress.c|chase_lev_stress|-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE -O2 -lpthread"
    "lib/rrb_vec/test_rrb_vec.c|rrb_vec|"
    "lib/rrb_vec/test_rrb_vec_ownership.c|rrb_vec_ownership|-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE -DNDEBUG -lpthread"
    "lib/rrb_vec/test_rrb_vec_concurrent.c|rrb_vec_concurrent|-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE -lpthread"
    "lib/rrb_vec/test_rrb_vec_transient_functional.c|rrb_vec_transient_functional|"
    "lib/rrb_vec/test_rrb_vec_transient_functional_ownership.c|rrb_vec_transient_functional_ownership|-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE -DNDEBUG -lpthread"
    "lib/rrb_vec/test_rrb_vec_hash.c|rrb_vec_hash|"
    "lib/platform/test_rwlock.c|rwlock|-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE -lpthread"
    "lib/sum_tree/test_sum_tree.c|sum_tree|"
    "lib/sum_tree/test_utf8.c|utf8|"
    "lib/sum_tree/test_rope.c|rope|"
    "lib/sum_tree/test_grapheme_split.c|grapheme_split|"
    "lib/sum_tree/test_rope_concat.c|rope_concat|"
    "lib/regex/test_nfa.c|nfa|"
    "lib/regex/test_nfa_rope.c|nfa_rope|"
    "lib/bignum/test_bigint.c|bigint|"
    "lib/bignum/test_bigfloat.c|bigfloat|"
    "lib/bignum/test_rational.c|rational|"
    "test/test_value.c|value|"
    "test/test_lexer.c|lexer|"
    "test/test_parser.c|parser|"
    "test/test_bytecode.c|bytecode|"
    "test/test_vm.c|vm|"
    "test/test_compiler.c|compiler|"
    "test/test_integration.c|integration|"
    "test/test_m4.c|m4|"
    "test/test_string.c|string|"
    "test/test_m5.c|m5|"
    "test/test_m6.c|m6|"
    "test/test_collections.c|collections|"
    "test/test_m9.c|m9|"
    "test/test_m10.c|m10|"
    "test/test_m11.c|m11|"
    "test/test_gc.c|gc|"
    "test/test_runtime.c|runtime|-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE -lpthread"
    "test/test_m12.c|m12|-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE -lpthread"
    "test/test_m13.c|m13|-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE -lpthread"
    "test/test_jacl_harness.c|jacl_harness|-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE -lpthread"
    "test/test_embed.c|embed|${LIBFFI_FLAGS}"
    "test/test_e2e_embed_basics.c|e2e_embed_basics|${LIBFFI_FLAGS}"
    "test/test_e2e_native_fns.c|e2e_native_fns|${LIBFFI_FLAGS}"
    "test/test_e2e_gc_handles.c|e2e_gc_handles|${LIBFFI_FLAGS}"
    "test/test_e2e_struct_tramp.c|e2e_struct_tramp|${LIBFFI_FLAGS}"
    "test/test_rope_string.c|rope_string|"
    "test/test_utf8_nfd.c|utf8_nfd|"
    "test/test_string_new.c|string_new|"
    "test/test_length_builtin.c|length_builtin|"
    "test/test_index_builtin.c|index_builtin|"
    "test/test_slice_builtin.c|slice_builtin|"
    "test/test_concat_tiers.c|concat_tiers|"
    "test/test_string_eq_cmp.c|string_eq_cmp|"
    "test/test_compiler_vm_integration.c|compiler_vm_integration|"
    "test/test_heap_strings.c|heap_strings|"
    "test/test_gc_rope_tracing.c|gc_rope_tracing|"
    "test/test_intern_eviction.c|intern_eviction|"
    "test/test_concurrent_intern.c|concurrent_intern|-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE -lpthread"
    "test/test_rope_slice_grapheme.c|rope_slice_grapheme|"
    "test/test_cross_thread_string.c|cross_thread_string|-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE -lpthread"
    "test/test_destructure_vec.c|destructure_vec|"
)

# Filter tests if --lib flag is set
if [ "$LIB_ONLY" -eq 1 ]; then
  FILTERED=()
  for entry in "${TESTS[@]}"; do
    IFS='|' read -r src name extra_cflags <<< "$entry"
    case "$src" in lib/*) FILTERED+=("$entry") ;; esac
  done
  TESTS=("${FILTERED[@]}")
fi

echo "=== ds test runner ==="
echo ""

# Phase 1: compile all tests in parallel
PIDS=()
for entry in "${TESTS[@]}"; do
    IFS='|' read -r src name extra_cflags <<< "$entry"
    flags="$CFLAGS"
    if [ -n "$extra_cflags" ]; then
        flags="$extra_cflags"
    fi
    $CC $flags "$src" -o "$BUILD_DIR/$name" -lm 2>"$BUILD_DIR/${name}.err" &
    PIDS+=($!)
done

# Wait for all compilations
COMPILE_OK=1
for i in "${!TESTS[@]}"; do
    IFS='|' read -r src name extra_cflags <<< "${TESTS[$i]}"
    if ! wait "${PIDS[$i]}"; then
        printf "%-30s COMPILE FAIL\n" "$name"
        cat "$BUILD_DIR/${name}.err" >&2
        FAIL=$((FAIL + 1))
        FAILED_TESTS="$FAILED_TESTS $name"
    fi
done

# Phase 2: run all compiled tests sequentially
for entry in "${TESTS[@]}"; do
    IFS='|' read -r src name extra_cflags <<< "$entry"
    if [ ! -x "$BUILD_DIR/$name" ]; then
        continue  # compilation failed, already counted
    fi
    printf "%-30s" "$name"
    if "$BUILD_DIR/$name"; then
        echo "PASS"
        PASS=$((PASS + 1))
    else
        echo "FAIL"
        FAIL=$((FAIL + 1))
        FAILED_TESTS="$FAILED_TESTS $name"
    fi
done

# WIP modules (skipped)
echo ""
echo "--- WIP (skipped) ---"
echo "  pvec"

# Summary
echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
if [ "$FAIL" -ne 0 ]; then
    echo "FAILED:$FAILED_TESTS"
    exit 1
fi
