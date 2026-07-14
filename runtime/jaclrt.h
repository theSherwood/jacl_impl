/* jaclrt.h — JACL runtime, value representation (Phase 1, P1.1)
 *
 * JaclVal is a 64-bit tagged value: an 8-bit tag byte (top) over a 56-bit
 * payload. Heap pointers are window offsets, which fit in 56 bits (the SVM window
 * is ~2^40). The tag/type layout mirrors the existing bytecode VM (src/jacl.h) so
 * ported builtins/collections stay compatible.
 *
 * P1.1 covers the inline scalars (nil/bool/i32/f32) + tag classification; heap
 * types (string/vector/map/i64/f64/...) get constructors once the allocator (P1.2)
 * and GC (P1.3) land.
 */
#ifndef JACLRT_H
#define JACLRT_H

#include <stdint.h>
#include <stdbool.h>

typedef uint64_t JaclVal;

#define JACL_TAG_SHIFT     56
#define JACL_TAG_MASK      ((uint64_t)0xFF << JACL_TAG_SHIFT)
#define JACL_PAYLOAD_MASK  ((((uint64_t)1) << JACL_TAG_SHIFT) - 1)
#define JACL_TYPE_MASK     ((uint64_t)0x1F << JACL_TAG_SHIFT)   /* 5-bit type, ignores flag bits */

/* Type tags (index << shift). Mirrors src/jacl.h. */
#define JACL_TAG_NIL           ((uint64_t)0x00 << JACL_TAG_SHIFT)
#define JACL_TAG_BOOL          ((uint64_t)0x01 << JACL_TAG_SHIFT)
#define JACL_TAG_I32           ((uint64_t)0x02 << JACL_TAG_SHIFT)
#define JACL_TAG_F32           ((uint64_t)0x03 << JACL_TAG_SHIFT)
#define JACL_TAG_INLINE_STRING ((uint64_t)0x04 << JACL_TAG_SHIFT)
#define JACL_TAG_STRING        ((uint64_t)0x05 << JACL_TAG_SHIFT)
#define JACL_TAG_VECTOR        ((uint64_t)0x06 << JACL_TAG_SHIFT)
#define JACL_TAG_MAP           ((uint64_t)0x07 << JACL_TAG_SHIFT)
#define JACL_TAG_CLOSURE       ((uint64_t)0x08 << JACL_TAG_SHIFT)
#define JACL_TAG_BIGNUM        ((uint64_t)0x09 << JACL_TAG_SHIFT)
#define JACL_TAG_CELL          ((uint64_t)0x0A << JACL_TAG_SHIFT)
#define JACL_TAG_BOX           ((uint64_t)0x0B << JACL_TAG_SHIFT)
#define JACL_TAG_ATOM          ((uint64_t)0x0C << JACL_TAG_SHIFT)
#define JACL_TAG_U32           ((uint64_t)0x0D << JACL_TAG_SHIFT)
#define JACL_TAG_I64           ((uint64_t)0x0E << JACL_TAG_SHIFT)
#define JACL_TAG_U64           ((uint64_t)0x0F << JACL_TAG_SHIFT)
#define JACL_TAG_F64           ((uint64_t)0x10 << JACL_TAG_SHIFT)
#define JACL_TAG_STRUCT        ((uint64_t)0x12 << JACL_TAG_SHIFT)
#define JACL_TAG_ROPE_STRING   ((uint64_t)0x14 << JACL_TAG_SHIFT)
#define JACL_TAG_STREAM        ((uint64_t)0x15 << JACL_TAG_SHIFT)
#define JACL_TAG_ARR           ((uint64_t)0x1A << JACL_TAG_SHIFT)   /* mutable array (mirrors src/jacl.h) */

/* Bitmask of heap-managed type indices (after >> shift) — O(1) is-heap test.
 * Mirrors src/jacl.h's JACL_HEAP_TAG_MASK, with STREAM (0x15, bit 21) added since
 * the runtime's streams are GC objects that hold heap references. */
#define JACL_HEAP_TAG_MASK     (0x07D7DFE0u | (1u << 0x15) | (1u << 0x1A))

