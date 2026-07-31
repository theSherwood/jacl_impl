/* JACL — Unity build. Include this single file to get the full pipeline.
 *
 * The sole backend is the SVM (codegen/*.c + the runtime/ Rust engine); the
 * legacy bytecode backend (bytecode.c / compiler.c / vm.c) and its scheduler +
 * embedding API (runtime.c / embed.c) were removed in item 6. This unity is the
 * frontend (lexer → parser → typer → shapes) plus the front-end GC (gc.c /
 * gc_collect.c) that backs the transient values macro expansion produces.
 *
 * The small surface the macro expander + GC tracer still need from the old
 * files (macro table + expand-state types, three helpers, the context-lifecycle
 * no-ops, and the JaclClosure/BytecodeChunk layouts the GC traces) is provided
 * by jacl_emit_support.c, a parallel mirror. The JACL_EMIT_ONLY guards
 * throughout the tree (a historical name from the emit-only split) now gate the
 * one and only build. */
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

/* platform.h is header-only, no IMPLEMENTATION define needed */
#include "../lib/platform/platform.h"

/* --- Unicode library (needed by string.c for grapheme counting) --- */
#include "../lib/unicode/unicode.h"

/* --- JACL pipeline (order matters) --- */
#include "value.c"
#include "gc.c"

/* --- Rope library (needed by string.c for JaclRopeString) ---
 * Included after gc.c so that gc_alloc and GCObjType are visible
 * to sum_tree.h's GC-mode allocation macros.
 * Override STREE_ALLOCATOR with a brace-enclosed initializer to avoid
 * the get_libc_allocator() function call in a static initializer
 * (not valid in -std=c99). */
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

/* Legacy-backend surface the macro expander + GC tracer need, mirrored
 * (no bytecode.c/compiler.c/vm.c). */
#include "jacl_emit_support.c"

#include "syntax.c"
#include "gc_collect.c"

#endif /* JACL_C */
