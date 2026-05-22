#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stdlib.h>

/*
 * ---------------------------------------------------------------------------
 * Atomics
 * ---------------------------------------------------------------------------
 */

#include <stdbool.h>

/*
 * Thread-local storage macro.
 * Emscripten single-threaded: plain static (no TLS).
 */
#ifdef __EMSCRIPTEN__
  #define JACL_THREAD_LOCAL /* nothing — single-threaded */
#elif defined(_MSC_VER)
  #define JACL_THREAD_LOCAL __declspec(thread)
#else
  #define JACL_THREAD_LOCAL __thread
#endif

#ifdef __EMSCRIPTEN__

/* Emscripten: single-threaded — atomics are plain read/write */
#define MEM_RELAXED 0
#define MEM_ACQUIRE 1
#define MEM_RELEASE 2
#define MEM_ACQ_REL 3
#define MEM_SEQ_CST 4

#define ATOMIC_INC(ptr)  (++(*(ptr)))
#define ATOMIC_DEC(ptr)  (--(*(ptr)))
#define ATOMIC_LOAD(ptr) (*(ptr))

#define ATOMIC_LOAD_EXPLICIT(ptr, order) (*(ptr))
#define ATOMIC_STORE_EXPLICIT(ptr, val, order) (*(ptr) = (val))
#define ATOMIC_CAS(ptr, expected, desired, succ, fail) \
  (*(ptr) == *(expected) ? (*(ptr) = (desired), true) : (*(expected) = *(ptr), false))
#define ATOMIC_FENCE(order) ((void)0)

#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(__STDC_NO_ATOMICS__)

/* C11 atomics (preferred when available) */
#include <stdatomic.h>

/* Memory order constants */
#define MEM_RELAXED memory_order_relaxed
#define MEM_ACQUIRE memory_order_acquire
#define MEM_RELEASE memory_order_release
#define MEM_ACQ_REL memory_order_acq_rel
#define MEM_SEQ_CST memory_order_seq_cst

/* Legacy macros */
#define ATOMIC_INC(ptr)  (atomic_fetch_add_explicit((_Atomic intptr_t*)(ptr), 1, memory_order_acq_rel) + 1)
#define ATOMIC_DEC(ptr)  (atomic_fetch_sub_explicit((_Atomic intptr_t*)(ptr), 1, memory_order_acq_rel) - 1)
#define ATOMIC_LOAD(ptr) atomic_load_explicit((_Atomic intptr_t*)(ptr), memory_order_relaxed)

/* Explicit atomic operations */
#define ATOMIC_LOAD_EXPLICIT(ptr, order) \
  atomic_load_explicit((_Atomic __typeof__(*(ptr))*)(ptr), (order))

#define ATOMIC_STORE_EXPLICIT(ptr, val, order) \
  atomic_store_explicit((_Atomic __typeof__(*(ptr))*)(ptr), (val), (order))

#define ATOMIC_CAS(ptr, expected, desired, succ, fail) \
  atomic_compare_exchange_strong_explicit( \
    (_Atomic __typeof__(*(ptr))*)(ptr), (expected), (desired), (succ), (fail))

#define ATOMIC_FENCE(order) atomic_thread_fence(order)

#elif defined(_MSC_VER)

#include <windows.h>
#include <intrin.h>

/* Memory order constants (MSVC uses full barriers for interlocked ops) */
#define MEM_RELAXED 0
#define MEM_ACQUIRE 1
#define MEM_RELEASE 2
#define MEM_ACQ_REL 3
#define MEM_SEQ_CST 4

/* Legacy macros */
#ifdef _WIN64
#define ATOMIC_INC(ptr)  _InterlockedIncrement64((volatile __int64*)(ptr))
#define ATOMIC_DEC(ptr)  _InterlockedDecrement64((volatile __int64*)(ptr))
#define ATOMIC_LOAD(ptr) _InterlockedCompareExchange64((volatile __int64*)(ptr), 0, 0)
#else
#define ATOMIC_INC(ptr)  _InterlockedIncrement((volatile long*)(ptr))
#define ATOMIC_DEC(ptr)  _InterlockedDecrement((volatile long*)(ptr))
#define ATOMIC_LOAD(ptr) _InterlockedCompareExchange((volatile long*)(ptr), 0, 0)
#endif

/* Explicit atomic operations (MSVC interlocked ops are full barriers) */
#define ATOMIC_LOAD_EXPLICIT(ptr, order) \
  (*(volatile __typeof__(*(ptr))*)(ptr))

#define ATOMIC_STORE_EXPLICIT(ptr, val, order) \
  do { _ReadWriteBarrier(); *(volatile __typeof__(*(ptr))*)(ptr) = (val); _ReadWriteBarrier(); } while (0)

