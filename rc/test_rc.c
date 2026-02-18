#include <assert.h>
#include <stdio.h>

#include "../platform/platform.h"

/* Instantiate RC with default (libc) allocator and empty prefix */
#define RC_NAME
#include "rc.h"

static int my_object_destroyed = 0;

typedef struct {
  int id;
  int magic_number;
} MyObject;

void my_object_destroy(void* ptr) {
  (void)ptr;
  my_object_destroyed++;
}

THREAD_PROC_RETURN THREAD_PROC_TYPE worker_thread(void* arg) {
  thread_t  tid = THREAD_SELF();
  MyObject* obj = (MyObject*)arg;

  _rc_ref(obj);
  printf("Thread %lu accessing ID: %d [RefCount: %ld]\n", (unsigned long)(uintptr_t)tid, obj->id, _rc_count(obj));
  SLEEP_MILLISECONDS(10);

  _rc_unref(obj);
  return 0;
}

int main(void) {
  printf("Initializing RefCount Test...\n");

  MyObject* obj     = _rc_alloc(sizeof(MyObject), my_object_destroy);
  obj->id           = 101;
  obj->magic_number = 42;

  int      threads_count = 10;
  thread_t threads[64]   = {0};

  for (int i = 0; i < threads_count; i++) {
    THREAD_CREATE(&threads[i], NULL, worker_thread, obj);
  }

  for (int i = 0; i < threads_count; i++) {
    THREAD_JOIN(threads[i], NULL);
  }

  printf("Main releasing its reference...\n");
  _rc_unref(obj);

  assert(my_object_destroyed);

  printf("TESTS PASSED\n");

  return 0;
}
