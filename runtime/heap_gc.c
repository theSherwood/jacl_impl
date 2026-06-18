/* heap_gc.c — JACL runtime heap allocator + conservative non-moving GC (P1.2/P1.3).
 *
 * Allocator: a granule-aligned region with in-band object headers, size-class free
 * lists (perfect reuse within a class) + a bump pointer for fresh cells, and a
 * per-granule **object-start bitmap** for O(1) conservative object-start checks.
 *
 * GC: conservative, non-moving mark-sweep. Roots come from the svm `gc.roots` op
 * (__vm_gc_roots) — the words on the control stack/registers that point into the
 * heap. Heap edges are traced conservatively (each live cell's payload words are
 * scanned for candidate object-starts); precise per-type tracing is a later
 * refinement (P1.5, once object layouts are defined). Non-moving ⇒ found pointers
 * stay valid; sweep returns unmarked cells to their size-class free list.
 *
 * Backing store: a large static region (no window growth yet). Swapping the chunk
 * source to __vm_map-grown window pages is a localized follow-up that needs the
 * powerbox harness (the Memory capability); the algorithm is unchanged.
 */
#include "jaclrt.h"
#include <stdint.h>
#include <string.h>

/* svm op: scan the calling fiber's roots into buf, return total candidate count.
 * `mask` is AND-ed with each scanned word before the range test (and the masked
 * value is what is returned), so a tagged JaclVal (tag in the high byte) resolves
 * to its bare heap offset. svm constrains the mask to top-byte-strip only, so no
 * host address can be folded into the heap window. */
long __vm_gc_roots(long heap_lo, long heap_hi, long mask, void *buf, long cap);

#ifndef JACL_HEAP_BYTES
#define JACL_HEAP_BYTES   (16u << 20)   /* 16 MiB backing region (override for tests) */
#endif
#define JACL_GRANULE      16u
#define JACL_MAX_CLASS    1024u         /* cells <= this get an exact-size free list */
#define JACL_NCLASS       (JACL_MAX_CLASS / JACL_GRANULE)   /* 64 classes */

static uint8_t  jacl_heap_mem[JACL_HEAP_BYTES] __attribute__((aligned(16)));
static uint32_t jacl_bump;                       /* next fresh byte offset */
static uint32_t jacl_freelist[JACL_NCLASS + 1];  /* head offsets per class (0 = empty/null sentinel) */
/* object-start bitmap: 1 bit per granule */
static uint8_t  jacl_startmap[JACL_HEAP_BYTES / JACL_GRANULE / 8];

/* a free cell stores the next free offset in the word right after its header */
static inline uint32_t* free_next_slot(JaclObj* o) { return (uint32_t*)((uint8_t*)o + sizeof(JaclObj)); }

static inline uint32_t round_granule(uint32_t n) { return (n + (JACL_GRANULE - 1)) & ~(JACL_GRANULE - 1); }

static inline void startbit_set(uint32_t off)   { uint32_t g = off / JACL_GRANULE; jacl_startmap[g >> 3] |=  (uint8_t)(1u << (g & 7)); }
static inline void startbit_clear(uint32_t off) { uint32_t g = off / JACL_GRANULE; jacl_startmap[g >> 3] &= (uint8_t)~(1u << (g & 7)); }
static inline int  startbit_test(uint32_t off)  { uint32_t g = off / JACL_GRANULE; return (jacl_startmap[g >> 3] >> (g & 7)) & 1; }

void jacl_heap_init(void) {
  jacl_bump = JACL_GRANULE;   /* leave offset 0 unused so a 0 offset reads as "null" */
  for (uint32_t i = 0; i <= JACL_NCLASS; i++) jacl_freelist[i] = 0;
  memset(jacl_startmap, 0, sizeof(jacl_startmap));
}

/* svm-llvm rejects a *constant* ptrtoint of a global address; route the cast
   through a noinline helper so it is a runtime instruction (Phase-0 finding). */
