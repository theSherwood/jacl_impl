/* heap_gc.c — JACL runtime heap allocator + conservative non-moving GC.
 *
 * Allocator (P3.4a — per-vCPU heaps): the backing region is carved into fixed-size
 * **regions**; each worker (vCPU) owns a current region and bump-allocates within it
 * with **no lock** (only the owner touches its bump pointer and its size-class free
 * lists). The sole cross-worker synchronization on the allocation path is claiming a
 * fresh region from a shared pool — one `__vm_atomic_add`, amortized once per region.
 * The current worker is the per-vCPU TLS word (`__vm_vcpu_tls_get`, seeded by svm to
 * a dense vCPU id), so a fiber that migrated under work-stealing allocates into the
 * vCPU it is *currently* on. The main thread is vCPU 0, so the single-thread path is
 * unchanged. (This replaces P1's single global bump + free lists.)
 *
 * GC: conservative, non-moving mark-sweep. Roots come from the svm `gc.roots` op
 * (__vm_gc_roots) — the words on the control stack/registers that point into the
 * heap. Heap edges are traced conservatively (each live cell's payload words are
 * scanned for candidate object-starts). Non-moving ⇒ found pointers stay valid; the
 * sweep returns each unmarked cell to its owning worker's size-class free list.
 * Multi-vCPU stop-the-world quiescing is P3.4c; until then the only safe point is the
 * single thread's allocation call, exactly as in P2.
 *
 * Heap iteration (live_count, mark-clear, sweep) walks the per-granule **object-start
 * bitmap** rather than `off += size`: with per-worker regions the live cells are no
 * longer one contiguous run (region tails and unclaimed regions are gaps), but every
 * allocated cell still has exactly one start bit, so the bitmap is the authoritative
 * iterator. Free cells clear their start bit, so they are skipped.
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

/* svm §12 atomics + §12 per-vCPU TLS register (svm-llvm lowers these). The TLS word
 * is seeded to a dense vCPU id (root 0, children sequential) and read at the
 * execution point, so it is the current worker's index even across fiber migration. */
long __vm_atomic_add(void *p, long v);   /* returns the previous value */
long __vm_vcpu_tls_get(void);

#ifndef JACL_HEAP_BYTES
#define JACL_HEAP_BYTES   (16u << 20)   /* 16 MiB backing region (override for tests) */
#endif
#define JACL_GRANULE      16u
#define JACL_MAX_CLASS    1024u         /* cells <= this get an exact-size free list */
#define JACL_NCLASS       (JACL_MAX_CLASS / JACL_GRANULE)   /* 64 classes */

#define JACL_REGION_BYTES (1u << 16)    /* 64 KiB per-worker allocation region */
#define JACL_MAX_WORKERS  64            /* vCPU ids 0 .. JACL_MAX_WORKERS-1 */
#define JACL_NREGIONS     (JACL_HEAP_BYTES / JACL_REGION_BYTES)
#define JACL_REGION_NONE  0xFFFFFFFFu   /* "shared pool exhausted" sentinel base */

static uint8_t  jacl_heap_mem[JACL_HEAP_BYTES] __attribute__((aligned(16)));
/* object-start bitmap: 1 bit per granule. A region spans a whole number of bitmap
 * bytes (64 KiB / 16 / 8 = 512), so distinct workers never share a bitmap byte. */
static uint8_t  jacl_startmap[JACL_HEAP_BYTES / JACL_GRANULE / 8];

/* shared region pool: a monotonic byte cursor bumped atomically, plus the owning
 * worker of each handed-out region (so the sweep returns a cell to its owner). */
static long     jacl_region_bump;
static uint8_t  jacl_region_owner[JACL_NREGIONS];

/* per-worker (per-vCPU) heap: a current bump region + exact-size free lists. Only the
 * owning worker reads/writes its own entry on the allocation fast path (lock-free);
 * the sweep (STW, all workers quiesced) is the only other writer. */
typedef struct {
  uint32_t bump;                        /* next fresh byte offset in the current region */
  uint32_t limit;                       /* end offset of the current region */
  uint32_t freelist[JACL_NCLASS + 1];   /* per-class swept-cell stacks (0 = empty) */
} JaclWorker;
static JaclWorker jacl_worker[JACL_MAX_WORKERS];

