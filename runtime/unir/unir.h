/* Unir vats and edges behind a C ABI (issues #18, #19). A C program llvm-links the
 * `unir-cabi` unit (unir_cabi.ll) before translation.
 *
 * Returns: a non-negative value on success, else one of UNIR_E*. After UNIR_EENDED, the
 * end's `*_ended` says how it ended. */
#ifndef UNIR_H
#define UNIR_H

#include <stdint.h>

/* A store in this vat, and its syncs with a home (issue #395): the unit holds unir_store.h's
 * functions too, the native static library's, and below are the edges its syncs travel over
 * here (`unir_home_*`). */
#include "unir_store.h"

typedef struct unir_vat unir_vat;
typedef struct unir_producer unir_producer;
typedef struct unir_consumer unir_consumer;
typedef struct unir_schema unir_schema;
typedef struct unir_agreement unir_agreement;
typedef struct unir_service unir_service;
typedef struct unir_transcript unir_transcript;
typedef struct unir_jobs unir_jobs;
typedef struct unir_job unir_job;
typedef struct unir_notification unir_notification;
typedef struct unir_drawer unir_drawer;
typedef struct unir_tui unir_tui;
typedef struct unir_home unir_home;

/* One named capability in a child's endowment. */
typedef struct unir_grant {
  const char *name;
  uint64_t name_len;
  int64_t cap;
} unir_grant;

#define UNIR_EENDED (-1)        /* the edge has ended; nothing more moves */
#define UNIR_ESTALLED (-2)      /* the peer rang nothing within the timeout; still open */
#define UNIR_ETOO_LARGE (-3)    /* payload over the geometry's maximum; edge unaffected */
#define UNIR_ETOO_SMALL (-4)    /* the next frame does not fit the buffer; it stays unread */
#define UNIR_EGEOMETRY (-5)     /* invalid geometry, or a region too small for it */
#define UNIR_ESUBSTRATE (-6)    /* a vat operation failed */
#define UNIR_EINVALID (-7)      /* a malformed argument */
#define UNIR_ENOCAP (-8)        /* the vat was endowed with nothing under that name */
#define UNIR_EREFUSED (-9)      /* the consumer refused the offer; it cancels the edge */
#define UNIR_EUNTYPED (-10)     /* no offer: the edge's first frame is not on substream 0 */
#define UNIR_EMALFORMED (-11)   /* not a canonical message of that type */
#define UNIR_EMISMATCH (-12)    /* a value that does not fit the type it is sent as */
#define UNIR_ELOST (-13)        /* a job's region was lost with its compositor: it was killed */

/* `*_ended`: 0 while open; else bits 0-1 kind (1 complete, 2 cancelled, 3 severed), for a
 * sever bits 2-5 the cause (unir-wire Cause) and bit 6 the side (1 = consumer). */
#define UNIR_ENDED_COMPLETE 1u
#define UNIR_ENDED_CANCELLED 2u

/* The embedder defines these: the unit's heap (its handles and the vat's bookkeeping). A
 * translated library has no allocator of its own. unir_host_alloc returns NULL on failure.
 * The unit asks for at most 16-byte alignment; it serves a larger one itself, from a larger
 * allocation. */
void *unir_host_alloc(uint64_t size, uint64_t align);
void unir_host_free(void *ptr, uint64_t size, uint64_t align);

/* The root vat. Regions map in [map_base, map_end), which must be free window space. */
unir_vat *unir_vat_root(uint64_t map_base, uint64_t map_end);

/* A spawned child's vat, holding the arguments its parent passed (NULL if they are
 * malformed). Only a child may call it: a root's argument area holds the host's argv. */
unir_vat *unir_vat_child(uint64_t map_base, uint64_t map_end);

/* Copies the vat's arguments into `buf`; returns their length, or UNIR_ETOO_SMALL. */
int64_t unir_vat_args(const unir_vat *vat, uint8_t *buf, uint64_t cap);

/* The capability the vat was endowed with under `name`, or UNIR_ENOCAP. */
int64_t unir_endowed(const unir_vat *vat, const char *name, uint64_t name_len);

/* A fresh zeroed region of `len` bytes; returns its capability. */
int64_t unir_region_create(unir_vat *vat, uint64_t len);

