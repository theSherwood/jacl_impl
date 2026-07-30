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
"$CLANG" "${CF[@]}" -I "$ROOT/runtime" "$MS/jaclrt_staging_guest.c"    -o "$OUT/jaclrt_staging_guest.ll"
"$CLANG" "${CF[@]}" -I "$ROOT/runtime" "$MS/stage_bridge_guest.c"      -o "$OUT/stage_bridge_guest.ll"
# In-guest `[interpret …]` (docs/SVM_GUEST_JIT_STAGING.md §5, model B): compile+run the source on the
# Jit cap in this same window (jacl_interpret_hook), retiring vm.c from the interpret path.
"$CLANG" "${CF[@]}" -I "$ROOT/runtime" "$MS/interpret_bridge_guest.c" -o "$OUT/interpret_bridge_guest.ll"

echo "=== linking whole program ==="
"$LLVM_LINK" -S "$OUT/emit_driver.ll" "$OUT/frontend.ll" "$OUT/codegen.ll" \
             "$OUT/irbuilder.ll" "$OUT/emit_shim.ll" \
             "$OUT/jaclrt_staging_guest.ll" "$OUT/stage_bridge_guest.ll" \
             "$OUT/interpret_bridge_guest.ll" -o "$OUT/jacl_compiler.ll"

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

  echo "=== stagetest: expand the whole macro corpus IN-GUEST via the Jit capability ==="
  # Each corpus program runs the whole compile — including its macro expansion — on the SVM engine,
  # with the in-guest staging hook (stage_bridge_guest.c) expanding via __vm_jit_compile_linked +
  # invoke2. Every body carries string/symbol literals (`+`, `if`, `vec`, `mut`, …), so all five
  # exercise the item-2 data-free string lowering (synrt_str_* instead of a `data` segment the Jit
  # cap forbids, §3b). One translate+run covers the corpus; the per-case markers below assert the
  # expansions match the native goldens (codegen/selfhost/macro_staging/golden):
  #   twice  21              -> [+ 21 21]            : i32.add of two `i32.const 21`
  #   unless [==1 2] {…}      -> [if … {} …]          : an `if`-lowering (br_if)
  #   quad   5               -> [+ [+ 5 5] [+ 5 5]]  : four `i32.const 5`, three `i32.add` (re-expansion)
  #   pack   10 20 30 40     -> [vec 10 20 30 40]    : four `jacl_vec_push` (variadic `~@rest` splice)
  #   fresh  42              -> [mut g__0 42]        : the `[gensym]` -> synrt_gensym mint runs in-guest
  cat > "$OUT/stagetest_main.c" <<'EOF'
#include <stdio.h>
#include <string.h>
extern char *jacl_emit_ir(const char *source);
extern void jacl_install_svm_stage_hook(void);
static const char *const CASES[][2] = {
  {"twice",  "defmacro twice {x} { syntax-quote [+ ~x ~x] }\ntwice 21\n"},
  {"unless", "defmacro unless {cond body} { syntax-quote [if ~cond {} ~body] }\nmut hit 0\nunless [== 1 2] { set hit 5 }\nhit\n"},
  {"nested", "defmacro twice {x} { syntax-quote [+ ~x ~x] }\ndefmacro quad {x} { syntax-quote [twice [twice ~x]] }\nquad 5\n"},
  {"splice", "defmacro pack {first ..rest} { syntax-quote [vec ~first ~@rest] }\npack 10 20 30 40\n"},
  {"gensym", "defmacro fresh {v} { syntax-quote [mut [gensym] ~v] }\nproc run {} {\n  fresh 42\n  99\n}\nrun\n"},
};
int main(void) {
  jacl_install_svm_stage_hook();
  for (int i = 0; i < (int)(sizeof CASES / sizeof CASES[0]); i++) {
    char *ir = jacl_emit_ir(CASES[i][1]);
    printf("===CASE %s BEGIN===\n%s\n===CASE %s END===\n", CASES[i][0], ir ? ir : "NULL", CASES[i][0]);
  }
  return 0;
}
EOF
  "$CLANG" "${CF[@]}" "$OUT/stagetest_main.c" -o "$OUT/stagetest_main.ll"
  "$LLVM_LINK" -S "$OUT/stagetest_main.ll" "$OUT/frontend.ll" "$OUT/codegen.ll" "$OUT/irbuilder.ll" \
               "$OUT/emit_shim.ll" "$OUT/jaclrt_staging_guest.ll" "$OUT/stage_bridge_guest.ll" \
               -o "$OUT/stagetest.ll"
  SVM_STUB_EXTERNS=1 "$TRY" "$OUT/stagetest.ll" 2>&1 | tee "$OUT/stagetest.out"
  # Slice per-case output with awk so the greps below can't cross a case boundary.
  case_out() { awk "/CASE $1 BEGIN/,/CASE $1 END/" "$OUT/stagetest.out"; }
  sf=0
  [ "$(case_out twice  | grep -c 'i32.const 21')" -ge 2 ] && case_out twice  | grep -q 'i32.add'      || { echo "STAGETEST FAIL: twice"  >&2; sf=1; }
  case_out unless | grep -q 'br_if'                                                                    || { echo "STAGETEST FAIL: unless" >&2; sf=1; }
  [ "$(case_out nested | grep -c 'i32.const 5')" -ge 4 ] && [ "$(case_out nested | grep -c 'i32.add')" -ge 3 ] || { echo "STAGETEST FAIL: nested" >&2; sf=1; }
  [ "$(case_out splice | grep -c 'jacl_vec_push')" -ge 4 ]                                             || { echo "STAGETEST FAIL: splice" >&2; sf=1; }
  case_out gensym | grep -q '144115188075855971'                                                       || { echo "STAGETEST FAIL: gensym" >&2; sf=1; }
  grep -q '%%ERROR%%\|NULL' "$OUT/stagetest.out"                                                       && { echo "STAGETEST FAIL: error/NULL in output" >&2; sf=1; }
  if [ "$sf" = 0 ]; then
    echo "STAGETEST PASS — the whole macro corpus (twice/unless/nested/splice/gensym) expanded in-guest via the Jit capability."
  else
    echo "STAGETEST FAIL — a corpus case did not expand as expected in-guest." >&2
    exit 1
  fi

  echo "=== interptest: run [interpret SRC] IN-GUEST via the Jit capability (model B, §5) ==="
  # `[interpret SRC]` with the in-guest interpret hook installed: SRC is compiled (svm_codegen_program
  # in_guest=1) and run on the Jit cap in this same window (interpret_bridge_guest.c), returning the
  # result as a live JaclVal — no host round-trip, no reference VM. Two cases, both printing a tagged
  # int (high byte 0x02, low bits the value):
  #   PLAIN  [+ 1 [* 2 3]]                          -> 7
  #   MACRO  defmacro dbl {x} {…[+ ~x ~x]}; dbl 5   -> 10  (the macro expands IN-GUEST via the staging
  #                                                         hook, then the program [+ 5 5] runs in-guest)
  cat > "$OUT/interptest_main.c" <<'EOF'
