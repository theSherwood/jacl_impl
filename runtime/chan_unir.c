/* chan_unir.c — the channel backend over Unir edges (docs/UNIR_CHANNELS.md). Included by
 * chan.c when the runtime is built with JACL_UNIR, which also requires linking the unir unit
 * (runtime/unir/unir_cabi.ll; runtime/build.sh does both).
 *
 * A channel is one edge whose region this vat maps twice, once per end. Edge writes and reads
 * run with no timeout: a wait inside them parks the calling fiber (TEMEN §3.6 5a), and
 * worker_loop's repoll_blocked resumes it. End operations take no vat, so fibers may park in
 * different ends at once; chan.c's busy flag keeps each end to one operation. */
#include "unir/unir.h"

/* Bytes per ring slot (frame header included): frames carry up to 1016 bytes. */
#define JACL_UNIR_SLOT 1024u

/* Where this vat maps edge regions: page-aligned window space that holds nothing else, which
 * the unir binding requires. Each channel maps its region twice (one 64 KiB page each at the
 * default capacity), so 8 MiB holds about 60 channels; the binding never unmaps
 * (docs/UNIR_CHANNELS.md, Later). */
#define JACL_UNIR_MAP_BYTES (8u << 20)
static uint8_t jacl_unir_map[JACL_UNIR_MAP_BYTES] __attribute__((aligned(65536)));

/* The unit's heap: unir_host_alloc/free over a static pool outside the GC heap. Size classes
 * of 16 << k bytes (k < 13, so up to 64 KiB: a service's schema, which a client adopts into one
 * buffer, passes 4 KiB), each with a free list, carved from a bump arena.
 * Its lock is held only inside these two functions, which never park or call out. */
#define JACL_UNIR_POOL_BYTES (1u << 20)
#define JACL_UNIR_CLASSES 13
static uint8_t jacl_unir_pool[JACL_UNIR_POOL_BYTES] __attribute__((aligned(16)));
static uint32_t jacl_unir_pool_used;
static void *jacl_unir_free[JACL_UNIR_CLASSES];
static int32_t jacl_unir_pool_lock;   /* the pool's free lists and bump pointer */
static int32_t jacl_unir_open_lock;   /* the vat, while a channel opens (the unit allocates inside) */

static void unir_lock(int32_t *l) {
  while (__vm_atomic_cas32(l, 0, 1) != 0) {}
}
static void unir_unlock(int32_t *l) { __vm_atomic_store32(l, 0); }

static int unir_class(uint64_t size) {
  for (int k = 0; k < JACL_UNIR_CLASSES; k++)
    if (size <= (16u << k)) return k;
  return -1;
}

void *unir_host_alloc(uint64_t size, uint64_t align) {
  int k = unir_class(size);
  if (k < 0 || align > 16) return 0;
  unir_lock(&jacl_unir_pool_lock);
  void *p = jacl_unir_free[k];
  if (p) {
    jacl_unir_free[k] = *(void **)p;
  } else if (jacl_unir_pool_used + (16u << k) <= JACL_UNIR_POOL_BYTES) {
    p = jacl_unir_pool + jacl_unir_pool_used;
    jacl_unir_pool_used += 16u << k;
  }
  unir_unlock(&jacl_unir_pool_lock);
  return p;
}

void unir_host_free(void *ptr, uint64_t size, uint64_t align) {
  (void)align;
  int k = unir_class(size);
  if (!ptr || k < 0) return;
  unir_lock(&jacl_unir_pool_lock);
  *(void **)ptr = jacl_unir_free[k];
  jacl_unir_free[k] = ptr;
  unir_unlock(&jacl_unir_pool_lock);
}

static unir_vat *jacl_unir_vat;
static int jacl_stage(void);

/* The vat, created on first use: a spawned one's (a pipeline stage's, pipe_unir.c, or a
 * service's client's, service_unir.c) holds the arguments and capabilities its parent passed,
 * which unir_vat_child reads; a root's is unir_vat_root. 0 if the map area is not page-aligned.
 * Under the open lock. */
static unir_vat *unir_vat_get(void) {
  if (!jacl_unir_vat) {
    uint64_t base = (uint64_t)jacl_fb_pti(jacl_unir_map);
    if (base & 0xFFFF) return 0;
    uint64_t end = base + JACL_UNIR_MAP_BYTES;
    jacl_unir_vat = unir_vat_child(base, end);
    if (!jacl_unir_vat) jacl_unir_vat = unir_vat_root(base, end);
  }
  return jacl_unir_vat;
}

