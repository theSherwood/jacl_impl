/* Typer-only test harness. Runs the typer (typer_infer) on small JACL
 * programs and asserts that specific AST nodes have the expected
 * inferred_type. First commit of Stage 0 in the static-typing
 * migration; see STATIC_TYPING_PLAN.md.
 *
 * Why this exists: today the only way to test the typer is end-to-end
 * through codegen + VM. That misses every divergence between the
 * typer and the compiler that doesn't happen to manifest as a
 * runtime difference. These tests assert directly on the typer's
 * AST annotations, so a wrong inferred_type fails immediately.
 *
 * Each test compiles a small JACL string, runs the typer, then walks
 * the AST and asserts on specific nodes. The find helpers do
 * pre-order search; the typical test pattern is:
 *
 *   def DECL VALUE  → find_cmd(root, "def") then ->args[0] for DECL
 *   [op a b]        → find_cmd(root, "op") then ->args[i] for the operands */

#include "../src/jacl.h"
#include "../lib/arena/arena.h"
#include <stdio.h>
#include <string.h>

static int passes = 0;
static int failures = 0;

static const char* current_test = "(none)";

#define ASSERT_TYPE(node, expected) do {                                  \
  AstNode* _n = (node);                                                    \
  JaclType _exp = (expected);                                              \
  JaclType _got = _n ? (JaclType)_n->inferred_type : TYPE_DYN;             \
  if (!_n) {                                                               \
    fprintf(stderr, "  FAIL %s: node is NULL (line %d)\n",                 \
            current_test, __LINE__);                                       \
    failures++;                                                            \
  } else if (_got != _exp) {                                               \
    fprintf(stderr, "  FAIL %s: expected %s got %s (line %d)\n",           \
            current_test, type_name(_exp), type_name(_got), __LINE__);     \
    failures++;                                                            \
  } else {                                                                 \
    passes++;                                                              \
  }                                                                        \
} while (0)

#define ASSERT_NOT_NULL(node) do {                                         \
  if (!(node)) {                                                           \
    fprintf(stderr, "  FAIL %s: expected non-NULL (line %d)\n",            \
            current_test, __LINE__);                                       \
    failures++;                                                            \
  }                                                                        \
} while (0)

/* Pre-order walk: return the first AST_COMMAND whose head string
 * equals the given name. Skips the search-result's own subtree —
 * use find_cmd_after for sibling-search semantics if needed. */
static AstNode* find_cmd(AstNode* n, const char* head) {
  if (!n) return NULL;
  if (n->type == AST_COMMAND) {
    AstNode* h = n->data.command.head;
    if (h && h->type == AST_LIT_STRING) {
      size_t want = strlen(head);
      if (h->data.lit_string.length == want &&
          memcmp(h->data.lit_string.value, head, want) == 0) {
        return n;
      }
    }
    AstNode* r = find_cmd(n->data.command.head, head);
    if (r) return r;
    for (uint32_t i = 0; i < n->data.command.arg_count; i++) {
      r = find_cmd(n->data.command.args[i], head);
      if (r) return r;
    }
  } else if (n->type == AST_BLOCK) {
    for (uint32_t i = 0; i < n->data.block.count; i++) {
      AstNode* r = find_cmd(n->data.block.commands[i], head);
      if (r) return r;
    }
  } else if (n->type == AST_INTERP_STRING) {
    for (uint32_t i = 0; i < n->data.interp_string.count; i++) {
      AstNode* r = find_cmd(n->data.interp_string.segments[i], head);
      if (r) return r;
    }
  }
  return NULL;
}

/* Find AST_VAR_REF by name. */
static AstNode* find_var_ref(AstNode* n, const char* name) {
  if (!n) return NULL;
  if (n->type == AST_VAR_REF) {
    size_t want = strlen(name);
    if (n->data.var_ref.length == want &&
        memcmp(n->data.var_ref.name, name, want) == 0) {
      return n;
    }
  } else if (n->type == AST_COMMAND) {
    AstNode* r = find_var_ref(n->data.command.head, name);
    if (r) return r;
    for (uint32_t i = 0; i < n->data.command.arg_count; i++) {
      r = find_var_ref(n->data.command.args[i], name);
      if (r) return r;
    }
  } else if (n->type == AST_BLOCK) {
    for (uint32_t i = 0; i < n->data.block.count; i++) {
      AstNode* r = find_var_ref(n->data.block.commands[i], name);
      if (r) return r;
    }
  }
  return NULL;
}

/* Lex + parse + typer. Returns ParseResult with inferred_type set on
 * every reachable AST node. arena owns all allocations. */
