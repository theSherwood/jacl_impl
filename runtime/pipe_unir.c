/* pipe_unir.c — pipelines of vats (docs/UNIR_PIPELINES.md, theSherwood/unir#19). Included by
 * chan.c after chan_unir.c when the runtime is built with JACL_UNIR.
 *
 * A **stage** is a JACL program spawned as a detached child vat. Its endowment names its
 * stdio edges `unir.stdin`, `unir.stdout` and `unir.stderr`, and its spawn payload is the
 * value `[argv env]`, encoded as value.c does (theSherwood/unir#43). A vat endowed with
 * `unir.stdout` is a stage: `[stdin]`, `[stdout]` and `[stderr]` are those edges as channel
 * ends, `print` writes to stdout, `emit` writes a value to it, and at exit the runtime sends
 * the program's value back on its stderr edge and ends the edges (see jacl_stage_finish). Its
 * join status is 0, or 1 if the program returned an error value.
 *
 * A stage's stdout carries bytes or values, decided by its first write: `emit` offers the value
 * schema (unir spec §3, typed edges) and every later message is a value; `print` and `write`
 * make it bytes. Its reader learns which from the first frame (chan_unir_next).
 *
 * The **shell** side: `!a x | !b` spawns the program capabilities `bin.a` and `bin.b` from
 * this vat's endowment, joined by edges. The fiber that evaluates it wires the last output
 * (decision 50): jacl_pipeline_stream gives it to a JACL stage as a channel read end, and
 * jacl_pipeline copies it to the enclosing output and returns the last program's value. A
 * stage that failed fails the pipeline (pipefail): the value is that stage's own error value,
 * or, if it returned none (it trapped), an error naming it. */

/* A JACL image's declared memory, log2, which a detached child's window must equal. The
 * runtime's statics set it (heap, the map area, fiber stacks); jacl_impl#161 shrinks it. The
 * harness asserts every stage image it builds declares it. */
#define JACL_STAGE_LOG2 26u
#define JACL_STAGE_FRAMES 16u
#define JACL_STAGES_MAX 16
/* Each stage's fuel, sub-allocated from this vat's (unir spec §12.4): about 10^12 steps, which a
 * root vat's unbounded budget can pay millions of times over. */
#define JACL_STAGE_FUEL (1ull << 40)
/* How much of a failed stage's stderr its error value carries: the end of it. */
#define JACL_STAGE_ERR_TAIL 512u
#define JACL_STAGE_POLL_NS 1000000
/* A stage's spawn payload, its arguments and environment as one value (docs/CTX_ENV.md): at most
 * the args area (unir.h, unir_spawn). */
#define JACL_STAGE_ARGS 16224u
/* The largest program value a stage sends back (its stderr, substream 2); a larger one comes back
 * as an error saying so. */
#define JACL_STAGE_VALUE (16u << 10)
/* The substreams of a stage's stderr edge: its text, and its value at exit. */
#define JACL_STDERR_TEXT 1
#define JACL_STDERR_VALUE 2

extern int __vm_cap_resolve(const char *name, long len);
int __vm_cap_count(void);
int __vm_cap_at(int index, int *type_id);

static int jacl_stage_known;          /* 0 unknown, 1 a stage, 2 not */
/* This stage's stdin, stdout and stderr as channel ends (nil until opened, or if absent);
 * GC roots. `print` shares the stdout end with `[stdout]`. */
JaclVal jacl_stage_ends[3] = {JACL_NIL, JACL_NIL, JACL_NIL};
static int32_t jacl_stage_opened;

static int jacl_stage(void) {
  if (!jacl_stage_known)
    jacl_stage_known = __vm_cap_resolve("unir.stdout", 11) >= 0 ? 1 : 2;
  return jacl_stage_known == 1;
}

/* Opens this stage's endowed stdio edges, once: stdin as a read end, stdout and stderr as
 * write ends. `jacl_stage_opened` is 0, 1 while one fiber opens them (the others wait), 2. */
