/* chase_lev.h - Chase-Lev work-stealing deque (include-as-template)
 *
 * Define CHASE_LEV_T (element type) and CHASE_LEV_NAME (name prefix) before
 * including. Can be included multiple times with different T/NAME values.
 *
 * Example:
 *   #define CHASE_LEV_T    int
 *   #define CHASE_LEV_NAME int_deque
 *   #include "chase_lev.h"
 *   // Now have: int_deque_buffer, int_deque_deque, int_deque_buffer_new(), etc.
 */

#ifndef CHASE_LEV_T
#error "CHASE_LEV_T must be defined before including chase_lev.h"
#define CHASE_LEV_T int /* suppress further errors */
#endif

#ifndef CHASE_LEV_NAME
#error "CHASE_LEV_NAME must be defined before including chase_lev.h"
#define CHASE_LEV_NAME _cl_err /* suppress further errors */
#endif

/* Concatenation helpers */
#define CL_CAT(a, b)  CL_XCAT(a, b)
#define CL_XCAT(a, b) a##b
#define CL_NS(name)   CL_CAT(CHASE_LEV_NAME, name)

/* Generated type and function names */
#define CL_BUFFER      CL_NS(_buffer)
#define CL_RETIRED     CL_NS(_retired_buffer)
#define CL_THIEF_SLOT  CL_NS(_thief_slot)
#define CL_DEQUE       CL_NS(_deque)
#define CL_BUFFER_NEW  CL_NS(_buffer_new)
#define CL_BUFFER_FREE CL_NS(_buffer_free)
#define CL_DEQUE_NEW   CL_NS(_deque_new)
#define CL_DEQUE_FREE  CL_NS(_deque_free)
#define CL_DEQUE_PUSH  CL_NS(_deque_push)
#define CL_DEQUE_TAKE  CL_NS(_deque_take)
#define CL_DEQUE_STEAL CL_NS(_deque_steal)
#define CL_DEQUE_RECLAIM  CL_NS(_deque_reclaim)
#define CL_DEQUE_REGISTER_THIEF   CL_NS(_deque_register_thief)
#define CL_DEQUE_UNREGISTER_THIEF CL_NS(_deque_unregister_thief)

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../platform/platform.h"

/* Maximum number of thieves — override by defining before include */
#ifndef CL_DEQUE_MAX_THIEVES
#define CL_DEQUE_MAX_THIEVES 64
#define CL_MAX_THIEVES_DEFAULTED 1
#endif

/* Sentinel value for inactive thief slots */
#define CL_EPOCH_INACTIVE UINT64_MAX

/* Allocation hooks — define CHASE_LEV_MALLOC/CHASE_LEV_FREE before including
 * to use tracked or custom allocators. Defaults to malloc/free. */
#ifndef CHASE_LEV_MALLOC
#define CHASE_LEV_MALLOC(sz) malloc(sz)
#define CHASE_LEV_FREE(p)    free(p)
#define CL_ALLOC_DEFAULTED   1
#endif

/* ---- Circular buffer ---- */

typedef struct CL_BUFFER {
  uint64_t     log_capacity;
  uint64_t     mask;
  CHASE_LEV_T  data[];
} CL_BUFFER;

/* ---- Retired buffer node (for safe reclamation after resize) ---- */

typedef struct CL_RETIRED CL_RETIRED;
struct CL_RETIRED {
  CL_RETIRED*  next;
  CL_BUFFER*   buffer;
  uint64_t     retire_epoch;
};

/* ---- Cache-line-padded thief epoch slot ---- */

typedef struct CL_THIEF_SLOT {
  volatile uint64_t epoch;
  char              pad_[64 - sizeof(uint64_t)];
} CL_THIEF_SLOT;

/* ---- Deque ---- */

typedef struct CL_DEQUE {
  volatile uint64_t  top;
  volatile uint64_t  bottom;
  CL_BUFFER*         buffer;
  CL_RETIRED*        retired;
  volatile uint64_t  epoch;
  volatile uint64_t  thief_count;
  CL_THIEF_SLOT      thieves[CL_DEQUE_MAX_THIEVES];
} CL_DEQUE;

/* ---- Buffer helpers ---- */

static inline CL_BUFFER* CL_BUFFER_NEW(uint64_t log_capacity) {
  uint64_t capacity = (uint64_t)1 << log_capacity;
  CL_BUFFER* buf = (CL_BUFFER*)CHASE_LEV_MALLOC(sizeof(CL_BUFFER) + capacity * sizeof(CHASE_LEV_T));
  if (!buf) return NULL;
  buf->log_capacity = log_capacity;
  buf->mask = capacity - 1;
  memset(buf->data, 0, capacity * sizeof(CHASE_LEV_T));
  return buf;
}

static inline void CL_BUFFER_FREE(CL_BUFFER* buf) {
  CHASE_LEV_FREE(buf);
}

/* ---- Deque lifecycle ---- */

