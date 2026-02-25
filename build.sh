#!/bin/bash

CC="${CC:-gcc}"
CFLAGS="-Wall -Wextra -std=c99 -g"
PASS=0
FAIL=0
FAILED_TESTS=""

LIB_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --lib) LIB_ONLY=1 ;;
  esac
done

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

BUILD_DIR=$(mktemp -d)
trap "rm -rf '$BUILD_DIR'" EXIT

# Define tests: src|name|flags
TESTS=(
    "lib/arena/test_arena.c|arena|"
    "lib/segment_array/test_segment_array.c|segment_array|"
    "lib/rc/test_rc.c|rc|"
    "lib/hamt/test_hamt.c|hamt|"
    "lib/hamt/test_hamt_types.c|hamt_types|"
    "lib/hamt/test_hamt_transient.c|hamt_transient|"
    "lib/hamt/test_hamt_hash.c|hamt_hash|"
    "lib/hamt/test_hamt_ownership.c|hamt_ownership|-Wall -Wextra -std=c99 -g -DNDEBUG -lpthread"
    "lib/hamt/test_hamt_concurrent.c|hamt_concurrent|-Wall -Wextra -std=c99 -g -lpthread"
    "lib/chase_lev/test_chase_lev.c|chase_lev|"
    "lib/chase_lev/test_chase_lev_stress.c|chase_lev_stress|-Wall -Wextra -std=c99 -g -O2 -lpthread"
    "lib/rrb_vec/test_rrb_vec.c|rrb_vec|"
    "lib/rrb_vec/test_rrb_vec_ownership.c|rrb_vec_ownership|-Wall -Wextra -std=c99 -g -DNDEBUG -lpthread"
    "lib/rrb_vec/test_rrb_vec_concurrent.c|rrb_vec_concurrent|-Wall -Wextra -std=c99 -g -lpthread"
    "lib/rrb_vec/test_rrb_vec_transient_functional.c|rrb_vec_transient_functional|"
    "lib/rrb_vec/test_rrb_vec_transient_functional_ownership.c|rrb_vec_transient_functional_ownership|-Wall -Wextra -std=c99 -g -DNDEBUG -lpthread"
    "lib/rrb_vec/test_rrb_vec_hash.c|rrb_vec_hash|"
    "lib/sum_tree/test_sum_tree.c|sum_tree|"
    "lib/sum_tree/test_utf8.c|utf8|"
    "lib/sum_tree/test_rope.c|rope|"
    "lib/sum_tree/test_rdoc.c|rdoc|"
    "lib/regex/test_nfa.c|nfa|"
    "lib/regex/test_nfa_rope.c|nfa_rope|"
    "lib/regex/test_nfa_rdoc.c|nfa_rdoc|"
    "lib/regex/test_nfa_cross.c|nfa_cross|"
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
    "test/test_runtime.c|runtime|-Wall -Wextra -std=c99 -g -lpthread"
    "test/test_jacl_harness.c|jacl_harness|"
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
    $CC $flags "$src" -o "$BUILD_DIR/$name" 2>"$BUILD_DIR/${name}.err" &
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
