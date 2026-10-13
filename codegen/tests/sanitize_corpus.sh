#!/usr/bin/env bash
# Compile JACL programs with the frontend built under AddressSanitizer + UBSan; fail on any report.
#
# The frontend and codegen also run as TEMEN guests (the compiler card, jacl_emit.wasm), where a
# memory bug corrupts quietly instead of crashing: the guest heap reuses freed memory (temen#1603)
# and nothing checks stack bounds. Natively, under the sanitizers, the same bug stops the compile
# with a report. So this builds the emit-only frontend those guests run and compiles each program.
# A compile error is not a failure (the corpus has `# expect-error` programs); a sanitizer report,
# a crash, or a hang is.
#
#   codegen/tests/sanitize_corpus.sh [prog.jacl ...]     (default: test/jacl/*.jacl)
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"           # codegen/tests
ROOT="$(cd "$DIR/../.." && pwd)"
CC="${CC:-gcc}"

if [ "$#" -eq 0 ]; then set -- "$ROOT"/test/jacl/*.jacl; fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# -fno-sanitize-recover makes a UBSan finding fatal, like an ASan one. The frontend leaks by
# design (its arenas outlive a one-shot compile), so leak detection stays off.
"$CC" -std=c99 -g -O1 -w -fsanitize=address,undefined -fno-sanitize-recover=all \
  -fno-omit-frame-pointer -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -DJACL_EMIT_ONLY \
  -I "$ROOT/codegen" "$DIR/emit_jacl.c" "$ROOT/codegen/codegen.c" "$ROOT/codegen/irbuilder.c" \
  -o "$WORK/emit_san" -lm -lpthread
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 WORK

# emit_jacl exits 0 on success, 1 on a compile error, 2 on a usage or read error. Anything else
# (a sanitizer abort, a signal, `timeout`'s 124), or a report on stderr, fails the program.
check() {
  local err; err="$(mktemp "$WORK/err.XXXXXX")"
  local st=0
  timeout 60 "$WORK/emit_san" --file "$1" > /dev/null 2> "$err" || st=$?
  if [ "$st" -gt 1 ] || grep -qE 'ERROR: AddressSanitizer|:[0-9]+:[0-9]+: runtime error:' "$err"; then
    { echo "FAIL $1 (exit $st)"; sed 's/^/  /' "$err"; } > "$err.fail"
  fi
}
export -f check
printf '%s\0' "$@" | xargs -0 -n 1 -P "$(nproc)" bash -c 'check "$1"' _

if compgen -G "$WORK/*.fail" > /dev/null; then
  cat "$WORK"/*.fail
  echo "sanitize_corpus: $(ls "$WORK"/*.fail | wc -l) of $# programs failed under ASan + UBSan." >&2
  exit 1
fi
echo "sanitize_corpus: $# programs, no ASan or UBSan reports."
