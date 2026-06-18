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
