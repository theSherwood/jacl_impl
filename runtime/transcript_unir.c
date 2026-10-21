/* transcript_unir.c — the transcript (theSherwood/unir decisions 100, 107 and 117; #204). Included
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
 *   [transcript-job T CMD JOB]       a place in the transcript for job JOB, which CMD started:
 *                                    a task whose `$ctx.out` is it writes its output there (see
 *                                    "A job's output" below); an integer handle, or an error
 *   [transcript-job-end S COMPLETE]  ends job S's place, its last block complete (COMPLETE true)
 *                                    or cancelled; how many blocks the job opened
 *   [transcript-pick T B V]          the name of reference V of block B, a block of references;
 *                                    nil if there is none (see "References" below)
 *   [schema DESC]                    the schema DESC describes: a connection schema, written as
 *                                    unir's meta-schema has it (`types`, each "text", "u64",
 *                                    [map "struct" [map "name" N "fields" [vec [map "name" F
 *                                    "type" I] …]]] …, and `substreams`, each [map "name" N
 *                                    "type" I "profile" P]); an integer handle, or an error. A
 *                                    block's values are messages of its entry 0, and a profile of
 *                                    3 (latest-wins) makes the block live. [schema "link"] is
 *                                    unir's link document, whose values are references: a
 *                                    renderer draws each as its name, underlined, and a person
 *                                    can activate it (unir decision 106).
 *
 * Each call that fails returns an error value and leaves the transcript as it was. A call that
 * waits for the store parks its fiber, so a transcript is one call at a time: another fiber's call
 * meanwhile waits its turn, since a shell's jobs write while the shell reads keys and flushes. A
 * job's output is committed whenever 32 KiB of it is waiting, so the writer never holds more. */

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

/* A reference a block holds (see "References" below): its block, and its name. */
typedef struct {
  uint32_t block, len;
  char *name;
} JaclRef;

typedef struct {
  unir_transcript *t;
  int32_t busy;       /* 1 while a call is in flight (cas32) */
  uint32_t pending;   /* output bytes written since the last flush */
  /* Each block's schema: an index into jacl_schemas, or -1 for this runtime's own values. */
  int8_t *schema;
  uint32_t blocks, cap;
  /* The references written, in order. */
  JaclRef *refs;
  uint32_t nrefs, refcap;
} JaclTranscript;

static JaclTranscript jacl_transcripts[JACL_TRANSCRIPTS];
static unir_schema *jacl_schemas[JACL_SCHEMAS];
/* [schema "link"]'s slot in jacl_schemas, once made. */
static int32_t jacl_link_slot = -1;

static JaclTranscript *transcript_of(JaclVal t) {
  if (!jaclrt_is_i32(t)) return 0;
  int32_t i = jaclrt_as_i32(t);
  return i >= 0 && (uint32_t)i < JACL_TRANSCRIPTS && jacl_transcripts[i].t ? &jacl_transcripts[i] : 0;
}

/* Takes `x` for one call (transcript_done releases it), parking while another fiber's call holds
 * it: a call holds it only while the store answers. */
static void transcript_wait(JaclTranscript *x) {
  while (__vm_atomic_cas32(&x->busy, 0, 1) != 0) {
    jacl_ctl_point();                              /* a waiting job can be cancelled or suspended */
    (void)__vm_wait32(&x->busy, 1, 1000000);
  }
}

/* The open transcript `t`, taken for one call (transcript_done releases it); 0 with `*err` set
 * if it is not one. */
static JaclTranscript *transcript_take(JaclVal t, const char *what, JaclVal *err) {
  JaclTranscript *x = transcript_of(t);
  if (x) {
    transcript_wait(x);
    return x;
  }
  static char msg[64];
  uint32_t n = 0;
  for (const char *p = what; *p && n < 32; p++) msg[n++] = *p;
  for (const char *p = ": not an open transcript"; *p && n < sizeof msg - 1; p++) msg[n++] = *p;
  msg[n] = 0;
  *err = chan_err(msg);
  return 0;
}

/* Gives `x` back after a call, waking a fiber that waits its turn, and one waiting for a
 * service's next event (vat_stir): a shell commits what a job wrote (#219). */
