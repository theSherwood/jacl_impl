/* P1.4 — string/GC interaction: an interned string survives collection via the
 * intern table (a runtime root), while non-interned heap strings are reclaimed.
 * Returns 707. */
#include "jaclrt.h"

static void intern_survivor(void);
static void make_garbage(long n);

int run(int n) {
  jacl_heap_init();
  jacl_intern_init();

  intern_survivor();              /* interns one heap string; handle discarded (table-only) */
  make_garbage(n);               /* n non-interned heap strings, dropped */

  long before = jacl_live_count();   /* survivor + n */
  jacl_gc_collect();
  long after = jacl_live_count();    /* survivor only (interned ⇒ rooted by the table) */

  int ok = (before == n + 1) && (after == 1);

  /* re-interning the same key must NOT allocate — it was kept alive in the table */
  jacl_str_intern("the-survivor-key-string", 23);
  ok = ok && (jacl_live_count() == 1);

  return ok ? 707 : -1;
}

__attribute__((noinline)) static void intern_survivor(void) {
  jacl_str_intern("the-survivor-key-string", 23);
}
__attribute__((noinline)) static void make_garbage(long n) {
  for (long i = 0; i < n; i++) {
    char b[24];
    for (int k = 0; k < 20; k++) b[k] = (char)('a' + ((i + k) % 26));
    b[20] = '\0';
    JaclVal s = jacl_str_new(b, 20);   /* heap (>7), non-interned, dropped */
    (void)s;
  }
}

#include "jaclrt.c"