static void stage_open(void) {
  if (__vm_atomic_load32(&jacl_stage_opened) == 2 || !jacl_stage()) return;
  if (__vm_atomic_cas32(&jacl_stage_opened, 0, 1) != 0) {
    while (__vm_atomic_load32(&jacl_stage_opened) != 2)
      __vm_wait32(&jacl_stage_opened, 1, JACL_STAGE_POLL_NS);
    return;
  }
  static const char *const names[3] = {"unir.stdin", "unir.stdout", "unir.stderr"};
  uint32_t max = (uint32_t)unir_edge_max_payload(JACL_STAGE_FRAMES, JACL_UNIR_SLOT);
  void *h[3] = {0, 0, 0};
  unir_lock(&jacl_unir_open_lock);
  unir_vat *vat = unir_vat_get();
  for (int i = 0; vat && i < 3; i++) {
    int64_t cap = unir_endowed(vat, names[i], i ? 11 : 10);
    if (cap < 0) continue;
    h[i] = i ? (void *)unir_producer_open(vat, cap, JACL_STAGE_FRAMES, JACL_UNIR_SLOT)
             : (void *)unir_consumer_open(vat, cap, JACL_STAGE_FRAMES, JACL_UNIR_SLOT);
  }
  unir_unlock(&jacl_unir_open_lock);
  /* Wrapped outside the lock: chan_end allocates, which may collect. stdin and stdout carry
   * bytes or values (their first frame decides); stderr is this runtime's own convention. */
  for (int i = 0; i < 3; i++) {
    if (!h[i]) continue;
    jacl_stage_ends[i] = chan_end(i ? CHAN_W : CHAN_R, h[i], max);
    if (i < 2) chan_of(jacl_stage_ends[i])->typed = CHAN_UNKNOWN;
  }
  __vm_atomic_store32(&jacl_stage_opened, 2);
  __vm_notify(&jacl_stage_opened, 1 << 30);
}

static JaclVal stage_end(int i) {
  stage_open();
  return jacl_stage_ends[i];
}

/* [stdin] / [stdout] / [stderr]: this stage's stdio channel ends; nil outside a stage. */
JaclVal jacl_stdin(void) { return stage_end(0); }
JaclVal jacl_stdout(void) { return stage_end(1); }
JaclVal jacl_stderr(void) { return stage_end(2); }

/* Claims a stage's end, waiting while another fiber's operation holds it: the wait on the busy
 * word parks this fiber, so the holder (perhaps parked for credit on this worker) can finish. */
static JaclChan *stage_claim(JaclVal end) {
  JaclChan *c = chan_of(end);
  while (__vm_atomic_cas32(&c->busy, 0, 1) != 0) __vm_wait32(&c->busy, 1, JACL_STAGE_POLL_NS);
  return c;
}

/* Writes bytes to a stage's write end, which then carries bytes. */
static int stage_write(JaclVal end, const uint8_t *b, uint32_t n) {
  JaclChan *c = stage_claim(end);
  if (c->typed == CHAN_UNKNOWN) c->typed = CHAN_BYTES;
  int ok = !c->closed && c->typed == CHAN_BYTES && jaclrt_is_nil(chan_write_all(c, b, n));
  chan_release(c);
  return ok;
}

/* Whether this stage's stdout has ended: its reader cancelled it, or the shell severed it. Polls
 * the edge, since a stage that is not writing has not observed it; the busy flag keeps the poll
 * off a write in flight. */
static int stage_stdout_ended(void) {
  JaclChan *c = jaclrt_is_nil(jacl_stage_ends[1]) ? 0 : chan_of(jacl_stage_ends[1]);
  if (!c || __vm_atomic_cas32(&c->busy, 0, 1) != 0) return 0;
  int ended = !c->closed && unir_producer_poll((unir_producer *)c->handle) != 0;
  chan_release(c);
  return ended;
}

/* A stage learns that its pipeline was cancelled, or its reader went away, only through its
 * edges (unir spec §10: kill is Severed(cancelled)). A read of its stdin fails once its stdout
 * has ended, so a stage that only reads ends too. */
static int stage_stdin_orphaned(JaclChan *c) {
  return !jaclrt_is_nil(jacl_stage_ends[0]) && c == chan_of(jacl_stage_ends[0]) && stage_stdout_ended();
}

/* stdout for `print`: the stage's stdout edge, else the host stream. `print` has no error to
 * return, so a task that prints to an ended stdout ends there, as SIGPIPE ends a Unix program
 * (jacl_task_end). It ends cleanly: its reader went away, which is not its failure, so pipefail
 * still names the stage that did fail. The program's own task ending ends the stage. */