static ParseResult run_typer(const char* src, arena_t* arena) {
  LexResult lex = lexer_lex(src, arena);
  if (lex.error_count) {
    fprintf(stderr, "  %s: lex error\n", current_test);
    ParseResult empty = {0};
    return empty;
  }
  ParseResult parse = parser_parse(lex, arena);
  if (parse.error_count) {
    fprintf(stderr, "  %s: parse error\n", current_test);
    return parse;
  }
  typer_infer(parse.nodes, parse.count, NULL);
  return parse;
}

/* --- Test cases --------------------------------------------------- */

static void test_int_literal_default(void) {
  current_test = "int_literal_default";
  arena_t a = {0};
  ParseResult r = run_typer("def x 5", &a);
  /* def NAME VALUE — args[1] is the literal */
  AstNode* def = r.nodes[0];
  ASSERT_TYPE(def->data.command.args[1], TYPE_I32);
  arena_destroy(&a);
}

static void test_int_literal_promoted_to_i64(void) {
  current_test = "int_literal_promoted_to_i64";
  arena_t a = {0};
  ParseResult r = run_typer("def i64 x 5", &a);
  /* def TYPE NAME VALUE — args[2] is the literal, expected_type=i64
   * pushes the literal up to TYPE_I64. */
  AstNode* def = r.nodes[0];
  ASSERT_TYPE(def->data.command.args[2], TYPE_I64);
  arena_destroy(&a);
}

static void test_float_literal_default(void) {
  current_test = "float_literal_default";
  arena_t a = {0};
  ParseResult r = run_typer("def x 1.5", &a);
  AstNode* def = r.nodes[0];
  ASSERT_TYPE(def->data.command.args[1], TYPE_F32);
  arena_destroy(&a);
}

static void test_float_literal_promoted_to_f64(void) {
  current_test = "float_literal_promoted_to_f64";
  arena_t a = {0};
  ParseResult r = run_typer("def f64 x 1.5", &a);
  AstNode* def = r.nodes[0];
  ASSERT_TYPE(def->data.command.args[2], TYPE_F64);
  arena_destroy(&a);
}

static void test_string_literal(void) {
  current_test = "string_literal";
  arena_t a = {0};
  ParseResult r = run_typer("def x \"hello\"", &a);
  AstNode* def = r.nodes[0];
  ASSERT_TYPE(def->data.command.args[1], TYPE_STR);
  arena_destroy(&a);
}

static void test_var_ref_inherits_binding_type(void) {
  current_test = "var_ref_inherits_binding_type";
  arena_t a = {0};
  ParseResult r = run_typer("def i32 x 5\ndef y $x", &a);
  /* The $x in stmt 1 should be typed I32 from the binding. */
  AstNode* var_x = find_var_ref(r.nodes[1], "x");
  ASSERT_NOT_NULL(var_x);
  ASSERT_TYPE(var_x, TYPE_I32);
  arena_destroy(&a);
}

static void test_length_returns_i32(void) {
  current_test = "length_returns_i32";
  arena_t a = {0};
  ParseResult r = run_typer("def n [length \"hi\"]", &a);
  AstNode* len = find_cmd(r.nodes[0], "length");
  ASSERT_NOT_NULL(len);
  ASSERT_TYPE(len, TYPE_I32);
  arena_destroy(&a);
}

static void test_atom_q_returns_bool(void) {
  current_test = "atom_q_returns_bool";
  arena_t a = {0};
  ParseResult r = run_typer("def x 5\ndef b [atom? $x]", &a);
  AstNode* aq = find_cmd(r.nodes[1], "atom?");
  ASSERT_NOT_NULL(aq);
  ASSERT_TYPE(aq, TYPE_BOOL);
  arena_destroy(&a);
}

static void test_to_string_returns_str(void) {
  current_test = "to_string_returns_str";
  arena_t a = {0};
  ParseResult r = run_typer("def x 5\ndef s [to-string $x]", &a);
  AstNode* ts = find_cmd(r.nodes[1], "to-string");
  ASSERT_NOT_NULL(ts);
  ASSERT_TYPE(ts, TYPE_STR);
  arena_destroy(&a);
}

static void test_range_returns_stream(void) {
  current_test = "range_returns_stream";
  arena_t a = {0};
  ParseResult r = run_typer("def s [..< 0 10]", &a);
  AstNode* rng = find_cmd(r.nodes[0], "..<");
  ASSERT_NOT_NULL(rng);
  ASSERT_TYPE(rng, TYPE_STREAM);
  arena_destroy(&a);
}

