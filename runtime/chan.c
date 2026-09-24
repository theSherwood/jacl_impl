/* chan.c — byte channels (docs/UNIR_CHANNELS.md): `[channel CAP]`, `read`, `write`, `close`.
 *
 * Platform-neutral. A channel is two ends over one backend connection, and the backend is
 * reached only through the chan_be_* functions below: chan_unir.c implements them over Unir
 * edges when the runtime is built with JACL_UNIR (and linked with the unir unit); without it,
 * `channel` returns an error value. A backend write or read may park the calling FIBER (a
 * TEMEN wait inside a fiber parks it; worker_loop's repoll_blocked resumes it), so nothing
 * here holds a runtime lock across one (jacl #142). Included in the unity build after
 * flatbuf.c (it builds `[Buf n u8]` results with fb_alloc_nd / fb_data).
 *
 * An end is a JACL_TAG_STREAM value over a JOBJ_BLOB (jobs and streams are JOBJ_NODE), so it
 * holds no traced pointers: its backend handle lives in the backend's own heap. */

enum { CHAN_W = 1, CHAN_R = 2 };

/* Backend results: >= 0 is success (a read's frame length); these are the failures. */
#define CHAN_BE_END     (-1)   /* the stream ended cleanly (read: EOF; write: reader cancelled) */
#define CHAN_BE_SEVERED (-2)   /* the stream was severed; chan_be_cause says why */
#define CHAN_BE_FAILED  (-3)   /* anything else */

typedef struct {
  uint32_t end;        /* CHAN_W / CHAN_R */
  int32_t  busy;       /* 1 while an operation is in flight (cas32): one at a time per end */
  uint32_t closed;     /* this end was closed with `close` */
  uint32_t max;        /* the largest frame the backend carries */
  void    *handle;     /* the backend's end */
  uint32_t tail_off;   /* read end: the unread bytes of the current frame */
  uint32_t tail_len;
  uint8_t  tail[];     /* read end: `max` bytes */
} JaclChan;

static int     chan_be_open(uint32_t cap, void **w, void **r, uint32_t *max);
static int64_t chan_be_write(void *w, const uint8_t *p, uint32_t n);
static int64_t chan_be_read(void *r, uint8_t *buf, uint32_t cap);
static void    chan_be_close(void *h, uint32_t end);
static int     chan_be_cause(void *h, uint32_t end);

static JaclChan *chan_of(JaclVal v) {
  if (jaclrt_type_index(v) != 0x15) return 0;
  JaclObj *o = (JaclObj *)jaclrt_as_ptr(v);
  return o->obj_type == JOBJ_BLOB ? (JaclChan *)jacl_obj_payload(o) : 0;
}

static JaclVal chan_err(const char *msg) {
  uint32_t n = 0;
  while (msg[n]) n++;
  return jacl_error_new(jacl_str_new(msg, n));
}

/* unir-wire's Cause, in order (unir spec §10). */
static JaclVal chan_severed(void *h, uint32_t end) {
  static const char *const names[] = {
    "channel severed: peer-reset", "channel severed: transport-timeout",
    "channel severed: io-error", "channel severed: malformed-frame",
    "channel severed: mark-regression", "channel severed: credit-overrun",
    "channel severed: revoked", "channel severed: cancelled",
    "channel severed: budget-exhausted", "channel severed: unknown",
  };
  int c = chan_be_cause(h, end);
  return chan_err(c >= 0 && c < 10 ? names[c] : names[9]);
}

static JaclVal chan_end(uint32_t end, void *h, uint32_t max) {
  uint32_t tail = end == CHAN_R ? max : 0;
  JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(JaclChan) + tail);
  JaclChan *c = (JaclChan *)jacl_obj_payload(o);
  c->end = end; c->busy = 0; c->closed = 0; c->max = max; c->handle = h;
  c->tail_off = 0; c->tail_len = 0;
  return jaclrt_from_ptr(JACL_TAG_STREAM, o);
}

/* Claims `v` as an open end of kind `end` for one operation; 0 and `*err` set if it can't. */
static JaclChan *chan_claim(JaclVal v, uint32_t end, const char *op, JaclVal *err) {
  JaclChan *c = chan_of(v);
  if (!c || c->end != end) {
    *err = chan_err(end == CHAN_W ? "write: not a channel's write end"
                                  : "read: not a channel's read end");
    return 0;
  }
  if (__vm_atomic_cas32(&c->busy, 0, 1) != 0) { *err = chan_err("channel busy"); return 0; }
  if (c->closed) {
    __vm_atomic_store32(&c->busy, 0);
    *err = chan_err(op);
    return 0;
  }
  return c;
}

static void chan_release(JaclChan *c) { __vm_atomic_store32(&c->busy, 0); }

/* Writes every byte on a claimed write end, in frames of at most `max`. */
static JaclVal chan_write_all(JaclChan *c, const uint8_t *p, uint32_t n) {
  for (uint32_t off = 0; off < n;) {
    uint32_t k = n - off < c->max ? n - off : c->max;
    int64_t s = chan_be_write(c->handle, p + off, k);
    if (s == CHAN_BE_END) return chan_err("write: the reader closed the channel");
    if (s == CHAN_BE_SEVERED) return chan_severed(c->handle, CHAN_W);
    if (s < 0) return chan_err("write: channel failed");
    off += k;
  }
  return JACL_NIL;
}

