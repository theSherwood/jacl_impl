#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../test/test_helpers.h"

#define ARENA_IMPLEMENTATION
#include "./arena.h"

/* Test: arena lazy initialization */
static int test_arena_lazy_init() {
  tracker_reset();

  arena_t arena = {.allocator = tracked_allocator};

  /* First allocation triggers initialization */
  void* ptr = arena_alloc(&arena, 32);
  ASSERT(ptr != NULL);

  /* Check allocation tracking shows first region with inline memory = 1 allocation */
  ASSERT(tracker_active_count() == 1);

  arena_reset(&arena);
  ASSERT(check_no_leaks());

  TEST_PASS();
}

/* Test: small allocation returns aligned pointer */
static int test_arena_alloc_aligned() {
  tracker_reset();

  arena_t arena = {.allocator = tracked_allocator};

  void* ptr = arena_alloc(&arena, 32);
  ASSERT(ptr != NULL);

  /* Check alignment: pointer should be aligned to sizeof(void*) */
  uintptr_t addr = (uintptr_t)ptr;
  ASSERT(addr % sizeof(void*) == 0);

  arena_reset(&arena);
  ASSERT(check_no_leaks());

  TEST_PASS();
}

/* Test: allocation is zero-initialized */
static int test_arena_alloc_zero_initialized() {
  tracker_reset();

  arena_t arena = {.allocator = tracked_allocator};

  void* ptr = arena_alloc(&arena, 64);
  ASSERT(ptr != NULL);

  /* Check that memory is zero-initialized */
  unsigned char* bytes = (unsigned char*)ptr;
  for (int i = 0; i < 64; i++) {
    ASSERT(bytes[i] == 0);
  }

  arena_reset(&arena);
  ASSERT(check_no_leaks());

  TEST_PASS();
}

/* Test: zero-byte allocation returns NULL */
static int test_arena_alloc_zero_size() {
  tracker_reset();

  arena_t arena = {.allocator = tracked_allocator};

  void* ptr = arena_alloc(&arena, 0);
  ASSERT(ptr == NULL);

  arena_reset(&arena);
  ASSERT(check_no_leaks());

  TEST_PASS();
}

/* Test: multiple small allocations maintain alignment */
static int test_arena_multiple_allocations_aligned() {
  tracker_reset();

  arena_t arena = {.allocator = tracked_allocator};

  /* Allocate multiple chunks of different sizes */
  void* ptr1 = arena_alloc(&arena, 17);
  ASSERT(ptr1 != NULL);
  ASSERT(((uintptr_t)ptr1) % sizeof(void*) == 0);

  void* ptr2 = arena_alloc(&arena, 33);
  ASSERT(ptr2 != NULL);
  ASSERT(((uintptr_t)ptr2) % sizeof(void*) == 0);

  void* ptr3 = arena_alloc(&arena, 64);
  ASSERT(ptr3 != NULL);
  ASSERT(((uintptr_t)ptr3) % sizeof(void*) == 0);

  /* Verify they're all zero-initialized */
  unsigned char* bytes1 = (unsigned char*)ptr1;
  unsigned char* bytes2 = (unsigned char*)ptr2;
  unsigned char* bytes3 = (unsigned char*)ptr3;

  for (int i = 0; i < 17; i++) ASSERT(bytes1[i] == 0);
  for (int i = 0; i < 33; i++) ASSERT(bytes2[i] == 0);
  for (int i = 0; i < 64; i++) ASSERT(bytes3[i] == 0);

  arena_reset(&arena);
  ASSERT(check_no_leaks());

  TEST_PASS();
}

/* Test: allocation larger than region creates new region */
static int test_arena_large_allocation() {
  tracker_reset();

  arena_t arena = {.allocator = tracked_allocator};

  /* Allocate something small in first region */
  void* ptr1 = arena_alloc(&arena, 32);
  ASSERT(ptr1 != NULL);

  int allocation_count_before = tracker_active_count();

  /* Allocate something larger than default region size */
  void* ptr2 = arena_alloc(&arena, 8192);
  ASSERT(ptr2 != NULL);

  /* Should have created a new region (1 new allocation: region with inline memory) */
  ASSERT(tracker_active_count() == allocation_count_before + 1);

  arena_reset(&arena);
  ASSERT(check_no_leaks());

  TEST_PASS();
}

/* Test: multiple regions allocate correctly */
static int test_arena_multiple_regions() {
  tracker_reset();

  arena_t arena = {.allocator = tracked_allocator};

  /* Fill first region with multiple allocations */
  void* ptr1 = arena_alloc(&arena, 2048);
  ASSERT(ptr1 != NULL);

  /* This should trigger second region creation */
  void* ptr2 = arena_alloc(&arena, 2048);
  ASSERT(ptr2 != NULL);

  /* This should trigger third region creation */
  void* ptr3 = arena_alloc(&arena, 2048);
  ASSERT(ptr3 != NULL);

  /* All should be valid pointers in different regions */
  ASSERT(ptr1 != ptr2);
  ASSERT(ptr2 != ptr3);
  ASSERT(ptr1 != ptr3);

  arena_reset(&arena);
  ASSERT(check_no_leaks());

  TEST_PASS();
}

