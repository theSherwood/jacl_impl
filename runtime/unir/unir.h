/* Unir vats and edges behind a C ABI (issues #18, #19). A C program llvm-links the
 * `unir-cabi` unit (unir_cabi.ll) before translation.
 *
 * Returns: a non-negative value on success, else one of UNIR_E*. After UNIR_EENDED, the
 * end's `*_ended` says how it ended. */
#ifndef UNIR_H
#define UNIR_H

#include <stdint.h>

typedef struct unir_vat unir_vat;
typedef struct unir_producer unir_producer;
typedef struct unir_consumer unir_consumer;
typedef struct unir_schema unir_schema;
typedef struct unir_agreement unir_agreement;
typedef struct unir_service unir_service;
typedef struct unir_transcript unir_transcript;
typedef struct unir_legacy unir_legacy;
typedef struct unir_notification unir_notification;
typedef struct unir_drawer unir_drawer;

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

/* `*_ended`: 0 while open; else bits 0-1 kind (1 complete, 2 cancelled, 3 severed), for a
 * sever bits 2-5 the cause (unir-wire Cause) and bit 6 the side (1 = consumer). */
#define UNIR_ENDED_COMPLETE 1u
#define UNIR_ENDED_CANCELLED 2u

/* The embedder defines these: the unit's heap (its handles and the vat's bookkeeping). A
 * translated library has no allocator of its own. unir_host_alloc returns NULL on failure. */
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
 * Budget). It starts with `args` (at most 16,224 bytes; see unir_vat_args), exactly
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
 * none. A session's root wires one for its shell (`shell.n`), which its pane's events signal
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
/* The legacy adapter as a job: `args` followed by the job's size for a pane `rows` by `cols`;
 * passed the program's terminal (`pty`) with `stdout`, and its bell (`pty.bell`, issue #259) if
 * this vat holds one. NULL with `*err` set if it could not start. unir_legacy_event: a pane
 * event as the runtime heard it, a value frame of `values` (the runtime's value schema):
 * {"keys": K} or {"geometry": {rows, cols}}; UNIR_EINVALID for any other. unir_legacy_next waits
 * up to `timeout_ns` (negative: as long as it takes) for the program's next screen and keeps it: its
 * length, 0 if none came, UNIR_EENDED once the program ended; unir_legacy_screen copies it out,
 * a message of unir_schema_grid's entry 0. unir_legacy_close ends the job and returns the
 * adapter's exit status. */
unir_legacy *unir_legacy_open(unir_vat *vat, int64_t module, uint32_t size_log2,
                              const uint8_t *args, uint64_t args_len, uint32_t rows,
                              uint32_t cols, uint32_t capacity, uint32_t slot_size, uint64_t fuel,
                              uint64_t memory, int64_t *err);
int64_t unir_legacy_event(unir_vat *vat, unir_legacy *l, const unir_schema *values,
                          const uint8_t *frame, uint64_t len);
int64_t unir_legacy_next(unir_vat *vat, unir_legacy *l, int64_t timeout_ns);
int64_t unir_legacy_screen(const unir_legacy *l, uint8_t *out, uint64_t cap);
int64_t unir_legacy_close(unir_vat *vat, unir_legacy *l);
/* The connections a renderer knows: references (`link.usc`; a block of them is drawn
 * underlined) and a legacy job's screens (`grid.usc`; latest-wins, so the block is live). Free
 * with unir_schema_free. */
unir_schema *unir_schema_link(void);
unir_schema *unir_schema_grid(void);

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
 * the focus; or the focus gained (focus 1) or lost. A key is a character, or a named key from
 * UNIR_KEY_ENTER on, with Ctrl (the lower-case byte) or Alt (a character) in `mods`. */
typedef struct unir_event {
  uint32_t kind; /* UNIR_EVENT_* */
  uint32_t rows, cols;
  uint32_t key, mods;
  uint32_t focus;
} unir_event;

#define UNIR_EVENT_SIZE 1u
#define UNIR_EVENT_KEY 2u
#define UNIR_EVENT_FOCUS 3u
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
 * its edges, and `name.post`, which they signal the compositor through. It offers the region's
 * schema and accepts it back. NULL with `*status` set if it could not connect. */
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
 * hears the end, keeping the last frame. */
void unir_drawer_close(unir_vat *vat, unir_drawer *d);
/* Writes UTF-8 `text` into row `row` of `cells`, `cols` a row, from column `col`, as a grid
 * writes text: a character a cell in `style` and colours `fg` on `bg`, a tab as spaces to the next
 * multiple of 8, any other control character in caret notation, cut at `cols`. Returns the column
 * after the last cell written, or UNIR_EINVALID if `text` is not UTF-8 or the style or a colour
 * is not one. */
int64_t unir_cells_write(unir_cell *cells, uint32_t cols, uint32_t row, uint32_t col,
                         const uint8_t *text, uint64_t len, uint32_t style, uint32_t fg,
                         uint32_t bg);

#endif
