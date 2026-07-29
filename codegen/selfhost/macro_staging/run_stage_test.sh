#!/usr/bin/env bash
# On-SVM staged-macro final-link e2e (Phase 3, docs/SVM_MACRO_STAGING_PLAN.md).
# For each `defmacro` case, codegen a complete staged-macro program (entry reads the
# argument syntax value on stdin, runs the body on SVM, writes the result), link it
# against the staging-extended runtime, and RUN it on the real engine via `stage_macro`:
#   gen_wire ARG  (AST -> wire) | stage_macro MACRO.jacl (on SVM) | wire_to_ast (wire -> AST)
# and assert the expansion. Unlike run_wrapper_test.sh (which uses a hand-written
# __jacl_macro stand-in run natively), this exercises the *codegen'd* body executing on
# SVM plus the real read/encode/write glue — the full final link. Heavy: builds the Rust
# harness + translates the runtime. Needs cargo+gcc+clang; skips cleanly if any is absent.
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

# macro source | arg | expected expansion.  `~x` splices the argument syntax value;
# `$x` reads the parameter (the whole argument) as a value.
cases=(
  'defmacro k {x} { syntax-quote 42 }|21|42'
  'defmacro s {x} { syntax-quote [+ 1 2] }|21|[+ 1 2]'
  'defmacro t {x} { syntax-quote ~x }|21|21'
  'defmacro twice {x} { syntax-quote [+ ~x ~x] }|21|[+ 21 21]'
  'defmacro dq {x} { syntax-quote [+ ~x 1] }|7|[+ 7 1]'
  'defmacro idd {x} { $x }|21|21'
  'defmacro twice {x} { syntax-quote [+ ~x ~x] }|[* 3 4]|[+ [* 3 4] [* 3 4]]'
)
fail=0
for c in "${cases[@]}"; do
  src="${c%%|*}"; rest="${c#*|}"; arg="${rest%|*}"; want="${rest#*|}"
  printf '%s\n' "$src" > "$B/_stage_case.jacl"
  got="$("$B/gen_wire" "$arg" | "$SM" "$B/_stage_case.jacl" 2>/dev/null | "$B/wire_to_ast")"
  if [ "$got" = "$want" ]; then echo "  PASS  ($arg)  ->  $got"; else echo "  FAIL  ($arg)  ->  $got  (want $want)" >&2; fail=1; fi
done
[ $fail = 0 ] && echo "staged macro on SVM: codegen'd body + glue evaluate the expansion on the real engine." || exit 1
