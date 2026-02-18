#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../test/test_helpers.h"

/* ========================================================================
 * Instantiation 1: uint64_t keys, int32_t values
 * ======================================================================== */

#define HAMT_ALLOCATOR                  \
  {                                     \
      .alloc = tracked_malloc_with_ctx, \
      .free  = tracked_free_with_ctx,   \
  }

#define HAMT_KEY_T  uint64_t
#define HAMT_VAL_T  int32_t
#define HAMT_NAME   u64i32
#include "hamt.h"

/* --- Hash / equality for uint64_t keys --- */

static uint32_t u64_hash(uint64_t key) {
  /* Simple integer hash (splitmix-style) */
  uint64_t x = key;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  x = x ^ (x >> 31);
  return (uint32_t)x;
}

static bool u64_eq(uint64_t a, uint64_t b) {
  return a == b;
}

/* --- Helper to update root --- */

static u64i32_node* u64i32_update_root(u64i32_node* root, u64i32_node* new_root) {
  if (root) u64i32_unref(root);
  return new_root;
}

/* ========================================================================
 * Instantiation 2: point_t keys, color_t values
 * ======================================================================== */

typedef struct { int32_t x; int32_t y; } point_t;
typedef struct { uint8_t r; uint8_t g; uint8_t b; uint8_t a; } color_t;

#define HAMT_ALLOCATOR                  \
  {                                     \
      .alloc = tracked_malloc_with_ctx, \
      .free  = tracked_free_with_ctx,   \
  }

#define HAMT_KEY_T  point_t
#define HAMT_VAL_T  color_t
#define HAMT_NAME   pt_color
#include "hamt.h"

/* --- Hash / equality for point_t keys --- */

static uint32_t point_hash(point_t key) {
  uint32_t hx = (uint32_t)key.x;
  uint32_t hy = (uint32_t)key.y;
  hx = ((hx >> 16) ^ hx) * 0x45d9f3b;
  hx = ((hx >> 16) ^ hx) * 0x45d9f3b;
  hx = (hx >> 16) ^ hx;
  hy = ((hy >> 16) ^ hy) * 0x45d9f3b;
  hy = ((hy >> 16) ^ hy) * 0x45d9f3b;
  hy = (hy >> 16) ^ hy;
  return hx ^ (hy * 0x9e3779b9 + (hx << 6) + (hx >> 2));
}

static bool point_eq(point_t a, point_t b) {
  return a.x == b.x && a.y == b.y;
}

/* Forced-collision hash: always returns the same value */
static uint32_t point_hash_constant(point_t key) {
  (void)key;
  return 0xDEADBEEF;
}

/* --- Helper to update root --- */

static pt_color_node* pt_color_update_root(pt_color_node* root, pt_color_node* new_root) {
  if (root) pt_color_unref(root);
  return new_root;
}

/* --- Helper to compare color_t --- */

