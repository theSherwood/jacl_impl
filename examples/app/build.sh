#!/usr/bin/env bash
# An application's build of a JACL program into its own temen program (#290): what nisaba's build
# does. JACL's parts come as link units (`jacl_units`), the application's C as a link unit from
# clang and temen's C on-ramp, and its Nim from nimony's Temen backend. temen-link links them all
# into one program, with unir's unit once.
#
#   bash examples/app/build.sh [outdir]      writes <outdir>/app.temen (default examples/app/build)
#
# Needs clang 18, cargo, and nimony on PATH or in NIMONY_BIN and NIM_BIN (vendor/temen's
# scripts/ci/provision-nimony.sh builds them). TEMEN_LINK, TEMEN_LLVM_TRANSLATE and JACL_UNITS name
# prebuilt tools; without them it builds each, the first two from vendor/temen.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
TEMEN="$ROOT/vendor/temen"
mkdir -p "${1:-$HERE/build}"
OUT="$(cd "${1:-$HERE/build}" && pwd)"
export PATH="${NIMONY_BIN:+$NIMONY_BIN:}${NIM_BIN:+$NIM_BIN:}$PATH"

if [ -z "${TEMEN_LINK:-}" ]; then
  cargo build --release -q --manifest-path "$TEMEN/Cargo.toml" -p temen-run --example temen_link
  TEMEN_LINK="$TEMEN/target/release/examples/temen_link"
fi
if [ -z "${TEMEN_LLVM_TRANSLATE:-}" ]; then
  cargo build --release -q --manifest-path "$TEMEN/crates/temen-llvm/Cargo.toml" \
    --bin temen-llvm-translate
  TEMEN_LLVM_TRANSLATE="$TEMEN/crates/temen-llvm/target/release/temen-llvm-translate"
fi
if [ -z "${JACL_UNITS:-}" ]; then
  cargo build --release -q --manifest-path "$ROOT/runtime/harness/Cargo.toml" --bin jacl_units
  JACL_UNITS="$ROOT/runtime/harness/target/release/jacl_units"
fi

# 1. JACL's units: its runtime, the program's object, and unir's C ABI.
"$JACL_UNITS" "$HERE/app.jacl" "$OUT"

# 2. The application's C, as a temen program's C library is built.
clang -O2 -fno-vectorize -fno-slp-vectorize -S -emit-llvm --target=x86_64-unknown-linux-gnu \
  "$HERE/app.c" -o "$OUT/app_c.ll"
"$TEMEN_LLVM_TRANSLATE" "$OUT/app_c.ll" -o "$OUT/app_c.temeno" --link-unit

# 3. The application's Nim, by nimony's Temen backend, which links the program with the temen-link
#    on PATH. It passes the program's modules alone, on the POSIX edge (theSherwood/temen#3182), so
#    the temen-link here adds the units and the capability edge to the program's own link, as unir's
#    e2e does (`e2e/temen/src/nim.rs`).
UNITS="$OUT/jaclrt.temeno $OUT/app.temeno $OUT/unir_cabi.temeno $OUT/app_c.temeno"
mkdir -p "$OUT/bin"
cat > "$OUT/bin/temen-link" <<EOF
#!/bin/sh
for a in "\$@"; do
  case "\$a" in -o:*/app.temen) exec "$TEMEN_LINK" "\$@" --edge:caps $UNITS ;; esac
done
exec "$TEMEN_LINK" "\$@"
EOF
chmod +x "$OUT/bin/temen-link"
cp "$HERE/app.nim" "$OUT/app.nim"
( cd "$OUT" && PATH="$OUT/bin:$PATH" nimony t --isMain app.nim )
cp "$OUT"/nimcache/t/*/app.temen "$OUT/app.temen"
echo "build.sh: $OUT/app.temen"
