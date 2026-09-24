/* pipe_unir.c — pipelines of vats (docs/UNIR_PIPELINES.md, theSherwood/unir#19). Included by
 * chan.c after chan_unir.c when the runtime is built with JACL_UNIR.
 *
 * A **stage** is a JACL program spawned as a detached child vat. Its endowment names its
 * stdio edges `unir.stdin`, `unir.stdout` and `unir.stderr`, and its arguments are argv
 * strings, NUL-terminated. A vat endowed with `unir.stdout` is a stage: `[stdin]`, `[stdout]`
 * and `[stderr]` are those edges as channel ends, `print` writes to stdout, and at exit the
 * runtime ends them (see jacl_stage_finish) and returns status 0, or 1 if the program
 * returned an error value, for its parent's join.
 *
 * The **shell** side is jacl_pipeline: `!a x | !b` spawns the program capabilities
 * `bin.a` and `bin.b` from this vat's endowment, joined by edges, and returns the last
 * stage's output as a string, or an error value naming the first stage that failed. */

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
  /* Wrapped outside the lock: chan_end allocates, which may collect. */
  for (int i = 0; i < 3; i++)
    if (h[i]) jacl_stage_ends[i] = chan_end(i ? CHAN_W : CHAN_R, h[i], max);
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

/* Writes to a stage's write end, waiting while another fiber's operation holds it: the wait
 * on the busy word parks this fiber, so the holder (perhaps parked for credit on this
 * worker) can finish. */
static void stage_write(JaclVal end, const uint8_t *b, uint32_t n) {
  JaclChan *c = chan_of(end);
  while (__vm_atomic_cas32(&c->busy, 0, 1) != 0) __vm_wait32(&c->busy, 1, JACL_STAGE_POLL_NS);
  if (!c->closed) (void)chan_write_all(c, b, n);
  chan_release(c);
}

