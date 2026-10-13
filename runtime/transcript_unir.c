/* transcript_unir.c — the transcript (theSherwood/unir decisions 100, 107 and 108; #204). Included
 * by chan.c with the unir backend, after value.c and service_unir.c.
 *
 * A shell writes what its jobs show into a transcript: a store document of blocks, each headed by
 * its command and job, which every renderer reads and draws by type (unir spec §15). The writer
 * is unir's, behind its C ABI, so nothing here encodes a record:
 *
 *   [transcript NAME T L]            a writer of the transcript at ref T, its typed line at ref
 *                                    L, over a session with the store granted as NAME; an integer
 *                                    handle, or an error
 *   [transcript-line T TEXT]         sets the typed line, committed at once
 *   [transcript-block T CMD JOB S]   opens a block for job JOB, which CMD started, of values of
 *                                    schema S (from [schema DESC]), or of this runtime's own
 *                                    values when S is nil; its index
 *   [transcript-output T CMD JOB]    opens a block for a job's output, text; its index
 *   [transcript-push T B V]          adds V to block B, converted by structure to its schema (a
 *                                    live block's replaces the last)
 *   [transcript-write T B TEXT]      appends TEXT to output block B
 *   [transcript-end T B]             ends block B, complete
 *   [transcript-flush T]             commits what was queued as one version
 *   [schema DESC]                    the schema DESC describes: a connection schema, written as
 *                                    unir's meta-schema has it (`types`, each "text", "u64",
 *                                    [map "struct" [map "name" N "fields" [vec [map "name" F
 *                                    "type" I] …]]] …, and `substreams`, each [map "name" N
 *                                    "type" I "profile" P]); an integer handle, or an error. A
 *                                    block's values are messages of its entry 0, and a profile of
 *                                    3 (latest-wins) makes the block live.
 *
 * Each call that fails returns an error value and leaves the transcript as it was. A call that
 * waits for the store parks its fiber, so a transcript is one call at a time, as a channel's end
 * is: another fiber's call meanwhile is an error ("transcript busy"). A job's output is committed
 * whenever 32 KiB of it is waiting, so the writer never holds more. */

#define JACL_TRANSCRIPTS 2u
#define JACL_SCHEMAS 64u
/* The transcript's session, as unir's root wires it (decision 107): requests in 64 KB frames, so a
 * commit carries as much of a job's output; replies as the store vat's other sessions'. */
#define JACL_TRANSCRIPT_FRAMES 4u
#define JACL_TRANSCRIPT_SLOT 65536u
/* The largest value a block takes, as a message of its schema. */
#define JACL_TRANSCRIPT_VALUE 16384u
/* Output written and not yet committed, at most. */
#define JACL_TRANSCRIPT_PENDING 32768u

typedef struct {
  unir_transcript *t;
  int32_t busy;       /* 1 while a call is in flight (cas32) */
  uint32_t pending;   /* output bytes written since the last flush */
  /* Each block's schema: an index into jacl_schemas, or -1 for this runtime's own values. */
  int8_t *schema;
  uint32_t blocks, cap;
} JaclTranscript;

static JaclTranscript jacl_transcripts[JACL_TRANSCRIPTS];
static unir_schema *jacl_schemas[JACL_SCHEMAS];

static JaclTranscript *transcript_of(JaclVal t) {
  if (!jaclrt_is_i32(t)) return 0;
  int32_t i = jaclrt_as_i32(t);
  return i >= 0 && (uint32_t)i < JACL_TRANSCRIPTS && jacl_transcripts[i].t ? &jacl_transcripts[i] : 0;
}

/* The open transcript `t`, taken for one call (transcript_done releases it); 0 with `*err` set
 * if it is not one, or another fiber's call holds it. */
static JaclTranscript *transcript_take(JaclVal t, const char *what, JaclVal *err) {
  JaclTranscript *x = transcript_of(t);
  static char msg[64];
  uint32_t n = 0;
  for (const char *p = what; *p && n < 32; p++) msg[n++] = *p;
  const char *why = !x ? ": not an open transcript" : ": transcript busy";
  if (x && __vm_atomic_cas32(&x->busy, 0, 1) == 0) return x;
  for (const char *p = why; *p && n < sizeof msg - 1; p++) msg[n++] = *p;
  msg[n] = 0;
  *err = chan_err(msg);
  return 0;
}

