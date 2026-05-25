/* Type-error corpus. Locks in the user-facing behavior of compile-time
 * type errors. See TYPE_SYSTEM.md for the type system overview.
 *
 * Why this exists: most type-mismatch errors fire from the typer
 * via shared formatters in src/type_error.c, with the compiler as
 * a backstop. Either pass can win the race depending on AST shape.
 * Substring matching on key terms (type names, "type error", proc /
 * struct names) catches regressions without locking in exact
 * sentence wording.
 *
 * Each case compiles a small JACL string that should fail with a
 * specific compile-time error. We assert error_count > 0 plus the
 * load-bearing substrings of the message. */

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

  /* [Vec T] constructor with wrong scalar element type. */
  RUN("typed_vec_ctor_scalar_mismatch", {
    .source = "def xs [[Vec i32] 1 \"oops\" 3]",
    .expect_substrings = { "Vec", "i32", "element", "str" },
  });

  /* [Map T] constructor with wrong scalar value type. */
  RUN("typed_map_ctor_scalar_value_mismatch", {
    .source = "def m [[Map i64] \"k\" 1 \"k2\" \"oops\"]",
    .expect_substrings = { "Map", "i64", "value", "str" },
  });

  /* [Map K V] constructor with wrong key type. */
  RUN("typed_map_kv_ctor_key_mismatch", {
    .source = "def m [[Map i32 str] \"oops\" \"v\"]",
    .expect_substrings = { "Map", "i32", "str", "key" },
  });

  /* Proc with typed return whose body tail is dyn — commitment-site rule. */
  RUN("proc_return_dyn_into_typed", {
    .source =
      "proc i32 f {x} { $x }\n"
      "[f 1]",
    .expect_substrings = { "type error", "i32", "dyn", "to i32" },
  });

  /* --- Stage 5a: typed pointer misuse rejections --- */

  /* ptr-cast first arg must be a [Ptr T] annotation. */
  RUN("ptr_cast_bad_first_arg", {
    .source = "def u64 addr 0\ndef p [ptr-cast i32 $addr]",
    .expect_substrings = { "ptr-cast", "Ptr T" },
  });

  /* ptr-null arg must be a [Ptr T] annotation. */
  RUN("ptr_null_bad_arg", {
    .source = "def p [ptr-null i32]",
    .expect_substrings = { "ptr-null", "Ptr T" },
  });

  /* ptr-cast value arg must be u64 (or dyn). i32 here is wrong. */
  RUN("ptr_cast_value_not_u64", {
    .source = "def i32 a 0\ndef p [ptr-cast [Ptr i32] $a]",
    .expect_substrings = { "ptr-cast", "u64", "i32" },
  });

  /* ptr-addr expects a pointer; passing a u64 directly is wrong. */
  RUN("ptr_addr_not_ptr", {
    .source = "def u64 a 0\ndef back [ptr-addr $a]",
    .expect_substrings = { "ptr-addr", "pointer", "u64" },
  });

  /* Assigning a pointer of the wrong pointee to a typed binding. */
  RUN("ptr_pointee_mismatch_assign", {
    .source =
      "struct Point {i32 x i32 y}\n"
      "struct Circle {i32 r}\n"
      "def u64 addr 0\n"
      "def [Ptr Point] p [ptr-cast [Ptr Point] $addr]\n"
      "def [Ptr Circle] c $p",
    .expect_substrings = { "type error", "pointer", "pointee", "c" },
  });

  /* Pointer arithmetic via + is rejected; user must use [ptr-offset]. */
  RUN("ptr_arithmetic_plus", {
    .source =
      "def u64 addr 0\n"
      "def [Ptr i32] p [ptr-cast [Ptr i32] $addr]\n"
      "+ $p 4",
    .expect_substrings = { "type error", "arithmetic", "pointer" },
  });

  /* Comparing two pointers with different pointees is rejected. */
  RUN("ptr_compare_different_pointees", {
    .source =
      "struct Point {i32 x i32 y}\n"
      "struct Circle {i32 r}\n"
      "def u64 addr 0\n"
      "def [Ptr Point] p [ptr-cast [Ptr Point] $addr]\n"
      "def [Ptr Circle] c [ptr-cast [Ptr Circle] $addr]\n"
      "if [== $p $c] { print \"eq\" }",
    .expect_substrings = { "type error", "pointers", "pointee" },
  });

  /* Stage 5b: ptr-deref on a struct pointer is a typer error —
   * struct pointees route through $p->field for field access. */
  RUN("ptr_deref_on_struct_pointer", {
    .source =
      "struct Point {i32 x i32 y}\n"
      "def u64 a 0\n"
      "def [Ptr Point] p [ptr-cast [Ptr Point] $a]\n"
      "ptr-deref $p",
    .expect_substrings = { "ptr-deref", "struct pointer", "$p->field" },
  });

  /* Stage 5b: ptr-deref on a non-pointer concrete type is rejected. */
  RUN("ptr_deref_non_pointer", {
    .source =
      "def i32 x 5\n"
      "ptr-deref $x",
    .expect_substrings = { "ptr-deref", "pointer", "i32" },
  });

  /* Stage 5b: assigning the wrong-typed value through $p->field
   * fires the existing field-mismatch formatter (typer auto-derefs
   * the pointer to its pointee struct). */
  RUN("ptr_field_set_wrong_type", {
    .source =
      "struct Point {i32 x i32 y}\n"
      "def u64 a 0\n"
      "def [Ptr Point] p [ptr-cast [Ptr Point] $a]\n"
      "set $p->x \"oops\"",
    .expect_substrings = { "type error", "x", "Point", "i32", "str" },
  });

  /* Stage 1 long-tail: deref of a typed box assigned to a wrong-type
   * binding errors at the def commitment site. */
  RUN("box_deref_wrong_typed_binding", {
    .source =
      "def b [box 42]\n"
      "def i64 x [deref $b]",
    .expect_substrings = { "type error", "i64", "i32" },
  });

  /* Stage 5b ext: addr requires a field-access chain argument. */
  RUN("addr_non_chain_argument", {
    .source = "def i32 x 5\naddr $x",
    .expect_substrings = { "addr", "field-access chain" },
  });

  /* Stage 5b ext: addr on a chain that doesn't bottom out in [Ptr T]
   * is rejected at compile time. Wrap in a proc so the local struct
   * binding is allowed. */
  RUN("addr_chain_no_ptr_base", {
    .source =
      "struct P {i32 x}\n"
      "proc f {} {\n"
      "  def p [P 1]\n"
      "  addr $p->x\n"
      "}",
    .expect_substrings = { "addr", "[Ptr T]", "base" },
  });

  /* Stage 5c: ptr-diff on two pointers with different pointees errors. */
  RUN("ptr_diff_different_pointees", {
    .source =
      "struct A {i32 x}\n"
      "struct B {i32 y}\n"
      "def u64 addr 0\n"
      "def [Ptr A] a [ptr-cast [Ptr A] $addr]\n"
      "def [Ptr B] b [ptr-cast [Ptr B] $addr]\n"
      "ptr-diff $a $b",
    .expect_substrings = { "type error", "ptr-diff", "same pointee" },
  });

  /* Stage 5c: ptr-offset with non-numeric offset is rejected. */
  RUN("ptr_offset_non_numeric_offset", {
    .source =
      "def u64 addr 0\n"
      "def [Ptr i32] p [ptr-cast [Ptr i32] $addr]\n"
      "ptr-offset $p \"oops\"",
    .expect_substrings = { "type error", "ptr-offset", "numeric", "str" },
  });

  /* extern declared with typed param: calling with the wrong arg
   * type fires the same dyn/typed barrier as a JACL proc call would. */
  RUN("extern_call_wrong_arg_type", {
    .source =
      "extern u64 add_one {u64 x}\n"
      "[add_one \"oops\"]",
    .expect_substrings = { "type error", "u64", "str" },
  });

  /* extern with [Ptr T] return: assigning the result to a different
   * pointee binding is a compile error (commitment-site rule). */
  RUN("extern_ptr_result_pointee_mismatch", {
    .source =
      "struct Point {i32 x i32 y}\n"
      "struct Circle {i32 r}\n"
      "extern [Ptr Point] make_pt {}\n"
      "def [Ptr Circle] c [make_pt]",
    .expect_substrings = { "type error", "pointer", "pointee" },
  });

  /* await on a known concrete non-future operand — typer rejects.
   * Dyn operands and typed futures still pass; this is the
   * statically-knowable bad case. */
  RUN("await_int_literal", {
    .source = "await 42",
    .expect_substrings = { "type error", "await", "future", "i32" },
  });

  RUN("await_typed_int_binding", {
    .source = "def i32 x 42\nawait $x",
    .expect_substrings = { "type error", "await", "future", "i32" },
  });

  /* --- Stage 6 M4.1: struct-element buf rejections --- */

  /* [[Buf N A] ...] constructor with element kind A but LHS uses B. */
  RUN("buf_struct_constructor_mismatch", {
    .source =
      "struct A {i32 x}\nstruct B {i32 y}\n"
      "def [Buf 2 A] xs [[Buf 2 B] [B 1]]",
    .expect_substrings = { "constructor", "struct type" },
  });

  /* --- Stage 6 M3: buf-to-ptr decay rejections --- */

  /* [Buf u8] passed to [Ptr i32] parameter — pointee mismatch. */
  RUN("buf_to_ptr_pointee_mismatch", {
    .source =
      "extern i32 read32 {[Ptr i32] dst}\n"
      "def [Buf 256 u8] b\n"
      "def r [read32 $b]",
    .expect_substrings = { "type error", "expected", "got" },
  });

  /* --- Stage 6: [Buf N T] buffer-type annotation rejections (M1) --- */

  /* Zero length rejected. */
  RUN("buf_zero_length", {
    .source = "def [Buf 0 i32] x 0",
    .expect_substrings = { "type error", "Buf", "positive" },
  });

  /* Non-literal N rejected. */
  RUN("buf_non_literal_len", {
    .source = "def i32 n 256\ndef [Buf $n i32] x 0",
    .expect_substrings = { "type error", "Buf", "positive" },
  });

  /* Unknown element type rejected (not a scalar keyword, not a
   * registered struct). M4.1 lifted the scalar-only restriction but
   * the name still has to resolve. */
  RUN("buf_unknown_elem_type", {
    .source = "def [Buf 8 Widget] b",
    .expect_substrings = { "Buf", "element type", "scalar" },
  });

  /* Ctx-field-set arrow form: typer recognizes `set $ctx->field val`
   * pre-rewrite and applies the field-type check. */
  RUN("ctx_set_arrow_type_mismatch", {
    .source =
      "ctx mut i32 level = 42\n"
      "set $ctx->level \"hello\"",
    .expect_substrings = { "type error", "level", "i32", "str" },
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