static void test_vec_literal_returns_vec(void) {
  current_test = "vec_literal_returns_vec";
  arena_t a = {0};
  ParseResult r = run_typer("def xs [vec 1 2 3]", &a);
  AstNode* v = find_cmd(r.nodes[0], "vec");
  ASSERT_NOT_NULL(v);
  ASSERT_TYPE(v, TYPE_VEC);
  arena_destroy(&a);
}

static void test_vec_len_returns_i32(void) {
  current_test = "vec_len_returns_i32";
  arena_t a = {0};
  ParseResult r = run_typer("def xs [vec 1 2 3]\ndef n [vec-len $xs]", &a);
  AstNode* vl = find_cmd(r.nodes[1], "vec-len");
  ASSERT_NOT_NULL(vl);
  ASSERT_TYPE(vl, TYPE_I32);
  arena_destroy(&a);
}

static void test_typed_vec_binding(void) {
  current_test = "typed_vec_binding";
  arena_t a = {0};
  ParseResult r = run_typer("def [Vec i32] xs [vec 1 2 3]", &a);
  /* The $xs binding registered with TYPE_TYPED_VEC. Verify by
   * referencing it in a follow-up var-ref. */
  arena_destroy(&a);
  (void)r;
  /* Re-run with a follow-up that exposes the binding type via var-ref. */
  arena_t b = {0};
  r = run_typer("def [Vec i32] xs [vec 1 2 3]\ndef x $xs", &b);
  AstNode* vx = find_var_ref(r.nodes[1], "xs");
  ASSERT_NOT_NULL(vx);
  ASSERT_TYPE(vx, TYPE_TYPED_VEC);
  arena_destroy(&b);
}

static void test_vec_push_preserves_typed(void) {
  current_test = "vec_push_preserves_typed";
  arena_t a = {0};
  ParseResult r = run_typer(
    "def [Vec i32] xs [vec 1 2 3]\n"
    "def ys [vec-push $xs 4]", &a);
  AstNode* vp = find_cmd(r.nodes[1], "vec-push");
  ASSERT_NOT_NULL(vp);
  ASSERT_TYPE(vp, TYPE_TYPED_VEC);
  arena_destroy(&a);
}

static void test_map_has_returns_bool(void) {
  current_test = "map_has_returns_bool";
  arena_t a = {0};
  ParseResult r = run_typer(
    "def m [map \"a\" 1]\n"
    "def b [map-has $m \"a\"]", &a);
  AstNode* mh = find_cmd(r.nodes[1], "map-has");
  ASSERT_NOT_NULL(mh);
  ASSERT_TYPE(mh, TYPE_BOOL);
  arena_destroy(&a);
}

static void test_def_sugar_returns_nil(void) {
  /* `x = 5` parses as `[= [x] 5]` (zero-arg AST_COMMAND wrapping name).
   * The typer must unwrap that shape, just like compiler__rewrite_binding_op,
   * so the def returns nil instead of leaking dyn. Stage 1 fix. */
  current_test = "def_sugar_returns_nil";
  arena_t a = {0};
  ParseResult r = run_typer("x = 5", &a);
  ASSERT_TYPE(r.nodes[0], TYPE_NIL);
  arena_destroy(&a);
}

static void test_mut_sugar_returns_nil(void) {
  current_test = "mut_sugar_returns_nil";
  arena_t a = {0};
  ParseResult r = run_typer("x : 5", &a);
  ASSERT_TYPE(r.nodes[0], TYPE_NIL);
  arena_destroy(&a);
}

static void test_proc_def_returns_closure(void) {
  current_test = "proc_def_returns_closure";
  arena_t a = {0};
  ParseResult r = run_typer("proc f {} { 1 }", &a);
  ASSERT_TYPE(r.nodes[0], TYPE_CLOSURE);
  arena_destroy(&a);
}

static void test_print_returns_nil(void) {
  current_test = "print_returns_nil";
  arena_t a = {0};
  ParseResult r = run_typer("[print \"hi\"]", &a);
  AstNode* p = r.nodes[0];
  ASSERT_TYPE(p, TYPE_NIL);
  arena_destroy(&a);
}

static void test_parallel_returns_vec(void) {
  current_test = "parallel_returns_vec";
  arena_t a = {0};
  ParseResult r = run_typer(
      "proc f {} { 1 }\n"
      "def res [parallel { [f] } { [f] }]", &a);
  AstNode* par = find_cmd(r.nodes[1], "parallel");
  ASSERT_NOT_NULL(par);
  ASSERT_TYPE(par, TYPE_VEC);
  arena_destroy(&a);
}