static JaclVal transcript_done(JaclTranscript *x, JaclVal r) {
  __vm_atomic_store32(&x->busy, 0);
  return r;
}

/* The string `v` copied into `buf` (NUL-terminated); its length, or -1 if it is not a string or
 * does not fit. */
static int64_t transcript_str(JaclVal v, char *buf, uint32_t cap) {
  if (!jaclrt_is_string(v)) return -1;
  uint32_t n = jacl_str_len(v);
  if (n >= cap) return -1;
  jacl_str_bytes(v, buf, cap);
  return n;
}

static JaclVal transcript_err(const char *what, int64_t r) {
  static char msg[112];
  const char *why = r == UNIR_EMISMATCH ? ": the value does not fit the block's schema"
                  : r == UNIR_EINVALID ? ": refused"
                  : r == UNIR_EENDED ? ": the store is gone"
                  : ": the store failed";
  uint32_t n = 0;
  for (const char *p = what; *p && n < 48; p++) msg[n++] = *p;
  for (const char *p = why; *p && n < sizeof msg - 1; p++) msg[n++] = *p;
  msg[n] = 0;
  return chan_err(msg);
}

/* [schema DESC] */
JaclVal jacl_schema(JaclVal desc) {
  static uint8_t buf[JACL_TRANSCRIPT_VALUE];
  JaclVal err = JACL_NIL;
  uint64_t n = jv_encode_into(desc, buf, sizeof buf, &err);
  if (!n) return err;
  int32_t slot = -1;
  for (uint32_t i = 0; i < JACL_SCHEMAS && slot < 0; i++)
    if (!jacl_schemas[i]) slot = (int32_t)i;
  if (slot < 0) return chan_err("schema: too many");
  unir_schema *s = unir_schema_from_value(jv_schema(), buf, n);
  if (!s) return chan_err("schema: not a valid connection schema");
  jacl_schemas[slot] = s;
  return jaclrt_i32(slot);
}

/* [transcript NAME T L] */
JaclVal jacl_transcript(JaclVal name, JaclVal transcript, JaclVal line) {
  char nb[128], tb[256], lb[256];
  int64_t nl = transcript_str(name, nb, sizeof nb), tl = transcript_str(transcript, tb, sizeof tb),
          ll = transcript_str(line, lb, sizeof lb);
  if (nl < 0 || tl < 0 || ll < 0) return chan_err("transcript: the names are not strings");
  int64_t st = UNIR_EINVALID;
  int32_t slot = -1;
  /* Under the open lock, as a service's session is opened: the store is running, so its offer
   * comes at once. */
  unir_lock(&jacl_unir_open_lock);
  unir_vat *vat = unir_vat_get();
  for (uint32_t i = 0; i < JACL_TRANSCRIPTS && slot < 0; i++)
    if (!jacl_transcripts[i].t) slot = (int32_t)i;
  unir_transcript *t = vat && slot >= 0
      ? unir_transcript_open(vat, (const uint8_t *)nb, (uint64_t)nl, JACL_TRANSCRIPT_FRAMES,
                             JACL_TRANSCRIPT_SLOT, JACL_SERVICE_FRAMES, JACL_SERVICE_SLOT,
                             (const uint8_t *)tb, (uint64_t)tl, (const uint8_t *)lb, (uint64_t)ll,
                             &st)
      : 0;
  if (t) jacl_transcripts[slot] = (JaclTranscript){t, 0, 0, 0, 0, 0};
  unir_unlock(&jacl_unir_open_lock);
  if (t) return jaclrt_i32(slot);
  if (slot < 0) return chan_err("transcript: too many open");
  return chan_err(st == UNIR_ENOCAP ? "transcript: no store granted by that name"
                                    : "transcript: the store did not connect");
}

/* [transcript-line T TEXT] */
JaclVal jacl_transcript_line(JaclVal t, JaclVal text) {
  if (!jaclrt_is_string(text)) return chan_err("transcript-line: the line is not a string");
  uint32_t n = jacl_str_len(text);
  int32_t cap = (int32_t)n + 1;
  JaclVal keep[1] = {fb_alloc_nd(1, 1, &cap)};  /* may collect; `keep` is on the data stack */
  char *b = (char *)fb_data(keep[0]);
  jacl_str_bytes(text, b, (uint32_t)cap);
  JaclVal err = JACL_NIL;
  JaclTranscript *x = transcript_take(t, "transcript-line", &err);
  if (!x) return err;
  int64_t r = unir_transcript_line(jacl_unir_vat, x->t, (const uint8_t *)b, n);
  return transcript_done(x, r < 0 ? transcript_err("transcript-line", r) : JACL_NIL);
}

