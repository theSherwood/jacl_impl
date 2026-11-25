/* The store vat's request handling, in a program's own memory (issues #375, #395): a program
 * runs the real store a frame at a time, as unir's store vat answers them over its edges; or
 * holds it as a replica of what it edits, read in place and kept in step with its home by syncs
 * whose frames the program carries. One implementation (`unir-store-cabi`), built natively as
 * `unir-store-c`'s static library, and on temen into the guest unit beside `unir.h`, which adds
 * the edges a guest's syncs travel over (`unir_home_*`). No clock, no entropy and no threads: a
 * simulation drives it deterministically, and a crash is reopening from what was flushed.
 *
 * Frames are `vat.usc`'s (unir-store-vat/vat.usc), or a typed session's: the bytes an edge
 * carries for one message, without the edge's own header. A frame given out stays valid until
 * the next call on the same store. */
#ifndef UNIR_STORE_H
#define UNIR_STORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct unir_store unir_store;

/* On temen the program defines these: the unit's heap. A translated library has no allocator of
 * its own (#445). unir_host_alloc returns NULL on failure. The unit asks for at most 16-byte
 * alignment; it serves a larger one itself, from a larger allocation. Natively, `unir-store-c`
 * brings Rust's allocator, and nothing calls them. */
void *unir_host_alloc(uint64_t size, uint64_t align);
void unir_host_free(void *ptr, uint64_t size, uint64_t align);

/* What a call that fails returns: here, and in unir.h, which includes this header. */
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

/* Catalog entries: what a frame is. Requests are entry 0; a sync takes the rest from its home
 * (`unir_sync_took`), and `unir_store_owed` gives changes and fed parts. */
#define UNIR_STORE_REPLY 1
#define UNIR_STORE_CHANGE 2
#define UNIR_STORE_FED 3

/* No version, no head, or no deadline: UINT64_MAX. */
#define UNIR_STORE_NONE 0xffffffffffffffffull

/* A store whose log is in memory: empty when `len` is 0, else reopened from `log`, the bytes
 * an earlier store flushed (`unir_store_flushed`), so that a crash keeps only those. `source`
 * names the chunks this session makes, as a store vat's session does; a source the log has
 * seen is refused. NULL if the log does not open. */
unir_store *unir_store_open(uint64_t source, const uint8_t *log, size_t len);

/* Frees the store and its clients' sessions. */
void unir_store_free(unir_store *store);

/* What the store has flushed: its log up to the durable ack mark, the bytes a crash keeps.
 * Its length; `*log` points at it. Nothing once it keeps no log (`unir_store_drop_log`). */
size_t unir_store_flushed(unir_store *store, const uint8_t **log);

/* Lets go of the store's log and keeps none from now on (#577): for a replica whose home holds
 * what it writes durably (§9.13), which has nothing to reopen from, so its log would cost as
 * much memory again as the document it holds. It flushes as before, durable in memory, and
 * holds all it reads. */
void unir_store_drop_log(unir_store *store);

/* Opens a client's session: `vat.usc` when `doc` is NULL, else the typed session of the
 * document the ref named `doc` names. Its replies are at most `frame` bytes, its edge's
 * largest payload. The client's number, which a detached client's may be again; -1 if `doc`
 * names no document a typed session can serve. */
int64_t unir_store_attach(unir_store *store, const char *doc, size_t frame);

/* Ends a client's session, releasing what its feeds held. */
void unir_store_detach(unir_store *store, int64_t client);

/* Answers a client's request frame with its reply frame: 0, with `*reply` and `*len` the
 * reply's; -1 if the frame is not a request its session takes, a client bug for which the
 * vat severs its edges. */
int32_t unir_store_request(unir_store *store, int64_t client, const uint8_t *frame,
                           size_t frame_len, const uint8_t **reply, size_t *len);

/* Hands each ref the requests moved since the last call to every client that follows it, as
 * the vat does once it has gone round its clients. */
void unir_store_settle(unir_store *store);

/* The next message a client is owed: its entry (UNIR_STORE_CHANGE, a change to a ref it
 * follows; UNIR_STORE_FED, a part of a bundle it is fed), with `*frame` and `*len` the
 * message's; 0 when it is owed nothing; -1 when what its feed sends could not be read back,
 * for which the vat severs its edges. */
int32_t unir_store_owed(unir_store *store, int64_t client, const uint8_t **frame, size_t *len);

/* ---- The store's own documents, as its application edits and reads them ---- */

