/* stage_bridge_guest — the **in-guest** macro-staging hook (docs/TEMEN_GUEST_JIT_STAGING.md §3/§4).
 * Installed into the frontend's `jacl_macro_stage_hook`; when the compiler runs as an TEMEN guest
 * (`jacl_compiler.temen`) it expands a macro call by running the macro body on the SAME domain via
 * the §22 `Jit` capability — no host round-trip, no reference VM:
 *   1. codegen the body into a staged-macro module (temen_codegen_staged_macro, in_guest=1)
 *   2. serialize it to a v9 temen-encode object                (irb_to_encoded)
 *   3. build the {name -> Slot(&fn)} symbol table            (synrt_build_symtab, guest glue)
 *   4. compile+link the object against the table             (__vm_jit_compile_linked)
 *   5. hand it the argument wire, run its entry              (synrt_set_arg_wire, __vm_jit_invoke2)
 *   6. drain + decode the result wire                        (synrt_take_result, synw_decode_result)
 *
 * The staged runtime (jacl_* / synrt_*) is linked into this same `.temen` (jaclrt_staging_guest.c);
 * the symtab binds the macro object's `call.sym` imports to those functions' call_indirect slots.
 * In the shell (jacl_install_temen_stage_hook_live, jacl_impl#214) the body runs on the shell's own
 * runtime instead, live: linked against the shell's table, and installed and called as a REPL line
 * is. Compiled as its own TU (not unity with the driver), like stage_bridge.c. */
#include "../../../src/jacl.h" /* AstNode, MacroEntry, arena_t */
#include "../../codegen.h"     /* temen_codegen_staged_macro */
#include "../../irbuilder.h"   /* irb_to_encoded */
#include "syn_wire.c"          /* synw_encode_args / synw_decode_result (unity; needs the frontend above) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* §22 Jit capability (temen-llvm binds these __vm_jit_* names to iface 11 ops). */
extern long __vm_jit_compile_linked(void *ir, long ir_len, void *symtab, long symtab_len);
extern long __vm_jit_invoke2(long code, long a, long b);
extern long __vm_jit_release(long code);
extern long __vm_jit_install(long code);
extern long __vm_jit_uninstall(long slot);

/* The in-guest staging glue (jaclrt_staging_guest.c, linked into this .temen). */
extern void synrt_set_arg_wire(const unsigned char *buf, size_t len);
extern unsigned char *synrt_take_result(size_t *len);
extern size_t synrt_build_symtab(unsigned char *out, size_t cap);
extern unsigned long synrt_macro_dstack_base(void);
extern unsigned char synrt_window_log2(void);

/* The shell's table of its runtime's functions, when a macro stages on that runtime, live
 * (jacl_install_temen_stage_hook_live); NULL on the compiler card, whose staging runtime each run
 * brings up afresh. */
static const unsigned char *g_live_symtab;
static size_t g_live_symtab_len;

/* Matches jacl_macro_stage_hook: expand the macro call `call` in-guest, returning the AST in
 * *out (arena-allocated). NULL on success, else an error string, with *err_at set when the
 * macro's own error names a node. */
