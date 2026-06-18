/* P1.2/P1.3 — heap allocator + conservative non-moving mark-sweep.
 *
 * Builds a reachable graph (root -> {c1 -> leaf, c2}) where `leaf` is reachable
 * ONLY by tracing c1's payload, plus n unreachable objects of varied sizes. After
 * collection: the reachable graph survives with intact data and exactly n garbage
 * objects are reclaimed; a second cycle proves swept cells are reused. `run`
 * returns 529 iff every invariant holds, independent of n.
 *
 * `run` is defined first (module function 0); helpers + the runtime impl follow. */
#include "jaclrt.h"

typedef struct { long value; JaclObj *a; JaclObj *b; } Node;

static JaclObj* mkblob(long v);
static JaclObj* mknode(long v, JaclObj *a, JaclObj *b);
static JaclObj* mknode_with_leaf(long v, long leafval);
static void     spawn_garbage(long n);
static long     nodeval(JaclObj *o);
static long     blobval(JaclObj *o);
static JaclObj* child_a(JaclObj *o);
static JaclObj* child_b(JaclObj *o);

int run(int n) {
  jacl_heap_init();

  JaclObj *c1 = mknode_with_leaf(20, 99);   /* c1 -> leaf(99); leaf is trace-only */
  JaclObj *c2 = mknode(30, 0, 0);
  JaclObj *root = mknode(10, c1, c2);        /* held across collection (a gc.roots root) */

  spawn_garbage(n);                          /* n unreachable objects, varied sizes */

  long before = jacl_live_count();           /* root,c1,c2,leaf + n = n + 4 */
  long swept1 = jacl_gc_collect();
  long after1 = jacl_live_count();

  int ok = (before == n + 4) && (swept1 == n) && (after1 == 4)
        && (nodeval(root) == 10)
        && (child_a(root) != 0) && (nodeval(child_a(root)) == 20)
        && (child_b(root) != 0) && (nodeval(child_b(root)) == 30)
        && (child_a(child_a(root)) != 0)                 /* leaf, reached via c1 only */
        && (blobval(child_a(child_a(root))) == 99);

  /* round 2: reuse swept cells, survive a second collection */
  spawn_garbage(n);
  long mid = jacl_live_count();              /* n + 4 again -> required slot reuse */
  long swept2 = jacl_gc_collect();
  long after2 = jacl_live_count();
  ok = ok && (mid == n + 4) && (swept2 == n) && (after2 == 4)
          && (nodeval(root) == 10) && (nodeval(child_a(root)) == 20)
          && (blobval(child_a(child_a(root))) == 99);

  return ok ? (int)(after2 * 100 + nodeval(root) + nodeval(child_a(root)) + blobval(child_a(child_a(root)))) : -1;
  /* 4*100 + 10 + 20 + 99 = 529 */
}

static JaclObj* mkblob(long v) {
  JaclObj *o = (JaclObj*)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));
  *(long*)jacl_obj_payload(o) = v;
  return o;
}
static JaclObj* mknode(long v, JaclObj *a, JaclObj *b) {
  JaclObj *o = (JaclObj*)jacl_alloc(JOBJ_NODE, (uint32_t)sizeof(Node));
  Node *nd = (Node*)jacl_obj_payload(o);
  nd->value = v; nd->a = a; nd->b = b;
  return o;
}
/* leaf created here, referenced ONLY via the returned node's payload -> trace-only */
static JaclObj* mknode_with_leaf(long v, long leafval) {
  JaclObj *lf = mkblob(leafval);
  return mknode(v, lf, 0);
}
static void spawn_garbage(long n) {
  for (long i = 0; i < n; i++) {
    if (i & 1) { JaclObj *g = mkblob(1000 + i); (void)g; }
    else       { JaclObj *g = mknode(2000 + i, 0, 0); (void)g; }
  }
}
static long     nodeval(JaclObj *o) { return ((Node*)jacl_obj_payload(o))->value; }
static long     blobval(JaclObj *o) { return *(long*)jacl_obj_payload(o); }
static JaclObj* child_a(JaclObj *o) { return ((Node*)jacl_obj_payload(o))->a; }
static JaclObj* child_b(JaclObj *o) { return ((Node*)jacl_obj_payload(o))->b; }

#include "jaclrt.c"   /* runtime impl last, so run() is module function 0 */