/* a free cell stores the next free offset in the word right after its header */
static inline uint32_t* free_next_slot(JaclObj* o) { return (uint32_t*)((uint8_t*)o + sizeof(JaclObj)); }

static inline uint32_t round_granule(uint32_t n) { return (n + (JACL_GRANULE - 1)) & ~(JACL_GRANULE - 1); }

static inline void startbit_set(uint32_t off)   { uint32_t g = off / JACL_GRANULE; jacl_startmap[g >> 3] |=  (uint8_t)(1u << (g & 7)); }
static inline void startbit_clear(uint32_t off) { uint32_t g = off / JACL_GRANULE; jacl_startmap[g >> 3] &= (uint8_t)~(1u << (g & 7)); }
static inline int  startbit_test(uint32_t off)  { uint32_t g = off / JACL_GRANULE; return (jacl_startmap[g >> 3] >> (g & 7)) & 1; }

void jacl_heap_init(void) {
  jacl_region_bump = 0;
  for (int w = 0; w < JACL_MAX_WORKERS; w++) {
    jacl_worker[w].bump = 0;
    jacl_worker[w].limit = 0;
    for (uint32_t i = 0; i <= JACL_NCLASS; i++) jacl_worker[w].freelist[i] = 0;
  }
  memset(jacl_startmap, 0, sizeof(jacl_startmap));
  memset(jacl_region_owner, 0, sizeof(jacl_region_owner));
}

/* svm-llvm rejects a *constant* ptrtoint of a global address; route the cast
   through a noinline helper so it is a runtime instruction (Phase-0 finding). */
__attribute__((noinline)) static long jacl_pti(void *p) { return (long)p; }
long jacl_heap_lo(void) { return jacl_pti(&jacl_heap_mem[0]); }
long jacl_heap_hi(void) { return jacl_pti(&jacl_heap_mem[JACL_HEAP_BYTES]); }

/* Claim `bytes` (rounded up to whole regions) from the shared pool with a single
 * atomic bump — the only cross-worker synchronization on the allocation path. Marks
 * each claimed region as owned by `w` and returns the base offset, or
 * JACL_REGION_NONE when the pool is exhausted. */
static uint32_t jacl_grab_regions(int w, uint32_t bytes) {
  uint32_t need = round_granule(bytes);
  need = (need + (JACL_REGION_BYTES - 1)) & ~(JACL_REGION_BYTES - 1);
  long base = __vm_atomic_add((void*)&jacl_region_bump, (long)need);
  if (base < 0 || base + (long)need > (long)(JACL_NREGIONS * JACL_REGION_BYTES))
    return JACL_REGION_NONE;
  uint32_t b = (uint32_t)base;
  for (uint32_t off = b; off < b + need; off += JACL_REGION_BYTES)
    jacl_region_owner[off / JACL_REGION_BYTES] = (uint8_t)w;
  return b;
}

/* noinline is load-bearing for conservative GC: if the allocator is inlined into
 * the program, the optimizer can constant-fold object addresses (a deterministic
 * bump sequence over a static heap), so "roots" become rematerialized constants
 * that are never live pointers on the stack — invisible to gc.roots. A separately
 * compiled runtime (the real backend) is opaque by construction; noinline
 * reproduces that opacity here. */
/* Try to obtain a cell offset of `cell` bytes for the current worker — from its
 * exact-size free list, its current region's bump, or a freshly claimed region.
 * Returns 0 (the null sentinel offset) when the shared pool is exhausted. */
