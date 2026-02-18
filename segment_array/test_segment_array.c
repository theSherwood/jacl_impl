#include "../test/test_helpers.h"

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

int main(void) {
  int passed = 0;
  int total  = 0;

  total++; passed += test_create_free();
  total++; passed += test_push_get();
  total++; passed += test_stable_pointers_across_segments();
  total++; passed += test_swap_remove();
  total++; passed += test_struct_type();
  total++; passed += test_out_of_bounds();
  total++; passed += test_iterator_basic();
  total++; passed += test_iterator_empty();
  total++; passed += test_iterator_across_segments();
  total++; passed += test_iterator_struct();

  printf("\n%d/%d tests passed\n", passed, total);
  return (passed == total) ? 0 : 1;
}