static JaclVal transcript_done(JaclTranscript *x, JaclVal r) {
  __vm_atomic_store32(&x->busy, 0);
  (void)__vm_notify(&x->busy, 1);
  vat_stir();
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
  int32_t slot = -1;
  for (uint32_t i = 0; i < JACL_SCHEMAS && slot < 0; i++)
    if (!jacl_schemas[i]) slot = (int32_t)i;
  /* A name: unir's own schema by that name, one slot however often it is asked for. */
  if (jaclrt_is_string(desc)) {
    char nb[8];
    if (transcript_str(desc, nb, sizeof nb) != 4 || nb[0] != 'l' || nb[1] != 'i' || nb[2] != 'n' ||
        nb[3] != 'k')
      return chan_err("schema: no schema by that name");
    if (jacl_link_slot >= 0) return jaclrt_i32(jacl_link_slot);
    if (slot < 0) return chan_err("schema: too many");
    unir_schema *s = unir_schema_link();
    if (!s) return chan_err("schema: unavailable");
    jacl_schemas[slot] = s;
    jacl_link_slot = slot;
    return jaclrt_i32(slot);
  }
  static uint8_t buf[JACL_TRANSCRIPT_VALUE];
  JaclVal err = JACL_NIL;
  uint64_t n = jv_encode_into(desc, buf, sizeof buf, &err);
  if (!n) return err;
  if (slot < 0) return chan_err("schema: too many");
  unir_schema *s = unir_schema_from_value(jv_schema(), buf, n);
  if (!s) return chan_err("schema: not a valid connection schema");
  jacl_schemas[slot] = s;
  return jaclrt_i32(slot);
}

/* [service-schema S NAME]: the schema of open service S's catalog entry NAME alone, the service's
 * types with that entry as entry 0, so a block can show what the entry carries typed as the service
 * sent it, as a shell shows a subscription's events (theSherwood/unir decision #282.5). */