static int chan_be_open(uint32_t cap, void **w, void **r, uint32_t *max) {
  int64_t len = unir_edge_len(cap, JACL_UNIR_SLOT);
  int64_t mp = unir_edge_max_payload(cap, JACL_UNIR_SLOT);
  if (len < 0 || mp <= 0) return 0;
  unir_lock(&jacl_unir_open_lock);
  unir_vat *vat = unir_vat_get();
  int64_t edge = vat ? unir_region_create(vat, (uint64_t)len) : -1;
  unir_producer *p = edge >= 0 ? unir_producer_open(vat, edge, cap, JACL_UNIR_SLOT) : 0;
  unir_consumer *c = p ? unir_consumer_open(vat, edge, cap, JACL_UNIR_SLOT) : 0;
  unir_unlock(&jacl_unir_open_lock);
  if (!c) {
    if (p) unir_producer_free(p);
    return 0;
  }
  *w = p;
  *r = c;
  *max = (uint32_t)mp;
  return 1;
}

/* An ended edge: a clean end (complete, or the reader's cancel) or a sever. */
static int64_t chan_unir_ended(uint32_t ended) {
  return (ended & 3) == 3 ? CHAN_BE_SEVERED : CHAN_BE_END;
}

static int64_t chan_be_write(void *w, const uint8_t *p, uint32_t n) {
  int64_t s = unir_producer_write((unir_producer *)w, 1, p, n, -1);
  if (s == UNIR_EENDED) return chan_unir_ended(unir_producer_ended((unir_producer *)w));
  return s < 0 ? CHAN_BE_FAILED : 0;
}

static unir_schema *jv_schema(void);

/* One attempt, waiting up to `timeout_ns`, at read end `c`'s next frame on its edge `h`: its
 * length, with the frame in `c->tail`, or a UNIR_E* code. A CHAN_UNKNOWN end first learns what
 * the edge carries (unir spec §3): an offer on substream 0 is accepted as the value schema, and
 * the end then reads verified messages; any other first frame means bytes, read as they are. A
 * writer offering another schema is refused (UNIR_EREFUSED). */
static int64_t chan_unir_next(JaclChan *c, unir_consumer *h, int64_t timeout_ns) {
  if (c->typed == CHAN_UNKNOWN) {
    int64_t sub = unir_consumer_next_substream(h, timeout_ns);
    if (sub < 0) return sub;
    if (sub == 0) {
      /* The writer sends its whole offer at once, so the accept waits no longer than it takes. */
      unir_schema *s = jv_schema();
      int64_t st = UNIR_EINVALID;
      c->agree = s ? unir_consumer_accept(h, s, -1, &st) : 0;
      if (!c->agree) return st < 0 ? st : UNIR_EINVALID;
      c->typed = CHAN_VALUES;
    } else {
      c->typed = CHAN_BYTES;
    }
  }
  if (c->typed == CHAN_BYTES) return unir_consumer_read(h, c->tail, c->max, timeout_ns, 0);
  uint32_t entry = 0;
  const uint8_t *frame = 0;
  int64_t n = unir_agreement_recv((unir_agreement *)c->agree, h, timeout_ns, &entry, &frame);
  if (n < 0) return n;
  if ((uint64_t)n > c->max) {   /* a materialized message past a frame; none with one schema */
    unir_consumer_sever(h, 3 /* malformed-frame */);
    return UNIR_EENDED;
  }
  memcpy(c->tail, frame, (size_t)n);   /* the verified message, private to this vat */
  return n;
}

/* Frees a read end's agreement, once nothing reads it. */
static void chan_unir_forget(JaclChan *c) {
  if (c->agree) unir_agreement_free((unir_agreement *)c->agree);
  c->agree = 0;
}

static int64_t chan_be_read(JaclChan *c) {
  unir_consumer *r = (unir_consumer *)c->handle;
  int64_t s = chan_unir_next(c, r, -1);
  if (s == UNIR_EENDED) return chan_unir_ended(unir_consumer_ended(r));
  return s < 0 ? CHAN_BE_FAILED : s;
}

static void chan_be_close(void *h, uint32_t end) {
  if (end == CHAN_W) unir_producer_complete((unir_producer *)h);
  else unir_consumer_cancel((unir_consumer *)h);
}

/* The cause of a sever (unir-wire Cause), or -1. */
static int chan_be_cause(void *h, uint32_t end) {
  uint32_t e = end == CHAN_W ? unir_producer_ended((unir_producer *)h)
                             : unir_consumer_ended((unir_consumer *)h);
  return (e & 3) == 3 ? (int)((e >> 2) & 0xF) : -1;
}
