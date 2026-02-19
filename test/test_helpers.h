#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/platform/platform.h"

/* --- Memory Spy / Tracker --- */

#define MAX_ALLOCS 100000

typedef struct {
  void*  ptr;
  size_t size;
  bool   active;
} Allocation;

static Allocation       tracking_pool[MAX_ALLOCS];
static int              allocation_count      = 0;
static size_t           total_allocated_bytes = 0;
static size_t           allocation_seq        = 0;
static platform_mutex_t tracker_mutex;

// Resets the tracker state and (re-)initializes the mutex.
// Frees any still-active (orphaned) allocations with a warning.
void tracker_reset() {
  // Free any orphaned allocations before resetting
  for (int i = 0; i < MAX_ALLOCS; i++) {
    if (tracking_pool[i].active) {
      fprintf(stderr,
              "WARN: tracker_reset freeing orphaned allocation: addr=%p, size=%zu\n",
              tracking_pool[i].ptr, tracking_pool[i].size);
      free(tracking_pool[i].ptr);
      tracking_pool[i].active = false;
    }
  }
  // Reset counters and (re-)initialize the mutex
  MUTEX_INIT(tracker_mutex);
  allocation_count      = 0;
  total_allocated_bytes = 0;
  allocation_seq        = 0;
}

// Custom Malloc (thread-safe: malloc outside lock, record inside lock)
void* tracked_malloc(size_t size) {
  void* ptr = malloc(size);

  MUTEX_LOCK(tracker_mutex);
  // Record the allocation
  for (int i = 0; i < MAX_ALLOCS; i++) {
    if (!tracking_pool[i].active) {
      tracking_pool[i].ptr    = ptr;
      tracking_pool[i].size   = size;
      tracking_pool[i].active = true;
      allocation_count++;
      total_allocated_bytes += size;
      allocation_seq++;
      MUTEX_UNLOCK(tracker_mutex);
      return ptr;
    }
  }
  MUTEX_UNLOCK(tracker_mutex);

  fprintf(stderr, "TEST ERROR: tracked_malloc out of slots\n");
  exit(1);
}

// Custom Free (thread-safe: lock to find/deactivate slot, free outside lock)
void tracked_free(void* ptr) {
  if (!ptr) return;  // Standard free handles null, we should too

  MUTEX_LOCK(tracker_mutex);
  for (int i = 0; i < MAX_ALLOCS; i++) {
    if (tracking_pool[i].active && tracking_pool[i].ptr == ptr) {
      tracking_pool[i].active = false;
      allocation_count--;
      total_allocated_bytes -= tracking_pool[i].size;
      MUTEX_UNLOCK(tracker_mutex);
      free(ptr);  // Actually free it outside the critical section
      return;
    }
  }
  MUTEX_UNLOCK(tracker_mutex);

  fprintf(stderr, "TEST FAILURE: Double free or Invalid free detected: %p\n", ptr);
  exit(1);
}

// Custom Realloc (thread-safe: mutex held for entire operation)
void* tracked_realloc(void* ptr, size_t size) {
  if (!ptr) return tracked_malloc(size);
  if (size == 0) {
    tracked_free(ptr);
    return NULL;
  }

  MUTEX_LOCK(tracker_mutex);
  for (int i = 0; i < MAX_ALLOCS; i++) {
    if (tracking_pool[i].active && tracking_pool[i].ptr == ptr) {
      size_t old_size = tracking_pool[i].size;
      void* new_ptr = realloc(ptr, size);
      if (!new_ptr) {
        // realloc failed: original pointer is still valid, slot unchanged
        MUTEX_UNLOCK(tracker_mutex);
        return NULL;
      }
      tracking_pool[i].ptr  = new_ptr;
      tracking_pool[i].size = size;
      total_allocated_bytes = total_allocated_bytes - old_size + size;
      allocation_seq++;
      MUTEX_UNLOCK(tracker_mutex);
      return new_ptr;
    }
  }
  MUTEX_UNLOCK(tracker_mutex);

  fprintf(stderr, "TEST FAILURE: tracked_realloc called with untracked pointer: %p\n", ptr);
  exit(1);
}

void* tracked_malloc_with_ctx(void* ctx, size_t size) {
  (void)ctx;
  return tracked_malloc(size);
}
void* tracked_realloc_with_ctx(void* ctx, void* ptr, size_t size) {
  (void)ctx;
  return tracked_realloc(ptr, size);
}
void tracked_free_with_ctx(void* ctx, void* ptr) {
  (void)ctx;
  tracked_free(ptr);
}

static Allocator tracked_allocator = {
    .alloc   = tracked_malloc_with_ctx,
    .realloc = tracked_realloc_with_ctx,
    .free    = tracked_free_with_ctx,
};

// Thread-safe accessor for active allocation count.
static int tracker_active_count(void) {
  MUTEX_LOCK(tracker_mutex);
  int count = allocation_count;
  MUTEX_UNLOCK(tracker_mutex);
  return count;
}