static const char *jacl_stage_macro_on_temen_guest(MacroEntry *entry, AstNode *call, arena_t *arena,
                                                 AstNode **out, SourcePos *err_at) {
  static char errbuf[256];
  *out = NULL;
  int live = g_live_symtab != NULL;

  /* 1-2. codegen the staged-macro module (guest ABI: arity-2 entry) and serialize it. */
  IrModule *m = temen_codegen_staged_macro((const char **)entry->param_names,
                                         entry->param_name_lens, entry->param_count,
                                         entry->variadic, entry->body, live ? 2 : 1,
                                         errbuf, sizeof errbuf);
  if (!m) return errbuf[0] ? errbuf : "staged codegen failed";
  /* Declare the parent window so the object clears the Jit memory-match precondition (§3d). */
  irb_set_memory(m, synrt_window_log2());
  int leaf = irb_module_is_leaf(m);
  size_t mlen = 0;
  uint8_t *mbytes = irb_to_encoded(m, &mlen);
  irb_module_free(m);

  /* 3. build the argument wire. */
  SynwCall c;
  synw_call_begin(&c, call);
  size_t wlen = 0;
  unsigned char *wire = synw_encode_args(entry, call, &wlen, &c);
  if (!wire) {
    free(mbytes);
    synw_call_refusal(&c, errbuf, sizeof errbuf);
    synw_call_end(&c);
    return errbuf;
  }

  /* 4. compile+link the object against a symbol table: the shell's, or one built here. The glue's
   * casts each address in place, which names the function before it in the shell's image
   * (temen#2308; runtime/harness/src/repl.rs builds the shell's). */
  static unsigned char symtab[4096];
  const unsigned char *st = g_live_symtab;
  size_t stlen = g_live_symtab_len;
  if (!live) {
    stlen = synrt_build_symtab(symtab, sizeof symtab);
    if (stlen > sizeof symtab) { free(wire); free(mbytes); synw_call_end(&c); return "staged symtab overflow"; }
    st = symtab;
  }
  long code = __vm_jit_compile_linked(mbytes, (long)mlen, (void *)st, (long)stlen);
  free(mbytes);
  if (code < 0) { free(wire); synw_call_end(&c); snprintf(errbuf, sizeof errbuf, "jit compile_linked failed (%ld)", code); return errbuf; }

  /* 5. feed the arg wire, run the entry, drain the result wire. On the compiler card the entry
   * runs over the staging data stack (invoke). On the shell's runtime it runs as a line does
   * (codegen/selfhost/repl/repl_guest.c): installed and called on this fiber, over its data
   * stack, since the line compiles on a fiber of its own, which an invoke would leave; and a unit
   * that makes a closure keeps its slot, as a line's does. Its value is the result itself, so only
   * a missing result wire is an error there. */
  synrt_set_arg_wire(wire, wlen);
  long r = 0;
  if (live) {
    long slot = __vm_jit_install(code);
    __vm_jit_release(code);
    if (slot < 0) { free(wire); synw_call_end(&c); return "no slot to install the staged macro in"; }
    (void)((long (*)(long))(uintptr_t)slot)(0);
    if (leaf) __vm_jit_uninstall(slot);
  } else {
    r = __vm_jit_invoke2(code, (long)synrt_macro_dstack_base(), 0);
    __vm_jit_release(code);
  }
  size_t reslen = 0; unsigned char *res = synrt_take_result(&reslen);
  free(wire);
  if (r < 0) { free(res); synw_call_end(&c); snprintf(errbuf, sizeof errbuf, "jit invoke failed (%ld)", r); return errbuf; }

  /* 6. decode the result wire back to an AST (strings copied into `arena`). */
  const char *err = NULL;
  *out = synw_decode_result(res, reslen, arena, &c, &err, err_at);
  free(res);
  synw_call_end(&c);
  return *out ? NULL : err;
}

/* Install the hook. The driver calls this once at startup; activation is still gated by the
 * `JACL_STAGE_ON_TEMEN` env var inside the expander, so installing it is always safe. */
void jacl_install_temen_stage_hook(void) {
  jacl_macro_stage_hook = jacl_stage_macro_on_temen_guest;
}

/* The hook for the shell's REPL (jacl_impl#214), whose image links this bridge and the glue beside
 * its one runtime: a staged macro runs on that runtime, live, linked against `symtab`, the table
 * the REPL links its lines against. */
void jacl_install_temen_stage_hook_live(const unsigned char *symtab, size_t len) {
  g_live_symtab = symtab;
  g_live_symtab_len = len;
  jacl_macro_stage_hook = jacl_stage_macro_on_temen_guest;
}
