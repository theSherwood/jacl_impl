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

/* A closure whose function takes no arguments after (sp, self): a spawned block's. */
JaclVal jacl_closure_new(JaclVal fnref, JaclVal nupvals) {
  return jacl_closure_new_shape(fnref, nupvals, 0);
}

/* A closure, with the shape of its function in the header's spare half-word: the parameters it
 * takes after (sp, self), and with JACL_CLOSURE_REST, that the last collects the rest. Such a
 * function takes (sp, self, args), every argument packed in one vector, since a caller cannot
 * know how many are fixed (#280). */
JaclVal jacl_closure_new_shape(JaclVal fnref, JaclVal nupvals, JaclVal shape) {
  uint32_t n = (uint32_t)(uint64_t)nupvals;
  JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_FUNC, (uint32_t)(sizeof(int64_t) * (1 + n)));
  o->_rsv = (uint16_t)(uint64_t)shape;
  int64_t *slots = (int64_t *)jacl_obj_payload(o);
  slots[0] = (int64_t)fnref;
  return jaclrt_from_ptr(JACL_TAG_CLOSURE, o);
}

/* The function to call closure `c` with `argc` arguments, by its shape: its index when it takes
 * exactly argc; the index with bit 32 set when its last parameter collects the rest and argc
 * covers the fixed ones, to call with the arguments packed in a vector; -1 when it takes neither,
 * which used to trap the call (#281). */
JaclVal jacl_closure_fn_for(JaclVal c, JaclVal argc) {
  JaclObj *o = (JaclObj *)jaclrt_as_ptr(c);
  uint64_t fn = (uint64_t)((int64_t *)jacl_obj_payload(o))[0];
  uint32_t shape = o->_rsv, n = (uint32_t)(uint64_t)argc;
  if (!(shape & JACL_CLOSURE_REST)) return shape == n ? (JaclVal)fn : (JaclVal)-1;
  return n >= (shape & ~JACL_CLOSURE_REST) ? (JaclVal)(fn | ((uint64_t)1 << 32)) : (JaclVal)-1;
}

/* The error for calling closure `c` with `argc` arguments it does not take: "proc 'NAME' expects
 * N arguments but got M", "closure expects …" when the call names none, "at least N" when its last
 * parameter collects the rest. */
JaclVal jacl_closure_arity_error(JaclVal name, JaclVal c, JaclVal argc) {
  uint32_t shape = ((JaclObj *)jaclrt_as_ptr(c))->_rsv;
  uint32_t want = shape & ~JACL_CLOSURE_REST;
  JaclVal s = jaclrt_is_string(name)
                  ? jacl_str_concat(jacl_str_concat(jacl_str_new("proc '", 6), name),
                                    jacl_str_new("' expects ", 10))
                  : jacl_str_new("closure expects ", 16);
  if (shape & JACL_CLOSURE_REST) s = jacl_str_concat(s, jacl_str_new("at least ", 9));
  s = jacl_str_concat(s, jacl_to_string(jaclrt_i32((int32_t)want)));
  s = jacl_str_concat(s, want == 1 ? jacl_str_new(" argument but got ", 18)
                                   : jacl_str_new(" arguments but got ", 19));
  s = jacl_str_concat(s, jacl_to_string(jaclrt_i32((int32_t)(uint64_t)argc)));
  return jacl_error_new(s);
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
