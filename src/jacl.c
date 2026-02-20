/* JACL — Unity build. Include this single file to get the full pipeline. */
#ifndef JACL_C
#define JACL_C

/* --- System headers --- */
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

/* --- JACL pipeline (order matters) --- */
#include "value.c"
#include "string.c"
#include "lexer.c"
#include "ast.c"
#include "parser.c"
#include "bytecode.c"
#include "compiler.c"
#include "vm.c"

#endif /* JACL_C */
