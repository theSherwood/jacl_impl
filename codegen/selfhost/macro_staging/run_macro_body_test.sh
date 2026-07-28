#!/usr/bin/env bash
# Test svm_codegen_macro_body: a macro body compiles to a single __jacl_macro(sp,params)
# whose syntax-quote lowers to vec construction and whose ~unquote holes splice the
# params. (Codegen half of Phase 3; the runtime wrapper + link + onramp run is the next
# slice.) Needs gcc; skips cleanly if absent.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
CODEGEN="$(cd "$DIR/../.." && pwd)"
B="$DIR/build"; mkdir -p "$B"
CC="${CC:-gcc}"
command -v "$CC" >/dev/null || { echo "note: $CC not found — skipping macro-body test."; exit 0; }
"$CC" -std=gnu11 -O1 -w -D_GNU_SOURCE -I "$CODEGEN" \
  "$DIR/macro_body_test.c" "$CODEGEN/codegen.c" "$CODEGEN/irbuilder.c" \
  -lpthread -lm -o "$B/macro_body_test"
"$B/macro_body_test"
