/* stage_bridge_guest — the **in-guest** macro-staging hook (docs/SVM_GUEST_JIT_STAGING.md §3/§4).
 * Installed into the frontend's `jacl_macro_stage_hook`; when the compiler runs as an SVM guest
 * (`jacl_compiler.svmb`) it expands a macro call by running the macro body on the SAME domain via
 * the §22 `Jit` capability — no host round-trip, no reference VM:
 *   1. codegen the body into a staged-macro module (svm_codegen_staged_macro, in_guest=1)
 *   2. serialize it to a v9 svm-encode object                (irb_to_encoded)
 *   3. build the {name -> Slot(&fn)} symbol table            (synrt_build_symtab, guest glue)
 *   4. compile+link the object against the table             (__vm_jit_compile_linked)
 *   5. hand it the argument wire, run its entry              (synrt_set_arg_wire, __vm_jit_invoke2)
 *   6. drain + decode the result wire                        (synrt_take_result, synw_decode)
 *
 * The staged runtime (jacl_* / synrt_*) is linked into this same `.svmb` (jaclrt_staging_guest.c);
 * the symtab binds the macro object's `call.sym` imports to those functions' call_indirect slots.
 * Compiled as its own TU (not unity with the driver), like stage_bridge.c. */
#include "../../../src/jacl.h" /* AstNode, MacroEntry, arena_t */
#include "../../codegen.h"     /* svm_codegen_staged_macro */
#include "../../irbuilder.h"   /* irb_to_encoded */
#include "syn_wire.c"          /* synw_encode / synw_decode (unity; needs the frontend above) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* §22 Jit capability (svm-llvm binds these __vm_jit_* names to iface 11 ops). */
extern long __vm_jit_compile_linked(void *ir, long ir_len, void *symtab, long symtab_len);
extern long __vm_jit_invoke2(long code, long a, long b);
extern long __vm_jit_release(long code);

/* The in-guest staging glue (jaclrt_staging_guest.c, linked into this .svmb). */
extern void synrt_set_arg_wire(const unsigned char *buf, size_t len);
extern unsigned char *synrt_take_result(size_t *len);
extern size_t synrt_build_symtab(unsigned char *out, size_t cap);
extern unsigned long synrt_macro_dstack_base(void);
extern unsigned char synrt_window_log2(void);

/* Matches jacl_macro_stage_hook: expand `entry(args…)` in-guest, returning the AST in *out
 * (arena-allocated). NULL on success, else an error string. */
static const char *jacl_stage_macro_on_svm_guest(MacroEntry *entry, AstNode **args, uint32_t argc,
                                                 arena_t *arena, AstNode **out) {
  static char errbuf[256];
  *out = NULL;

  /* 1-2. codegen the staged-macro module (guest ABI: arity-2 entry) and serialize it. */
  IrModule *m = svm_codegen_staged_macro((const char **)entry->param_names,
                                         entry->param_name_lens, entry->param_count,
                                         entry->variadic, entry->body, /*in_guest=*/1,
                                         errbuf, sizeof errbuf);
  if (!m) return errbuf[0] ? errbuf : "staged codegen failed";
  /* Declare the parent window so the object clears the Jit memory-match precondition (§3d). */
  irb_set_memory(m, synrt_window_log2());
  size_t mlen = 0;
  uint8_t *mbytes = irb_to_encoded(m, &mlen);
  irb_module_free(m);

  /* 3. build the argument wire (one shared version byte, then a syntax node per fixed parameter,
   * then — variadic only — a u32 count and that many rest-argument nodes). synw_encode prepends a
   * per-node version byte, so drop it off each node after the first shared one. */
  uint32_t nfixed = (entry->variadic && entry->param_count > 0) ? entry->param_count - 1 : entry->param_count;
  size_t cap = 256, wlen = 0; unsigned char *wire = (unsigned char *)malloc(cap);
  wire[wlen++] = (unsigned char)SYNW_VERSION;
  #define STAGE_APPEND(PTR, N) do { \
      while (wlen + (N) > cap) { cap *= 2; wire = (unsigned char *)realloc(wire, cap); } \
      memcpy(wire + wlen, (PTR), (N)); wlen += (N); } while (0)
  for (uint32_t i = 0; i < argc; i++) {
    if (i == nfixed) { uint32_t rest_count = argc - nfixed; STAGE_APPEND(&rest_count, sizeof rest_count); }
    size_t bn; unsigned char *b = synw_encode(args[i], &bn);
    if (!b) { free(wire); free(mbytes); return "staged arg encode failed"; }
    STAGE_APPEND(b + 1, bn - 1);
    free(b);
  }
  if (entry->variadic && argc == nfixed) { uint32_t rest_count = 0; STAGE_APPEND(&rest_count, sizeof rest_count); }
  #undef STAGE_APPEND

  /* 4. build the symbol table and compile+link the object against it. */
  static unsigned char symtab[4096];
  size_t stlen = synrt_build_symtab(symtab, sizeof symtab);
  if (stlen > sizeof symtab) { free(wire); free(mbytes); return "staged symtab overflow"; }
  long code = __vm_jit_compile_linked(mbytes, (long)mlen, symtab, (long)stlen);
  free(mbytes);
  if (code < 0) { free(wire); snprintf(errbuf, sizeof errbuf, "jit compile_linked failed (%ld)", code); return errbuf; }

  /* 5. feed the arg wire, run the entry over the staging data stack, drain the result wire. */
  synrt_set_arg_wire(wire, wlen);
  long r = __vm_jit_invoke2(code, (long)synrt_macro_dstack_base(), 0);
  size_t reslen = 0; unsigned char *res = synrt_take_result(&reslen);
  __vm_jit_release(code);
  free(wire);
  if (r < 0) { free(res); snprintf(errbuf, sizeof errbuf, "jit invoke failed (%ld)", r); return errbuf; }

  /* 6. decode the result wire back to an AST (strings copied into `arena`). */
  *out = synw_decode(res, reslen, arena);
  free(res);
  return *out ? NULL : "staged expansion produced invalid syntax";
}

/* Install the hook. The driver calls this once at startup; activation is still gated by the
 * `JACL_STAGE_ON_SVM` env var inside the expander, so installing it is always safe. */
void jacl_install_svm_stage_hook(void) {
  jacl_macro_stage_hook = jacl_stage_macro_on_svm_guest;
}
