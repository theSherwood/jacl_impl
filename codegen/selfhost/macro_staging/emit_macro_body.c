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
                                 dm->data.defmacro.variadic,
                                 dm->data.defmacro.body, /*in_guest=*/0, err, sizeof err);
  } else {
    uint32_t idx = 0;
    m = svm_codegen_macro_body((const char **)dm->data.defmacro.param_names,
                               dm->data.defmacro.param_name_lens,
                               dm->data.defmacro.param_count,
                               dm->data.defmacro.body, &idx, err, sizeof err);
  }
  if (!m) { fprintf(stderr, "codegen: %s\n", err); return 1; }
  /* Emit the svm-encode **object** (v9); stage_macro.rs decodes it with `decode_emitted`.
   * String/data literals lower to inline `data.self` addresses that `link` resolves — no
   * separate relocation section (the object carries everything). */
  size_t len = 0;
  uint8_t *bytes = irb_to_encoded(m, &len);
  if (len && fwrite(bytes, 1, len, stdout) != len) { perror("fwrite"); free(bytes); return 1; }
  free(bytes);
  return 0;
}