/* stdout for `print`: the stage's stdout edge, else the host stream. */
void jacl_out(const char *b, long n) {
  JaclVal out = jacl_stage() ? stage_end(1) : JACL_NIL;
  if (jaclrt_is_nil(out)) { write(1, b, n); return; }
  stage_write(out, (const uint8_t *)b, (uint32_t)n);
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

/* [args]: the argv this stage was spawned with, as a vector of strings; empty otherwise. */
JaclVal jacl_args(void) {
  JaclVal out = jacl_vec_empty();
  if (!jacl_stage()) return out;
  char ab[4096];
  unir_lock(&jacl_unir_open_lock);
  unir_vat *vat = unir_vat_get();
  int64_t n = vat ? unir_vat_args(vat, (uint8_t *)ab, sizeof ab) : -1;
  unir_unlock(&jacl_unir_open_lock);
  for (int64_t at = 0; at < n;) {
    int64_t end = at;
    while (end < n && ab[end]) end++;
    out = jacl_vec_push(out, jacl_str_new(ab + at, (uint32_t)(end - at)));
    at = end + 1;
  }
  return out;
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

/* Appends `n` bytes to `acc`, keeping at most `keep` bytes (0: all) from its end. */
static JaclVal pipe_append(JaclVal acc, const uint8_t *b, int64_t n, uint32_t keep) {
  acc = jacl_str_concat(acc, jacl_str_new((const char *)b, (uint32_t)n));
  uint32_t len = jacl_str_len(acc);
  if (!keep || len <= keep) return acc;
  char all[JACL_STAGE_ERR_TAIL + 1016 + 1];   /* `acc` held at most `keep` before this frame */
  jacl_str_bytes(acc, all, sizeof all);
  return jacl_str_new(all + len - keep, keep);
}

/* [pipeline STAGES]: STAGES is a vector of argv vectors. Each stage runs the program
 * capability `bin.<argv 0>` as a child vat; stage i's stdout is stage i+1's stdin, and the
 * last one's is read here. Returns that output as a string, or an error value naming the
 * first stage that failed (with the end of its stderr). A lone `!cmd` whose program this
 * vat does not hold runs through the `exec` capability instead, as it always has. */
JaclVal jacl_pipeline(JaclVal stages) {
  int32_t n = jaclrt_as_i32(jacl_len(stages));
  if (n < 1 || n > JACL_STAGES_MAX) return pipe_err("1..16 stages", JACL_NIL, JACL_NIL);
  /* JACL values live across parks only in `keep` (on the fiber's data stack, which the
   * collector scans): [0] the output, [1 + i] stage i's stderr tail, [1 + n + i] its argv. */
  JaclVal keep[1 + 2 * JACL_STAGES_MAX];
  int64_t module[JACL_STAGES_MAX], child[JACL_STAGES_MAX];
  int64_t out[JACL_STAGES_MAX], err[JACL_STAGES_MAX];
  unir_consumer *errc[JACL_STAGES_MAX];
  keep[0] = jacl_str_new("", 0);
  unir_lock(&jacl_unir_open_lock);
  unir_vat *vat = unir_vat_get();
  unir_unlock(&jacl_unir_open_lock);
  for (int32_t i = 0; i < n; i++) {
    JaclVal argv = jacl_vec_get_at(stages, jaclrt_i32(i));
    keep[1 + i] = jacl_str_new("", 0);
    keep[1 + n + i] = argv;
    JaclVal name = jacl_vec_get_at(argv, jaclrt_i32(0));
    if (!jaclrt_is_string(name)) return pipe_err("a program name must be a string", JACL_NIL, JACL_NIL);
    char nb[128] = "bin.";
    uint32_t nl = jacl_str_len(name);
    if (nl > sizeof nb - 5) return pipe_err("program name too long: ", name, JACL_NIL);
    jacl_str_bytes(name, nb + 4, sizeof nb - 4);
    module[i] = vat ? unir_endowed(vat, nb, 4 + nl) : UNIR_ENOCAP;
    if (module[i] < 0) {
      if (n == 1) return jacl_exec_capture(argv);
      return pipe_err("no program ", name, JACL_NIL);
    }
  }
  int64_t len = unir_edge_len(JACL_STAGE_FRAMES, JACL_UNIR_SLOT);
  for (int32_t i = 0; i < n; i++) {
    out[i] = unir_region_create(vat, (uint64_t)len);
    err[i] = unir_region_create(vat, (uint64_t)len);
    if (out[i] < 0 || err[i] < 0) return pipe_err("out of edge memory", JACL_NIL, JACL_NIL);
  }
  int host = jacl_host_stdout();
  unir_consumer *outc = 0;
  int32_t spawned = 0;
  for (int32_t i = 0; i < n; i++) {
    char ab[4096];
    uint64_t alen = 0;
    JaclVal argv = keep[1 + n + i];
    int32_t argc = jaclrt_as_i32(jacl_len(argv));
    for (int32_t k = 0; k < argc; k++) {
      JaclVal s = jacl_to_string(jacl_vec_get_at(argv, jaclrt_i32(k)));
      uint32_t sl = jaclrt_is_string(s) ? jacl_str_len(s) : 0;
      if (alen + sl + 1 > sizeof ab) break;
      if (sl) jacl_str_bytes(s, ab + alen, (uint32_t)(sizeof ab - alen));
      alen += sl;
      ab[alen++] = 0;
    }
    unir_grant g[4];
    uint32_t ng = 0;
    if (i > 0) g[ng++] = (unir_grant){"unir.stdin", 10, out[i - 1]};
    g[ng++] = (unir_grant){"unir.stdout", 11, out[i]};
    g[ng++] = (unir_grant){"unir.stderr", 11, err[i]};
    if (host >= 0) g[ng++] = (unir_grant){"stdout", 6, host};
    child[i] = unir_spawn(vat, module[i], JACL_STAGE_LOG2, (const uint8_t *)ab, alen, g, ng,
                          JACL_STAGE_FUEL);
    errc[i] = child[i] >= 0 ? unir_consumer_open(vat, err[i], JACL_STAGE_FRAMES, JACL_UNIR_SLOT) : 0;
    if (child[i] < 0 || !errc[i]) break;
    spawned++;
  }
  if (spawned == n) {
    outc = unir_consumer_open(vat, out[n - 1], JACL_STAGE_FRAMES, JACL_UNIR_SLOT);
  } else if (spawned) {
    /* The last stage that did start has no reader: cancel its stdout so it can finish. */
    unir_consumer *c = unir_consumer_open(vat, out[spawned - 1], JACL_STAGE_FRAMES, JACL_UNIR_SLOT);
    if (c) { unir_consumer_cancel(c); unir_consumer_free(c); }
  }
  uint8_t buf[1016];
  /* Read the output to its end, draining every stderr edge between frames so no stage
   * blocks on a full stderr ring. */
  int done = outc == 0;
  while (!done) {
    int64_t s = unir_consumer_read(outc, buf, sizeof buf, JACL_STAGE_POLL_NS, 0);
    if (s >= 0) keep[0] = pipe_append(keep[0], buf, s, 0);
    else if (s != UNIR_ESTALLED) done = 1;
    for (int32_t i = 0; i < spawned; i++)
      for (int64_t e; (e = unir_consumer_read(errc[i], buf, sizeof buf, 0, 0)) >= 0;)
        keep[1 + i] = pipe_append(keep[1 + i], buf, e, JACL_STAGE_ERR_TAIL);
  }
  for (int32_t i = 0; i < spawned; i++)
    for (int64_t e; (e = unir_consumer_read(errc[i], buf, sizeof buf, -1, 0)) >= 0;)
      keep[1 + i] = pipe_append(keep[1 + i], buf, e, JACL_STAGE_ERR_TAIL);
  JaclVal result = keep[0];
  for (int32_t i = 0; i < spawned; i++) {
    int64_t status = unir_join(vat, child[i]);
    if (status != 0 && !jaclrt_is_error(result)) {
      JaclVal name = jacl_vec_get_at(keep[1 + n + i], jaclrt_i32(0));
      JaclVal tail = jacl_str_len(keep[1 + i]) ? keep[1 + i] : JACL_NIL;
      result = pipe_err("stage failed: ", name, tail);
    }
    unir_consumer_free(errc[i]);
  }
  if (outc) unir_consumer_free(outc);
  if (spawned < n) return pipe_err("could not spawn ", jacl_vec_get_at(keep[1 + n + spawned], jaclrt_i32(0)), JACL_NIL);
  return result;
}
