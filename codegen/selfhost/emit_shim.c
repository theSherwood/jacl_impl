/* Emit-path libc shim for the JACL-compiler-on-SVM build.
 *
 * The SVM LLVM on-ramp synthesizes malloc/calloc/realloc/free and memcpy/memset,
 * and binds read/write/exit to host caps by name. Everything else the JACL frontend
 * calls on the codegen path is supplied here. Measured surface (call counts) from
 * `docs/SVM_SELFHOST_FEASIBILITY.md`: memcmp, strlen/strcmp/strcpy/strrchr,
 * snprintf/vsnprintf (integer/string specifiers only — the IR-text builder uses
 * %u %s %d %llu %lld %02x, no floats), strtol, strerror, qsort, fmod,
 * __errno_location.
 *
 * This is intentionally small and self-contained (no libc headers) so the whole
 * program is one clean LLVM module. A production build may instead link the reused
 * shims under vendor/svm/crates/svm-run/demos/{postgres,strtod}. */

typedef unsigned long size_t;
typedef __builtin_va_list va_list;
#define va_start __builtin_va_start
#define va_arg   __builtin_va_arg
#define va_end   __builtin_va_end

/* --- fixed pre-sized heap arena (SVM_WARM_COMPILER.md Slice 1) ---------------------------------
 * Defining malloc/calloc/realloc/free here *shadows* the on-ramp's synthesized `__temen_malloc`,
 * a bump allocator that grows the window via `vm_map` on demand. That mid-run `vm_map` grow traps
 * the coop tier-up on the emitted tier (temen #1151, still open — re-measured against temen main
 * @ 90a1604c on 2026-09-02: a growing card still traps, tierups=0, while this arena card tiers up).
 * A fixed static arena lives in the guest image's committed data, so no `vm_map` runs and the paged
 * table stays valid. Build with `-DJACL_GROW_HEAP` to omit this shim and use the engine's growing
 * allocator — the one-flag re-check for when #1151 lands. The arena stays the default: it also
 * serves the warm-snapshot path, which needs a non-growing window to be memcpy-restorable (#816).
 * The allocator keeps the synthesized one's semantics exactly — never-reusing bump, `free` a no-op,
 * `calloc` zeroed — so compiler output is byte-identical. ARENA_BYTES must fit under the window with
 * the guest's static data (~26 MiB) + stack. */
#ifndef JACL_GROW_HEAP
#ifndef JACL_ARENA_BYTES
#define JACL_ARENA_BYTES (4u * 1024u * 1024u)
#endif
static unsigned char g_arena[JACL_ARENA_BYTES] __attribute__((aligned(16)));
static size_t g_brk;
static size_t g_arena_hi; /* high-water, for the OOM report */

void *malloc(size_t n) {
  size_t payload = (n + 15u) & ~(size_t)15u; /* 16-align the payload */
  if (g_brk + 16u + payload > JACL_ARENA_BYTES) return 0; /* arena exhausted */
  unsigned char *p = g_arena + g_brk + 16u;
  *(size_t *)(p - 16u) = n; /* size header (for realloc), matching the synthesized layout */
  g_brk += 16u + payload;
  if (g_brk > g_arena_hi) g_arena_hi = g_brk;
  return p;
}
void *calloc(size_t nmemb, size_t sz) {
  size_t n = nmemb * sz;
  unsigned char *p = malloc(n);
  if (p) for (size_t i = 0; i < n; i++) p[i] = 0;
  return p;
}
void free(void *p) { (void)p; } /* never reuse — matches the synthesized allocator */
void *realloc(void *p, size_t n) {
  if (!p) return malloc(n);
  size_t old = *(size_t *)((unsigned char *)p - 16u);
  unsigned char *q = malloc(n);
  if (q) { size_t m = old < n ? old : n; unsigned char *s = p; for (size_t i = 0; i < m; i++) q[i] = s[i]; }
  return q;
}
#endif /* !JACL_GROW_HEAP */

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *p = a, *q = b;
  for (size_t i = 0; i < n; i++)
    if (p[i] != q[i]) return (int)p[i] - (int)q[i];
  return 0;
}
int bcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }

size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) {} return r; }
char *strrchr(const char *s, int c) {
  const char *last = 0;
  do { if (*s == (char)c) last = s; } while (*s++);
  return (char *)last;
}
char *strchr(const char *s, int c) {
  for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return 0; }
}
char *strerror(int e) { (void)e; return "error"; }

static int g_errno;
int *__errno_location(void) { return &g_errno; }

long strtol(const char *s, char **end, int base) {
  long v = 0; int neg = 0;
  if (base == 0) base = 10;
  while (*s == ' ' || *s == '\t') s++;
  if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
  for (;; s++) {
    int d; char c = *s;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
    else break;
    if (d >= base) break;
    v = v * base + d;
  }
  if (end) *end = (char *)s;
  return neg ? -v : v;
}

