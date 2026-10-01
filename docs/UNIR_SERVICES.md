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
`Pull`/`Push`, and `Feed`, which streams a ref's bundles on a `fed` substream as it moves: unir
decision 66), which no JACL program uses yet. Directories (unir decision 65): `Put` commits files'
versions into a directory ref by name (`[map "Put" [map "name" R "base" B "entries" [vec [map "path" P
"version" V]]]]`, a version of nil removing the name), and `List` pages its entries; a directory's
version is any other version, so `Info`, `Advance` and `Merge` work on it too.

**Two machines** (unir decisions 64, 66). Run with `peer ROLE LOG2` instead, the vat is one of two
machines replicating ref `doc` over a pair of connections: its client image, granted as
`store-client` with a window of `2^LOG2` bytes, is the machine's editor, and the unit itself,
translated as a child and granted as `store-self`, runs the machine's vat, its puller (which carries
`doc` from the other machine's vat, machine 1 merging) and its connections' outgoing halves. The
host grants the connections as `link.in` and `link.out`, host procedures that write a buffer to
one, and `links`, one that reads whichever has bytes; and `entropy`, 64 random bits, from which each
vat draws its session source; and optionally `fs`, a file system where the vat keeps its store as
`store.log` (theSherwood/unir#86), so a machine run again on it picks up where it left off.
Replication stops once each machine's editor has made ref `~done` (a session ref, so a restart never finds it), and
each machine reports its head's id and text, its editor's and puller's statuses, and its senders'.
The editor also gets the vat's stdin: whatever follows the command on it is the editor's to read
(`[read-line]`). `runtime/harness/src/node.rs` is that host, shared by the tests and `unir_node`.

## Two people, two terminals

```
cargo run --release --bin unir_node -- --listen 127.0.0.1:4000     # one terminal: machine 0
cargo run --release --bin unir_node -- --connect 127.0.0.1:4000    # another, or another host
```

(from `runtime/harness`). Each runs the vat in peer mode with `tests/services/ed.jacl`, or the JACL
editor named after the flags. In `ed.jacl`, a line is appended as typed; `:i N TEXT` inserts before
line N, `:c N TEXT` changes it, `:d N` deletes it, `:p` shows the document, and `:q` (or end of
input) quits. It shows the document whenever either machine changes it, while it waits for input
(#195): `unir_node` reads the terminal on a thread of its own, so the editor's `[try-read-line]`
never waits on it (temen#2019), and the editor polls its terminal and the vat's `doc` events in turn.
An edit is made to the version last shown and merged into the head (#196), so a line number means the
line the user saw by it, wherever the other machine's edits have moved it since
(`an_edit_means_the_line_its_user_saw`). A machine's run ends once both editors have quit.

It runs on temen's JIT unless `--engine tree-walk` or `--engine bytecode` says otherwise. Each edit
shown takes well under 10 ms on the JIT, about 40 ms on the bytecode engine and about 70 ms on the
tree-walker, for about 7 s more compiling at the start.

Each machine keeps its store in a directory, `unir-node-0` or `unir-node-1` in the current one
unless `--dir DIR` names another: quit both and start them again, and the document is as you left
it (`two_people_pick_up_where_they_left_off`).

`runtime/harness/tests/services/editor.jacl` is an editor over it, run twice on one store by
`an_editor_keeps_its_document_in_the_store_vat`: it types, undoes, redoes, branches and saves, and
after the restart finds what it saved and not what it typed after.
`runtime/harness/tests/services/files.jacl` keeps a directory of files, run twice on one store by
`a_directory_of_files_lives_in_the_store_vat`: each file is written on a ref of its own, and two
commits put them into `root`; after the restart the tree is as committed, and the first commit
still holds the first version of the file the second rewrote.
`runtime/harness/tests/services/pair.jacl` is one of two editors on one document, run by
`two_editors_share_a_document_across_machines` on two TEMEN instances joined by loopback TCP: they
take turns typing, then type at once, and both machines end at the same version, `"> hello world!"`.
`two_people_edit_one_document_from_two_machines` drives `ed.jacl` on both machines through scripted
terminals, each waiting to see the other's edits before its next.

The unir unit allocates through `unir_host_alloc` (`runtime/chan_unir.c`), a pool of size classes up
to 64 KiB: a client adopts a service's whole schema into one buffer, and the store vat's passed
4 KiB with directories.

## Build

`runtime/service_unir.c`, included by `chan.c` with the unir backend, is the whole JACL side: the
session table and the three builtins over `unir.h`'s `unir_service_*`. The unit converts between the
service's types and JACL's value frames (`runtime/unir/value.usc`), so no service needs code here.
Without the unir backend the builtins return an error value.