/* Flag bits (61..63), above the 5-bit type (mirrors src/value.c). */
#define JACL_FLAG_TAINTED      ((uint64_t)1 << 63)
#define JACL_FLAG_SECRET       ((uint64_t)1 << 62)
#define JACL_FLAG_ERROR        ((uint64_t)1 << 61)

#define JACL_NIL    ((JaclVal)JACL_TAG_NIL)
#define JACL_TRUE   ((JaclVal)(JACL_TAG_BOOL | 1))
#define JACL_FALSE  ((JaclVal)(JACL_TAG_BOOL))

/* ----- type queries ----- */
static inline uint32_t jaclrt_type_index(JaclVal v) {
  return (uint32_t)((v >> JACL_TAG_SHIFT) & 0x1F);
}
static inline bool jaclrt_is_nil(JaclVal v)  { return jaclrt_type_index(v) == 0x00; }
static inline bool jaclrt_is_bool(JaclVal v) { return jaclrt_type_index(v) == 0x01; }
static inline bool jaclrt_is_i32(JaclVal v)  { return jaclrt_type_index(v) == 0x02; }
static inline bool jaclrt_is_f32(JaclVal v)  { return jaclrt_type_index(v) == 0x03; }
/* Is this value a heap-managed pointer (the GC must trace/scan its payload)? */
static inline bool jaclrt_is_heap(JaclVal v) {
  return (JACL_HEAP_TAG_MASK >> jaclrt_type_index(v)) & 1u;
}
static inline bool jaclrt_is_error(JaclVal v) { return (v & JACL_FLAG_ERROR) != 0; }
static inline JaclVal jaclrt_set_error(JaclVal v) { return v | JACL_FLAG_ERROR; }
static inline JaclVal jaclrt_error(void) { return JACL_NIL | JACL_FLAG_ERROR; }

/* ----- constructors (inline scalars) ----- */
static inline JaclVal jaclrt_nil(void)        { return JACL_NIL; }
static inline JaclVal jaclrt_bool(bool b)     { return b ? JACL_TRUE : JACL_FALSE; }
static inline JaclVal jaclrt_i32(int32_t x)   { return JACL_TAG_I32 | (uint64_t)(uint32_t)x; }

/* ----- extractors ----- */
static inline bool    jaclrt_as_bool(JaclVal v) { return (v & 1u) != 0; }
static inline int32_t jaclrt_as_i32(JaclVal v)  { return (int32_t)(uint32_t)(v & 0xFFFFFFFFu); }
/* Heap payload as a window offset (for P1.2+). */
static inline uint64_t jaclrt_payload(JaclVal v) { return v & JACL_PAYLOAD_MASK; }

/* Heap pointer <-> tagged value. Heap pointers are window offsets, so they round
 * trip through the 56-bit payload. */
static inline void*   jaclrt_as_ptr(JaclVal v) { return (void*)(uintptr_t)(v & JACL_PAYLOAD_MASK); }
static inline JaclVal jaclrt_from_ptr(uint64_t tag, void *p) {
  return tag | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
}

/* ----- inline strings (<=7 bytes packed into the payload; length = first NUL) ----- */
static inline JaclVal jaclrt_inline_string(const char *s, uint32_t len) {
  uint64_t payload = 0;
  for (uint32_t i = 0; i < len && i < 7; i++) {
    if (s[i] == '\0') break;
    payload |= ((uint64_t)(unsigned char)s[i]) << (i * 8);
  }
  return JACL_TAG_INLINE_STRING | payload;
}
static inline bool jaclrt_is_inline_string(JaclVal v) { return jaclrt_type_index(v) == 0x04; }
static inline uint32_t jaclrt_inline_len(JaclVal v) {
  uint64_t p = v & JACL_PAYLOAD_MASK; uint32_t n = 0;
  for (uint32_t i = 0; i < 7; i++) { if (((p >> (i * 8)) & 0xFF) == 0) break; n++; }
  return n;
}
static inline void jaclrt_inline_get(JaclVal v, char *buf, uint32_t buflen) {
  uint64_t p = v & JACL_PAYLOAD_MASK; uint32_t i = 0;
  for (; i < 7 && i + 1 < buflen; i++) {
    unsigned char c = (unsigned char)((p >> (i * 8)) & 0xFF);
    if (c == '\0') break; buf[i] = (char)c;
  }
  if (buflen) buf[i] = '\0';
}
static inline bool jaclrt_is_string(JaclVal v) {
  uint32_t t = jaclrt_type_index(v);
  return t == 0x04 || t == 0x05 || t == 0x14;   /* inline / heap / rope */
}