__attribute__((noinline)) static long jacl_pti(void *p) { return (long)p; }
long jacl_heap_lo(void) { return jacl_pti(&jacl_heap_mem[0]); }
long jacl_heap_hi(void) { return jacl_pti(&jacl_heap_mem[JACL_HEAP_BYTES]); }

/* noinline is load-bearing for conservative GC: if the allocator is inlined into
 * the program, the optimizer can constant-fold object addresses (a deterministic
 * bump sequence over a static heap), so "roots" become rematerialized constants
 * that are never live pointers on the stack — invisible to gc.roots. A separately
 * compiled runtime (the real backend) is opaque by construction; noinline
 * reproduces that opacity here. */
/* Try to obtain a cell offset of `cell` bytes from the free list or the bump pointer;
 * returns 0 (the null sentinel offset) if neither has room. */
static uint32_t jacl_alloc_off(uint32_t cell) {
  uint32_t cls = cell / JACL_GRANULE;
  if (cls <= JACL_NCLASS && jacl_freelist[cls]) {
    uint32_t off = jacl_freelist[cls];                /* reuse a swept cell of this exact size */
    JaclObj* fo = (JaclObj*)&jacl_heap_mem[off];
    jacl_freelist[cls] = *free_next_slot(fo);
    return off;
  }
  if (jacl_bump + cell <= JACL_HEAP_BYTES) {
    uint32_t off = jacl_bump;
    jacl_bump += cell;
    return off;
  }
  return 0;                                           /* no room */
}

__attribute__((noinline))
void* jacl_alloc(uint32_t obj_type, uint32_t payload) {
  uint32_t cell = round_granule((uint32_t)sizeof(JaclObj) + payload);
  uint32_t off = jacl_alloc_off(cell);
  if (!off) {
    /* Out of room: a stop-the-world, conservative, non-moving collection. The only
     * fiber (Phase 2 is sequential) is at this call — a safe point — and gc.roots
     * conservatively scans the whole control stack, so every live root (the caller's
     * tagged JaclVals and any in-progress runtime allocations) is found without
     * precise stack maps or a rooting protocol. Multi-fiber quiescing is Phase 3. */
    jacl_gc_collect();
    off = jacl_alloc_off(cell);
    if (!off) return 0;                               /* genuine OOM after collection */
  }
  JaclObj* o = (JaclObj*)&jacl_heap_mem[off];
  o->size = cell;
  o->obj_type = (uint8_t)obj_type;
  o->mark = 0;
  o->_rsv = 0;
  memset(jacl_obj_payload(o), 0, cell - (uint32_t)sizeof(JaclObj));
  startbit_set(off);
  return o;
}

long jacl_live_count(void) {
  long n = 0;
  for (uint32_t off = JACL_GRANULE; off < jacl_bump; ) {
    JaclObj* o = (JaclObj*)&jacl_heap_mem[off];
    if (o->obj_type != JOBJ_FREE) n++;
    off += o->size;
  }
  return n;
}

/* ---- mark phase: explicit worklist, conservative ---- */
#define MARK_STACK_CAP 65536
static uint32_t mark_stack[MARK_STACK_CAP];
static uint32_t mark_sp;

/* Resolve a candidate window-offset word (possibly an INTERIOR pointer — the
 * optimizer often holds a pointer to an object's payload, not its cell start) to
 * the enclosing object and mark it. Scans the object-start bitmap back to the
 * nearest start at-or-below the candidate, then checks the object's extent
 * contains it. Conservative + non-moving, so interior pointers are first-class. */
