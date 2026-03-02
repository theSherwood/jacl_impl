#include "test_helpers.h"
#include "../src/jacl.c"

#include <dirent.h>

/* ===== Print capture helper ===== */

typedef struct {
  char     buf[8192];
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

/* ===== Thread-safe print capture for concurrent mode ===== */

typedef struct {
  PrintCapture     cap;
  platform_mutex_t mutex;
} ThreadSafePrintCapture;

static void capture_print_threadsafe(const char* text, uint32_t len, void* ctx) {
  ThreadSafePrintCapture* tspc = (ThreadSafePrintCapture*)ctx;
  MUTEX_LOCK(tspc->mutex);
  capture_print(text, len, &tspc->cap);
  MUTEX_UNLOCK(tspc->mutex);
}

/* ===== Expected output parsing ===== */

#define MAX_EXPECT_LINES 256
#define MAX_LINE_LEN     512

typedef struct {
  char     lines[MAX_EXPECT_LINES][MAX_LINE_LEN];
  int      count;
  char     error_substr[MAX_LINE_LEN];
  int      expect_error;
  int      concurrent;   /* 1 if '# mode: concurrent' header present */
} Expectations;

/* Parse # expect:, # expect-error:, and # mode: concurrent header comments.
   Returns 1 on success, 0 on parse error (e.g., mixing expect and expect-error). */
static int parse_expectations(const char* source, Expectations* exp) {
  memset(exp, 0, sizeof(*exp));

  const char* p = source;
  while (*p) {
    /* Skip blank lines */
    if (*p == '\n') { p++; continue; }

    /* Must be a comment line to continue parsing headers */
    if (*p != '#') break;

    if (strncmp(p, "# expect-error: ", 16) == 0) {
      if (exp->count > 0) {
        fprintf(stderr, "  ERROR: Cannot mix # expect: and # expect-error:\n");
        return 0;
      }
      p += 16;
      const char* end = strchr(p, '\n');
      size_t len = end ? (size_t)(end - p) : strlen(p);
      if (len >= MAX_LINE_LEN) len = MAX_LINE_LEN - 1;
      memcpy(exp->error_substr, p, len);
      exp->error_substr[len] = '\0';
      exp->expect_error = 1;
      p = end ? end + 1 : p + len;
    } else if (strncmp(p, "# expect: ", 10) == 0) {
      if (exp->expect_error) {
        fprintf(stderr, "  ERROR: Cannot mix # expect: and # expect-error:\n");
        return 0;
      }
      if (exp->count >= MAX_EXPECT_LINES) {
        fprintf(stderr, "  ERROR: Too many # expect: lines\n");
        return 0;
      }
      p += 10;
      const char* end = strchr(p, '\n');
      size_t len = end ? (size_t)(end - p) : strlen(p);
      if (len >= MAX_LINE_LEN) len = MAX_LINE_LEN - 1;
      memcpy(exp->lines[exp->count], p, len);
      exp->lines[exp->count][len] = '\0';
      exp->count++;
      p = end ? end + 1 : p + len;
    } else if (strncmp(p, "# mode: concurrent", 18) == 0) {
      exp->concurrent = 1;
      const char* end = strchr(p, '\n');
      p = end ? end + 1 : p + strlen(p);
    } else {
      /* Regular comment — skip to end of line */
      const char* end = strchr(p, '\n');
      p = end ? end + 1 : p + strlen(p);
    }
  }
  return 1;
}

/* ===== File reading ===== */

static char* read_file(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* buf = (char*)malloc((size_t)size + 1);
  if (!buf) { fclose(f); return NULL; }

  size_t nread = fread(buf, 1, (size_t)size, f);
  buf[nread] = '\0';
  fclose(f);
  return buf;
}

/* ===== Copy environment from source VM to destination VM ===== */

static void harness__copy_env(VM* dest, VM* src) {
  uint32_t i;
  for (i = 0; i < src->env.count; i++) {
    /* Check if we need to grow dest env */
    if (dest->env.count >= dest->env.cap) {
      uint32_t new_cap = dest->env.cap * 2;
      JaclVal* new_names = (JaclVal*)arena_alloc(dest->arena, new_cap * sizeof(JaclVal));
      JaclVal* new_values = (JaclVal*)arena_alloc(dest->arena, new_cap * sizeof(JaclVal));
      memcpy(new_names, dest->env.names, dest->env.count * sizeof(JaclVal));
      memcpy(new_values, dest->env.values, dest->env.count * sizeof(JaclVal));
      dest->env.names = new_names;
      dest->env.values = new_values;
      dest->env.cap = new_cap;
    }
    /* Check if entry already exists (overwrite) */
    uint32_t j;
    int found = 0;
    for (j = 0; j < dest->env.count; j++) {
      if (dest->env.names[j] == src->env.names[i]) {
        dest->env.values[j] = src->env.values[i];
        found = 1;
        break;
      }
    }
    if (!found) {
      dest->env.names[dest->env.count] = src->env.names[i];
      dest->env.values[dest->env.count] = src->env.values[i];
      dest->env.count++;
    }
  }
}

/* ===== Concurrent test runner ===== */

/* Runs a .jacl test using the runtime worker pool (# mode: concurrent).
   Returns 1 if pass, 0 if fail. */
static int run_jacl_test_concurrent(const char* source, Expectations* exp) {
  int ok = 1;
  int i;

  /* Use plain arena (no tracked_allocator) — runtime manages its own memory */
  arena_t arena = {0};
  VM vm;
  vm_init(&vm, &arena);

  /* Step 1: Compile */
  LexResult tokens = lexer_lex(source, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  if (parse.error_count > 0) {
    if (exp->expect_error && strstr("parse error", exp->error_substr)) {
      vm_destroy(&vm);
      arena_destroy(&arena);
      return 1;
    }
    fprintf(stderr, "  Parse error in concurrent test\n");
    vm_destroy(&vm);
    arena_destroy(&arena);
    return 0;
  }

  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);

  CompileResult cr = compiler_compile(parse, &arena, &intern_table, &vm.heap);
  if (cr.error_count > 0) {
    if (exp->expect_error) {
      int match = cr.error_message && strstr(cr.error_message, exp->error_substr);
      vm_destroy(&vm);
      arena_destroy(&arena);
      return match != 0;
    }
    fprintf(stderr, "  Compile error: %s\n",
            cr.error_message ? cr.error_message : "(unknown)");
    vm_destroy(&vm);
    arena_destroy(&arena);
    return 0;
  }

  vm.intern_table = &intern_table;

  /* Step 2: Get the closure to submit */
  JaclClosure *closure = NULL;

  if (cr.suspending) {
    /* Execute chunk on temp VM to define procs and extract CPS closure */
    VMResult r = vm_exec(&vm, &cr.chunk);
    if (r != VM_OK) {
      fprintf(stderr, "  Error extracting CPS closure: %s\n",
              vm.error_message ? vm.error_message : "(unknown)");
      vm_destroy(&vm);
      arena_destroy(&arena);
      return 0;
    }
    if (vm.stack_top == 0 || !jacl_is_closure(vm.stack[0])) {
      fprintf(stderr, "  Top-level CPS did not produce closure\n");
      vm_destroy(&vm);
      arena_destroy(&arena);
      return 0;
    }
    closure = jacl_as_closure(vm.stack[0]);
  } else {
    /* Non-suspending: wrap chunk in a closure for runtime submission */
    closure = (JaclClosure*)gc_alloc(&vm.heap, OBJ_CLOSURE, sizeof(JaclClosure));
    memset(closure, 0, sizeof(JaclClosure));
    closure->chunk = cr.chunk;
    closure->param_count = 0;
    closure->upvalue_count = 0;
  }

  /* Step 3: Set up runtime */
  Runtime rt;
  runtime_init(&rt, 2);

  /* Thread-safe print capture */
  ThreadSafePrintCapture tspc;
  memset(&tspc, 0, sizeof(tspc));
  MUTEX_INIT(tspc.mutex);

  /* Configure all worker VMs: print capture, intern table, env */
  for (i = 0; i < rt.num_workers; i++) {
    rt.workers[i].vm.print_fn = capture_print_threadsafe;
    rt.workers[i].vm.print_ctx = &tspc;
    rt.workers[i].vm.intern_table = &intern_table;
    /* Copy env from temp VM so workers have user-defined procs */
    harness__copy_env(&rt.workers[i].vm, &vm);
  }

  /* Step 4: Submit closure and wait for completion */
  JaclVal completion = jacl_future(&rt.workers[0].vm.heap);
  JaclFuture *cfut = jacl_as_future(completion);
  bool is_cps = (closure->param_count == 1);
  runtime__submit_spawn_task(&rt, closure, completion, is_cps);

  /* Block until completion future resolves */
  for (;;) {
    uint32_t state = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_ACQUIRE);
    if (state == FUTURE_RESOLVED || state == FUTURE_ERROR) break;
    SLEEP_MILLISECONDS(1);
  }

  uint32_t final_state = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_RELAXED);
  VMResult result = (final_state == FUTURE_ERROR) ? VM_RUNTIME_ERROR : VM_OK;

  /* Step 5: Check output */
  if (exp->expect_error) {
    if (result != VM_RUNTIME_ERROR) {
      fprintf(stderr, "  Expected runtime error, got VM_OK\n");
      ok = 0;
    } else {
      /* Try to find error message from worker VMs */
      const char* err_msg = NULL;
      for (i = 0; i < rt.num_workers; i++) {
        if (rt.workers[i].vm.error_message) {
          err_msg = rt.workers[i].vm.error_message;
          break;
        }
      }
      if (err_msg && !strstr(err_msg, exp->error_substr)) {
        fprintf(stderr, "  Expected error containing \"%s\", got: \"%s\"\n",
                exp->error_substr, err_msg);
        ok = 0;
      }
    }
  } else {
    if (result != VM_OK) {
      /* Find error message from workers for diagnostics */
      const char* err_msg = NULL;
      for (i = 0; i < rt.num_workers; i++) {
        if (rt.workers[i].vm.error_message) {
          err_msg = rt.workers[i].vm.error_message;
          break;
        }
      }
      fprintf(stderr, "  Expected VM_OK, got runtime error: %s\n",
              err_msg ? err_msg : "(unknown)");
      ok = 0;
    } else {
      /* Build expected output string */
      char expected[8192];
      expected[0] = '\0';
      for (i = 0; i < exp->count; i++) {
        strcat(expected, exp->lines[i]);
        strcat(expected, "\n");
      }

      if (strcmp(tspc.cap.buf, expected) != 0) {
        fprintf(stderr, "  Output mismatch:\n");
        fprintf(stderr, "    Expected: \"%s\"\n", expected);
        fprintf(stderr, "    Actual:   \"%s\"\n", tspc.cap.buf);
        ok = 0;
      }
    }
  }

  /* Step 6: Cleanup */
  runtime_destroy(&rt);
  MUTEX_DESTROY(tspc.mutex);
  vm_destroy(&vm);
  arena_destroy(&arena);

  return ok;
}