/* Test: arena reset frees all regions */
static int test_arena_reset_frees_regions() {
  tracker_reset();

  arena_t arena = {.allocator = tracked_allocator};

  /* Allocate across multiple regions */
  void* ptr1 = arena_alloc(&arena, 2048);
  ASSERT(ptr1 != NULL);

  void* ptr2 = arena_alloc(&arena, 2048);
  ASSERT(ptr2 != NULL);

  void* ptr3 = arena_alloc(&arena, 2048);
  ASSERT(ptr3 != NULL);

  int initial_count = tracker_active_count();
  ASSERT(initial_count >= 2);

  /* Reset should free all regions */
  arena_reset(&arena);

  ASSERT(tracker_active_count() == 0);

  /* Allocations should work again after reset */
  void* ptr4 = arena_alloc(&arena, 32);
  ASSERT(ptr4 != NULL);

  arena_reset(&arena);
  ASSERT(check_no_leaks());

  TEST_PASS();
}

/* Test: arena destroy frees all regions and zeros struct */
static int test_arena_destroy() {
  tracker_reset();

  arena_t arena = {.allocator = tracked_allocator};

  /* Allocate across multiple regions */
  void* ptr1 = arena_alloc(&arena, 2048);
  ASSERT(ptr1 != NULL);

  void* ptr2 = arena_alloc(&arena, 2048);
  ASSERT(ptr2 != NULL);

  /* Destroy should free all regions and zero the struct */
  arena_destroy(&arena);

  ASSERT(tracker_active_count() == 0);
  ASSERT(arena.head == NULL);
  ASSERT(arena.region_size == 0);
  ASSERT(arena.alignment == 0);
  ASSERT(arena.allocator.alloc == NULL);

  ASSERT(check_no_leaks());

  TEST_PASS();
}

/* Test: destroy on zero-initialized arena is safe */
static int test_arena_destroy_zero_init() {
  arena_t arena = {0};
  arena_destroy(&arena);
  ASSERT(arena.head == NULL);

  TEST_PASS();
}

/* Test: stress test with many allocations (> 100) */
static int test_arena_stress_test() {
  tracker_reset();

  arena_t arena = {.allocator = tracked_allocator};

  /* Allocate many objects of various sizes */
  void* ptrs[150];
  for (int i = 0; i < 150; i++) {
    size_t size = (i % 64) + 1; /* Sizes from 1 to 64 bytes */
    ptrs[i]     = arena_alloc(&arena, size);
    ASSERT(ptrs[i] != NULL);

    /* Verify alignment */
    ASSERT(((uintptr_t)ptrs[i]) % sizeof(void*) == 0);

    /* Verify zero-initialization */
    unsigned char* bytes = (unsigned char*)ptrs[i];
    for (size_t j = 0; j < size; j++) {
      ASSERT(bytes[j] == 0);
    }
  }

  /* Verify no leaks after many allocations */
  arena_reset(&arena);
  ASSERT(check_no_leaks());

  TEST_PASS();
}

int main(void) {
  printf("Test: arena lazy initialization\n");
  if (!test_arena_lazy_init()) return 1;

  printf("Test: small allocation returns aligned pointer\n");
  if (!test_arena_alloc_aligned()) return 1;

  printf("Test: allocation is zero-initialized\n");
  if (!test_arena_alloc_zero_initialized()) return 1;

  printf("Test: zero-byte allocation returns NULL\n");
  if (!test_arena_alloc_zero_size()) return 1;

  printf("Test: multiple small allocations maintain alignment\n");
  if (!test_arena_multiple_allocations_aligned()) return 1;

  printf("Test: allocation larger than region creates new region\n");
  if (!test_arena_large_allocation()) return 1;

  printf("Test: multiple regions allocate correctly\n");
  if (!test_arena_multiple_regions()) return 1;

  printf("Test: arena reset frees all regions\n");
  if (!test_arena_reset_frees_regions()) return 1;

  printf("Test: arena destroy frees all regions and zeros struct\n");
  if (!test_arena_destroy()) return 1;

  printf("Test: destroy on zero-initialized arena is safe\n");
  if (!test_arena_destroy_zero_init()) return 1;

  printf("Test: stress test with many allocations (> 100)\n");
  if (!test_arena_stress_test()) return 1;

  printf("\nAll tests passed!\n");
  return 0;
}
