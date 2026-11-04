/* stage_bridge — the Phase 5 macro-staging hook implementation (installed into the frontend's
 * `jacl_macro_stage_hook`). It expands a macro call by running the macro body on the TEMEN engine:
 *   1. codegen the body into a staged-macro module   (temen_codegen_staged_macro)
 *   2. encode the call's argument ASTs to a syn_wire  (synw_encode_args)
 *   3. run the module on TEMEN in-process               (jacl_temen_stage, runtime/harness staticlib)
 *   4. decode the result wire back to an AstNode       (synw_decode_result)
 * This lives in the codegen/driver layer (not src/) so the frontend stays free of any codegen
 * or TEMEN dependency — the driver installs the hook. See docs/TEMEN_MACRO_STAGING_PLAN.md (Phase 5).
 *
 * Compiled as its own TU (not unity with the driver) to avoid static-symbol collisions with
 * the frontend's serializers; it pulls in the frontend header for the shared types. */
#include "../../../src/jacl.h" /* AstNode, MacroEntry, arena_t, ast_alloc, arena_alloc */
#include "../../codegen.h"     /* temen_codegen_staged_macro */
#include "../../irbuilder.h"   /* irb_to_encoded */
#include "syn_wire.c"          /* synw_encode_args / synw_decode_result (unity; needs the frontend above) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The in-process TEMEN staging bridge (runtime/harness staticlib): takes the temen-encode object
 * bytes (v9; own-data addresses are inline `data.self`, resolved by link — no reloc table). */
extern int jacl_temen_stage(const unsigned char *module_bytes, size_t module_len,
                          const unsigned char *arg_wire, size_t arg_len,
                          unsigned char **out_ptr, size_t *out_len);
extern void jacl_temen_stage_free(unsigned char *ptr, size_t len);

/* Matches jacl_macro_stage_hook: expand the macro call `call` on TEMEN, returning the AST in
 * *out (arena-allocated). NULL on success, else an error string. */
static const char *jacl_stage_macro_on_temen(MacroEntry *entry, AstNode *call, arena_t *arena,
                                           AstNode **out) {
  static char errbuf[256];
  *out = NULL;

  /* 1. codegen the staged-macro module for this body. */
  IrModule *m = temen_codegen_staged_macro((const char **)entry->param_names,
                                         entry->param_name_lens, entry->param_count,
                                         entry->variadic, entry->body, /*in_guest=*/0, errbuf, sizeof errbuf);
  if (!m) return errbuf[0] ? errbuf : "staged codegen failed";
  size_t mlen = 0;
  uint8_t *mbytes = irb_to_encoded(m, &mlen);

  /* 2. encode the argument ASTs to the wire the staged entry reads. */
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

  /* 3. run on TEMEN. */
  unsigned char *res = NULL; size_t reslen = 0;
  int rc = jacl_temen_stage(mbytes, mlen, wire, wlen, &res, &reslen);
  free(wire); free(mbytes);
  if (rc != 0) {
    synw_call_end(&c);
    snprintf(errbuf, sizeof errbuf, "TEMEN staging failed (rc=%d)", rc);
    return errbuf;
  }

  /* 4. decode the result wire back to an AST (strings are copied into `arena`). */
  const char *err = NULL;
  *out = synw_decode_result(res, reslen, arena, &c, &err);
  jacl_temen_stage_free(res, reslen);
  synw_call_end(&c);
  return *out ? NULL : err;
}

/* Install the hook. The driver calls this once at startup; activation is still gated by the
 * `JACL_STAGE_ON_TEMEN` env var inside the expander, so installing it is always safe. */
void jacl_install_temen_stage_hook(void) {
  jacl_macro_stage_hook = jacl_stage_macro_on_temen;
}