void jacl_out(const char *b, long n) {
  JaclVal out = jacl_stage() ? stage_end(1) : JACL_NIL;
  if (jaclrt_is_nil(out)) { write(1, b, n); return; }
  if (chan_of(out)->typed == CHAN_VALUES) {
    /* A stdout that carries values: the printed line is one text value. */
    if (n > 0 && b[n - 1] == '\n') n--;
    if (jaclrt_is_error(jacl_emit(jacl_str_new(b, (uint32_t)n))) && stage_stdout_ended()) jacl_task_end(JACL_NIL);
    return;
  }
  if (!stage_write(out, (const uint8_t *)b, (uint32_t)n) && stage_stdout_ended()) jacl_task_end(JACL_NIL);
}

/* [emit V]: V as the next message of the enclosing output. In a stage that is its stdout, which
 * the first `emit` makes a typed edge: it offers the value schema and waits for the reader to
 * accept (unir spec §3). Elsewhere it is the host stream, where V is printed. nil, or an error
 * value: V cannot cross (value.c) or needs more than one frame, the output already carries bytes,
 * or its reader refused the offer or went away. */
JaclVal jacl_emit(JaclVal v) {
  JaclVal out = jacl_stage() ? stage_end(1) : JACL_NIL;
  if (jaclrt_is_nil(out)) { jacl_print(v); return JACL_NIL; }
  JaclChan *c = stage_claim(out);
  const char *why = c->closed ? "emit: stdout closed"
                  : c->typed == CHAN_BYTES ? "emit: stdout already carries bytes" : 0;
  if (why) { chan_release(c); return chan_err(why); }
  uint8_t buf[JACL_UNIR_SLOT];
  JaclVal err = JACL_NIL;
  uint64_t n = jv_encode_into(v, buf, c->max, &err);
  if (!n) { chan_release(c); return err; }
  unir_producer *p = (unir_producer *)c->handle;
  if (c->typed == CHAN_UNKNOWN) {
    unir_schema *s = jv_schema();
    int64_t st = s ? unir_producer_offer(p, s, -1) : UNIR_EINVALID;
    if (st < 0) {
      chan_release(c);
      return chan_err(st == UNIR_EREFUSED ? "emit: the reader does not take values"
                                          : "emit: the reader closed the channel");
    }
    c->typed = CHAN_VALUES;
  }
  int64_t st = unir_producer_write(p, 1, buf, n, -1);
  chan_release(c);
  if (st == UNIR_EENDED) return chan_err("emit: the reader closed the channel");
  return st < 0 ? chan_err("emit: channel failed") : JACL_NIL;
}

/* Sends `v`, the program's value, on stderr's value substream in frames of at most a slot, and
 * returns whether it went: a value that cannot cross goes as the error saying so. */
static void stage_send_value(JaclVal v) {
  JaclVal keep[2] = {v, JACL_NIL};
  uint64_t n = jv_size(keep[0], &keep[1]);
  if (!n) {
    keep[0] = keep[1];
    n = jv_size(keep[0], &keep[1]);
    if (!n) return;
  }
  int32_t dims = (int32_t)n;
  keep[1] = fb_alloc_nd(1, 1, &dims);   /* zeroed; may collect, and `keep` is on the data stack */
  uint8_t *b = (uint8_t *)fb_data(keep[1]);
  jv_write(keep[0], b);
  JaclChan *c = stage_claim(jacl_stage_ends[2]);
  for (uint64_t off = 0; !c->closed && off < n;) {
    uint64_t k = n - off < c->max ? n - off : c->max;
    if (unir_producer_write((unir_producer *)c->handle, JACL_STDERR_VALUE, b + off, k, -1) < 0) break;
    off += k;
  }
  chan_release(c);
}

/* At exit: report an error value on stderr, end the stdio edges the program left open, and
 * turn the program's value into the join status. stdout completes, or severs with io-error if
 * the program failed, so the next stage fails too (unir spec §10); stderr completes either way,
 * since a sever would drop the message; stdin cancels, so its producer stops. Not a stage: the
 * value, unchanged. */
