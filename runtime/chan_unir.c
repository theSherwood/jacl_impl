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
 * of 16 << k bytes (k < 15, so up to 256 KiB: a legacy job's screen, which the adapter sends as
 * one frame of up to 100 KiB, is read into one buffer, as a service's schema is, which passes
 * 4 KiB), each with a free list, carved from a bump arena. A request the pool cannot serve fails
 * the unit's allocation, which traps.
 * Its lock is held only inside these two functions, which never park or call out. */
#define JACL_UNIR_POOL_BYTES (4u << 20)
#define JACL_UNIR_CLASSES 15
static uint8_t jacl_unir_pool[JACL_UNIR_POOL_BYTES] __attribute__((aligned(16)));
static uint32_t jacl_unir_pool_used;
static void *jacl_unir_free[JACL_UNIR_CLASSES];
static int32_t jacl_unir_pool_lock;   /* the pool's free lists and bump pointer */

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
static unir_vat *unir_vat_get(void);

/* The vat, one call at a time: of unir.h's calls only an edge's end operations and a
 * notification's take no vat, and this runtime's fibers run on several workers. A call waits its
 * turn parked, never spinning against a fiber parked inside one. A call that waits for a peer (a
 * service's reply, a child that starts or ends) holds the vat meanwhile; a wait that may be long
 * (a service's next event, a legacy job's next screen) does not: it waits on the vat's
 * notification, or polls, giving the vat back between polls. The vat, or 0. */
static int32_t jacl_unir_vat_lock;
static unir_vat *vat_take(void) {
  while (__vm_atomic_cas32(&jacl_unir_vat_lock, 0, 1) != 0)
    (void)__vm_wait32(&jacl_unir_vat_lock, 1, 1000000);
  return unir_vat_get();
}
static void vat_give(void) {
  __vm_atomic_store32(&jacl_unir_vat_lock, 0);
  (void)__vm_notify(&jacl_unir_vat_lock, 1);
}

/* Waits `ns` nanoseconds parked, as a poll's interval. */
static int32_t jacl_unir_idle;
static void vat_idle(long ns) { (void)__vm_wait32(&jacl_unir_idle, 0, ns); }

/* The vat's notification (unir decision #208.1), once a service is opened with it ([service NAME
 * N]): the service's events signal it while it is armed, and this runtime signals it itself
 * (vat_stir, pool_wake), so an idle worker waits on it rather than in ticks (vat_wait_idle; #219).
 * A wait takes every badge, so one worker waits on it at a time. */
static unir_notification *jacl_unir_note;
static char jacl_unir_note_name[64];
static int32_t jacl_unir_note_waiting;
/* The badge this runtime signals itself with; a root's are small. */
#define JACL_UNIR_OWN (1ull << 63)
/* The longest one wait lasts: an unbounded one is made of these (temen#1711). */
#define JACL_UNIR_SLICE_NS 1000000000L

/* Moves whenever a service opened with the notification may have an event, or a task wrote to a
 * transcript or ended: what a fiber waiting for a service's next event parks on (vat_await). */
static int32_t jacl_unir_events;
/* Moves when a task writes to a transcript or ends (vat_stir): what ends a timed wait early. */
static int32_t jacl_unir_stirs;

/* Wakes the worker waiting on the notification, if one is open. */
static void vat_wake(void) {
  if (jacl_unir_note) (void)unir_notification_signal(jacl_unir_note, JACL_UNIR_OWN);
}

/* What a fiber waiting for a service's next event wants to see: a task wrote to a transcript, or
 * ended. Wakes it, and the worker waiting on the notification for it. */
static void vat_stir(void) {
  (void)__vm_atomic_add32(&jacl_unir_stirs, 1);
  (void)__vm_atomic_add32(&jacl_unir_events, 1);
  (void)__vm_notify(&jacl_unir_events, 1 << 30);
  vat_wake();
}

/* An idle worker's wait on the notification, up to `ns` (negative: as long as it takes), if one
 * is open and no other worker waits on it: 1 if it waited. A badge from outside, a service's
 * event, moves jacl_unir_events, so the fibers parked for one see it when the worker re-polls
 * them; this runtime's own signals moved it already. */