/* Spawns a child running `module` (a module capability whose export 0 is a child entry) in
 * a window of its own, 2^size_log2 bytes (the module's declared memory, paid from the vat's
 * Budget). It starts with `args` (at most 16,208 bytes; see unir_vat_args), exactly
 * `grants`, and `fuel`. Returns the child's capability. */
int64_t unir_spawn(unir_vat *vat, int64_t module, uint32_t size_log2, const uint8_t *args,
                   uint64_t args_len, const unir_grant *grants, uint32_t grants_n,
                   uint64_t fuel);

/* Waits for a child and returns its status; a negative status reads as an error code. A child
 * that ran out of fuel, trapped or was killed joins as UNIR_ESUBSTRATE. */
int64_t unir_join(unir_vat *vat, int64_t child);

/* Ends a child and every vat it spawned, running or blocked; one that has ended is unaffected.
 * Sever its edges with `cancelled` first, since peers learn only through their edges (unir §10),
 * then join it. Returns 0. */
int64_t unir_kill(unir_vat *vat, int64_t child);

/* The region length an edge of this geometry needs; UNIR_EGEOMETRY if invalid. */
int64_t unir_edge_len(uint32_t capacity, uint32_t slot_size);

/* The largest payload a frame of this geometry carries. */
int64_t unir_edge_max_payload(uint32_t capacity, uint32_t slot_size);

/* Maps the edge region `cap` and attaches as its producer (NULL on failure). */
unir_producer *unir_producer_open(unir_vat *vat, int64_t cap, uint32_t capacity,
                                  uint32_t slot_size);

/* End operations take no vat: they wait on and notify only their own edge's words, so
 * different ends may be used concurrently, e.g. by fibers parked in them. One end, one
 * operation at a time. A call that takes the vat reads and grows its tables, so a vat is one
 * such call at a time: a runtime whose fibers run on several threads serializes them. */

/* Writes one frame, parking while there is no credit; a negative timeout never stalls. */
int64_t unir_producer_write(unir_producer *p, uint16_t substream, const uint8_t *buf,
                            uint64_t len, int64_t timeout_ns);
int64_t unir_producer_complete(unir_producer *p);
int64_t unir_producer_sever(unir_producer *p, uint32_t cause);
uint32_t unir_producer_ended(const unir_producer *p);
/* Observes the consumer without writing and returns `unir_producer_ended`: how a producer with
 * nothing to write learns that its reader cancelled or severed (job control). */
uint32_t unir_producer_poll(unir_producer *p);
void unir_producer_free(unir_producer *p);

/* Maps the edge region `cap`, attaches as its consumer, and grants the first credit. */
unir_consumer *unir_consumer_open(unir_vat *vat, int64_t cap, uint32_t capacity,
                                  uint32_t slot_size);
/* Reads the next frame into `buf`, parking while there is none; returns its length and,
 * if `substream` is not NULL, stores its substream. */
int64_t unir_consumer_read(unir_consumer *c, uint8_t *buf, uint64_t cap, int64_t timeout_ns,
                           uint16_t *substream);
/* Waits for the next frame and returns its substream without consuming it (a typed edge's
 * offer is on substream 0; see unir_consumer_accept). */
int64_t unir_consumer_next_substream(unir_consumer *c, int64_t timeout_ns);
int64_t unir_consumer_cancel(unir_consumer *c);
/* Grants no credit past what this end has read, so the producer parks once the ring is
 * full (job control's suspend, by backpressure); resume lifts that. */
int64_t unir_consumer_suspend(unir_consumer *c);
int64_t unir_consumer_resume(unir_consumer *c);
int64_t unir_consumer_sever(unir_consumer *c, uint32_t cause);
uint32_t unir_consumer_ended(const unir_consumer *c);
void unir_consumer_free(unir_consumer *c);

/* Typed edges (unir spec §3, issues #41, #43). A schema is a connection schema's canonical
 * encoding, as unir-schemac emits it (`<name>_schema`); NULL if malformed or invalid. */
unir_schema *unir_schema_new(const uint8_t *bytes, uint64_t len);
void unir_schema_free(unir_schema *s);
/* 0 if `frame` is the canonical encoding of a message of catalog entry `entry`, else
 * UNIR_EMALFORMED (UNIR_EINVALID: no such entry). Verify only memory no peer can write. */
int64_t unir_schema_verify(const unir_schema *s, uint32_t entry, const uint8_t *frame,
                           uint64_t len);

