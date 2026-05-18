/* Field lists for struct-drift detection between the unity build and jacl.h.
 *
 * Each entry expands to whatever X(struct, field) is defined as at the use
 * site — embed.c expands these into `offsetof` getter functions, and
 * test_struct_sizes.c expands them into checks against the jacl.h view.
 * If a field is added to one definition but not the other, one of those
 * two compilations fails with "no member named …" — catching the drift
 * before it can silently corrupt memory.
 *
 * Adding a field to Runtime / WorkerThread:
 *   1. Add it to the struct in src/runtime.c.
 *   2. Add it to the struct in src/jacl.h.
 *   3. Add an X(struct, field) line below.
 *
 * Steps 1+2 mirror the existing dual-definition contract; step 3 makes
 * test_struct_sizes.c catch any omission of either step.
 *
 * This header is intentionally not guarded — both consumers may include it
 * multiple times with different X definitions.
 */

#ifndef RUNTIME_FIELDS
#define RUNTIME_FIELDS(X) \
    X(Runtime, workers) \
    X(Runtime, num_workers) \
    X(Runtime, global_epoch) \
    X(Runtime, gc_running) \
    X(Runtime, gc_active) \
    X(Runtime, block_pool) \
    X(Runtime, shutdown) \
    X(Runtime, inbox) \
    X(Runtime, inbox_count) \
    X(Runtime, inbox_cap) \
    X(Runtime, inbox_mutex) \
    X(Runtime, work_cv) \
    X(Runtime, gc_thief_public_ids) \
    X(Runtime, gc_thief_private_ids) \
    X(Runtime, external_roots) \
    X(Runtime, external_root_count) \
    X(Runtime, external_root_cap) \
    X(Runtime, external_roots_mutex) \
    X(Runtime, timer_thread) \
    X(Runtime, timer_thread_started) \
    X(Runtime, timer_shutdown) \
    X(Runtime, timer_mutex) \
    X(Runtime, timer_cv) \
    X(Runtime, timer_head) \
    X(Runtime, gc_stats)
#endif

#ifndef WORKER_FIELDS
#define WORKER_FIELDS(X) \
    X(WorkerThread, public_deque) \
    X(WorkerThread, private_deque) \
    X(WorkerThread, grey_buf) \
    X(WorkerThread, remembered_set) \
    X(WorkerThread, currently_executing) \
    X(WorkerThread, thread_epoch) \
    X(WorkerThread, vm) \
    X(WorkerThread, runtime) \
    X(WorkerThread, id) \
    X(WorkerThread, thread) \
    X(WorkerThread, arena) \
    X(WorkerThread, steal_ids) \
    X(WorkerThread, pinned_inbox) \
    X(WorkerThread, pinned_inbox_count) \
    X(WorkerThread, pinned_inbox_cap) \
    X(WorkerThread, pinned_inbox_mutex) \
    X(WorkerThread, retired_tasks) \
    X(WorkerThread, retired_epochs) \
    X(WorkerThread, retired_count) \
    X(WorkerThread, retired_cap) \
    X(WorkerThread, stats)
#endif
