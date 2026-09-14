/* P1.7 — builtins: arithmetic, comparisons, type ops, len/to_string, error
 * propagation. Returns 777. */
#include "jaclrt.h"

static inline int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

int run(int n) {
  (void)n;
  jacl_heap_init();
  jacl_intern_init();
  jacl_map_init();
  int ok = 1;
  char buf[32];

  /* arithmetic */
  ok &= (jaclrt_as_i32(jacl_add(jaclrt_i32(3), jaclrt_i32(4))) == 7);
  ok &= (jaclrt_as_i32(jacl_sub(jaclrt_i32(10), jaclrt_i32(4))) == 6);
  ok &= (jaclrt_as_i32(jacl_mul(jaclrt_i32(6), jaclrt_i32(7))) == 42);
  ok &= (jaclrt_as_i32(jacl_div(jaclrt_i32(20), jaclrt_i32(5))) == 4);
  ok &= (jaclrt_as_i32(jacl_mod(jaclrt_i32(20), jaclrt_i32(6))) == 2);
  ok &= (jaclrt_as_i32(jacl_neg(jaclrt_i32(5))) == -5);

  /* domain + type errors, and error propagation */
  ok &= jaclrt_is_error(jacl_div(jaclrt_i32(1), jaclrt_i32(0)));
  ok &= jaclrt_is_error(jacl_mod(jaclrt_i32(1), jaclrt_i32(0)));
  ok &= jaclrt_is_error(jacl_add(jaclrt_i32(1), jaclrt_bool(true)));
  ok &= jaclrt_is_error(jacl_add(jacl_div(jaclrt_i32(1), jaclrt_i32(0)), jaclrt_i32(5)));

  /* comparisons */
  ok &= jaclrt_as_bool(jacl_lt(jaclrt_i32(3), jaclrt_i32(5)));
  ok &= !jaclrt_as_bool(jacl_lt(jaclrt_i32(5), jaclrt_i32(5)));
  ok &= jaclrt_as_bool(jacl_le(jaclrt_i32(5), jaclrt_i32(5)));
  ok &= jaclrt_as_bool(jacl_gt(jaclrt_i32(9), jaclrt_i32(2)));
  ok &= jaclrt_as_bool(jacl_ge(jaclrt_i32(2), jaclrt_i32(2)));
  ok &= jaclrt_as_bool(jacl_eq(jaclrt_i32(7), jaclrt_i32(7)));
  ok &= !jaclrt_as_bool(jacl_eq(jaclrt_i32(7), jaclrt_i32(8)));
  ok &= jaclrt_as_bool(jacl_ne(jaclrt_i32(7), jaclrt_i32(8)));
  ok &= jaclrt_as_bool(jacl_eq(jacl_str_new("hi", 2), jacl_str_new("hi", 2)));  /* inline -> same bits */

  /* #98: `==`/`!=` settle nil/bool/i32 pairs inline — same answers as the generic walk,
   * including across numeric representations (i32 vs heap i64/f64) and with flags set. */
  ok &= jaclrt_as_bool(jacl_eq(jaclrt_nil(), jaclrt_nil()));
  ok &= !jaclrt_as_bool(jacl_ne(jaclrt_nil(), jaclrt_nil()));
  ok &= jaclrt_as_bool(jacl_eq(jaclrt_bool(true), jaclrt_bool(true)));
  ok &= !jaclrt_as_bool(jacl_eq(jaclrt_bool(true), jaclrt_bool(false)));
  ok &= jaclrt_as_bool(jacl_ne(jaclrt_bool(true), jaclrt_bool(false)));
  ok &= !jaclrt_as_bool(jacl_eq(jaclrt_nil(), jaclrt_bool(false)));   /* different tags */
  ok &= !jaclrt_as_bool(jacl_eq(jaclrt_nil(), jaclrt_i32(0)));
  ok &= jaclrt_as_bool(jacl_eq(jaclrt_i32(-7), jacl_wide_new(0x0E, -7)));  /* i32 == heap i64 */
  ok &= jaclrt_as_bool(jacl_ne(jaclrt_i32(-7), jacl_wide_new(0x0E, -8)));
  {
    JaclVal ti = jaclrt_i32(7) | JACL_FLAG_TAINTED;
    ok &= jaclrt_as_bool(jacl_eq(ti, jaclrt_i32(7)));
    ok &= ((jacl_eq(ti, jaclrt_i32(7)) & JACL_FLAG_TAINTED) != 0);   /* flags still propagate */
    ok &= ((jacl_ne(ti, jaclrt_i32(8)) & JACL_FLAG_TAINTED) != 0);
  }

  /* jacl #95 — taint and secret belong to *dynamic* values. Coercing a flagged value to any
   * static type is an **error**, not a flag drop: the static representation has no bits to
   * carry the flag, so allowing it would launder the value into a plain number. The refusal
   * keeps the flags, so the fact is not lost with the cast.
   *
   * This is the only place the rule can be exercised at all — taint and secret have no
   * source-level producer yet (NOT_IMPLEMENTED.md), so a `.jacl` corpus case cannot build a
   * flagged value. The rule is enforced at the crossings so it holds the day one lands. */
  {
    JaclVal ti = jaclrt_i32(7) | JACL_FLAG_TAINTED;
    JaclVal si = jaclrt_i32(7) | JACL_FLAG_SECRET;
    JaclVal i32n = jacl_str_new("i32", 3), i64n = jacl_str_new("i64", 3);

    ok &= jaclrt_is_error(jacl_to_cast(ti, i32n));
    ok &= jaclrt_is_error(jacl_to_cast(ti, i64n));
    ok &= jaclrt_is_error(jacl_to_cast(si, i32n));
    ok &= ((jacl_to_cast(ti, i32n) & JACL_FLAG_TAINTED) != 0);   /* the refusal keeps the flag */
    ok &= ((jacl_to_cast(si, i32n) & JACL_FLAG_SECRET) != 0);

    /* the declared-binding widening (`def i64 x V`) is the same crossing */
    ok &= jaclrt_is_error(jacl_widen_to(ti, jaclrt_i32(0x0E)));
    ok &= jaclrt_is_error(jacl_widen_to(si, jaclrt_i32(0x10)));

    /* and the raw-word crossing never hands back a laundered payload */
    ok &= (jacl_i64_unbox(ti) == 0);
    ok &= (jacl_i64_unbox(si) == 0);
    ok &= (jacl_i64_unbox(jaclrt_i32(7)) == 7);                  /* unflagged still works */

    /* an unflagged value is unaffected by any of it */
    ok &= (jaclrt_as_i32(jacl_to_cast(jaclrt_i32(7), i32n)) == 7);
  }

  /* #98: `box` snapshots without a deep copy for immediates, and still copies the two
   * mutable aggregates — a boxed arr does not observe later mutation of the source. */
  ok &= (jaclrt_as_i32(jacl_box_get(jacl_box_new(jaclrt_i32(5)))) == 5);
  ok &= jaclrt_is_nil(jacl_box_get(jacl_box_new(jaclrt_nil())));
  {
    JaclVal src = jacl_arr_new();
    src = jacl_arr_push(src, jaclrt_i32(1));
    JaclVal bx = jacl_box_new(src);
    src = jacl_arr_push(src, jaclrt_i32(2));
    ok &= (jaclrt_as_i32(jacl_len(jacl_box_get(bx))) == 1);   /* snapshot, not the live arr */
    ok &= (jaclrt_as_i32(jacl_len(src)) == 2);
  }

  /* logic */
  ok &= jaclrt_as_bool(jacl_not(jaclrt_bool(false)));
  ok &= !jaclrt_as_bool(jacl_not(jaclrt_bool(true)));

  /* typeof */
  jacl_str_bytes(jacl_typeof(jaclrt_i32(1)), buf, sizeof buf);     ok &= seq(buf, "i32");
  jacl_str_bytes(jacl_typeof(jaclrt_bool(1)), buf, sizeof buf);      ok &= seq(buf, "bool");
  jacl_str_bytes(jacl_typeof(jaclrt_nil()), buf, sizeof buf);      ok &= seq(buf, "nil");
  jacl_str_bytes(jacl_typeof(jacl_str_new("x", 1)), buf, sizeof buf); ok &= seq(buf, "string");
  jacl_str_bytes(jacl_typeof(jacl_vec_empty()), buf, sizeof buf);  ok &= seq(buf, "vec");
  jacl_str_bytes(jacl_typeof(jacl_map_empty()), buf, sizeof buf);  ok &= seq(buf, "map");

  /* polymorphic len */
  ok &= (jaclrt_as_i32(jacl_len(jacl_str_new("hello, world", 12))) == 12);
  JaclVal v = jacl_vec_empty();
  v = jacl_vec_push(v, jaclrt_i32(1));
  v = jacl_vec_push(v, jaclrt_i32(2));
  ok &= (jaclrt_as_i32(jacl_len(v)) == 2);
  JaclVal m = jacl_map_set(jacl_map_empty(), jaclrt_i32(1), jaclrt_i32(9));
  ok &= (jaclrt_as_i32(jacl_len(m)) == 1);
  ok &= jaclrt_is_error(jacl_len(jaclrt_i32(5)));   /* i32 not lengthable */

  /* to_string */
  jacl_str_bytes(jacl_to_string(jaclrt_i32(-42)), buf, sizeof buf); ok &= seq(buf, "-42");
  jacl_str_bytes(jacl_to_string(jaclrt_i32(0)), buf, sizeof buf);   ok &= seq(buf, "0");
  jacl_str_bytes(jacl_to_string(jaclrt_i32(123456)), buf, sizeof buf); ok &= seq(buf, "123456");
  jacl_str_bytes(jacl_to_string(jaclrt_bool(1)), buf, sizeof buf);    ok &= seq(buf, "true");
  jacl_str_bytes(jacl_to_string(jaclrt_nil()), buf, sizeof buf);    ok &= seq(buf, "nil");

  /* string concat */
  JaclVal cc = jacl_str_concat(jacl_str_new("hello, ", 7), jacl_str_new("world!", 6));
  ok &= (jacl_str_len(cc) == 13);
  jacl_str_bytes(cc, buf, sizeof buf); ok &= seq(buf, "hello, world!");
  JaclVal cc2 = jacl_str_concat(jacl_str_new("ab", 2), jacl_str_new("cd", 2));  /* inline result */
  ok &= !jaclrt_is_heap(cc2) && (jacl_str_len(cc2) == 4);
  jacl_str_bytes(cc2, buf, sizeof buf); ok &= seq(buf, "abcd");

  return ok ? 777 : -1;
}

#include "jaclrt.c"
