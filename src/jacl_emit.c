/* JACL — emit-only unity build (item 6).
 *
 * The same frontend + codegen pipeline as src/jacl.c, but WITHOUT the legacy bytecode backend
 * (bytecode.c / compiler.c / vm.c) or the bytecode-VM scheduler + embedding API (runtime.c /
 * embed.c). It is what the SVM `.svmb` compiler-guest and the browser emit-wasm compile: they
 * only ever *compile JACL source to SVM IR* (and, via the macro-staging + interpret hooks, run
 * it on the SVM engine), so the bytecode interpreter is dead weight — dropping it shrinks the
 * guest and removes the ~33 stubbed externals (docs/SVM_SELFHOST_FEASIBILITY.md §2).
 *
 * The front-end GC (gc.c / gc_collect.c) stays — it is the front-end's own allocator for the
 * transient values macro expansion produces. It no longer names the bytecode `VM` type (the GC
 * root-provider decouple), so it compiles here with no legacy backend present.
 *
 * The small surface the emit path still needs from the legacy files (macro table + expand-state
 * types, three helpers, the context-lifecycle no-ops, and the JaclClosure/BytecodeChunk layouts
 * the GC traces) is provided by jacl_emit_support.c, a parallel mirror — the two definition sets
 * never share a translation unit.
 *
 * Build with -DJACL_EMIT_ONLY (emit_jacl.c selects this unity under that flag). The full native
 * runtime still lives in src/jacl.c. */
#ifndef JACL_C
#define JACL_C

#ifndef JACL_EMIT_ONLY
#define JACL_EMIT_ONLY 1
#endif

/* --- System headers --- */
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- Infrastructure (single-header libraries) --- */
#define ARENA_IMPLEMENTATION
#include "../lib/arena/arena.h"
#include "../lib/platform/platform.h"
#include "../lib/unicode/unicode.h"

/* --- JACL frontend pipeline (order matters) --- */
#include "value.c"
#include "gc.c"

#define STREE_ALLOCATOR {libc_alloc, libc_realloc, libc_free, NULL}
#define STREE_GC_MODE
#define STREE_GC_ALLOC(t, sz)    gc_alloc(gc__current_heap, (t), (sz))
#define STREE_GC_OBJ_INTERNAL    OBJ_ROPE_INTERNAL
#define STREE_GC_OBJ_LEAF        OBJ_ROPE_LEAF
#include "../lib/sum_tree/rope.h"

#include "string.c"
#include "lexer.c"
#include "ast.c"
#include "type_error.c"
#include "parser.c"
#include "shapes.c"
#include "typer.c"
#include "collections.c"

/* Legacy-backend surface the macro expander + GC tracer need, mirrored (no bytecode.c/compiler.c/vm.c). */
#include "jacl_emit_support.c"

#include "syntax.c"
#include "gc_collect.c"

#endif /* JACL_C */
