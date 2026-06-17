/* P1.4 — strings (non-GC): inline / heap / interning / equality. Returns 1414. */
#include "jaclrt.h"

/* svm-llvm has no strcmp; hand-roll (static inline so run() stays func 0). */
static inline int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

int run(int n) {
  (void)n;
  jacl_heap_init();
  jacl_intern_init();
  int ok = 1;
  char buf[64];

  /* inline (<=7 bytes) */
  JaclVal hi = jacl_str_new("hi", 2);
  ok &= jaclrt_is_inline_string(hi) && !jaclrt_is_heap(hi);
  ok &= (jacl_str_len(hi) == 2);
  jacl_str_bytes(hi, buf, sizeof buf); ok &= (streq(buf, "hi"));

  JaclVal seven = jacl_str_new("abcdefg", 7);            /* 7-byte boundary stays inline */
  ok &= jaclrt_is_inline_string(seven) && (jacl_str_len(seven) == 7);

  /* heap (>7 bytes) */
  JaclVal hw = jacl_str_new("hello, world!", 13);
  ok &= jaclrt_is_heap(hw) && (jaclrt_type_index(hw) == 0x05) && (jacl_str_len(hw) == 13);
  jacl_str_bytes(hw, buf, sizeof buf); ok &= (streq(buf, "hello, world!"));

  /* interning: equal heap strings canonicalise to one object */
  JaclVal a = jacl_str_intern("a-fairly-long-interned-key", 26);
  JaclVal b = jacl_str_intern("a-fairly-long-interned-key", 26);
  ok &= (a == b);
  JaclVal c = jacl_str_new("a-fairly-long-interned-key", 26);   /* fresh, non-interned */
  ok &= (c != a) && jacl_str_eq(c, a);

  /* interning an inline string yields the same value (canonical by bits) */
  ok &= (jacl_str_intern("hi", 2) == hi);

  /* equality */
  ok &= jacl_str_eq(jacl_str_new("xy", 2), jacl_str_new("xy", 2));
  ok &= !jacl_str_eq(hw, a);

  return ok ? 1414 : -1;
}

#include "jaclrt.c"