JaclVal jacl_stage_finish(JaclVal result) {
  if (!jacl_stage()) return result;
  stage_open();
  int failed = jaclrt_is_error(result);
  if (!jaclrt_is_nil(jacl_stage_ends[2])) stage_send_value(result);
  if (failed && !jaclrt_is_nil(jacl_stage_ends[2])) {
    JaclVal s = jacl_to_string(jacl_error_val(result));
    char sb[1024];
    uint32_t len = jaclrt_is_string(s) ? jacl_str_len(s) : 0;
    if (len > sizeof sb - 1) len = sizeof sb - 1;
    if (len) jacl_str_bytes(s, sb, sizeof sb);
    stage_write(jacl_stage_ends[2], (const uint8_t *)sb, len);
  }
  for (int i = 0; i < 3; i++) {
    JaclChan *c = jaclrt_is_nil(jacl_stage_ends[i]) ? 0 : chan_of(jacl_stage_ends[i]);
    if (!c || c->closed) continue;
    c->closed = 1;
    if (i == 1 && failed) unir_producer_sever((unir_producer *)c->handle, 2 /* io-error */);
    else chan_be_close(c->handle, c->end);
  }
  return (JaclVal)(failed ? 1 : 0);
}

/* This stage's spawn payload, the value `[argv env]` its spawner encoded (jacl_pipeline_stream),
 * copied out of the argument area, verified and decoded: its argv and $env. A payload that is
 * not one gives an error value for each. */
static void stage_args(JaclVal *argv, JaclVal *env) {
  int32_t cap = (int32_t)JACL_STAGE_ARGS;
  JaclVal keep[2] = {fb_alloc_nd(1, 1, &cap), JACL_NIL};
  unir_lock(&jacl_unir_open_lock);
  unir_vat *vat = unir_vat_get();
  int64_t n = vat ? unir_vat_args(vat, (uint8_t *)fb_data(keep[0]), JACL_STAGE_ARGS) : -1;
  unir_unlock(&jacl_unir_open_lock);
  keep[1] = n > 0 ? jv_decode((const uint8_t *)fb_data(keep[0]), (uint64_t)n) : JACL_NIL;
  int ok = !jaclrt_is_error(keep[1]) && jaclrt_type_index(keep[1]) == 0x06 && jacl_vec_count(keep[1]) == 2;
  JaclVal a = ok ? jacl_vec_get(keep[1], 0) : JACL_NIL, e = ok ? jacl_vec_get(keep[1], 1) : JACL_NIL;
  ok = ok && jaclrt_type_index(a) == 0x06 && (jaclrt_type_index(e) == 0x07 || jaclrt_is_nil(e));
  JaclVal bad = ok ? JACL_NIL : chan_err("args: this stage's payload is not [argv env]");
  if (argv) *argv = ok ? a : bad;
  if (env) *env = ok ? e : bad;
}

/* [args]: the argv this stage was spawned with, as a vector of values; empty otherwise. */
JaclVal jacl_args(void) {
  JaclVal keep[1] = {JACL_NIL};
  if (!jacl_stage()) return jacl_vec_empty();
  stage_args(&keep[0], 0);
  return keep[0];
}

/* $env: this vat's environment, read once. A stage's is what its spawner passed (empty unless it
 * passed one); the root's is the host's environment block. A GC root. */
JaclVal jacl_env_cur = JACL_NIL;
JaclVal jacl_env(void) {
  if (jaclrt_is_nil(jacl_env_cur)) {
    JaclVal keep[1] = {JACL_NIL};
    if (jacl_stage()) stage_args(0, &keep[0]);
    else keep[0] = jacl_root_env();
    jacl_env_cur = keep[0];
  }
  return jacl_env_cur;
}

/* This vat's host stdout stream, re-granted to stages so their `write` import binds (TEMEN's
 * stdio convention names it "stdout"); -1 if it holds none. */
static int jacl_host_stdout(void) {
  int h = __vm_cap_resolve("stdout", 6);
  for (int i = 0, n = __vm_cap_count(); h < 0 && i < n; i++) {
    int ty = -1;
    int c = __vm_cap_at(i, &ty);
    if (ty == 0 /* Stream */) h = c;
  }
  return h;
}

static JaclVal pipe_err(const char *what, JaclVal name, JaclVal detail) {
  uint32_t wl = 0;
  while (what[wl]) wl++;
  JaclVal m = jacl_str_concat(jacl_str_new("pipeline: ", 10), jacl_str_new(what, wl));
  if (!jaclrt_is_nil(name)) m = jacl_str_concat(m, name);
  if (!jaclrt_is_nil(detail)) m = jacl_str_concat(jacl_str_concat(m, jacl_str_new(": ", 2)), detail);
  return jacl_error_new(m);
}

