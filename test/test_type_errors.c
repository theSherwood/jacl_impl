/* Type-error corpus. Locks in the user-facing behavior of compile-time
 * type errors that the compiler emits today. Stage 0 of the
 * static-typing migration; see STATIC_TYPING_PLAN.md.
 *
 * Why this exists: Stage 1 moves the type-check logic for typed
 * bindings, struct fields, ctx fields, etc. from compiler.c into
 * the typer. The risk is silently changing or losing user-facing
 * error messages. This corpus asserts on substrings of the error
 * messages so a regression fails the test rather than passing
 * silently.
 *
 * Each case compiles a small JACL string that should fail with a
 * specific compile-time error. We assert error_count > 0 plus
 * substrings of the message. Substring matching (not exact) lets
 * Stage 1 reword messages slightly without breaking the corpus,
 * as long as the key terms (types, names) stay. */

#include "../src/jacl.h"
#include "../lib/arena/arena.h"
#include <stdio.h>
#include <string.h>

static int passes = 0;
static int failures = 0;
static const char* current_test = "(none)";

/* One-shot compile. Caller owns arena and heap; we just thread state.
 * Returns the CompileResult so the test can inspect error_count and
 * error_message. */
static CompileResult try_compile(const char* src, arena_t* arena,
                                 ThreadHeap* heap) {
  LexResult lex = lexer_lex(src, arena);
  ParseResult parse = parser_parse(lex, arena);
  JaclInternTable intern;
  intern_table_init(&intern, arena);
  return compiler_compile(parse, arena, &intern, heap, NULL, NULL, JACL_NIL);
}

/* Setup helper: build arena + heap + pool, call the inner test
 * function with them, and tear down. Centralizes the boilerplate
 * so each test is just (source, expected substrings). */
typedef struct {
  const char* source;
  const char* expect_substrings[4];  /* NULL-terminated, up to 4 */
} ErrorCase;

static void run_error_case(const ErrorCase* tc) {
  arena_t arena = {0};
  BlockPool pool;  gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool);
  ThreadHeap* prev = gc__current_heap;
  gc__current_heap = &heap;

  CompileResult cr = try_compile(tc->source, &arena, &heap);

  if (cr.error_count == 0) {
    fprintf(stderr,
      "  FAIL %s: expected compile error, got none\n"
      "    src: %s\n",
      current_test, tc->source);
    failures++;
  } else if (cr.error_message == NULL) {
    fprintf(stderr,
      "  FAIL %s: error_count=%u but no error_message\n"
      "    src: %s\n",
      current_test, cr.error_count, tc->source);
    failures++;
  } else {
    bool all_found = true;
    for (int i = 0; i < 4 && tc->expect_substrings[i]; i++) {
      if (!strstr(cr.error_message, tc->expect_substrings[i])) {
        fprintf(stderr,
          "  FAIL %s: error message missing substring %s\n"
          "    src:  %s\n"
          "    got:  %s\n",
          current_test, tc->expect_substrings[i],
          tc->source, cr.error_message);
        all_found = false;
        break;
      }
    }
    if (all_found) passes++;
    else failures++;
  }

  gc__current_heap = prev;
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
}

#define RUN(name, ...) do {                                                \
  current_test = name;                                                      \
  ErrorCase _tc = __VA_ARGS__;                                              \
  run_error_case(&_tc);                                                     \
} while (0)

/* --- Test cases --------------------------------------------------- */

static void run_all(void) {

  /* Typed binding: assigning wrong concrete type. */
  RUN("def_i32_with_str", {
    .source = "def i32 x \"hello\"",
    .expect_substrings = { "type error", "i32", "str" },
  });

  RUN("def_f64_with_str", {
    .source = "def f64 x \"hi\"",
    .expect_substrings = { "type error", "f64", "str" },
  });

  /* Set on typed binding: wrong type. */
  RUN("set_i32_with_str", {
    .source = "mut i32 x 1\nset x \"oops\"",
    .expect_substrings = { "type error", "i32", "str" },
  });

  /* Negation of unsigned 64-bit — explicit reject. */
  RUN("negate_u64", {
    .source = "def u64 x 1\ndef y (- $x)",
    .expect_substrings = { "negate", "u64" },
  });

  /* Comparing different struct types. Requires $ on operands so the
   * binary op sees var-refs (typed bindings) rather than bare-name
   * commands. */
  RUN("struct_eq_different_types", {
    .source =
      "struct A {i32 x}\n"
      "struct B {i32 y}\n"
      "proc test {} {\n"
      "  def a [A 1]\n"
      "  def b [B 2]\n"
      "  if ($a == $b) { print \"eq\" }\n"
      "}",
    .expect_substrings = { "type error", "struct" },
  });

  /* Struct field assignment with wrong type (typed mutable field).
   * Surface syntax for field set is `. $struct field new_value`. */
  RUN("struct_field_set_wrong_type", {
    .source =
      "struct P {mut i32 x}\n"
      "proc test {} {\n"
      "  def p [P 1]\n"
      "  . $p x \"bad\"\n"
      "}",
    .expect_substrings = { "type error", "i32", "str" },
  });

  /* Struct constructor with wrong arg type (positional). */
  RUN("struct_ctor_wrong_arg_type", {
    .source =
      "struct P {i32 x}\n"
      "proc test {} {\n"
      "  def p [P \"oops\"]\n"
      "}",
    .expect_substrings = { "type error", "i32", "str" },
  });

  /* Typed-vec push with wrong element type. */
  RUN("typed_vec_push_wrong_elem", {
    .source =
      "def [Vec i32] xs [vec 1 2 3]\n"
      "def ys [vec-push $xs \"oops\"]",
    .expect_substrings = { "type" },
  });

  /* Typed-vec concat with mismatched element types. */
  RUN("typed_vec_concat_mismatched", {
    .source =
      "def [Vec i32] a [vec 1 2 3]\n"
      "def [Vec f32] b [vec 1.0 2.0]\n"
      "def c [vec-concat $a $b]",
    .expect_substrings = { "type" },
  });

  /* Proc declared return type doesn't match body. */
  RUN("proc_return_type_mismatch", {
    .source =
      "proc i32 f {i32 x} { \"not an int\" }\n"
      "[f 1]",
    .expect_substrings = { "return", "i32", "str" },
  });

  /* Calling a typed proc with wrong-typed argument. */
  RUN("proc_call_wrong_arg_type", {
    .source =
      "proc f {i32 x} { print $x }\n"
      "[f \"oops\"]",
    .expect_substrings = { "type error", "i32", "str" },
  });
}

/* --- Driver ------------------------------------------------------- */

int main(void) {
  printf("=== test_type_errors ===\n");
  run_all();
  printf("\n%d/%d passed", passes, passes + failures);
  if (failures > 0) {
    printf(" (%d FAILED)\n", failures);
    return 1;
  }
  printf("\n");
  return 0;
}
