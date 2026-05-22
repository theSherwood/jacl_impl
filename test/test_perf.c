/*
 * test_perf.c — Runtime/GC perf benchmark harness.
 *
 * Distinct from test_bench.c (which times the compile pipeline). This
 * harness compiles a scenario once, then submits the resulting closure
 * to a fresh Runtime N times. Each iteration is timed end-to-end; perf
 * snapshots bracket the timed region so we capture cumulative GC cycles,
 * allocations, steals, etc.
 *
 * Output: JSONL on stdout (or BENCH_JSON_OUT=path to write to a file).
 * Each line is one scenario. The bench-diff tool consumes these.
 *
 * Always exits 0 unless a scenario crashes. Failure to find scenarios is
 * not an error — useful for incremental development.
 */

#define _POSIX_C_SOURCE 199309L
#include "test_helpers.h"
#include "../src/jacl.h"
#include <time.h>

/* ── Config ─────────────────────────────────────────────────────── */

#define BENCH_DIR        "test/jacl/bench"
#define WARMUP_ITERS_DEFAULT 2
#define TIMED_ITERS_DEFAULT  5
#define TIMED_ITERS_MAX      8192
#define COMPLETION_MS        30000 /* 30s per iter — bench scenarios can be slow */

/* Iteration counts can be overridden via env for profiling runs:
 *   BENCH_WARMUP_ITERS, BENCH_TIMED_ITERS
 * Output is still 5-number-summary stats so JSON output stays comparable. */
static int s_warmup_iters = WARMUP_ITERS_DEFAULT;
static int s_timed_iters  = TIMED_ITERS_DEFAULT;

/* ── Timing helpers ─────────────────────────────────────────────── */