#include <stdio.h>
extern void jacl_install_svm_stage_hook(void);
extern void jacl_install_svm_interpret_hook(void);
extern long jacl_interpret1(long src);                 /* JaclVal jacl_interpret1(JaclVal) */
extern long jacl_str_new(const char *s, unsigned len); /* JaclVal jacl_str_new(const char*, uint32) */
extern void jacl_heap_init(void);
extern void jacl_intern_init(void);
extern void jacl_map_init(void);
static void run(const char *label, const char *prog, unsigned len) {
  long r = jacl_interpret1(jacl_str_new(prog, len));
  printf("%s TAG=%d RESULT=%d\n", label, (int)((unsigned long)r >> 56), (int)(r & 0xffffffff));
}
int main(void) {
  jacl_heap_init(); jacl_intern_init(); jacl_map_init();
  jacl_install_svm_stage_hook();       /* so macros in interpreted source expand in-guest */
  jacl_install_svm_interpret_hook();
  run("PLAIN", "[+ 1 [* 2 3]]", 13);
  const char *mac = "defmacro dbl {x} { syntax-quote [+ ~x ~x] }\ndbl 5\n";
  run("MACRO", mac, (unsigned)__builtin_strlen(mac));
  /* CONCUR: concurrent source (spawn/await) — the bridge detects concurrency and codegens the
   * scheduler entry (in_guest=2), so the program body runs on the scheduler root fiber over the
   * fiber-hosting Jit grant; the spawned { block } runs as a job (its funcref installed unit-own,
   * vm #546) and await suspends/resumes the root fiber for the result. Returns 7. */
  const char *con = "def f [spawn { [+ 1 [* 2 3]] }]\nawait $f\n";
  run("CONCUR", con, (unsigned)__builtin_strlen(con));
  /* MAP: a collection program — build a map literal and read it back. Exercises the broadened
   * runtime symtab (jacl_map_empty/set/get) beyond the arithmetic/macro subset. Returns 42. */
  const char *mp = "def m [map \"k\" 42]\nmap-get $m \"k\"\n";
  run("MAP", mp, (unsigned)__builtin_strlen(mp));
  /* NESTED: `[interpret …]` *inside* interpreted source — the inner program imports jacl_interpret1,
   * which (the hook still installed) compiles+runs it on the Jit cap in this same window too. Returns 7. */
  const char *nst = "[interpret \"[+ 4 3]\"]";
  run("NESTED", nst, (unsigned)__builtin_strlen(nst));
  return 0;
}
EOF
  "$CLANG" "${CF[@]}" "$OUT/interptest_main.c" -o "$OUT/interptest_main.ll"
  "$LLVM_LINK" -S "$OUT/interptest_main.ll" "$OUT/frontend.ll" "$OUT/codegen.ll" "$OUT/irbuilder.ll" \
               "$OUT/emit_shim.ll" "$OUT/jaclrt_staging_guest.ll" "$OUT/stage_bridge_guest.ll" \
               "$OUT/interpret_bridge_guest.ll" -o "$OUT/interptest.ll"
  # JACL_POOL_WORKERS=1 keeps the scheduler cooperative single-thread (cont.* only, no thread.spawn) —
  # the submitted-unit concurrency model (a spawned vCPU may not outlive the synchronous cap.call).
  JACL_POOL_WORKERS=1 SVM_STUB_EXTERNS=1 "$TRY" "$OUT/interptest.ll" 2>&1 | tee "$OUT/interptest.out"
  if grep -q 'PLAIN TAG=2 RESULT=7' "$OUT/interptest.out" && grep -q 'MACRO TAG=2 RESULT=10' "$OUT/interptest.out" \
     && grep -q 'CONCUR TAG=2 RESULT=7' "$OUT/interptest.out" \
     && grep -q 'MAP TAG=2 RESULT=42' "$OUT/interptest.out" \
     && grep -q 'NESTED TAG=2 RESULT=7' "$OUT/interptest.out"; then
    echo "INTERPTEST PASS — [interpret …] ran in-guest via the Jit capability (plain -> 7, macro-bearing -> 10, concurrent spawn/await -> 7, map -> 42, nested interpret -> 7), returning live JaclVals."
  else
    echo "INTERPTEST FAIL — expected in-guest interpret: plain 7, macro 10, concurrent 7, map 42, nested 7." >&2
    exit 1
  fi
fi

echo "=== done ==="