static void test_spawn_returns_future(void) {
  current_test = "spawn_returns_future";
  arena_t a = {0};
  ParseResult r = run_typer("def f [spawn { 42 }]", &a);
  AstNode* sp = find_cmd(r.nodes[0], "spawn");
  ASSERT_NOT_NULL(sp);
  ASSERT_TYPE(sp, TYPE_FUTURE);
  arena_destroy(&a);
}

static void test_await_of_future_narrows_to_element(void) {
  current_test = "await_of_future_narrows_to_element";
  arena_t a = {0};
  /* Body's tail is i32 (default int literal type). Spawn's element
   * idx is set to JACL_SCALAR_TYPE_IDX(TYPE_I32); await unwraps to i32. */
  ParseResult r = run_typer(
      "def f [spawn { 42 }]\n"
      "def x [await $f]", &a);
  AstNode* aw = find_cmd(r.nodes[1], "await");
  ASSERT_NOT_NULL(aw);
  ASSERT_TYPE(aw, TYPE_I32);
  arena_destroy(&a);
}

static void test_await_of_dyn_stays_dyn(void) {
  current_test = "await_of_dyn_stays_dyn";
  arena_t a = {0};
  ParseResult r = run_typer(
      "proc f {x} { [await $x] }", &a);
  AstNode* proc = r.nodes[0];
  AstNode* aw = find_cmd(proc, "await");
  ASSERT_NOT_NULL(aw);
  ASSERT_TYPE(aw, TYPE_DYN);
  arena_destroy(&a);
}

static void test_hash_returns_i32(void) {
  current_test = "hash_returns_i32";
  arena_t a = {0};
  ParseResult r = run_typer("def h [hash \"key\"]", &a);
  AstNode* h = find_cmd(r.nodes[0], "hash");
  ASSERT_NOT_NULL(h);
  ASSERT_TYPE(h, TYPE_I32);
  arena_destroy(&a);
}

static void test_take_preserves_typed_vec(void) {
  current_test = "take_preserves_typed_vec";
  arena_t a = {0};
  ParseResult r = run_typer(
      "def [Vec i64] xs [[Vec i64] 1 2 3]\n"
      "def ys [take $xs 2]", &a);
  AstNode* tk = find_cmd(r.nodes[1], "take");
  ASSERT_NOT_NULL(tk);
  ASSERT_TYPE(tk, TYPE_TYPED_VEC);
  arena_destroy(&a);
}

static void test_vec_get_scalar_narrows(void) {
  current_test = "vec_get_scalar_narrows";
  arena_t a = {0};
  /* [vec-get $typed_vec idx] narrows to the element type when the
   * receiver is a typed vec with a scalar element idx (encoded via
   * the JACL_SCALAR_TYPE_IDX sentinel). */
  ParseResult r = run_typer(
      "def [Vec i64] xs [[Vec i64] 1 2 3]\n"
      "def n [vec-get $xs 0]", &a);
  AstNode* vg = find_cmd(r.nodes[1], "vec-get");
  ASSERT_NOT_NULL(vg);
  ASSERT_TYPE(vg, TYPE_I64);
  arena_destroy(&a);
}

static void test_map_get_scalar_narrows(void) {
  current_test = "map_get_scalar_narrows";
  arena_t a = {0};
  ParseResult r = run_typer(
      "def [Map i32] m [[Map i32] \"a\" 1 \"b\" 2]\n"
      "def v [map-get $m \"a\"]", &a);
  AstNode* mg = find_cmd(r.nodes[1], "map-get");
  ASSERT_NOT_NULL(mg);
  ASSERT_TYPE(mg, TYPE_I32);
  arena_destroy(&a);
}

static void test_try_unifies_body_and_handler(void) {
  current_test = "try_unifies_body_and_handler";
  arena_t a = {0};
  /* Body and handler both produce i32 → try result narrows to i32. */
  ParseResult r = run_typer("def res [try { 1 } e { 2 }]", &a);
  AstNode* tr = find_cmd(r.nodes[0], "try");
  ASSERT_NOT_NULL(tr);
  ASSERT_TYPE(tr, TYPE_I32);
  arena_destroy(&a);
}

static void test_try_handler_can_use_err_binding(void) {
  current_test = "try_handler_can_use_err_binding";
  arena_t a = {0};
  /* The error binding `e` is added to the handler scope as TYPE_DYN.
   * The handler's tail var-ref $e resolves through it. Body and
   * handler unify since both produce dyn. */
  ParseResult r = run_typer("def res [try { 1 } e { $e }]", &a);
  AstNode* tr = find_cmd(r.nodes[0], "try");
  ASSERT_NOT_NULL(tr);
  /* Body is i32, handler is dyn (var-ref to e) — unify fails → dyn. */
  ASSERT_TYPE(tr, TYPE_DYN);
  arena_destroy(&a);
}