/* The environment for the programs this task starts: `$ctx`'s `env` field, which `with-env` sets
 * (docs/CTX_ENV.md), with its keys and values made strings (an environment is data, str → str).
 * No field, no environment: a child inherits nothing implicitly. An error value if the field is
 * not a map. */
static JaclVal pipe_env(void) {
  JaclVal keep[4] = {jacl_map_get(jacl_ctx_get(), jacl_str_new("env", 3)), jacl_map_empty(), JACL_NIL, JACL_NIL};
  if (jaclrt_is_nil(keep[0])) return keep[1];
  if (jaclrt_type_index(keep[0]) != 0x07 || jaclrt_is_error(keep[0]))
    return pipe_err("with-env: the environment must be a map", JACL_NIL, JACL_NIL);
  if (!jaclrt_as_ptr(keep[0])) return keep[1];
  jmap_iter it = jmap_iter_init((jmap_node *)jaclrt_as_ptr(keep[0]));
  for (;;) {
    jmap_iter_result r = jmap_next_leaf(&it);
    if (r.done) break;
    keep[2] = jacl_to_string(r.item->slots[0]);
    keep[3] = jacl_to_string(*(JaclVal *)((char *)r.item->slots + sizeof(JaclVal) * r.item->key_stride));
    keep[1] = jacl_map_set(keep[1], keep[2], keep[3]);
  }
  return keep[1];
}

/* Stage `argv`'s spawn payload: the value `[argv env]`, encoded, as a [Buf n u8]; or an error
 * value if it cannot cross or is larger than the argument area. */
static JaclVal stage_payload(JaclVal argv, JaclVal env) {
  JaclVal keep[3] = {jacl_vec_push(jacl_vec_push(jacl_vec_empty(), argv), env), JACL_NIL, JACL_NIL};
  uint64_t n = jv_size(keep[0], &keep[1]);
  if (!n) return keep[1];
  if (n > JACL_STAGE_ARGS) return pipe_err("arguments too large for a stage: ", jacl_vec_get_at(argv, jaclrt_i32(0)), JACL_NIL);
  int32_t dims = (int32_t)n;
  keep[2] = fb_alloc_nd(1, 1, &dims);
  jv_write(keep[0], (uint8_t *)fb_data(keep[2]));
  return keep[2];
}

/* A running pipeline: the state its read end carries after its tail (JaclChan.pipe). Bytes
 * only, since a channel's blob holds no traced pointers: the stages' names, the ends of their
 * stderr, and their values as sent (encoded, JACL_STAGE_VALUE each, after the struct) are
 * copied in. */
#define JACL_STAGE_NAME 128u
typedef struct {
  int32_t n, spawned;
  int32_t done;                 /* the stages were joined and the ends freed */
  int32_t cause;                /* the output's sever cause (unir spec §10), or -1 */
  unir_consumer *outc;          /* the last stage's stdout; also the channel's handle */
  unir_consumer *errc[JACL_STAGES_MAX];
  int64_t child[JACL_STAGES_MAX];
  int64_t status[JACL_STAGES_MAX];
  uint32_t tail_len[JACL_STAGES_MAX];
  uint32_t val_len[JACL_STAGES_MAX];
  uint8_t val_over[JACL_STAGES_MAX];   /* the stage sent more than JACL_STAGE_VALUE */
  char name[JACL_STAGES_MAX][JACL_STAGE_NAME];
  uint8_t tail[JACL_STAGES_MAX][JACL_STAGE_ERR_TAIL];
} JaclPipe;

static JaclPipe *pipe_of(JaclChan *c) { return (JaclPipe *)((uint8_t *)c + c->pipe); }
static uint8_t *pipe_val(JaclPipe *p, int32_t i) {
  return (uint8_t *)p + ((sizeof(JaclPipe) + 7u) & ~(size_t)7u) + (size_t)i * JACL_STAGE_VALUE;
}

/* Appends a frame of stage i's value, as its stderr sends it. */
static void pipe_value_part(JaclPipe *p, int32_t i, const uint8_t *b, int64_t n) {
  if (p->val_over[i] || p->val_len[i] + (uint64_t)n > JACL_STAGE_VALUE) { p->val_over[i] = 1; return; }
  memcpy(pipe_val(p, i) + p->val_len[i], b, (size_t)n);
  p->val_len[i] += (uint32_t)n;
}