/* ===== Single test file runner ===== */

/* Runs a single .jacl test file. Returns 1 if pass, 0 if fail. */
static int run_jacl_test(const char* filepath) {
  char* source = read_file(filepath);
  if (!source) {
    fprintf(stderr, "  Could not read file: %s\n", filepath);
    return 0;
  }

  /* Parse expectations */
  Expectations exp;
  if (!parse_expectations(source, &exp)) {
    free(source);
    return 0;
  }

  /* Dispatch to concurrent runner if requested */
  if (exp.concurrent) {
    int result = run_jacl_test_concurrent(source, &exp);
    free(source);
    return result;
  }

  /* Set up VM with tracked allocator */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(source, &vm, &arena);
  free(source);

  int ok = 1;

  if (exp.expect_error) {
    /* Expecting a runtime error with a substring match */
    if (result != VM_RUNTIME_ERROR) {
      fprintf(stderr, "  Expected runtime error, got VM_OK\n");
      ok = 0;
    } else if (!vm.error_message || !strstr(vm.error_message, exp.error_substr)) {
      fprintf(stderr, "  Expected error containing \"%s\", got: \"%s\"\n",
              exp.error_substr, vm.error_message ? vm.error_message : "(null)");
      ok = 0;
    }
  } else {
    /* Expecting success */
    if (result != VM_OK) {
      fprintf(stderr, "  Expected VM_OK, got error: %s (line %u)\n",
              vm.error_message ? vm.error_message : "(unknown)", vm.error_line);
      ok = 0;
    } else {
      /* Build expected output string */
      char expected[8192];
      expected[0] = '\0';
      for (int i = 0; i < exp.count; i++) {
        strcat(expected, exp.lines[i]);
        strcat(expected, "\n");
      }

      if (strcmp(cap.buf, expected) != 0) {
        fprintf(stderr, "  Output mismatch:\n");
        fprintf(stderr, "    Expected: \"%s\"\n", expected);
        fprintf(stderr, "    Actual:   \"%s\"\n", cap.buf);
        ok = 0;
      }
    }
  }

  /* Check for memory leaks */
  vm_destroy(&vm);
  arena_destroy(&arena);
  if (!check_no_leaks()) {
    fprintf(stderr, "  Memory leak detected!\n");
    ok = 0;
  }

  return ok;
}

