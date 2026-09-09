#!/usr/bin/env bash
#
# Macro-staging differential gate (Phase 0 of docs/TEMEN_MACRO_STAGING_PLAN.md).
#
# For each user-`defmacro` program in corpus/, emit its TEMEN IR through the JACL
# frontend and compare against the checked-in golden/. The golden is the oracle: IR
# produced with macros expanded on the **legacy** bytecode VM (src/vm.c). As the
# macro evaluator is re-hosted on TEMEN (behind JACL_STAGE_ON_TEMEN), this must stay
# byte-identical — the whole point of the migration is that *nothing observable
# changes* except which engine runs the macro.
#
#   run_diff.sh            # build oracle, diff every corpus program vs golden
#   run_diff.sh --update   # regenerate golden/ from the current frontend
#   run_diff.sh --temen      # diff the TEMEN-staged path (JACL_STAGE_ON_TEMEN) vs golden — Phase 5
#
# The default/`--update` modes need gcc only. `--temen` additionally needs cargo+clang: it links
# the frontend against the TEMEN staging bridge (stage_bridge.c + the runtime/harness staticlib),
# runs expansion with JACL_STAGE_ON_TEMEN=1 so the macro evaluator runs on the TEMEN engine, and
# asserts the emitted IR still matches the byte-for-byte oracle — the Phase 5 acceptance gate.
# Skips cleanly (exit 0) if a required tool is absent.
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

mode="${1:-check}"

# Pick the frontend binary + the env that activates staging. Default modes use the native
# oracle build; --temen uses a build that links the TEMEN bridge and sets JACL_STAGE_ON_TEMEN.
STAGE_ENV=""
if [ "$mode" = "--temen" ]; then
  for t in cargo clang; do command -v "$t" >/dev/null || { echo "note: $t not found — skipping TEMEN-staged diff."; exit 0; }; done
  echo "=== building staging runtime staticlib (cargo) ==="
  ( cd "$ROOT/runtime/harness" && cargo build --release >/dev/null 2>&1 )
  LIB="$ROOT/runtime/harness/target/release"
  EMIT="$BUILD/emit_jacl_temen"
  echo "=== building TEMEN-staged frontend ==="
  "$CC" -DJACL_STAGE_ON_TEMEN_BUILD -std=gnu11 -O1 -w -D_GNU_SOURCE -I "$CODEGEN" \
    "$CODEGEN/tests/emit_jacl.c" "$CODEGEN/codegen.c" "$CODEGEN/irbuilder.c" \
    "$DIR/stage_bridge.c" \
    -L "$LIB" -ljacl_runtime_harness -lgcc_s -lutil -lrt -lpthread -lm -ldl -lc \
    -o "$EMIT"
  STAGE_ENV="JACL_STAGE_ON_TEMEN=1"
else
  EMIT="$BUILD/emit_jacl_native"
  if [ ! -x "$EMIT" ] || [ "$CODEGEN/tests/emit_jacl.c" -nt "$EMIT" ]; then
    echo "=== building native frontend oracle ==="
    "$CC" -std=gnu11 -O1 -w -D_GNU_SOURCE -I "$CODEGEN" \
      "$CODEGEN/tests/emit_jacl.c" "$CODEGEN/codegen.c" "$CODEGEN/irbuilder.c" \
      -lpthread -lm -o "$EMIT"
  fi
fi

# Default/--update fold stderr into the compared output (error-case goldens include it); --temen
# drops stderr (clang emits translation warnings while building the staging runtime).
redir_err() { if [ "$mode" = "--temen" ]; then "$@" 2>/dev/null; else "$@" 2>&1; fi; }

fail=0
for f in "$CORPUS"/*.jacl; do
  n="$(basename "$f" .jacl)"
  # --text: the goldens are human-readable temen-text; the driver now defaults to the binary
  # temen-encode container (guest-JIT staging item 7).
  got="$(redir_err env $STAGE_ENV "$EMIT" --text --file "$f")"
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
if [ "$fail" = 0 ]; then
  if [ "$mode" = "--temen" ]; then
    echo "macro-staging diff (--temen): the TEMEN-staged expander matches the oracle byte-for-byte."
  else
    echo "macro-staging diff: all corpus programs match the oracle."
  fi
else
  exit 1
fi
