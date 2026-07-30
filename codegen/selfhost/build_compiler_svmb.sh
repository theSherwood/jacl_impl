#!/usr/bin/env bash
#
# Build the JACL compiler as an SVM guest: compile the C frontend to LLVM IR, run it
# through the vendor/svm LLVM on-ramp, and emit `jacl_compiler.svmb` — a module that
# reads JACL source on stdin and writes SVM-IR text on stdout.
#
#   codegen/selfhost/build_compiler_svmb.sh [--selftest]
#
# Env overrides: CLANG (clang-18), LLVM_LINK (llvm-link-18), SVM_LLVM_TRANSLATE and
# TRY_TRANSLATE (prebuilt on-ramp binaries; otherwise built from vendor/svm).
#
# Requires clang-18 + llvm-link-18. Skips cleanly (exit 0) if they are absent, like
# the svm demo builds, so it can sit in a build that lacks the dev toolchain.
# Rationale + measured feasibility: docs/SVM_SELFHOST_FEASIBILITY.md.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"          # codegen/selfhost
CODEGEN="$(cd "$DIR/.." && pwd)"              # codegen
ROOT="$(cd "$CODEGEN/.." && pwd)"             # repo root
SVM="$ROOT/vendor/svm"
OUT="$DIR/build"
mkdir -p "$OUT"

CLANG="${CLANG:-clang-18}"
LLVM_LINK="${LLVM_LINK:-llvm-link-18}"

if ! command -v "$CLANG" >/dev/null || ! command -v "$LLVM_LINK" >/dev/null; then
  echo "note: $CLANG / $LLVM_LINK not found — skipping JACL-on-SVM build (dev toolchain absent)."
  exit 0
fi

# On-ramp binaries: use prebuilt if given, else build from the vendored svm submodule.
TRANSLATE="${SVM_LLVM_TRANSLATE:-$SVM/crates/svm-llvm/target/release/svm-llvm-translate}"
TRY="${TRY_TRANSLATE:-$SVM/crates/svm-llvm/target/release/examples/try_translate}"
if [ ! -x "$TRANSLATE" ]; then
  echo "=== building svm-llvm-translate from vendor/svm ==="
  ( cd "$SVM/crates/svm-llvm" && cargo build --release --bin svm-llvm-translate --example try_translate )
fi

CF=(-std=c99 -O2 -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -D__EMSCRIPTEN__
    -Wno-implicit-function-declaration -fno-vectorize -fno-slp-vectorize
    -emit-llvm -S -I "$CODEGEN" -I "$DIR/inc")

echo "=== compiling frontend + driver + shim to LLVM IR ==="
# emit_jacl.c is the unity frontend (defines jacl_emit_ir); demote its CLI main so
# the driver's main is the single entry.
"$CLANG" "${CF[@]}" -Dmain=jacl_cli_main "$CODEGEN/tests/emit_jacl.c" -o "$OUT/frontend.ll"
"$CLANG" "${CF[@]}" "$CODEGEN/codegen.c"    -o "$OUT/codegen.ll"
"$CLANG" "${CF[@]}" "$CODEGEN/irbuilder.c"  -o "$OUT/irbuilder.ll"
"$CLANG" "${CF[@]}" "$DIR/emit_driver.c"    -o "$OUT/emit_driver.ll"
"$CLANG" "${CF[@]}" "$DIR/emit_shim.c"      -o "$OUT/emit_shim.ll"

# In-guest macro staging (docs/SVM_GUEST_JIT_STAGING.md §3/§4): the jaclrt runtime + staged-macro
# glue (jaclrt_staging_guest.c) and the Jit-capability hook (stage_bridge_guest.c), linked in so
# macros expand on the SVM engine in this same domain instead of the reference VM. Needs `-I runtime`.
MS="$DIR/macro_staging"
"$CLANG" "${CF[@]}" -I "$ROOT/runtime" "$MS/jaclrt_staging_guest.c" -o "$OUT/jaclrt_staging_guest.ll"
"$CLANG" "${CF[@]}" -I "$ROOT/runtime" "$MS/stage_bridge_guest.c"   -o "$OUT/stage_bridge_guest.ll"

echo "=== linking whole program ==="
"$LLVM_LINK" -S "$OUT/emit_driver.ll" "$OUT/frontend.ll" "$OUT/codegen.ll" \
             "$OUT/irbuilder.ll" "$OUT/emit_shim.ll" \
             "$OUT/jaclrt_staging_guest.ll" "$OUT/stage_bridge_guest.ll" -o "$OUT/jacl_compiler.ll"