/* Offers `s` as the edge's first traffic (a Hello, then the schema) and waits for the
 * consumer's answer: 0 once accepted, then entry i's messages go on substream i + 1;
 * UNIR_EREFUSED; or an edge error. */
int64_t unir_producer_offer(unir_producer *p, const unir_schema *s, int64_t timeout_ns);

/* Reads the producer's offer and answers it: accepts a writer schema that `reader` succeeds
 * or equals, else refuses (and cancels). Stores 0 or an error in `*status`. UNIR_EUNTYPED:
 * the edge carries no offer (a bytes edge), and its first frame stays unread. A malformed
 * offer severs the edge with malformed-frame (UNIR_EENDED). */
unir_agreement *unir_consumer_accept(unir_consumer *c, const unir_schema *reader,
                                     int64_t timeout_ns, int64_t *status);
/* Receives the next message on `c`: copied into the agreement's private 8-aligned buffer,
 * verified, and materialized into the reader's shape under version skew. Returns its length,
 * with its catalog entry in `*entry` and the frame in `*frame` (valid until the next call on
 * `a`). A frame that does not verify severs the edge with malformed-frame (UNIR_EENDED). */
int64_t unir_agreement_recv(unir_agreement *a, unir_consumer *c, int64_t timeout_ns,
                            uint32_t *entry, const uint8_t **frame);
void unir_agreement_free(unir_agreement *a);
/* Reads the producer's offer and agrees to it whatever its schema (issue #160): a reader that
 * knows none in advance, such as a shell forwarding a job's output. As unir_consumer_accept
 * otherwise. */
unir_agreement *unir_consumer_adopt(unir_consumer *c, int64_t timeout_ns, int64_t *status);
/* A copy of the schema `a` reads messages as (the writer's, once adopted); unir_schema_free it. */
unir_schema *unir_agreement_schema(const unir_agreement *a);

/* Notifications (unir spec §16, decision 56; issue #208). unir_notification_open: the
 * notification the vat was endowed with under `name`, mapped, for the vat's life; NULL if there is
 * none. A session's activator gives its shell one (`n`), which its pane's events signal
 * while the pane is armed (unir_service_arm). unir_notification_wait takes the badges signalled
 * since the last wait into `*badges` (if not NULL), waiting up to `timeout_ns` (negative: as long
 * as it takes) while there are none: 0 if none came, a timeout or a spurious wakeup, so a waiter
 * retries what it waits for either way. It returns 0 or UNIR_ESUBSTRATE. A wait takes every
 * badge, so a notification has one waiter at a time. unir_notification_signal signals `badge`
 * (nonzero, else UNIR_EINVALID), waking the waiter: how a vat's own tasks wake the one waiting.
 * Neither takes the vat, as an end's operations do not: a runtime waits while its tasks use it. */
unir_notification *unir_notification_open(unir_vat *vat, const char *name, uint64_t name_len);
int64_t unir_notification_wait(unir_notification *n, int64_t timeout_ns, uint64_t *badges);
int64_t unir_notification_signal(unir_notification *n, uint64_t badge);

/* Services (unir spec §13, issue #50): a session with a service vat granted as `name`, i.e.
 * `name.requests`, `name.replies` and `name.post`; granted without `name.requests`, a
 * subscription, which hears what the service writes and sends nothing (decision #282.5). The
 * service offers its schema first; this end adopts it. Values cross as the runtime's value
 * frames, `values` being the runtime's value schema (a connection schema whose entry 0 is its
 * value type), converted to and from the service's messages by structure. Stores 0 or an error
 * in `*status`. */
unir_service *unir_service_open(unir_vat *vat, const uint8_t *name, uint64_t name_len,
                                uint32_t capacity, uint32_t slot_size, const uint8_t *values,
                                uint64_t values_len, int64_t *status);
/* Sends `request` (a value frame) and waits for its reply: returns the reply's length as a
 * value frame, which unir_service_result copies out. UNIR_EMISMATCH: the request does not fit
 * the service's request type (nothing was sent); UNIR_EINVALID from a subscription. Events
 * that arrive meanwhile are kept. */
int64_t unir_service_call(unir_vat *vat, unir_service *s, const uint8_t *request, uint64_t len);
/* Sends `message`, a value frame `{name: value}`, on the catalog entry named `name`, waiting
 * for nothing: a connection that is not request and reply (a pane's `show`). 0, or
 * UNIR_EMISMATCH if it is not one entry's name and a value of its type (nothing was sent), or
 * UNIR_EINVALID from a subscription. */