static uint64_t __attribute__((noinline)) now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void* a, const void* b) {
    uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

/* ── File reading ───────────────────────────────────────────────── */

static char* slurp_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

/* ── Env copy (subset of test_jacl_harness's helper) ─────────────── */

static void copy_env(VM* dst, VM* src) {
    for (uint32_t i = 0; i < src->env.count; i++) {
        if (dst->env.count >= dst->env.cap) {
            uint32_t nc = dst->env.cap * 2;
            JaclVal* nn = (JaclVal*)arena_alloc(dst->arena, nc * sizeof(JaclVal));
            JaclVal* nv = (JaclVal*)arena_alloc(dst->arena, nc * sizeof(JaclVal));
            memcpy(nn, dst->env.names, dst->env.count * sizeof(JaclVal));
            memcpy(nv, dst->env.values, dst->env.count * sizeof(JaclVal));
            dst->env.names = nn;
            dst->env.values = nv;
            dst->env.cap = nc;
        }
        int found = 0;
        for (uint32_t j = 0; j < dst->env.count; j++) {
            if (dst->env.names[j] == src->env.names[i]) {
                dst->env.values[j] = src->env.values[i];
                found = 1; break;
            }
        }
        if (!found) {
            dst->env.names[dst->env.count]  = src->env.names[i];
            dst->env.values[dst->env.count] = src->env.values[i];
            dst->env.count++;
        }
    }
}

/* Sink print fn — swallow output so scenarios printing diagnostics
 * don't blow up an in-memory buffer over many iterations. */
static void sink_print(const char* text, uint32_t len, void* ctx) {
    (void)text; (void)len; (void)ctx;
}

/* ── One scenario: compile once, run N times, snapshot, emit JSON ── */

static int run_scenario(const char* path, const char* name,
                        const char* git_sha, FILE* out) {
    char* source = slurp_file(path);
    if (!source) {
        fprintf(stderr, "  %s: could not read %s\n", name, path);
        return 0;
    }

    /* Compile once on a temp VM. */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    LexResult tokens   = lexer_lex(source, &arena);
    ParseResult parse  = parser_parse(tokens, &arena);
    if (parse.error_count > 0) {
        fprintf(stderr, "  %s: parse error\n", name);
        free(source);
        vm_destroy(&vm);
        arena_destroy(&arena);
        return 0;
    }
    JaclInternTable intern_table;
    intern_table_init(&intern_table, &arena);
    CompileResult cr = compiler_compile(parse, &arena, &intern_table,
                                         &vm.heap, NULL, NULL, JACL_NIL);
    if (cr.error_count > 0) {
        fprintf(stderr, "  %s: compile error: %s\n", name,
                cr.error_message ? cr.error_message : "(unknown)");
        free(source);
        vm_destroy(&vm);
        arena_destroy(&arena);
        return 0;
    }
    vm.intern_table = &intern_table;

    /* Extract the closure to submit (suspending) or wrap the chunk
     * (non-suspending). Same logic as test_jacl_harness.c. */
    JaclClosure* closure = NULL;
    if (cr.suspending) {
        VMResult r = vm_exec(&vm, &cr.chunk);
        if (r != VM_OK || vm.stack_top == 0 || !jacl_is_closure(vm.stack[0])) {
            fprintf(stderr, "  %s: failed to extract suspending closure\n", name);
            free(source); vm_destroy(&vm); arena_destroy(&arena);
            return 0;
        }
        closure = jacl_as_closure(vm.stack[0]);
    } else {
        closure = (JaclClosure*)gc_alloc(&vm.heap, OBJ_CLOSURE, sizeof(JaclClosure));
        memset(closure, 0, sizeof(JaclClosure));
        closure->chunk = cr.chunk;
        closure->param_count = 0;
        closure->upvalue_count = 0;
    }

    /* Stand up a runtime mirroring test_jacl_harness defaults. */
    Runtime rt;
    runtime_init(&rt, 2);
    JaclCtxPool ctx_pools[16];
    for (int i = 0; i < rt.num_workers; i++) {
        rt.workers[i].vm.print_fn        = sink_print;
        rt.workers[i].vm.print_ctx       = NULL;
        rt.workers[i].vm.intern_table    = &intern_table;
        rt.workers[i].vm.struct_registry = cr.struct_registry;
        ctx__init_vm(&rt.workers[i].vm, &ctx_pools[i]);
        copy_env(&rt.workers[i].vm, &vm);
    }

    uint64_t *timings = (uint64_t *)calloc((size_t)s_timed_iters, sizeof(uint64_t));
    int total_iters = s_warmup_iters + s_timed_iters;
    int ok = 1;

    JaclPerfSnapshot pre = jacl_perf_snapshot(&rt);

    for (int iter = 0; iter < total_iters; iter++) {
        JaclVal completion = jacl_future(&rt.workers[0].vm.heap);
        JaclFuture* cfut   = jacl_as_future(completion);

        /* Pin so concurrent GC doesn't reclaim the future while we're
         * polling it via a raw C pointer (AUDIT.md #0c). */
        uint32_t pin = runtime_pin_value(&rt, completion);

        /* worker 0's vm.ctx is mutated by ctx_unfork from the worker
         * thread between iterations (release-store on each task); read
         * with matching acquire so TSAN sees the happens-before. The
         * worker is idle between iters, so the read is unambiguous. */
        JaclVal w0_ctx = (JaclVal)ATOMIC_LOAD_EXPLICIT(
            (volatile uint64_t*)&rt.workers[0].vm.ctx, MEM_ACQUIRE);
        uint64_t t0 = now_ns();
        runtime__submit_spawn_task(&rt, closure, completion, w0_ctx);

        /* Busy-spin on future state for nanosecond timing resolution.
         * SLEEP_MILLISECONDS(1) would impose a ~1ms floor (worse on
         * Linux where nanosleep rounds up to the next jiffy), making
         * sub-ms scenarios unmeasurable. The test thread has no other
         * work between submit and resolve, so spinning is the right
         * tradeoff for a perf bench. Clock is sampled every 1024 spins
         * to keep clock_gettime out of the hot path. */
        const uint64_t budget_ns = (uint64_t)COMPLETION_MS * 1000000ULL;
        int timed_out = 1;
        uint64_t spin = 0;
        for (;;) {
            uint32_t st = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_ACQUIRE);
            if (st == FUTURE_RESOLVED || st == FUTURE_ERROR) {
                timed_out = 0;
                break;
            }
            if (((spin++) & 1023) == 0) {
                if (now_ns() - t0 > budget_ns) break;
            }
        }
        uint64_t dt = now_ns() - t0;

        if (timed_out) {
            fprintf(stderr, "  %s iter %d: timed out\n", name, iter);
            runtime_unpin_value(&rt, pin);
            ok = 0;
            break;
        }
        uint32_t final = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_RELAXED);
        if (final == FUTURE_ERROR) {
            const char* err = NULL;
            for (int i = 0; i < rt.num_workers; i++) {
                if (rt.workers[i].vm.error_message) {
                    err = rt.workers[i].vm.error_message; break;
                }
            }
            fprintf(stderr, "  %s iter %d: runtime error: %s\n", name, iter,
                    err ? err : "(unknown)");
            runtime_unpin_value(&rt, pin);
            ok = 0;
            break;
        }

        if (iter >= s_warmup_iters) {
            timings[iter - s_warmup_iters] = dt;
        }
        runtime_unpin_value(&rt, pin);
    }

    if (ok) {
        JaclPerfSnapshot post = jacl_perf_snapshot(&rt);

        qsort(timings, (size_t)s_timed_iters, sizeof(uint64_t), cmp_u64);
        uint64_t wall_min    = timings[0];
        uint64_t wall_median = timings[s_timed_iters / 2];
        uint64_t wall_max    = timings[s_timed_iters - 1];

        fprintf(out,
            "{"
            "\"scenario\":\"%s\","
            "\"git_sha\":\"%s\","
            "\"warmup_iters\":%d,"
            "\"timed_iters\":%d,"
            "\"wall_ns_min\":%llu,"
            "\"wall_ns_median\":%llu,"
            "\"wall_ns_max\":%llu,"
            "\"delta\":{"
              "\"gc_cycles\":%llu,"
              "\"gc_total_ns\":%llu,"
              "\"gc_max_ns\":%llu,"
              "\"gc_mark_rounds\":%llu,"
              "\"bytes_allocated\":%llu,"
              "\"allocs\":%llu,"
              "\"slow_path_allocs\":%llu,"
              "\"blocks_scanned\":%llu,"
              "\"lines_scanned\":%llu,"
              "\"blocks_rejected_fast\":%llu,"
              "\"tasks_executed\":%llu,"
              "\"steal_attempts\":%llu,"
              "\"steal_successes\":%llu,"
              "\"idle_sleep_ns\":%llu,"
              "\"inbox_pops\":%llu,"
              "\"pinned_inbox_pops\":%llu"
            "},"
            "\"final\":{"
              "\"current_heap_blocks\":%u"
            "}"
            "}\n",
            name, git_sha, s_warmup_iters, s_timed_iters,
            (unsigned long long)wall_min,
            (unsigned long long)wall_median,
            (unsigned long long)wall_max,
            (unsigned long long)(post.gc.total_cycles - pre.gc.total_cycles),
            (unsigned long long)(post.gc.total_cycle_ns - pre.gc.total_cycle_ns),
            (unsigned long long)post.gc.max_cycle_ns,
            (unsigned long long)(post.gc.total_mark_rounds - pre.gc.total_mark_rounds),
            (unsigned long long)(post.total_bytes_allocated - pre.total_bytes_allocated),
            (unsigned long long)(post.total_allocs - pre.total_allocs),
            (unsigned long long)(post.slow_path_allocs - pre.slow_path_allocs),
            (unsigned long long)(post.blocks_scanned - pre.blocks_scanned),
            (unsigned long long)(post.lines_scanned - pre.lines_scanned),
            (unsigned long long)(post.blocks_rejected_fast - pre.blocks_rejected_fast),
            (unsigned long long)(post.workers_total.tasks_executed -
                                 pre.workers_total.tasks_executed),
            (unsigned long long)(post.workers_total.steal_attempts -
                                 pre.workers_total.steal_attempts),
            (unsigned long long)(post.workers_total.steal_successes -
                                 pre.workers_total.steal_successes),
            (unsigned long long)(post.workers_total.idle_sleep_ns -
                                 pre.workers_total.idle_sleep_ns),
            (unsigned long long)(post.workers_total.inbox_pops -
                                 pre.workers_total.inbox_pops),
            (unsigned long long)(post.workers_total.pinned_inbox_pops -
                                 pre.workers_total.pinned_inbox_pops),
            post.current_heap_blocks);
        fflush(out);
    }

    /* Defensive quiesce: wait briefly so any post-resolution worker work
     * (continuation pumps, GC follow-up) settles before we tear down. */
    SLEEP_MILLISECONDS(50);

    runtime_destroy(&rt);
    free(timings);
    free(source);
    vm_destroy(&vm);
    arena_destroy(&arena);
    return ok;
}

