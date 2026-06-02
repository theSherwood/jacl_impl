/* Typer-only test harness. Runs the typer (typer_infer) on small JACL
 * programs and asserts that specific AST nodes have the expected
 * inferred_type. See TYPE_SYSTEM.md for the type system overview.
 *
 * Why this exists: end-to-end tests through codegen + VM miss any
 * divergence between the typer and the compiler that doesn't happen
 * to manifest as a runtime difference. Asserting directly on the
 * typer's AST annotations, a wrong inferred_type fails immediately
 * even when the bytecode compiles cleanly.
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
  typer_infer(parse.nodes, parse.count, NULL, NULL, 0, NULL);
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

static void test_first_narrows_typed_vec_elem(void) {
  current_test = "first_narrows_typed_vec_elem";
  arena_t a = {0};
  ParseResult r = run_typer(
      "def [Vec i64] xs [[Vec i64] 1 2 3]\n"
      "def n [first $xs]", &a);
  AstNode* fst = find_cmd(r.nodes[1], "first");
  ASSERT_NOT_NULL(fst);
  ASSERT_TYPE(fst, TYPE_I64);
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

/* --- [Stream T] generator-return annotation ---------------------- */

static void test_proc_stream_return_compound(void) {
  current_test = "proc_stream_return_compound";
  arena_t a = {0};
  /* proc with [Stream i64] return type — call returns TYPE_STREAM with
   * i64 element idx; for-loop binding narrows to i64. Mirrors the
   * [Future T] roundtrip — element idx encoded via JACL_SCALAR_TYPE_IDX. */
  ParseResult r = run_typer(
      "proc gen {} [Stream i64] { yield 1 }\n"
      "def s [gen]", &a);
  AstNode* call = find_cmd(r.nodes[1], "gen");
  ASSERT_NOT_NULL(call);
  ASSERT_TYPE(call, TYPE_STREAM);
  if (call) {
    uint32_t want = JACL_SCALAR_TYPE_IDX(TYPE_I64);
    if (call->inferred_struct_idx != want) {
      fprintf(stderr, "  FAIL %s: stream elem idx expected %u got %u\n",
              current_test, want, call->inferred_struct_idx);
      failures++;
    } else { passes++; }
  }
  arena_destroy(&a);
}

static void test_range_stream_elem_i64(void) {
  current_test = "range_stream_elem_i64";
  arena_t a = {0};
  /* Range operators ..< / ..= produce TYPE_STREAM with i64 element idx
   * (matches the runtime — OP_RANGE emits jacl_i64 values). */
  ParseResult r = run_typer("def s [..< 0 10]", &a);
  AstNode* rng = find_cmd(r.nodes[0], "..<");
  ASSERT_NOT_NULL(rng);
  ASSERT_TYPE(rng, TYPE_STREAM);
  if (rng) {
    uint32_t want = JACL_SCALAR_TYPE_IDX(TYPE_I64);
    if (rng->inferred_struct_idx != want) {
      fprintf(stderr, "  FAIL %s: range elem idx expected %u got %u\n",
              current_test, want, rng->inferred_struct_idx);
      failures++;
    } else { passes++; }
  }
  arena_destroy(&a);
}

static void test_lines_stream_elem_str(void) {
  current_test = "lines_stream_elem_str";
  arena_t a = {0};
  /* `lines` produces TYPE_STREAM with str element idx. */
  ParseResult r = run_typer("def s [lines \"a\\nb\"]", &a);
  AstNode* ln = find_cmd(r.nodes[0], "lines");
  ASSERT_NOT_NULL(ln);
  ASSERT_TYPE(ln, TYPE_STREAM);
  if (ln) {
    uint32_t want = JACL_SCALAR_TYPE_IDX(TYPE_STR);
    if (ln->inferred_struct_idx != want) {
      fprintf(stderr, "  FAIL %s: lines elem idx expected %u got %u\n",
              current_test, want, ln->inferred_struct_idx);
      failures++;
    } else { passes++; }
  }
  arena_destroy(&a);
}