int64_t unir_service_send(unir_vat *vat, unir_service *s, const uint8_t *message, uint64_t len);
/* The next event, `{name: value}`: kept, or the next to arrive, waiting for it when `wait`;
 * its length as a value frame, 0 if there is none, or an error. A session that never called
 * hears entry 1 as an event too. */
int64_t unir_service_event(unir_vat *vat, unir_service *s, uint32_t wait);
/* Arms `s` for a wait on a notification: unless an event is kept or has arrived, the service's
 * end signals the notification it was wired to on its next event. 1 if it armed, 0 if an event is
 * ready (then do not wait), or an error. unir_service_disarm it before its next use, whatever the
 * wait returned. */
int64_t unir_service_arm(unir_vat *vat, unir_service *s);
int64_t unir_service_disarm(unir_vat *vat, unir_service *s);
/* Copies the last result out; its length, or UNIR_ETOO_SMALL. */
int64_t unir_service_result(const unir_service *s, uint8_t *out, uint64_t cap);
void unir_service_close(unir_vat *vat, unir_service *s);
/* The schema of the service's entry named `name` alone: the service's types, with that entry as
 * entry 0, so a shell can show what the entry carries as a transcript block typed as the service
 * sent it (decision #282.5). NULL if there is no such entry; else unir_schema_free it. */
unir_schema *unir_service_schema(const unir_service *s, const uint8_t *name, uint64_t len);

/* A runtime's values as schemas and messages (issue #160), converted by structure as a
 * service's are; `values` is the runtime's value schema. unir_schema_from_value: the
 * connection schema the value frame `frame` describes, as the meta-schema's ConnectionSchema
 * (`types`, each a Type variant such as "text" or {"struct": {name, fields}}, and `substreams`,
 * each {name, type, profile, class}); NULL if it is not a valid one. unir_value_message: the
 * value as a message of `schema`'s entry `entry`, copied to `out` if it fits `cap`; returns its
 * length either way, UNIR_EMISMATCH if it does not fit the type, or UNIR_EINVALID. */
unir_schema *unir_schema_from_value(const unir_schema *values, const uint8_t *frame,
                                    uint64_t len);
int64_t unir_value_message(const unir_schema *values, const uint8_t *frame, uint64_t len,
                           const unir_schema *schema, uint32_t entry, uint8_t *out,
                           uint64_t cap);

/* The transcript (unir spec §15, decisions 100 and 107; issue #160): a writer of the
 * transcript at ref `transcript`, with its typed line at ref `line`, committing through a
 * session with the store vat granted as `name` (its requests' ring, then its replies'). Stores
 * 0 or an error in `*status`. Blocks are named by their index, which unir_transcript_begin
 * and unir_transcript_begin_output return; changes are queued until unir_transcript_flush
 * commits them as one version, but the line is committed at once. */
unir_transcript *unir_transcript_open(unir_vat *vat, const uint8_t *name, uint64_t name_len,
                                      uint32_t requests_capacity, uint32_t requests_slot_size,
                                      uint32_t replies_capacity, uint32_t replies_slot_size,
                                      const uint8_t *transcript, uint64_t transcript_len,
                                      const uint8_t *line, uint64_t line_len, int64_t *status);
int64_t unir_transcript_line(unir_vat *vat, unir_transcript *t, const uint8_t *text,
                             uint64_t len);
/* A block of job `job`'s values, messages of `schema`'s entry `entry`; live (each value
 * replacing the last) if the entry is latest-wins. */
int64_t unir_transcript_begin(unir_transcript *t, const uint8_t *command, uint64_t len,
                              uint64_t job, const unir_schema *schema, uint32_t entry);
/* A block of job `job`'s output: text in a document of its own (decision 107). */
int64_t unir_transcript_begin_output(unir_transcript *t, const uint8_t *command, uint64_t len,
                                     uint64_t job);
/* A message of the block's schema (verified; UNIR_EINVALID if it is not one). */
int64_t unir_transcript_push(unir_transcript *t, uint64_t block, const uint8_t *frame,
                             uint64_t len);
int64_t unir_transcript_write(unir_transcript *t, uint64_t block, const uint8_t *bytes,
                              uint64_t len);
