#!/usr/bin/env bash
# Build + run the syn_wire round-trip test. Needs gcc; skips cleanly if absent.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
CODEGEN="$(cd "$DIR/../.." && pwd)"
BUILD="$DIR/build"; mkdir -p "$BUILD"
CC="${CC:-gcc}"
command -v "$CC" >/dev/null || { echo "note: $CC not found — skipping syn_wire test."; exit 0; }
"$CC" -std=gnu11 -O1 -w -D_GNU_SOURCE -I "$CODEGEN" "$DIR/syn_wire_test.c" -lpthread -lm -o "$BUILD/syn_wire_test"
"$BUILD/syn_wire_test"
