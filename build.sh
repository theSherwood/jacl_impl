#!/bin/bash

CC="${CC:-gcc}"
CFLAGS="-Wall -Wextra -std=c99 -g"
PASS=0
FAIL=0
FAILED_TESTS=""

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

BUILD_DIR=$(mktemp -d)
trap "rm -rf '$BUILD_DIR'" EXIT

# Define tests: src|name|flags
TESTS=(
    "arena/test_arena.c|arena|"
    "segment_array/test_segment_array.c|segment_array|"
    "rc/test_rc.c|rc|"
    "hamt/test_hamt.c|hamt|"
    "hamt/test_hamt_types.c|hamt_types|"
    "hamt/test_hamt_transient.c|hamt_transient|"
    "hamt/test_hamt_ownership.c|hamt_ownership|-Wall -Wextra -std=c99 -g -DNDEBUG -lpthread"
    "hamt/test_hamt_concurrent.c|hamt_concurrent|-Wall -Wextra -std=c99 -g -lpthread"
    "chase_lev/test_chase_lev.c|chase_lev|"
    "chase_lev/test_chase_lev_stress.c|chase_lev_stress|-Wall -Wextra -std=c99 -g -O2 -lpthread"
    "rrb_vec/test_rrb_vec.c|rrb_vec|"
    "rrb_vec/test_rrb_vec_ownership.c|rrb_vec_ownership|-Wall -Wextra -std=c99 -g -DNDEBUG -lpthread"
    "rrb_vec/test_rrb_vec_concurrent.c|rrb_vec_concurrent|-Wall -Wextra -std=c99 -g -lpthread"
    "rrb_vec/test_rrb_vec_transient_functional.c|rrb_vec_transient_functional|"
    "rrb_vec/test_rrb_vec_transient_functional_ownership.c|rrb_vec_transient_functional_ownership|-Wall -Wextra -std=c99 -g -DNDEBUG -lpthread"
    "sum_tree/test_sum_tree.c|sum_tree|"
    "sum_tree/test_utf8.c|utf8|"
    "sum_tree/test_rope.c|rope|"
    "sum_tree/test_rdoc.c|rdoc|"
    "regex/test_nfa.c|nfa|"
    "regex/test_nfa_rope.c|nfa_rope|"
    "regex/test_nfa_rdoc.c|nfa_rdoc|"
    "regex/test_nfa_cross.c|nfa_cross|"
    "bignum/test_bigint.c|bigint|"
    "bignum/test_bigfloat.c|bigfloat|"
    "bignum/test_rational.c|rational|"
    "lexer/test_lexer.c|lexer|"
    "value/test_value.c|value|"
    "parser/test_parser.c|parser|"
    "compiler/test_bytecode.c|bytecode|"
)

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
