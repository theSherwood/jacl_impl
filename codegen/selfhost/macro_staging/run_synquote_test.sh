#!/usr/bin/env bash
# Structural test for the codegen `syntax-quote` lowering (docs/SVM_MACRO_STAGING_PLAN.md).
# A `syntax-quote <template>` must lower to construction of a plain-data syntax vec
# (jacl_vec_empty/push, the syn_rt schema). We assert the emit succeeds and that the
# number of `jacl_vec_empty` calls equals the number of syntax nodes in the template
# (one vec per node) — a structural fingerprint of a correct quasiquote lowering.
# (Full runtime equivalence lands with the staging pipeline; this is the codegen unit.)
# Needs gcc; skips cleanly if absent.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
CODEGEN="$(cd "$DIR/../.." && pwd)"
B="$DIR/build"; mkdir -p "$B"
CC="${CC:-gcc}"
command -v "$CC" >/dev/null || { echo "note: $CC not found — skipping syntax-quote test."; exit 0; }

EMIT="$B/emit_jacl_native"
if [ ! -x "$EMIT" ] || [ "$CODEGEN/codegen.c" -nt "$EMIT" ]; then
  "$CC" -std=gnu11 -O1 -w -D_GNU_SOURCE -I "$CODEGEN" \
    "$CODEGEN/tests/emit_jacl.c" "$CODEGEN/codegen.c" "$CODEGEN/irbuilder.c" \
    -lpthread -lm -o "$EMIT"
fi

# program : expected syntax-node count (== expected jacl_vec_empty calls)
cases=(
  "syntax-quote 5|1"
  "syntax-quote [+ 1 2]|4"
  "syntax-quote [+ 1 [* 2 3]]|7"
  "syntax-quote [f x y]|4"
)
fail=0
for c in "${cases[@]}"; do
  prog="${c%|*}"; want="${c#*|}"
  printf '%s\n' "$prog" > "$B/sq.jacl"
  out="$("$EMIT" --file "$B/sq.jacl" 2>&1)"
  if ! printf '%s' "$out" | grep -qE '^func \('; then
    echo "  FAIL  '$prog' — did not lower:"; printf '%s\n' "$out" | head -2; fail=1; continue
  fi
  got="$(printf '%s' "$out" | grep -cE 'jacl_vec_empty')"
  if [ "$got" = "$want" ]; then echo "  PASS  '$prog'  ($got vecs)"; else echo "  FAIL  '$prog'  vecs=$got want=$want" >&2; fail=1; fi
done
[ $fail = 0 ] && echo "syntax-quote lowering: all shapes build the expected plain-data vec structure." || exit 1