/* Ends the block as its job's output edge ended: `ended` is that edge's `*_ended` word. */
int64_t unir_transcript_end(unir_transcript *t, uint64_t block, uint32_t ended);
int64_t unir_transcript_flush(unir_vat *vat, unir_transcript *t);
void unir_transcript_close(unir_vat *vat, unir_transcript *t);

/* A shell's session beside its transcript (issue #172; decisions 105, 106). Each spawns the image
 * granted as `module` (from unir_endowed), with a window of 2^size_log2 bytes (its declared
 * memory), `args`, and `fuel` and `memory`; an edge to it has `capacity` slots of `slot_size`
 * bytes. unir_handler_open: a handler for a file, granted the file's edge as `file` and passed
 * the surface the session set aside for handlers (`handler.show`, `handler.draw`,
 * `handler.post`) with `stdout`, and sent `text` (UTF-8) as the file; returns the handler, or an
 * error. */
int64_t unir_handler_open(unir_vat *vat, int64_t module, uint32_t size_log2, const uint8_t *args,
                          uint64_t args_len, const uint8_t *text, uint64_t text_len,
                          uint32_t capacity, uint32_t slot_size, uint64_t fuel, uint64_t memory);
/* The connections a renderer knows: references (`link.usc`; a block of them is drawn
 * underlined) and a full-screen job's screen (`screen.usc`; latest-wins, so the block is live).
 * Free with unir_schema_free. */
unir_schema *unir_schema_link(void);
unir_schema *unir_schema_grid(void);

/* Full-screen jobs (unir spec §12.3, §15; decisions #236.5, #236.7, #236.9). A shell asks its
 * activator for a job's region on the jobs session its System grants it: unir_jobs_open opens
 * the one its door names `name` (`name.requests`, `name.replies`); NULL with `*status` set,
 * UNIR_ENOCAP if there is none.
 *
 * unir_job_open asks for job `id`'s region, waiting up to `timeout_ns` for the answer
 * (UNIR_EREFUSED: an id already open, or a peer that does not run; UNIR_ESTALLED: no answer),
 * then spawns the image granted as `module`, with a window of 2^size_log2 bytes, `args`, `fuel`
 * and `memory`, granted the region as `region` (what unir_drawer_open opens: `region.lines`,
 * `region.input`, `region.post`, and its notification `region.n`), the edge it reports its last
 * screen on (`region.screen`, `region.wake`), and `grants` besides; the shell keeps none of the
 * region. The compositor shows the region over its surface while the job draws, and sends it
 * the keys typed. The legacy adapter is such a job, granted the program's terminal (`pty`, and
 * `pty.bell`, issue #259) and `stdout`.
 *
 * unir_job_next takes what came, waiting up to `timeout_ns` (negative: as long as it takes) when
 * nothing did: the length of the screen the job reported, kept for unir_job_screen (a message of
 * unir_schema_grid's entry 0, the job's block's value); 0 if none came; UNIR_EENDED once the job
 * is done; UNIR_ELOST if its region was lost, or another error, the job killed either way.
 * unir_job_arm arms the job's report and the jobs session for a wait on the shell's
 * notification (unir_notification_wait, which takes no vat, issue #208): 1 if both armed, 0 if
 * something is there to take (then do not wait); unir_job_disarm them after the wait. A job that
 * ends without reporting signals nothing, so wait with a timeout. unir_job_kill kills it, as
 * when the pane closes under it. unir_job_close closes its region with the activator and joins
 * it: its exit status, or UNIR_ESUBSTRATE if it was killed. */
unir_jobs *unir_jobs_open(unir_vat *vat, const char *name, uint64_t name_len, int64_t *status);
void unir_jobs_close(unir_vat *vat, unir_jobs *jobs);
unir_job *unir_job_open(unir_vat *vat, unir_jobs *jobs, uint32_t id, int64_t module,
                        uint32_t size_log2, const uint8_t *args, uint64_t args_len,
                        const unir_grant *grants, uint64_t grants_n, uint64_t fuel,
                        uint64_t memory, int64_t timeout_ns, int64_t *status);
int64_t unir_job_next(unir_vat *vat, unir_jobs *jobs, unir_job *job, int64_t timeout_ns);
int64_t unir_job_arm(unir_vat *vat, unir_jobs *jobs, unir_job *job);
int64_t unir_job_disarm(unir_vat *vat, unir_jobs *jobs, unir_job *job);
int64_t unir_job_screen(const unir_job *job, uint8_t *out, uint64_t cap);
int64_t unir_job_kill(unir_vat *vat, unir_job *job);
int64_t unir_job_close(unir_vat *vat, unir_jobs *jobs, unir_job *job);

