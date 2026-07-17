/* flatbuf.c — flat, C-ABI scalar buffers ([Buf N T] for a scalar T). See docs/SVM_BUFFERS.md.
 *
 * A scalar buffer is backed by REAL contiguous memory: a traced 3-slot header
 * { count, elem-code, data-blob } (JACL_TAG_FBUF over a JOBJ_NODE) whose third slot points
 * at a JOBJ_BLOB of count*sizeof(T) raw bytes. `[addr $b]` / `[addr $b->i]` yield the blob's
 * genuine linear-memory address (the JACL heap IS svm linear memory; the collector is
 * non-moving so the address is stable), so a compiled-to-svm extern dereferences it with C
 * semantics. The header is traced (it references the blob); the blob holds untraced raw bytes.
 *
 * Element codes (size, load tag): 0 i8, 1 u8 (1B, i32) · 2 i16, 3 u16 (2B, i32) ·
 * 4 i32, 5 u32 (4B, i32) · 6 i64, 7 u64 (8B, wide) · 8 f32 (4B) · 9 f64 (8B). Included in
 * the unity build after builtins.c, so it sees jacl_wide_new / jacl_int_val / jacl_num_f64. */
#include <string.h>

/* svm-llvm rejects a *constant* ptrtoint of an address; route through a noinline call so it
 * lowers to a runtime instruction (mirrors heap_gc.c's jacl_pti). */
__attribute__((noinline)) static long jacl_fb_pti(void *p) { return (long)(uintptr_t)p; }

static int fb_esize(int32_t code) {
  static const int s[10] = {1, 1, 2, 2, 4, 4, 8, 8, 4, 8};
  return (code >= 0 && code < 10) ? s[code] : 8;
}
static JaclVal *fb_hdr(JaclVal b) { return (JaclVal *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(b)); }
static void *fb_data(JaclVal b) {
  return jacl_obj_payload((JaclObj *)jaclrt_as_ptr(fb_hdr(b)[2]));
}
/* Load the scalar at `slot`, tagged the way buf-get widens it (small ints -> i32, etc.). */
static JaclVal fb_load(const void *slot, int32_t code) {
  switch (code) {
    case 0: return jaclrt_i32((int32_t)*(const int8_t *)slot);
    case 1: return jaclrt_i32((int32_t)*(const uint8_t *)slot);
    case 2: return jaclrt_i32((int32_t)*(const int16_t *)slot);
    case 3: return jaclrt_i32((int32_t)*(const uint16_t *)slot);
    case 4: return jaclrt_i32(*(const int32_t *)slot);
    case 5: return jaclrt_i32((int32_t)*(const uint32_t *)slot);
    case 6: return jacl_wide_new(0x0E, *(const int64_t *)slot);
    case 7: return jacl_wide_new(0x0F, (int64_t)*(const uint64_t *)slot);
    case 8: return jaclrt_f32v(*(const float *)slot);
    case 9: { union { double d; int64_t b; } c; c.d = *(const double *)slot; return jacl_wide_new(0x10, c.b); }
    default: return jaclrt_nil();
  }
}
/* Narrow `v` into the scalar at `slot`. */
static void fb_store(void *slot, int32_t code, JaclVal v) {
  switch (code) {
    case 0: case 1: *(uint8_t *)slot = (uint8_t)jacl_int_val(v); break;
    case 2: case 3: *(uint16_t *)slot = (uint16_t)jacl_int_val(v); break;
    case 4: case 5: *(uint32_t *)slot = (uint32_t)jacl_int_val(v); break;
    case 6: case 7: *(int64_t *)slot = (int64_t)jacl_int_val(v); break;
    case 8: *(float *)slot = (float)jacl_num_f64(v); break;
    case 9: *(double *)slot = jacl_num_f64(v); break;
  }
}

