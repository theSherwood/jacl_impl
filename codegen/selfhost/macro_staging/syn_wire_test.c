/* Tests for syn_wire. Round trip: parse a snippet -> AST -> bytes -> AST, and assert the
 * pretty-print and a re-encode are identical and every node comes back at the position it had.
 * Then the per-expansion rules: a node the macro made takes the call's position, a gensym the
 * macro minted is named for the expansion, a form that cannot cross is refused with a reason,
 * and a result wire carries either the expansion or the macro's error. Unity build over the
 * frontend. */
#include "../../../src/jacl.c"
#include "syn_wire.c"
#include <stdio.h>

static AstNode *parse_one(const char *src, arena_t *a) {
  ParseResult pr = parser_parse(lexer_lex(src, a), a);
  return (pr.count >= 1 && pr.error_count == 0) ? pr.nodes[0] : NULL;
}

static int same_table(const SynwCall *x, const SynwCall *y) {
  return x->count == y->count &&
         (x->count == 0 || (memcmp(x->starts, y->starts, x->count * sizeof(SourcePos)) == 0 &&
                            memcmp(x->ends, y->ends, x->count * sizeof(SourcePos)) == 0));
}

static int roundtrip(const char *src) {
  arena_t a = {0};
  AstNode *orig = parse_one(src, &a);
  if (!orig) { printf("  PARSE FAIL: %s\n", src); return 1; }
  SynwCall c1, c2;
  synw_call_begin(&c1, orig);
  synw_call_begin(&c2, orig);
  size_t n1, n2;
  unsigned char *b1 = synw_encode(orig, &n1, &c1);
  if (!b1) { printf("  ENCODE FAIL: %s\n", src); return 1; }
  AstNode *dec = synw_decode(b1, n1, &a, &c1);
  if (!dec) { printf("  DECODE FAIL: %s\n", src); free(b1); return 1; }
  const char *p1 = ast_pretty_print(orig, &a), *p2 = ast_pretty_print(dec, &a);
  int ok_pp = p1 && p2 && strcmp(p1, p2) == 0;
  /* Re-encoding the decoded tree records its positions in the same order; equal tables mean
   * every node got back where it was. */
  unsigned char *b2 = synw_encode(dec, &n2, &c2);
  int ok_bytes = b2 && n1 == n2 && memcmp(b1, b2, n1) == 0;
  int ok_pos = same_table(&c1, &c2);
  free(b1); free(b2);
  synw_call_end(&c1); synw_call_end(&c2);
  if (ok_pp && ok_bytes && ok_pos) { printf("  PASS  %-46s (%zu bytes)\n", src, n1); return 0; }
  printf("  FAIL  %s\n    pp1=[%s]\n    pp2=[%s]\n    bytes_eq=%d positions_eq=%d\n", src,
         p1 ? p1 : "?", p2 ? p2 : "?", ok_bytes, ok_pos);
  return 1;
}

static int check(int ok, const char *what) {
  printf(ok ? "  PASS  %s\n" : "  FAIL  %s\n", what);
  return ok ? 0 : 1;
}

/* Positions are real source positions: the last node of a three-line block is on line 3. */
static int positions_are_recorded(void) {
  arena_t a = {0};
  AstNode *orig = parse_one("{\n  def a 1\n  print \"a is $a\"\n}", &a);
  SynwCall c;
  synw_call_begin(&c, orig);
  size_t n;
  unsigned char *b = synw_encode(orig, &n, &c);
  int ok = b && c.count > 3 && c.starts[0].line == 1 && c.starts[c.count - 1].line == 3;
  free(b);
  synw_call_end(&c);
  return check(ok, "positions are recorded per node");
}

/* A node the macro made (handle 0) takes the call's position. */
static int made_nodes_take_the_call_position(void) {
  arena_t a = {0};
  AstNode *call = parse_one("\n\n   unless $c { print 1 }", &a);
  AstNode *tmpl = parse_one("[if $c {} { print 1 }]", &a);
  SynwCall c;
  synw_call_begin(&c, call);
  size_t n;
  unsigned char *b = synw_encode(tmpl, &n, NULL);   /* no table: every handle is 0 */
  AstNode *dec = b ? synw_decode(b, n, &a, &c) : NULL;
  int ok = dec && dec->start.line == 3 && dec->start.column == 4 &&
           dec->data.command.args[0]->start.line == 3;
  free(b);
  synw_call_end(&c);
  return check(ok, "nodes the macro made take the call's position");
}

/* A gensym the macro minted (handle 0) is named for the expansion; one that came in as an
 * argument keeps its name. */