/* Stage i's value, verified (it was copied here, out of its peer's reach) and decoded; nil if it
 * sent none (it trapped, or was killed). */
static JaclVal pipe_value(JaclPipe *p, int32_t i) {
  if (p->val_over[i]) {
    uint32_t nl = 0;
    while (p->name[i][nl]) nl++;
    return pipe_err("a value larger than 16 KiB from ", jacl_str_new(p->name[i], nl), JACL_NIL);
  }
  if (!p->val_len[i]) return JACL_NIL;
  return jv_decode(pipe_val(p, i), p->val_len[i]);
}

/* Keeps the last JACL_STAGE_ERR_TAIL bytes of stage i's stderr. */
static void pipe_tail(JaclPipe *p, int32_t i, const uint8_t *b, int64_t n) {
  uint32_t keep = JACL_STAGE_ERR_TAIL, have = p->tail_len[i];
  if ((uint64_t)n >= keep) { memcpy(p->tail[i], b + n - keep, keep); p->tail_len[i] = keep; return; }
  uint32_t k = (uint32_t)n, drop = have + k > keep ? have + k - keep : 0;
  memmove(p->tail[i], p->tail[i] + drop, have - drop);
  memcpy(p->tail[i] + have - drop, b, k);
  p->tail_len[i] = have - drop + k;
}

/* Drains every stage's stderr, waiting up to `timeout_ns` per frame (-1: to its end), so no
 * stage blocks on a full stderr ring: its text, and its value. */
static void pipe_pump(JaclPipe *p, int64_t timeout_ns) {
  uint8_t buf[1016];
  for (int32_t i = 0; i < p->spawned; i++) {
    uint16_t sub = 0;
    for (int64_t e; (e = unir_consumer_read(p->errc[i], buf, sizeof buf, timeout_ns, &sub)) >= 0;) {
      if (sub == JACL_STDERR_VALUE) pipe_value_part(p, i, buf, e);
      else pipe_tail(p, i, buf, e);
    }
  }
}

/* Waits for the stages to end and reaps them: their stderr to its end, then their statuses. */
static void pipe_finish(JaclPipe *p) {
  if (p->done) return;
  pipe_pump(p, -1);
  for (int32_t i = 0; i < p->spawned; i++) {
    p->status[i] = unir_join(jacl_unir_vat, p->child[i]);
    unir_consumer_free(p->errc[i]);
  }
  if (p->outc) unir_consumer_free(p->outc);
  p->outc = 0;
  p->done = 1;
}

/* A finished pipeline's failure (pipefail: the first stage that failed, as its own error value,
 * or, if it sent none, an error naming it with the end of its stderr; else a stage that could
 * not spawn; else a sever of the output), or nil. */
static JaclVal pipe_failure(JaclPipe *p) {
  for (int32_t i = 0; i < p->n; i++) {
    uint32_t nl = 0;
    while (p->name[i][nl]) nl++;
    JaclVal name = jacl_str_new(p->name[i], nl);
    if (i >= p->spawned) return pipe_err("could not spawn ", name, JACL_NIL);
    if (p->status[i] == 0) continue;
    JaclVal own = pipe_value(p, i);
    if (jaclrt_is_error(own)) return own;
    JaclVal tail = p->tail_len[i] ? jacl_str_new((const char *)p->tail[i], p->tail_len[i]) : JACL_NIL;
    return pipe_err("stage failed: ", name, tail);
  }
  return p->cause >= 0 ? chan_cause_err(p->cause) : JACL_NIL;
}

/* Cancels a running pipeline: severs every edge end this vat holds with `cancelled`, so the last
 * stage fails its next write and each stage's end ends the one before it, and reaps the stages. */
static void pipe_cancel(JaclPipe *p) {
  unir_consumer_sever(p->outc, 7 /* cancelled */);
  for (int32_t i = 0; i < p->spawned; i++) unir_consumer_sever(p->errc[i], 7);
  p->cause = 7;
  pipe_finish(p);
}

