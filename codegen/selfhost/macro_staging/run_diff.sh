#!/usr/bin/env bash
#
# Macro-staging differential gate (Phase 0 of docs/SVM_MACRO_STAGING_PLAN.md).
#
# For each user-`defmacro` program in corpus/, emit its SVM IR through the JACL
# frontend and compare against the checked-in golden/. The golden is the oracle: IR
# produced with macros expanded on the **legacy** bytecode VM (src/vm.c). As the
# macro evaluator is re-hosted on SVM (behind JACL_STAGE_ON_SVM), this must stay
# byte-identical — the whole point of the migration is that *nothing observable
# changes* except which engine runs the macro.
#
#   run_diff.sh            # build oracle, diff every corpus program vs golden
#   run_diff.sh --update   # regenerate golden/ from the current frontend
#   JACL_STAGE_ON_SVM=1 run_diff.sh   # (future) diff the SVM-staged path vs golden
#
# Needs gcc. Skips cleanly (exit 0) if absent.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"          # codegen/selfhost/macro_staging
CODEGEN="$(cd "$DIR/../.." && pwd)"           # codegen
ROOT="$(cd "$CODEGEN/.." && pwd)"             # repo root
CORPUS="$DIR/corpus"
GOLDEN="$DIR/golden"
BUILD="$DIR/build"
mkdir -p "$GOLDEN" "$BUILD"

CC="${CC:-gcc}"
if ! command -v "$CC" >/dev/null; then
  echo "note: $CC not found — skipping macro-staging diff."
  exit 0
fi

EMIT="$BUILD/emit_jacl_native"
if [ ! -x "$EMIT" ] || [ "$CODEGEN/tests/emit_jacl.c" -nt "$EMIT" ]; then
  echo "=== building native frontend oracle ==="
  "$CC" -std=gnu11 -O1 -w -D_GNU_SOURCE -I "$CODEGEN" \
    "$CODEGEN/tests/emit_jacl.c" "$CODEGEN/codegen.c" "$CODEGEN/irbuilder.c" \
    -lpthread -lm -o "$EMIT"
fi

mode="${1:-check}"
fail=0
for f in "$CORPUS"/*.jacl; do
  n="$(basename "$f" .jacl)"
  got="$("$EMIT" --file "$f" 2>&1)"
  if [ "$mode" = "--update" ]; then
    printf '%s\n' "$got" > "$GOLDEN/$n.ir"
    echo "  updated golden/$n.ir"
    continue
  fi
  if [ ! -f "$GOLDEN/$n.ir" ]; then
    echo "  MISSING golden/$n.ir (run --update)" >&2; fail=1; continue
  fi
  if diff -q <(printf '%s\n' "$got") "$GOLDEN/$n.ir" >/dev/null; then
    echo "  PASS  $n"
  else
    echo "  FAIL  $n (IR differs from golden)" >&2
    diff <(printf '%s\n' "$got") "$GOLDEN/$n.ir" | head -20 >&2
    fail=1
  fi
done
[ "$mode" = "--update" ] && { echo "golden regenerated."; exit 0; }
[ "$fail" = 0 ] && echo "macro-staging diff: all corpus programs match the oracle." || exit 1