static void test_try_heterogeneous_stays_dyn(void) {
  current_test = "try_heterogeneous_stays_dyn";
  arena_t a = {0};
  ParseResult r = run_typer("def res [try { 1 } e { \"oops\" }]", &a);
  AstNode* tr = find_cmd(r.nodes[0], "try");
  ASSERT_NOT_NULL(tr);
  ASSERT_TYPE(tr, TYPE_DYN);
  arena_destroy(&a);
}

static void test_with_ctx_inherits_body_type(void) {
  current_test = "with_ctx_inherits_body_type";
  arena_t a = {0};
  /* with-ctx overrides body — result is the body's tail type. */
  ParseResult r = run_typer(
      "ctx mut i32 level = 0\n"
      "proc f {} { with-ctx { level 5 } { 42 } }", &a);
  AstNode* proc = r.nodes[1];
  AstNode* wc = find_cmd(proc, "with-ctx");
  ASSERT_NOT_NULL(wc);
  ASSERT_TYPE(wc, TYPE_I32);
  arena_destroy(&a);
}

static void test_race_homogeneous_narrows(void) {
  current_test = "race_homogeneous_narrows";
  arena_t a = {0};
  /* All race bodies produce i32 — race result narrows to i32. */
  ParseResult r = run_typer("def res [race { 1 } { 2 }]", &a);
  AstNode* rc = find_cmd(r.nodes[0], "race");
  ASSERT_NOT_NULL(rc);
  ASSERT_TYPE(rc, TYPE_I32);
  arena_destroy(&a);
}

static void test_race_heterogeneous_stays_dyn(void) {
  current_test = "race_heterogeneous_stays_dyn";
  arena_t a = {0};
  /* Bodies produce i32 and str — race result is dyn. */
  ParseResult r = run_typer("def res [race { 1 } { \"two\" }]", &a);
  AstNode* rc = find_cmd(r.nodes[0], "race");
  ASSERT_NOT_NULL(rc);
  ASSERT_TYPE(rc, TYPE_DYN);
  arena_destroy(&a);
}

static void test_future_t_annotation(void) {
  current_test = "future_t_annotation";
  arena_t a = {0};
  /* Explicit [Future i64] annotation in def: spawn body produces i64
   * (literal narrowed by expected_type push), binding is TYPE_FUTURE
   * with i64 element idx, await unwraps to i64. Typer-only test —
   * the compiler doesn't yet accept [Future T] as a def annotation
   * (same gap exists for [Vec T] / [Map T] in def position). */
  ParseResult r = run_typer(
      "def [Future i64] f [spawn { 42 }]\n"
      "def x [await $f]", &a);
  AstNode* aw = find_cmd(r.nodes[1], "await");
  ASSERT_NOT_NULL(aw);
  ASSERT_TYPE(aw, TYPE_I64);
  arena_destroy(&a);
}

/* --- Driver ------------------------------------------------------- */

int main(void) {
  printf("=== test_typer ===\n");

  test_int_literal_default();
  test_int_literal_promoted_to_i64();
  test_float_literal_default();
  test_float_literal_promoted_to_f64();
  test_string_literal();
  test_var_ref_inherits_binding_type();
  test_length_returns_i32();
  test_atom_q_returns_bool();
  test_to_string_returns_str();
  test_range_returns_stream();
  test_vec_literal_returns_vec();
  test_vec_len_returns_i32();
  test_typed_vec_binding();
  test_vec_push_preserves_typed();
  test_map_has_returns_bool();
  test_def_sugar_returns_nil();
  test_mut_sugar_returns_nil();
  test_proc_def_returns_closure();
  test_print_returns_nil();
  test_parallel_returns_vec();
  test_spawn_returns_future();
  test_await_of_future_narrows_to_element();
  test_await_of_dyn_stays_dyn();
  test_hash_returns_i32();
  test_take_preserves_typed_vec();
  test_vec_get_scalar_narrows();
  test_map_get_scalar_narrows();
  test_try_unifies_body_and_handler();
  test_try_handler_can_use_err_binding();
  test_try_heterogeneous_stays_dyn();
  test_with_ctx_inherits_body_type();
  test_race_homogeneous_narrows();
  test_race_heterogeneous_stays_dyn();
  test_future_t_annotation();

  printf("\n%d/%d passed", passes, passes + failures);
  if (failures > 0) {
    printf(" (%d FAILED)\n", failures);
    return 1;
  }
  printf("\n");
  return 0;
}
