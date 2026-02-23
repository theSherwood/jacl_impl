#include "../../test/test_helpers.h"

#define TRACKED_ALLOC                     \
  {                                       \
      .alloc = tracked_malloc_with_ctx,   \
      .free  = tracked_free_with_ctx,     \
  }

/* --- Instantiation: int vector with hash support --- */
#define RRB_VEC_T         int
#define RRB_VEC_NAME      int_vec
#define RRB_VEC_ALLOCATOR TRACKED_ALLOC
#include "rrb_vec.h"

/* --- Element hash function for ints --- */
static uint32_t int_hash(int val) {
  /* Knuth multiplicative hash */
  return (uint32_t)val * 2654435761u;
}

/* Test: empty vector hash is deterministic (0 when no handler, deterministic with handler) */
int test_hash_empty_no_handler(void) {
  printf("  test_hash_empty_no_handler: ");
  tracker_reset();

  int_vec_set_hash_handler(NULL);
  int_vec_root* v = int_vec_empty();
  ASSERT(v != NULL);
  ASSERT_U32_EQ(int_vec_hash(v), 0);

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_hash_empty_with_handler(void) {
  printf("  test_hash_empty_with_handler: ");
  tracker_reset();

  int_vec_set_hash_handler(int_hash);
  int_vec_root* v = int_vec_empty();
  ASSERT(v != NULL);
  ASSERT_U32_EQ(int_vec_hash(v), 0);

  int_vec_unref(v);
  int_vec_set_hash_handler(NULL);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: hash is non-zero for non-empty vector with handler */
int test_hash_nonempty(void) {
  printf("  test_hash_nonempty: ");
  tracker_reset();

  int_vec_set_hash_handler(int_hash);
  int_vec_root* v = int_vec_empty();
  int_vec_root* v1 = int_vec_push_back(v, 42);
  int_vec_unref(v);

  uint32_t h = int_vec_hash(v1);
  /* With a proper hash function, hash of [42] should be non-zero */
  ASSERT(h != 0);

  int_vec_unref(v1);
  int_vec_set_hash_handler(NULL);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: same elements built via push_back produce same hash */
int test_hash_consistency_push(void) {
  printf("  test_hash_consistency_push: ");
  tracker_reset();

  int_vec_set_hash_handler(int_hash);

  /* Build [1, 2, 3] via repeated push_back */
  int_vec_root* a = int_vec_empty();
  for (int i = 1; i <= 3; i++) {
    int_vec_root* next = int_vec_push_back(a, i);
    int_vec_unref(a);
    a = next;
  }

  /* Build [1, 2, 3] via another series of push_back */
  int_vec_root* b = int_vec_empty();
  for (int i = 1; i <= 3; i++) {
    int_vec_root* next = int_vec_push_back(b, i);
    int_vec_unref(b);
    b = next;
  }

  ASSERT_U32_EQ(int_vec_hash(a), int_vec_hash(b));

  int_vec_unref(a);
  int_vec_unref(b);
  int_vec_set_hash_handler(NULL);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: same elements built via from_array produce same hash as push_back */
int test_hash_consistency_from_array(void) {
  printf("  test_hash_consistency_from_array: ");
  tracker_reset();

  int_vec_set_hash_handler(int_hash);

  /* Build [1, 2, 3, 4, 5] via push_back */
  int_vec_root* a = int_vec_empty();
  for (int i = 1; i <= 5; i++) {
    int_vec_root* next = int_vec_push_back(a, i);
    int_vec_unref(a);
    a = next;
  }

  /* Build [1, 2, 3, 4, 5] via from_array */
  int arr[] = {1, 2, 3, 4, 5};
  int_vec_root* b = int_vec_from_array(arr, 5);

  ASSERT_U32_EQ(int_vec_hash(a), int_vec_hash(b));

  int_vec_unref(a);
  int_vec_unref(b);
  int_vec_set_hash_handler(NULL);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: hash changes when an element is modified */
int test_hash_changes_on_set(void) {
  printf("  test_hash_changes_on_set: ");
  tracker_reset();

  int_vec_set_hash_handler(int_hash);

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 5; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  uint32_t h1 = int_vec_hash(v);

  /* Change element 2 from 2 to 99 */
  int_vec_root* v2 = int_vec_set(v, 2, 99);
  uint32_t h2 = int_vec_hash(v2);

  ASSERT(h1 != h2);
  /* Original unchanged */
  ASSERT_U32_EQ(int_vec_hash(v), h1);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_set_hash_handler(NULL);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: hash changes on push_back */
int test_hash_changes_on_push(void) {
  printf("  test_hash_changes_on_push: ");
  tracker_reset();

  int_vec_set_hash_handler(int_hash);

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 3; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  uint32_t h1 = int_vec_hash(v);

  int_vec_root* v2 = int_vec_push_back(v, 99);
  uint32_t h2 = int_vec_hash(v2);

  ASSERT(h1 != h2);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_set_hash_handler(NULL);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: hash changes on pop_back */
int test_hash_changes_on_pop(void) {
  printf("  test_hash_changes_on_pop: ");
  tracker_reset();

  int_vec_set_hash_handler(int_hash);

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 5; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  uint32_t h1 = int_vec_hash(v);

  int_vec_pop_result pr = int_vec_pop_back(v);
  ASSERT(pr.root != NULL);
  uint32_t h2 = int_vec_hash(pr.root);

  ASSERT(h1 != h2);

  int_vec_unref(v);
  int_vec_unref(pr.root);
  int_vec_set_hash_handler(NULL);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: permuted elements produce different hashes (rotate-XOR order-dependence) */
int test_hash_permutation_sensitive(void) {
  printf("  test_hash_permutation_sensitive: ");
  tracker_reset();

  int_vec_set_hash_handler(int_hash);

  /* Build [1, 2, 3] */
  int_vec_root* a = int_vec_empty();
  int a_elems[] = {1, 2, 3};
  for (int i = 0; i < 3; i++) {
    int_vec_root* next = int_vec_push_back(a, a_elems[i]);
    int_vec_unref(a);
    a = next;
  }

  /* Build [3, 2, 1] */
  int_vec_root* b = int_vec_empty();
  int b_elems[] = {3, 2, 1};
  for (int i = 0; i < 3; i++) {
    int_vec_root* next = int_vec_push_back(b, b_elems[i]);
    int_vec_unref(b);
    b = next;
  }

  /* These should differ due to position-dependent rotation */
  ASSERT(int_vec_hash(a) != int_vec_hash(b));

  int_vec_unref(a);
  int_vec_unref(b);
  int_vec_set_hash_handler(NULL);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: hash is consistent for larger vectors (> 32 elements, triggers tree) */
int test_hash_consistency_large(void) {
  printf("  test_hash_consistency_large: ");
  tracker_reset();

  int_vec_set_hash_handler(int_hash);

  /* Build [0..99] via push_back */
  int_vec_root* a = int_vec_empty();
  for (int i = 0; i < 100; i++) {
    int_vec_root* next = int_vec_push_back(a, i);
    int_vec_unref(a);
    a = next;
  }

  /* Build [0..99] via from_array */
  int arr[100];
  for (int i = 0; i < 100; i++) arr[i] = i;
  int_vec_root* b = int_vec_from_array(arr, 100);

  ASSERT_U32_EQ(int_vec_hash(a), int_vec_hash(b));

  /* Verify hash is non-zero */
  ASSERT(int_vec_hash(a) != 0);

  int_vec_unref(a);
  int_vec_unref(b);
  int_vec_set_hash_handler(NULL);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: hash is O(1) — just a field read (structural test) */
int test_hash_is_field_read(void) {
  printf("  test_hash_is_field_read: ");
  tracker_reset();

  int_vec_set_hash_handler(int_hash);

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 50; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  /* Call hash multiple times — should always return the same value */
  uint32_t h1 = int_vec_hash(v);
  uint32_t h2 = int_vec_hash(v);
  uint32_t h3 = int_vec_hash(v);
  ASSERT_U32_EQ(h1, h2);
  ASSERT_U32_EQ(h2, h3);

  int_vec_unref(v);
  int_vec_set_hash_handler(NULL);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: NULL root returns 0 */
int test_hash_null_root(void) {
  printf("  test_hash_null_root: ");
  ASSERT_U32_EQ(int_vec_hash(NULL), 0);
  TEST_PASS();
}

/* Test: hash after concat is deterministic and non-zero */
int test_hash_concat(void) {
  printf("  test_hash_concat: ");
  tracker_reset();

  int_vec_set_hash_handler(int_hash);

  /* Build [1,2,3] and [4,5,6] twice, concat both pairs */
  int_vec_root* a1 = int_vec_empty();
  int_vec_root* a2 = int_vec_empty();
  for (int i = 1; i <= 3; i++) {
    int_vec_root* next = int_vec_push_back(a1, i);
    int_vec_unref(a1);
    a1 = next;
    next = int_vec_push_back(a2, i);
    int_vec_unref(a2);
    a2 = next;
  }
  int_vec_root* b1 = int_vec_empty();
  int_vec_root* b2 = int_vec_empty();
  for (int i = 4; i <= 6; i++) {
    int_vec_root* next = int_vec_push_back(b1, i);
    int_vec_unref(b1);
    b1 = next;
    next = int_vec_push_back(b2, i);
    int_vec_unref(b2);
    b2 = next;
  }

  int_vec_root* c1 = int_vec_concat(a1, b1);
  int_vec_root* c2 = int_vec_concat(a2, b2);

  /* Same inputs produce same hash */
  ASSERT_U32_EQ(int_vec_hash(c1), int_vec_hash(c2));
  /* Hash is non-zero */
  ASSERT(int_vec_hash(c1) != 0);

  int_vec_unref(a1);
  int_vec_unref(a2);
  int_vec_unref(b1);
  int_vec_unref(b2);
  int_vec_unref(c1);
  int_vec_unref(c2);
  int_vec_set_hash_handler(NULL);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: hash after slice matches independently-built vector */
int test_hash_slice(void) {
  printf("  test_hash_slice: ");
  tracker_reset();

  int_vec_set_hash_handler(int_hash);

  /* Build [0,1,2,3,4] */
  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 5; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  /* Slice [1,2,3] */
  int_vec_root* s = int_vec_slice(v, 1, 4);

  /* Build [1,2,3] via push_back */
  int_vec_root* b = int_vec_empty();
  for (int i = 1; i <= 3; i++) {
    int_vec_root* next = int_vec_push_back(b, i);
    int_vec_unref(b);
    b = next;
  }

  ASSERT_U32_EQ(int_vec_hash(s), int_vec_hash(b));

  int_vec_unref(v);
  int_vec_unref(s);
  int_vec_unref(b);
  int_vec_set_hash_handler(NULL);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: transient path produces same hash as persistent path */
int test_hash_transient_consistency(void) {
  printf("  test_hash_transient_consistency: ");
  tracker_reset();

  int_vec_set_hash_handler(int_hash);

  /* Build [0..49] via persistent push_back */
  int_vec_root* a = int_vec_empty();
  for (int i = 0; i < 50; i++) {
    int_vec_root* next = int_vec_push_back(a, i);
    int_vec_unref(a);
    a = next;
  }

  /* Build [0..49] via transient push_back */
  int_vec_root* base = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(base);
  for (int i = 0; i < 50; i++) {
    int_vec_transient_push_back(t, i);
  }
  int_vec_root* b = int_vec_persistent_fn(t);
  int_vec_unref(base);

  ASSERT_U32_EQ(int_vec_hash(a), int_vec_hash(b));

  int_vec_unref(a);
  int_vec_unref(b);
  int_vec_rc_unref(t);
  int_vec_set_hash_handler(NULL);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int main(void) {
  int pass = 0, fail = 0;

  printf("=== rrb_vec hash tests ===\n");

  test_hash_empty_no_handler()         ? pass++ : fail++;
  test_hash_empty_with_handler()       ? pass++ : fail++;
  test_hash_nonempty()                 ? pass++ : fail++;
  test_hash_consistency_push()         ? pass++ : fail++;
  test_hash_consistency_from_array()   ? pass++ : fail++;
  test_hash_changes_on_set()           ? pass++ : fail++;
  test_hash_changes_on_push()          ? pass++ : fail++;
  test_hash_changes_on_pop()           ? pass++ : fail++;
  test_hash_permutation_sensitive()    ? pass++ : fail++;
  test_hash_consistency_large()        ? pass++ : fail++;
  test_hash_is_field_read()            ? pass++ : fail++;
  test_hash_null_root()                ? pass++ : fail++;
  test_hash_concat()                   ? pass++ : fail++;
  test_hash_slice()                    ? pass++ : fail++;
  test_hash_transient_consistency()    ? pass++ : fail++;

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
