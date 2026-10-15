/* closure.c — P2.6 closure + cell objects (the runtime side of codegen closures).
 *
 * A closure is a JOBJ_FUNC whose payload is `[ i64 fnref ][ JaclVal upval0 ] …`:
 * `fnref` is the (link-relocated) TEMEN function index obtained from `ref.func`, and
 * the upvals are the captured environment. The closure function has signature
 * `(i64 sp, i64 self, args…) -> i64` and reads its upvals back via jacl_closure_upval.
 *
 * A cell is a JOBJ_NODE holding a single JaclVal — a shared mutable box so a captured
 * `mut` variable observed through a closure and through the defining scope stay in
 * sync. Both are traced (a closure as a JOBJ_FUNC, which a collection can tell from other nodes by
 * its first word, #215; a cell as a JOBJ_NODE), so the conservative GC traces the captured values.
 *
 * fnref / index / count flow as raw i64 integers; captured values flow as JaclVals. */
#include "jaclrt.h"

JaclVal jacl_closure_new(JaclVal fnref, JaclVal nupvals) {
  uint32_t n = (uint32_t)(uint64_t)nupvals;
  JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_FUNC, (uint32_t)(sizeof(int64_t) * (1 + n)));
  int64_t *slots = (int64_t *)jacl_obj_payload(o);
  slots[0] = (int64_t)fnref;
  return jaclrt_from_ptr(JACL_TAG_CLOSURE, o);
}

/* #215: a REPL unit's closure function calls this as it returns, passing its closure, which so
 * stays live in its frame while it runs: the REPL frees a unit's slots only once no closure of it
 * is live (codegen/selfhost/repl/repl_guest.c). */
__attribute__((noinline)) void jacl_closure_keep(JaclVal c) { (void)c; }

JaclVal jacl_closure_set(JaclVal c, JaclVal i, JaclVal v) {
  int64_t *slots = (int64_t *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(c));
  slots[1 + (uint32_t)(uint64_t)i] = (int64_t)v;
  return c;
}

JaclVal jacl_closure_fn(JaclVal c) {
  int64_t *slots = (int64_t *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(c));
  return (JaclVal)slots[0];
}

JaclVal jacl_closure_upval(JaclVal c, JaclVal i) {
  int64_t *slots = (int64_t *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(c));
  return (JaclVal)slots[1 + (uint32_t)(uint64_t)i];
}

JaclVal jacl_cell_new(JaclVal v) {
  JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_NODE, (uint32_t)sizeof(int64_t));
  *(int64_t *)jacl_obj_payload(o) = (int64_t)v;
  return jaclrt_from_ptr(JACL_TAG_CELL, o);
}

JaclVal jacl_cell_get(JaclVal c) {
  return (JaclVal) * (int64_t *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(c));
}

JaclVal jacl_cell_set(JaclVal c, JaclVal v) {
  *(int64_t *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(c)) = (int64_t)v;
  return v;
}
