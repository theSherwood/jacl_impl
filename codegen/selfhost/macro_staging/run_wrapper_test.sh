#!/usr/bin/env bash
# Staged-macro data-flow test (arity-1). Exercises the wrapper (macro_main.c) + both
# real codecs end-to-end for a `twice {x} = [+ ~x ~x]`-shaped macro:
#   gen_wire ARG  (AST -> wire)  |  macro_main (decode -> __jacl_macro -> encode)  |
#   wire_to_ast   (wire -> AST pretty-print)
# and asserts the expansion. Only __jacl_macro is a hand-written stand-in (fake_twice.c)
# for the codegen'd body — the codegen'd version is proven by run_macro_body_test.sh and
# run_svm_e2e.sh; here we prove the marshalling that carries args in and the result out.
# Needs gcc; skips cleanly if absent.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
CG="$(cd "$DIR/../.." && pwd)"; RT="$(cd "$DIR/../../../runtime" && pwd)"
B="$DIR/build"; mkdir -p "$B"
CC="${CC:-gcc}"
command -v "$CC" >/dev/null || { echo "note: $CC not found — skipping wrapper test."; exit 0; }
"$CC" -std=gnu11 -O1 -w -D_GNU_SOURCE -I "$CG" "$DIR/gen_wire.c" -lpthread -lm -o "$B/gen_wire"
"$CC" -std=gnu11 -O1 -w -D_GNU_SOURCE -I "$CG" "$DIR/wire_to_ast.c" -lpthread -lm -o "$B/wire_to_ast"
"$CC" -std=gnu11 -O1 -w -D_GNU_SOURCE -I "$RT" -I "$DIR" \
  "$DIR/macro_main.c" "$DIR/fake_twice.c" "$DIR/rt_native_shim.c" \
  -x c "$RT/jaclrt.c" -x c "$DIR/syn_rt.c" -lm -o "$B/macro_main"

# arg | expected expansion (twice x = [+ x x])
cases=("21|[+ 21 21]" "5|[+ 5 5]" "[* 3 4]|[+ [* 3 4] [* 3 4]]" "foo|[+ [foo] [foo]]")
fail=0
for c in "${cases[@]}"; do
  arg="${c%|*}"; want="${c#*|}"
  "$B/gen_wire" "$arg" | "$B/macro_main" > "$B/res.wire"
  got="$("$B/wire_to_ast" < "$B/res.wire")"
  if [ "$got" = "$want" ]; then echo "  PASS  twice $arg  ->  $got"; else echo "  FAIL  twice $arg  ->  $got  (want $want)" >&2; fail=1; fi
done
[ $fail = 0 ] && echo "staged-macro data flow: arg -> decode -> macro -> encode -> expansion, all correct." || exit 1