// Assertion: Ensure zero leaks (thread-safe)
int check_no_leaks() {
  MUTEX_LOCK(tracker_mutex);
  int count = allocation_count;
  if (count != 0) {
    fprintf(stderr, "TEST FAILURE: Memory Leaks Detected!\n");
    fprintf(stderr, "Remaining allocations: %d\n", count);
    for (int i = 0; i < MAX_ALLOCS; i++) {
      if (tracking_pool[i].active) {
        fprintf(stderr, " - Leaked Addr: %p, Size: %zu\n", tracking_pool[i].ptr, tracking_pool[i].size);
      }
    }
    MUTEX_UNLOCK(tracker_mutex);
    return 0;
  }
  MUTEX_UNLOCK(tracker_mutex);
  return 1;
}

static uint32_t string_hash(const void* key) {
  const char* str  = (char*)key;
  uint32_t    hash = 2166136261u;
  while (*str) {
    hash ^= (uint8_t)(*str);
    hash *= 16777619;
    str++;
  }
  return hash;
}

static bool string_eq(const void* k1, const void* k2) {
  return strcmp((char*)k1, (char*)k2) == 0;
}

/* --- Test Utilities and Macros --- */

#define ASSERT(expr)                                            \
  do {                                                          \
    if (!(expr)) {                                              \
      fprintf(stderr, "FAIL: %s (Line %d)\n", #expr, __LINE__); \
      return 0;                                                 \
    }                                                           \
  } while (0)

#define ASSERT_PTR_EQ(actual, expected)                                            \
  do {                                                                             \
    void* __actual   = (void*)(actual);                                            \
    void* __expected = (void*)(expected);                                          \
    if (__actual != __expected) {                                                  \
      fprintf(stderr, "FAIL: %s == %s (Line %d)\n", #actual, #expected, __LINE__); \
      fprintf(stderr, "  Actual:   %p\n", __actual);                               \
      fprintf(stderr, "  Expected: %p\n", __expected);                             \
      return 0;                                                                    \
    }                                                                              \
  } while (0)

#define ASSERT_INT_EQ(actual, expected)                                            \
  do {                                                                             \
    int __actual   = (int)(actual);                                                \
    int __expected = (int)(expected);                                              \
    if (__actual != __expected) {                                                  \
      fprintf(stderr, "FAIL: %s == %s (Line %d)\n", #actual, #expected, __LINE__); \
      fprintf(stderr, "  Actual:   %d\n", __actual);                               \
      fprintf(stderr, "  Expected: %d\n", __expected);                             \
      return 0;                                                                    \
    }                                                                              \
  } while (0)

#define ASSERT_U64_EQ(actual, expected)                                           \
  do {                                                                            \
    uint64_t _a = (uint64_t)(actual);                                             \
    uint64_t _e = (uint64_t)(expected);                                           \
    if (_a != _e) {                                                               \
      fprintf(stderr, "FAIL: %s == %s (Line %d)\n", #actual, #expected, __LINE__);\
      fprintf(stderr, "  Actual:   %llu\n", (unsigned long long)_a);              \
      fprintf(stderr, "  Expected: %llu\n", (unsigned long long)_e);              \
      return 0;                                                                   \
    }                                                                             \
  } while (0)

#define ASSERT_I64_EQ(actual, expected)                                           \
  do {                                                                            \
    int64_t _a = (int64_t)(actual);                                               \
    int64_t _e = (int64_t)(expected);                                             \
    if (_a != _e) {                                                               \
      fprintf(stderr, "FAIL: %s == %s (Line %d)\n", #actual, #expected, __LINE__);\
      fprintf(stderr, "  Actual:   %lld\n", (long long)_a);                      \
      fprintf(stderr, "  Expected: %lld\n", (long long)_e);                      \
      return 0;                                                                   \
    }                                                                             \
  } while (0)

#define ASSERT_U32_EQ(actual, expected)                                           \
  do {                                                                            \
    uint32_t _a = (uint32_t)(actual);                                             \
    uint32_t _e = (uint32_t)(expected);                                           \
    if (_a != _e) {                                                               \
      fprintf(stderr, "FAIL: %s == %s (Line %d)\n", #actual, #expected, __LINE__);\
      fprintf(stderr, "  Actual:   %u\n", _a);                                   \
      fprintf(stderr, "  Expected: %u\n", _e);                                   \
      return 0;                                                                   \
    }                                                                             \
  } while (0)

#define ASSERT_SIZE_EQ(actual, expected)                                           \
  do {                                                                             \
    size_t _a = (size_t)(actual);                                                  \
    size_t _e = (size_t)(expected);                                                \
    if (_a != _e) {                                                                \
      fprintf(stderr, "FAIL: %s == %s (Line %d)\n", #actual, #expected, __LINE__); \
      fprintf(stderr, "  Actual:   %zu\n", _a);                                    \
      fprintf(stderr, "  Expected: %zu\n", _e);                                    \
      return 0;                                                                    \
    }                                                                              \
  } while (0)

#define ASSERT_STR_EQ(actual, expected)                                           \
  do {                                                                            \
    const char* _a = (actual);                                                    \
    const char* _e = (expected);                                                  \
    if (_a == NULL || strcmp(_a, _e) != 0) {                                      \
      fprintf(stderr, "FAIL: %s == %s (Line %d)\n", #actual, #expected, __LINE__);\
      fprintf(stderr, "  Actual:   %s\n", _a ? _a : "(null)");                   \
      fprintf(stderr, "  Expected: %s\n", _e);                                   \
      return 0;                                                                   \
    }                                                                             \
  } while (0)

#define TEST_PASS()   \
  do {                \
    printf("PASS\n"); \
    return 1;         \
  } while (0)