/* ===================================================================
 * Heap + GC (P1.2 / P1.3) — non-inline; defined in heap_gc.c.
 * Conservative, non-moving mark-sweep over a heap region; roots via the
 * svm `gc.roots` op (__vm_gc_roots). Object pointers are the cell address
 * (header included). See heap_gc.c.
 * =================================================================== */

/* GC object types (expands as real heap types land in P1.4/P1.5). */
enum {
  JOBJ_FREE = 0,   /* a swept/free cell (skipped by tracing) */
  JOBJ_BLOB = 1,   /* opaque payload, no outgoing pointers   */
  JOBJ_NODE = 2,   /* payload may contain heap pointers (conservatively traced) */
  JOBJ_STR  = 3,   /* heap string: { u32 len; bytes[] } — no outgoing pointers */
};

typedef struct JaclObj {
  uint32_t size;     /* total cell size in bytes (header + payload), granule-aligned */
  uint8_t  obj_type; /* JOBJ_* */
  uint8_t  mark;     /* GC mark bit */
  uint16_t _rsv;
} JaclObj;

void   jacl_heap_init(void);                              /* reset the heap */
void*  jacl_alloc(uint32_t obj_type, uint32_t payload);   /* -> object cell (header+payload), zeroed payload */
long   jacl_gc_collect(void);                             /* mark-sweep; returns #cells reclaimed */
long   jacl_live_count(void);                             /* #live (non-free) cells */
long   jacl_heap_lo(void);                                /* window-offset bounds, for gc.roots */
long   jacl_heap_hi(void);
void   jacl_gc_mark(JaclVal v);                           /* mark a heap value as reachable (for runtime roots) */
void   jacl_gc_mark_word(long w);                         /* conservatively mark a tagged-or-raw heap word */
static inline void*    jacl_obj_payload(JaclObj* o) { return (void*)((uint8_t*)o + sizeof(JaclObj)); }

/* Multi-vCPU stop-the-world quiesce over the fiber scheduler (P3.4c). A worker (vCPU)
 * registers before it allocates and unregisters before it blocks off a safepoint or
 * exits. The scheduler brackets each task resume with task_begin/end and calls
 * worker_park_if_requested at its loop top; a task safepoint (jacl_alloc + loop
 * back-edges) suspends the task to the scheduler so its roots are scannable. Any worker
 * in jacl_gc_collect becomes the collector, stops the others, scans, and resumes. The
 * main thread is worker 0, registered by jacl_heap_init. */
void   jacl_gc_worker_register(void);
void   jacl_gc_worker_unregister(void);
void   jacl_gc_task_begin(void);                         /* scheduler: a task fiber is now running */
void   jacl_gc_task_end(void);                           /* scheduler: the task suspended/returned */
void   jacl_gc_safepoint(void);                          /* task code: suspend if a collection is in progress */
void   jacl_gc_worker_park_if_requested(void);           /* scheduler loop top: park the vCPU if stopping */
long   jacl_gc_violation_count(void);                    /* STW mutual-exclusion violations (must be 0) */

/* Reusable worker-pool batch scheduler (P3.4d, sched.c): run n task fibers across
 * nworkers vCPU workers, GC-safe via the quiesce primitives above. The substrate
 * parallel/race/await move onto. */