static void test_unannotated_generator_stream_dyn(void) {
  current_test = "unannotated_generator_stream_dyn";
  arena_t a = {0};
  /* A generator without [Stream T] annotation returns TYPE_STREAM
   * with no element idx (UINT32_MAX) — same shape as [Stream dyn].
   * Mixed-type yields are permitted (yield is lenient when there's
   * no declared element type to check against). */
  ParseResult r = run_typer(
      "proc gen {} { yield 1; yield \"two\" }\n"
      "def s [gen]", &a);
  AstNode* call = find_cmd(r.nodes[1], "gen");
  ASSERT_NOT_NULL(call);
  ASSERT_TYPE(call, TYPE_STREAM);
  if (call) {
    if (call->inferred_struct_idx != UINT32_MAX) {
      fprintf(stderr, "  FAIL %s: stream elem idx expected sentinel got %u\n",
              current_test, call->inferred_struct_idx);
      failures++;
    } else { passes++; }
  }
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

/* --- Stage 5a: typed pointers --- */

static void test_ptr_null_typed(void) {
  current_test = "ptr_null_typed";
  arena_t a = {0};
  /* [ptr-null [Ptr i32]] types as TYPE_PTR with i32 scalar pointee. */
  ParseResult r = run_typer("def p [ptr-null [Ptr i32]]", &a);
  AstNode* pn = find_cmd(r.nodes[0], "ptr-null");
  ASSERT_NOT_NULL(pn);
  ASSERT_TYPE(pn, TYPE_PTR);
  if (pn) {
    uint32_t want = JACL_SCALAR_TYPE_IDX(TYPE_I32);
    if (pn->inferred_struct_idx != want) {
      fprintf(stderr, "  FAIL %s: pointee idx expected %u got %u\n",
              current_test, want, pn->inferred_struct_idx);
      failures++;
    } else { passes++; }
  }
  arena_destroy(&a);
}

static void test_ptr_null_struct_pointee(void) {
  current_test = "ptr_null_struct_pointee";
  arena_t a = {0};
  ParseResult r = run_typer(
      "struct Point {i32 x i32 y}\n"
      "def p [ptr-null [Ptr Point]]", &a);
  AstNode* pn = find_cmd(r.nodes[1], "ptr-null");
  ASSERT_NOT_NULL(pn);
  ASSERT_TYPE(pn, TYPE_PTR);
  if (pn) {
    if (JACL_IS_SCALAR_TYPE_IDX(pn->inferred_struct_idx)) {
      fprintf(stderr, "  FAIL %s: pointee should be struct idx, got scalar sentinel %u\n",
              current_test, pn->inferred_struct_idx);
      failures++;
    } else { passes++; }
  }
  arena_destroy(&a);
}

static void test_ptr_cast_returns_typed_ptr(void) {
  current_test = "ptr_cast_returns_typed_ptr";
  arena_t a = {0};
  /* [ptr-cast [Ptr i32] $addr] types as TYPE_PTR with i32-pointee idx
   * (scalar sentinel). */
  ParseResult r = run_typer(
      "def u64 addr 0\n"
      "def p [ptr-cast [Ptr i32] $addr]", &a);
  AstNode* pc = find_cmd(r.nodes[1], "ptr-cast");
  ASSERT_NOT_NULL(pc);
  ASSERT_TYPE(pc, TYPE_PTR);
  if (pc) {
    uint32_t want = JACL_SCALAR_TYPE_IDX(TYPE_I32);
    if (pc->inferred_struct_idx != want) {
      fprintf(stderr, "  FAIL %s: pointee idx expected %u got %u\n",
              current_test, want, pc->inferred_struct_idx);
      failures++;
    } else { passes++; }
  }
  arena_destroy(&a);
}

static void test_ptr_cast_struct_pointee(void) {
  current_test = "ptr_cast_struct_pointee";
  arena_t a = {0};
  /* [ptr-cast [Ptr Point] $addr] types as TYPE_PTR with Point's struct
   * registry index as pointee. */
  ParseResult r = run_typer(
      "struct Point {i32 x i32 y}\n"
      "def u64 addr 0\n"
      "def p [ptr-cast [Ptr Point] $addr]", &a);
  AstNode* pc = find_cmd(r.nodes[2], "ptr-cast");
  ASSERT_NOT_NULL(pc);
  ASSERT_TYPE(pc, TYPE_PTR);
  if (pc) {
    /* Point is the first user struct; its idx should be a real
     * registry idx, not a scalar sentinel. */
    if (JACL_IS_SCALAR_TYPE_IDX(pc->inferred_struct_idx)) {
      fprintf(stderr, "  FAIL %s: pointee should be struct idx, got scalar sentinel %u\n",
              current_test, pc->inferred_struct_idx);
      failures++;
    } else { passes++; }
  }
  arena_destroy(&a);
}

static void test_ptr_addr_returns_u64(void) {
  current_test = "ptr_addr_returns_u64";
  arena_t a = {0};
  ParseResult r = run_typer(
      "def u64 addr 0\n"
      "def [Ptr i32] p [ptr-cast [Ptr i32] $addr]\n"
      "def back [ptr-addr $p]", &a);
  AstNode* pa = find_cmd(r.nodes[2], "ptr-addr");
  ASSERT_NOT_NULL(pa);
  ASSERT_TYPE(pa, TYPE_U64);
  arena_destroy(&a);
}

static void test_ptr_t_def_annotation(void) {
  current_test = "ptr_t_def_annotation";
  arena_t a = {0};
  /* def [Ptr Point] p ... — binding's static type is TYPE_PTR with
   * Point's pointee idx; annotation parsing routes through typer. */
  ParseResult r = run_typer(
      "struct Point {i32 x i32 y}\n"
      "def u64 addr 0\n"
      "def [Ptr Point] p [ptr-cast [Ptr Point] $addr]\n"
      "$p", &a);
  AstNode* var = find_var_ref(r.nodes[3], "p");
  ASSERT_NOT_NULL(var);
  ASSERT_TYPE(var, TYPE_PTR);
  arena_destroy(&a);
}

static void test_proc_ptr_return(void) {
  current_test = "proc_ptr_return";
  arena_t a = {0};
  /* proc with [Ptr Point] return type — call site narrows to TYPE_PTR
   * with Point's pointee idx. */
  ParseResult r = run_typer(
      "struct Point {i32 x i32 y}\n"
      "proc make {} [Ptr Point] {\n"
      "  def u64 a 0\n"
      "  ptr-cast [Ptr Point] $a\n"
      "}\n"
      "def p [make]", &a);
  /* The 'make' call returns the proc's declared type (TYPE_PTR). */
  AstNode* call = find_cmd(r.nodes[2], "make");
  ASSERT_NOT_NULL(call);
  ASSERT_TYPE(call, TYPE_PTR);
  arena_destroy(&a);
}

static void test_proc_future_return_compound(void) {
  current_test = "proc_future_return_compound";
  arena_t a = {0};
  /* proc with [Future i64] return type — call returns TYPE_FUTURE
   * with i64 element idx; await unwraps to i64. */
  ParseResult r = run_typer(
      "proc mk {} [Future i64] { spawn { 42 } }\n"
      "def f [mk]\n"
      "def x [await $f]", &a);
  AstNode* aw = find_cmd(r.nodes[2], "await");
  ASSERT_NOT_NULL(aw);
  ASSERT_TYPE(aw, TYPE_I64);
  arena_destroy(&a);
}

static void test_extern_ptr_return_call(void) {
  current_test = "extern_ptr_return_call";
  arena_t a = {0};
  /* extern declaration registers a typed signature with the typer.
   * Subsequent calls narrow to the declared return type. */
  ParseResult r = run_typer(
      "struct Point {i32 x i32 y}\n"
      "extern get_pt {} [Ptr Point]\n"
      "def p [get_pt]", &a);
  AstNode* call = find_cmd(r.nodes[2], "get_pt");
  ASSERT_NOT_NULL(call);
  ASSERT_TYPE(call, TYPE_PTR);
  arena_destroy(&a);
}

static void test_extern_with_params(void) {
  current_test = "extern_with_params";
  arena_t a = {0};
  ParseResult r = run_typer(
      "extern add_one {u64 x} u64\n"
      "def r [add_one 41]", &a);
  AstNode* call = find_cmd(r.nodes[1], "add_one");
  ASSERT_NOT_NULL(call);
  ASSERT_TYPE(call, TYPE_U64);
  arena_destroy(&a);
}

static void test_extern_buf_to_ptr_decay(void) {
  /* [Buf N T] passed to a [Ptr T] parameter at an extern call site
   * type-checks (typer accepts the implicit decay). The compiler
   * emits OP_BUF_ADDR_LOCAL for the arg. See BUFFER_DESIGN.md M3. */
  current_test = "extern_buf_to_ptr_decay";
  arena_t a = {0};
  ParseResult r = run_typer(
      "extern sys_read {i32 fd [Ptr u8] dst u64 len} i32\n"
      "def [Buf 256 u8] buf\n"
      "def r [sys_read 0 $buf 256]", &a);
  AstNode* call = find_cmd(r.nodes[2], "sys_read");
  ASSERT_NOT_NULL(call);
  ASSERT_TYPE(call, TYPE_I32);
  arena_destroy(&a);
}

/* --- Stage 5b: deref + field auto-deref --- */

static void test_ptr_arrow_field_narrows(void) {
  current_test = "ptr_arrow_field_narrows";
  arena_t a = {0};
  /* $p->x where $p is [Ptr Point] narrows to the field's type (i32). */
  ParseResult r = run_typer(
      "struct Point {i32 x i32 y}\n"
      "def u64 addr 0\n"
      "def [Ptr Point] p [ptr-cast [Ptr Point] $addr]\n"
      "$p->x", &a);
  AstNode* dot = find_cmd(r.nodes[3], ".");
  ASSERT_NOT_NULL(dot);
  ASSERT_TYPE(dot, TYPE_I32);
  arena_destroy(&a);
}

static void test_ptr_deref_scalar_narrows(void) {
  current_test = "ptr_deref_scalar_narrows";
  arena_t a = {0};
  ParseResult r = run_typer(
      "def u64 addr 0\n"
      "def [Ptr u64] p [ptr-cast [Ptr u64] $addr]\n"
      "ptr-deref $p", &a);
  AstNode* d = find_cmd(r.nodes[2], "ptr-deref");
  ASSERT_NOT_NULL(d);
  ASSERT_TYPE(d, TYPE_U64);
  arena_destroy(&a);
}

/* --- Stage 5c: pointer arithmetic --- */

static void test_ptr_offset_preserves_pointee(void) {
  current_test = "ptr_offset_preserves_pointee";
  arena_t a = {0};
  ParseResult r = run_typer(
      "struct Point {i32 x i32 y}\n"
      "def u64 addr 0\n"
      "def [Ptr Point] p [ptr-cast [Ptr Point] $addr]\n"
      "ptr-offset $p 3", &a);
  AstNode* off = find_cmd(r.nodes[3], "ptr-offset");
  ASSERT_NOT_NULL(off);
  ASSERT_TYPE(off, TYPE_PTR);
  if (off) {
    AstNode* p_ref = r.nodes[2];   /* def node for p */
    /* The result's pointee idx should match p's pointee idx. */
    if (off->inferred_struct_idx != p_ref->inferred_struct_idx) {
      /* p's binding-side struct_idx lives on the def itself; just
       * verify the offset node has a struct (non-scalar) idx. */
      if (JACL_IS_SCALAR_TYPE_IDX(off->inferred_struct_idx) ||
          off->inferred_struct_idx == UINT32_MAX) {
        fprintf(stderr, "  FAIL %s: pointee idx not preserved (got %u)\n",
                current_test, off->inferred_struct_idx);
        failures++;
        arena_destroy(&a);
        return;
      }
    }
    passes++;
  }
  arena_destroy(&a);
}

static void test_ptr_diff_returns_i64(void) {
  current_test = "ptr_diff_returns_i64";
  arena_t a = {0};
  ParseResult r = run_typer(
      "def u64 addr 0\n"
      "def [Ptr i32] p [ptr-cast [Ptr i32] $addr]\n"
      "def [Ptr i32] q [ptr-cast [Ptr i32] $addr]\n"
      "ptr-diff $p $q", &a);
  AstNode* d = find_cmd(r.nodes[3], "ptr-diff");
  ASSERT_NOT_NULL(d);
  ASSERT_TYPE(d, TYPE_I64);
  arena_destroy(&a);
}

/* --- Stage 5b extension: nested struct field access through pointers --- */

static void test_ptr_nested_struct_field_chain(void) {
  current_test = "ptr_nested_struct_field_chain";
  arena_t a = {0};
  /* $o->inner->b types as i32 (inner is struct field, b is i32).
   * Nodes: [0] struct Inner, [1] struct Outer, [2] def addr,
   * [3] def o, [4] $o->inner->b expression. */
  ParseResult r = run_typer(
      "struct Inner {i32 a i32 b}\n"
      "struct Outer {Inner inner i32 tag}\n"
      "def u64 addr 0\n"
      "def [Ptr Outer] o [ptr-cast [Ptr Outer] $addr]\n"
      "$o->inner->b", &a);
  AstNode* outer_dot = find_cmd(r.nodes[4], ".");
  ASSERT_NOT_NULL(outer_dot);
  ASSERT_TYPE(outer_dot, TYPE_I32);
  arena_destroy(&a);
}

static void test_ptr_struct_field_returns_struct(void) {
  current_test = "ptr_struct_field_returns_struct";
  arena_t a = {0};
  ParseResult r = run_typer(
      "struct Inner {i32 a i32 b}\n"
      "struct Outer {Inner inner i32 tag}\n"
      "def u64 addr 0\n"
      "def [Ptr Outer] o [ptr-cast [Ptr Outer] $addr]\n"
      "$o->inner", &a);
  AstNode* dot = find_cmd(r.nodes[4], ".");
  ASSERT_NOT_NULL(dot);
  ASSERT_TYPE(dot, TYPE_STRUCT);
  arena_destroy(&a);
}

static void test_addr_of_scalar_field_returns_ptr(void) {
  current_test = "addr_of_scalar_field_returns_ptr";
  arena_t a = {0};
  /* Nodes: [0] struct, [1] def addr, [2] def p, [3] addr expr. */
  ParseResult r = run_typer(
      "struct P {i32 x}\n"
      "def u64 addr 0\n"
      "def [Ptr P] p [ptr-cast [Ptr P] $addr]\n"
      "addr $p->x", &a);
  AstNode* a_cmd = find_cmd(r.nodes[3], "addr");
  ASSERT_NOT_NULL(a_cmd);
  ASSERT_TYPE(a_cmd, TYPE_PTR);
  arena_destroy(&a);
}

/* --- Stage 1 long-tail: box element narrowing --- */

static void test_box_of_int_typed(void) {
  current_test = "box_of_int_typed";
  arena_t a = {0};
  ParseResult r = run_typer("def b [box 42]", &a);
  AstNode* box = find_cmd(r.nodes[0], "box");
  ASSERT_NOT_NULL(box);
  ASSERT_TYPE(box, TYPE_BOX);
  if (box) {
    uint32_t want = JACL_SCALAR_TYPE_IDX(TYPE_I32);
    if (box->inferred_struct_idx != want) {
      fprintf(stderr, "  FAIL %s: box element idx expected %u got %u\n",
              current_test, want, box->inferred_struct_idx);
      failures++;
    } else { passes++; }
  }
  arena_destroy(&a);
}

static void test_deref_box_narrows_to_int(void) {
  current_test = "deref_box_narrows_to_int";
  arena_t a = {0};
  ParseResult r = run_typer(
      "def b [box 42]\n"
      "def x [deref $b]", &a);
  AstNode* d = find_cmd(r.nodes[1], "deref");
  ASSERT_NOT_NULL(d);
  ASSERT_TYPE(d, TYPE_I32);
  arena_destroy(&a);
}

static void test_deref_box_string(void) {
  current_test = "deref_box_string";
  arena_t a = {0};
  ParseResult r = run_typer(
      "def b [box \"hi\"]\n"
      "def s [deref $b]", &a);
  AstNode* d = find_cmd(r.nodes[1], "deref");
  ASSERT_NOT_NULL(d);
  ASSERT_TYPE(d, TYPE_STR);
  arena_destroy(&a);
}

static void test_swap_box_narrows_to_element(void) {
  current_test = "swap_box_narrows_to_element";
  arena_t a = {0};
  ParseResult r = run_typer(
      "def b [box 0]\n"
      "proc inc {x} { + $x 1 }\n"
      "def n [swap $b $inc]", &a);
  AstNode* s = find_cmd(r.nodes[2], "swap");
  ASSERT_NOT_NULL(s);
  ASSERT_TYPE(s, TYPE_I32);
  arena_destroy(&a);
}

static void test_deref_box_struct_narrows(void) {
  current_test = "deref_box_struct_narrows";
  arena_t a = {0};
  /* deref of a struct-element box narrows to TYPE_STRUCT with the
   * pointee's struct registry idx — paired with OP_DEREF_INLINE on
   * the codegen side so inline struct bytes flow without a heap
   * round-trip. */
  ParseResult r = run_typer(
      "struct Point {i32 x i32 y}\n"
      "def b [box [Point x 1 y 2]]\n"
      "def p [deref $b]", &a);
  AstNode* d = find_cmd(r.nodes[2], "deref");
  ASSERT_NOT_NULL(d);
  ASSERT_TYPE(d, TYPE_STRUCT);
  if (d) {
    if (JACL_IS_SCALAR_TYPE_IDX(d->inferred_struct_idx)) {
      fprintf(stderr, "  FAIL %s: pointee should be struct idx, got scalar sentinel %u\n",
              current_test, d->inferred_struct_idx);
      failures++;
    } else { passes++; }
  }
  arena_destroy(&a);
}

/* --- Stage 1 long-tail: nested proc registration --- */

static void test_nested_proc_call_narrows(void) {
  current_test = "nested_proc_call_narrows";
  arena_t a = {0};
  /* outer defines inner; calls to `inner` from outer's body should
   * narrow to inner's declared return type (i32), not dyn. */
  ParseResult r = run_typer(
      "proc outer {} {\n"
      "  proc inner {} i32 { 42 }\n"
      "  inner\n"
      "}", &a);
  AstNode* outer = r.nodes[0];
  /* The inner-call is the second AST_COMMAND with head "inner" — the
   * first is the proc definition. find_cmd does pre-order, so it
   * returns the proc-def node. We need the bare call. Walk the
   * outer's body block manually. */
  ASSERT_NOT_NULL(outer);
  if (outer && outer->type == AST_COMMAND &&
      outer->data.command.arg_count >= 3) {
    AstNode* body = outer->data.command.args[2];
    ASSERT_NOT_NULL(body);
    if (body && body->type == AST_BLOCK && body->data.block.count >= 2) {
      AstNode* call = body->data.block.commands[1];
      ASSERT_TYPE(call, TYPE_I32);
    } else {
      fprintf(stderr, "  FAIL %s: body shape unexpected\n", current_test);
      failures++;
    }
  }
  arena_destroy(&a);
}

/* --- Buffer types (M1: type plumbing only) ---------------------- */

/* `def [Buf N T] x ...` binds x as TYPE_BUF with scalar-encoded
 * element type and N stored in inferred_buf_len. The RHS will fail
 * the type-mismatch check (no buf-producing form ships in M1), but
 * the annotation must still annotate the binding correctly so the
 * var-ref to $x picks up the buf shape. See BUFFER_DESIGN.md M1. */
static void test_buf_def_annotation(void) {
  current_test = "buf_def_annotation";
  arena_t a = {0};
  ParseResult r = run_typer("def [Buf 256 u8] x 0\n$x", &a);
  AstNode* var = find_var_ref(r.nodes[1], "x");
  ASSERT_NOT_NULL(var);
  ASSERT_TYPE(var, TYPE_BUF);
  if (var) {
    if (var->inferred_buf_len != 256) {
      fprintf(stderr, "  FAIL %s: expected buf_len=256 got %u\n",
              current_test, var->inferred_buf_len);
      failures++;
    } else {
      passes++;
    }
    uint32_t want = JACL_SCALAR_TYPE_IDX(TYPE_U8);
    if (var->inferred_struct_idx != want) {
      fprintf(stderr, "  FAIL %s: expected struct_idx=%u got %u\n",
              current_test, want, var->inferred_struct_idx);
      failures++;
    } else {
      passes++;
    }
  }
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
  test_first_narrows_typed_vec_elem();
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
  test_proc_stream_return_compound();
  test_range_stream_elem_i64();
  test_lines_stream_elem_str();
  test_unannotated_generator_stream_dyn();

  test_ptr_null_typed();
  test_ptr_null_struct_pointee();
  test_ptr_cast_returns_typed_ptr();
  test_ptr_cast_struct_pointee();
  test_ptr_addr_returns_u64();
  test_ptr_t_def_annotation();
  test_proc_ptr_return();
  test_proc_future_return_compound();
  test_extern_ptr_return_call();
  test_extern_with_params();
  test_extern_buf_to_ptr_decay();
  test_ptr_arrow_field_narrows();
  test_ptr_deref_scalar_narrows();
  test_ptr_offset_preserves_pointee();
  test_ptr_diff_returns_i64();
  test_ptr_nested_struct_field_chain();
  test_ptr_struct_field_returns_struct();
  test_addr_of_scalar_field_returns_ptr();
  test_box_of_int_typed();
  test_deref_box_narrows_to_int();
  test_deref_box_string();
  test_swap_box_narrows_to_element();
  test_deref_box_struct_narrows();
  test_nested_proc_call_narrows();

  test_buf_def_annotation();

  printf("\n%d/%d passed", passes, passes + failures);
  if (failures > 0) {
    printf(" (%d FAILED)\n", failures);
    return 1;
  }
  printf("\n");
  return 0;
}