#define ATOMIC_CAS(ptr, expected, desired, succ, fail) \
  _platform_cas((volatile void*)(ptr), (expected), (desired), sizeof(*(ptr)))

static inline bool _platform_cas(volatile void* ptr, void* expected, long long desired, size_t size) {
  if (size == 8) {
    long long old = _InterlockedCompareExchange64((volatile __int64*)ptr, desired, *(long long*)expected);
    if (old == *(long long*)expected) return true;
    *(long long*)expected = old;
    return false;
  } else {
    long old = _InterlockedCompareExchange((volatile long*)ptr, (long)desired, *(long*)expected);
    if (old == *(long*)expected) return true;
    *(long*)expected = old;
    return false;
  }
}

#define ATOMIC_FENCE(order) _ReadWriteBarrier(); MemoryBarrier()

#elif defined(__GNUC__) || defined(__clang__)

/* Memory order constants */
#define MEM_RELAXED __ATOMIC_RELAXED
#define MEM_ACQUIRE __ATOMIC_ACQUIRE
#define MEM_RELEASE __ATOMIC_RELEASE
#define MEM_ACQ_REL __ATOMIC_ACQ_REL
#define MEM_SEQ_CST __ATOMIC_SEQ_CST

/*
 * We use __atomic builtins (GCC 4.7+).
 * If you are on very old GCC, fallback to __sync_add_and_fetch.
 */

/* Legacy macros */
#define ATOMIC_INC(ptr)  __atomic_add_fetch(ptr, 1, __ATOMIC_ACQ_REL)
#define ATOMIC_DEC(ptr)  __atomic_sub_fetch(ptr, 1, __ATOMIC_ACQ_REL)
#define ATOMIC_LOAD(ptr) __atomic_load_n(ptr, __ATOMIC_RELAXED)

/* Explicit atomic operations */
#define ATOMIC_LOAD_EXPLICIT(ptr, order) \
  __atomic_load_n(ptr, order)

#define ATOMIC_STORE_EXPLICIT(ptr, val, order) \
  __atomic_store_n(ptr, val, order)

#define ATOMIC_CAS(ptr, expected, desired, succ, fail) \
  __atomic_compare_exchange_n(ptr, expected, desired, 0, succ, fail)

#define ATOMIC_FENCE(order) __atomic_thread_fence(order)

#else
#error "Compiler not supported for atomic operations."
#endif

/*
 * ---------------------------------------------------------------------------
 * Sleep
 * ---------------------------------------------------------------------------
 */

#ifdef __EMSCRIPTEN__

/* Emscripten: no real sleep — yield or no-op */
#define SLEEP_MILLISECONDS(ms) ((void)(ms))

#elif defined(_WIN32)

#include <windows.h>
#define SLEEP_MILLISECONDS(ms) Sleep(ms)
#else

#include <time.h>
static inline void SLEEP_MILLISECONDS(long ms) {
  struct timespec ts;
  ts.tv_sec  = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}

#endif

/*
 * ---------------------------------------------------------------------------
 * Threads
 * ---------------------------------------------------------------------------
 */

#ifdef __EMSCRIPTEN__

/* Emscripten: single-threaded — thread primitives are no-ops */
#define THREAD_PROC_RETURN void*
#define THREAD_PROC_TYPE
typedef unsigned long thread_t;
#define THREAD_CREATE(thread, attr, start_routine, arg) (*(thread) = 0, -1)
#define THREAD_CREATE_WITH_STACK(thread, stack_bytes, start_routine, arg)     \
    ((void)(stack_bytes), *(thread) = 0, -1)
#define THREAD_JOIN(thread, retval)                     ((void)(thread), (void)(retval))
#define THREAD_SELF()                                   ((thread_t)0)
#define THREAD_EQUAL(a, b)                              ((a) == (b))

#elif defined(_WIN32)

#include <windows.h>
#define THREAD_PROC_RETURN DWORD
#define THREAD_PROC_TYPE   WINAPI
#define THREAD_SELF()      GetCurrentThread()
#define THREAD_EQUAL(a, b) ((a) == (b))
typedef HANDLE thread_t;
int            THREAD_CREATE(thread_t* thread, void* attr, LPTHREAD_START_ROUTINE start_routine, void* arg) {
  *thread = CreateThread(NULL, 0, start_routine, arg, 0, NULL);
  return (*thread == NULL);
}
/* THREAD_CREATE_WITH_STACK: CreateThread's second arg is the initial commit
 * (reservation is set by /STACK linker option). Passing stack_bytes makes
 * sure the thread gets at least that committed up-front. */
