/* stage_bridge — the Phase 5 macro-staging hook implementation (installed into the frontend's
 * `jacl_macro_stage_hook`). It expands a macro call by running the macro body on the SVM engine:
 *   1. codegen the body into a staged-macro module   (svm_codegen_staged_macro)
 *   2. encode the call's argument ASTs to a syn_wire  (synw_encode; version byte, then N nodes)
 *   3. run the module on SVM in-process               (jacl_svm_stage, runtime/harness staticlib)
 *   4. decode the result wire back to an AstNode       (synw_decode)
 * This lives in the codegen/driver layer (not src/) so the frontend stays free of any codegen
 * or SVM dependency — the driver installs the hook. See docs/SVM_MACRO_STAGING_PLAN.md (Phase 5).
 *
 * Compiled as its own TU (not unity with the driver) to avoid static-symbol collisions with
 * the frontend's serializers; it pulls in the frontend header for the shared types. */
#include "../../../src/jacl.h" /* AstNode, MacroEntry, arena_t, ast_alloc, arena_alloc */
#include "../../codegen.h"     /* svm_codegen_staged_macro */
#include "../../irbuilder.h"   /* irb_to_text, irb_relocs_text */
#include "syn_wire.c"          /* synw_encode / synw_decode (unity; needs the frontend above) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The in-process SVM staging bridge (runtime/harness staticlib). */
extern int jacl_svm_stage(const char *module_text, const unsigned *relocs, size_t relocs_len,
                          const unsigned char *arg_wire, size_t arg_len,
                          unsigned char **out_ptr, size_t *out_len);
extern void jacl_svm_stage_free(unsigned char *ptr, size_t len);

/* Parse "<func> <block> <inst>\n…" into a flat u32-triple array; *n gets the triple count. */
static unsigned *stage__parse_relocs(const char *text, size_t *n) {
  size_t cap = 8, cnt = 0; unsigned *v = (unsigned *)malloc(cap * 3 * sizeof(unsigned));
  const char *p = text;
  while (p && *p) {
    unsigned f, b, i;
    if (sscanf(p, "%u %u %u", &f, &b, &i) == 3) {
      if (cnt == cap) { cap *= 2; v = (unsigned *)realloc(v, cap * 3 * sizeof(unsigned)); }
      v[cnt*3] = f; v[cnt*3+1] = b; v[cnt*3+2] = i; cnt++;
    }
    const char *nl = strchr(p, '\n'); p = nl ? nl + 1 : NULL;
  }
  *n = cnt; return v;
}

/* Matches jacl_macro_stage_hook: expand `entry(args…)` on SVM, returning the AST in *out
 * (arena-allocated). NULL on success, else an error string. */
static const char *jacl_stage_macro_on_svm(MacroEntry *entry, AstNode **args, uint32_t argc,
                                           arena_t *arena, AstNode **out) {
  static char errbuf[256];
  *out = NULL;

  /* 1. codegen the staged-macro module for this body. */
  IrModule *m = svm_codegen_staged_macro((const char **)entry->param_names,
                                         entry->param_name_lens, entry->param_count,
                                         entry->body, errbuf, sizeof errbuf);
  if (!m) return errbuf[0] ? errbuf : "staged codegen failed";
  char *text  = irb_to_text(m);
  char *rtext = irb_relocs_text(m);
  size_t nrelocs = 0; unsigned *relocs = (rtext && rtext[0]) ? stage__parse_relocs(rtext, &nrelocs) : NULL;

  /* 2. encode the argument ASTs: one version byte, then each arg node back-to-back. */
  size_t cap = 256, wlen = 0; unsigned char *wire = (unsigned char *)malloc(cap);
  for (uint32_t i = 0; i < argc; i++) {
    size_t bn; unsigned char *b = synw_encode(args[i], &bn);
    if (!b) { free(wire); free(relocs); return "staged arg encode failed"; }
    size_t off = (i == 0) ? 0 : 1;   /* drop the repeated version byte on nodes after the first */
    while (wlen + (bn - off) > cap) { cap *= 2; wire = (unsigned char *)realloc(wire, cap); }
    memcpy(wire + wlen, b + off, bn - off); wlen += bn - off;
    free(b);
  }

  /* 3. run on SVM. */
  unsigned char *res = NULL; size_t reslen = 0;
  int rc = jacl_svm_stage(text, relocs, nrelocs, wire, wlen, &res, &reslen);
  free(wire); free(relocs);
  if (rc != 0) {
    snprintf(errbuf, sizeof errbuf, "SVM staging failed (rc=%d)", rc);
    return errbuf;
  }

  /* 4. decode the result wire back to an AST (strings are copied into `arena`). */
  *out = synw_decode(res, reslen, arena);
  jacl_svm_stage_free(res, reslen);
  return *out ? NULL : "staged expansion produced invalid syntax";
}

/* Install the hook. The driver calls this once at startup; activation is still gated by the
 * `JACL_STAGE_ON_SVM` env var inside the expander, so installing it is always safe. */
void jacl_install_svm_stage_hook(void) {
  jacl_macro_stage_hook = jacl_stage_macro_on_svm;
}
