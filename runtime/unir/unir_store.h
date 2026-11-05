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
 * Its length; `*log` points at it. */
size_t unir_store_flushed(unir_store *store, const uint8_t **log);

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

/* An edit of a text document: `del` bytes at `at` replaced by the `len` bytes at `ins`. */
typedef struct unir_edit {
  uint64_t at;
  uint64_t del;
  const uint8_t *ins;
  uint64_t len;
} unir_edit;

/* Commits the `n` edits at `edits` to ref `name` as one version, from version `base`
 * (UNIR_STORE_NONE to make the ref): each applies to the text the ones before it left. 0, with
 * `*version` the ref's new head; 1 if the ref was not at `base`, with `*version` its head
 * (UNIR_STORE_NONE if none); -1 if the store refused it, with `*version` its failure code
 * (`unir_store_vat::fail`). Followers hear of it at once. */
int32_t unir_store_commit(unir_store *store, const char *name, uint64_t base,
                          const unir_edit *edits, size_t n, uint64_t *version);

/* Moves ref `name` from version `expected` (UNIR_STORE_NONE: it must not exist) to version `to`,
 * an earlier version or a later one: undo and redo (§9.5). Returns as unir_store_commit does: 0
 * with `*version` the ref's new head, 1 if the ref was not at `expected`, -1 if refused. */
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

/* How many bytes text document `version` holds; -1 if it is no text document. */
int64_t unir_store_len(unir_store *store, uint64_t version);

/* Where line `line` (from 0) of text document `version` starts, found through the store's
 * line summary: 0 with `*offset` set; -1 if it has fewer lines, or is no text document. */
int32_t unir_store_line(unir_store *store, uint64_t version, uint64_t line, uint64_t *offset);

/* The line (from 0) offset `offset` of text document `version` lies on: how many line breaks
 * come before it, found through the store's line summary. 0 with `*line` set; -1 if `offset` is
 * past its end, or it is no text document. */
int32_t unir_store_line_of(unir_store *store, uint64_t version, uint64_t offset, uint64_t *line);

/* ---- Syncs with the store's home, their frames carried by the caller ---- */

/* A sync of the store with its home (decision #395.4): a puller or a pusher, run a step at a
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

/* How a puller moves its ref to what the home feeds it: merging it in, as a writer does; or
 * along with the home, as a reader of a ref it never writes does. Or'ed in, UNIR_SYNC_ONCE: it
 * asks the home to flush, so that what it pulls is durable there, then ends once it holds what
 * the home's ref held, at once if that had no head, as an editor opening a document pulls it
 * (#442). */
#define UNIR_SYNC_MERGE 0
#define UNIR_SYNC_WITH_HOME 2
#define UNIR_SYNC_ONCE 4

/* A puller of ref `name` from the home: fed it as it moves, and moving the ref here to it as
 * `moves` says. `me` and `peer` name this store and the home, the same two on both sides; it
 * keeps a lease at the home of `lease` ns, renewed at half that. NULL if a name is not UTF-8,
 * or `moves` is neither. */
unir_sync *unir_sync_pull(unir_store *store, const char *name, const char *me,
                          const char *peer, int32_t moves, uint64_t lease,
                          const unir_limits *limits);

/* A pusher of ref `name` to the home: it publishes the ref each time it moves here, waiting at
 * most `wait` ns for each move. A head it publishes is durable at the home: the home flushed
 * after its ref moved (#442). NULL if `name` is not UTF-8. */
unir_sync *unir_sync_push(unir_store *store, const char *name, uint64_t wait,
                          const unir_limits *limits);

/* What a step left a sync doing. */
#define UNIR_SYNC_DONE 0 /* it ended */
#define UNIR_SYNC_HOME 1 /* it waits for a message from the home */
#define UNIR_SYNC_HERE 2 /* it waits for an edit to the store */

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

/* The sync as text, all of its state: its length, of which up to `cap` bytes are copied to
 * `buf`. Two syncs with the same text are the same. */
size_t unir_sync_print(const unir_sync *sync, char *buf, size_t cap);

/* Frees a sync of `store`: its session there ends. */
void unir_sync_free(unir_store *store, unir_sync *sync);

#ifdef __cplusplus
}
#endif

#endif