echo "=== translating to SVM IR (jacl_compiler.svmb) ==="
# --stub-externs traps-if-called the dead runtime surface (fork/exec/... from vm.c,
# never reached on the emit path). See the feasibility doc's symbol footprint.
SVM_STUB_EXTERNS=1 "$TRANSLATE" "$OUT/jacl_compiler.ll" -o "$OUT/jacl_compiler.svmb" --binary --stub-externs
echo "  -> $OUT/jacl_compiler.svmb ($(wc -c < "$OUT/jacl_compiler.svmb") bytes)"

if [ "${1:-}" = "--selftest" ]; then
  echo "=== selftest: compile a fixed JACL program on the SVM engine ==="
  # A driver with a hardcoded source (no stdin), so try_translate — which runs the
  # entry under the on-ramp powerbox but feeds no stdin — can prove source -> IR.
  cat > "$OUT/selftest_main.c" <<'EOF'
#include <stdio.h>
extern char *jacl_emit_ir(const char *source);
int main(void) { char *ir = jacl_emit_ir("[+ 1 [* 2 3]]"); fputs(ir ? ir : "NULL", stdout); return 0; }
EOF
  "$CLANG" "${CF[@]}" "$OUT/selftest_main.c" -o "$OUT/selftest_main.ll"
  "$LLVM_LINK" -S "$OUT/selftest_main.ll" "$OUT/frontend.ll" "$OUT/codegen.ll" \
               "$OUT/irbuilder.ll" "$OUT/emit_shim.ll" -o "$OUT/selftest.ll"
  SVM_STUB_EXTERNS=1 "$TRY" "$OUT/selftest.ll" 2>&1 | tee "$OUT/selftest.out"
  # 1 + 2*3 = 7 must appear as an i32.mul + i32.add in the emitted IR.
  if grep -q 'i32.mul' "$OUT/selftest.out" && grep -q 'i32.add' "$OUT/selftest.out"; then
    echo "SELFTEST PASS — JACL source compiled to SVM IR on the SVM engine."
  else
    echo "SELFTEST FAIL — expected mul/add in emitted IR." >&2
    exit 1
  fi

  echo "=== stagetest: expand a macro IN-GUEST via the Jit capability ==="
  # `twice 21` runs the whole compile — including the macro expansion — on the SVM engine, with the
  # in-guest staging hook (stage_bridge_guest.c) doing the expansion via __vm_jit_compile_linked +
  # invoke2. `twice` builds `[+ ~x ~x]`, so its body has the `+` symbol literal — exercising the
  # item-2 data-free string lowering (synrt_str_* instead of a data segment the Jit cap forbids, §3b).
  # The expansion `[+ 21 21]` type-lowers to `i32.const 21` (twice) + `i32.add` in the emitted IR.
  cat > "$OUT/stagetest_main.c" <<'EOF'
#include <stdio.h>
extern char *jacl_emit_ir(const char *source);
extern void jacl_install_svm_stage_hook(void);
int main(void) {
  jacl_install_svm_stage_hook();
  char *ir = jacl_emit_ir("defmacro twice {x} {\n  syntax-quote [+ ~x ~x]\n}\ntwice 21\n");
  fputs(ir ? ir : "NULL", stdout);
  return 0;
}
EOF
  "$CLANG" "${CF[@]}" "$OUT/stagetest_main.c" -o "$OUT/stagetest_main.ll"
  "$LLVM_LINK" -S "$OUT/stagetest_main.ll" "$OUT/frontend.ll" "$OUT/codegen.ll" "$OUT/irbuilder.ll" \
               "$OUT/emit_shim.ll" "$OUT/jaclrt_staging_guest.ll" "$OUT/stage_bridge_guest.ll" \
               -o "$OUT/stagetest.ll"
  SVM_STUB_EXTERNS=1 "$TRY" "$OUT/stagetest.ll" 2>&1 | tee "$OUT/stagetest.out"
  if grep -q 'i32.add' "$OUT/stagetest.out" && [ "$(grep -c 'i32.const 21' "$OUT/stagetest.out")" -ge 2 ] \
       && ! grep -q '%%ERROR%%' "$OUT/stagetest.out"; then
    echo "STAGETEST PASS — string-bearing macro expanded in-guest via the Jit capability."
  else
    echo "STAGETEST FAIL — expected 'twice 21' -> [+ 21 21] (i32.add of two 21s) with no error." >&2
    exit 1
  fi
fi

echo "=== done ==="
