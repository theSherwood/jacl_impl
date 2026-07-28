#!/usr/bin/env bash
# End-to-end: compile a runtime `syntax-quote` program with the SVM codegen and RUN it on
# SVM (jacl_svm --interp), asserting it builds the expected plain-data syntax vec
# (the syn_rt schema). This proves the codegen `syntax-quote` lowering + jaclrt vec
# construction execute correctly on the real engine — the runtime half of Phase 3.
# Heavy (builds the Rust harness + translates the runtime). Needs cargo+gcc+clang; skips
# cleanly if any is absent. Use --update to regenerate golden.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
for t in cargo gcc clang; do command -v "$t" >/dev/null || { echo "note: $t not found — skipping SVM e2e."; exit 0; }; done
HARNESS="$ROOT/runtime/harness"
BIN="$HARNESS/target/release/jacl_svm"
( cd "$HARNESS" && cargo build --release --bin jacl_svm >/dev/null 2>&1 )
mode="${1:-check}"; fail=0
for prog in "$DIR"/corpus_svm/*.jacl; do
  n="$(basename "$prog" .jacl)"
  got="$("$BIN" run "$prog" --interp 2>/dev/null | tail -1)"
  if [ "$mode" = "--update" ]; then printf '%s\n' "$got" > "$DIR/golden_svm/$n.out"; echo "  updated $n"; continue; fi
  want="$(cat "$DIR/golden_svm/$n.out" 2>/dev/null || true)"
  if [ "$got" = "$want" ]; then echo "  PASS  $n  ->  $got"; else echo "  FAIL  $n" >&2; echo "    got:  $got" >&2; echo "    want: $want" >&2; fail=1; fi
done
[ "$mode" = "--update" ] && { echo "golden regenerated."; exit 0; }
[ $fail = 0 ] && echo "SVM e2e: syntax-quote lowering builds the expected plain-data on the real engine." || exit 1
