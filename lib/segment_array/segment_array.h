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
#define SA_SORT         SA_NS(_sort)
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
  /* Cached cursor for the last element. Maintained on push and swap_remove
   * so both hot paths skip _get's log2 decode for the always-known last
   * element:
   *   - last_seg, last_seg_base: the segment that holds element count-1
   *     and its slot-0 global offset (= capacity_for_segments(last_seg)).
   *   - next_grow_at: count value at which push must advance to the next
   *     segment (= last_seg_base + slots_in_segment(last_seg)). The hot
   *     bound check is `count >= next_grow_at`, one load + compare.
   *
   * Initial state: last_seg = -1, both base and grow_at = 0, so the very
   * first push trips the grow path and allocates segment 0. */
  int              last_seg;
  uint32_t         last_seg_base;
  uint32_t         next_grow_at;
  uint8_t*         segments[SA_MAX_SEGMENTS];
} SA_T;

/* Iterator caches the current segment's element span as a [cur_ptr, end_ptr)
 * pointer pair. The hot path is `cur_ptr < end_ptr` + post-increment — no
 * separate index counter to maintain, no per-element segment-decode. */
typedef struct SA_ITER_T {
  SA_T*        arr;
  SEG_ARRAY_T* cur_ptr;     /* next element to return */
  SEG_ARRAY_T* end_ptr;     /* exclusive end of the current segment's span,
                             * clamped at arr->count for the trailing segment */
  int          cur_segment; /* index of the current segment */
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
  sa->allocator     = allocator;
  sa->count         = 0;
  sa->used_segments = 0;
  sa->last_seg      = -1;
  sa->last_seg_base = 0;
  sa->next_grow_at  = 0;
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

  /* Single bound check covers all three cases: first push (next_grow_at=0
   * trips on count=0), normal grow into a new segment, and the cold case
   * where a prior swap_remove cycle slid last_seg down and a refill now
   * needs to cross back into an existing segment. */
  if (sa->count >= sa->next_grow_at) {
    int next_seg = sa->last_seg + 1;
    if (next_seg >= sa->used_segments) {
      /* No segment to reuse — allocate. */
      if (sa->used_segments >= SA_MAX_SEGMENTS) return NULL;
      size_t segment_size = sizeof(SEG_ARRAY_T) * SA_NS(_slots_in_segment)(next_seg);
      sa->segments[next_seg] = (uint8_t*)sa->allocator->alloc(sa->allocator->ctx, segment_size);
      if (!sa->segments[next_seg]) return NULL;
      sa->used_segments = next_seg + 1;
    }
    sa->last_seg_base = sa->next_grow_at;
    sa->last_seg      = next_seg;
    sa->next_grow_at  = sa->last_seg_base + SA_NS(_slots_in_segment)(next_seg);
  }

  uint32_t     slot = sa->count - sa->last_seg_base;
  SEG_ARRAY_T* ptr  = (SEG_ARRAY_T*)(sa->segments[sa->last_seg] + slot * sizeof(SEG_ARRAY_T));
  *ptr = value;
  sa->count++;
  return ptr;
}

static inline int SA_SWAP_REMOVE(SA_T* sa, uint32_t index) {
  if (!sa || index >= sa->count) return -1;
  if (index != sa->count - 1) {
    /* Source is always the last element. Address it directly via the
     * cached last_seg/last_seg_base instead of routing through _get, which
     * would re-derive the segment via log2. Destination still goes through
     * _get because `index` is arbitrary. */
    uint32_t     last_slot = (sa->count - 1) - sa->last_seg_base;
    SEG_ARRAY_T* src       = (SEG_ARRAY_T*)(sa->segments[sa->last_seg] + last_slot * sizeof(SEG_ARRAY_T));
    *SA_GET(sa, index) = *src;
  }
  sa->count--;
  /* If we just emptied the last segment, slide the cursor down one. The
   * segment itself stays allocated so a future push refills it without a
   * malloc. last_seg can't drop below 0 here: when count went from 1 to 0
   * we were on segment 0 (last_seg_base == 0), and 0 != 0 fails the guard
   * because last_seg > 0 is required. */
  if (sa->last_seg > 0 && sa->count == sa->last_seg_base) {
    sa->next_grow_at  = sa->last_seg_base;
    sa->last_seg--;
    sa->last_seg_base = SA_NS(_capacity_for_segments)(sa->last_seg);
  }
  return 0;
}