JaclVal jacl_service_schema(JaclVal svc, JaclVal name) {
  unir_service *s = service_of(svc);
  if (!s) return chan_err("service-schema: not an open service");
  char nb[128];
  int64_t nl = transcript_str(name, nb, sizeof nb);
  if (nl < 0) return chan_err("service-schema: the entry's name is not a string");
  int32_t slot = -1;
  for (uint32_t i = 0; i < JACL_SCHEMAS && slot < 0; i++)
    if (!jacl_schemas[i]) slot = (int32_t)i;
  if (slot < 0) return chan_err("service-schema: too many");
  unir_vat *vat = vat_take();
  unir_schema *sch = unir_service_schema(s, (const uint8_t *)nb, (uint64_t)nl);
  vat_give();
  if (!sch) return chan_err("service-schema: the service has no entry by that name");
  jacl_schemas[slot] = sch;
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
  /* As a service's session is opened: the store is running, so its offer comes at once. */
  unir_vat *vat = vat_take();
  for (uint32_t i = 0; i < JACL_TRANSCRIPTS && slot < 0; i++)
    if (!jacl_transcripts[i].t) slot = (int32_t)i;
  unir_transcript *t = vat && slot >= 0
      ? unir_transcript_open(vat, (const uint8_t *)nb, (uint64_t)nl, JACL_TRANSCRIPT_FRAMES,
                             JACL_TRANSCRIPT_SLOT, JACL_SERVICE_FRAMES, JACL_SERVICE_SLOT,
                             (const uint8_t *)tb, (uint64_t)tl, (const uint8_t *)lb, (uint64_t)ll,
                             &st)
      : 0;
  if (t) jacl_transcripts[slot] = (JaclTranscript){.t = t};
  vat_give();
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
  unir_vat *vat = vat_take();
  int64_t r = unir_transcript_line(vat, x->t, (const uint8_t *)b, n);
  vat_give();
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

/* ---- References (unir decision 106) ----
 * A value of the link document's is a reference: a name a job's output gives what the shell
 * granted it. The writer keeps no values, so the transcript keeps each reference's name as it is
 * written, for the shell to resolve an activation (`pane.usc`'s Pick: a block's place among the
 * transcript's, and the value's within it) to the name it designates. A pipeline's references,
 * forwarded without decoding, are not kept. */

/* Keeps `v`'s name as block `b`'s next reference: a map's "name", or a list's first value. A
 * value with no name, or no memory for it, is not kept. Under the transcript's turn. */
static void transcript_ref(JaclTranscript *x, int64_t b, JaclVal v) {
  JaclVal name = jaclrt_type_index(v) == 0x07 ? jacl_map_get(v, jacl_str_new("name", 4))
               : jaclrt_type_index(v) == 0x06 && jacl_vec_count(v) > 0 ? jacl_vec_get(v, 0)
               : JACL_NIL;
  char nb[1024];
  int64_t n = transcript_str(name, nb, sizeof nb);
  if (n < 0) return;
  if (x->nrefs == x->refcap) {
    uint32_t cap = x->refcap ? x->refcap * 2 : 16;
    JaclRef *r = (JaclRef *)unir_host_alloc((uint64_t)cap * sizeof *r, 8);
    if (!r) return;
    for (uint32_t i = 0; i < x->nrefs; i++) r[i] = x->refs[i];
    if (x->refs) unir_host_free(x->refs, (uint64_t)x->refcap * sizeof *r, 8);
    x->refs = r;
    x->refcap = cap;
  }
  char *copy = n ? (char *)unir_host_alloc((uint64_t)n, 1) : 0;
  if (n && !copy) return;
  for (int64_t i = 0; i < n; i++) copy[i] = nb[i];
  x->refs[x->nrefs++] = (JaclRef){(uint32_t)b, (uint32_t)n, copy};
}

/* [transcript-pick T B V] */
JaclVal jacl_transcript_pick(JaclVal t, JaclVal block, JaclVal value) {
  int64_t b, v;
  if (!jacl_int_val_checked(block, &b) || !jacl_int_val_checked(value, &v))
    return chan_err("transcript-pick: a block and a value, each a number");
  JaclVal err = JACL_NIL;
  JaclTranscript *x = transcript_take(t, "transcript-pick", &err);
  if (!x) return err;
  char nb[1024];
  int64_t n = -1;
  for (uint32_t i = 0, seen = 0; i < x->nrefs && n < 0; i++) {
    if ((int64_t)x->refs[i].block != b || (int64_t)seen++ != v) continue;
    n = x->refs[i].len;
    for (int64_t k = 0; k < n; k++) nb[k] = x->refs[i].name[k];
  }
  /* The string is made once the turn is given back: it may collect. */
  (void)transcript_done(x, JACL_NIL);
  return n < 0 ? JACL_NIL : jacl_str_new(nb, (uint32_t)n);
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
  if (r >= 0 && k >= 0 && k == jacl_link_slot) transcript_ref(x, b, v);
  return transcript_done(x, r < 0 ? transcript_err("transcript-push", r) : JACL_NIL);
}

/* Commits what is queued; 0 or an error. */
static int64_t transcript_commit(JaclTranscript *x) {
  unir_vat *vat = vat_take();
  int64_t r = unir_transcript_flush(vat, x->t);
  vat_give();
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

/* ---- A job's output (#204) ----
 * A shell runs each line as a job, a task whose `$ctx.out` is the job's place in the transcript
 * (`transcript-job`), and what the task outputs goes there rather than to the host stream
 * (pipe_unir.c): into blocks headed by the job's command and number, each opened as output of its
 * kind comes. `print`'s text goes to an output block (decision 107); `emit`'s values to a block of
 * this runtime's values; and a pipeline's output to an output block if it is bytes, else to a
 * block of the schema its last program offered, adopted and forwarded without decoding (decision
 * 117). Output of another kind than the open block's, or a later pipeline's typed output, ends
 * that block and opens the next. A write looks its job up by handle under the transcript's turn,
 * so a task that outlives its job (one its line spawned) writes nowhere once the job has ended. */
#define JACL_JOBS 64u
enum { JOB_TEXT = 1, JOB_VALUES = 2, JOB_ADOPTED = 3 };

typedef struct {
  int32_t used;            /* cas32 */
  uint32_t gen;            /* bumped as the slot is freed, so a stale handle names nothing */
  JaclVal t;               /* the transcript: an integer handle */
  uint32_t job;
  int64_t block;           /* the open block, or -1 */
  uint32_t kind;           /* the open block's JOB_* */
  const void *agree;       /* an adopted block's agreement, while its pipeline runs */
  uint32_t entry;          /* an adopted block's catalog entry */
  uint32_t blocks;         /* how many blocks the job opened */
  uint32_t cmd_len;
  char cmd[1024];
} JaclJob;

static JaclJob jacl_jobs[JACL_JOBS];

/* A job's handle: its slot, and the slot's generation above it. */
static JaclJob *job_of_handle(JaclVal s) {
  if (!jaclrt_is_i32(s) || jaclrt_as_i32(s) < 0) return 0;
  uint32_t h = (uint32_t)jaclrt_as_i32(s), i = h % JACL_JOBS;
  JaclJob *j = &jacl_jobs[i];
  return __vm_atomic_load32(&j->used) && j->gen == h / JACL_JOBS ? j : 0;
}

/* Whether the running task's output goes to a job: `$ctx.out` is set, `*s`, the job's handle. */
static int job_named(JaclVal *s) {
  if (jaclrt_is_nil(jacl_ctx_cur)) return 0;
  *s = jacl_map_get(jacl_ctx_cur, jacl_str_new("out", 3));
  return !jaclrt_is_nil(*s);
}

/* Job `s` with its transcript's turn taken (transcript_done releases it); 0 if `s` names no job,
 * as when the job ended while this waited for the turn. */
static JaclJob *job_take(JaclVal s, JaclTranscript **xp) {
  JaclJob *j = job_of_handle(s);
  JaclTranscript *x = j ? transcript_of(j->t) : 0;
  if (!x) return 0;
  transcript_wait(x);
  if (job_of_handle(s) != j) {
    (void)transcript_done(x, JACL_NIL);
    return 0;
  }
  *xp = x;
  return j;
}

/* Job j's block for output of `kind`: the open one if it is of that kind (and, adopted, of that
 * agreement and entry), else the next, which ends the open one. The next is an output block for
 * text, else a block of `schema`'s `entry` (an adopted block's: the agreement's schema). The
 * block, or a UNIR_E* code. Under the transcript's turn. */
static int64_t job_block(JaclTranscript *x, JaclJob *j, uint32_t kind, const unir_schema *schema,
                         const void *agree, uint32_t entry) {
  if (j->block >= 0 && j->kind == kind && j->agree == agree && j->entry == entry) return j->block;
  if (j->block >= 0) (void)unir_transcript_end(x->t, (uint64_t)j->block, UNIR_ENDED_COMPLETE);
  j->block = -1;
  int64_t b;
  if (kind == JOB_TEXT) {
    b = unir_transcript_begin_output(x->t, (const uint8_t *)j->cmd, j->cmd_len, j->job);
  } else if (kind == JOB_ADOPTED) {
    unir_schema *own = unir_agreement_schema((const unir_agreement *)agree);
    b = own ? unir_transcript_begin(x->t, (const uint8_t *)j->cmd, j->cmd_len, j->job, own, entry)
            : UNIR_EINVALID;
    if (own) unir_schema_free(own);
  } else {
    b = unir_transcript_begin(x->t, (const uint8_t *)j->cmd, j->cmd_len, j->job, schema, entry);
  }
  if (b < 0) return b;
  /* Keeps the transcript's count of blocks; the shell names none of a job's to push into. */
  if (!transcript_note(x, b, -1)) return UNIR_EINVALID;
  j->block = b;
  j->kind = kind;
  j->agree = agree;
  j->entry = entry;
  j->blocks++;
  return b;
}

/* Text `b[0..n)` to job `s`'s output. Like `print`'s, it has no failure to report. */
static void job_write(JaclVal s, const uint8_t *b, uint64_t n) {
  JaclTranscript *x;
  JaclJob *j = job_take(s, &x);
  if (!j) return;
  int64_t r = job_block(x, j, JOB_TEXT, 0, 0, 0);
  /* A part at a time, committing whenever a part's worth waits, as transcript-write does. */
  for (uint64_t done = 0; r >= 0 && done < n;) {
    uint64_t part = n - done;
    if (part > JACL_TRANSCRIPT_PENDING - x->pending) part = JACL_TRANSCRIPT_PENDING - x->pending;
    r = unir_transcript_write(x->t, (uint64_t)j->block, b + done, part);
    if (r < 0) break;
    done += part;
    x->pending += (uint32_t)part;
    if (x->pending >= JACL_TRANSCRIPT_PENDING) r = transcript_commit(x);
  }
  (void)transcript_done(x, JACL_NIL);
}

/* A message of entry `entry` of the schema `agree` adopted, `frame[0..n)`, to job `s`'s output. */
static void job_forward(JaclVal s, const void *agree, uint32_t entry, const uint8_t *frame,
                        uint64_t n) {
  JaclTranscript *x;
  JaclJob *j = job_take(s, &x);
  if (!j) return;
  if (job_block(x, j, JOB_ADOPTED, 0, agree, entry) >= 0)
    (void)unir_transcript_push(x->t, (uint64_t)j->block, frame, n);
  (void)transcript_done(x, JACL_NIL);
}

/* `[emit V]` to job `s`, or `[emit V S]` with S's slot `k` (-1 without): V joins a block of
 * this runtime's values, or of S's entry 0, a live block when that is latest-wins. Values of
 * another schema than the open block's open the next. nil, or an error value. */
static JaclVal job_emit(JaclVal s, JaclVal v, int32_t k) {
  JaclTranscript *x;
  JaclJob *j = job_take(s, &x);
  if (!j) return chan_err("emit: the job has ended");
  static uint8_t value[JACL_TRANSCRIPT_VALUE], msg[JACL_TRANSCRIPT_VALUE];  /* under the turn */
  JaclVal err = JACL_NIL;
  uint64_t n = jv_encode_into(v, value, sizeof value, &err);
  if (!n) return transcript_done(x, err);
  const unir_schema *schema = k >= 0 ? jacl_schemas[k] : jv_schema();
  const uint8_t *frame = value;
  if (k >= 0) {
    int64_t m = unir_value_message(jv_schema(), value, n, schema, 0, msg, sizeof msg);
    if (m < 0) return transcript_done(x, transcript_err("emit", m));
    if ((uint64_t)m > sizeof msg) return transcript_done(x, chan_err("emit: the value is too large"));
    frame = msg;
    n = (uint64_t)m;
  }
  /* The schema is the block's agreement, so values of one schema share a block. */
  int64_t r = job_block(x, j, JOB_VALUES, schema, schema, 0);
  if (r >= 0) r = unir_transcript_push(x->t, (uint64_t)j->block, frame, n);
  if (r >= 0 && k >= 0 && k == jacl_link_slot) transcript_ref(x, j->block, v);
  return transcript_done(x, r < 0 ? transcript_err("emit", r) : JACL_NIL);
}

/* [emit V S]: in a job, V as a value of schema S (see job_emit); elsewhere, an error. */
JaclVal jacl_emit_as(JaclVal v, JaclVal schema) {
  int32_t k = jaclrt_is_i32(schema) ? jaclrt_as_i32(schema) : -1;
  if (k < 0 || (uint32_t)k >= JACL_SCHEMAS || !jacl_schemas[k]) return chan_err("emit: not a schema");
  JaclVal job;
  if (!job_named(&job)) return chan_err("emit: a value of a schema goes only to a job's output");
  return job_emit(job, v, k);
}

/* Ends job `s`'s adopted block, as the pipeline whose output it holds ends: the next pipeline's
 * output, even of the same schema, is a block of its own. */
static void job_pipeline_done(JaclVal s) {
  JaclTranscript *x;
  JaclJob *j = job_take(s, &x);
  if (!j) return;
  if (j->block >= 0 && j->kind == JOB_ADOPTED) {
    (void)unir_transcript_end(x->t, (uint64_t)j->block, UNIR_ENDED_COMPLETE);
    j->block = -1;
    j->agree = 0;
  }
  (void)transcript_done(x, JACL_NIL);
}

/* [transcript-job T CMD JOB] */
JaclVal jacl_transcript_job(JaclVal t, JaclVal command, JaclVal job) {
  if (!transcript_of(t)) return chan_err("transcript-job: not an open transcript");
  if (!jaclrt_is_string(command) || jacl_str_len(command) >= sizeof jacl_jobs[0].cmd ||
      !jaclrt_is_i32(job) || jaclrt_as_i32(job) < 0)
    return chan_err("transcript-job: a command and a job number");
  for (uint32_t i = 0; i < JACL_JOBS; i++) {
    JaclJob *j = &jacl_jobs[i];
    if (__vm_atomic_cas32(&j->used, 0, 1) != 0) continue;
    j->t = t;
    j->job = (uint32_t)jaclrt_as_i32(job);
    j->block = -1;
    j->kind = 0;
    j->agree = 0;
    j->entry = 0;
    j->blocks = 0;
    j->cmd_len = (uint32_t)transcript_str(command, j->cmd, sizeof j->cmd);
    return jaclrt_i32((int32_t)(j->gen * JACL_JOBS + i));
  }
  return chan_err("transcript-job: too many jobs");
}

/* [transcript-job-end S COMPLETE] */
JaclVal jacl_transcript_job_end(JaclVal s, JaclVal complete) {
  JaclTranscript *x;
  JaclJob *j = job_take(s, &x);
  if (!j) return chan_err("transcript-job-end: not a job");
  int64_t r = 0;
  if (j->block >= 0)
    r = unir_transcript_end(x->t, (uint64_t)j->block,
                            complete == JACL_FALSE || jaclrt_is_nil(complete) ? UNIR_ENDED_CANCELLED
                                                                             : UNIR_ENDED_COMPLETE);
  uint32_t n = j->blocks;
  j->block = -1;
  /* Past 2^31 / JACL_JOBS generations a handle would wrap; a slot is freed far less often. */
  j->gen = (j->gen + 1) % (0x7fffffffu / JACL_JOBS);
  __vm_atomic_store32(&j->used, 0);
  return transcript_done(x, r < 0 ? transcript_err("transcript-job-end", r) : jaclrt_i32((int32_t)n));
}

/* ---- Handlers and the legacy adapter (unir decisions 105 and 106; theSherwood/unir#172) ----
 * What a shell starts beside its transcript, each a child vat from an image it was granted (by
 * the capability's name), with a window of the image's declared memory and FUEL and MEMORY of
 * this vat's budget:
 *
 *   [handler-open IMAGE ARGS TEXT FUEL MEMORY]
 *       a handler for a file: IMAGE run with ARGS (a string's bytes), granted the surface the
 *       session set aside for handlers and sent TEXT as the file, which it shows until a person
 *       closes it. Its child, or an error. The program ends after its handlers.
 *   [legacy-job IMAGE ARGS ROWS COLS FUEL MEMORY]
 *       in a job: the legacy adapter, IMAGE run with ARGS, running a POSIX program in a terminal
 *       of the job's size in a pane of ROWS by COLS. Each screen it draws replaces the last in
 *       the job's live grid block, and the pane's events the shell gives the job (`$ctx.in`, an
 *       atom of a vector of them, which this empties) go to the program, until it ends. The
 *       adapter's exit status, or an error. Cancelled, it closes the adapter first. */
#define JACL_HANDLERS 16u
#define JACL_SCREEN (128u << 10)   /* the largest screen, a message of the grid's */
static int64_t jacl_handlers[JACL_HANDLERS];
static int32_t jacl_nhandlers;   /* slots claimed (cas32) */

/* The capability `name` names, taking the vat (vat_give gives it back); with `*module` set,
 * the vat, or 0 if it holds none by that name (and it is given back). */
static unir_vat *granted(JaclVal name, int64_t *module) {
  char nb[128];
  int64_t n = transcript_str(name, nb, sizeof nb);
  unir_vat *vat = vat_take();
  *module = vat && n >= 0 ? unir_endowed(vat, nb, (uint64_t)n) : UNIR_ENOCAP;
  if (*module >= 0) return vat;
  vat_give();
  return 0;
}

/* [handler-open IMAGE ARGS TEXT FUEL MEMORY] */
JaclVal jacl_handler_open(JaclVal image, JaclVal args, JaclVal text, JaclVal fuel, JaclVal memory) {
  int64_t f, m;
  if (!jaclrt_is_string(args) || !jaclrt_is_string(text) || !jacl_int_val_checked(fuel, &f) ||
      !jacl_int_val_checked(memory, &m) || f < 0 || m < 0)
    return chan_err("handler-open: an image, its arguments and the file as strings, and a budget");
  int32_t slot = -1;
  for (int32_t n = __vm_atomic_load32(&jacl_nhandlers); slot < 0 && n < (int32_t)JACL_HANDLERS;
       n = __vm_atomic_load32(&jacl_nhandlers))
    if (__vm_atomic_cas32(&jacl_nhandlers, n, n + 1) == n) slot = n;
  if (slot < 0) return chan_err("handler-open: too many handlers");
  jacl_handlers[slot] = -1;
  uint32_t al = jacl_str_len(args), tl = jacl_str_len(text);
  int32_t cap = (int32_t)(al + tl + 2);
  JaclVal keep[1] = {fb_alloc_nd(1, 1, &cap)};  /* may collect; `keep` is on the data stack */
  uint8_t *b = (uint8_t *)fb_data(keep[0]);
  jacl_str_bytes(args, (char *)b, al + 1);
  jacl_str_bytes(text, (char *)b + al + 1, tl + 1);
  int64_t module;
  unir_vat *vat = granted(image, &module);
  if (!vat) return chan_err("handler-open: no image granted by that name");
  /* It holds the vat until the file is sent: the handler reads it as it starts. */
  int64_t h = unir_handler_open(vat, module, 0, b, al, b + al + 1, tl, JACL_SERVICE_FRAMES,
                                JACL_SERVICE_SLOT, (uint64_t)f, (uint64_t)m);
  vat_give();
  if (h < 0) return chan_err("handler-open: the handler did not start");
  jacl_handlers[slot] = h;
  return jacl_int_result(h);
}

/* Sends the legacy adapter `l` the events waiting in `in` (an atom of a vector of them, or nil),
 * taking them all at once, as the shell adds to it. */
static void legacy_events(unir_legacy *l, JaclVal in) {
  if (jaclrt_is_nil(in)) return;
  JaclVal keep[2] = {JACL_NIL, jacl_vec_empty()};   /* the events taken stay reachable */
  for (;;) {
    keep[0] = jacl_box_get(in);
    if (jaclrt_type_index(keep[0]) != 0x06 || jacl_vec_count(keep[0]) == 0) return;
    if (jacl_box_cas(in, keep[0], keep[1]) == JACL_TRUE) break;
  }
  for (uint32_t i = 0; i < jacl_vec_count(keep[0]); i++) {
    uint8_t frame[512];
    JaclVal err = JACL_NIL;
    uint64_t n = jv_encode_into(jacl_vec_get(keep[0], i), frame, sizeof frame, &err);
    /* Keys and sizes go; any other event is the shell's, and the adapter refuses it. */
    if (!n) continue;
    unir_vat *vat = vat_take();
    (void)unir_legacy_event(vat, l, jv_schema(), frame, n);
    vat_give();
  }
}

/* A screen, `frame[0..n)`, to job `s`'s live grid block, committed as it comes, so each is a
 * version a renderer draws. 0, or a UNIR_E* code. */
static int64_t job_screen(JaclVal s, const unir_schema *grid, const uint8_t *frame, uint64_t n) {
  JaclTranscript *x;
  JaclJob *j = job_take(s, &x);
  if (!j) return UNIR_EENDED;
  int64_t r = job_block(x, j, JOB_VALUES, grid, grid, 0);
  if (r >= 0) r = unir_transcript_push(x->t, (uint64_t)j->block, frame, n);
  if (r >= 0) r = transcript_commit(x);
  (void)transcript_done(x, JACL_NIL);
  return r < 0 ? r : 0;
}

/* [legacy-job IMAGE ARGS ROWS COLS FUEL MEMORY] */
JaclVal jacl_legacy_job(JaclVal image, JaclVal args, JaclVal rows, JaclVal cols, JaclVal fuel,
                        JaclVal memory) {
  int64_t r, c, f, m;
  if (!jaclrt_is_string(args) || !jacl_int_val_checked(rows, &r) || !jacl_int_val_checked(cols, &c) ||
      !jacl_int_val_checked(fuel, &f) || !jacl_int_val_checked(memory, &m) || r < 1 || c < 1 ||
      r > 0xffff || c > 0xffff || f < 0 || m < 0)
    return chan_err("legacy-job: an image, its arguments, a pane's rows and columns, and a budget");
  JaclVal s;
  if (!job_named(&s)) return chan_err("legacy-job: only in a job");
  JaclVal in = jacl_map_get(jacl_ctx_cur, jacl_str_new("in", 2));
  static unir_schema *grid;
  if (!grid) grid = unir_schema_grid();
  char ab[256];
  int64_t al = transcript_str(args, ab, sizeof ab), module;
  int32_t cap = (int32_t)JACL_SCREEN;
  JaclVal keep[2] = {in, fb_alloc_nd(1, 1, &cap)};  /* may collect; `keep` is on the data stack */
  uint8_t *screen = (uint8_t *)fb_data(keep[1]);
  unir_vat *vat = al >= 0 && grid ? granted(image, &module) : 0;
  if (!vat) return chan_err("legacy-job: no image granted by that name");
  /* It holds the vat until the adapter has connected. */
  int64_t err = 0;
  unir_legacy *l = unir_legacy_open(vat, module, 0, (const uint8_t *)ab, (uint64_t)al, (uint32_t)r,
                                    (uint32_t)c, JACL_SERVICE_FRAMES, JACL_SERVICE_SLOT,
                                    (uint64_t)f, (uint64_t)m, &err);
  vat_give();
  if (!l) return chan_err("legacy-job: the adapter did not start");
  int64_t failed = 0;
  for (;;) {
    int ctl = jacl_ctl_pending();
    if (ctl & JACL_CTL_CANCEL) {
      vat = vat_take();
      (void)unir_legacy_close(vat, l);
      vat_give();
      jacl_ctl_point();   /* does not return */
    }
    if (ctl) jacl_ctl_point();   /* stopped, until resumed */
    legacy_events(l, keep[0]);
    /* The next screen, polled: a wait would hold the vat. */
    vat = vat_take();
    int64_t n = unir_legacy_next(vat, l, 0);
    vat_give();
    if (n == UNIR_EENDED) break;
    if (n == 0) vat_idle(1000000);
    if (n > 0) n = unir_legacy_screen(l, screen, JACL_SCREEN);
    if (n > 0) n = job_screen(s, grid, screen, (uint64_t)n);
    if (n < 0) {
      failed = n;
      break;
    }
  }
  vat = vat_take();
  int64_t st = unir_legacy_close(vat, l);
  vat_give();
  if (failed) return chan_err("legacy-job: the adapter failed");
  return jacl_int_result(st);
}

/* At exit, once the pane is closed: a handler ends when a person closes it, and this vat after
 * its handlers. */
static void jacl_handlers_join(void) {
  int32_t n = __vm_atomic_load32(&jacl_nhandlers);
  for (int32_t i = 0; i < n && i < (int32_t)JACL_HANDLERS; i++) {
    if (jacl_handlers[i] < 0) continue;
    unir_vat *vat = vat_take();
    (void)unir_join(vat, jacl_handlers[i]);
    vat_give();
  }
  __vm_atomic_store32(&jacl_nhandlers, 0);
}

/* At exit: close the transcripts still open. What was not flushed is lost, as a program's
 * unflushed output is. */
static void jacl_transcripts_close(void) {
  for (uint32_t i = 0; i < JACL_TRANSCRIPTS; i++) {
    JaclTranscript *x = &jacl_transcripts[i];
    if (!x->t) continue;
    unir_vat *vat = vat_take();
    unir_transcript_close(vat, x->t);
    vat_give();
    if (x->schema) unir_host_free(x->schema, x->cap, 1);
    for (uint32_t r = 0; r < x->nrefs; r++)
      if (x->refs[r].len) unir_host_free(x->refs[r].name, x->refs[r].len, 1);
    if (x->refs) unir_host_free(x->refs, (uint64_t)x->refcap * sizeof *x->refs, 8);
    *x = (JaclTranscript){0};
  }
}
