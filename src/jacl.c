/* JACL — Unity build. Include this single file to get the full pipeline. */
#ifndef JACL_C
#define JACL_C

/* --- System headers --- */
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

/* --- Rope library (needed by string.c for JaclRopeString) ---
 * Override STREE_ALLOCATOR with a brace-enclosed initializer to avoid
 * the get_libc_allocator() function call in a static initializer
 * (not valid in -std=c99). */
#define STREE_ALLOCATOR {libc_alloc, libc_realloc, libc_free, NULL}
#include "../lib/sum_tree/rope.h"

/* --- JACL pipeline (order matters) --- */
#include "value.c"
#include "gc.c"
#include "string.c"
#include "lexer.c"
#include "ast.c"
#include "parser.c"
#include "bytecode.c"
#include "collections.c"
#include "compiler.c"
#include "vm.c"
#include "gc_collect.c"
#include "runtime.c"
#include "embed.c"

#endif /* JACL_C */
