/* repl_libc.c — the C library the shell's compiler needs (jacl_impl#204). The JACL runtime is a
 * library, so the on-ramp synthesizes no malloc for it (it synthesizes one only for a program with
 * `main`); the frontend and codegen allocate, format and, on paths the REPL never takes, open
 * files. emit_shim.c has the string and formatting functions; this adds the heap and stubs. */
#include "../emit_shim.c"

extern int __vm_atomic_cas32(int *p, int expected, int desired);
extern void __vm_atomic_store32(int *p, int v);
extern void abort(void);

/* The heap: size classes of 16 << k bytes, each with a free list, carved from a static arena. A
 * block's class sits in the 16 bytes before it. The compiler's arenas free whole on a failed line,
 * and the session's grows with the lines it keeps. */
#define REPL_HEAP_BYTES (32ul << 20)
#define REPL_CLASSES 26
static unsigned char repl_heap[REPL_HEAP_BYTES] __attribute__((aligned(16)));
static size_t repl_heap_used;
static void *repl_free[REPL_CLASSES];
static int repl_heap_lock;

static int repl_class(size_t n) {
  for (int k = 0; k < REPL_CLASSES; k++)
    if (n <= (16ul << k)) return k;
  return -1;
}

void *malloc(size_t n) {
  int k = repl_class(n ? n : 1);
  if (k < 0) return 0;
  while (__vm_atomic_cas32(&repl_heap_lock, 0, 1) != 0) {}
  unsigned char *p = repl_free[k];
  if (p) {
    repl_free[k] = *(void **)p;
  } else if (repl_heap_used + 16 + (16ul << k) <= REPL_HEAP_BYTES) {
    p = repl_heap + repl_heap_used + 16;
    repl_heap_used += 16 + (16ul << k);
    *(size_t *)(p - 16) = (size_t)k;
  }
  __vm_atomic_store32(&repl_heap_lock, 0);
  return p;
}

void free(void *ptr) {
  if (!ptr) return;
  int k = (int)*(size_t *)((unsigned char *)ptr - 16);
  while (__vm_atomic_cas32(&repl_heap_lock, 0, 1) != 0) {}
  *(void **)ptr = repl_free[k];
  repl_free[k] = ptr;
  __vm_atomic_store32(&repl_heap_lock, 0);
}

void *calloc(size_t n, size_t size) {
  size_t total = n * size;
  if (size && total / size != n) return 0;
  unsigned char *p = malloc(total);
  if (p)
    for (size_t i = 0; i < total; i++) p[i] = 0;
  return p;
}

void *realloc(void *ptr, size_t n) {
  if (!ptr) return malloc(n);
  size_t have = 16ul << *(size_t *)((unsigned char *)ptr - 16);
  if (n <= have) return ptr;
  unsigned char *q = malloc(n);
  if (!q) return 0;
  for (size_t i = 0; i < have; i++) q[i] = ((unsigned char *)ptr)[i];
  free(ptr);
  return q;
}

/* No files and no environment in the shell's domain: the compiler reads its source from the line
 * and reports through its results, so what reaches these is a CLI path or a diagnostic. */
typedef struct FILE FILE;
FILE *stdout;
FILE *stderr;
char *getenv(const char *name) { (void)name; return 0; }
FILE *fopen(const char *path, const char *mode) { (void)path; (void)mode; return 0; }
int fclose(FILE *f) { (void)f; return 0; }
size_t fread(void *p, size_t size, size_t n, FILE *f) { (void)p; (void)size; (void)n; (void)f; return 0; }
size_t fwrite(const void *p, size_t size, size_t n, FILE *f) { (void)p; (void)size; (void)f; return n; }
int fputs(const char *s, FILE *f) { (void)s; (void)f; return 0; }
int fprintf(FILE *f, const char *fmt, ...) { (void)f; (void)fmt; return 0; }
int fseek(FILE *f, long off, int whence) { (void)f; (void)off; (void)whence; return -1; }
long ftell(FILE *f) { (void)f; return -1; }
void perror(const char *s) { (void)s; }
void __assert_fail(const char *a, const char *f, unsigned l, const char *fn) {
  (void)a; (void)f; (void)l; (void)fn;
  abort();
}
