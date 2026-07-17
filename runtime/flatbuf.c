/* flatbuf.c — flat, C-ABI buffers ([Buf N T] with a scalar leaf T, any nesting depth).
 * See docs/SVM_BUFFERS.md.
 *
 * A buffer is backed by REAL contiguous memory: one JOBJ_BLOB of total*sizeof(T) raw bytes
 * (all the leaf scalars, row-major) plus a traced header (JACL_TAG_FBUF over a JOBJ_NODE)
 * describing a *view* onto it:
 *   [0] elem-code   (leaf scalar type, 0..9)
 *   [1] base        (this view's first leaf, in element units)
 *   [2] blob        (JACL_TAG_FBUF ptr to the shared JOBJ_BLOB)
 *   [3] ndims       (this view's dimensionality; 1 == a flat scalar row)
 *   [4 + k] dim k   (dim 0 is the outer/`buf-len` dimension)
 * A nested `[Buf N1 [Buf N2 T]]` is a single N1*N2 blob with ndims=2, dims={N1,N2}; `$m->i`
 * returns a *sub-view* — a fresh header over the SAME blob at base i*stride (stride = product
 * of the inner dims) with one fewer dimension. Sub-views therefore ALIAS: a write through one
 * is visible through every view of the same blob (mark-sweep is non-moving, so the blob's
 * address is stable). At ndims==1 an index yields the leaf scalar. `[addr …]` yields the
 * blob's genuine linear-memory address so a compiled-to-svm extern dereferences it with C
 * semantics.
 *
 * Element codes (size, load tag): 0 i8, 1 u8 (1B, i32) · 2 i16, 3 u16 (2B, i32) ·
 * 4 i32, 5 u32 (4B, i32) · 6 i64, 7 u64 (8B, wide) · 8 f32 (4B) · 9 f64 (8B). Included in
 * the unity build after builtins.c, so it sees jacl_wide_new / jacl_int_val / jacl_num_f64. */
#include <string.h>

#define FB_MAX_DIMS 6   /* the fixed-arity constructor / view headers support up to 6 dims */

/* svm-llvm rejects a *constant* ptrtoint of an address; route through a noinline call so it
 * lowers to a runtime instruction (mirrors heap_gc.c's jacl_pti). */
__attribute__((noinline)) static long jacl_fb_pti(void *p) { return (long)(uintptr_t)p; }