/* Records block `b`'s schema, growing the table as blocks come. */
static int transcript_note(JaclTranscript *x, int64_t b, int8_t schema) {
  if ((uint64_t)b != x->blocks) return 0;
  if (x->blocks == x->cap) {
    uint32_t cap = x->cap ? x->cap * 2 : 64;
    int8_t *s = (int8_t *)unir_host_alloc(cap, 1);
    if (!s) return 0;
    for (uint32_t i = 0; i < x->blocks; i++) s[i] = x->schema[i];
    if (x->schema) unir_host_free(x->schema, x->cap, 1);
    x->schema = s;
    x->cap = cap;
  }
  x->schema[x->blocks++] = schema;
  return 1;
}

/* [transcript-block T CMD JOB S] */
JaclVal jacl_transcript_block(JaclVal t, JaclVal command, JaclVal job, JaclVal schema) {
  char cb[1024];
  int64_t cl = transcript_str(command, cb, sizeof cb);
  if (cl < 0 || !jaclrt_is_i32(job) || jaclrt_as_i32(job) < 0)
    return chan_err("transcript-block: a command and a job number");
  int8_t k = -1;
  const unir_schema *s = jv_schema();
  if (!jaclrt_is_nil(schema)) {
    int32_t i = jaclrt_is_i32(schema) ? jaclrt_as_i32(schema) : -1;
    if (i < 0 || (uint32_t)i >= JACL_SCHEMAS || !jacl_schemas[i])
      return chan_err("transcript-block: not a schema");
    k = (int8_t)i;
    s = jacl_schemas[i];
  }
  JaclVal err = JACL_NIL;
  JaclTranscript *x = transcript_take(t, "transcript-block", &err);
  if (!x) return err;
  int64_t b = unir_transcript_begin(x->t, (const uint8_t *)cb, (uint64_t)cl,
                                    (uint64_t)jaclrt_as_i32(job), s, 0);
  if (b < 0) return transcript_done(x, transcript_err("transcript-block", b));
  if (!transcript_note(x, b, k)) return transcript_done(x, chan_err("transcript-block: out of memory"));
  return transcript_done(x, jaclrt_i32((int32_t)b));
}

/* [transcript-output T CMD JOB] */
JaclVal jacl_transcript_output(JaclVal t, JaclVal command, JaclVal job) {
  char cb[1024];
  int64_t cl = transcript_str(command, cb, sizeof cb);
  if (cl < 0 || !jaclrt_is_i32(job) || jaclrt_as_i32(job) < 0)
    return chan_err("transcript-output: a command and a job number");
  JaclVal err = JACL_NIL;
  JaclTranscript *x = transcript_take(t, "transcript-output", &err);
  if (!x) return err;
  int64_t b = unir_transcript_begin_output(x->t, (const uint8_t *)cb, (uint64_t)cl,
                                           (uint64_t)jaclrt_as_i32(job));
  if (b < 0) return transcript_done(x, transcript_err("transcript-output", b));
  if (!transcript_note(x, b, -1)) return transcript_done(x, chan_err("transcript-output: out of memory"));
  return transcript_done(x, jaclrt_i32((int32_t)b));
}

static int64_t transcript_block_of(JaclTranscript *x, JaclVal b) {
  if (!jaclrt_is_i32(b)) return -1;
  int32_t i = jaclrt_as_i32(b);
  return i >= 0 && (uint32_t)i < x->blocks ? i : -1;
}