/* Regions (unir spec §15; decisions #236.1 to #236.3; issue #236): a rectangle of a screen a
 * program draws in, which a compositor composes. A program draws a frame as an array of cells in
 * its own memory, row by row, and presents it; only the lines that changed go, and the
 * compositor shows a frame once all of it has come.
 *
 * A cell: a character, its colours and its style. A zeroed cell is blank: a space, plain, in the
 * theme's colours. A control character shows as U+FFFD; a cell whose character is not a Unicode
 * scalar, whose style has another bit, or whose colour is not one, is UNIR_EINVALID. */
typedef struct unir_cell {
  uint32_t c;     /* a Unicode scalar; 0 is a space */
  uint32_t fg;    /* its colour: UNIR_COLOR_* */
  uint32_t bg;    /* the colour behind it */
  uint32_t style; /* UNIR_STYLE_* bits */
} unir_cell;

#define UNIR_COLOR_DEFAULT 0u                   /* the theme's text, or its background */
#define UNIR_COLOR_SLOT(n) (0x01000000u | (n))  /* a theme slot, n < 256; 0-15 the ANSI colours */
#define UNIR_COLOR_RGB(r, g, b) \
  (0x02000000u | (uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))
#define UNIR_STYLE_BOLD 1u
#define UNIR_STYLE_FAINT 2u
#define UNIR_STYLE_UNDERLINE 4u
#define UNIR_STYLE_REVERSE 8u

/* What a program hears: the region's size (rows, cols); a key (key, mods) while the region has
 * the focus; the focus gained (focus 1) or lost; or the pointer over the region (issue #284): its
 * cell (row, col, from the region's top left) and the buttons held (bit 0 the primary, which a
 * touch presses; 1 the middle; 2 the secondary). A key is a character, or a named key from
 * UNIR_KEY_ENTER on, with Ctrl (the lower-case byte) or Alt (a character) in `mods`. */
typedef struct unir_event {
  uint32_t kind; /* UNIR_EVENT_* */
  uint32_t rows, cols;
  uint32_t key, mods;
  uint32_t focus;
  uint32_t row, col, buttons;
} unir_event;

#define UNIR_EVENT_SIZE 1u
#define UNIR_EVENT_KEY 2u
#define UNIR_EVENT_FOCUS 3u
#define UNIR_EVENT_POINTER 4u
#define UNIR_KEY_ENTER 0x110000u
#define UNIR_KEY_TAB 0x110001u
#define UNIR_KEY_BACKSPACE 0x110002u
#define UNIR_KEY_DELETE 0x110003u
#define UNIR_KEY_ESCAPE 0x110004u
#define UNIR_KEY_UP 0x110005u
#define UNIR_KEY_DOWN 0x110006u
#define UNIR_KEY_RIGHT 0x110007u
#define UNIR_KEY_LEFT 0x110008u
#define UNIR_KEY_HOME 0x110009u
#define UNIR_KEY_END 0x11000au
#define UNIR_KEY_PAGE_UP 0x11000bu
#define UNIR_KEY_PAGE_DOWN 0x11000cu
#define UNIR_MOD_CTRL 1u
#define UNIR_MOD_ALT 2u

/* The program's end of the region it was endowed with as `name`: `name.lines` and `name.input`,
 * its edges, and `name.post`, which they signal the compositor through; and, a shell's job,
 * `name.screen`, which it reports its last frame on as it closes, and `name.wake`, which that
 * signals the shell through (decision #236.7). It offers the region's schema and accepts it
 * back. NULL with `*status` set if it could not connect. */
unir_drawer *unir_drawer_open(unir_vat *vat, const char *name, uint64_t name_len, int64_t *status);
/* Presents `rows` by `cols` cells as the region's next frame (at most 512 by 1024: the rest is
 * cut), the cursor at (cursor_row, cursor_col), hidden if either is negative or outside. It goes
 * once the frame going has, replacing any presented since; this sends what the ring has room
 * for and never waits. 0, UNIR_EINVALID for a cell that is not one (nothing is presented), or an
 * edge error. */
int64_t unir_drawer_present(unir_vat *vat, unir_drawer *d, const unir_cell *cells, uint32_t rows,
                            uint32_t cols, int32_t cursor_row, int32_t cursor_col);
