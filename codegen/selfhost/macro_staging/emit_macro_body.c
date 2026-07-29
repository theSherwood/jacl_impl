/* Tool: read a .jacl file containing a `defmacro`, compile its body to a module with a
 * single __jacl_macro(sp, params…) function (svm_codegen_macro_body), print the svm-text.
 * The codegen input to the staged-macro link (see stage_macro.rs). */
#include "../../../src/jacl.c"
HeadId jacl_compute_head_id_bridge(AstNode *head) { return ast__compute_head_id(head); }
#include "../../codegen.h"
#include "../../irbuilder.h"
#include <stdio.h>
#include <stdlib.h>

static char *slurp(const char *path) {
  FILE *f = fopen(path, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  char *b = malloc(n + 1); if (!b) { fclose(f); return NULL; }
  if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
  b[n] = 0; fclose(f); return b;
}

int main(int argc, char **argv) {
  /* `--staged` emits a complete runnable staged-macro program (entry + body, for the
   * on-SVM run); default emits just the __jacl_macro function (for inspection / linking). */
  int staged = 0; const char *path = NULL;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--staged")) staged = 1;
    else path = argv[i];
  }
  if (!path) { fprintf(stderr, "usage: emit_macro_body [--staged] <file.jacl>\n"); return 2; }
  char *src = slurp(path);
  if (!src) { fprintf(stderr, "cannot read %s\n", path); return 1; }
  arena_t a = {0};
  ParseResult pr = parser_parse(lexer_lex(src, &a), &a);
  AstNode *dm = NULL;
  for (uint32_t i = 0; i < pr.count; i++) if (pr.nodes[i]->type == AST_DEFMACRO) { dm = pr.nodes[i]; break; }
  if (!dm) { fprintf(stderr, "no defmacro in %s\n", path); return 1; }
  char err[256] = {0};
  IrModule *m;
  if (staged) {
    m = svm_codegen_staged_macro((const char **)dm->data.defmacro.param_names,
                                 dm->data.defmacro.param_name_lens,
                                 dm->data.defmacro.param_count,
                                 dm->data.defmacro.body, err, sizeof err);
  } else {
    uint32_t idx = 0;
    m = svm_codegen_macro_body((const char **)dm->data.defmacro.param_names,
                               dm->data.defmacro.param_name_lens,
                               dm->data.defmacro.param_count,
                               dm->data.defmacro.body, &idx, err, sizeof err);
  }
  if (!m) { fprintf(stderr, "codegen: %s\n", err); return 1; }
  fputs(irb_to_text(m), stdout);
  /* String/data literals lower to data-segment references that need SelfData relocations;
   * emit them after a %%RELOCS%% sentinel (same wire format the frontend --file path uses),
   * so the linker (stage_macro.rs) can apply them. Without this, strings come out empty. */
  char *relocs = irb_relocs_text(m);
  if (relocs && relocs[0]) { fputs("%%RELOCS%%\n", stdout); fputs(relocs, stdout); }
  return 0;
}