double fmod(double x, double y) {
  if (y == 0) return 0;
  long long q = (long long)(x / y);
  return x - (double)q * y;
}

void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) {
  char *a = base, tmp;                              /* insertion sort — small n on the emit path */
  for (size_t i = 1; i < n; i++)
    for (size_t j = i; j > 0 && cmp(a + (j - 1) * sz, a + j * sz) > 0; j--)
      for (size_t k = 0; k < sz; k++) {
        tmp = a[(j - 1) * sz + k]; a[(j - 1) * sz + k] = a[j * sz + k]; a[j * sz + k] = tmp;
      }
}

/* --- minimal vsnprintf/snprintf: %s %c %d/%ld/%lld %u/%lu/%llu %x/%X (0-pad width) %% --- */
static void emit_ch(char *buf, size_t cap, size_t *pos, char c) {
  if (*pos < cap) buf[*pos] = c;
  (*pos)++;
}
static void emit_str(char *buf, size_t cap, size_t *pos, const char *s) {
  while (*s) emit_ch(buf, cap, pos, *s++);
}
/* Print up to `prec` chars of `s` (a precision-bounded string, may be non-NUL-terminated). */
static void emit_str_n(char *buf, size_t cap, size_t *pos, const char *s, int prec) {
  for (int i = 0; i < prec && s[i]; i++) emit_ch(buf, cap, pos, s[i]);
}
static void emit_uint(char *buf, size_t cap, size_t *pos, unsigned long long v,
                      int base, int upper, int width, char pad) {
  char t[24]; int n = 0;
  const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  if (v == 0) t[n++] = '0'; else while (v) { t[n++] = dig[v % base]; v /= base; }
  for (int i = n; i < width; i++) emit_ch(buf, cap, pos, pad);
  while (n) emit_ch(buf, cap, pos, t[--n]);
}
int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap) {
  size_t pos = 0;
  for (const char *p = fmt; *p; p++) {
    if (*p != '%') { emit_ch(buf, cap, &pos, *p); continue; }
    p++;
    char pad = ' '; int width = 0;
    if (*p == '0') { pad = '0'; p++; }
    while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
    /* Precision: `.` then `*` (from an int arg) or digits. Only `%.*s`/`%.Ns` (bounded
     * string) is used by the frontend's `macro '%.*s'` diagnostics. */
    int prec = -1;
    if (*p == '.') {
      p++;
      if (*p == '*') { prec = va_arg(ap, int); p++; }
      else { prec = 0; while (*p >= '0' && *p <= '9') { prec = prec * 10 + (*p - '0'); p++; } }
    }
    int lng = 0; while (*p == 'l') { lng++; p++; }
    switch (*p) {
      case 's':
        if (prec >= 0) emit_str_n(buf, cap, &pos, va_arg(ap, const char *), prec);
        else emit_str(buf, cap, &pos, va_arg(ap, const char *));
        break;
      case 'c': emit_ch(buf, cap, &pos, (char)va_arg(ap, int)); break;
      case '%': emit_ch(buf, cap, &pos, '%'); break;
      case 'd': case 'i': {
        long long v = lng >= 2 ? va_arg(ap, long long) : (long long)va_arg(ap, int);
        if (v < 0) { emit_ch(buf, cap, &pos, '-'); emit_uint(buf, cap, &pos, (unsigned long long)(-v), 10, 0, width, pad); }
        else emit_uint(buf, cap, &pos, (unsigned long long)v, 10, 0, width, pad);
        break;
      }
      case 'u': { unsigned long long v = lng >= 2 ? va_arg(ap, unsigned long long) : (unsigned long long)va_arg(ap, unsigned); emit_uint(buf, cap, &pos, v, 10, 0, width, pad); break; }
      case 'x': { unsigned long long v = lng >= 2 ? va_arg(ap, unsigned long long) : (unsigned long long)va_arg(ap, unsigned); emit_uint(buf, cap, &pos, v, 16, 0, width, pad); break; }
      case 'X': { unsigned long long v = lng >= 2 ? va_arg(ap, unsigned long long) : (unsigned long long)va_arg(ap, unsigned); emit_uint(buf, cap, &pos, v, 16, 1, width, pad); break; }
      default: emit_ch(buf, cap, &pos, '%'); emit_ch(buf, cap, &pos, *p); break;
    }
  }
  if (cap) buf[pos < cap ? pos : cap - 1] = 0;
  return (int)pos;
}
int snprintf(char *buf, size_t cap, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  int r = vsnprintf(buf, cap, fmt, ap);
  va_end(ap);
  return r;
}