/* [channel] / [channel CAP] -> [w r]: a bounded byte channel of CAP frames (default 16). */
JaclVal jacl_channel_n(JaclVal cap) {
  int32_t n = jaclrt_is_nil(cap) ? 16 : jaclrt_is_i32(cap) ? jaclrt_as_i32(cap) : -1;
  if (n < 1 || n > 1024) return chan_err("channel: capacity must be 1..1024 frames");
  void *w, *r;
  uint32_t max;
  if (!chan_be_open((uint32_t)n, &w, &r, &max)) return chan_err("channel: unavailable");
  JaclVal wv = chan_end(CHAN_W, w, max);
  JaclVal rv = chan_end(CHAN_R, r, max);
  return jacl_vec_push(jacl_vec_push(jacl_vec_empty(), wv), rv);
}
JaclVal jacl_channel(void) { return jacl_channel_n(JACL_NIL); }

/* [write W BYTES]: every byte of a str or a u8/i8 buffer, in frames of at most `max`. */
JaclVal jacl_chan_write(JaclVal w, JaclVal bytes) {
  const uint8_t *p;
  uint32_t n;
  char inl[8];
  if (jaclrt_is_inline_string(bytes)) {
    jaclrt_inline_get(bytes, inl, sizeof inl);
    p = (const uint8_t *)inl;
    n = jaclrt_inline_len(bytes);
  } else if (jaclrt_type_index(bytes) == 0x05) {
    p = (const uint8_t *)str_data(bytes);
    n = str_obj(bytes)->len;
  } else if (jaclrt_type_index(bytes) == 0x1E && fb_ndims(bytes) == 1 && fb_code(bytes) <= 1) {
    p = (const uint8_t *)fb_data(bytes);
    n = (uint32_t)fb_outer(bytes);
  } else {
    return chan_err("write: bytes must be a str or a [Buf n u8]");
  }
  JaclVal err;
  JaclChan *c = chan_claim(w, CHAN_W, "write: channel closed", &err);
  if (!c) return err;
  JaclVal out = chan_write_all(c, p, n);
  chan_release(c);
  return out;
}

/* [read R N]: up to N bytes as a [Buf n u8]; nil at end of stream. */
JaclVal jacl_chan_read(JaclVal r, JaclVal n) {
  if (!jaclrt_is_i32(n) || jaclrt_as_i32(n) < 1) return chan_err("read: N must be a positive i32");
  JaclVal err;
  JaclChan *c = chan_claim(r, CHAN_R, "read: channel closed", &err);
  if (!c) return err;
  JaclVal out = JACL_NIL;
  while (c->tail_len == 0) {              /* skip empty frames: a byte stream has none */
    int64_t s = chan_be_read(c->handle, c->tail, c->max);
    if (s == CHAN_BE_END) { chan_release(c); return JACL_NIL; }
    if (s == CHAN_BE_SEVERED) { chan_release(c); return chan_severed(c->handle, CHAN_R); }
    if (s < 0) { chan_release(c); return chan_err("read: channel failed"); }
    c->tail_off = 0;
    c->tail_len = (uint32_t)s;
  }
  uint32_t k = (uint32_t)jaclrt_as_i32(n);
  if (k > c->tail_len) k = c->tail_len;
  int32_t dims = (int32_t)k;
  out = fb_alloc_nd(1, 1, &dims);          /* may reach a GC safepoint; `c` is non-moving */
  memcpy(fb_data(out), c->tail + c->tail_off, k);
  c->tail_off += k;
  c->tail_len -= k;
  chan_release(c);
  return out;
}

/* [close CH]: a write end completes (the reader sees the buffered bytes, then nil); a read
 * end cancels (the writer's next write is an error). Closing twice is a no-op. */
JaclVal jacl_chan_close(JaclVal ch) {
  JaclChan *c = chan_of(ch);
  if (!c) return chan_err("close: not a channel end");
  if (__vm_atomic_cas32(&c->busy, 0, 1) != 0) return chan_err("channel busy");
  if (!c->closed) {
    c->closed = 1;
    chan_be_close(c->handle, c->end);
  }
  chan_release(c);
  return JACL_NIL;
}

#ifdef JACL_UNIR
#include "chan_unir.c"
#include "pipe_unir.c"
#else
static int     chan_be_open(uint32_t cap, void **w, void **r, uint32_t *max) { (void)cap; (void)w; (void)r; (void)max; return 0; }
static int64_t chan_be_write(void *w, const uint8_t *p, uint32_t n) { (void)w; (void)p; (void)n; return CHAN_BE_FAILED; }
static int64_t chan_be_read(void *r, uint8_t *buf, uint32_t cap) { (void)r; (void)buf; (void)cap; return CHAN_BE_FAILED; }
static void    chan_be_close(void *h, uint32_t end) { (void)h; (void)end; }
static int     chan_be_cause(void *h, uint32_t end) { (void)h; (void)end; return -1; }
/* Without vats there are no stages: output is the host's, `[stdin]` etc. are nil, `[args]` empty,
 * and `!cmd` runs through the `exec` capability (pipe_unir.c). */
JaclVal jacl_stage_ends[3] = {JACL_NIL, JACL_NIL, JACL_NIL};
void jacl_out(const char *b, long n) { write(1, b, n); }
JaclVal jacl_stage_finish(JaclVal result) { return result; }
JaclVal jacl_stdin(void) { return JACL_NIL; }
JaclVal jacl_stdout(void) { return JACL_NIL; }
JaclVal jacl_stderr(void) { return JACL_NIL; }
JaclVal jacl_args(void) { return jacl_vec_empty(); }
JaclVal jacl_pipeline(JaclVal stages) {
  if (jaclrt_as_i32(jacl_len(stages)) == 1) return jacl_exec_capture(jacl_vec_get_at(stages, jaclrt_i32(0)));
  return chan_err("pipeline: needs vats (a TEMEN runtime built with JACL_UNIR)");
}
#endif
