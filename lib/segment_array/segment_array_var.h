/* segment_array_var.h - Resizable array with stable pointers and a RUNTIME
 * element size.
 *
 * Companion to segment_array.h. The templated version fixes the element size
 * at instantiation via sizeof(T); this one carries elem_size as a runtime
 * field so a single type can back collections whose element width is only
 * known at runtime (e.g. JACL's [Arr T] where T may be i32, f64, or a struct
 * of arbitrary size). See ARR_DESIGN.md (M4a).
 *
 * Same guarantees as segment_array.h: O(1) random access, O(1) amortized
 * append, stable element pointers (no reallocation moves). Segments are
 * sized in WHOLE elements (segment k holds 2^(6+k) elements of elem_size
 * bytes each), so an element never straddles a segment boundary regardless
 * of elem_size.
 *
 * Header-only; safe to include once into a single translation unit. */

#ifndef SEGMENT_ARRAY_VAR_H
#define SEGMENT_ARRAY_VAR_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../platform/platform.h"

#define SA_VAR_MAX_SEGMENTS 26
#define SA_VAR_SKIP         6   /* first segment holds 2^6 = 64 elements */

typedef struct {
  const Allocator* allocator;
  uint32_t         count;
  uint32_t         elem_size;     /* bytes per element (runtime) */
  int              used_segments;
  int              last_seg;       /* segment holding element count-1 */
  uint32_t         last_seg_base;  /* element index of last_seg's slot 0 */
  uint32_t         next_grow_at;   /* count at which push advances a segment */
  uint8_t*         segments[SA_VAR_MAX_SEGMENTS];
} sa_var_t;

static inline uint32_t sa_var__log2i(uint64_t x) {
  return (uint32_t)(8 * sizeof(unsigned long long) - __builtin_clzll(x) - 1);
}

static inline uint32_t sa_var__slots_in_segment(int segment_index) {
  return (1u << SA_VAR_SKIP) << segment_index;
}

static inline uint32_t sa_var__capacity_for_segments(int segment_count) {
  return ((1u << SA_VAR_SKIP) << segment_count) - (1u << SA_VAR_SKIP);
}

/* In-place init (no allocation of the header itself — the caller owns it,
 * e.g. embedded inside a GC object). */
static inline void sa_var_init(sa_var_t* sa, const Allocator* allocator,
                               uint32_t elem_size) {
  sa->allocator     = allocator;
  sa->count         = 0;
  sa->elem_size     = elem_size;
  sa->used_segments = 0;
  sa->last_seg      = -1;
  sa->last_seg_base = 0;
  sa->next_grow_at  = 0;
  memset(sa->segments, 0, sizeof(sa->segments));
}

static inline uint32_t sa_var_count(const sa_var_t* sa) {
  return sa ? sa->count : 0;
}

/* Pointer to element `index`'s bytes, or NULL if out of bounds. */
static inline uint8_t* sa_var_get(sa_var_t* sa, uint32_t index) {
  if (!sa || index >= sa->count) return NULL;
  int      segment = sa_var__log2i((index >> SA_VAR_SKIP) + 1);
  uint32_t slot    = index - sa_var__capacity_for_segments(segment);
  return sa->segments[segment] + (size_t)slot * sa->elem_size;
}

/* Internal: ensure capacity for one more element and return its slot ptr,
 * advancing the cursor. Returns NULL on OOM / segment exhaustion. */
static inline uint8_t* sa_var__next_slot(sa_var_t* sa) {
  if (sa->count >= sa->next_grow_at) {
    int next_seg = sa->last_seg + 1;
    if (next_seg >= sa->used_segments) {
      if (sa->used_segments >= SA_VAR_MAX_SEGMENTS) return NULL;
      size_t seg_bytes = (size_t)sa->elem_size * sa_var__slots_in_segment(next_seg);
      sa->segments[next_seg] = (uint8_t*)sa->allocator->alloc(sa->allocator->ctx, seg_bytes);
      if (!sa->segments[next_seg]) return NULL;
      sa->used_segments = next_seg + 1;
    }
    sa->last_seg_base = sa->next_grow_at;
    sa->last_seg      = next_seg;
    sa->next_grow_at  = sa->last_seg_base + sa_var__slots_in_segment(next_seg);
  }
  uint32_t slot = sa->count - sa->last_seg_base;
  uint8_t* ptr  = sa->segments[sa->last_seg] + (size_t)slot * sa->elem_size;
  sa->count++;
  return ptr;
}

/* Append: copy elem_size bytes from src into a new slot. Returns the slot
 * pointer, or NULL on OOM. */
static inline uint8_t* sa_var_push(sa_var_t* sa, const void* src) {
  if (!sa) return NULL;
  uint8_t* ptr = sa_var__next_slot(sa);
  if (ptr && src) memcpy(ptr, src, sa->elem_size);
  return ptr;
}

/* Append a zero-filled element (used to grow a gap on lenient set). */
static inline uint8_t* sa_var_push_zero(sa_var_t* sa) {
  if (!sa) return NULL;
  uint8_t* ptr = sa_var__next_slot(sa);
  if (ptr) memset(ptr, 0, sa->elem_size);
  return ptr;
}

/* In-place write of an existing element. NULL if out of bounds. Does not
 * grow (growth is the caller's concern, via push). */
static inline uint8_t* sa_var_set(sa_var_t* sa, uint32_t index, const void* src) {
  uint8_t* slot = sa_var_get(sa, index);
  if (slot && src) memcpy(slot, src, sa->elem_size);
  return slot;
}

/* Order-preserving tail remove. Copies the removed element to *out (if
 * non-NULL) and returns 0; returns -1 if empty. Mirrors segment_array.h's
 * SA_POP cursor-slide across segment boundaries. */
static inline int sa_var_pop(sa_var_t* sa, void* out) {
  if (!sa || sa->count == 0) return -1;
  uint32_t last_slot = (sa->count - 1) - sa->last_seg_base;
  uint8_t* src       = sa->segments[sa->last_seg] + (size_t)last_slot * sa->elem_size;
  if (out) memcpy(out, src, sa->elem_size);
  sa->count--;
  if (sa->last_seg > 0 && sa->count == sa->last_seg_base) {
    sa->next_grow_at  = sa->last_seg_base;
    sa->last_seg--;
    sa->last_seg_base = sa_var__capacity_for_segments(sa->last_seg);
  }
  return 0;
}

/* Free the malloc'd segment backing store (not the header). */
static inline void sa_var_free_segments(sa_var_t* sa) {
  if (!sa) return;
  for (int i = 0; i < sa->used_segments; i++) {
    sa->allocator->free(sa->allocator->ctx, sa->segments[i]);
    sa->segments[i] = NULL;
  }
  sa->used_segments = 0;
}

#endif /* SEGMENT_ARRAY_VAR_H */
