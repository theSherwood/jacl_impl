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

Its requests (unir `crates/unir-store-vat/vat.usc`): `Commit` (edits to a text buffer as one version
on a ref, or a `Conflict`), `Read`, `Head`, `Info` (length, lines, stamp, parents), `Advance` (move a
ref by compare-and-swap: undo and redo), `Publish`, `Flush` (save), and `Follow` (hear a ref's position
now, then its moves, as `changes` events `[map "name" N "seq" S "to" V]`). `seq` counts the ref's moves
and grows with each, so a follower keeps the event with the highest and needs them in no order; one that
lags gets only the latest (unir decision 58).

`runtime/harness/tests/services/editor.jacl` is an editor over it, run twice on one store by
`an_editor_keeps_its_document_in_the_store_vat`: it types, undoes, redoes, branches and saves, and
after the restart finds what it saved and not what it typed after.

## Build

`runtime/service_unir.c`, included by `chan.c` with the unir backend, is the whole JACL side: the
session table and the three builtins over `unir.h`'s `unir_service_*`. The unit converts between the
service's types and JACL's value frames (`runtime/unir/value.usc`), so no service needs code here.
Without the unir backend the builtins return an error value.