typedef long (*JaclTaskFn)(long);
void   jacl_sched_run_batch(JaclTaskFn *fn, long *arg, long *result, long n, int nworkers);
JaclVal jacl_parallel(JaclVal closures);   /* run each 0-arg closure on the pool → vector of results */
JaclVal jacl_race(JaclVal closures);       /* run each on the pool → first block's result */
JaclVal jacl_sched_run_main(JaclVal fnref);/* run the program body as the root task fiber (program-as-a-job) */
JaclVal jacl_spawn(JaclVal closure);       /* record a pending task; returns a future */
JaclVal jacl_await(JaclVal future);        /* flush pending tasks onto the pool; return this one's result */
void   jacl_sched_mark_roots(void);        /* GC: root pending futures during a flush (STW hook) */

/* Runtime-internal roots the collector must also mark (intern table, …) — JACL
 * obligation #3. Defined in string.c (extends as more runtime state lands). */
void   jacl_mark_runtime_roots(void);

/* ===================================================================
 * Strings (P1.4) — inline / heap / interned. Defined in string.c.
 * =================================================================== */
/* Host I/O (P3.5, io.c) — through the svm powerbox Stream capability. */
JaclVal  jacl_print(JaclVal v);                           /* write V's text + newline to stdout; -> nil */

void     jacl_intern_init(void);                          /* reset the intern table */
JaclVal  jacl_str_new(const char *s, uint32_t len);       /* inline if <=7, else a fresh heap string */
JaclVal  jacl_str_intern(const char *s, uint32_t len);    /* canonical (pointer-equal) string */
uint32_t jacl_str_len(JaclVal v);
void     jacl_str_bytes(JaclVal v, char *buf, uint32_t buflen);  /* NUL-terminated copy */
bool     jacl_str_eq(JaclVal a, JaclVal b);
uint32_t jacl_str_hash(JaclVal v);                        /* hash of string bytes (inline or heap) */
JaclVal  jacl_str_concat(JaclVal a, JaclVal b);           /* a ++ b */

/* ===================================================================
 * Collections (P1.5) — persistent vector over the GC heap. Defined in
 * collections.c (instantiates lib/rrb_vec for JaclVal). A vector value is
 * JACL_TAG_VECTOR over the RRB root cell; nodes are JOBJ_NODE (traced), so
 * the GC keeps the whole structure — including heap-valued elements — alive.
 * =================================================================== */
JaclVal  jacl_vec_empty(void);
JaclVal  jacl_vec_push(JaclVal vec, JaclVal elem);   /* persistent: returns a new vector */
JaclVal  jacl_vec_get(JaclVal vec, uint32_t idx);    /* element, or JACL_NIL if out of range */
uint32_t jacl_vec_count(JaclVal vec);

/* persistent map (HAMT) over the GC heap; keys/values are JaclVals (value-aware
 * hash/eq for strings + scalars). An empty map is JACL_TAG_MAP over a NULL root. */
void     jacl_map_init(void);                        /* install key handlers (call once) */
JaclVal  jacl_map_empty(void);
JaclVal  jacl_map_set(JaclVal map, JaclVal key, JaclVal val);   /* persistent: returns a new map */
JaclVal  jacl_map_get(JaclVal map, JaclVal key);     /* value, or JACL_NIL if absent */
int      jacl_map_has(JaclVal map, JaclVal key);
JaclVal  jacl_map_remove(JaclVal map, JaclVal key);  /* persistent */
uint32_t jacl_map_count(JaclVal map);

/* ===================================================================
 * Streams (P1.6) — stackless pull-based iterators. Defined in stream.c.
 * No fibers, no state machines: each combinator is a heap object (JOBJ_NODE,
 * JACL_TAG_STREAM) holding its source + state; `next` pulls from the source.
 * Transforms/predicates are C function pointers here (stand-ins for JACL
 * closures, which arrive with Phase-2 codegen).
 * =================================================================== */
typedef JaclVal (*JaclMapFn)(JaclVal);   /* element -> element */
typedef int     (*JaclPredFn)(JaclVal);  /* element -> keep? (nonzero = keep) */

JaclVal jacl_stream_range(int64_t n);                 /* 0..n-1 as i32 */
JaclVal jacl_stream_vec(JaclVal vec);                 /* a vector's elements */
JaclVal jacl_stream_map(JaclVal src, JaclMapFn f);
JaclVal jacl_stream_filter(JaclVal src, JaclPredFn p);
JaclVal jacl_stream_take(JaclVal src, int64_t k);
int     jacl_stream_next(JaclVal s, JaclVal *out);    /* 1 + *out if a value; 0 if done */
JaclVal jacl_stream_collect(JaclVal s);               /* drain into a vector */