/* Sends what waits to be sent, then takes the next event into `*e`: 1, 0 if none has come, or an
 * error (UNIR_EENDED once the compositor ended the region). */
int64_t unir_drawer_next(unir_vat *vat, unir_drawer *d, unir_event *e);
/* Arms the region for a wait on the program's notification (unir_notification_wait), which the
 * compositor's ends signal: 1 if it armed, 0 if an event, or room for a frame waiting to go, is
 * there (then do not wait). unir_drawer_disarm it after the wait, whatever the wait returned. */
int64_t unir_drawer_arm(unir_vat *vat, unir_drawer *d);
int64_t unir_drawer_disarm(unir_vat *vat, unir_drawer *d);
/* Ends the region: what was presented goes, waiting for room as it must, then the compositor
 * hears the end, keeping the last frame; and that frame is reported, if the region has a
 * report. */
void unir_drawer_close(unir_vat *vat, unir_drawer *d);
/* Writes UTF-8 `text` into row `row` of `cells`, `cols` a row, from column `col`, as a grid
 * writes text: a character a cell in `style` and colours `fg` on `bg`, a tab as spaces to the next
 * multiple of 8, any other control character in caret notation, cut at `cols`. Returns the column
 * after the last cell written, or UNIR_EINVALID if `text` is not UTF-8 or the style or a colour
 * is not one. */
int64_t unir_cells_write(unir_cell *cells, uint32_t cols, uint32_t row, uint32_t col,
                         const uint8_t *text, uint64_t len, uint32_t style, uint32_t fg,
                         uint32_t bg);

/* The TUI library (#237): a program's frames in immediate mode, presented on the region it holds
 * as a drawer (unir_drawer_open). Each round the program gives it what the drawer heard
 * (unir_drawer_next, then unir_tui_event), begins a frame (unir_tui_begin), fills it with cells,
 * text and widgets in rectangles it cuts (unir_tui_split), and presents it on the drawer
 * (unir_tui_present), which sends only the lines that changed. A widget's data stays the
 * program's (a list's items and selection, an input's bytes); the library keeps only what is
 * transient under the widget's id: a list's scroll, an input's cursor, and which widget has the
 * focus. Tab moves the focus to the next widget in the order the frame drew them, as the next
 * frame begins; the focused widget takes the keys it knows, and the rest are the program's
 * (unir_tui_key). A frame takes the keys up to the next Tab; that Tab and those after it wait for
 * the next frame (unir_tui_pending). Once running, a frame allocates nothing. */
typedef struct unir_rect {
  uint32_t x, y; /* its top left: column x of row y */
  uint32_t w, h;
} unir_rect;

/* A part of a cut: `n` cells (UNIR_PART_FIXED), `n / d` of the whole (UNIR_PART_RATIO), or at
 * least `n` cells and a share of what the others leave (UNIR_PART_MIN). */
typedef struct unir_part {
  uint32_t kind;
  uint32_t n, d;
} unir_part;

#define UNIR_PART_FIXED 0u
#define UNIR_PART_RATIO 1u
#define UNIR_PART_MIN 2u

/* A program's frames, before its region has told it a size. Never NULL; unir_tui_free it. */
unir_tui *unir_tui_new(void);
void unir_tui_free(unir_tui *t);
/* Takes `*e`, an event the program's drawer heard: the region's size, which the next frame is
 * drawn at, or a key, which waits for a frame. 0, or UNIR_EINVALID for an event that is not
 * one. */
int64_t unir_tui_event(unir_tui *t, const unir_event *e);
/* 1 if keys wait for a frame: draw one, whether or not anything else came. Else 0. */
int64_t unir_tui_pending(const unir_tui *t);
/* Begins a frame at the region's size, every cell blank; its area, the whole region, in
 * `*area`. */
void unir_tui_begin(unir_tui *t, unir_rect *area);
/* Cuts `*r` into `n` rows, top to bottom (`across` 0), or columns, left to right (`across` 1), by
 * `parts`, into `out`. 0, or UNIR_EINVALID for a part that is not one or more than 16. */
int64_t unir_tui_split(const unir_rect *r, uint32_t across, const unir_part *parts, uint32_t n,
                       unir_rect *out);
