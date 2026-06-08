#include "../../test/test_helpers.h"
#include "segment_array_var.h"

typedef struct { int a, b, c; } Triple; /* 12 bytes — exercises a non-pow2-of-8 stride */

int test_var_push_get_int(void) {
  printf("test_var_push_get_int: ");
  tracker_reset();
  sa_var_t sa;
  sa_var_init(&sa, &tracked_allocator, sizeof(int));

  for (int i = 0; i < 5; i++) {
    int v = i * 100;
    uint8_t* p = sa_var_push(&sa, &v);
    ASSERT(p != NULL);
  }
  ASSERT_INT_EQ((int)sa_var_count(&sa), 5);
  for (int i = 0; i < 5; i++) {
    ASSERT_INT_EQ(*(int*)sa_var_get(&sa, (uint32_t)i), i * 100);
  }
  /* OOB get is NULL */
  ASSERT(sa_var_get(&sa, 5) == NULL);

  sa_var_free_segments(&sa);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_var_stable_across_segments(void) {
  printf("test_var_stable_across_segments: ");
  tracker_reset();
  sa_var_t sa;
  sa_var_init(&sa, &tracked_allocator, sizeof(Triple));

  /* Cache element pointers as we cross the 64 / 192 segment boundaries. */
  uint8_t* ptrs[300];
  for (int i = 0; i < 300; i++) {
    Triple t = { i, i + 1, i + 2 };
    ptrs[i] = sa_var_push(&sa, &t);
    ASSERT(ptrs[i] != NULL);
  }
  ASSERT_INT_EQ((int)sa_var_count(&sa), 300);
  /* Pointers stay valid (no relocation) and values intact. */
  for (int i = 0; i < 300; i++) {
    ASSERT_PTR_EQ(sa_var_get(&sa, (uint32_t)i), ptrs[i]);
    Triple* t = (Triple*)ptrs[i];
    ASSERT_INT_EQ(t->a, i);
    ASSERT_INT_EQ(t->b, i + 1);
    ASSERT_INT_EQ(t->c, i + 2);
  }
  sa_var_free_segments(&sa);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_var_set(void) {
  printf("test_var_set: ");
  tracker_reset();
  sa_var_t sa;
  sa_var_init(&sa, &tracked_allocator, sizeof(int));
  for (int i = 0; i < 3; i++) { int v = i; sa_var_push(&sa, &v); }

  int nv = 99;
  uint8_t* p = sa_var_set(&sa, 1, &nv);
  ASSERT(p != NULL);
  ASSERT_INT_EQ(*(int*)sa_var_get(&sa, 1), 99);
  ASSERT_INT_EQ((int)sa_var_count(&sa), 3);     /* set doesn't grow */
  ASSERT(sa_var_set(&sa, 3, &nv) == NULL);      /* OOB no-op */

  sa_var_free_segments(&sa);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_var_push_zero(void) {
  printf("test_var_push_zero: ");
  tracker_reset();
  sa_var_t sa;
  sa_var_init(&sa, &tracked_allocator, sizeof(Triple));
  uint8_t* p = sa_var_push_zero(&sa);
  ASSERT(p != NULL);
  Triple* t = (Triple*)p;
  ASSERT_INT_EQ(t->a, 0);
  ASSERT_INT_EQ(t->b, 0);
  ASSERT_INT_EQ(t->c, 0);
  sa_var_free_segments(&sa);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_var_pop(void) {
  printf("test_var_pop: ");
  tracker_reset();
  sa_var_t sa;
  sa_var_init(&sa, &tracked_allocator, sizeof(int));

  int out = -777;
  ASSERT_INT_EQ(sa_var_pop(&sa, &out), -1);     /* empty */
  ASSERT_INT_EQ(out, -777);

  for (int i = 0; i < 300; i++) { int v = i; sa_var_push(&sa, &v); }
  for (int i = 299; i >= 0; i--) {
    out = -1;
    ASSERT_INT_EQ(sa_var_pop(&sa, &out), 0);
    ASSERT_INT_EQ(out, i);
    ASSERT_INT_EQ((int)sa_var_count(&sa), i);
    if (i > 0) ASSERT_INT_EQ(*(int*)sa_var_get(&sa, (uint32_t)(i - 1)), i - 1);
  }
  /* Refill after full drain reuses retained segments. */
  int v = 42;
  sa_var_push(&sa, &v);
  ASSERT_INT_EQ(*(int*)sa_var_get(&sa, 0), 42);

  sa_var_free_segments(&sa);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int main(void) {
  int passed = 0;
  int total  = 0;
  total++; passed += test_var_push_get_int();
  total++; passed += test_var_stable_across_segments();
  total++; passed += test_var_set();
  total++; passed += test_var_push_zero();
  total++; passed += test_var_pop();
  printf("\n%d/%d tests passed\n", passed, total);
  return (passed == total) ? 0 : 1;
}
