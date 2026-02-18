/* rc.h - Reference Counting (include-as-template)
 *
 * Thread-safe reference counting with optional custom allocators.
 *
 * Define RC_NAME (name prefix) before including. Can be included multiple
 * times with different RC_NAME values.
 *
 * Example:
 *   #define RC_NAME foo
 *   #include "rc.h"
 *   // Now have: foo_rc_alloc, foo_rc_ref, foo_rc_unref, foo_rc_count
 *
 * For custom allocators, define RC_ALLOCATOR before including:
 *   #define RC_NAME tracked
 *   #define RC_ALLOCATOR tracked_allocator
 *   #include "rc.h"
 */

#ifndef RC_H
#define RC_H

#include <stdint.h>

typedef struct RCHeader {
  intptr_t ref_count;
  void (*destructor)(void*);
} RCHeader;

#endif  /* RC_H */

#ifndef RC_NAME
#error "RC_NAME must be defined before including rc.h"
#define RC_NAME _rc_err /* suppress further errors */
#endif

/* Concatenation helpers */
#define RC_CAT(a, b)  RC_XCAT(a, b)
#define RC_XCAT(a, b) a##b
#define RC_NS(name)   RC_CAT(RC_NAME, name)

/* Generated function names */
#define RC_ALLOC RC_NS(_rc_alloc)
#define RC_REF   RC_NS(_rc_ref)
#define RC_UNREF RC_NS(_rc_unref)
#define RC_COUNT RC_NS(_rc_count)

#include <assert.h>
#include <stdlib.h>

#include "../platform/platform.h"

/* Allocation hooks — define RC_ALLOCATOR before including to use custom allocator.
 * Defaults to libc_allocator. */
#ifndef RC_ALLOCATOR
#define RC_ALLOCATOR libc_allocator
#define RC_ALLOC_DEFAULTED 1
#endif

#define RC_PAYLOAD_FROM_HEADER(rc)   ((void*)((char*)(rc) + sizeof(RCHeader)))
#define RC_HEADER_FROM_PAYLOAD(pay)  ((RCHeader*)((char*)(pay) - sizeof(RCHeader)))

static inline void* RC_ALLOC(int payload_size, void (*destructor)(void*)) {
  RCHeader* rc = (RCHeader*)RC_ALLOCATOR.alloc(RC_ALLOCATOR.ctx, sizeof(RCHeader) + payload_size);
  if (!rc) return NULL;

  rc->ref_count  = 1;
  rc->destructor = destructor;

  return RC_PAYLOAD_FROM_HEADER(rc);
}

static inline void* RC_REF(void* payload) {
  if (!payload) return NULL;
  RCHeader* rc = RC_HEADER_FROM_PAYLOAD(payload);

  intptr_t new_count = ATOMIC_INC(&rc->ref_count);
  assert(new_count > 1);

  return payload;
}

static inline void RC_UNREF(void* payload) {
  if (!payload) return;

  RCHeader* rc        = RC_HEADER_FROM_PAYLOAD(payload);
  intptr_t  new_count = ATOMIC_DEC(&rc->ref_count);

  if (new_count == 0) {
    if (rc->destructor) rc->destructor(payload);
    RC_ALLOCATOR.free(RC_ALLOCATOR.ctx, rc);
  }
}

static inline intptr_t RC_COUNT(void* payload) {
  if (!payload) return 0;

  RCHeader* rc = RC_HEADER_FROM_PAYLOAD(payload);
  return ATOMIC_LOAD(&rc->ref_count);
}

/* Cleanup all internal macros */
#undef RC_NAME
#undef RC_CAT
#undef RC_XCAT
#undef RC_NS
#undef RC_ALLOC
#undef RC_REF
#undef RC_UNREF
#undef RC_COUNT
#undef RC_PAYLOAD_FROM_HEADER
#undef RC_HEADER_FROM_PAYLOAD
#ifdef RC_ALLOC_DEFAULTED
#undef RC_ALLOCATOR
#undef RC_ALLOC_DEFAULTED
#endif
