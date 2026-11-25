# A JACL program in an application's own program

An application on unir builds one temen program from its own code and a JACL program, and the JACL
program calls the application's functions with `extern` (#290). nisaba is the first: an editor
whose core is Nim and whose command language is JACL. The application's linker, temen-link, links
JACL's parts as libraries beside its own units (#290's decision B). The application's code starts
the JACL program, and the program runs its loop until it ends.

`examples/app/` is a whole application of this shape: `build.sh` builds it, and `tests/app.rs` runs
it on the tree-walker, the bytecode engine and the JIT.

## JACL's parts

`jacl_units <prog.jacl> <outdir>` (`runtime/harness/src/bin/jacl_units.rs`) writes three link units:

| Unit | What it is |
|---|---|
| `jaclrt.temeno` | JACL's runtime with its unir layer, translated as a link unit. Its calls into unir's C ABI stay symbols. |
| `<prog>.temeno` | The program's object. It exports `__jacl_entry`. |
| `unir_cabi.temeno` | unir's C ABI, the unit the runtime was built against (`runtime/unir/UNIR_REV`). |

The application links them with its own units on the capability edge, the edge a unir host runs a
vat on:

```
temen-link -o:app.temen --edge:caps <its Nim modules> jaclrt.temeno prog.temeno unir_cabi.temeno <its C units>
```

## The rules

- **The entry.** The application's code calls `__jacl_entry` once. From Nim:
  `proc jaclEntry(): int64 {.importc: "__jacl_entry".}`. It brings the runtime up, runs the
  program's top level as the root fiber of JACL's scheduler, and returns the value of its last
  form when the program ends.
- **One unir unit.** Link the `unir_cabi.temeno` that `jacl_units` writes and no other copy. A second
  copy exports every unir name again, and the link fails.
- **One owner of unir's heap.** The runtime serves the unit's heap: `unir_host_alloc` and
  `unir_host_free` (`runtime/chan_unir.c`). An application whose own code exports them too, as
  unir's `unir_store.nim` does, leaves its copy out. With both, temen-link fails the link (exit 3).
- **`extern`.** A JACL program calls an application's C function or Nim `exportc` proc by its C
  name. Each scalar crosses in its declared width (SYNTAX.md, `[Buf N T]`), a pointer into a JACL
  buffer as its address, and the data-stack pointer goes first, as temen's on-ramp passes it.
  temen-link binds Nim's procs and C's functions alike (theSherwood/temen#3178).
- **The stack.** A function JACL calls runs on the calling JACL fiber's stack, 64 KiB
  (`runtime/sched.c`), so it has to fit there.
- **What the host grants.** The program's manifest asks for Nim's capability edge (`stream_write`,
  `stream_read`, `stderr`, `exit`, `vm_map`, `vm_page_size`), the runtime's `read` and `write`, and
  `vm_region_create`, in which unir makes a channel's edges. The runtime resolves others by name
  when the program uses them (`stdin`, `clock`, `fs`, `exec`).

## Building with nimony

nimony's Temen backend, `nimony t`, links the program with the `temen-link` on `PATH`, but passes it
the program's modules alone, on the POSIX edge (theSherwood/temen#3182). So `examples/app/build.sh`
puts a `temen-link` on `PATH` that adds the units and `--edge:caps` to the program's own link, as
unir's e2e does. The wrapper goes when nimony passes both.

A JACL program has no native build, so an application that also builds natively with `nimony c`
leaves it out of that build.
