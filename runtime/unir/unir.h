/* Unir vats and edges behind a C ABI (issues #18, #19). A C program llvm-links the
 * `unir-cabi` unit (unir_cabi.ll) before translation.
 *
 * Returns: a non-negative value on success, else one of UNIR_E*. After UNIR_EENDED, the
 * end's `*_ended` says how it ended. */
#ifndef UNIR_H
#define UNIR_H

#include <stdint.h>

typedef struct unir_vat unir_vat;
typedef struct unir_producer unir_producer;
typedef struct unir_consumer unir_consumer;

/* One named capability in a child's endowment. */
typedef struct unir_grant {
  const char *name;
  uint64_t name_len;
  int64_t cap;
} unir_grant;

#define UNIR_EENDED (-1)        /* the edge has ended; nothing more moves */
#define UNIR_ESTALLED (-2)      /* the peer rang nothing within the timeout; still open */
#define UNIR_ETOO_LARGE (-3)    /* payload over the geometry's maximum; edge unaffected */
#define UNIR_ETOO_SMALL (-4)    /* the next frame does not fit the buffer; it stays unread */
#define UNIR_EGEOMETRY (-5)     /* invalid geometry, or a region too small for it */
#define UNIR_ESUBSTRATE (-6)    /* a vat operation failed */
#define UNIR_EINVALID (-7)      /* a malformed argument */
#define UNIR_ENOCAP (-8)        /* the vat was endowed with nothing under that name */

/* `*_ended`: 0 while open; else bits 0-1 kind (1 complete, 2 cancelled, 3 severed), for a
 * sever bits 2-5 the cause (unir-wire Cause) and bit 6 the side (1 = consumer). */
#define UNIR_ENDED_COMPLETE 1u
#define UNIR_ENDED_CANCELLED 2u

/* The embedder defines these: the unit's heap (its handles and the vat's bookkeeping). A
 * translated library has no allocator of its own. unir_host_alloc returns NULL on failure. */
void *unir_host_alloc(uint64_t size, uint64_t align);
void unir_host_free(void *ptr, uint64_t size, uint64_t align);

/* The root vat. Regions map in [map_base, map_end), which must be free window space. */
unir_vat *unir_vat_root(uint64_t map_base, uint64_t map_end);

/* A spawned child's vat, holding the arguments its parent passed (NULL if they are
 * malformed). Only a child may call it: a root's argument area holds the host's argv. */
unir_vat *unir_vat_child(uint64_t map_base, uint64_t map_end);

/* Copies the vat's arguments into `buf`; returns their length, or UNIR_ETOO_SMALL. */
int64_t unir_vat_args(const unir_vat *vat, uint8_t *buf, uint64_t cap);

/* The capability the vat was endowed with under `name`, or UNIR_ENOCAP. */
int64_t unir_endowed(const unir_vat *vat, const char *name, uint64_t name_len);

/* A fresh zeroed region of `len` bytes; returns its capability. */
int64_t unir_region_create(unir_vat *vat, uint64_t len);

/* Spawns a child running `module` (a module capability whose export 0 is a child entry) in
 * a window of its own, 2^size_log2 bytes (the module's declared memory, paid from the vat's
 * Budget). It starts with `args` (at most 16,224 bytes; see unir_vat_args), exactly
 * `grants`, and `fuel`. Returns the child's capability. */
int64_t unir_spawn(unir_vat *vat, int64_t module, uint32_t size_log2, const uint8_t *args,
                   uint64_t args_len, const unir_grant *grants, uint32_t grants_n,
                   uint64_t fuel);

/* Waits for a child and returns its status; a negative status reads as an error code. */
int64_t unir_join(unir_vat *vat, int64_t child);

/* The region length an edge of this geometry needs; UNIR_EGEOMETRY if invalid. */
int64_t unir_edge_len(uint32_t capacity, uint32_t slot_size);

/* The largest payload a frame of this geometry carries. */
int64_t unir_edge_max_payload(uint32_t capacity, uint32_t slot_size);

/* Maps the edge region `cap` and attaches as its producer (NULL on failure). */
unir_producer *unir_producer_open(unir_vat *vat, int64_t cap, uint32_t capacity,
                                  uint32_t slot_size);

/* End operations take no vat: they wait on and notify only their own edge's words, so
 * different ends may be used concurrently, e.g. by fibers parked in them. One end, one
 * operation at a time. */

/* Writes one frame, parking while there is no credit; a negative timeout never stalls. */
int64_t unir_producer_write(unir_producer *p, uint16_t substream, const uint8_t *buf,
                            uint64_t len, int64_t timeout_ns);
int64_t unir_producer_complete(unir_producer *p);
int64_t unir_producer_sever(unir_producer *p, uint32_t cause);
uint32_t unir_producer_ended(const unir_producer *p);
void unir_producer_free(unir_producer *p);

/* Maps the edge region `cap`, attaches as its consumer, and grants the first credit. */
unir_consumer *unir_consumer_open(unir_vat *vat, int64_t cap, uint32_t capacity,
                                  uint32_t slot_size);
/* Reads the next frame into `buf`, parking while there is none; returns its length and,
 * if `substream` is not NULL, stores its substream. */
int64_t unir_consumer_read(unir_consumer *c, uint8_t *buf, uint64_t cap, int64_t timeout_ns,
                           uint16_t *substream);
int64_t unir_consumer_cancel(unir_consumer *c);
/* Grants no credit past what this end has read, so the producer parks once the ring is
 * full (job control's suspend, by backpressure); resume lifts that. */
int64_t unir_consumer_suspend(unir_consumer *c);
int64_t unir_consumer_resume(unir_consumer *c);
int64_t unir_consumer_sever(unir_consumer *c, uint32_t cause);
uint32_t unir_consumer_ended(const unir_consumer *c);
void unir_consumer_free(unir_consumer *c);

#endif