static inline CL_DEQUE* CL_DEQUE_NEW(uint64_t initial_log_capacity) {
  CL_DEQUE* dq = (CL_DEQUE*)CHASE_LEV_MALLOC(sizeof(CL_DEQUE));
  if (!dq) return NULL;
  CL_BUFFER* buf = CL_BUFFER_NEW(initial_log_capacity);
  if (!buf) {
    CHASE_LEV_FREE(dq);
    return NULL;
  }
  dq->top     = 0;
  dq->bottom  = 0;
  dq->buffer  = buf;
  dq->retired = NULL;
  dq->epoch   = 0;
  dq->thief_count = 0;
  for (int i = 0; i < CL_DEQUE_MAX_THIEVES; i++) {
    dq->thieves[i].epoch = CL_EPOCH_INACTIVE;
  }
  return dq;
}

static inline void CL_DEQUE_FREE(CL_DEQUE* dq) {
  if (!dq) return;
  /* Free current buffer */
  if (dq->buffer) {
    CL_BUFFER_FREE(dq->buffer);
  }
  /* Free all retired buffers */
  CL_RETIRED* node = dq->retired;
  while (node) {
    CL_RETIRED* next = node->next;
    CL_BUFFER_FREE(node->buffer);
    CHASE_LEV_FREE(node);
    node = next;
  }
  CHASE_LEV_FREE(dq);
}

/* ---- Thief registration ---- */

static inline int CL_DEQUE_REGISTER_THIEF(CL_DEQUE* dq) {
  uint64_t slot = (uint64_t)(ATOMIC_INC(&dq->thief_count) - 1);
  if (slot >= CL_DEQUE_MAX_THIEVES) {
    ATOMIC_DEC(&dq->thief_count);
    return -1;
  }
  ATOMIC_STORE_EXPLICIT(&dq->thieves[slot].epoch, CL_EPOCH_INACTIVE, MEM_RELAXED);
  return (int)slot;
}

static inline void CL_DEQUE_UNREGISTER_THIEF(CL_DEQUE* dq, int thief_id) {
  if (thief_id < 0 || thief_id >= CL_DEQUE_MAX_THIEVES) return;
  ATOMIC_STORE_EXPLICIT(&dq->thieves[thief_id].epoch, CL_EPOCH_INACTIVE, MEM_RELAXED);
}

/* ---- Reclaim (owner only) ---- */

static inline void CL_DEQUE_RECLAIM(CL_DEQUE* dq) {
  if (!dq->retired) return;

  /* Find minimum epoch observed by any active thief */
  uint64_t thief_count = ATOMIC_LOAD_EXPLICIT(&dq->thief_count, MEM_RELAXED);
  uint64_t min_epoch = UINT64_MAX;
  for (uint64_t i = 0; i < thief_count; i++) {
    uint64_t e = ATOMIC_LOAD_EXPLICIT(&dq->thieves[i].epoch, MEM_ACQUIRE);
    if (e != CL_EPOCH_INACTIVE && e < min_epoch) {
      min_epoch = e;
    }
  }

  /* Free retired buffers with retire_epoch strictly less than min_epoch.
   * If no active thieves (min_epoch == UINT64_MAX), all are freed. */
  CL_RETIRED** pp = &dq->retired;
  while (*pp) {
    CL_RETIRED* node = *pp;
    if (node->retire_epoch < min_epoch) {
      *pp = node->next;
      CL_BUFFER_FREE(node->buffer);
      CHASE_LEV_FREE(node);
    } else {
      pp = &node->next;
    }
  }
}

/* ---- Push (owner only) ---- */

static inline void CL_DEQUE_PUSH(CL_DEQUE* dq, CHASE_LEV_T element) {
  uint64_t b = ATOMIC_LOAD_EXPLICIT(&dq->bottom, MEM_RELAXED);
  uint64_t t = ATOMIC_LOAD_EXPLICIT(&dq->top, MEM_ACQUIRE);
  CL_BUFFER* buf = ATOMIC_LOAD_EXPLICIT(&dq->buffer, MEM_RELAXED);
  uint64_t size = b - t;

  if (size >= ((uint64_t)1 << buf->log_capacity)) {
    /* Buffer full — grow */
    CL_BUFFER* new_buf = CL_BUFFER_NEW(buf->log_capacity + 1);
    /* Copy existing elements preserving circular mapping */
    for (uint64_t i = t; i < b; i++) {
      new_buf->data[i & new_buf->mask] = buf->data[i & buf->mask];
    }
    /* Retire old buffer (thieves may still be reading it) */
    CL_RETIRED* node = (CL_RETIRED*)CHASE_LEV_MALLOC(sizeof(CL_RETIRED));
    node->buffer = buf;
    node->retire_epoch = ATOMIC_LOAD_EXPLICIT(&dq->epoch, MEM_RELAXED);
    node->next = dq->retired;
    dq->retired = node;
    buf = new_buf;
    ATOMIC_STORE_EXPLICIT(&dq->buffer, buf, MEM_RELEASE);
    /* Advance global epoch so thieves entering after this point
       will observe a newer epoch than the retired buffer's tag */
    ATOMIC_STORE_EXPLICIT(&dq->epoch, node->retire_epoch + 1, MEM_RELEASE);

    /* Reclaim any retired buffers that are no longer referenced */
    CL_DEQUE_RECLAIM(dq);
  }

  buf->data[b & buf->mask] = element;
  ATOMIC_FENCE(MEM_RELEASE);
  ATOMIC_STORE_EXPLICIT(&dq->bottom, b + 1, MEM_RELAXED);
}