static int64_t pipe_read(JaclChan *c, JaclVal *err) {
  JaclPipe *p = pipe_of(c);
  for (;;) {
    /* Job control for the task reading this pipeline acts on its edges (decision 50). Suspend
     * withholds credit, so the stages stall by backpressure, and holds the task; resume grants
     * credit again. Cancel severs and reaps, then ends the task. */
    int ctl = p->done ? 0 : jacl_ctl_pending();
    if (ctl & JACL_CTL_CANCEL) {
      pipe_cancel(p);
      jacl_ctl_point();
    } else if (ctl & JACL_CTL_SUSPEND) {
      unir_consumer_suspend(p->outc);
      jacl_ctl_point();
      unir_consumer_resume(p->outc);
      continue;
    }
    int64_t s = p->done ? UNIR_EENDED : chan_unir_next(c, p->outc, JACL_STAGE_POLL_NS);
    if (!p->done) pipe_pump(p, 0);
    if (s >= 0) return s;
    if (s == UNIR_ESTALLED) continue;
    if (!p->done) {
      uint32_t e = s == UNIR_EENDED ? unir_consumer_ended(p->outc) : 3 | (9u << 2);
      if ((e & 3) == 3) p->cause = (int32_t)((e >> 2) & 0xF);
      pipe_finish(p);
    }
    chan_unir_forget(c);
    *err = pipe_failure(p);
    return jaclrt_is_nil(*err) ? CHAN_BE_END : CHAN_BE_FAILED;
  }
}

/* Closing a pipeline's read end cancels the last stage's stdout, which ends the stages in turn
 * (each one's cancelled stdout, then its stdin), and reaps them. */
static void pipe_close(JaclChan *c) {
  JaclPipe *p = pipe_of(c);
  if (p->outc) unir_consumer_cancel(p->outc);
  pipe_finish(p);
  chan_unir_forget(c);
}

/* [pipeline-stream STAGES]: STAGES is a vector of argv vectors, whose elements are any values
 * that can cross (value.c). Each stage runs the program capability `bin.<argv 0>` as a child
 * vat, with `[argv env]` as its payload, and stage i's stdout is stage i+1's stdin. Returns the
 * last stage's stdout as a channel read end, whose reads drain every stage's stderr and, at its
 * end, reap the stages: a stage that failed fails that read (pipefail). The read end carries
 * bytes or values, as the last stage wrote. An error value if the pipeline cannot start. A lone
 * `!cmd` whose program this vat does not hold runs through the `exec` capability instead, as it
 * always has, and gives its output as a string.
 *
 * A read end dropped before its end is not reaped (docs/UNIR_PIPELINES.md, Later). */
