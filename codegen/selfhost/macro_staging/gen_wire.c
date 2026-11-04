/* Compiler-side helper. `gen_wire SNIPPET` parses the snippet and writes its nodes[0] as a
 * single-value syn_wire (version byte + node) on stdout — what the cross-codec test feeds the
 * runtime side. `gen_wire --args A B …` writes the argument wire a macro of that many
 * parameters is called with (version, header, then each node) — what the staging test feeds a
 * staged macro. `gen_wire --pp SNIPPET` prints the frontend's pretty-print of the snippet. */
#include "../../../src/jacl.c"
#include "syn_wire.c"
#include <stdio.h>
HeadId jacl_compute_head_id_bridge(AstNode *head) { return ast__compute_head_id(head); }

static AstNode *parse_one(const char *src, arena_t *a) {
  ParseResult pr = parser_parse(lexer_lex(src, a), a);
  return pr.count >= 1 ? pr.nodes[0] : NULL;
}

int main(int argc, char **argv) {
  if (argc < 2) return 2;
  arena_t a = {0};
  unsigned char *b = NULL;
  size_t n = 0;
  if (strcmp(argv[1], "--pp") == 0 && argc == 3) {   /* the frontend's own pretty-print */
    AstNode *node = parse_one(argv[2], &a);
    if (!node) return 3;
    fputs(ast_pretty_print(node, &a), stdout);
    return 0;
  }
  if (strcmp(argv[1], "--args") == 0) {
    uint32_t nargs = (uint32_t)(argc - 2);
    AstNode **args = nargs ? (AstNode **)arena_alloc(&a, sizeof(AstNode *) * nargs) : NULL;
    for (uint32_t i = 0; i < nargs; i++)
      if (!(args[i] = parse_one(argv[2 + i], &a))) return 3;
    AstNode call = {0};
    call.type = AST_COMMAND;
    call.data.command.args = args;
    call.data.command.arg_count = nargs;
    MacroEntry entry = {0};
    entry.param_count = nargs;
    SynwCall c;
    synw_call_begin(&c, &call);
    b = synw_encode_args(&entry, &call, &n, &c);
    synw_call_end(&c);
  } else {
    AstNode *node = parse_one(argv[1], &a);
    if (!node) return 3;
    /* With a call table, so the nodes carry real (nonzero) position handles. */
    SynwCall c;
    synw_call_begin(&c, node);
    b = synw_encode(node, &n, &c);
    synw_call_end(&c);
  }
  if (!b) return 4;
  fwrite(b, 1, n, stdout);
  free(b);
  return 0;
}