/* An edit of a text document, as its `kind` says (#525):
 * - UNIR_EDIT_REPLACE: `del` bytes at `at` replaced by the `len` bytes at `ins`;
 * - UNIR_EDIT_MOVE: a move (§9.3). The `del` bytes at `from` go so that they start at `at`,
 *   carrying the annotations they hold (§9.8). It inserts nothing, so `len` is 0.
 * A `kind` of 0 is a replace, so an initializer that names neither `kind` nor `from` makes one. */
typedef struct unir_edit {
  uint64_t at;
  uint64_t del;
  const uint8_t *ins;
  uint64_t len;
  uint8_t kind;
  uint64_t from;
} unir_edit;

#define UNIR_EDIT_REPLACE 0
#define UNIR_EDIT_MOVE 1

/* An annotation of a text document (§9.8): `[start, start + len)` of its text, a point if `len`
 * is 0; an id unique in the document, which a merge matches it by; what it says, `payload_len`
 * bytes at `payload`; and its edge policy, `assoc`: 0 before, 1 after, 2 grow, 3 shrink. */
typedef struct unir_annotation {
  uint64_t start;
  uint64_t len;
  uint64_t id;
  const uint8_t *payload;
  uint64_t payload_len;
  uint8_t assoc;
} unir_annotation;

/* What a commit makes of its base, in order: the `n_edits` edits at `edits`, each applying to
 * the text the ones before it left; then the annotations whose ids the `n_remove` at `remove`
 * are removed, each starting where the same place of `starts` says once the edits are applied,
 * if `starts` is not NULL (#455); then the `n_add` annotations at `add` added (#358). `stamp`
 * is the client's clock, recorded in the version as data (§1a.0). A count of 0 may come with
 * NULL. */
typedef struct unir_changes {
  const unir_edit *edits;
  uint64_t n_edits;
  const unir_annotation *add;
  uint64_t n_add;
  const uint64_t *remove;
  const uint64_t *starts;
  uint64_t n_remove;
  uint64_t stamp;
} unir_changes;

/* Commits `changes` to ref `name` as one version, from version `base` (UNIR_STORE_NONE to make
 * the ref). 0, with `*version` the ref's new head; 1 if the ref was not at `base`, with
 * `*version` its head (UNIR_STORE_NONE if none); -1 if the store refused it, with `*version`
 * its failure code (`unir_store_vat::fail`), or UNIR_STORE_NONE for an edge policy or an edit
 * kind that is no code. Followers hear of it at once. */
int32_t unir_store_commit(unir_store *store, const char *name, uint64_t base,
                          const unir_changes *changes, uint64_t *version);

/* Moves ref `name` from version `expected` (UNIR_STORE_NONE: it must not exist) to version `to`,
 * an earlier version or a later one: undo and redo (§9.5). The move is logged in the ref's move
 * log, `<name>@moves`, and a forwarder of the ref sends it to the home before the commits made
 * after it (#559): there it moves the home's ref to the same version, unless another writer has
 * written there since. Returns as unir_store_commit does: 0 with `*version` the ref's new head, 1
 * if the ref was not at `expected`, -1 if refused. */
int32_t unir_store_advance(unir_store *store, const char *name, uint64_t expected, uint64_t to,
                           uint64_t *version);

/* The head of ref `name`: 1 with `*version` set, 0 if it has none. */
int32_t unir_store_head(unir_store *store, const char *name, uint64_t *version);

/* Version `version`'s id, published: the same in every store that holds it. 0 with `id` set;
 * -1 if the store holds no such version. */
int32_t unir_store_publish(unir_store *store, uint64_t version, uint8_t id[32]);

/* Up to `cap` bytes of text document `version` from offset `at`, read in place: how many; -1
 * if `version` is no text document here, or `at` is past its end. */
int64_t unir_store_read(unir_store *store, uint64_t version, uint64_t at, uint8_t *buf,
                        size_t cap);

/* The annotations of text document `version` whose range meets `[from, to]`, in the document's
 * order, read in place: how many there are, of which up to `cap` are copied to `out`, their
 * payloads valid until the next call on the store; -1 if `version` is no text document here.
 * O(log m + k) of the document's m. */
int64_t unir_store_annotations(unir_store *store, uint64_t version, uint64_t from, uint64_t to,
                               unir_annotation *out, size_t cap);

/* How many bytes text document `version` holds; -1 if it is no text document. */
int64_t unir_store_len(unir_store *store, uint64_t version);

/* Whether document versions `a` and `b` pin the same buffers, each by its table hash (#557,
 * decision #557.1): 1 if they do, so they hold the same content; 0 if not; -1 if either is no
 * document version this store holds. A text's hash commits to where its bytes were placed, so
 * two texts that read alike but were written apart are not the same. A carried commit keeps its
 * placement, so a forwarder that rebases its writer's ref onto the home's version of the commit
 * moves it to the same buffers, and a reader mirroring its content has nothing to reload. It
 * costs the hashing of what was not yet hashed, as publishing does. */