/* [transcript-push T B V] */
JaclVal jacl_transcript_push(JaclVal t, JaclVal block, JaclVal v) {
  JaclVal err = JACL_NIL;
  JaclTranscript *x = transcript_take(t, "transcript-push", &err);
  if (!x) return err;
  int64_t b = transcript_block_of(x, block);
  if (b < 0) return transcript_done(x, chan_err("transcript-push: not a block"));
  /* Under the busy flag, which keeps these buffers to one call at a time. */
  static uint8_t value[JACL_TRANSCRIPT_VALUE], msg[JACL_TRANSCRIPT_VALUE];
  uint64_t n = jv_encode_into(v, value, sizeof value, &err);
  if (!n) return transcript_done(x, err);
  const uint8_t *frame = value;
  int8_t k = x->schema[b];
  if (k >= 0) {
    int64_t m = unir_value_message(jv_schema(), value, n, jacl_schemas[k], 0, msg, sizeof msg);
    if (m < 0) return transcript_done(x, transcript_err("transcript-push", m));
    if ((uint64_t)m > sizeof msg)
      return transcript_done(x, chan_err("transcript-push: the value is too large"));
    frame = msg;
    n = (uint64_t)m;
  }
  int64_t r = unir_transcript_push(x->t, (uint64_t)b, frame, n);
  return transcript_done(x, r < 0 ? transcript_err("transcript-push", r) : JACL_NIL);
}

/* Commits what is queued; 0 or an error. */
static int64_t transcript_commit(JaclTranscript *x) {
  int64_t r = unir_transcript_flush(jacl_unir_vat, x->t);
  if (r >= 0) x->pending = 0;
  return r;
}

/* [transcript-write T B TEXT] */
JaclVal jacl_transcript_write(JaclVal t, JaclVal block, JaclVal text) {
  if (!jaclrt_is_string(text)) return chan_err("transcript-write: the output is not a string");
  uint32_t n = jacl_str_len(text);
  int32_t cap = (int32_t)n + 1;
  JaclVal keep[1] = {fb_alloc_nd(1, 1, &cap)};  /* may collect; `keep` is on the data stack */
  char *buf = (char *)fb_data(keep[0]);
  jacl_str_bytes(text, buf, (uint32_t)cap);
  JaclVal err = JACL_NIL;
  JaclTranscript *x = transcript_take(t, "transcript-write", &err);
  if (!x) return err;
  int64_t b = transcript_block_of(x, block);
  if (b < 0) return transcript_done(x, chan_err("transcript-write: not a block"));
  /* A part at a time, committing whenever a part's worth waits. */
  int64_t r = 0;
  for (uint32_t done = 0; r >= 0 && done < n;) {
    uint32_t part = n - done;
    if (part > JACL_TRANSCRIPT_PENDING - x->pending) part = JACL_TRANSCRIPT_PENDING - x->pending;
    r = unir_transcript_write(x->t, (uint64_t)b, (const uint8_t *)buf + done, part);
    if (r < 0) break;
    done += part;
    x->pending += part;
    if (x->pending >= JACL_TRANSCRIPT_PENDING) r = transcript_commit(x);
  }
  return transcript_done(x, r < 0 ? transcript_err("transcript-write", r) : JACL_NIL);
}

/* [transcript-end T B] */
JaclVal jacl_transcript_end(JaclVal t, JaclVal block) {
  JaclVal err = JACL_NIL;
  JaclTranscript *x = transcript_take(t, "transcript-end", &err);
  if (!x) return err;
  int64_t b = transcript_block_of(x, block);
  if (b < 0) return transcript_done(x, chan_err("transcript-end: not a block"));
  int64_t r = unir_transcript_end(x->t, (uint64_t)b, UNIR_ENDED_COMPLETE);
  return transcript_done(x, r < 0 ? transcript_err("transcript-end", r) : JACL_NIL);
}

/* [transcript-flush T] */
JaclVal jacl_transcript_flush(JaclVal t) {
  JaclVal err = JACL_NIL;
  JaclTranscript *x = transcript_take(t, "transcript-flush", &err);
  if (!x) return err;
  int64_t r = transcript_commit(x);
  return transcript_done(x, r < 0 ? transcript_err("transcript-flush", r) : JACL_NIL);
}

/* At exit: close the transcripts still open. What was not flushed is lost, as a program's
 * unflushed output is. */
static void jacl_transcripts_close(void) {
  for (uint32_t i = 0; i < JACL_TRANSCRIPTS; i++) {
    JaclTranscript *x = &jacl_transcripts[i];
    if (!x->t) continue;
    unir_transcript_close(jacl_unir_vat, x->t);
    if (x->schema) unir_host_free(x->schema, x->cap, 1);
    *x = (JaclTranscript){0, 0, 0, 0, 0, 0};
  }
}
