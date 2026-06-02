# jacl

Just A Command Lisp. A fusion of a command language and a lisp. Love child of Tcl and Clojure. Gradually typed. Supports inline operators. Easily embeddable in C with top-tier FFI. Supports multithreaded runtime using NxM work stealing. Garbage collection. Be as safe or as unsafe as you like and the language will support you.

## Goals/Features

- Safety by default
  - Absolute freedom to be unsafe if you like
    - Poke at memory addresses directly
- Great comptime/macros (lisp under the hood)
- Great shell language (command syntax)
  - Pragma to fall back to binaries on the path if a command is not defined
  - Aliases? (maybe some kind of macro)
- Top-tier FFI
  - Use it as an embedded command language in other applications
- Effortless, safe multithreading/concurrency
- Gradual yet strict typing
  - Type system strict enough that you can have unboxed structs yet gradual enough to support dynamic immutable value semantics
  - Some type inference
- Sandbox untrusted code without performance compromises
  - Use restricted environments (includes restricting/replacing builtins) for untrusted code

## Syntax

Comments

```
# This is a comment
```

Assignments

```
foo = 3   # immutable definition
bar : 4   # mutable definition
bar :: 5  # mutable reassignment

i64 baz = 13          # statically typed
i64 werf : $baz + 3   # use $var to read var
```

Commands

```
proc add [x y] {
  x + y
}

ten = add 3 7
```

Subcommands

```
prn "three: " [add 1 2] " five: " [add 2 3]
# or
prn "three: $[add 1 2] five: $[add 2 3]"
```

## Build & Test

Three pre-merge baselines:

```
./build.sh                  # full normal-mode test sweep (90/0 today)
./build.sh --tsan           # ThreadSanitizer sweep (86/2 baseline; the
                            #   two failures are documented known-and-safe)
./build.sh --wasm           # builds via Emscripten and runs the WASM
                            #   suite (test/test_wasm.mjs, including
                            #   tour.jacl end-to-end). Skips with a clear
                            #   notice when emcc or node isn't on PATH.
```

Filter to one test:

```
./build.sh --test=<name>          # one test, normal mode
./build.sh --tsan --test=<name>   # one test, TSAN
```

Run a single `.jacl` source through the harness:

```
.build/jacl_harness test/jacl/<name>.jacl
```

Or compile and run a single module's test by hand:

```
gcc -Wall -Wextra -std=c99 -g <module>/test_<name>.c && ./a.out
```

The dispatch loop in `src/vm.c` is direct-threaded (computed-goto) on
GCC/Clang non-WASM targets and falls back to a switch on Emscripten
and other compilers. See `AUDIT.md` for the per-baseline rationale and
`AUDIT_HISTORY.md` for the audit campaign and §13 implementation
notes.

## Playground

A browser playground for JACL lives in `demo/` — CodeMirror 6 editor
with a position-aware JACL syntax mode, optional vim keybindings, a
draggable panel divider, and the WASM build of the VM. To run it
locally:

```
bash build_wasm.sh           # produces build_wasm/jacl.{js,wasm}
cd demo && bash build_demo.sh   # symlinks wasm/, regenerates examples.json,
                                # bundles src/ → dist/playground.js
python3 -m http.server 8080  # or any static server
# open http://localhost:8080
```

The bundle (`demo/dist/playground.js`, ~425 KB minified) is committed
so the playground works from a clone without a Node toolchain;
`build_demo.sh` only re-bundles when you edit `demo/src/`.
