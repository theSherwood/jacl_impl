#include "../../test/test_helpers.h"

/* Instantiate for int */
#define SEG_ARRAY_T    int
#define SEG_ARRAY_NAME int_seg
#include "segment_array.h"

/* Instantiate for a struct type */
typedef struct {
  int x, y;
} Point;

#define SEG_ARRAY_T    Point
#define SEG_ARRAY_NAME point_seg
#include "segment_array.h"

/* Tests */

int test_create_free(void) {
  printf("test_create_free: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);
  ASSERT(arr != NULL);
  ASSERT_INT_EQ(int_seg_count(arr), 0);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_push_get(void) {
  printf("test_push_get: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);

  int* p0 = int_seg_push(arr, 100);
  int* p1 = int_seg_push(arr, 200);
  int* p2 = int_seg_push(arr, 300);

  ASSERT(p0 != NULL);
  ASSERT(p1 != NULL);
  ASSERT(p2 != NULL);
  ASSERT_INT_EQ(int_seg_count(arr), 3);

  ASSERT_INT_EQ(*int_seg_get(arr, 0), 100);
  ASSERT_INT_EQ(*int_seg_get(arr, 1), 200);
  ASSERT_INT_EQ(*int_seg_get(arr, 2), 300);

  /* Verify stable pointers */
  ASSERT_PTR_EQ(int_seg_get(arr, 0), p0);
  ASSERT_PTR_EQ(int_seg_get(arr, 1), p1);
  ASSERT_PTR_EQ(int_seg_get(arr, 2), p2);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_stable_pointers_across_segments(void) {
  printf("test_stable_pointers_across_segments: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);

  /* Push enough elements to span multiple segments */
  /* First segment is 64 elements, second is 128, etc. */
  int* first_ptr = int_seg_push(arr, 0);

  for (int i = 1; i < 500; i++) {
    int_seg_push(arr, i);
  }

  ASSERT_INT_EQ(int_seg_count(arr), 500);

  /* First pointer should still be valid and unchanged */
  ASSERT_PTR_EQ(int_seg_get(arr, 0), first_ptr);
  ASSERT_INT_EQ(*first_ptr, 0);

  /* Verify all values */
  for (int i = 0; i < 500; i++) {
    ASSERT_INT_EQ(*int_seg_get(arr, i), i);
  }

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_swap_remove(void) {
  printf("test_swap_remove: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);

  int_seg_push(arr, 10);
  int_seg_push(arr, 20);
  int_seg_push(arr, 30);
  int_seg_push(arr, 40);

  ASSERT_INT_EQ(int_seg_count(arr), 4);

  /* Remove index 1 (value 20), should be replaced by last (40) */
  int result = int_seg_swap_remove(arr, 1);
  ASSERT_INT_EQ(result, 0);
  ASSERT_INT_EQ(int_seg_count(arr), 3);

  ASSERT_INT_EQ(*int_seg_get(arr, 0), 10);
  ASSERT_INT_EQ(*int_seg_get(arr, 1), 40);
  ASSERT_INT_EQ(*int_seg_get(arr, 2), 30);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_iterator_after_swap_remove_to_empty(void) {
  printf("test_iterator_after_swap_remove_to_empty: ");
  tracker_reset();

  /* Push past segment 0 so used_segments > 1, then swap_remove down to
   * empty. used_segments stays at 2 but count is 0. iter must report
   * empty and not read freed/uninitialized memory in segment 1. */
  int_seg_t* arr = int_seg_create(&tracked_allocator);
  for (int i = 0; i < 100; i++) int_seg_push(arr, i);  /* spans seg 0 + seg 1 */
  while (int_seg_count(arr) > 0) int_seg_swap_remove(arr, 0);
  ASSERT_INT_EQ((int)int_seg_count(arr), 0);

  int_seg_iter_t it = int_seg_iter_begin(arr);
  ASSERT(!int_seg_iter_valid(&it));
  ASSERT_PTR_EQ(int_seg_iter_next(&it), NULL);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_swap_remove_across_segments(void) {
  printf("test_swap_remove_across_segments: ");
  tracker_reset();

  /* 500 elements span ~4 segments (64+128+256+512 = 960). Removing from the
   * tail in order forces last_seg to slide down through every boundary. */
  const int N = 500;
  int_seg_t* arr = int_seg_create(&tracked_allocator);
  for (int i = 0; i < N; i++) int_seg_push(arr, i);

  /* Pop from the back, verifying value each time. */
  for (int i = N - 1; i >= 0; i--) {
    ASSERT_INT_EQ(*int_seg_get(arr, i), i);
    int rc = int_seg_swap_remove(arr, i);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ((int)int_seg_count(arr), i);
  }

  /* Reusable after empty: push back through several boundary crossings. */
  for (int i = 0; i < N; i++) int_seg_push(arr, i * 7);
  for (int i = 0; i < N; i++) ASSERT_INT_EQ(*int_seg_get(arr, i), i * 7);

  /* Interleaved random-index swap_remove until empty. */
  uint32_t lcg = 0xdeadbeefu;
  while (int_seg_count(arr) > 0) {
    lcg = lcg * 1664525u + 1013904223u;
    uint32_t idx = lcg % int_seg_count(arr);
    int rc = int_seg_swap_remove(arr, idx);
    ASSERT_INT_EQ(rc, 0);
  }
  ASSERT_INT_EQ((int)int_seg_count(arr), 0);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_struct_type(void) {
  printf("test_struct_type: ");
  tracker_reset();

  point_seg_t* arr = point_seg_create(&tracked_allocator);

  Point* p0 = point_seg_push(arr, (Point){1, 2});
  Point* p1 = point_seg_push(arr, (Point){3, 4});

  ASSERT(p0 != NULL);
  ASSERT(p1 != NULL);
  ASSERT_INT_EQ(point_seg_count(arr), 2);

  Point* got = point_seg_get(arr, 0);
  ASSERT_INT_EQ(got->x, 1);
  ASSERT_INT_EQ(got->y, 2);

  got = point_seg_get(arr, 1);
  ASSERT_INT_EQ(got->x, 3);
  ASSERT_INT_EQ(got->y, 4);

  point_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_out_of_bounds(void) {
  printf("test_out_of_bounds: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);
  int_seg_push(arr, 42);

  ASSERT_PTR_EQ(int_seg_get(arr, 1), NULL);
  ASSERT_PTR_EQ(int_seg_get(arr, 100), NULL);

  int result = int_seg_swap_remove(arr, 5);
  ASSERT_INT_EQ(result, -1);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_iterator_basic(void) {
  printf("test_iterator_basic: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);
  int_seg_push(arr, 10);
  int_seg_push(arr, 20);
  int_seg_push(arr, 30);

  int_seg_iter_t it = int_seg_iter_begin(arr);
  ASSERT(int_seg_iter_valid(&it));

  int* p = int_seg_iter_next(&it);
  ASSERT(p != NULL);
  ASSERT_INT_EQ(*p, 10);

  p = int_seg_iter_next(&it);
  ASSERT(p != NULL);
  ASSERT_INT_EQ(*p, 20);

  p = int_seg_iter_next(&it);
  ASSERT(p != NULL);
  ASSERT_INT_EQ(*p, 30);

  ASSERT(!int_seg_iter_valid(&it));
  p = int_seg_iter_next(&it);
  ASSERT_PTR_EQ(p, NULL);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_iterator_empty(void) {
  printf("test_iterator_empty: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);

  int_seg_iter_t it = int_seg_iter_begin(arr);
  ASSERT(!int_seg_iter_valid(&it));
  ASSERT_PTR_EQ(int_seg_iter_next(&it), NULL);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_iterator_across_segments(void) {
  printf("test_iterator_across_segments: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);

  /* Push 200 elements to span multiple segments */
  for (int i = 0; i < 200; i++) {
    int_seg_push(arr, i);
  }

  int_seg_iter_t it = int_seg_iter_begin(arr);
  int count = 0;
  int* p;
  while ((p = int_seg_iter_next(&it)) != NULL) {
    ASSERT_INT_EQ(*p, count);
    count++;
  }
  ASSERT_INT_EQ(count, 200);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_iterator_struct(void) {
  printf("test_iterator_struct: ");
  tracker_reset();

  point_seg_t* arr = point_seg_create(&tracked_allocator);
  point_seg_push(arr, (Point){1, 2});
  point_seg_push(arr, (Point){3, 4});
  point_seg_push(arr, (Point){5, 6});

  point_seg_iter_t it = point_seg_iter_begin(arr);
  
  Point* p = point_seg_iter_next(&it);
  ASSERT(p != NULL);
  ASSERT_INT_EQ(p->x, 1);
  ASSERT_INT_EQ(p->y, 2);

  p = point_seg_iter_next(&it);
  ASSERT(p != NULL);
  ASSERT_INT_EQ(p->x, 3);
  ASSERT_INT_EQ(p->y, 4);

  p = point_seg_iter_next(&it);
  ASSERT(p != NULL);
  ASSERT_INT_EQ(p->x, 5);
  ASSERT_INT_EQ(p->y, 6);

  ASSERT(!point_seg_iter_valid(&it));

  point_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int cmp_int_asc(const int* a, const int* b) {
  return (*a > *b) - (*a < *b);
}

static int cmp_int_desc(const int* a, const int* b) {
  return (*b > *a) - (*b < *a);
}

static int cmp_point_by_x(const Point* a, const Point* b) {
  return (a->x > b->x) - (a->x < b->x);
}

int test_sort_basic(void) {
  printf("test_sort_basic: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);
  int_seg_push(arr, 30);
  int_seg_push(arr, 10);
  int_seg_push(arr, 50);
  int_seg_push(arr, 20);
  int_seg_push(arr, 40);

  int_seg_sort(arr, cmp_int_asc);

  ASSERT_INT_EQ(*int_seg_get(arr, 0), 10);
  ASSERT_INT_EQ(*int_seg_get(arr, 1), 20);
  ASSERT_INT_EQ(*int_seg_get(arr, 2), 30);
  ASSERT_INT_EQ(*int_seg_get(arr, 3), 40);
  ASSERT_INT_EQ(*int_seg_get(arr, 4), 50);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_sort_descending(void) {
  printf("test_sort_descending: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);
  int_seg_push(arr, 1);
  int_seg_push(arr, 5);
  int_seg_push(arr, 3);
  int_seg_push(arr, 2);
  int_seg_push(arr, 4);

  int_seg_sort(arr, cmp_int_desc);

  ASSERT_INT_EQ(*int_seg_get(arr, 0), 5);
  ASSERT_INT_EQ(*int_seg_get(arr, 1), 4);
  ASSERT_INT_EQ(*int_seg_get(arr, 2), 3);
  ASSERT_INT_EQ(*int_seg_get(arr, 3), 2);
  ASSERT_INT_EQ(*int_seg_get(arr, 4), 1);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_sort_already_sorted(void) {
  printf("test_sort_already_sorted: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);
  for (int i = 0; i < 10; i++) {
    int_seg_push(arr, i);
  }

  int_seg_sort(arr, cmp_int_asc);

  for (int i = 0; i < 10; i++) {
    ASSERT_INT_EQ(*int_seg_get(arr, i), i);
  }

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_sort_single_element(void) {
  printf("test_sort_single_element: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);
  int_seg_push(arr, 42);

  int_seg_sort(arr, cmp_int_asc);

  ASSERT_INT_EQ(*int_seg_get(arr, 0), 42);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_sort_empty(void) {
  printf("test_sort_empty: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);

  int_seg_sort(arr, cmp_int_asc);

  ASSERT_INT_EQ(int_seg_count(arr), 0);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_sort_duplicates(void) {
  printf("test_sort_duplicates: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);
  int_seg_push(arr, 3);
  int_seg_push(arr, 1);
  int_seg_push(arr, 3);
  int_seg_push(arr, 1);
  int_seg_push(arr, 2);

  int_seg_sort(arr, cmp_int_asc);

  ASSERT_INT_EQ(*int_seg_get(arr, 0), 1);
  ASSERT_INT_EQ(*int_seg_get(arr, 1), 1);
  ASSERT_INT_EQ(*int_seg_get(arr, 2), 2);
  ASSERT_INT_EQ(*int_seg_get(arr, 3), 3);
  ASSERT_INT_EQ(*int_seg_get(arr, 4), 3);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_sort_across_segments(void) {
  printf("test_sort_across_segments: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);

  /* Push 200 elements in reverse order to span multiple segments */
  for (int i = 199; i >= 0; i--) {
    int_seg_push(arr, i);
  }

  int_seg_sort(arr, cmp_int_asc);

  for (int i = 0; i < 200; i++) {
    ASSERT_INT_EQ(*int_seg_get(arr, i), i);
  }

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_sort_struct(void) {
  printf("test_sort_struct: ");
  tracker_reset();

  point_seg_t* arr = point_seg_create(&tracked_allocator);
  point_seg_push(arr, (Point){5, 1});
  point_seg_push(arr, (Point){2, 2});
  point_seg_push(arr, (Point){8, 3});
  point_seg_push(arr, (Point){1, 4});

  point_seg_sort(arr, cmp_point_by_x);

  ASSERT_INT_EQ(point_seg_get(arr, 0)->x, 1);
  ASSERT_INT_EQ(point_seg_get(arr, 1)->x, 2);
  ASSERT_INT_EQ(point_seg_get(arr, 2)->x, 5);
  ASSERT_INT_EQ(point_seg_get(arr, 3)->x, 8);

  /* Verify y values followed their x values */
  ASSERT_INT_EQ(point_seg_get(arr, 0)->y, 4);
  ASSERT_INT_EQ(point_seg_get(arr, 1)->y, 2);
  ASSERT_INT_EQ(point_seg_get(arr, 2)->y, 1);
  ASSERT_INT_EQ(point_seg_get(arr, 3)->y, 3);

  point_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Simple xorshift PRNG for deterministic shuffling */
static uint32_t xorshift32(uint32_t* state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

int test_sort_stress(void) {
  printf("test_sort_stress: ");
  tracker_reset();

  const int N = 100000;
  int_seg_t* arr = int_seg_create(&tracked_allocator);

  /* Fill with pseudo-random values using a seeded PRNG */
  uint32_t rng = 12345;
  for (int i = 0; i < N; i++) {
    int_seg_push(arr, (int)(xorshift32(&rng) % 50000));
  }

  int_seg_sort(arr, cmp_int_asc);

  /* Verify sorted order */
  for (int i = 1; i < N; i++) {
    ASSERT(*int_seg_get(arr, i - 1) <= *int_seg_get(arr, i));
  }

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_sort_stress_reverse(void) {
  printf("test_sort_stress_reverse: ");
  tracker_reset();

  const int N = 100000;
  int_seg_t* arr = int_seg_create(&tracked_allocator);

  /* Worst case for many sorts: fully reversed input */
  for (int i = N - 1; i >= 0; i--) {
    int_seg_push(arr, i);
  }

  int_seg_sort(arr, cmp_int_asc);

  for (int i = 0; i < N; i++) {
    ASSERT_INT_EQ(*int_seg_get(arr, i), i);
  }

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_sort_stress_all_equal(void) {
  printf("test_sort_stress_all_equal: ");
  tracker_reset();

  const int N = 100000;
  int_seg_t* arr = int_seg_create(&tracked_allocator);

  for (int i = 0; i < N; i++) {
    int_seg_push(arr, 42);
  }

  int_seg_sort(arr, cmp_int_asc);

  for (int i = 0; i < N; i++) {
    ASSERT_INT_EQ(*int_seg_get(arr, i), 42);
  }

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_set(void) {
  printf("test_set: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);
  int* p0 = int_seg_push(arr, 10);
  int_seg_push(arr, 20);
  int_seg_push(arr, 30);

  /* In-bounds set returns the (stable) slot pointer and updates in place. */
  int* sp = int_seg_set(arr, 1, 99);
  ASSERT(sp != NULL);
  ASSERT_INT_EQ(*int_seg_get(arr, 1), 99);
  ASSERT_INT_EQ(int_seg_count(arr), 3);          /* count unchanged */
  ASSERT_PTR_EQ(int_seg_set(arr, 0, 11), p0);    /* stable pointer */
  ASSERT_INT_EQ(*int_seg_get(arr, 0), 11);

  /* Out-of-bounds set is a no-op that returns NULL. */
  ASSERT(int_seg_set(arr, 3, 123) == NULL);
  ASSERT(int_seg_set(arr, 999, 123) == NULL);
  ASSERT_INT_EQ(int_seg_count(arr), 3);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_pop(void) {
  printf("test_pop: ");
  tracker_reset();

  int_seg_t* arr = int_seg_create(&tracked_allocator);

  /* Pop on empty returns -1 and leaves count at 0. */
  int out = -777;
  ASSERT_INT_EQ(int_seg_pop(arr, &out), -1);
  ASSERT_INT_EQ(out, -777);                       /* out untouched */
  ASSERT_INT_EQ(int_seg_count(arr), 0);

  /* Push past the first two segment boundaries (64, then 192) so pop has to
   * slide the cursor back across segments. */
  for (int i = 0; i < 300; i++) int_seg_push(arr, i);
  ASSERT_INT_EQ(int_seg_count(arr), 300);

  /* Order-preserving: pops come back 299, 298, ... and remaining order holds. */
  for (int i = 299; i >= 0; i--) {
    out = -1;
    ASSERT_INT_EQ(int_seg_pop(arr, &out), 0);
    ASSERT_INT_EQ(out, i);
    ASSERT_INT_EQ((int)int_seg_count(arr), i);
    if (i > 0) ASSERT_INT_EQ(*int_seg_get(arr, i - 1), i - 1);
  }
  ASSERT_INT_EQ(int_seg_count(arr), 0);

  /* Empty again after draining. */
  ASSERT_INT_EQ(int_seg_pop(arr, &out), -1);

  /* Refill after full drain still works (segments were retained). */
  int_seg_push(arr, 42);
  ASSERT_INT_EQ(*int_seg_get(arr, 0), 42);
  ASSERT_INT_EQ(int_seg_count(arr), 1);

  /* Pop with NULL out is allowed (discards the value). */
  ASSERT_INT_EQ(int_seg_pop(arr, NULL), 0);
  ASSERT_INT_EQ(int_seg_count(arr), 0);

  int_seg_free(arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int main(void) {
  int passed = 0;
  int total  = 0;

  total++; passed += test_create_free();
  total++; passed += test_push_get();
  total++; passed += test_stable_pointers_across_segments();
  total++; passed += test_set();
  total++; passed += test_pop();
  total++; passed += test_swap_remove();
  total++; passed += test_swap_remove_across_segments();
  total++; passed += test_iterator_after_swap_remove_to_empty();
  total++; passed += test_struct_type();
  total++; passed += test_out_of_bounds();
  total++; passed += test_iterator_basic();
  total++; passed += test_iterator_empty();
  total++; passed += test_iterator_across_segments();
  total++; passed += test_iterator_struct();
  total++; passed += test_sort_basic();
  total++; passed += test_sort_descending();
  total++; passed += test_sort_already_sorted();
  total++; passed += test_sort_single_element();
  total++; passed += test_sort_empty();
  total++; passed += test_sort_duplicates();
  total++; passed += test_sort_across_segments();
  total++; passed += test_sort_struct();
  total++; passed += test_sort_stress();
  total++; passed += test_sort_stress_reverse();
  total++; passed += test_sort_stress_all_equal();

  printf("\n%d/%d tests passed\n", passed, total);
  return (passed == total) ? 0 : 1;
}
