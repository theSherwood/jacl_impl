/* string.c — JACL runtime strings (P1.4): inline / heap / interned.
 *
 * - Inline strings (<=7 bytes) are pure values (jaclrt.h); equal inline strings
 *   have identical bits, so they are canonical by construction.
 * - Heap strings are GC objects (JOBJ_STR): { u32 len; bytes[]; NUL }. No outgoing
 *   pointers, so the GC never traces them (only marks/sweeps).
 * - Interning canonicalises heap strings via a fixed open-addressing table so equal
 *   strings share one object (pointer equality). The table holds STRONG references:
 *   interned strings are runtime roots (jacl_mark_runtime_roots), so they survive
 *   GC even when unreferenced on the stack. (Weak interning — collect when only the
 *   table refers — is a later refinement.)
 */
#include "jaclrt.h"
#include <stdint.h>
#include <string.h>

/* svm-llvm provides memcpy/memset but not bcmp/memcmp; hand-roll the compare. */
static int rt_bytes_eq(const char *a, const char *b, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return 0;
  return 1;
}

typedef struct { uint32_t len; /* bytes follow, NUL-terminated */ } JaclStr;

static inline JaclStr* str_obj(JaclVal v) {
  return (JaclStr*)jacl_obj_payload((JaclObj*)jaclrt_as_ptr(v));
}
static inline const char* str_data(JaclVal v) { return (const char*)(str_obj(v) + 1); }

JaclVal jacl_str_new(const char *s, uint32_t len) {
  if (len <= 7) return jaclrt_inline_string(s, len);
  JaclObj *o = (JaclObj*)jacl_alloc(JOBJ_STR, (uint32_t)sizeof(JaclStr) + len + 1);
  JaclStr *st = (JaclStr*)jacl_obj_payload(o);
  st->len = len;
  memcpy((char*)(st + 1), s, len);
  ((char*)(st + 1))[len] = '\0';
  return jaclrt_from_ptr(JACL_TAG_STRING, o);
}

uint32_t jacl_str_len(JaclVal v) {
  if (jaclrt_is_inline_string(v)) return jaclrt_inline_len(v);
  return str_obj(v)->len;
}

void jacl_str_bytes(JaclVal v, char *buf, uint32_t buflen) {
  if (jaclrt_is_inline_string(v)) { jaclrt_inline_get(v, buf, buflen); return; }
  uint32_t n = str_obj(v)->len;
  const char *d = str_data(v);
  uint32_t i = 0;
  for (; i < n && i + 1 < buflen; i++) buf[i] = d[i];
  if (buflen) buf[i] = '\0';
}

bool jacl_str_eq(JaclVal a, JaclVal b) {
  if (a == b) return true;                              /* inline or same object */
  uint32_t la = jacl_str_len(a), lb = jacl_str_len(b);
  if (la != lb) return false;
  char ba[256], bb[256];
  if (la + 1 > sizeof(ba)) {                            /* long: compare heap data directly */
    if (jaclrt_is_inline_string(a) || jaclrt_is_inline_string(b)) return false;
    return rt_bytes_eq(str_data(a), str_data(b), la);
  }
  jacl_str_bytes(a, ba, sizeof ba);
  jacl_str_bytes(b, bb, sizeof bb);
  return rt_bytes_eq(ba, bb, la);
}

/* ----- intern table: fixed open-addressing over interned heap strings ----- */
#define INTERN_CAP 4096                                  /* power of two */
static JaclVal intern_tab[INTERN_CAP];                   /* 0 (JACL_NIL) = empty slot */
static uint32_t intern_count;

void jacl_intern_init(void) {
  for (uint32_t i = 0; i < INTERN_CAP; i++) intern_tab[i] = JACL_NIL;
  intern_count = 0;
}

static uint32_t str_hash(const char *s, uint32_t len) {
  uint32_t h = 2166136261u;                              /* FNV-1a */
  for (uint32_t i = 0; i < len; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
  return h;
}

JaclVal jacl_str_intern(const char *s, uint32_t len) {
  if (len <= 7) return jaclrt_inline_string(s, len);     /* inline strings are already canonical */
  uint32_t mask = INTERN_CAP - 1;
  uint32_t i = str_hash(s, len) & mask;
  for (uint32_t probe = 0; probe < INTERN_CAP; probe++) {
    JaclVal e = intern_tab[i];
    if (jaclrt_is_nil(e)) {                              /* empty -> insert */
      JaclVal v = jacl_str_new(s, len);
      intern_tab[i] = v;
      intern_count++;
      return v;
    }
    /* entries are always heap strings (len>7) — compare directly, avoiding a
       second tag check (which lets clang merge into an i64 switch svm-llvm
       can't lower; runtime tag dispatch should switch on the i32 type index). */
    JaclStr *es = str_obj(e);
    if (es->len == len && rt_bytes_eq((const char*)(es + 1), s, len)) return e;
    i = (i + 1) & mask;
  }
  return jacl_str_new(s, len);                            /* table full (shouldn't happen in P1) */
}

/* Hash of a string's bytes (inline or heap) — shared by interning and map keys. */
uint32_t jacl_str_hash(JaclVal v) {
  if (jaclrt_is_inline_string(v)) {
    char b[8]; jaclrt_inline_get(v, b, sizeof b);
    return str_hash(b, jaclrt_inline_len(v));
  }
  JaclStr *st = str_obj(v);
  return str_hash((const char*)(st + 1), st->len);
}

/* GC: interned strings are strong roots. */
void jacl_mark_runtime_roots(void) {
  for (uint32_t i = 0; i < INTERN_CAP; i++) {
    if (!jaclrt_is_nil(intern_tab[i])) jacl_gc_mark(intern_tab[i]);
  }
}
