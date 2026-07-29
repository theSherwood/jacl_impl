#!/usr/bin/env bash
# On-SVM staged-macro final-link e2e (Phase 3+, docs/SVM_MACRO_STAGING_PLAN.md).
# For each `defmacro` case, codegen a complete staged-macro program (entry reads the N
# argument syntax values on stdin, runs the body on SVM, writes the result), link it
# against the staging-extended runtime, and RUN it on the real engine via `stage_macro`:
#   gen_wire ARGS…  (ASTs -> wire) | stage_macro MACRO.jacl (on SVM) | wire_to_ast (-> AST)
# and assert the expansion. Unlike run_wrapper_test.sh (a hand-written __jacl_macro run
# natively), this exercises the *codegen'd* body executing on SVM plus the real
# read/encode/write glue — the full final link, including multi-arity macros. Heavy:
# builds the Rust harness + translates the runtime. Needs cargo+gcc+clang; skips cleanly
# if any is absent.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
CG="$(cd "$DIR/../.." && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
B="$DIR/build"; mkdir -p "$B"
CC="${CC:-gcc}"
for t in cargo gcc clang; do command -v "$t" >/dev/null || { echo "note: $t not found — skipping stage e2e."; exit 0; }; done

"$CC" -std=gnu11 -O1 -w -D_GNU_SOURCE -I "$CG" "$DIR/gen_wire.c"    -lpthread -lm -o "$B/gen_wire"
"$CC" -std=gnu11 -O1 -w -D_GNU_SOURCE -I "$CG" "$DIR/wire_to_ast.c" -lpthread -lm -o "$B/wire_to_ast"
( cd "$ROOT/runtime/harness" && cargo build --release --bin stage_macro >/dev/null 2>&1 )
SM="$ROOT/runtime/harness/target/release/stage_macro"

fail=0
# check SRC WANT ARG…  — write the defmacro SRC, feed each ARG (a JACL snippet) as a
# syntax value on the wire, run on SVM, assert the pretty-printed expansion equals WANT.
check() {
  local src="$1" want="$2"; shift 2
  printf '%s\n' "$src" > "$B/_stage_case.jacl"
  local got
  got="$("$B/gen_wire" "$@" | "$SM" "$B/_stage_case.jacl" 2>/dev/null | "$B/wire_to_ast")"
  if [ "$got" = "$want" ]; then echo "  PASS  ($*)  ->  $got"
  else echo "  FAIL  ($*)  ->  $got  (want $want)" >&2; fail=1; fi
}

# arity-1: `~x` splices the argument, `$x` reads the whole argument as a value.
check 'defmacro k {x} { syntax-quote 42 }'              '42'          '21'
check 'defmacro s {x} { syntax-quote [+ 1 2] }'         '[+ 1 2]'     '21'
check 'defmacro t {x} { syntax-quote ~x }'              '21'          '21'
check 'defmacro twice {x} { syntax-quote [+ ~x ~x] }'   '[+ 21 21]'   '21'
check 'defmacro dq {x} { syntax-quote [+ ~x 1] }'       '[+ 7 1]'     '7'
check 'defmacro idd {x} { $x }'                         '21'          '21'
check 'defmacro twice {x} { syntax-quote [+ ~x ~x] }'   '[+ [* 3 4] [* 3 4]]'  '[* 3 4]'
# arity-2 (the corpus `unless`): two syntax values on the wire, bound in order.
check 'defmacro unless {cond body} { syntax-quote [if ~cond {} ~body] }' \
      '[if [== 1 2] {} { [set hit 5] }]'  '[== 1 2]'  '{ set hit 5 }'

[ $fail = 0 ] && echo "staged macro on SVM: codegen'd body + glue evaluate the expansion on the real engine." || exit 1