/* Allocate a zeroed flat buffer of `count` elements of element-type `code`. */
JaclVal jacl_fbuf_new(JaclVal count, JaclVal elem_code) {
  int32_t n = jaclrt_is_i32(count) ? jaclrt_as_i32(count) : 0;
  int32_t code = jaclrt_is_i32(elem_code) ? jaclrt_as_i32(elem_code) : 9;
  if (n < 0) n = 0;
  int es = fb_esize(code);
  /* Header first (kept as a live local while the blob alloc may collect), like jacl_arr_new. */
  JaclObj *hdr = (JaclObj *)jacl_alloc(JOBJ_NODE, 3 * 8);
  JaclVal *h = (JaclVal *)jacl_obj_payload(hdr);
  h[0] = jaclrt_i32(n);
  h[1] = jaclrt_i32(code);
  h[2] = JACL_NIL;
  JaclObj *data = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)((long)n * es));   /* zeroed */
  h = (JaclVal *)jacl_obj_payload(hdr);                                          /* re-read */
  h[2] = jaclrt_from_ptr(JACL_TAG_FBUF, data);
  return jaclrt_from_ptr(JACL_TAG_FBUF, hdr);
}
/* An independent copy — a fresh header + blob with the same bytes (by-value buffer params). */
JaclVal jacl_fbuf_copy(JaclVal b) {
  if (jaclrt_type_index(b) != 0x1E) return b;
  JaclVal *h = fb_hdr(b);
  int32_t n = jaclrt_as_i32(h[0]), code = jaclrt_as_i32(h[1]);
  JaclVal out = jacl_fbuf_new(jaclrt_i32(n), jaclrt_i32(code));
  memcpy(fb_data(out), fb_data(b), (size_t)((long)n * fb_esize(code)));
  return out;
}
JaclVal jacl_fbuf_len(JaclVal b) {
  if (jaclrt_type_index(b) != 0x1E) return jaclrt_error();
  return jaclrt_i32(jaclrt_as_i32(fb_hdr(b)[0]));
}
JaclVal jacl_fbuf_get(JaclVal b, JaclVal idx) {
  if (jaclrt_type_index(b) != 0x1E || !jaclrt_is_i32(idx)) return jaclrt_error();
  JaclVal *h = fb_hdr(b);
  int32_t n = jaclrt_as_i32(h[0]), i = jaclrt_as_i32(idx), code = jaclrt_as_i32(h[1]);
  if (i < 0 || i >= n) return jaclrt_nil();                       /* lenient read -> nil */
  return fb_load((char *)fb_data(b) + (long)i * fb_esize(code), code);
}
JaclVal jacl_fbuf_set(JaclVal b, JaclVal idx, JaclVal v) {
  if (jaclrt_type_index(b) != 0x1E || !jaclrt_is_i32(idx)) return jaclrt_error();
  JaclVal *h = fb_hdr(b);
  int32_t n = jaclrt_as_i32(h[0]), i = jaclrt_as_i32(idx), code = jaclrt_as_i32(h[1]);
  if (i < 0 || i >= n) return jaclrt_error();                     /* fixed size -> OOB errors */
  fb_store((char *)fb_data(b) + (long)i * fb_esize(code), code, v);
  return v;
}
/* `[addr $b->i]` — a flat pointer { address, elem-code } at element i's raw bytes. */
JaclVal jacl_fbuf_addr(JaclVal b, JaclVal idx) {
  if (jaclrt_type_index(b) != 0x1E) return jaclrt_error();
  int32_t i = jaclrt_is_i32(idx) ? jaclrt_as_i32(idx) : 0;
  JaclVal *h = fb_hdr(b);
  int code = jaclrt_as_i32(h[1]);
  long addr = jacl_fb_pti((char *)fb_data(b) + (long)i * fb_esize(code));
  JaclObj *p = (JaclObj *)jacl_alloc(JOBJ_NODE, 2 * 8);
  JaclVal *pp = (JaclVal *)jacl_obj_payload(p);
  pp[0] = jacl_wide_new(0x0F, addr);        /* the raw address as u64 */
  pp[1] = jaclrt_i32(code);
  return jaclrt_from_ptr(JACL_TAG_FPTR, p);
}
/* ptr-deref / ptr-offset for a flat pointer (0x1F). Called from jacl_ptr_deref/offset. */
int jacl_is_fptr(JaclVal p) { return jaclrt_type_index(p) == 0x1F; }
JaclVal jacl_fptr_deref(JaclVal p) {
  JaclVal *pp = fb_hdr(p);                                        /* {addr(u64), code} */
  long addr = (long)jacl_int_val(pp[0]);
  int code = jaclrt_as_i32(pp[1]);
  return fb_load((const void *)(uintptr_t)addr, code);
}
/* `$p->i` read / write through a flat pointer (0x1F): scalar at addr + i*esize. */
JaclVal jacl_fptr_get(JaclVal p, JaclVal idx) {
  if (jaclrt_type_index(p) != 0x1F || !jaclrt_is_i32(idx)) return jaclrt_error();
  JaclVal *pp = fb_hdr(p);
  long addr = (long)jacl_int_val(pp[0]);
  int code = jaclrt_as_i32(pp[1]);
  return fb_load((const void *)(uintptr_t)(addr + (long)jaclrt_as_i32(idx) * fb_esize(code)), code);
}
JaclVal jacl_fptr_set(JaclVal p, JaclVal idx, JaclVal v) {
  if (jaclrt_type_index(p) != 0x1F || !jaclrt_is_i32(idx)) return jaclrt_error();
  JaclVal *pp = fb_hdr(p);
  long addr = (long)jacl_int_val(pp[0]);
  int code = jaclrt_as_i32(pp[1]);
  fb_store((void *)(uintptr_t)(addr + (long)jaclrt_as_i32(idx) * fb_esize(code)), code, v);
  return v;
}
JaclVal jacl_fptr_offset(JaclVal p, JaclVal n) {
  if (!jaclrt_is_i32(n)) return jaclrt_error();
  JaclVal *pp = fb_hdr(p);
  long addr = (long)jacl_int_val(pp[0]);
  int code = jaclrt_as_i32(pp[1]);
  long naddr = addr + (long)jaclrt_as_i32(n) * fb_esize(code);
  JaclObj *q = (JaclObj *)jacl_alloc(JOBJ_NODE, 2 * 8);
  JaclVal *qq = (JaclVal *)jacl_obj_payload(q);
  qq[0] = jacl_wide_new(0x0F, naddr);
  qq[1] = jaclrt_i32(code);
  return jaclrt_from_ptr(JACL_TAG_FPTR, q);
}
/* The raw linear-memory address of a flat pointer or buffer — the value an extern receives
 * when a [Buf N T] / [Ptr T] decays at a C-ABI call boundary. */
JaclVal jacl_fptr_raw(JaclVal p) {
  if (jaclrt_type_index(p) == 0x1F) return jacl_wide_new(0x0F, (long)jacl_int_val(fb_hdr(p)[0]));
  if (jaclrt_type_index(p) == 0x1E) return jacl_wide_new(0x0F, jacl_fb_pti(fb_data(p)));
  return p;
}
/* The bare i64 address a buffer / flat pointer decays to at a C-ABI extern boundary — the
 * unboxed form of jacl_fptr_raw (no heap cell), passed straight as a machine pointer arg. */
long jacl_raw_ptr(JaclVal p) {
  if (jaclrt_type_index(p) == 0x1F) return (long)jacl_int_val(fb_hdr(p)[0]);
  if (jaclrt_type_index(p) == 0x1E) return jacl_fb_pti(fb_data(p));
  return 0;
}
