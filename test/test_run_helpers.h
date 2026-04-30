/*
 * Shared test helpers for running JACL programs and capturing output.
 *
 * Usage:
 *   #include "test_helpers.h"
 *   #include "../src/jacl.h"
 *   // optionally: #define TEST_CAPTURE_BUF_SIZE 16384
 *   #include "test_run_helpers.h"
 */

#ifndef TEST_RUN_HELPERS_H
#define TEST_RUN_HELPERS_H

#ifndef TEST_CAPTURE_BUF_SIZE
#define TEST_CAPTURE_BUF_SIZE 4096
#endif

typedef struct {
  char     buf[TEST_CAPTURE_BUF_SIZE];
  uint32_t len;
} PrintCapture;

static void capture_print(const char* text, uint32_t len, void* ctx) {
  PrintCapture* cap = (PrintCapture*)ctx;
  uint32_t remaining = (uint32_t)sizeof(cap->buf) - cap->len - 1;
  uint32_t copy_len = len < remaining ? len : remaining;
  memcpy(cap->buf + cap->len, text, copy_len);
  cap->len += copy_len;
  cap->buf[cap->len] = '\0';
}

/* Run program, capture output, assert success */
static int run_ok(const char* source, PrintCapture* cap, const char* expected) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  if (cap) { cap->len = 0; cap->buf[0] = '\0'; }
  VM vm;
  vm_init(&vm, &arena);
  if (cap) {
    vm.print_fn  = capture_print;
    vm.print_ctx = cap;
  }

  VMResult result = jacl_run(source, &vm, &arena);
  if (result != VM_OK) {
    fprintf(stderr, "  Expected VM_OK but got error: %s\n",
            vm.error_message ? vm.error_message : "(null)");
    vm_destroy(&vm);
    arena_destroy(&arena);
    return 0;
  }
  if (expected && cap) {
    if (strcmp(cap->buf, expected) != 0) {
      fprintf(stderr, "  Output mismatch:\n  Actual:   '%s'\n  Expected: '%s'\n",
              cap->buf, expected);
      vm_destroy(&vm);
      arena_destroy(&arena);
      return 0;
    }
  }

  vm_destroy(&vm);
  arena_destroy(&arena);
  if (!check_no_leaks()) return 0;
  return 1;
}

/* Run program, expect runtime error containing substring */
static int run_err(const char* source, const char* err_substr) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  VM vm;
  vm_init(&vm, &arena);

  VMResult result = jacl_run(source, &vm, &arena);
  if (result != VM_RUNTIME_ERROR) {
    fprintf(stderr, "  Expected VM_RUNTIME_ERROR but got VM_OK\n");
    vm_destroy(&vm);
    arena_destroy(&arena);
    return 0;
  }
  if (err_substr && (!vm.error_message || !strstr(vm.error_message, err_substr))) {
    fprintf(stderr, "  Error message '%s' does not contain '%s'\n",
            vm.error_message ? vm.error_message : "(null)", err_substr);
    vm_destroy(&vm);
    arena_destroy(&arena);
    return 0;
  }

  vm_destroy(&vm);
  arena_destroy(&arena);
  if (!check_no_leaks()) return 0;
  return 1;
}

#endif /* TEST_RUN_HELPERS_H */