/* ===== Directory scanning ===== */

/* Compare function for qsort of filenames */
static int cmp_strings(const void* a, const void* b) {
  return strcmp(*(const char**)a, *(const char**)b);
}

#define MAX_TEST_FILES 1024

/* Scan directory for .jacl files, run each, return pass/fail counts. */
static void run_directory(const char* dirpath, int* passed, int* failed) {
  DIR* dir = opendir(dirpath);
  if (!dir) {
    fprintf(stderr, "Could not open test directory: %s\n", dirpath);
    return;
  }

  /* Collect filenames for deterministic ordering */
  char* filenames[MAX_TEST_FILES];
  int file_count = 0;

  struct dirent* entry;
  while ((entry = readdir(dir)) != NULL) {
    size_t namelen = strlen(entry->d_name);
    if (namelen > 5 && strcmp(entry->d_name + namelen - 5, ".jacl") == 0) {
      if (file_count < MAX_TEST_FILES) {
        filenames[file_count] = (char*)malloc(namelen + 1);
        strcpy(filenames[file_count], entry->d_name);
        file_count++;
      }
    }
  }
  closedir(dir);

  /* Sort alphabetically for deterministic ordering */
  qsort(filenames, (size_t)file_count, sizeof(char*), cmp_strings);

  /* Run each file */
  for (int i = 0; i < file_count; i++) {
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, filenames[i]);

    printf("  %-40s ", filenames[i]);
    if (run_jacl_test(filepath)) {
      printf("PASS\n");
      (*passed)++;
    } else {
      printf("FAIL\n");
      (*failed)++;
    }

    free(filenames[i]);
  }
}

/* ===== Main ===== */

int main(int argc, char* argv[]) {
  int passed = 0;
  int failed = 0;

  if (argc > 1) {
    /* Single file mode */
    const char* path = argv[1];
    char filepath[1024];

    /* If no directory separator, look in default directory */
    if (!strchr(path, '/')) {
      snprintf(filepath, sizeof(filepath), "test/jacl/%s", path);
      path = filepath;
    }

    printf("  %-40s ", argv[1]);
    if (run_jacl_test(path)) {
      printf("PASS\n");
      passed++;
    } else {
      printf("FAIL\n");
      failed++;
    }
  } else {
    /* Directory mode */
    run_directory("test/jacl", &passed, &failed);
  }

  printf("\n  %d/%d passed\n", passed, passed + failed);
  return failed > 0 ? 1 : 0;
}