/* ===================================================================
 * Builtins (P1.7) — the dynamic value-op layer JACL codegen calls into.
 * Errors short-circuit (an error operand is returned as-is); type/domain
 * errors return an error-flagged value (the NaN-like propagation model).
 * The full opcode surface is ported incrementally (continues into Phase 2).
 * =================================================================== */
JaclVal jacl_add(JaclVal a, JaclVal b);
JaclVal jacl_sub(JaclVal a, JaclVal b);
JaclVal jacl_mul(JaclVal a, JaclVal b);
JaclVal jacl_div(JaclVal a, JaclVal b);    /* div-by-zero -> error */
JaclVal jacl_mod(JaclVal a, JaclVal b);    /* mod-by-zero -> error */
JaclVal jacl_neg(JaclVal a);
JaclVal jacl_eq(JaclVal a, JaclVal b);     /* bitwise type+payload equality -> bool */
JaclVal jacl_ne(JaclVal a, JaclVal b);
JaclVal jacl_lt(JaclVal a, JaclVal b);     /* ordered comparisons: i32 (else error) */
JaclVal jacl_le(JaclVal a, JaclVal b);
JaclVal jacl_gt(JaclVal a, JaclVal b);
JaclVal jacl_ge(JaclVal a, JaclVal b);
JaclVal jacl_not(JaclVal a);               /* bool negation */
JaclVal jacl_typeof(JaclVal v);            /* -> type-name string */
JaclVal jacl_len(JaclVal v);               /* string/vec/map length -> i32 (else error) */
JaclVal jacl_vec_get_at(JaclVal v, JaclVal idx);   /* JaclVal-uniform vec-get (i32 index) */
JaclVal jacl_map_has_v(JaclVal m, JaclVal k);      /* JaclVal-uniform map-has? -> bool */
int     jacl_val_equal(JaclVal a, JaclVal b);      /* structural equality (vec/map recursive) */
JaclVal jacl_vec_set(JaclVal v, uint32_t idx, JaclVal elem);       /* persistent update */
uint32_t jacl_map_entries(JaclVal m, JaclVal *ks, JaclVal *vs, uint32_t cap);
JaclVal jacl_is_error_v(JaclVal v);                /* error? -> bool */
JaclVal jacl_error_new(JaclVal v);                 /* [error V]: set the error flag */
JaclVal jacl_error_val(JaclVal v);                 /* payload with the error flag cleared */
JaclVal jacl_struct_new(JaclVal typename_, JaclVal nfields);
JaclVal jacl_struct_init_field(JaclVal s, JaclVal idx, JaclVal name, JaclVal value);
JaclVal jacl_struct_get(JaclVal s, JaclVal name);
JaclVal jacl_struct_put(JaclVal s, JaclVal name, JaclVal v);
JaclVal jacl_box_new(JaclVal v);                   /* [box V] — mutable single-slot ref */
JaclVal jacl_box_get(JaclVal b);                   /* [deref B] */
JaclVal jacl_box_set(JaclVal b, JaclVal v);        /* [reset B V] */
JaclVal jacl_is_box_v(JaclVal v);                  /* box? */
JaclVal jacl_field_get(JaclVal v, JaclVal name);   /* struct field / map entry by name */
JaclVal jacl_sleep(JaclVal secs);                  /* [sleep S] — futex-timeout sleep */
JaclVal jacl_atom_new(JaclVal v);                  /* [atom V] */
JaclVal jacl_is_atom_v(JaclVal v);                 /* atom? */
JaclVal jacl_range_vec(JaclVal a, JaclVal b);      /* [range A B) as a vector */
JaclVal jacl_range_inclusive(JaclVal a, JaclVal b);/* [range-inclusive A B] as a vector */
JaclVal jacl_vec_push_v(JaclVal v, JaclVal elem);  /* error-propagating [vec-push V E] */
JaclVal jacl_vec_slice(JaclVal v, JaclVal s, JaclVal e); /* [vec-slice V S E) sub-vector */
JaclVal jacl_assert(JaclVal v);                    /* [assert COND] -> nil / error */
JaclVal jacl_ctx_get(void);                        /* the ambient \$ctx map */
JaclVal jacl_ctx_swap(JaclVal m);                  /* replace \$ctx, returning the old */
JaclVal jacl_ctx_set_field(JaclVal name, JaclVal v);
JaclVal jacl_wide_new(uint32_t tidx, int64_t bits); /* heap i64/u64/f64 cell */
JaclVal jacl_widen_to(JaclVal v, JaclVal kind);     /* typed-def widening */
JaclVal jacl_to_cast(JaclVal v, JaclVal tname);     /* [to TYPE V] */
JaclVal jacl_dot_dyn(JaclVal v, JaclVal k);         /* dynamic ->: index or field */
JaclVal jacl_qdot(JaclVal v, JaclVal key);          /* [?. V KEY] optional chaining */
JaclVal jacl_dot_dyn_set(JaclVal v, JaclVal k, JaclVal nv);
JaclVal jacl_vec_concat(JaclVal a, JaclVal b);      /* persistent vector concat */
JaclVal jacl_lines(JaclVal s);                      /* split on newlines -> vec */
JaclVal jacl_assert_type(JaclVal v, JaclVal tname);/* dynamic type assertion */
JaclVal jacl_vec_set_at(JaclVal v, JaclVal idx, JaclVal elem);     /* uniform vec-set */
JaclVal jacl_map_keys_v(JaclVal m);                /* keys as a vector */
JaclVal jacl_map_vals_v(JaclVal m);                /* values as a vector */
JaclVal jacl_arr_new(void);                        /* mutable array (aliasing-visible) */
JaclVal jacl_arr_push(JaclVal a, JaclVal v);       /* in-place append -> a */
JaclVal jacl_arr_pop(JaclVal a);                   /* remove + return last (error if empty) */
JaclVal jacl_arr_get_at(JaclVal a, JaclVal idx);   /* i32 index -> element */
JaclVal jacl_arr_set_at(JaclVal a, JaclVal idx, JaclVal v); /* in-place -> v */
uint32_t jacl_arr_count(JaclVal a);
JaclVal jacl_arr_get(JaclVal a, uint32_t i);       /* raw-index get (internal users) */
JaclVal jacl_index_get(JaclVal coll, JaclVal idx); /* vec OR arr element (for-each) */
JaclVal jacl_iter_val_at(JaclVal c, JaclVal idx);  /* i-th value (vec/arr/map) */
JaclVal jacl_iter_key_at(JaclVal c, JaclVal idx);  /* i-th key (vec/arr index; map key) */
JaclVal jacl_to_string(JaclVal v);         /* i32/bool/nil/string -> string */

/* P2.6 closures + cells (see closure.c). fnref/index/count are raw i64; values are
 * JaclVals. A closure holds [fnref, upvals…]; a cell is a shared mutable box. */
JaclVal jacl_closure_new(JaclVal fnref, JaclVal nupvals);
JaclVal jacl_closure_set(JaclVal c, JaclVal i, JaclVal v);
JaclVal jacl_closure_fn(JaclVal c);
JaclVal jacl_closure_upval(JaclVal c, JaclVal i);
JaclVal jacl_cell_new(JaclVal v);
JaclVal jacl_cell_get(JaclVal c);
JaclVal jacl_cell_set(JaclVal c, JaclVal v);

/* P3.1 generators on fibers (see fiber.c). fnref is a raw funcref (from ref.func). */
JaclVal jacl_gen_new(JaclVal fnref, JaclVal arg);
JaclVal jacl_gen_next(JaclVal gen);
JaclVal jacl_gen_done(JaclVal gen);

/* P3.2 spawn/await: a future is a task fiber driven to completion by await. */
JaclVal jacl_spawn(JaclVal closure);
JaclVal jacl_await(JaclVal future);

#endif /* JACLRT_H */