int32_t unir_store_same(unir_store *store, uint64_t a, uint64_t b);

/* Where line `line` (from 0) of text document `version` starts, found through the store's
 * line summary: 0 with `*offset` set; -1 if it has fewer lines, or is no text document. */
int32_t unir_store_line(unir_store *store, uint64_t version, uint64_t line, uint64_t *offset);

/* The line (from 0) offset `offset` of text document `version` lies on: how many line breaks
 * come before it, found through the store's line summary. 0 with `*line` set; -1 if `offset` is
 * past its end, or it is no text document. */
int32_t unir_store_line_of(unir_store *store, uint64_t version, uint64_t offset, uint64_t *line);

/* ---- Syncs with the store's home, their frames carried by the caller ---- */

/* A sync of the store with its home (decision #395.4): a puller or a forwarder, run a step at a
 * time. Its asks of the store are answered in place; its asks of the home are frames the
 * caller carries to the home and back, however it reaches it. It reads no clock: each step is
 * given the time. Between steps it is data: `unir_sync_print` shows all of it. */
typedef struct unir_sync unir_sync;

/* The limits of a sync's session with the home: its request edge's largest payload and
 * capacity, and its reply edge's largest payload. */
typedef struct unir_limits {
  uint64_t requests;
  uint64_t window;
  uint64_t replies;
} unir_limits;

/* How a puller moves its ref to what the home feeds it: merging it in, as one of two peers
 * does; rebasing this store's own commits onto it, as a writer does, which a forwarder sends
 * the home (#187); or along with the home, as a reader of a ref it never writes does. Or'ed in,
 * UNIR_SYNC_ONCE: it asks the home to flush, so that what it pulls is durable there, then ends
 * once it holds what the home's ref held, at once if that had no head, as an editor opening a
 * document pulls it (#442). It flushes this store before it ends, so that what it pulled is
 * durable here too, and the editor's first commit flushes only itself (#575). */
#define UNIR_SYNC_MERGE 0
#define UNIR_SYNC_REBASE 1
#define UNIR_SYNC_WITH_HOME 2
#define UNIR_SYNC_ONCE 4

/* A puller of ref `name` from the home: fed it as it moves, and moving the ref here to it as
 * `moves` says. `me` and `peer` name this store and the home, the same two on both sides; it
 * keeps a lease at the home of `lease` ns, renewed at half that. NULL if a name is not UTF-8,
 * or `moves` is neither. */
unir_sync *unir_sync_pull(unir_store *store, const char *name, const char *me,
                          const char *peer, int32_t moves, uint64_t lease,
                          const unir_limits *limits);

/* A forwarder of ref `name`'s own commits to the home, named `peer` (#187): each goes as
 * `writer`'s transaction, one at a time and in order, once its puller (UNIR_SYNC_REBASE) has
 * landed the home's version holding the one before; it waits at most `wait` ns for each commit.
 * The home replies once its outcome is durable there, and one sent again gets the outcome it
 * had. A ref the home has no head of it makes this store's head, durably (#442). NULL if a name
 * is not UTF-8. A puller (UNIR_SYNC_REBASE) or a forwarder whose store's own commits share nothing
 * with the home's ref stops: UNIR_SYNC_APART (#552). */
unir_sync *unir_sync_forward(unir_store *store, const char *name, const char *peer,
                             const char *writer, uint64_t wait, const unir_limits *limits);

/* What a step left a sync doing. */
#define UNIR_SYNC_DONE 0 /* it ended */
#define UNIR_SYNC_HOME 1 /* it waits for a message from the home */
#define UNIR_SYNC_HERE 2 /* it waits for an edit to the store */
/* It stopped, carrying nothing: the home's ref holds a history this store's own commits share
 * nothing with, as when another made the ref there while this store made its own (#552). It
 * stopped as one told to stop does, so another sync may take its session. The home's head is
 * landed here as ref `name@peer`. Which document to keep is the caller's: to take the home's,
 * move `name` to that head (unir_store_advance) and sync again. */
#define UNIR_SYNC_APART 3

/* Steps a sync of `store` at time `now`, in ns on the caller's clock, telling it to stop if
 * `stop`: until it ends or waits. Then send the home every frame it made (`unir_sync_sent`),
 * and step it again once a message comes from the home (`unir_sync_took`), once the store is
 * edited, or once the clock reaches `*until` (UNIR_STORE_NONE: it waits for good). Returns
 * UNIR_SYNC_*; -1 if it failed, which `unir_sync_print` says. */