int THREAD_CREATE_WITH_STACK(thread_t* thread, size_t stack_bytes,
                             LPTHREAD_START_ROUTINE start_routine, void* arg) {
  *thread = CreateThread(NULL, (SIZE_T)stack_bytes, start_routine, arg, 0, NULL);
  return (*thread == NULL);
}
void THREAD_JOIN(thread_t thread, void** retval) {
  WaitForSingleObject(thread, INFINITE);
  CloseHandle(thread);
}

#else

#include <pthread.h>
#define THREAD_PROC_RETURN void*
#define THREAD_PROC_TYPE
typedef pthread_t thread_t;
#define THREAD_CREATE(thread, attr, start_routine, arg) pthread_create(thread, attr, start_routine, arg)
/* THREAD_CREATE_WITH_STACK: create a thread with at least stack_bytes of
 * stack space. The pthread default varies (512KB on macOS, 8MB on glibc),
 * which leaves macOS workers vulnerable to stack overflow on recursive C
 * code in -O0 builds (the compiler descent is the known case — see
 * jacl_harness flake). */
static inline int THREAD_CREATE_WITH_STACK(thread_t* thread, size_t stack_bytes,
                                           void* (*start_routine)(void*),
                                           void* arg) {
  pthread_attr_t attr;
  if (pthread_attr_init(&attr) != 0) return -1;
  /* pthread_attr_setstacksize may round up to a multiple of PAGESIZE or fail
   * if stack_bytes < PTHREAD_STACK_MIN. Both are fine — fall back to default
   * (pass NULL attr) on failure rather than refusing to start the thread. */
  pthread_attr_setstacksize(&attr, stack_bytes);
  int rc = pthread_create(thread, &attr, start_routine, arg);
  pthread_attr_destroy(&attr);
  return rc;
}
#define THREAD_JOIN(thread, retval)                     pthread_join(thread, retval)
#define THREAD_SELF()                                   pthread_self()
#define THREAD_EQUAL(a, b)                              pthread_equal((a), (b))

#endif

/*
 * ---------------------------------------------------------------------------
 * Mutex
 * ---------------------------------------------------------------------------
 */

#ifdef __EMSCRIPTEN__

/* Emscripten: single-threaded — mutexes are no-ops */
typedef int platform_mutex_t;
#define MUTEX_INIT(m)    ((void)(m))
#define MUTEX_LOCK(m)    ((void)(m))
#define MUTEX_UNLOCK(m)  ((void)(m))
#define MUTEX_DESTROY(m) ((void)(m))

#elif defined(_WIN32)

typedef CRITICAL_SECTION platform_mutex_t;
#define MUTEX_INIT(m)    InitializeCriticalSection(&(m))
#define MUTEX_LOCK(m)    EnterCriticalSection(&(m))
#define MUTEX_UNLOCK(m)  LeaveCriticalSection(&(m))
#define MUTEX_DESTROY(m) DeleteCriticalSection(&(m))

#else

typedef pthread_mutex_t platform_mutex_t;
#define MUTEX_INIT(m)    pthread_mutex_init(&(m), NULL)
#define MUTEX_LOCK(m)    pthread_mutex_lock(&(m))
#define MUTEX_UNLOCK(m)  pthread_mutex_unlock(&(m))
#define MUTEX_DESTROY(m) pthread_mutex_destroy(&(m))

#endif

/*
 * ---------------------------------------------------------------------------
 * Condition variable
 *
 * Pairs with platform_mutex_t. Mutex must be held when calling COND_WAIT*.
 * COND_WAIT_FOR_MS returns 0 on signal, non-zero on timeout. The mutex may
 * be released and re-acquired internally; the caller must re-check its
 * predicate after waking.
 * ---------------------------------------------------------------------------
 */

#ifdef __EMSCRIPTEN__

/* Single-threaded: no waiters, no signaling */
typedef int platform_cond_t;
#define COND_INIT(c)          ((void)(c))
#define COND_DESTROY(c)       ((void)(c))
#define COND_WAIT(c, m)       ((void)(c), (void)(m))
#define COND_WAIT_FOR_MS(c, m, ms) ((void)(c), (void)(m), (void)(ms), 0)
#define COND_SIGNAL(c)        ((void)(c))
#define COND_BROADCAST(c)     ((void)(c))

#elif defined(_WIN32)

typedef CONDITION_VARIABLE platform_cond_t;
#define COND_INIT(c)      InitializeConditionVariable(&(c))
#define COND_DESTROY(c)   ((void)(c))
#define COND_WAIT(c, m)   SleepConditionVariableCS(&(c), &(m), INFINITE)
#define COND_WAIT_FOR_MS(c, m, ms) \
    (SleepConditionVariableCS(&(c), &(m), (DWORD)(ms)) ? 0 : 1)