JaclVal jacl_pipeline_stream(JaclVal stages) {
  int32_t n = jaclrt_as_i32(jacl_len(stages));
  if (n < 1 || n > JACL_STAGES_MAX) return pipe_err("1..16 stages", JACL_NIL, JACL_NIL);
  unir_lock(&jacl_unir_open_lock);
  unir_vat *vat = unir_vat_get();
  unir_unlock(&jacl_unir_open_lock);
  int64_t module[JACL_STAGES_MAX], out[JACL_STAGES_MAX], err[JACL_STAGES_MAX];
  char nb[4 + JACL_STAGE_NAME] = "bin.";
  for (int32_t i = 0; i < n; i++) {
    JaclVal argv = jacl_vec_get_at(stages, jaclrt_i32(i));
    JaclVal name = jacl_vec_get_at(argv, jaclrt_i32(0));
    if (!jaclrt_is_string(name)) return pipe_err("a program name must be a string", JACL_NIL, JACL_NIL);
    uint32_t nl = jacl_str_len(name);
    if (nl >= JACL_STAGE_NAME) return pipe_err("program name too long: ", name, JACL_NIL);
    jacl_str_bytes(name, nb + 4, JACL_STAGE_NAME);
    module[i] = vat ? unir_endowed(vat, nb, 4 + nl) : UNIR_ENOCAP;
    if (module[i] < 0) {
      if (n == 1) return jacl_exec_capture(argv);
      return pipe_err("no program ", name, JACL_NIL);
    }
  }
  /* Each stage's payload, `[argv env]` encoded (docs/CTX_ENV.md), before anything starts; kept
   * in `pay` (on the fiber's data stack, which the collector scans). */
  JaclVal pay[1 + JACL_STAGES_MAX];
  pay[0] = pipe_env();
  if (jaclrt_is_error(pay[0])) return pay[0];
  for (int32_t i = 0; i < n; i++) {
    pay[1 + i] = stage_payload(jacl_vec_get_at(stages, jaclrt_i32(i)), pay[0]);
    if (jaclrt_is_error(pay[1 + i])) return pay[1 + i];
  }
  int64_t len = unir_edge_len(JACL_STAGE_FRAMES, JACL_UNIR_SLOT);
  for (int32_t i = 0; i < n; i++) {
    out[i] = unir_region_create(vat, (uint64_t)len);
    err[i] = unir_region_create(vat, (uint64_t)len);
    if (out[i] < 0 || err[i] < 0) return pipe_err("out of edge memory", JACL_NIL, JACL_NIL);
  }
  /* The read end, and with it the pipeline's state; it lives across the stages' spawns in
   * `keep` (on the fiber's data stack, which the collector scans), as do the argvs. */
  uint32_t max = (uint32_t)unir_edge_max_payload(JACL_STAGE_FRAMES, JACL_UNIR_SLOT);
  uint32_t state = (uint32_t)(((sizeof(JaclPipe) + 7u) & ~(size_t)7u) + (size_t)n * JACL_STAGE_VALUE);
  JaclVal keep[2] = {stages, chan_end_ex(CHAN_R, 0, max, state)};
  JaclChan *c = chan_of(keep[1]);
  c->typed = CHAN_UNKNOWN;
  JaclPipe *p = pipe_of(c);
  p->n = n;
  p->cause = -1;
  int host = jacl_host_stdout();
  for (int32_t i = 0; i < n; i++) {
    JaclVal argv = jacl_vec_get_at(keep[0], jaclrt_i32(i));
    jacl_str_bytes(jacl_vec_get_at(argv, jaclrt_i32(0)), p->name[i], JACL_STAGE_NAME);
  }
  for (int32_t i = 0; i < n; i++) {
    unir_grant g[4];
    uint32_t ng = 0;
    if (i > 0) g[ng++] = (unir_grant){"unir.stdin", 10, out[i - 1]};
    g[ng++] = (unir_grant){"unir.stdout", 11, out[i]};
    g[ng++] = (unir_grant){"unir.stderr", 11, err[i]};
    if (host >= 0) g[ng++] = (unir_grant){"stdout", 6, host};
    p->child[i] = unir_spawn(vat, module[i], JACL_STAGE_LOG2, (const uint8_t *)fb_data(pay[1 + i]),
                             (uint64_t)fb_outer(pay[1 + i]), g, ng, JACL_STAGE_FUEL);
    p->errc[i] = p->child[i] >= 0 ? unir_consumer_open(vat, err[i], JACL_STAGE_FRAMES, JACL_UNIR_SLOT) : 0;
    if (p->child[i] < 0 || !p->errc[i]) break;
    p->spawned++;
  }
  if (p->spawned == n) {
    p->outc = unir_consumer_open(vat, out[n - 1], JACL_STAGE_FRAMES, JACL_UNIR_SLOT);
    c->handle = p->outc;
  }
  if (!p->outc) {
    /* The last stage that did start has no reader: cancel its stdout so it can finish. */
    if (p->spawned) {
      unir_consumer *oc = unir_consumer_open(vat, out[p->spawned - 1], JACL_STAGE_FRAMES, JACL_UNIR_SLOT);
      if (oc) { unir_consumer_cancel(oc); unir_consumer_free(oc); }
    }
    pipe_finish(p);
    JaclVal e = pipe_failure(p);
    return jaclrt_is_nil(e) ? pipe_err("could not open the output", JACL_NIL, JACL_NIL) : e;
  }
  return keep[1];
}

/* [pipeline STAGES]: runs the pipeline as jacl_pipeline_stream does, copying its output to the
 * enclosing output (`jacl_out`: the host stream in the shell, this stage's stdout in a stage;
 * values are emitted there, which in the shell prints them), and returns the last program's
 * value, or the failure. */
JaclVal jacl_pipeline(JaclVal stages) {
  JaclVal keep[3] = {jacl_pipeline_stream(stages), JACL_NIL, JACL_NIL};
  JaclChan *c = chan_of(keep[0]);
  if (!c || !c->pipe) return keep[0];      /* an error value, or `exec`'s output */
  int64_t s;
  while ((s = pipe_read(c, &keep[1])) >= 0) {
    if (c->typed != CHAN_VALUES) { jacl_out((const char *)c->tail, (long)s); continue; }
    keep[2] = chan_value(c);
    jacl_emit(keep[2]);
  }
  c->closed = 1;
  if (s != CHAN_BE_END) return keep[1];
  JaclPipe *p = pipe_of(c);
  return pipe_value(p, p->n - 1);
}
