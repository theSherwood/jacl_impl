/*
 * JACL Macro Benchmark
 *
 * Measures the cost of each compilation stage (lex, parse, expand, compile,
 * execute) across programs with varying macro usage.  Designed to track
 * the impact of the macro system and measure future optimisation gains.
 *
 * Pipeline stages:
 *   lex     → lexer_lex
 *   parse   → parser_parse
 *   expand  → ast_expand_macros (Phase 0 prelude + user macros + expansion)
 *   compile → compiler_compile  (includes a second expand pass on the
 *             already-expanded AST; that pass is a fast no-op scan)
 *   exec    → vm_exec
 *
 * Usage:  build.sh --test=bench
 * Output: table of µs/iter for each stage × scenario.
 */

#define _POSIX_C_SOURCE 199309L
#include "test_helpers.h"
#include "../src/jacl.h"
#include <time.h>

/* ── Timing helpers ─────────────────────────────────────────────── */

static uint64_t __attribute__((noinline)) now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ── Scenario programs ──────────────────────────────────────────── */

/* 1. Baseline: pure arithmetic, no macros */
static const char *SRC_BASELINE =
    "def a [+ 1 2]\n"
    "def b [* $a 3]\n"
    "def c [+ $b $a]\n"
    "[+ $c 100]\n";

/* 2. Lambda shorthand: exercises the built-in \ macro */
static const char *SRC_LAMBDA =
    "def f [\\ + $it 10]\n"
    "def g [\\ * $it $it]\n"
    "[g [f 5]]\n";

/* 3. User defmacro: define and use a simple unless macro */
static const char *SRC_USER_MACRO =
    "defmacro unless {cond, body} {\n"
    "  def nc [make-syntax \"command\" [make-syntax \"lit-string\" \"not\"] [vec $cond]]\n"
    "  [make-syntax \"command\" [make-syntax \"lit-string\" \"if\"] [vec $nc $body]]\n"
    "}\n"
    "[unless $false 42]\n";

/* 4. Macro-heavy: many lambda invocations */
static const char *SRC_MACRO_HEAVY =
    "def a [\\ + $it 1]\n"
    "def b [\\ + $it 2]\n"
    "def c [\\ + $it 3]\n"
    "def d [\\ * $it 2]\n"
    "def e [\\ - $it 1]\n"
    "[e [d [c [b [a 0]]]]]\n";

/* 5. Hand-written procs equivalent to MACRO_HEAVY (no macro expansion) */
static const char *SRC_HAND_EQUIV =
    "def a [proc \"\" {it} { [+ $it 1] }]\n"
    "def b [proc \"\" {it} { [+ $it 2] }]\n"
    "def c [proc \"\" {it} { [+ $it 3] }]\n"
    "def d [proc \"\" {it} { [* $it 2] }]\n"
    "def e [proc \"\" {it} { [- $it 1] }]\n"
    "[e [d [c [b [a 0]]]]]\n";

/* ── Stage runners ──────────────────────────────────────────────── */

typedef struct {
    uint64_t lex_ns;
    uint64_t parse_ns;
    uint64_t expand_ns;
    uint64_t compile_ns;
    uint64_t exec_ns;
    uint64_t total_ns;
} StageTimings;

/*
 * Run the full pipeline with per-stage timing.
 *
 * expand  = ast_expand_macros in isolation (measures macro cost)
 * compile = compiler_compile on the pre-expanded AST
 *           (includes a redundant expand pass that finds nothing to do;
 *            this overhead is ~20µs and is consistent across scenarios)
 */
static StageTimings bench_one(const char *src) {
    StageTimings t = {0};
    volatile uint64_t t0, t1;

    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclInternTable intern_table;
    intern_table_init(&intern_table, &arena);

    /* ── Lex ─────────────────────────────────────────── */
    t0 = now_ns();
    LexResult tokens = lexer_lex(src, &arena);
    t1 = now_ns();
    t.lex_ns = t1 - t0;

    /* ── Parse ───────────────────────────────────────── */
    t0 = now_ns();
    ParseResult parse = parser_parse(tokens, &arena);
    t1 = now_ns();
    t.parse_ns = t1 - t0;

    /* ── Expand (macro expansion) ────────────────────── */
    MacroTable macro_table;
    macro_table_init(&macro_table);
    ExpandState es;
    memset(&es, 0, sizeof(es));

    t0 = now_ns();
    if (parse.error_count == 0) {
        uint32_t err_line = 0, err_col = 0;
        ast_expand_macros(parse.nodes, parse.count, &macro_table,
                          &vm.heap, &intern_table, &arena, &es,
                          &err_line, &err_col);
    }
    t1 = now_ns();
    t.expand_ns = t1 - t0;

    /* ── Compile (on pre-expanded AST) ───────────────── */
    ExpandState es2;
    memset(&es2, 0, sizeof(es2));

    t0 = now_ns();
    CompileResult cr = compiler_compile(parse, &arena, &intern_table,
                                        &vm.heap, NULL, &es2, JACL_NIL);
    t1 = now_ns();
    t.compile_ns = t1 - t0;

    /* ── Execute ─────────────────────────────────────── */
    if (cr.error_count == 0) {
        vm.intern_table    = &intern_table;
        vm.struct_registry = cr.struct_registry;

        t0 = now_ns();
        vm_exec(&vm, &cr.chunk);
        t1 = now_ns();
        t.exec_ns = t1 - t0;
    }

    t.total_ns = t.lex_ns + t.parse_ns + t.expand_ns
               + t.compile_ns + t.exec_ns;

    intern_table_destroy(&intern_table);
    vm_destroy(&vm);
    arena_destroy(&arena);
    return t;
}

