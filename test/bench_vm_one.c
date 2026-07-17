/*
 * bench_vm_one.c — standalone old-VM execution timer for a single .jacl file.
 *
 * Companion to runtime/harness/src/bin/bench_svm.rs (the SVM-backend timer), so
 * the two backends can be timed on the *same* scaled scenario files. Uses the
 * public embed API (`jacl_vm_new` / `jacl_eval_file`): a fresh VM per iteration
 * (compile + run), min/median over N — for the scaled workloads compile is a
 * sub-millisecond constant, so this tracks execution just like test_perf's
 * compile-once medians.
 *
 * The guest's own stdout (its final `print`) is redirected to a temp file and
 * the last line is reported back as "output" for cross-backend equality checks.
 *
 * Usage: bench_vm_one <file.jacl>...
 * Env:   BENCH_ITERS (default 25), BENCH_WARMUP (default 3),
 *        BENCH_JSON_OUT (path; default stdout).
 */
#define _POSIX_C_SOURCE 200809L
#include "../include/jacl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

/* The implementation exports `_str`-suffixed symbols; the public header name
 * `jacl_error_message` has no backing definition in the archive. */
extern const char* jacl_error_message_str(JaclVM* vm, JaclVal err);
#define jacl_error_message jacl_error_message_str

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void* a, const void* b) {
    uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

/* Run `path` once on a fresh VM, capturing the guest's stdout into `outbuf`
 * (last non-empty line). Returns elapsed ns, or 0 on error. */
static uint64_t run_one(const char* path, char* outbuf, size_t outcap, int capture) {
    fflush(stdout);
    int saved = -1, devfd = -1;
    char tmpl[] = "/tmp/bench_vm_out_XXXXXX";
    int tmpfd = -1;
    if (capture) {
        tmpfd = mkstemp(tmpl);
        saved = dup(1);
        dup2(tmpfd, 1);
    } else {
        saved = dup(1);
        devfd = open("/dev/null", O_WRONLY);
        dup2(devfd, 1);
    }

    /* Ample heap so a large scaled workload degrades gracefully instead of
     * OOM-segfaulting; keeps the comparison about speed, not heap limits. */
    JaclConfig cfg = {0};
    cfg.max_heap_size = (size_t)512 * 1024 * 1024;
    cfg.max_handles = 4096;
    JaclVM* vm = jacl_vm_new_ex(&cfg);
    uint64_t t0 = now_ns();
    JaclVal r = jacl_eval_file(vm, path);
    uint64_t dt = now_ns() - t0;
    int err = jacl_is_error(r);
    const char* emsg = err ? jacl_error_message(vm, r) : NULL;
    char errcopy[256] = {0};
    if (emsg) { strncpy(errcopy, emsg, sizeof(errcopy) - 1); }

    fflush(stdout);
    dup2(saved, 1);
    close(saved);
    if (devfd >= 0) close(devfd);

    if (capture && tmpfd >= 0) {
        lseek(tmpfd, 0, SEEK_SET);
        char buf[4096];
        ssize_t n = read(tmpfd, buf, sizeof(buf) - 1);
        if (n < 0) n = 0;
        buf[n] = '\0';
        /* last non-empty line */
        char* last = buf;
        for (char* p = buf; *p; p++) {
            if (*p == '\n' && *(p + 1)) last = p + 1;
        }
        char* nl = strchr(last, '\n');
        if (nl) *nl = '\0';
        strncpy(outbuf, last, outcap - 1);
        outbuf[outcap - 1] = '\0';
        close(tmpfd);
        unlink(tmpl);
    }

    jacl_vm_free(vm);
    if (err) {
        fprintf(stderr, "  %s: eval error: %s\n", path, errcopy);
        return 0;
    }
    return dt;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: bench_vm_one <file.jacl>...\n"); return 2; }
    int iters  = getenv("BENCH_ITERS")  ? atoi(getenv("BENCH_ITERS"))  : 25;
    int warmup = getenv("BENCH_WARMUP") ? atoi(getenv("BENCH_WARMUP")) : 3;
    if (iters < 1) iters = 1;

    const char* out_path = getenv("BENCH_JSON_OUT");
    FILE* out = out_path ? fopen(out_path, "w") : stdout;
    if (!out) { out = stdout; }

    fprintf(stderr, "%-24s %12s %12s %14s\n", "scenario", "min_us", "med_us", "output");
    fprintf(stderr, "%.*s\n", 66, "----------------------------------------------------------------------");

    uint64_t* t = (uint64_t*)calloc((size_t)iters, sizeof(uint64_t));
    for (int a = 1; a < argc; a++) {
        const char* path = argv[a];
        const char* base = strrchr(path, '/');
        base = base ? base + 1 : path;
        char name[128];
        strncpy(name, base, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        char* dot = strstr(name, ".jacl");
        if (dot) *dot = '\0';

        char output[256] = {0};
        int ok = 1;
        for (int w = 0; w < warmup; w++) {
            if (run_one(path, output, sizeof(output), w == 0) == 0) { ok = 0; break; }
        }
        if (!ok) {
            fprintf(out, "{\"scenario\":\"%s\",\"status\":\"error\"}\n", name);
            continue;
        }
        for (int i = 0; i < iters; i++) {
            t[i] = run_one(path, output, sizeof(output), 0);
            if (t[i] == 0) { ok = 0; break; }
        }
        if (!ok) {
            fprintf(out, "{\"scenario\":\"%s\",\"status\":\"error\"}\n", name);
            continue;
        }
        qsort(t, (size_t)iters, sizeof(uint64_t), cmp_u64);
        uint64_t mn = t[0], md = t[iters / 2];
        fprintf(stderr, "%-24s %12.1f %12.1f %14s\n", name, mn / 1000.0, md / 1000.0, output);
        fprintf(out,
            "{\"scenario\":\"%s\",\"status\":\"ok\",\"output\":\"%s\","
            "\"wall_ns_min\":%llu,\"wall_ns_median\":%llu,\"iters\":%d}\n",
            name, output, (unsigned long long)mn, (unsigned long long)md, iters);
    }
    free(t);
    if (out != stdout) fclose(out);
    return 0;
}
