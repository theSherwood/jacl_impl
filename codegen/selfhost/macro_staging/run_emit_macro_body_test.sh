#!/usr/bin/env bash
# Smoke test for emit_macro_body: a `defmacro` compiles to a module with a single
# __jacl_macro function that builds a plain-data syntax vec (jacl_vec_empty/push), and the
# output parses as svm-text. This tool feeds the codegen'd __jacl_macro into the staged-
# macro link (see docs/SVM_MACRO_STAGING_PLAN.md "final link"). Needs gcc; skips if absent.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
CG="$(cd "$DIR/../.." && pwd)"
B="$DIR/build"; mkdir -p "$B"
CC="${CC:-gcc}"
command -v "$CC" >/dev/null || { echo "note: $CC not found — skipping emit_macro_body test."; exit 0; }
"$CC" -std=gnu11 -O1 -w -D_GNU_SOURCE -I "$CG" \
  "$DIR/emit_macro_body.c" "$CG/codegen.c" "$CG/irbuilder.c" -lpthread -lm -o "$B/emit_macro_body"
printf 'defmacro twice {x} { syntax-quote [+ ~x ~x] }\n' > "$B/twice.jacl"
out="$("$B/emit_macro_body" "$B/twice.jacl")"
if printf '%s' "$out" | grep -qE '^func \(' && printf '%s' "$out" | grep -q 'jacl_vec_empty'; then
  echo "  PASS  twice -> __jacl_macro svm-text (builds plain-data vec)"
else
  echo "  FAIL  emit_macro_body output:" >&2; printf '%s\n' "$out" | head -5 >&2; exit 1
fi