#define COND_SIGNAL(c)    WakeConditionVariable(&(c))
#define COND_BROADCAST(c) WakeAllConditionVariable(&(c))

#else

typedef pthread_cond_t platform_cond_t;
#define COND_INIT(c)      pthread_cond_init(&(c), NULL)
#define COND_DESTROY(c)   pthread_cond_destroy(&(c))
#define COND_WAIT(c, m)   pthread_cond_wait(&(c), &(m))
static inline int platform__cond_timedwait_ms(pthread_cond_t *c,
                                              pthread_mutex_t *m,
                                              long ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec  += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(c, m, &ts);
}
#define COND_WAIT_FOR_MS(c, m, ms) platform__cond_timedwait_ms(&(c), &(m), (ms))
#define COND_SIGNAL(c)    pthread_cond_signal(&(c))
#define COND_BROADCAST(c) pthread_cond_broadcast(&(c))

#endif

/*
 * ---------------------------------------------------------------------------
 * Read-Write Lock
 * ---------------------------------------------------------------------------
 */

#ifdef __EMSCRIPTEN__

/* Emscripten: single-threaded — rwlocks are no-ops */
typedef int platform_rwlock_t;
#define RWLOCK_INIT(rw)     ((void)(rw))
#define RWLOCK_DESTROY(rw)  ((void)(rw))
#define RWLOCK_RDLOCK(rw)   ((void)(rw))
#define RWLOCK_RDUNLOCK(rw) ((void)(rw))
#define RWLOCK_WRLOCK(rw)   ((void)(rw))
#define RWLOCK_WRUNLOCK(rw) ((void)(rw))

#elif defined(_WIN32)

typedef SRWLOCK platform_rwlock_t;
#define RWLOCK_INIT(rw)     InitializeSRWLock(&(rw))
#define RWLOCK_DESTROY(rw)  ((void)(rw))
#define RWLOCK_RDLOCK(rw)   AcquireSRWLockShared(&(rw))
#define RWLOCK_RDUNLOCK(rw) ReleaseSRWLockShared(&(rw))
#define RWLOCK_WRLOCK(rw)   AcquireSRWLockExclusive(&(rw))
#define RWLOCK_WRUNLOCK(rw) ReleaseSRWLockExclusive(&(rw))

#else

typedef pthread_rwlock_t platform_rwlock_t;
#define RWLOCK_INIT(rw)     pthread_rwlock_init(&(rw), NULL)
#define RWLOCK_DESTROY(rw)  pthread_rwlock_destroy(&(rw))
#define RWLOCK_RDLOCK(rw)   pthread_rwlock_rdlock(&(rw))
#define RWLOCK_RDUNLOCK(rw) pthread_rwlock_unlock(&(rw))
#define RWLOCK_WRLOCK(rw)   pthread_rwlock_wrlock(&(rw))
#define RWLOCK_WRUNLOCK(rw) pthread_rwlock_unlock(&(rw))

#endif

/*
 * ---------------------------------------------------------------------------
 * Popcount
 * ---------------------------------------------------------------------------
 */

static inline int get_popcount(uint32_t n) {
#if defined(__GNUC__)
  return __builtin_popcount(n);
#elif defined(_MSC_VER)
  return __popcnt(n);
#else
  // Fallback for pure C11 without extensions
  n = n - ((n >> 1) & 0x55555555);
  n = (n & 0x33333333) + ((n >> 2) & 0x33333333);
  return (((n + (n >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
#endif
}

/*
 * ---------------------------------------------------------------------------
 * Allocator
 * ---------------------------------------------------------------------------
 */

typedef struct {
  void* (*alloc)(void* ctx, size_t size);
  void* (*realloc)(void* ctx, void* ptr, size_t size);
  void (*free)(void* ctx, void* ptr);
  void* ctx;
} Allocator;

static inline void* libc_alloc(void* ctx, size_t size) {
  (void)ctx;
  return malloc(size);
}

static inline void* libc_realloc(void* ctx, void* ptr, size_t size) {
  (void)ctx;
  return realloc(ptr, size);
}

static inline void libc_free(void* ctx, void* ptr) {
  (void)ctx;
  free(ptr);
}

static inline Allocator get_libc_allocator(void) {
  return (Allocator){.alloc = libc_alloc, .realloc = libc_realloc, .free = libc_free, .ctx = NULL};
}

#define libc_allocator get_libc_allocator()

#endif /* PLATFORM_H */
