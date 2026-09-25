/* jacl #159 — out of memory stops the guest in the allocator, rather than handing back NULL
 * for the caller to write through. Keeps a linked list of 64-byte nodes live until the heap
 * cannot hold another; `run` never returns. */
#include "jaclrt.h"

static long grow_forever(void);

int run(int n) {
  (void)n;
  jacl_heap_init();
  return (int)grow_forever();
}

__attribute__((noinline)) static long grow_forever(void) {
  JaclObj *head = 0;
  for (long i = 0;; i++) {
    JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_NODE, 48);
    long *p = (long *)jacl_obj_payload(o);
    p[0] = (long)head;   /* traced: every node stays reachable from `head` */
    p[1] = i;
    head = o;
  }
}

#include "jaclrt.c"