static int vat_wait_idle(long ns) {
  if (!jacl_unir_note || __vm_atomic_cas32(&jacl_unir_note_waiting, 0, 1) != 0) return 0;
  uint64_t badges = 0;
  (void)unir_notification_wait(jacl_unir_note, ns, &badges);
  __vm_atomic_store32(&jacl_unir_note_waiting, 0);
  if (badges & ~JACL_UNIR_OWN) {
    (void)__vm_atomic_add32(&jacl_unir_events, 1);
    (void)__vm_notify(&jacl_unir_events, 1 << 30);
  }
  return 1;
}

/* Parks the calling fiber until jacl_unir_events moves from `seen` or `ns` passes; 2 if it
 * passed. `mark` is how long its job waits on the notification, in µs (-1: as long as it takes),
 * so its worker, idle, waits there for it rather than re-polling it in ticks (idle_ns); 0 for a
 * poll's interval, which needs the ticks. */
static int vat_await(int32_t seen, long ns, int32_t mark) {
  if (mark) jacl_event_wait(mark);
  int w = __vm_wait32(&jacl_unir_events, seen, ns);
  if (mark) jacl_event_wait(0);
  return w;
}

/* Opens the vat's notification, the one it was endowed with under `name`, unless it is open:
 * under the vat's lock. 0, or why not. A vat has one: another name is refused. */
static const char *vat_note_open(unir_vat *vat, const char *name, uint32_t len) {
  if (jacl_unir_note) {
    uint32_t k = 0;
    while (k < len && jacl_unir_note_name[k] == name[k]) k++;
    return k == len && !jacl_unir_note_name[k] ? 0 : "the vat has one notification";
  }
  if (len >= sizeof jacl_unir_note_name) return "the notification's name is too long";
  unir_notification *n = vat ? unir_notification_open(vat, name, len) : 0;
  if (!n) return "no notification granted by that name";
  for (uint32_t k = 0; k < len; k++) jacl_unir_note_name[k] = name[k];
  jacl_unir_note = n;
  return 0;
}

/* The vat, created on first use: a spawned one's (a pipeline stage's, pipe_unir.c, or a
 * service's client's, service_unir.c) holds the arguments and capabilities its parent passed,
 * which unir_vat_child reads; a root's is unir_vat_root. 0 if the map area is not page-aligned.
 * Under the vat's lock. */
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
  unir_vat *vat = vat_take();
  int64_t edge = vat ? unir_region_create(vat, (uint64_t)len) : -1;
  unir_producer *p = edge >= 0 ? unir_producer_open(vat, edge, cap, JACL_UNIR_SLOT) : 0;
  unir_consumer *c = p ? unir_consumer_open(vat, edge, cap, JACL_UNIR_SLOT) : 0;
  vat_give();
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
 * writer offering another schema is refused (UNIR_EREFUSED). A CHAN_ADOPT end adopts whatever
 * schema is offered instead, and keeps each message's catalog entry in `c->entry`. */
static int64_t chan_unir_next(JaclChan *c, unir_consumer *h, int64_t timeout_ns) {
  if (c->typed == CHAN_UNKNOWN || c->typed == CHAN_ADOPT) {
    int64_t sub = unir_consumer_next_substream(h, timeout_ns);
    if (sub < 0) return sub;
    if (sub == 0) {
      /* The writer sends its whole offer at once, so the accept waits no longer than it takes. */
      int64_t st = UNIR_EINVALID;
      if (c->typed == CHAN_ADOPT) {
        c->agree = unir_consumer_adopt(h, -1, &st);
        if (!c->agree) return st < 0 ? st : UNIR_EINVALID;
        c->typed = CHAN_ADOPTED;
      } else {
        unir_schema *s = jv_schema();
        c->agree = s ? unir_consumer_accept(h, s, -1, &st) : 0;
        if (!c->agree) return st < 0 ? st : UNIR_EINVALID;
        c->typed = CHAN_VALUES;
      }
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
  c->entry = entry;
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