/* Sets cell `col` of row `row` to `*c`, or every cell of `*r`: nothing outside the frame. 0, or
 * UNIR_EINVALID for a cell that is not one. */
int64_t unir_tui_put(unir_tui *t, uint32_t row, uint32_t col, const unir_cell *c);
int64_t unir_tui_fill(unir_tui *t, const unir_rect *r, const unir_cell *c);
/* Writes UTF-8 `text` into `*r`, a line of it a row, each cut at the rectangle's width, as
 * unir_cells_write writes text. Returns the rows written, or UNIR_EINVALID if `text` is not
 * UTF-8 or the style or a colour is not one. */
int64_t unir_tui_text(unir_tui *t, const unir_rect *r, const uint8_t *text, uint64_t len,
                      uint32_t style, uint32_t fg, uint32_t bg);
/* A list of `n` items in `*r`, a row each, item `i` the UTF-8 `items[i]` of `lens[i]` bytes,
 * item `*selected` reversed with the focus and bold without it. With the focus it takes Up, Down,
 * Page Up, Page Down, Home and End, which move `*selected`, scrolling to keep it shown. Returns 1
 * if the selection moved, 0 if not, or UNIR_EINVALID if an item is not UTF-8. */
int64_t unir_tui_list(unir_tui *t, uint32_t id, const unir_rect *r, const uint8_t *const *items,
                      const uint64_t *lens, uint32_t n, uint32_t *selected);
/* A line of text in `*r`'s first row, underlined across it: the UTF-8 `buf[..*len]`, never past
 * `cap` bytes, edited in place. With the focus it takes printable characters, inserted at its
 * cursor while they fit, and Backspace, Delete, Left, Right, Home and End, and shows the cursor.
 * What is not UTF-8 is cut. Returns 1 if the text changed, else 0. */
int64_t unir_tui_input(unir_tui *t, uint32_t id, const unir_rect *r, uint8_t *buf, uint64_t cap,
                       uint64_t *len);
/* A button, `[label]` in `*r`'s first row, reversed with the focus, which takes Enter and Space.
 * Returns 1 if it was pressed, 0 if not, or UNIR_EINVALID if `label` is not UTF-8. */
int64_t unir_tui_button(unir_tui *t, uint32_t id, const unir_rect *r, const uint8_t *label,
                        uint64_t len);
/* The frame's next key no widget took, now the program's, into `*e`: 1, or 0 once none is left.
 * Ask after the widgets that may take keys. */
int64_t unir_tui_key(unir_tui *t, unir_event *e);
/* Ends the frame and presents it on the drawer `d`, as unir_drawer_present presents cells. 0, or
 * an edge error. */
int64_t unir_tui_present(unir_vat *vat, unir_tui *t, unir_drawer *d);

/* A replica's home (issue #395): a store's syncs (unir_store.h) carry their frames over a
 * session with a store vat, which a vat opens here. unir_home_open opens the session with the
 * store vat granted to `vat` as `name` (`name.requests`, `name.replies`, `name.post`; or the one
 * its door names `name`), each ring of `capacity` slots of `slot_size` bytes: NULL with
 * `*status` set. unir_home_pull and unir_home_push make a sync of `store` with the home, as
 * unir_sync_pull and unir_sync_push do, over the session as it connected (a store vat grown
 * past this end is spoken to as this end knows it); one sync on a home at a time.
 * unir_home_send sends the home every frame the sync made, in order: 0, or an edge error, which
 * the sync is told (unir_sync_ended). unir_home_take gives the sync every message the home sent,
 * waiting up to `timeout_ns` (negative: as long as it takes) for the first when none has come:
 * how many, 0 if none came; or an edge error, which the sync is told. unir_home_free ends the
 * session. */
unir_home *unir_home_open(unir_vat *vat, const uint8_t *name, uint64_t name_len,
                          uint32_t capacity, uint32_t slot_size, int64_t *status);
unir_sync *unir_home_pull(unir_home *h, unir_store *store, const char *name, const char *me,
                          const char *peer, int32_t moves, uint64_t lease);
unir_sync *unir_home_push(unir_home *h, unir_store *store, const char *name, uint64_t wait);
int64_t unir_home_send(unir_vat *vat, unir_home *h, unir_sync *sync);
int64_t unir_home_take(unir_vat *vat, unir_home *h, unir_sync *sync, int64_t timeout_ns);
void unir_home_free(unir_vat *vat, unir_home *h);

#endif