/* === Sort (heapsort) === */

static inline void SA_SORT(SA_T* sa, int (*cmp)(const SEG_ARRAY_T*, const SEG_ARRAY_T*)) {
  if (!sa || sa->count < 2) return;

  uint32_t n = sa->count;

  /* Build max-heap (sift down from last parent to root) */
  for (uint32_t start = n / 2; start > 0; start--) {
    uint32_t root = start - 1;
    while (2 * root + 1 < n) {
      uint32_t child = 2 * root + 1;
      if (child + 1 < n && cmp(SA_GET(sa, child), SA_GET(sa, child + 1)) < 0)
        child++;
      if (cmp(SA_GET(sa, root), SA_GET(sa, child)) < 0) {
        SEG_ARRAY_T tmp = *SA_GET(sa, root);
        *SA_GET(sa, root) = *SA_GET(sa, child);
        *SA_GET(sa, child) = tmp;
        root = child;
      } else {
        break;
      }
    }
  }

  /* Extract elements from heap */
  for (uint32_t end = n - 1; end > 0; end--) {
    SEG_ARRAY_T tmp = *SA_GET(sa, 0);
    *SA_GET(sa, 0) = *SA_GET(sa, end);
    *SA_GET(sa, end) = tmp;

    uint32_t root = 0;
    while (2 * root + 1 < end) {
      uint32_t child = 2 * root + 1;
      if (child + 1 < end && cmp(SA_GET(sa, child), SA_GET(sa, child + 1)) < 0)
        child++;
      if (cmp(SA_GET(sa, root), SA_GET(sa, child)) < 0) {
        SEG_ARRAY_T t = *SA_GET(sa, root);
        *SA_GET(sa, root) = *SA_GET(sa, child);
        *SA_GET(sa, child) = t;
        root = child;
      } else {
        break;
      }
    }
  }
}

/* === Iterator functions === */

static inline SA_ITER_T SA_ITER_BEGIN(SA_T* arr) {
  SA_ITER_T it;
  it.arr         = arr;
  it.cur_segment = 0;
  if (arr && arr->count > 0) {
    it.cur_ptr   = (SEG_ARRAY_T*)arr->segments[0];
    uint32_t cap = SA_NS(_slots_in_segment)(0);
    uint32_t span = cap < arr->count ? cap : arr->count;
    it.end_ptr   = it.cur_ptr + span;
  } else {
    it.cur_ptr = NULL;
    it.end_ptr = NULL;
  }
  return it;
}

static inline int SA_ITER_VALID(SA_ITER_T* it) {
  if (!it || !it->arr) return 0;
  if (it->cur_ptr < it->end_ptr) return 1;
  /* Current segment exhausted. The next segment has data only if it
   * exists AND its base offset is below count — swap_remove can leave
   * trailing segments allocated but empty. */
  int next_seg = it->cur_segment + 1;
  if (next_seg >= it->arr->used_segments) return 0;
  return SA_NS(_capacity_for_segments)(next_seg) < it->arr->count;
}

static inline SEG_ARRAY_T* SA_ITER_NEXT(SA_ITER_T* it) {
  /* Fast path: still inside the current segment — single pointer compare
   * + post-increment. */
  if (it->cur_ptr < it->end_ptr) {
    return it->cur_ptr++;
  }
  /* Slow path: segment exhausted. Either advance to next segment or stop. */
  if (!it->arr) return NULL;
  int next_seg = it->cur_segment + 1;
  if (next_seg >= it->arr->used_segments) return NULL;
  uint32_t consumed = SA_NS(_capacity_for_segments)(next_seg);
  /* Trailing segments may be allocated-but-empty after a swap_remove
   * cycle (used_segments doesn't shrink). Stop if there is nothing in
   * this or any later segment. */
  if (consumed >= it->arr->count) return NULL;
  it->cur_segment   = next_seg;
  SEG_ARRAY_T* base = (SEG_ARRAY_T*)it->arr->segments[next_seg];
  uint32_t slots    = SA_NS(_slots_in_segment)(next_seg);
  uint32_t remain   = it->arr->count - consumed;
  uint32_t span     = remain < slots ? remain : slots;
  it->cur_ptr = base;
  it->end_ptr = base + span;
  return it->cur_ptr++;
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
#undef SA_SORT
#undef SA_ITER_T
#undef SA_ITER_BEGIN
#undef SA_ITER_NEXT
#undef SA_ITER_VALID