#define INTERIOR_SCAN_GRANULES 8192   /* cap the back-scan (objects are small here) */
static void mark_push_word(long word) {
  long lo = jacl_heap_lo();
  if (word < lo || word >= jacl_heap_hi()) return;
  uint32_t off = (uint32_t)(word - lo);
  uint32_t g = off / JACL_GRANULE;                 /* candidate granule */
  uint32_t limit = g > INTERIOR_SCAN_GRANULES ? g - INTERIOR_SCAN_GRANULES : 0;
  while (g + 1 > limit + 1) {                       /* scan back to nearest start granule */
    if (startbit_test(g * JACL_GRANULE)) break;
    if (g == limit) return;                         /* no object start within reach */
    g--;
  }
  uint32_t soff = g * JACL_GRANULE;
  if (!startbit_test(soff)) return;
  JaclObj* o = (JaclObj*)&jacl_heap_mem[soff];
  if (off >= soff + o->size) return;               /* candidate lies past this object */
  if (o->obj_type == JOBJ_FREE || o->mark) return;
  o->mark = 1;
  if (mark_sp < MARK_STACK_CAP) mark_stack[mark_sp++] = soff;
}

/* Public: mark a heap-typed JaclVal as reachable. Used by jacl_mark_runtime_roots
 * (intern table, etc.) — the roots gc.roots cannot see (they are not on the stack). */
void jacl_gc_mark(JaclVal v) {
  if (jaclrt_is_heap(v)) mark_push_word((long)(v & JACL_PAYLOAD_MASK));
}

static void mark_drain(void) {
  while (mark_sp) {
    uint32_t off = mark_stack[--mark_sp];
    JaclObj* o = (JaclObj*)&jacl_heap_mem[off];
    if (o->obj_type != JOBJ_NODE) continue;        /* BLOBs have no outgoing pointers */
    /* conservative trace: scan payload words for candidate object-starts */
    uint8_t* p = (uint8_t*)jacl_obj_payload(o);
    uint8_t* end = (uint8_t*)o + o->size;
    for (; p + 8 <= end; p += 8) {
      long w = (long)*(uint64_t*)p;
      /* heap pointers are stored as JaclVal payloads or raw cell pointers; mask off
         any tag byte so a tagged heap JaclVal resolves to its cell offset */
      mark_push_word(w & (long)JACL_PAYLOAD_MASK);
      mark_push_word(w);
    }
  }
}

long jacl_gc_collect(void) {
  /* 1. clear marks */
  for (uint32_t off = JACL_GRANULE; off < jacl_bump; ) {
    JaclObj* o = (JaclObj*)&jacl_heap_mem[off];
    o->mark = 0;
    off += o->size;
  }
  /* 2. roots from the svm gc.roots op.
   * Stack roots are tagged JaclVals (tag in the high byte: STRING<<56, VECTOR<<56,
   * …) whose full word sits far above the heap range, plus raw heap pointers
   * (tag byte 0) on C frames. We pass JACL_PAYLOAD_MASK so svm strips the tag byte
   * before the range test and returns the bare offset: a tagged JaclVal masks to
   * its heap offset (in range → kept), a raw heap pointer is unchanged (kept), and
   * a host address never folds into the heap window (the mask only clears the top
   * byte). The returned words are already masked + in [heap_lo, heap_hi). */
  static long rootbuf[8192];
  long n = __vm_gc_roots(jacl_heap_lo(), jacl_heap_hi(), (long)JACL_PAYLOAD_MASK, rootbuf, 8192);
  long scan = n < 8192 ? n : 8192;
  mark_sp = 0;
  for (long i = 0; i < scan; i++) mark_push_word(rootbuf[i]);
  /* 2b. runtime-internal roots (intern table, …) — not visible to gc.roots */
  jacl_mark_runtime_roots();
  /* 3. trace */
  mark_drain();
  /* 4. sweep: unmarked live cells -> free list */
  long swept = 0;
  for (uint32_t off = JACL_GRANULE; off < jacl_bump; ) {
    JaclObj* o = (JaclObj*)&jacl_heap_mem[off];
    uint32_t next = off + o->size;
    if (o->obj_type != JOBJ_FREE && !o->mark) {
      uint32_t cls = o->size / JACL_GRANULE;
      o->obj_type = JOBJ_FREE;
      startbit_clear(off);
      if (cls <= JACL_NCLASS) { *free_next_slot(o) = jacl_freelist[cls]; jacl_freelist[cls] = off; }
      swept++;
    }
    off = next;
  }
  return swept;
}