/* ── Main: iterate scenarios ─────────────────────────────────────── */

typedef struct {
    const char* name;
    const char* file;
} Scenario;

static const Scenario SCENARIOS[] = {
    { "collection_churn",    BENCH_DIR "/collection_churn.jacl" },
    { "spawn_chain",         BENCH_DIR "/spawn_chain.jacl" },
    { "box_churn",           BENCH_DIR "/box_churn.jacl" },
    { "string_concat",       BENCH_DIR "/string_concat.jacl" },
    { "box_literal_churn",   BENCH_DIR "/box_literal_churn.jacl" },
    { "fib_recursive",       BENCH_DIR "/fib_recursive.jacl" },
    { "sieve_primes",        BENCH_DIR "/sieve_primes.jacl" },
    { "map_lookup_hot",      BENCH_DIR "/map_lookup_hot.jacl" },
    { "parallel_map_reduce", BENCH_DIR "/parallel_map_reduce.jacl" },
};

int main(void) {
    /* Iteration overrides for profiling runs */
    const char* w_env = getenv("BENCH_WARMUP_ITERS");
    const char* t_env = getenv("BENCH_TIMED_ITERS");
    /* Comma-separated allowlist of scenarios; if set, others are skipped.
     * Useful for isolating one scenario under a profiler. */
    const char* only_env = getenv("BENCH_SCENARIOS");
    if (w_env && *w_env) {
        int n = atoi(w_env);
        if (n >= 0 && n <= TIMED_ITERS_MAX) s_warmup_iters = n;
    }
    if (t_env && *t_env) {
        int n = atoi(t_env);
        if (n >= 1 && n <= TIMED_ITERS_MAX) s_timed_iters = n;
    }

    const char* sha_env = getenv("BENCH_GIT_SHA");
    char sha[64];
    if (sha_env && *sha_env) {
        strncpy(sha, sha_env, sizeof(sha) - 1);
        sha[sizeof(sha) - 1] = '\0';
    } else {
        FILE* p = popen("git rev-parse --short HEAD 2>/dev/null", "r");
        if (p) {
            if (!fgets(sha, sizeof(sha), p)) sha[0] = '\0';
            pclose(p);
            sha[strcspn(sha, "\n")] = '\0';
        } else {
            sha[0] = '\0';
        }
    }

    const char* out_path = getenv("BENCH_JSON_OUT");
    FILE* out = stdout;
    if (out_path && *out_path) {
        out = fopen(out_path, "a");
        if (!out) {
            fprintf(stderr, "could not open BENCH_JSON_OUT=%s; using stdout\n",
                    out_path);
            out = stdout;
        }
    }

    int passed = 0;
    int ran    = 0;
    int total  = (int)(sizeof(SCENARIOS) / sizeof(SCENARIOS[0]));
    for (int i = 0; i < total; i++) {
        if (only_env && *only_env && !strstr(only_env, SCENARIOS[i].name)) {
            continue;
        }
        ran++;
        fprintf(stderr, "  %-30s ", SCENARIOS[i].name);
        if (run_scenario(SCENARIOS[i].file, SCENARIOS[i].name, sha, out)) {
            fprintf(stderr, "OK\n");
            passed++;
        } else {
            fprintf(stderr, "FAIL\n");
        }
    }
    if (ran == 0) ran = total; /* avoid 0/0 in output */

    if (out != stdout) fclose(out);
    fprintf(stderr, "\n%d/%d scenarios passed\n", passed, ran);
    /* Pass the build-runner expectation (build.sh greps for "x/y passed"). */
    printf("%d/%d tests passed\n", passed, ran);
    return passed == ran ? 0 : 1;
}