static uint32_t jacl_alloc_off(uint32_t cell) {
  int w = (int)__vm_vcpu_tls_get();
  if (w < 0 || w >= JACL_MAX_WORKERS) w = 0;   /* defensive: stay on worker 0 */
  JaclWorker* wh = &jacl_worker[w];
  uint32_t cls = cell / JACL_GRANULE;

  /* 1. exact-size free list — reuse a swept cell (single-owner, no lock) */
  if (cls <= JACL_NCLASS && wh->freelist[cls]) {
    uint32_t off = wh->freelist[cls];
    JaclObj* fo = (JaclObj*)&jacl_heap_mem[off];
    wh->freelist[cls] = *free_next_slot(fo);
    return off;
  }
  /* 2. bump within the worker's current region (lock-free fast path) */
  if (wh->bump + cell <= wh->limit) {
    uint32_t off = wh->bump;
    wh->bump += cell;
    return off;
  }
  /* 3. a large cell gets its own dedicated run of regions (never a TLAB) */
  if (cell > JACL_REGION_BYTES) {
    uint32_t base = jacl_grab_regions(w, cell);
    if (base == JACL_REGION_NONE) return 0;
    /* base==0 is unreachable here: small allocations always claim region 0 first. */
    return base;
  }
  /* 4. refill: claim a fresh region from the shared pool, then bump */
  uint32_t base = jacl_grab_regions(w, JACL_REGION_BYTES);
  if (base == JACL_REGION_NONE) return 0;
  wh->limit = base + JACL_REGION_BYTES;
  wh->bump  = (base == 0) ? JACL_GRANULE : base;   /* reserve offset 0 as the null sentinel */
  uint32_t off = wh->bump;
  wh->bump += cell;
  return off;
}

__attribute__((noinline))
void* jacl_alloc(uint32_t obj_type, uint32_t payload) {
  uint32_t cell = round_granule((uint32_t)sizeof(JaclObj) + payload);
  uint32_t off = jacl_alloc_off(cell);
  if (!off) {
    /* Out of room: a stop-the-world, conservative, non-moving collection. The calling
     * fiber is at this allocation — a safe point — and gc.roots conservatively scans
     * the whole control stack, so every live root is found without precise stack maps.
     * (Multi-vCPU quiescing of the other workers is P3.4c.) */
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
  uint32_t hi = (uint32_t)jacl_region_bump;          /* high-water of claimed regions */
  uint32_t nbytes = (hi / JACL_GRANULE + 7) / 8;
  for (uint32_t byte = 0; byte < nbytes; byte++) {
    uint8_t bits = jacl_startmap[byte];
    if (!bits) continue;
    for (uint32_t b = 0; b < 8; b++) {
      if (!(bits & (1u << b))) continue;
      uint32_t off = (byte * 8 + b) * JACL_GRANULE;
      if (off >= hi) break;
      JaclObj* o = (JaclObj*)&jacl_heap_mem[off];
      if (o->obj_type != JOBJ_FREE) n++;
    }
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
  uint32_t hi = (uint32_t)jacl_region_bump;
  uint32_t nbytes = (hi / JACL_GRANULE + 7) / 8;
  /* 1. clear marks (walk the start bitmap) */
  for (uint32_t byte = 0; byte < nbytes; byte++) {
    uint8_t bits = jacl_startmap[byte];
    if (!bits) continue;
    for (uint32_t b = 0; b < 8; b++) {
      if (!(bits & (1u << b))) continue;
      uint32_t off = (byte * 8 + b) * JACL_GRANULE;
      if (off >= hi) break;
      ((JaclObj*)&jacl_heap_mem[off])->mark = 0;
    }
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
  /* 4. sweep: unmarked live cells -> their owning worker's free list */
  long swept = 0;
  for (uint32_t byte = 0; byte < nbytes; byte++) {
    uint8_t bits = jacl_startmap[byte];
    if (!bits) continue;
    for (uint32_t b = 0; b < 8; b++) {
      if (!(bits & (1u << b))) continue;
      uint32_t off = (byte * 8 + b) * JACL_GRANULE;
      if (off >= hi) break;
      JaclObj* o = (JaclObj*)&jacl_heap_mem[off];
      if (o->obj_type != JOBJ_FREE && !o->mark) {
        uint32_t cls = o->size / JACL_GRANULE;
        int owner = jacl_region_owner[off / JACL_REGION_BYTES];
        o->obj_type = JOBJ_FREE;
        startbit_clear(off);
        if (cls <= JACL_NCLASS) {
          *free_next_slot(o) = jacl_worker[owner].freelist[cls];
          jacl_worker[owner].freelist[cls] = off;
        }
        swept++;
      }
    }
  }
  return swept;
}