int32_t unir_sync_step(unir_store *store, unir_sync *sync, uint64_t now, int32_t stop,
                       uint64_t *until);

/* The next frame to send the home, in the order made: 1 with `*frame` and `*len` set, valid
 * until the next call on the sync; 0 when there is none. */
int32_t unir_sync_sent(unir_sync *sync, const uint8_t **frame, size_t *len);

/* Gives a sync a message from the home, in the order the home sent it: its catalog entry
 * (UNIR_STORE_REPLY, UNIR_STORE_CHANGE or UNIR_STORE_FED) and frame. */
void unir_sync_took(unir_sync *sync, uint32_t entry, const uint8_t *frame, size_t len);

/* Tells a sync its edges with the home ended: `ended` is unir.h's `*_ended` word (bits 0-1
 * the kind, 1 complete, 2 cancelled, 3 severed; for a sever, bits 2-5 the cause and bit 6 the
 * side, 1 the consumer). Once the messages that came are taken, its session with the home
 * fails with it, as one over those edges would. */
#define UNIR_SYNC_ENDED_COMPLETE 1u
void unir_sync_ended(unir_sync *sync, uint32_t ended);

/* What a forwarder has made durable at its home: 1 with `*version` the version here it last made
 * the home's ref hold, the home's version holding its last transaction committed, the version its
 * last move moved the ref to, or this store's own head where it made the ref there: what survives
 * a crash of this store, with the versions before it; 0 if it has made none durable; -1 for a
 * puller. */
int32_t unir_sync_published(const unir_sync *sync, uint64_t *version);

/* The sync as text, all of its state: its length, of which up to `cap` bytes are copied to
 * `buf`. Two syncs with the same text are the same. */
size_t unir_sync_print(const unir_sync *sync, char *buf, size_t cap);

/* Frees a sync of `store`: its session there ends. */
void unir_sync_free(unir_store *store, unir_sync *sync);

/* Messages (unir spec §3, #579): a connection schema and its codec, which verifies a message and
 * writes one. The codec is the store's own, natively and on temen. A schema is a connection
 * schema's canonical encoding, as unir-schemac emits it (`<name>_schema`). */
typedef struct unir_schema unir_schema;

/* `len` bytes at `at`: a tail of a message a program lays out for unir_schema_encode, which the
 * message's slots name by its number. `at` may be NULL when `len` is 0. */
typedef struct unir_piece {
  const void *at;
  uint64_t len;
} unir_piece;

/* NULL if `bytes` is malformed, or no valid schema. */
unir_schema *unir_schema_new(const uint8_t *bytes, uint64_t len);

/* The session of a typed document whose elements are of the type the schema document `t`
 * describes (unir spec §9.7): `typed.usc`'s, its requests entry 0 and its replies entry 1, by
 * which a client encodes its requests. `t` is a root type's schema document, as unir-schemac
 * emits it without `--connection`. NULL if `t` is no schema document, or a type no typed
 * session can carry. */
unir_schema *unir_schema_typed(const uint8_t *t, uint64_t len);

void unir_schema_free(unir_schema *s);

/* 0 if `frame` is the canonical encoding of a message of catalog entry `entry`, else
 * UNIR_EMALFORMED (UNIR_EINVALID: no such entry). Verify only memory no peer can write. */
int64_t unir_schema_verify(const unir_schema *s, uint32_t entry, const uint8_t *frame,
                           uint64_t len);

/* Encodes the message of catalog entry `entry` that the program lays out in its own memory.
 * Returns its canonical frame's length, and copies the frame to `out` when it fits `cap` bytes:
 * a program with too little room calls again with as much as it returned.
 * - `head` is the message's head, `head_len` bytes, as its struct in the generated header.
 * - Each slot (`unir_slot`) holds a piece's number in `off` and its count in `len`: `len`
 *   bytes, or elements back to back at their head's size, in `pieces[off]`. An element's own
 *   slots name pieces in turn, and each piece is one slot's. A slot of count 0 is empty.
 * - Padding is never read, so it may hold anything, and a head need hold only what its value's
 *   fields occupy: a sum's struct generated for another element type serves.
 * UNIR_EMALFORMED if they lay out no such message: a slot naming no piece, a piece of the
 * wrong length or named twice, a bool, tag or presence byte out of range, text that is not
 * UTF-8, or nesting too deep. UNIR_ETOO_LARGE past 4 GiB; UNIR_EINVALID: no such entry. */
int64_t unir_schema_encode(const unir_schema *s, uint32_t entry, const void *head,
                           uint64_t head_len, const unir_piece *pieces, uint64_t n,
                           uint8_t *out, uint64_t cap);

#ifdef __cplusplus
}
#endif

#endif