static bool color_eq(color_t a, color_t b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/* === US-001 Tests === */

int test_u64i32_set_get() {
  printf("Running test_u64i32_set_get... ");
  tracker_reset();
  u64i32_set_key_handlers(u64_hash, u64_eq);
  u64i32_set_lifecycle_hooks(NULL, NULL);

  u64i32_node* root = NULL;
  for (uint64_t i = 0; i < 100; i++) {
    root = u64i32_update_root(root, u64i32_set(root, i, (int32_t)(i * 7)));
  }

  ASSERT(u64i32_count(root) == 100);

  for (uint64_t i = 0; i < 100; i++) {
    int32_t val = u64i32_get(root, i);
    ASSERT(val == (int32_t)(i * 7));
  }

  u64i32_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_u64i32_has() {
  printf("Running test_u64i32_has... ");
  tracker_reset();
  u64i32_set_key_handlers(u64_hash, u64_eq);
  u64i32_set_lifecycle_hooks(NULL, NULL);

  u64i32_node* root = NULL;
  root = u64i32_set(root, 42, 100);
  root = u64i32_update_root(root, u64i32_set(root, 99, 200));

  ASSERT(u64i32_has(root, 42) == true);
  ASSERT(u64i32_has(root, 99) == true);
  ASSERT(u64i32_has(root, 0) == false);
  ASSERT(u64i32_has(root, 1000) == false);

  u64i32_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_u64i32_get_or_default() {
  printf("Running test_u64i32_get_or_default... ");
  tracker_reset();
  u64i32_set_key_handlers(u64_hash, u64_eq);
  u64i32_set_lifecycle_hooks(NULL, NULL);

  u64i32_node* root = NULL;
  root = u64i32_set(root, 10, 555);

  ASSERT(u64i32_get_or_default(root, 10, -1) == 555);
  ASSERT(u64i32_get_or_default(root, 999, -1) == -1);

  u64i32_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_u64i32_unset() {
  printf("Running test_u64i32_unset... ");
  tracker_reset();
  u64i32_set_key_handlers(u64_hash, u64_eq);
  u64i32_set_lifecycle_hooks(NULL, NULL);

  u64i32_node* root = NULL;
  for (uint64_t i = 0; i < 100; i++) {
    root = u64i32_update_root(root, u64i32_set(root, i, (int32_t)(i * 7)));
  }

  /* Remove even keys */
  for (uint64_t i = 0; i < 100; i += 2) {
    root = u64i32_update_root(root, u64i32_unset(root, i));
  }

  ASSERT(u64i32_count(root) == 50);

  /* Removed keys should return 0 (the {0} default) */
  for (uint64_t i = 0; i < 100; i += 2) {
    ASSERT(u64i32_get(root, i) == 0);
  }

  /* Remaining keys should still be correct */
  for (uint64_t i = 1; i < 100; i += 2) {
    ASSERT(u64i32_get(root, i) == (int32_t)(i * 7));
  }

  u64i32_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_u64i32_count() {
  printf("Running test_u64i32_count... ");
  tracker_reset();
  u64i32_set_key_handlers(u64_hash, u64_eq);
  u64i32_set_lifecycle_hooks(NULL, NULL);

  u64i32_node* root = NULL;
  ASSERT(u64i32_count(root) == 0);

  for (uint64_t i = 0; i < 20; i++) {
    root = u64i32_update_root(root, u64i32_set(root, i, (int32_t)i));
  }
  ASSERT(u64i32_count(root) == 20);

  for (uint64_t i = 0; i < 10; i++) {
    root = u64i32_update_root(root, u64i32_unset(root, i));
  }
  ASSERT(u64i32_count(root) == 10);

  u64i32_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_u64i32_persistence() {
  printf("Running test_u64i32_persistence... ");
  tracker_reset();
  u64i32_set_key_handlers(u64_hash, u64_eq);
  u64i32_set_lifecycle_hooks(NULL, NULL);

  /* t1: 10 entries */
  u64i32_node* t1 = NULL;
  for (uint64_t i = 0; i < 10; i++) {
    t1 = u64i32_update_root(t1, u64i32_set(t1, i, (int32_t)(i + 1)));
  }
  ASSERT(u64i32_count(t1) == 10);

  /* t2: t1 + 5 more entries */
  u64i32_node* t2 = t1;
  u64i32_ref((u64i32_node*)t2);
  for (uint64_t i = 10; i < 15; i++) {
    t2 = u64i32_update_root(t2, u64i32_set(t2, i, (int32_t)(i + 1)));
  }

  ASSERT(u64i32_count(t1) == 10);
  ASSERT(u64i32_count(t2) == 15);

  /* t1 should not have keys 10..14 */
  for (uint64_t i = 10; i < 15; i++) {
    ASSERT(u64i32_has(t1, i) == false);
  }

  /* t2 should have all keys 0..14 */
  for (uint64_t i = 0; i < 15; i++) {
    ASSERT(u64i32_has(t2, i) == true);
    ASSERT(u64i32_get(t2, i) == (int32_t)(i + 1));
  }

  u64i32_unref(t1);
  u64i32_unref(t2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_u64i32_iteration() {
  printf("Running test_u64i32_iteration... ");
  tracker_reset();
  u64i32_set_key_handlers(u64_hash, u64_eq);
  u64i32_set_lifecycle_hooks(NULL, NULL);

  u64i32_node* root = NULL;
  for (uint64_t i = 0; i < 50; i++) {
    root = u64i32_update_root(root, u64i32_set(root, i, (int32_t)(i * 3)));
  }

  /* Track which keys we've seen */
  bool seen[50];
  memset(seen, 0, sizeof(seen));
  int count = 0;

  u64i32_iter        it = u64i32_iter_init(root);
  u64i32_iter_result result;
  while (true) {
    result = u64i32_next_leaf(&it);
    if (result.done) break;
    uint64_t key = u64i32_key_from_leaf(result.item);
    int32_t  val = u64i32_value_from_leaf(result.item);
    ASSERT(key < 50);
    ASSERT(val == (int32_t)(key * 3));
    ASSERT(seen[key] == false);
    seen[key] = true;
    count++;
  }

  ASSERT(count == 50);
  for (int i = 0; i < 50; i++) {
    ASSERT(seen[i] == true);
  }

  u64i32_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-002 Tests === */

int test_pt_color_set_get() {
  printf("Running test_pt_color_set_get... ");
  tracker_reset();
  pt_color_set_key_handlers(point_hash, point_eq);
  pt_color_set_lifecycle_hooks(NULL, NULL);

  pt_color_node* root = NULL;
  for (int i = 0; i < 20; i++) {
    point_t p = {.x = i, .y = i * 2};
    color_t c = {.r = (uint8_t)(i * 10), .g = (uint8_t)(i * 5), .b = (uint8_t)(i * 3), .a = 255};
    root = pt_color_update_root(root, pt_color_set(root, p, c));
  }

  ASSERT(pt_color_count(root) == 20);

  for (int i = 0; i < 20; i++) {
    point_t p   = {.x = i, .y = i * 2};
    color_t val = pt_color_get(root, p);
    ASSERT(val.r == (uint8_t)(i * 10));
    ASSERT(val.g == (uint8_t)(i * 5));
    ASSERT(val.b == (uint8_t)(i * 3));
    ASSERT(val.a == 255);
  }

  pt_color_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_pt_color_get_missing_returns_zero() {
  printf("Running test_pt_color_get_missing_returns_zero... ");
  tracker_reset();
  pt_color_set_key_handlers(point_hash, point_eq);
  pt_color_set_lifecycle_hooks(NULL, NULL);

  pt_color_node* root = NULL;
  point_t p = {.x = 1, .y = 1};
  color_t c = {.r = 100, .g = 200, .b = 50, .a = 255};
  root = pt_color_set(root, p, c);

  /* Missing key should return zero-initialized color_t */
  point_t missing = {.x = 999, .y = 999};
  color_t result  = pt_color_get(root, missing);
  ASSERT(result.r == 0);
  ASSERT(result.g == 0);
  ASSERT(result.b == 0);
  ASSERT(result.a == 0);

  pt_color_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_pt_color_key_from_leaf_null() {
  printf("Running test_pt_color_key_from_leaf_null... ");
  tracker_reset();

  /* NULL leaf should return zero-initialized point_t */
  point_t key = pt_color_key_from_leaf(NULL);
  ASSERT(key.x == 0);
  ASSERT(key.y == 0);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_pt_color_unset() {
  printf("Running test_pt_color_unset... ");
  tracker_reset();
  pt_color_set_key_handlers(point_hash, point_eq);
  pt_color_set_lifecycle_hooks(NULL, NULL);

  pt_color_node* root = NULL;
  for (int i = 0; i < 10; i++) {
    point_t p = {.x = i, .y = i};
    color_t c = {.r = (uint8_t)i, .g = 0, .b = 0, .a = 255};
    root = pt_color_update_root(root, pt_color_set(root, p, c));
  }

  /* Remove entries with even x */
  for (int i = 0; i < 10; i += 2) {
    point_t p = {.x = i, .y = i};
    root = pt_color_update_root(root, pt_color_unset(root, p));
  }

  ASSERT(pt_color_count(root) == 5);

  /* Removed keys should be gone */
  for (int i = 0; i < 10; i += 2) {
    point_t p = {.x = i, .y = i};
    ASSERT(pt_color_has(root, p) == false);
  }

  /* Remaining keys should still be correct */
  for (int i = 1; i < 10; i += 2) {
    point_t p   = {.x = i, .y = i};
    color_t val = pt_color_get(root, p);
    ASSERT(val.r == (uint8_t)i);
    ASSERT(val.a == 255);
  }

  pt_color_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_pt_color_persistence() {
  printf("Running test_pt_color_persistence... ");
  tracker_reset();
  pt_color_set_key_handlers(point_hash, point_eq);
  pt_color_set_lifecycle_hooks(NULL, NULL);

  /* v1: insert a point with one color */
  point_t p = {.x = 5, .y = 10};
  color_t c1 = {.r = 255, .g = 0, .b = 0, .a = 255};
  pt_color_node* v1 = pt_color_set(NULL, p, c1);

  /* v2: same point, different color — ref v1 so it survives */
  pt_color_ref(v1);  /* v1 refcount: 1 (creation) + 1 (ref) = 2 */
  color_t c2 = {.r = 0, .g = 255, .b = 0, .a = 128};
  pt_color_node* v2 = pt_color_set(v1, p, c2);

  /* v1 should have old color */
  color_t val1 = pt_color_get(v1, p);
  ASSERT(color_eq(val1, c1));

  /* v2 should have new color */
  color_t val2 = pt_color_get(v2, p);
  ASSERT(color_eq(val2, c2));

  pt_color_unref(v1);  /* undo the extra ref */
  pt_color_unref(v1);  /* undo the creation ref */
  pt_color_unref(v2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_pt_color_collision() {
  printf("Running test_pt_color_collision... ");
  tracker_reset();
  /* Use constant hash to force collisions */
  pt_color_set_key_handlers(point_hash_constant, point_eq);
  pt_color_set_lifecycle_hooks(NULL, NULL);

  pt_color_node* root = NULL;
  int n = 5;
  for (int i = 0; i < n; i++) {
    point_t p = {.x = i, .y = i * 10};
    color_t c = {.r = (uint8_t)(i + 1), .g = (uint8_t)(i + 2), .b = (uint8_t)(i + 3), .a = (uint8_t)(i + 4)};
    root = pt_color_update_root(root, pt_color_set(root, p, c));
  }

  ASSERT(pt_color_count(root) == (size_t)n);

  /* All distinct point keys should be retrievable */
  for (int i = 0; i < n; i++) {
    point_t p   = {.x = i, .y = i * 10};
    color_t val = pt_color_get(root, p);
    ASSERT(val.r == (uint8_t)(i + 1));
    ASSERT(val.g == (uint8_t)(i + 2));
    ASSERT(val.b == (uint8_t)(i + 3));
    ASSERT(val.a == (uint8_t)(i + 4));
  }

  pt_color_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_pt_color_iteration() {
  printf("Running test_pt_color_iteration... ");
  tracker_reset();
  pt_color_set_key_handlers(point_hash, point_eq);
  pt_color_set_lifecycle_hooks(NULL, NULL);

  int n = 30;
  pt_color_node* root = NULL;
  for (int i = 0; i < n; i++) {
    point_t p = {.x = i, .y = i + 100};
    color_t c = {.r = (uint8_t)i, .g = (uint8_t)(i + 1), .b = (uint8_t)(i + 2), .a = (uint8_t)(i + 3)};
    root = pt_color_update_root(root, pt_color_set(root, p, c));
  }

  /* Iterate with next_key / next_value (same as next_leaf but tests the API) */
  bool seen[30];
  memset(seen, 0, sizeof(seen));
  int count = 0;

  pt_color_iter it = pt_color_iter_init(root);
  pt_color_iter_result result;
  while (true) {
    result = pt_color_next_leaf(&it);
    if (result.done) break;
    point_t key = pt_color_key_from_leaf(result.item);
    color_t val = pt_color_value_from_leaf(result.item);
    ASSERT(key.x >= 0 && key.x < n);
    ASSERT(key.y == key.x + 100);
    ASSERT(val.r == (uint8_t)key.x);
    ASSERT(val.g == (uint8_t)(key.x + 1));
    ASSERT(seen[key.x] == false);
    seen[key.x] = true;
    count++;
  }

  ASSERT(count == n);
  for (int i = 0; i < n; i++) {
    ASSERT(seen[i] == true);
  }

  /* Also test next_key and next_value return items */
  pt_color_iter it2 = pt_color_iter_init(root);
  result = pt_color_next_key(&it2);
  ASSERT(result.done == false);
  ASSERT(result.item != NULL);

  pt_color_iter it3 = pt_color_iter_init(root);
  result = pt_color_next_value(&it3);
  ASSERT(result.done == false);
  ASSERT(result.item != NULL);

  pt_color_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-003 Tests === */

/* Counter for lifecycle hook testing */
static int pt_color_destroy_count = 0;

static void test_pt_color_destroy_hook(point_t key, color_t value) {
  (void)key;
  (void)value;
  pt_color_destroy_count++;
}

int test_handler_independence() {
  printf("Running test_handler_independence... ");
  tracker_reset();

  /* Set different handlers for each instantiation */
  u64i32_set_key_handlers(u64_hash, u64_eq);
  u64i32_set_lifecycle_hooks(NULL, NULL);
  pt_color_set_key_handlers(point_hash, point_eq);
  pt_color_set_lifecycle_hooks(NULL, NULL);

  /* Insert into u64i32 */
  u64i32_node* u_root = NULL;
  for (uint64_t i = 0; i < 10; i++) {
    u_root = u64i32_update_root(u_root, u64i32_set(u_root, i, (int32_t)(i * 11)));
  }

  /* Insert into pt_color */
  pt_color_node* p_root = NULL;
  for (int i = 0; i < 10; i++) {
    point_t p = {.x = i, .y = -i};
    color_t c = {.r = (uint8_t)i, .g = (uint8_t)(i + 50), .b = 0, .a = 255};
    p_root = pt_color_update_root(p_root, pt_color_set(p_root, p, c));
  }

  /* Verify u64i32 results are correct (not contaminated by pt_color handlers) */
  for (uint64_t i = 0; i < 10; i++) {
    ASSERT(u64i32_get(u_root, i) == (int32_t)(i * 11));
  }

  /* Verify pt_color results are correct (not contaminated by u64i32 handlers) */
  for (int i = 0; i < 10; i++) {
    point_t p   = {.x = i, .y = -i};
    color_t val = pt_color_get(p_root, p);
    ASSERT(val.r == (uint8_t)i);
    ASSERT(val.g == (uint8_t)(i + 50));
    ASSERT(val.a == 255);
  }

  ASSERT(u64i32_count(u_root) == 10);
  ASSERT(pt_color_count(p_root) == 10);

  u64i32_unref(u_root);
  pt_color_unref(p_root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_lifecycle_hook_independence() {
  printf("Running test_lifecycle_hook_independence... ");
  tracker_reset();

  /* Set handlers for both */
  u64i32_set_key_handlers(u64_hash, u64_eq);
  u64i32_set_lifecycle_hooks(NULL, NULL);  /* No hooks on u64i32 */
  pt_color_set_key_handlers(point_hash, point_eq);
  pt_color_set_lifecycle_hooks(NULL, test_pt_color_destroy_hook);  /* on_destroy hook on pt_color */

  pt_color_destroy_count = 0;

  /* Create and destroy u64i32 nodes — should NOT trigger pt_color's hook */
  u64i32_node* u_root = NULL;
  for (uint64_t i = 0; i < 5; i++) {
    u_root = u64i32_update_root(u_root, u64i32_set(u_root, i, (int32_t)i));
  }
  u64i32_unref(u_root);
  ASSERT(pt_color_destroy_count == 0);

  /* Create and destroy pt_color nodes — SHOULD trigger pt_color's hook */
  pt_color_node* p_root = NULL;
  for (int i = 0; i < 3; i++) {
    point_t p = {.x = i, .y = i};
    color_t c = {.r = (uint8_t)i, .g = 0, .b = 0, .a = 255};
    p_root = pt_color_update_root(p_root, pt_color_set(p_root, p, c));
  }
  pt_color_unref(p_root);
  ASSERT(pt_color_destroy_count == 3);

  /* Reset hook */
  pt_color_set_lifecycle_hooks(NULL, NULL);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_interleaved_operations() {
  printf("Running test_interleaved_operations... ");
  tracker_reset();

  u64i32_set_key_handlers(u64_hash, u64_eq);
  u64i32_set_lifecycle_hooks(NULL, NULL);
  pt_color_set_key_handlers(point_hash, point_eq);
  pt_color_set_lifecycle_hooks(NULL, NULL);

  u64i32_node*   u_root = NULL;
  pt_color_node* p_root = NULL;

  /* Alternate between inserting into each */
  for (int i = 0; i < 20; i++) {
    u_root = u64i32_update_root(u_root, u64i32_set(u_root, (uint64_t)i, (int32_t)(i * 100)));

    point_t p = {.x = i * 3, .y = i * 7};
    color_t c = {.r = (uint8_t)(i * 12), .g = (uint8_t)(i * 6), .b = (uint8_t)(i * 3), .a = (uint8_t)(i + 100)};
    p_root = pt_color_update_root(p_root, pt_color_set(p_root, p, c));
  }

  /* Verify u64i32 */
  ASSERT(u64i32_count(u_root) == 20);
  for (int i = 0; i < 20; i++) {
    ASSERT(u64i32_get(u_root, (uint64_t)i) == (int32_t)(i * 100));
  }

  /* Verify pt_color */
  ASSERT(pt_color_count(p_root) == 20);
  for (int i = 0; i < 20; i++) {
    point_t p   = {.x = i * 3, .y = i * 7};
    color_t val = pt_color_get(p_root, p);
    ASSERT(val.r == (uint8_t)(i * 12));
    ASSERT(val.g == (uint8_t)(i * 6));
    ASSERT(val.b == (uint8_t)(i * 3));
    ASSERT(val.a == (uint8_t)(i + 100));
  }

  u64i32_unref(u_root);
  pt_color_unref(p_root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === main === */

int main() {
  int passed = 0;
  int failed = 0;

#define RUN_TEST(func)   \
  do {                   \
    if (func()) {        \
      passed++;          \
    } else {             \
      failed++;          \
    }                    \
  } while (0)

  /* US-001: uint64_t keys, int32_t values */
  RUN_TEST(test_u64i32_set_get);
  RUN_TEST(test_u64i32_has);
  RUN_TEST(test_u64i32_get_or_default);
  RUN_TEST(test_u64i32_unset);
  RUN_TEST(test_u64i32_count);
  RUN_TEST(test_u64i32_persistence);
  RUN_TEST(test_u64i32_iteration);

  /* US-002: point_t keys, color_t values */
  RUN_TEST(test_pt_color_set_get);
  RUN_TEST(test_pt_color_get_missing_returns_zero);
  RUN_TEST(test_pt_color_key_from_leaf_null);
  RUN_TEST(test_pt_color_unset);
  RUN_TEST(test_pt_color_persistence);
  RUN_TEST(test_pt_color_collision);
  RUN_TEST(test_pt_color_iteration);

  /* US-003: Multiple instantiations coexist */
  RUN_TEST(test_handler_independence);
  RUN_TEST(test_lifecycle_hook_independence);
  RUN_TEST(test_interleaved_operations);

  printf("\n=== Test Summary ===\n");
  printf("Passed: %d\n", passed);
  printf("Failed: %d\n", failed);

  return failed > 0 ? 1 : 0;
}
