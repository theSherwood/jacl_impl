/* interpret_bridge_guest — the **in-guest** `[interpret SRC]` hook (docs/SVM_GUEST_JIT_STAGING.md §5,
 * model B). Installed into `jacl_interpret_hook`; when a codegen-linked program (the compiler `.svmb`,
 * or a test harness) evaluates `[interpret SRC]`, `jacl_interpret1` calls this instead of the host
 * `interp` capability round-trip (interpcap.c) — retiring `vm.c` from the interpret path.
 *
 * It is the macro-staging machinery generalized from a macro *body* to a whole *program*:
 *   1. lex + parse + type the source                   (the frontend, linked into this .svmb;
 *                                                        macro expansion is a follow-up — see below)
 *   2. codegen a runnable in-guest module               (svm_codegen_program, in_guest=1 — an arity-2
 *                                                         entry that runs the program inline, no
 *                                                         scheduler, since the §22 Jit cap forbids §12)
 *   3. serialize to a v9 svm-encode object              (irb_to_encoded, data-segment-free via §3b)
 *   4. compile+link against the runtime symbol table    (synrt_build_symtab, __vm_jit_compile_linked)
 *   5. run it on the Jit cap, over the staging stack     (__vm_jit_invoke2)
 *
 * The result needs **no marshalling**: the unit runs in *this* domain (same window, same heap — the
 * symtab binds its `jacl_*` imports to the compiler's own runtime), so the entry's return value IS a
 * live `JaclVal` in this window — a rich value (int / vec / map / string / closure), not just the
 * scalar the host wire could carry. Non-concurrent source only for now (the inline entry has no
 * scheduler); concurrent `interpret` (spawn/await) is a later slice on the fiber-hosting Jit grant.
 *
 * NB: value predicates/constructors come from the **frontend** (`src/jacl.h`: `jacl_is_string`,
 * `JACL_NIL`/`JACL_FLAG_ERROR`) — the runtime's `jaclrt_*` equivalents are `static inline` in
 * `jaclrt.h` (no external symbol), so declaring them `extern` here would trap under `--stub-externs`.
 * The two worlds share the JaclVal encoding (a heap string is tag 5 in both). */
#include "../../../src/jacl.h" /* JaclVal, JACL_NIL/JACL_FLAG_ERROR, jacl_is_string, lexer/parser/… */
#include "../../codegen.h"     /* svm_codegen_program */
#include "../../irbuilder.h"   /* irb_to_encoded, irb_set_memory, irb_module_free */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* §22 Jit capability (svm-llvm binds these to iface 11 ops). */
extern long __vm_jit_compile_linked(void *ir, long ir_len, void *symtab, long symtab_len);
extern long __vm_jit_invoke2(long code, long a, long b);
extern long __vm_jit_release(long code);

/* The in-guest staging glue (jaclrt_staging_guest.c, linked into this .svmb) — shared with macros. */
extern size_t synrt_build_symtab(unsigned char *out, size_t cap);
extern unsigned long synrt_macro_dstack_base(void);
extern unsigned char synrt_window_log2(void);

/* Real (non-inline) jaclrt string accessors (string.c) — safe to call. */
extern uint32_t jacl_str_len(JaclVal s);
extern void jacl_str_bytes(JaclVal s, char *out, uint32_t cap);

#define INTERP_ERR ((JaclVal)(JACL_NIL | JACL_FLAG_ERROR))

/* `jacl_interpret_hook`-shaped: compile SRC and run it on the Jit cap, returning its value (a live
 * JaclVal in this window). An error value on any failure — a bad source is a catchable interpret
 * error, exactly like the ungranted-cap path (interpcap.c). */
static JaclVal jacl_interpret_on_svm_guest(JaclVal src) {
  if (!jacl_is_string(src)) return INTERP_ERR;
  uint32_t slen = jacl_str_len(src);
  char *buf = (char *)malloc((size_t)slen + 1);
  if (!buf) return INTERP_ERR;
  jacl_str_bytes(src, buf, slen + 1);
  buf[slen] = 0;

  /* 1. frontend: source -> typed AST (the reference-VM static-error oracle is intentionally skipped —
   * it is a compile-time diagnostic that reaches vm.c, which this path exists to retire). */
  arena_t arena = {0};
  LexResult toks = lexer_lex(buf, &arena);
  free(buf);
  if (toks.error_count) { arena_destroy(&arena); return INTERP_ERR; }
  ParseResult parse = parser_parse(toks, &arena);
  if (parse.error_count) { arena_destroy(&arena); return INTERP_ERR; }
  /* NOTE (slice 1): macro expansion is intentionally not run yet. The frontend's
   * `expand_macros_inplace` is `static` (emit_jacl.c) and spins up a throwaway reference VM for the
   * expansion heap; wiring an in-guest expansion (or exposing a linkable entry) is a follow-up, so
   * for now `[interpret …]` handles macro-free source. */
  typer_infer(parse.nodes, parse.count, NULL, NULL, 0, NULL, 0);

  /* 2-3. codegen a runnable in-guest module and serialize it. */
  char err[256] = {0};
  IrModule *m = svm_codegen_program(parse.nodes, parse.count, /*module_mode=*/0, /*in_guest=*/1,
                                    err, sizeof err);
  arena_destroy(&arena);
  if (!m) return INTERP_ERR;
  irb_set_memory(m, synrt_window_log2());   /* satisfy the Jit memory-match precondition (§3d) */
  size_t mlen = 0;
  uint8_t *mbytes = irb_to_encoded(m, &mlen);
  irb_module_free(m);
  if (!mbytes) return INTERP_ERR;

  /* 4. build the runtime symbol table and compile+link the object against it. */
  static unsigned char symtab[4096];
  size_t stlen = synrt_build_symtab(symtab, sizeof symtab);
  if (stlen > sizeof symtab) { free(mbytes); return INTERP_ERR; }
  long code = __vm_jit_compile_linked(mbytes, (long)mlen, symtab, (long)stlen);
  free(mbytes);
  if (code < 0) return INTERP_ERR;

  /* 5. run the entry over the staging data stack; its return value is the program's last value —
   * a live JaclVal in this same window (no wire, no marshalling). */
  long r = __vm_jit_invoke2(code, (long)synrt_macro_dstack_base(), 0);
  __vm_jit_release(code);
  return (JaclVal)r;
}

/* Install the hook. The driver calls this once at startup; `jacl_interpret1` (interpcap.c) uses it
 * when set and otherwise falls back to the host `interp` capability, so installing it is always safe. */
void jacl_install_svm_interpret_hook(void) {
  jacl_interpret_hook = jacl_interpret_on_svm_guest;
}
