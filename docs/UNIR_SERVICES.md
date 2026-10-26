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
def p [service "pane" "n"]                        ; a session whose events signal this vat's notification
[service-send $p [map "content" [map "Text" "hi"]]] ; a message on a named entry, waiting for nothing
def e [service "events"]                          ; a subscription: granted without its requests
def t [service-schema $e "events"]                ; one entry's schema, for a transcript block
```

- `[service NAME]` connects: the service offers its schema on the reply edge, the runtime adopts it,
  then offers its request type back. It returns a small integer handle. A program has up to 8 sessions
  open; all close when it exits.
- `[service-call S REQ]` converts `REQ` to the service's request type, sends it, and returns the
  reply, converted back. One request at a time per session.
- `[service-event S WAIT]` returns the next event the service sent, as `[map ENTRY VALUE]`, where
  `ENTRY` is the event's catalog entry name (the store vat's is `changes`). Events that arrive while a
  call waits for its reply are kept for later. With `WAIT` false or nil it returns at once, nil if no
  event is there; with `WAIT` true it waits for one; with a number of seconds it waits that long, and
  returns nil sooner once one of the program's tasks writes to a transcript or ends, so a shell waiting
  for its pane notices its jobs (#219).
- `[service NAME N]` connects as `[service NAME]` does, to a service whose events signal the
  notification the vat was endowed with as `N` (unir decision #208.1): in unir's sessions a shell's
  pane signals its vat's own, `n` (unir decision #262.9). Waiting for such a service's events waits
  on that notification rather than polling (see "Waiting" below). A vat has one notification: a
  second name is an error.

- `[service-send S [map ENTRY VALUE]]` sends `VALUE` as a message of the catalog entry named
  `ENTRY`, waiting for nothing: a connection that is not request and reply, such as a pane. A session
  that never calls hears every entry the other end writes as an event.
- A service granted without `NAME.requests` is a **subscription** (unir decision #282.5): it hears
  what the service writes, as events, and sends nothing, so `service-call` and `service-send` on it
  are errors. unir's sessions grant a shell the mount's syncs so, as `events`.
- `[service-schema S NAME]` is the schema of the service's catalog entry `NAME` alone: the service's
  types, with that entry as entry 0. `transcript-block` takes it, so a shell shows what a
  subscription hears typed as the service sent it (unir decision #282.5). An error if the service
  has no entry by that name.

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

**Two machines** (unir decisions 64, 66, 88). Run with `peer ROLE LOG2` instead, the vat is one of two
machines replicating ref `doc` over one connection: its client image, granted as `store-client`
with a window of `2^LOG2` bytes, is the machine's editor, and the unit itself, translated as a
child and granted as `store-self`, runs the machine's vat, its puller (which carries `doc` from the
other machine's vat, machine 1 merging) and its senders. The host grants:
- `link`, the connection: a host procedure that writes a buffer to it, reads what has arrived
  without waiting, and shuts it down;
- `bell`, what the root waits on while the connection has nothing: an offloadable host procedure
  that waits until the host rings it, when something arrives, or until a time on `clock`
  (theSherwood/unir#225). Without it the root polls `link` every 50 µs;
- `keys`: the machine's keystore and the other machine's key. With them the machines authenticate
  each other and encrypt what crosses (Noise, with UCAN delegations as the proof: unir decisions 84
  and 86);
- `entropy`, 64 random bits, from which each vat draws its session source and its handshakes;
- `clock`, by which it times out a silent connection and checks a token;
- optionally `fs`, a file system where the vat keeps its store as `store.log` and its keystore in a
  store of its own (theSherwood/unir#86), so a machine run again on it picks up where it left off.

Run with `pair` and only `entropy`, the unit makes two machines' keys, each delegating its store to
the other until either rotates its key, and prints a line for each. A host with none of unir's code
keeps each line beside its machine's store.
Replication stops once each machine's editor has made ref `~done` (a session ref, so a restart never finds it), and
each machine reports its head's id and text, its editor's and puller's statuses, and its senders'.
The editor also gets the vat's stdin: whatever follows the command on it is the editor's to read
(`[read-line]`). `runtime/harness/src/node.rs` is that host, shared by the tests and `unir_node`.

## Two people, two terminals

```
cargo run --release --bin unir_node -- --pair unir-node-0 unir-node-1   # once: both machines' keys
cargo run --release --bin unir_node -- --listen 127.0.0.1:4000          # one terminal: machine 0
cargo run --release --bin unir_node -- --connect 127.0.0.1:4000         # another, or another host
```

(from `runtime/harness`). `--pair` makes both machines' keys, each in its directory as `keys`; for
another host, copy `unir-node-1` there. Each run then reads its keys from its directory and runs the
vat in peer mode with `tests/services/ed.jacl`, or the JACL editor named after the flags. In `ed.jacl`, a line is appended as typed; `:i N TEXT` inserts before
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

Each machine keeps its store and its keys in a directory, `unir-node-0` or `unir-node-1` in the
current one unless `--dir DIR` names another: quit both and start them again, and the document is as
you left it (`two_people_pick_up_where_they_left_off`).

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
to 256 KiB: a client adopts a service's whole schema into one buffer, and the store vat's passed
4 KiB with directories.

### Waiting

The vat is one call at a time (docs/UNIR_CHANNELS.md), so no wait for a service's events may hold it.
- **A service opened with the vat's notification** (`[service NAME N]`) is armed before the wait
  (`unir_service_arm`): its next event signals the notification (unir decision #208.1). The waiting
  fiber parks on a word of its own (`jacl_unir_events`), and its job is marked as waiting on the
  notification. A worker with nothing to run, whose parked fibers all wait so, waits on the notification
  itself (`unir_notification_wait`, which takes no vat), until its fibers' first timeout, rather than
  re-polling them every millisecond. An event from outside wakes the worker, which moves the word and
  re-polls the fiber. The runtime signals the notification itself when a task writes to a transcript or
  ends, and when a job becomes ready to run (`pool_wake`). Only one worker waits on it at a time, since
  a wait takes every badge; with more than one worker, each still re-polls every millisecond, when it
  also answers a collection.
- **Any other service** is polled, from 50 µs apart to a millisecond, the vat given back between polls.

## Build

`runtime/service_unir.c`, included by `chan.c` with the unir backend, is the whole JACL side: the
session table and the three builtins over `unir.h`'s `unir_service_*`. The unit converts between the
service's types and JACL's value frames (`runtime/unir/value.usc`), so no service needs code here.
Without the unir backend the builtins return an error value.

## A pane

unir's store vat in its `shell SOURCE LOG2 ROWS COLS` mode (theSherwood/unir#102, unir decisions 81, 100
and 117) also runs a terminal vat, the only vat that holds the terminal, and a composition vat keeping
ref `view` as the references no file in directory `dir` defines. It grants its client the store twice,
as `store` and as `transcript` (a session for the transcript's writer, its requests in 64 KiB frames),
and the pane as `pane` (`pane.requests` what it shows, `pane.replies` its events). A program opens
`store` and `transcript` before `pane`: the vat connects its clients' sessions in order before it
answers any, and the terminal reads the store before it offers the pane.

- **What it shows** is a transcript: a document in the store of blocks, each headed by the command and
  job that wrote it, which the terminal vat reads and draws by type (see "The transcript" below), and
  the line being typed, a document of its own. `[service-send $p [map "show" [map "transcript" T
  "line" L]]]` names the two refs to the pane.
- **Events.** `[service-event $p $false]` returns:
  - `[map "keys" K]`, where `K` is a named key (`"Enter"`, `"Backspace"`, …) or
    `[map "Char" CODE]`, `[map "Ctrl" LETTER]` or `[map "Alt" CODE]`;
  - `[map "geometry" [map "rows" R "cols" C]]`;
  - `[map "lifecycle" …]`: focus, or `"Close"`;
  - `[map "acts" …]`: a value the person activated, copied or pasted.

Nothing a program writes is an escape sequence, and a control character in its text is shown, not
obeyed. A program must not print: only the terminal vat writes to the terminal.

`runtime/harness/tests/services/pane.jacl` is the example. It runs as follows:
- It edits keys into the typed line.
- Each command writes a block: `ls` the directory as a table (rows of a `File` schema it declares),
  `add` and `fg` a line of text.
- `watch` starts a job that shows the view live. A job writes to a channel, and the program forwards
  the foreground job's output into the job's block.
- Ctrl-Z stops forwarding, so the job waits in its write; `fg` resumes forwarding.

## The transcript

`runtime/transcript_unir.c` is unir's transcript writer (unir decision 117) behind its C ABI, so a
program writes blocks without encoding a record:

| builtin | does |
|---|---|
| `[transcript NAME T L]` | opens a writer of the transcript at ref `T`, its typed line at ref `L`, over the store session granted as `NAME`; an integer handle |
| `[transcript-line T TEXT]` | sets the typed line, committed at once |
| `[transcript-block T CMD JOB S]` | opens a block of job `JOB`'s values, which `CMD` started; its index |
| `[transcript-output T CMD JOB]` | opens a block of job `JOB`'s output, text in a document of its own (unir decision 107); its index |
| `[transcript-push T B V]` | adds `V` to block `B` (a live block's replaces its last) |
| `[transcript-write T B TEXT]` | appends `TEXT` to output block `B` |
| `[transcript-end T B]` | ends block `B` |
| `[transcript-flush T]` | commits what was queued as one version |
| `[transcript-job T CMD JOB]` | a place in the transcript for job `JOB`, which `CMD` started: a task whose `$ctx.out` is it writes its output there (see "The shell" below); an integer handle |
| `[transcript-job-end S COMPLETE]` | ends job `S`'s place, its last block complete or (`COMPLETE` false) cancelled; how many blocks the job opened |
| `[transcript-pick T B V]` | the name of reference `V` of block `B`, a block of references; nil if there is none |
| `[schema DESC]` | the schema `DESC` describes; an integer handle. `[schema "link"]` is unir's link document, whose values are references |
| `[emit V S]` | in a job, `V` as a value of schema `S`: a block of `S`'s entry 0 (live if it is latest-wins) |
| `[handler-open IMAGE ARGS TEXT FUEL MEMORY]` | spawns a handler for a file (unir decision 106) from the image granted as `IMAGE`, with `ARGS`, the surface the session set aside for handlers and that budget, and sends it `TEXT` as the file; its child. The program ends after its handlers |
| `[legacy-job IMAGE ARGS ROWS COLS FUEL MEMORY]` | in a job: runs the legacy adapter (unir decision 105) from `IMAGE`, a POSIX program in a terminal of the job's size in a pane of `ROWS` by `COLS`; its screens are the job's live grid block and the job's input goes to it until the program ends; its exit status |

A block's values are messages of its schema: `S` from `[schema DESC]`, or nil for JACL's own values
(`value.usc`), which a renderer shows as nested structure. `DESC` is a connection schema written as
unir's meta-schema has it, converted by structure: `types`, each a type such as `"text"`, `"u64"` or
`[map "struct" [map "name" N "fields" [vec [map "name" F "type" I] …]]]`, and `substreams`, each
`[map "name" N "type" I "profile" P]`. A block holds messages of its entry 0, converted from JACL values by
structure as service requests are, and a profile of 3 (latest-wins) makes it live. A struct of one-line
fields shows as a table:

```
def files [schema [map "types" [vec "text" "u64" [map "struct" [map "name" "File" "fields" [vec [map "name" "file" "type" 0] [map "name" "bytes" "type" 1]]]]] "substreams" [vec [map "name" "files" "type" 2 "profile" 1]]]]
def b [transcript-block $t "ls" 2 $files]
transcript-push $t $b [map "file" "notes" "bytes" 345]
transcript-end $t $b
transcript-flush $t
```

A value of the link document is a **reference**, a name a job's output gives something the shell
granted it (unir decision 106): a renderer draws it underlined, and a person can activate it. The
writer keeps no values, so the transcript keeps each reference's name as it is written, for
`transcript-pick` to resolve an activation (a block's place among the transcript's and the value's
within it). A pipeline's references, forwarded without decoding, are not kept.

A value that does not fit is an error value, and nothing is written. A call that waits for the store
parks its fiber, so a transcript is one call at a time: another fiber's call meanwhile waits its turn.
A job's output is committed whenever 32 KiB of it waits.

`jacl_pane [PROGRAM.jacl]` runs a program this way on the terminal it is started from: the shell
below, unless another is named. It puts the terminal in raw mode for the run, and grants the terminal
vat stdin's bell (`stdin.bell`, unir decision #259.1), which the thread reading the terminal rings
for each read, so the vat waits for keys rather than polling stdin.
`codegen.rs::a_jacl_program_runs_in_a_terminal_pane` drives it with a scripted terminal on all three
engines, and fails if the terminal vat polled.

## The shell

`runtime/harness/tests/services/shell.jacl` is the JACL shell (#204; unir decision 118), a pane client.
Each line typed is a **job**: compiled in the shell by the compiler linked into its image, and run as a
task in the shell's own domain (`[repl-eval LINE]`, temen's §22 `Jit` capability). So what one line
defines, the lines after it see: a binding, a `mut` a later line sets, a proc, a closure, a struct.

- **Output goes to the job's blocks.** The shell starts each job with `$ctx.out` naming the job's place
  in the transcript (`transcript-job`), and the task's output goes there rather than to the host
  stream: what it prints into an output block, the values it emits into a block of values, and its
  pipelines' output, bytes as text and typed output under the schema the last program offered
  (adopted, unir decision 117). Each block is headed by the line. A job that shows nothing else shows
  its value as a block; one that fails shows why.
- **Programs are the shell's grants.** `!name args` runs the program the shell was granted as
  `bin.name` (docs/UNIR_PIPELINES.md); unir's `shell` mode passes the client each program its command
  names (`shell SOURCE LOG2 ROWS COLS [PROGRAM…]`), and the Jit capability.
- **Job control.** The shell waits for a line's job unless the line ends in `&`. While it waits,
  Ctrl-C ends the job (cancel: its pipelines are severed and their stages killed) and Ctrl-Z stops it
  (suspend: its pipelines stall under backpressure), returning to the prompt; other keys wait with it.
  `jobs` lists the jobs running or stopped, `fg [N]` waits for job `N` (the last, by default),
  resuming it if it is stopped, `bg [N]` resumes it in the background, and `kill [N]` ends it. A
  background job's output goes to its blocks as it comes.
- **Editing.** Backspace edits the line, and Up and Down move through the earlier lines.
- **A job's input.** While the shell waits for a job, the pane's keys (but Ctrl-C and Ctrl-Z) and
  sizes are the job's (`$ctx->in`, an atom of a vector of them, which the job empties as it takes
  them: `legacy-job` sends them to the program). What the job leaves is typed at the prompt once it
  ends, after what was already waiting there.
- **References.** A reference activated in the transcript runs `open NAME` as a job, `NAME` the
  reference's, which shows only its output: the profile says what opening a name means.
- **Waiting.** The shell opens its pane with the notification its events signal (`[service "pane"
  "n"]`). When a pass of its loop finds nothing to do, its next read of the pane waits on that
  notification for up to a second, or until a job writes to the transcript or ends (#219). So a key, a
  job's output and a job's end each start a pass as they come, and an idle shell makes one pass a
  second.
- **The profile.** Before the first prompt the shell evaluates the store's document `profile`, if it
  has one, in its own domain, so every line sees what the profile defines. It reads it 2 KiB at a
  time, since a reply is one of the session's 4 KiB frames. unir's stage 7 session
  commits one (`e2e/temen/jacl/profile.jacl` there) that defines its commands, its plumber (`open`)
  and `legacy`; a profile that fails shows why in a block.

A trap in a line ends the shell, as one in the shell's own code would. A line's loops are
job-control points (#218): once a slice of their headers has run, the line yields to the shell's own
task, which reads the keys, and Ctrl-C and Ctrl-Z act there. So they reach a line that computes
without an `await`, a `sleep` or a pipeline's read; one long call into the runtime is not
interrupted, but it ends.
A line may define a macro (#214). Its body runs as each later line that uses it compiles, on the
shell's own runtime: staged into a unit of its own through the same Jit capability, linked against
the same table, and called on the fiber the line compiles on (`stage_bridge_guest.c` in
`codegen/selfhost/macro_staging/`). A collection that stops it there stops the line's task in its
stead, so both are scanned. A macro whose body does not compile is refused before the session's
macro table holds it, so a redefinition leaves the old one, and the lines after compile as before. A
body that waits (a `sleep`, an `await`) leaves its line unfinished (`repl-eval: the compiler did not
finish`). A macro's unit keeps its slot by the same rule as a line's, below.
A line's unit stays installed while its code may still be called. A leaf line (no function of its
unit takes a function's reference, `irb_module_is_leaf`) gives its slot in the shell's dispatch table
back when it returns. Any other line's unit names its own functions, so it takes a slot for each as
it compiles, and one more for its install (temen §22, unit-own funcrefs). It gives them all back once
nothing can reach its code (#215): its line has returned, and no closure or generator a collection
keeps holds one of its slots. No frame of its code is live then either, since each lies above its
line's entry or above a frame that keeps a closure of the unit live to its return
(`jacl_closure_keep`). When a line finds the table full, the REPL collects, noting the slots of the
closures and generators the collection keeps (`jacl_gc_collect_noting`), frees every unit none of
whose slots is noted, and tries again. The code behind a freed slot stays in the JIT's code arena,
which only the embedder can compact (temen §22, code reclaim). The compiled code's handle, a
capability, is given up as soon as the unit is installed, since the capability table holds 1,024.
A line's source, tokens and AST are freed once it has compiled, unless it declares a struct or a
macro, which later lines' compiles use; a session that kept every line's ran out of memory at about
1,350 lines. `repl.rs::a_session_outlasts_the_jit_tables` runs 1,000 lines, then defines a proc and
calls it. `repl.rs::a_session_redefines_a_proc_past_the_dispatch_table` defines a proc 1,000 times,
calling it after each, while three old units that something still reaches keep working: a closure
kept under another name, and a proc and a line each running in a task.

The shell's image is the runtime with the compiler, the REPL bridge (`codegen/selfhost/repl/`) and
the macro-staging bridge and glue linked in (`repl::shell_image`).
`repl.rs::the_shell_runs_each_line_in_its_own_domain` drives it in a pane with a scripted terminal on all three engines: definitions across lines, history, a pipeline's
text, a program's values, a background job listed and killed, a stopped job finishing in the
background after `bg`, one waited for again with `fg N` and ended with Ctrl-C, a reference emitted
under `[schema "link"]` drawn underlined, and keys typed while a job runs reaching its input and
coming back to the prompt. `jacl_shell_image OUT` writes the image encoded, for unir's session
(theSherwood/unir#106) to vendor; there it takes the shell's seat in the typed terminal on all three
engines, its profile defining the session's commands.

`[clock]` is the host's time in nanoseconds, the granted `clock` capability's op 0
(`runtime/clockcap.c`), and an error where no clock is granted. temen's own clock is deterministic;
in unir's session this is the clock its host stamps keys and frames with, so a job can say when it
wrote what a frame shows, as the session's benchmark needs (`codegen.rs::clock_reads_the_granted_clock`).
