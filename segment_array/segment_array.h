/* segment_array.h - Resizable array with stable pointers (include-as-template)
 *
 * A segment array provides O(1) random access and O(1) amortized append while
 * guaranteeing that element pointers remain stable (no reallocation moves).
 *
 * Define SEG_ARRAY_T (element type) and SEG_ARRAY_NAME (name prefix) before including.
 * Can be included multiple times with different T/NAME values.
 *
 * Example:
 *   #define SEG_ARRAY_T    int
 *   #define SEG_ARRAY_NAME int_seg
 *   #include "segment_array.h"
 *   // Now have: int_seg_t, int_seg_create(allocator), int_seg_push(), int_seg_get(), etc.
 */

#ifndef SEG_ARRAY_T
#error "SEG_ARRAY_T must be defined before including segment_array.h"
#define SEG_ARRAY_T int /* suppress further errors */
#endif

#ifndef SEG_ARRAY_NAME
#error "SEG_ARRAY_NAME must be defined before including segment_array.h"
#define SEG_ARRAY_NAME _seg_err
#endif

/* === Concatenation helpers === */
#define SA_CAT(a, b)  SA_XCAT(a, b)
#define SA_XCAT(a, b) a##b
#define SA_NS(name)   SA_CAT(SEG_ARRAY_NAME, name)

/* === Generated names === */
#define SA_T            SA_NS(_t)
#define SA_CREATE       SA_NS(_create)
#define SA_FREE         SA_NS(_free)
#define SA_COUNT        SA_NS(_count)
#define SA_PUSH         SA_NS(_push)
#define SA_GET          SA_NS(_get)
#define SA_SWAP_REMOVE  SA_NS(_swap_remove)
#define SA_ITER_T       SA_NS(_iter_t)
#define SA_ITER_BEGIN   SA_NS(_iter_begin)
#define SA_ITER_NEXT    SA_NS(_iter_next)
#define SA_ITER_VALID   SA_NS(_iter_valid)

/* === One-time definitions === */
#ifndef SEG_ARRAY_CONFIG_DEFINED
#define SEG_ARRAY_CONFIG_DEFINED

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../platform/platform.h"

/* Max segments: 26 segments with 6 skipped can hold ~4 billion items */
#define SA_MAX_SEGMENTS        26
#define SA_SMALL_SEGMENTS_SKIP 6 /* First segment is 64 items (2^6) */

#endif /* SEG_ARRAY_CONFIG_DEFINED */

/* === Types === */

typedef struct SA_T {
  const Allocator* allocator;
  uint32_t         count;
  int              used_segments;
  uint8_t*         segments[SA_MAX_SEGMENTS];
} SA_T;

typedef struct SA_ITER_T {
  SA_T*    arr;
  uint32_t index;
} SA_ITER_T;

/* === Internal helpers === */

static inline uint32_t SA_NS(_log2i)(uint64_t x) {
  return (uint32_t)(8 * sizeof(unsigned long long) - __builtin_clzll(x) - 1);
}

static inline uint32_t SA_NS(_slots_in_segment)(int segment_index) {
  return (1u << SA_SMALL_SEGMENTS_SKIP) << segment_index;
}

static inline uint32_t SA_NS(_capacity_for_segments)(int segment_count) {
  return ((1u << SA_SMALL_SEGMENTS_SKIP) << segment_count) - (1u << SA_SMALL_SEGMENTS_SKIP);
}

/* === Functions === */

static inline SA_T* SA_CREATE(const Allocator* allocator) {
  SA_T* sa = (SA_T*)allocator->alloc(allocator->ctx, sizeof(SA_T));
  if (!sa) return NULL;
  sa->allocator = allocator;
  sa->count = 0;
  sa->used_segments = 0;
  memset(sa->segments, 0, sizeof(sa->segments));
  return sa;
}

static inline void SA_FREE(SA_T* sa) {
  if (!sa) return;
  const Allocator* allocator = sa->allocator;
  for (int i = 0; i < sa->used_segments; i++) {
    allocator->free(allocator->ctx, sa->segments[i]);
  }
  allocator->free(allocator->ctx, sa);
}

static inline uint32_t SA_COUNT(SA_T* sa) {
  return sa ? sa->count : 0;
}

static inline SEG_ARRAY_T* SA_GET(SA_T* sa, uint32_t index) {
  if (!sa || index >= sa->count) return NULL;
  int      segment = SA_NS(_log2i)((index >> SA_SMALL_SEGMENTS_SKIP) + 1);
  uint32_t slot    = index - SA_NS(_capacity_for_segments)(segment);
  return (SEG_ARRAY_T*)(sa->segments[segment] + slot * sizeof(SEG_ARRAY_T));
}

static inline SEG_ARRAY_T* SA_PUSH(SA_T* sa, SEG_ARRAY_T value) {
  if (!sa) return NULL;

  /* Allocate new segment if needed */
  if (sa->count >= SA_NS(_capacity_for_segments)(sa->used_segments)) {
    if (sa->used_segments >= SA_MAX_SEGMENTS) return NULL;
    size_t segment_size = sizeof(SEG_ARRAY_T) * SA_NS(_slots_in_segment)(sa->used_segments);
    sa->segments[sa->used_segments] = (uint8_t*)sa->allocator->alloc(sa->allocator->ctx, segment_size);
    if (!sa->segments[sa->used_segments]) return NULL;
    sa->used_segments++;
  }

  uint32_t     index = sa->count++;
  SEG_ARRAY_T* ptr   = SA_GET(sa, index);
  *ptr = value;
  return ptr;
}

static inline int SA_SWAP_REMOVE(SA_T* sa, uint32_t index) {
  if (!sa || index >= sa->count) return -1;
  if (index != sa->count - 1) {
    *SA_GET(sa, index) = *SA_GET(sa, sa->count - 1);
  }
  sa->count--;
  return 0;
}

/* === Iterator functions === */

static inline SA_ITER_T SA_ITER_BEGIN(SA_T* arr) {
  SA_ITER_T it = {arr, 0};
  return it;
}

static inline int SA_ITER_VALID(SA_ITER_T* it) {
  return it && it->arr && it->index < it->arr->count;
}

static inline SEG_ARRAY_T* SA_ITER_NEXT(SA_ITER_T* it) {
  if (!SA_ITER_VALID(it)) return NULL;
  SEG_ARRAY_T* ptr = SA_GET(it->arr, it->index);
  it->index++;
  return ptr;
}

/* === Cleanup === */

#undef SEG_ARRAY_T
#undef SEG_ARRAY_NAME
#undef SA_CAT
#undef SA_XCAT
#undef SA_NS
#undef SA_T
#undef SA_CREATE
#undef SA_FREE
#undef SA_COUNT
#undef SA_PUSH
#undef SA_GET
#undef SA_SWAP_REMOVE
#undef SA_ITER_T
#undef SA_ITER_BEGIN
#undef SA_ITER_NEXT
#undef SA_ITER_VALID
