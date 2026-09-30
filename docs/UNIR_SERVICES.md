# Services on Unir

**Status:** built (2026-09-29, theSherwood/unir#50 and #51). A JACL program is a client of a service
vat, unir's store vat first, on all three TEMEN engines.

A service is a vat that serves a typed endpoint to its clients (unir spec §9.6, decision 57). A program
granted one as `NAME` holds three capabilities: `NAME.requests` and `NAME.replies`, its two edges, and
`NAME.post`, a post-only capability to the service's notification, which wakes the service for either
edge. Nothing about the service is compiled into the program.

## The surface

```
def s [service "store"]                           ; a session, or an error value
def r [service-call $s [map "Head" "doc"]]        ; a request; its reply
[service-event $s $true]                          ; the next event, waiting; nil if none and not waiting
```

- `[service NAME]` connects: the service offers its schema on the reply edge, the runtime adopts it,
  then offers its request type back. It returns a small integer handle. A program has up to 8 sessions
  open; all close when it exits.
- `[service-call S REQ]` converts `REQ` to the service's request type, sends it, and returns the
  reply, converted back. One request at a time per session.
- `[service-event S WAIT]` returns the next event the service sent, as `[map ENTRY VALUE]`, where
  `ENTRY` is the event's catalog entry name (the store vat's is `changes`). Events that arrive while a
  call waits for its reply are kept for later.

A value that does not fit the service's type is an error value, and nothing is sent.

## Values

Requests and replies are plain JACL values, converted by structure (unir's `Dynamic`, spec decision 57):

| service type | JACL value |
|---|---|
| struct | a map from field name (a missing field takes its default), or a vector in field order |
| sum | the variant's name for a unit variant (`"Flush"`), else `[map "Variant" payload]` |
| optional | nil, or the value |
| integers, timestamps | integers, range-checked |
| text | a string |
| bytes | a string going in; a `[Buf n u8]` coming back (`buf-string` turns it into a string) |
| list, array | a vector |
| capability | refused |

Map keys may be written `"name"` or `":name"`. An unknown field or variant, a value out of range, or
a wrong shape is refused, never guessed at.

## The store vat

unir's store vat is vendored as `runtime/unir/unir_store_vat.ll` (`cargo run --release -- store-ll`
in unir's `e2e/temen`), a root image. Run with `serve K SOURCE LOG2` on stdin, a window of its image's
memory times 256, a file system granted as `fs`, and the client image granted as `store-client`, it
spawns `K` clients with windows of `2^LOG2` bytes, each granted the store as `store` and the vat's own
`stdout` (which a JACL image imports as `write`). It persists to `store.log`, reopened as session
`SOURCE`, and reports each client's status, ref `doc`'s text and the version count.

Its requests (unir `crates/unir-store-vat/vat.usc`): `Commit` (edits to a document's text as one
version on a ref, or a `Conflict`), `Read`, `Head`, `Info` (length, lines, stamp, parents), `Advance`
(move a ref by compare-and-swap: undo and redo), `Publish`, `Flush` (save), and `Follow` (hear a ref's
position now, then its moves, as `changes` events `[map "name" N "epoch" E "seq" S "to" V]`). `epoch`
counts the times the store was opened and `seq` the ref's moves, so a follower keeps the event with the
greatest `(epoch, seq)` and needs them in no order, across restarts too; one that lags gets only the
latest (unir decisions 58, 60). Since unir stage 4 a version is a document, text plus annotations, and
the vat also annotates, merges, diffs and replicates (`Annotate`, `Annotations`, `Merge`, `Diff`,
`Pull`/`Push`), which no JACL program uses yet. Directories (unir decision 65): `Put` commits files'
versions into a directory ref by name (`[map "Put" [map "name" R "base" B "entries" [vec [map "path" P
"version" V]]]]`, a version of nil removing the name), and `List` pages its entries; a directory's
version is any other version, so `Info`, `Advance` and `Merge` work on it too.

`runtime/harness/tests/services/editor.jacl` is an editor over it, run twice on one store by
`an_editor_keeps_its_document_in_the_store_vat`: it types, undoes, redoes, branches and saves, and
after the restart finds what it saved and not what it typed after.
`runtime/harness/tests/services/files.jacl` keeps a directory of files, run twice on one store by
`a_directory_of_files_lives_in_the_store_vat`: each file is written on a ref of its own, and two
commits put them into `root`; after the restart the tree is as committed, and the first commit
still holds the first version of the file the second rewrote.

The unir unit allocates through `unir_host_alloc` (`runtime/chan_unir.c`), a pool of size classes up
to 64 KiB: a client adopts a service's whole schema into one buffer, and the store vat's passed
4 KiB with directories.

## Build

`runtime/service_unir.c`, included by `chan.c` with the unir backend, is the whole JACL side: the
session table and the three builtins over `unir.h`'s `unir_service_*`. The unit converts between the
service's types and JACL's value frames (`runtime/unir/value.usc`), so no service needs code here.
Without the unir backend the builtins return an error value.