static int gensyms_are_named_for_the_expansion(void) {
  arena_t a = {0};
  AstNode *g = parse_one("$k__0", &a);
  g->is_gensym = 1;
  SynwCall c1, c2;
  synw_call_begin(&c1, g);
  synw_call_begin(&c2, g);
  size_t n;
  unsigned char *made = synw_encode(g, &n, NULL);
  AstNode *d1 = synw_decode(made, n, &a, &c1);
  AstNode *d2 = synw_decode(made, n, &a, &c2);
  char want1[32], want2[32];
  snprintf(want1, sizeof want1, "k__0_%u", c1.expansion);
  snprintf(want2, sizeof want2, "k__0_%u", c2.expansion);
  int ok_made = d1 && d2 && c1.expansion != c2.expansion &&
                strcmp(d1->data.var_ref.name, want1) == 0 &&
                strcmp(d2->data.var_ref.name, want2) == 0;
  free(made);
  unsigned char *passed = synw_encode(g, &n, &c1);   /* an argument: it gets a handle */
  AstNode *d3 = synw_decode(passed, n, &a, &c1);
  int ok_passed = d3 && strcmp(d3->data.var_ref.name, "k__0") == 0;
  free(passed);
  synw_call_end(&c1); synw_call_end(&c2);
  return check(ok_made, "a gensym the macro minted is named for its expansion") +
         check(ok_passed, "a gensym passed in as an argument keeps its name");
}

static int wide_literals_are_refused(void) {
  arena_t a = {0};
  AstNode *big = parse_one("99999999999999999999999", &a);
  SynwCall c;
  synw_call_begin(&c, big);
  size_t n;
  unsigned char *b = synw_encode(big, &n, &c);
  char msg[256];
  synw_call_refusal(&c, msg, sizeof msg);
  int ok = !b && c.bad == big && strstr(msg, "wider than 64 bits") != NULL;
  free(b);
  synw_call_end(&c);
  return check(ok, "a literal wider than 64 bits is refused, with the reason");
}

static int results_carry_the_expansion_or_an_error(void) {
  arena_t a = {0};
  const unsigned char failed[] = { SYNW_VERSION, SYNW_RESULT_ERROR, 3, 0, 0, 0, 'b', 'a', 'd' };
  const char *err = NULL;
  AstNode *n = synw_decode_result(failed, sizeof failed, &a, NULL, &err);
  int ok_err = !n && err && strcmp(err, "bad") == 0;

  AstNode *orig = parse_one("[+ 1 2]", &a);
  size_t len;
  unsigned char *node = synw_encode(orig, &len, NULL);
  unsigned char *ok_wire = (unsigned char *)malloc(len + 1);
  ok_wire[0] = SYNW_VERSION;
  ok_wire[1] = SYNW_RESULT_OK;
  memcpy(ok_wire + 2, node + 1, len - 1);
  AstNode *exp = synw_decode_result(ok_wire, len + 1, &a, NULL, &err);
  int ok_exp = exp && strcmp(ast_pretty_print(exp, &a), ast_pretty_print(orig, &a)) == 0;
  free(node); free(ok_wire);

  n = synw_decode_result(NULL, 0, &a, NULL, &err);
  int ok_empty = !n && err != NULL;
  return check(ok_err, "a result wire carries the macro's error") +
         check(ok_exp, "a result wire carries the expansion") +
         check(ok_empty, "an empty result is an error, not a crash");
}

int main(void) {
  const char *cases[] = {
    "42", "foo", "[+ 1 2]", "[+ 1 [* 2 3]]",
    "[if [== 1 2] {} { set hit 5 }]", "{ set x 1 }", "[twice [twice 5]]",
    "\"x is $x and $[+ 1 2]\"", "{ print \"x is $x\" }", "[vec ..$xs]",
    "{ !ls -la | !wc -l }", "{ !sleep 1 & }",
    "{ for $xs x { if [> $x 2] { break } else { continue } } }",
    "{ return 5 }", "{ return }", "{ break 3 }",
    "syntax-quote [if ~c {} ~@body]", "quote [a b]",
    "{\n  def a 1\n  print \"$a\"\n}",
  };
  int fails = 0;
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) fails += roundtrip(cases[i]);
  fails += positions_are_recorded();
  fails += made_nodes_take_the_call_position();
  fails += gensyms_are_named_for_the_expansion();
  fails += wide_literals_are_refused();
  fails += results_carry_the_expansion_or_an_error();
  printf(fails ? "syn_wire: %d FAILED\n" : "syn_wire: all tests passed\n", fails);
  return fails ? 1 : 0;
}
