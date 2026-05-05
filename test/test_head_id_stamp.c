#include "../src/jacl.h"
#include "../lib/arena/arena.h"
#include <stdio.h>
#include <string.h>

/* Walk AST checking head_id matches head string for a known subset. */
static int errors = 0;
static int checked = 0;

static HeadId expected_for(const char* s, uint32_t len) {
  if (len == 5 && memcmp(s, "while", 5) == 0) return HEAD_WHILE;
  if (len == 3 && memcmp(s, "def",   3) == 0) return HEAD_DEF;
  if (len == 3 && memcmp(s, "vec",   3) == 0) return HEAD_VEC;
  if (len == 4 && memcmp(s, "proc",  4) == 0) return HEAD_PROC;
  if (len == 2 && memcmp(s, "if",    2) == 0) return HEAD_IF;
  if (len == 7 && memcmp(s, "vec-get", 7) == 0) return HEAD_VEC_GET;
  if (len == 1 && s[0] == '+') return HEAD_PLUS;
  if (len == 1 && s[0] == '<') return HEAD_LT;
  if (len == 2 && memcmp(s, "==", 2) == 0) return HEAD_EQ_EQ;
  if (len == 5 && memcmp(s, "print", 5) == 0) return HEAD_PRINT;
  if (len == 6 && memcmp(s, "return", 6) == 0) return HEAD_RETURN;
  return HEAD_NONE;
}

static void walk(AstNode* n) {
  if (!n) return;
  if (n->type == AST_COMMAND) {
    AstNode* h = n->data.command.head;
    if (h && h->type == AST_LIT_STRING && h->data.lit_string.length > 0) {
      HeadId got = (HeadId)n->data.command.head_id;
      HeadId exp = expected_for(h->data.lit_string.value, h->data.lit_string.length);
      checked++;
      if (exp != HEAD_NONE && got != exp) {
        fprintf(stderr, "MISMATCH: head=\"%.*s\" expected=%d got=%d\n",
                (int)h->data.lit_string.length, h->data.lit_string.value, (int)exp, (int)got);
        errors++;
      }
      /* If the string is one we know, ensure ast_head_id matches via re-lookup */
      if (exp != HEAD_NONE && got == HEAD_NONE) {
        fprintf(stderr, "UNSTAMPED: head=\"%.*s\" head_id=HEAD_NONE\n",
                (int)h->data.lit_string.length, h->data.lit_string.value);
        errors++;
      }
    }
    walk(n->data.command.head);
    for (uint32_t i = 0; i < n->data.command.arg_count; i++) {
      walk(n->data.command.args[i]);
    }
  } else if (n->type == AST_BLOCK) {
    for (uint32_t i = 0; i < n->data.block.count; i++) walk(n->data.block.commands[i]);
  } else if (n->type == AST_INTERP_STRING) {
    for (uint32_t i = 0; i < n->data.interp_string.count; i++) walk(n->data.interp_string.segments[i]);
  } else if (n->type == AST_BREAK) walk(n->data.break_stmt.value);
  else if (n->type == AST_RETURN) walk(n->data.return_stmt.value);
  else if (n->type == AST_SHELL_CMD) {
    walk(n->data.shell_cmd.head);
    for (uint32_t i = 0; i < n->data.shell_cmd.arg_count; i++) walk(n->data.shell_cmd.args[i]);
  }
}

int main(void) {
  const char* src =
    "def x = 1\n"
    "if (x < 10) { print x } else { print 0 }\n"
    "def v = [vec 1 2 3]\n"
    "def y = (1 + 2)\n"
    "while (x == 1) { def z = [vec-get $v 0] }\n"
    "proc f {a b} { def r = (a + b); return $r }\n";

  arena_t arena = {0};
  LexResult lex = lexer_lex(src, &arena);
  if (lex.error_count) { fprintf(stderr, "lex errors: %u\n", lex.error_count); return 2; }
  ParseResult parse = parser_parse(lex, &arena);
  if (parse.error_count) { fprintf(stderr, "parse errors: %u\n", parse.error_count); return 2; }

  for (uint32_t i = 0; i < parse.count; i++) walk(parse.nodes[i]);

  printf("checked %d head_ids\n", checked);
  if (errors) { printf("FAIL: %d mismatch(es)\n", errors); return 1; }
  printf("OK: all stamped head_ids match expected values\n");
  arena_destroy(&arena);
  return 0;
}