/* ---- Take (owner only, pops from bottom) ---- */

static inline bool CL_DEQUE_TAKE(CL_DEQUE* dq, CHASE_LEV_T* result) {
  uint64_t b = ATOMIC_LOAD_EXPLICIT(&dq->bottom, MEM_RELAXED) - 1;
  CL_BUFFER* buf = ATOMIC_LOAD_EXPLICIT(&dq->buffer, MEM_RELAXED);
  ATOMIC_STORE_EXPLICIT(&dq->bottom, b, MEM_RELAXED);
  ATOMIC_FENCE(MEM_SEQ_CST);
  uint64_t t = ATOMIC_LOAD_EXPLICIT(&dq->top, MEM_RELAXED);

  int64_t size = (int64_t)(b - t);
  if (size < 0) {
    /* Empty — restore bottom */
    ATOMIC_STORE_EXPLICIT(&dq->bottom, t, MEM_RELAXED);
    return false;
  }

  *result = buf->data[b & buf->mask];
  if (size > 0) {
    /* Safe zone — no race with thieves */
    return true;
  }

  /* size == 0: race zone — compete with thieves via CAS on top */
  uint64_t expected = t;
  if (ATOMIC_CAS(&dq->top, &expected, t + 1, MEM_SEQ_CST, MEM_RELAXED)) {
    ATOMIC_STORE_EXPLICIT(&dq->bottom, t + 1, MEM_RELAXED);
    return true;
  }
  /* Lost the race — element was stolen */
  ATOMIC_STORE_EXPLICIT(&dq->bottom, t + 1, MEM_RELAXED);
  return false;
}

/* ---- Steal (thief, pops from top) ---- */

static inline bool CL_DEQUE_STEAL(CL_DEQUE* dq, CHASE_LEV_T* result, int thief_id) {
  /* Epoch enter: announce which epoch we are observing */
  uint64_t e = ATOMIC_LOAD_EXPLICIT(&dq->epoch, MEM_RELAXED);
  ATOMIC_STORE_EXPLICIT(&dq->thieves[thief_id].epoch, e, MEM_RELAXED);

  uint64_t t = ATOMIC_LOAD_EXPLICIT(&dq->top, MEM_ACQUIRE);
  ATOMIC_FENCE(MEM_SEQ_CST);
  uint64_t b = ATOMIC_LOAD_EXPLICIT(&dq->bottom, MEM_ACQUIRE);

  if ((int64_t)(b - t) <= 0) {
    /* Empty — epoch exit */
    ATOMIC_STORE_EXPLICIT(&dq->thieves[thief_id].epoch, CL_EPOCH_INACTIVE, MEM_RELAXED);
    return false;
  }

  /* Read element optimistically before CAS */
  CL_BUFFER* buf = ATOMIC_LOAD_EXPLICIT(&dq->buffer, MEM_ACQUIRE);
  CHASE_LEV_T elem = buf->data[t & buf->mask];

  /* Try to increment top to claim this element */
  uint64_t expected = t;
  if (!ATOMIC_CAS(&dq->top, &expected, t + 1, MEM_SEQ_CST, MEM_RELAXED)) {
    /* Lost race with another thief or the owner — epoch exit */
    ATOMIC_STORE_EXPLICIT(&dq->thieves[thief_id].epoch, CL_EPOCH_INACTIVE, MEM_RELAXED);
    return false;
  }

  *result = elem;
  /* Epoch exit: sentinel MUST be written after CAS and element read are complete */
  ATOMIC_STORE_EXPLICIT(&dq->thieves[thief_id].epoch, CL_EPOCH_INACTIVE, MEM_RELAXED);
  return true;
}

/* ---- Cleanup all internal macros ---- */

#undef CHASE_LEV_T
#undef CHASE_LEV_NAME
#undef CL_CAT
#undef CL_XCAT
#undef CL_NS
#undef CL_BUFFER
#undef CL_RETIRED
#undef CL_THIEF_SLOT
#undef CL_DEQUE
#undef CL_BUFFER_NEW
#undef CL_BUFFER_FREE
#undef CL_DEQUE_NEW
#undef CL_DEQUE_FREE
#undef CL_DEQUE_PUSH
#undef CL_DEQUE_TAKE
#undef CL_DEQUE_STEAL
#undef CL_DEQUE_RECLAIM
#undef CL_DEQUE_REGISTER_THIEF
#undef CL_DEQUE_UNREGISTER_THIEF
#undef CL_EPOCH_INACTIVE
#ifdef CL_MAX_THIEVES_DEFAULTED
#undef CL_DEQUE_MAX_THIEVES
#undef CL_MAX_THIEVES_DEFAULTED
#endif
#ifdef CL_ALLOC_DEFAULTED
#undef CHASE_LEV_MALLOC
#undef CHASE_LEV_FREE
#undef CL_ALLOC_DEFAULTED
#endif