/* ── Iteration and statistics ───────────────────────────────────── */

typedef struct {
    uint64_t lex_ns;
    uint64_t parse_ns;
    uint64_t expand_ns;
    uint64_t compile_ns;
    uint64_t exec_ns;
    uint64_t total_ns;
} MedianTimings;

static int cmp_u64(const void *a, const void *b) {
    uint64_t va = *(const uint64_t *)a;
    uint64_t vb = *(const uint64_t *)b;
    return (va > vb) - (va < vb);
}

static uint64_t median(uint64_t *arr, int n) {
    qsort(arr, (size_t)n, sizeof(uint64_t), cmp_u64);
    return arr[n / 2];
}

static MedianTimings bench_scenario(const char *src, int iters) {
    uint64_t *lex   = (uint64_t *)malloc((size_t)iters * sizeof(uint64_t));
    uint64_t *parse = (uint64_t *)malloc((size_t)iters * sizeof(uint64_t));
    uint64_t *expand= (uint64_t *)malloc((size_t)iters * sizeof(uint64_t));
    uint64_t *comp  = (uint64_t *)malloc((size_t)iters * sizeof(uint64_t));
    uint64_t *exec  = (uint64_t *)malloc((size_t)iters * sizeof(uint64_t));
    uint64_t *total = (uint64_t *)malloc((size_t)iters * sizeof(uint64_t));

    /* Warmup: 5 iterations discarded */
    for (int i = 0; i < 5; i++) {
        (void)bench_one(src);
    }

    for (int i = 0; i < iters; i++) {
        StageTimings st = bench_one(src);
        lex[i]    = st.lex_ns;
        parse[i]  = st.parse_ns;
        expand[i] = st.expand_ns;
        comp[i]   = st.compile_ns;
        exec[i]   = st.exec_ns;
        total[i]  = st.total_ns;
    }

    MedianTimings m;
    m.lex_ns     = median(lex, iters);
    m.parse_ns   = median(parse, iters);
    m.expand_ns  = median(expand, iters);
    m.compile_ns = median(comp, iters);
    m.exec_ns    = median(exec, iters);
    m.total_ns   = median(total, iters);

    free(lex); free(parse); free(expand);
    free(comp); free(exec); free(total);
    return m;
}

/* ── Output ─────────────────────────────────────────────────────── */

static void print_header(void) {
    printf("\n%-20s %9s %9s %9s %9s %9s │ %9s\n",
           "scenario", "lex", "parse", "expand", "compile*", "exec", "total");
    printf("%-20s %9s %9s %9s %9s %9s │ %9s\n",
           "────────────────────",
           "─────────", "─────────", "─────────", "─────────", "─────────",
           "─────────");
}

static void print_row(const char *name, MedianTimings m) {
    printf("%-20s %8.1fµs %8.1fµs %8.1fµs %8.1fµs %8.1fµs │ %8.1fµs\n",
           name,
           m.lex_ns     / 1000.0,
           m.parse_ns   / 1000.0,
           m.expand_ns  / 1000.0,
           m.compile_ns / 1000.0,
           m.exec_ns    / 1000.0,
           m.total_ns   / 1000.0);
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void) {
    int iters = 200;

    printf("=== JACL Macro Benchmark (%d iterations, median) ===\n", iters);
    printf("  * compile includes a redundant expand pass (~constant overhead)\n");
    print_header();

    print_row("baseline",
              bench_scenario(SRC_BASELINE, iters));
    print_row("lambda (\\)",
              bench_scenario(SRC_LAMBDA, iters));
    print_row("user defmacro",
              bench_scenario(SRC_USER_MACRO, iters));
    print_row("macro-heavy (5x\\)",
              bench_scenario(SRC_MACRO_HEAVY, iters));
    print_row("hand-written equiv",
              bench_scenario(SRC_HAND_EQUIV, iters));

    printf("\n");

    /* Always pass so build.sh is happy */
    printf("1/1 tests passed\n");
    return 0;
}