static int fb_esize(int32_t code) {
  static const int s[10] = {1, 1, 2, 2, 4, 4, 8, 8, 4, 8};
  return (code >= 0 && code < 10) ? s[code] : 8;
}
static JaclVal *fb_hdr(JaclVal b) { return (JaclVal *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(b)); }
static int32_t fb_code(JaclVal b)  { return jaclrt_as_i32(fb_hdr(b)[0]); }
static int32_t fb_base(JaclVal b)  { return jaclrt_as_i32(fb_hdr(b)[1]); }
static int32_t fb_ndims(JaclVal b) { return jaclrt_as_i32(fb_hdr(b)[3]); }
static int32_t fb_dim(JaclVal b, int k) { return jaclrt_as_i32(fb_hdr(b)[4 + k]); }
static int32_t fb_outer(JaclVal b) { return fb_dim(b, 0); }
/* This view's first leaf, as a real pointer into the shared blob (base-adjusted). */
static void *fb_data(JaclVal b) {
  JaclVal *h = fb_hdr(b);
  char *blob = (char *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(h[2]));
  return blob + (long)jaclrt_as_i32(h[1]) * fb_esize(jaclrt_as_i32(h[0]));
}
/* Leaf elements spanned by one step of the outer dimension (product of the inner dims). */
static long fb_stride(JaclVal b) {
  JaclVal *h = fb_hdr(b);
  int32_t nd = jaclrt_as_i32(h[3]);
  long s = 1;
  for (int k = 1; k < nd; k++) s *= jaclrt_as_i32(h[4 + k]);
  return s;
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

/* Allocate a zeroed buffer with `nd` dimensions `dims[0..nd)` of leaf element-type `code`. */
static JaclVal fb_alloc_nd(int32_t code, int32_t nd, const int32_t *dims) {
  if (nd < 1) nd = 1;
  if (nd > FB_MAX_DIMS) nd = FB_MAX_DIMS;
  int es = fb_esize(code);
  long total = 1;
  for (int k = 0; k < nd; k++) total *= (dims[k] < 0 ? 0 : dims[k]);
  /* Header first (kept as a live local while the blob alloc may collect), like jacl_arr_new. */
  JaclObj *hdr = (JaclObj *)jacl_alloc(JOBJ_NODE, (uint32_t)((4 + nd) * 8));
  JaclVal *h = (JaclVal *)jacl_obj_payload(hdr);
  h[0] = jaclrt_i32(code);
  h[1] = jaclrt_i32(0);          /* base */
  h[2] = JACL_NIL;               /* blob, set after alloc */
  h[3] = jaclrt_i32(nd);
  for (int k = 0; k < nd; k++) h[4 + k] = jaclrt_i32(dims[k] < 0 ? 0 : dims[k]);
  JaclObj *data = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)(total * es));   /* zeroed */
  h = (JaclVal *)jacl_obj_payload(hdr);                                       /* re-read */
  h[2] = jaclrt_from_ptr(JACL_TAG_FBUF, data);
  return jaclrt_from_ptr(JACL_TAG_FBUF, hdr);
}
/* A fresh header over `b`'s blob at base + i*stride, one dimension shallower (`$m->i`). */
static JaclVal fb_subview(JaclVal b, int32_t i) {
  JaclVal *h = fb_hdr(b);
  int32_t code = jaclrt_as_i32(h[0]), base = jaclrt_as_i32(h[1]), nd = jaclrt_as_i32(h[3]);
  int32_t idims[FB_MAX_DIMS];
  for (int k = 0; k < nd && k < FB_MAX_DIMS; k++) idims[k] = jaclrt_as_i32(h[4 + k]);
  JaclVal blob = h[2];
  long stride = 1;
  for (int k = 1; k < nd; k++) stride *= idims[k];
  int32_t nd2 = nd - 1;
  JaclObj *s = (JaclObj *)jacl_alloc(JOBJ_NODE, (uint32_t)((4 + nd2) * 8));   /* may collect */
  JaclVal *sh = (JaclVal *)jacl_obj_payload(s);
  sh[0] = jaclrt_i32(code);
  sh[1] = jaclrt_i32(base + i * (int32_t)stride);
  sh[2] = blob;                                        /* shared — sub-views alias */
  sh[3] = jaclrt_i32(nd2);
  for (int k = 0; k < nd2; k++) sh[4 + k] = jaclrt_i32(idims[k + 1]);
  return jaclrt_from_ptr(JACL_TAG_FBUF, s);
}

