/* jacl #175 — the mark stack overflows without losing anything reachable.
 *
 * One live node points at N child nodes, each holding the only pointer to a leaf. Draining the
 * fan pushes all N children before popping any, and N is past the mark stack's capacity. A
 * dropped push used to leave its child MARKED but never traced — so no later push would queue it
 * again — and the child's leaf, reachable only through it, was swept while live. Every leaf must
 * survive with its value. `run` returns 175 iff they do. */
#include "jaclrt.h"

#define N 70000   /* > MARK_STACK_CAP (65536) */

static JaclObj* build(void);
static int      check(JaclObj *fan);

int run(int n) {
  (void)n;
  jacl_heap_init();
  JaclObj *fan = build();
  long before = jacl_live_count();   /* fan + N children + N leaves */
  jacl_gc_collect();
  long after = jacl_live_count();
  if (before != 1 + 2L * N) return -1;
  if (after != before) return -2;     /* something reachable was swept */
  if (!check(fan)) return -3;
  return 175;
}

__attribute__((noinline)) static JaclObj* build(void) {
  JaclObj *fan = (JaclObj *)jacl_alloc(JOBJ_NODE, N * 8);
  for (long i = 0; i < N; i++) {
    JaclObj *leaf = (JaclObj *)jacl_alloc(JOBJ_BLOB, 8);
    *(long *)jacl_obj_payload(leaf) = i * 7 + 1;
    JaclObj *child = (JaclObj *)jacl_alloc(JOBJ_NODE, 8);
    *(JaclObj **)jacl_obj_payload(child) = leaf;
    ((JaclObj **)jacl_obj_payload(fan))[i] = child;
  }
  return fan;
}
__attribute__((noinline)) static int check(JaclObj *fan) {
  for (long i = 0; i < N; i++) {
    JaclObj *child = ((JaclObj **)jacl_obj_payload(fan))[i];
    JaclObj *leaf = *(JaclObj **)jacl_obj_payload(child);
    if (*(long *)jacl_obj_payload(leaf) != i * 7 + 1) return 0;
  }
  return 1;
}

#include "jaclrt.c"
