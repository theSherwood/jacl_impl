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

/* ===== Expected output parsing ===== */

#define MAX_EXPECT_LINES 256
#define MAX_LINE_LEN     512

typedef struct {
  char     lines[MAX_EXPECT_LINES][MAX_LINE_LEN];
  int      count;
  char     error_substr[MAX_LINE_LEN];
  int      expect_error;
} Expectations;

/* Parse # expect: and # expect-error: header comments from file contents.
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