/* Scalar convenience constructor: a flat 1-D buffer of `count` `code`-typed elements. */
JaclVal jacl_fbuf_new(JaclVal count, JaclVal elem_code) {
  int32_t n = jaclrt_is_i32(count) ? jaclrt_as_i32(count) : 0;
  int32_t code = jaclrt_is_i32(elem_code) ? jaclrt_as_i32(elem_code) : 9;
  return fb_alloc_nd(code, 1, &n);
}
/* N-dimensional constructor (`def [Buf N1 [Buf N2 …]] b`): up to FB_MAX_DIMS dims. */
JaclVal jacl_fbuf_new_nd(JaclVal code, JaclVal ndims, JaclVal d0, JaclVal d1,
                         JaclVal d2, JaclVal d3, JaclVal d4, JaclVal d5) {
  int32_t c = jaclrt_is_i32(code) ? jaclrt_as_i32(code) : 9;
  int32_t nd = jaclrt_is_i32(ndims) ? jaclrt_as_i32(ndims) : 1;
  JaclVal dv[FB_MAX_DIMS] = {d0, d1, d2, d3, d4, d5};
  int32_t dims[FB_MAX_DIMS];
  for (int k = 0; k < FB_MAX_DIMS; k++) dims[k] = jaclrt_is_i32(dv[k]) ? jaclrt_as_i32(dv[k]) : 0;
  return fb_alloc_nd(c, nd, dims);
}
/* An independent copy — this view's elements into a fresh base-0 buffer of the same shape. */
JaclVal jacl_fbuf_copy(JaclVal b) {
  if (jaclrt_type_index(b) != 0x1E) return b;
  int32_t code = fb_code(b), nd = fb_ndims(b);
  int32_t dims[FB_MAX_DIMS];
  long total = 1;
  for (int k = 0; k < nd && k < FB_MAX_DIMS; k++) { dims[k] = fb_dim(b, k); total *= dims[k]; }
  JaclVal out = fb_alloc_nd(code, nd, dims);
  memcpy(fb_data(out), fb_data(b), (size_t)(total * fb_esize(code)));
  return out;
}
/* `buf-len` — the OUTER dimension (not the flat total). */
JaclVal jacl_fbuf_len(JaclVal b) {
  if (jaclrt_type_index(b) != 0x1E) return jaclrt_error();
  return jaclrt_i32(fb_outer(b));
}
/* `$b->i` — a leaf scalar (ndims==1) or a sub-view onto the same blob (ndims>1). */
JaclVal jacl_fbuf_get(JaclVal b, JaclVal idx) {
  if (jaclrt_type_index(b) != 0x1E || !jaclrt_is_i32(idx)) return jaclrt_error();
  int32_t i = jaclrt_as_i32(idx), nd = fb_ndims(b), outer = fb_outer(b), code = fb_code(b);
  if (nd <= 1) {
    if (i < 0 || i >= outer) return jaclrt_nil();                /* lenient scalar read -> nil */
    return fb_load((char *)fb_data(b) + (long)i * fb_esize(code), code);
  }
  if (i < 0 || i >= outer) return jaclrt_error();                /* nested OOB -> error */
  return fb_subview(b, i);
}
/* `set $b->i V` — writes a leaf scalar; setting a whole sub-buffer is an error. */
JaclVal jacl_fbuf_set(JaclVal b, JaclVal idx, JaclVal v) {
  if (jaclrt_type_index(b) != 0x1E || !jaclrt_is_i32(idx)) return jaclrt_error();
  int32_t i = jaclrt_as_i32(idx), nd = fb_ndims(b), outer = fb_outer(b), code = fb_code(b);
  if (nd != 1) return jaclrt_error();                            /* only a scalar leaf is settable */
  if (i < 0 || i >= outer) return jaclrt_error();                /* fixed size -> OOB errors */
  fb_store((char *)fb_data(b) + (long)i * fb_esize(code), code, v);
  return v;
}
/* `[addr $b->i]` — a flat pointer { address, elem-code } at this view's element i. */
JaclVal jacl_fbuf_addr(JaclVal b, JaclVal idx) {
  if (jaclrt_type_index(b) != 0x1E) return jaclrt_error();
  int32_t i = jaclrt_is_i32(idx) ? jaclrt_as_i32(idx) : 0;
  int code = fb_code(b);
  long addr = jacl_fb_pti((char *)fb_data(b) + (long)i * fb_esize(code));
  JaclObj *p = (JaclObj *)jacl_alloc(JOBJ_NODE, 2 * 8);
  JaclVal *pp = (JaclVal *)jacl_obj_payload(p);
  pp[0] = jacl_wide_new(0x0F, addr);        /* the raw address as u64 */
  pp[1] = jaclrt_i32(code);
  return jaclrt_from_ptr(JACL_TAG_FPTR, p);
}
/* ptr-deref / ptr-offset for a flat pointer (0x1F). Called from jacl_ptr_deref/offset. A flat
 * pointer is its own 2-slot object { addr(u64), elem-code } — unrelated to the FBUF header. */
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
