# Atom Watchers — Design

> Adds `watch` / `unwatch` to JACL's existing atom primitive. Watchers
> are user-supplied closures that fire when an atom transitions to a
> new value via `reset` or `swap`. Patterned on Clojure's
> `add-watch` / `remove-watch`; explicitly *not* alien-signals-style
> auto-tracked computed values (see § Out of scope).

## Motivation

`atom` / `deref` / `reset` / `swap` already ship — but there's no way
for code to react to atom mutations without polling. Watches unlock:

- `$env` syncing keys to `setenv()`/`unsetenv()` on OS without
  special-casing the language (`SYNTAX.md` §"Environment variables").
- The `$home` / `$pwd` / `$pid` aliases as watchers on `$env` and
  process state.
- User code: e.g. a counter atom whose changes drive a status display,
  cache invalidation, debug logging.

The mechanism is general; `$env` is just one application.

## Surface

```jacl
def counter [atom 0]

watch $counter :log [proc {old new} {
  print "counter: $old → $new"
}]

reset $counter 1               # prints "counter: 0 → 1"
swap $counter [\ + $it 1]      # prints "counter: 1 → 2"

unwatch $counter :log
```

Three new builtins:

- `[watch ATOM KEY FN]` — registers `FN` under `KEY` on `ATOM`.
  `FN` must take exactly two parameters; called as `(FN old new)` on
  every committed transition. Returns nil. Re-registering the same
  `KEY` replaces the previous watcher.
- `[unwatch ATOM KEY]` — removes the watcher at `KEY`. Returns nil
  whether or not a watcher was actually present.
- `[watchers ATOM]` *(optional, v1.1)* — returns a vec of registered
  keys, useful for diagnostics and for `$env`'s built-in watcher
  introspection. Not on the critical path.

The `KEY` is any JACL value compared with `jacl_equal` (atoms, strings,
integers all work). Using an atom keyword like `:log` is idiomatic;
anything goes.

`watch` returns nil rather than an "unwatch handle" — the explicit-key
idiom mirrors Clojure and composes better with JACL's pipe model
(handles don't pipe).

## Storage

Watchers live in a lazily-allocated structure hanging off the atom's
`JaclMutableRef`:

```c
typedef struct JaclWatcherList {
  uint32_t        count;
  uint32_t        capacity;
  JaclVal*        keys;    /* parallel array, GC-traced */
  JaclVal*        fns;     /* parallel array, GC-traced */
  platform_mutex_t lock;   /* short critical sections only */
} JaclWatcherList;
```

The `JaclMutableRef` data area for atoms (type_idx == 0) gains an
extra slot:

```
ref->data[0]   JaclVal   atomic current value      (existing)
ref->data[1]   JaclWatcherList* watchers (or NULL) (new, atom-only)
```

`NULL` until the first `watch` call — atoms that nobody watches pay
zero overhead. The slot is read with `MEM_ACQUIRE` in `reset`/`swap`
so the publish in `OP_WATCH` is observed.

**Why parallel arrays, not a HAMT?** Watcher counts will typically be
< 10. Linear scan for equality lookup is cache-friendly and avoids
HAMT allocation churn on every watch/unwatch. If a real workload pins
this as a bottleneck (unlikely), swap to a HAMT later.

**Why a mutex?** Concurrent `watch`/`unwatch` from different threads
on the same atom is a small but real case, and watcher firing must
see a consistent snapshot. The mutex covers the snapshot copy (see
§Threading); firing happens outside the lock.

## OP_SWAP as CAS-retry

Today's `OP_SWAP` is:

```
1. load old = ATOMIC_LOAD(ref->val)
2. call fn(old) → new
3. ATOMIC_STORE(ref->val, new)
```

Two concurrent swaps racing on the same atom can lose updates: both
read `old`, both compute their `new`, both store — one update is
silently dropped. This was tolerable when nothing observed the
in-between states, but watchers turn lost transitions into lost
notifications.

**Fix (lands as its own commit, before watchers):** wrap in
compare-and-set retry:

```
loop:
  load old = ATOMIC_LOAD(ref->val)
  call fn(old) → new
  if ATOMIC_CAS(ref->val, old, new): commit
  else: retry
```

This matches Clojure `swap!` exactly. Implications:

- The closure can run *more than once* per `[swap ...]` call under
  contention. Closures are expected to be pure. JACL doesn't enforce
  purity, but the same caveat applies as in Clojure; document it.
- The committed old/new pair is unambiguous — exactly the pair seen
  by the CAS that succeeded. Watchers fire with that pair.
- For uncontended swaps (the common case), the CAS succeeds first try
  and there's no perf regression.

`OP_RESET` already does an unconditional atomic store; it stays
unconditional. The watchers fire with `(old_loaded_before_store, new)`.

## Fire semantics

After the value-changing op (`OP_RESET` store, or `OP_SWAP`'s
successful CAS) commits:

1. Acquire `ref->watchers->lock` if non-NULL.
2. Snapshot the keys/fns arrays into stack-allocated locals (or a
   small heap copy if larger than a fixed inline threshold, say 16).
3. Release the lock.
4. For each `(key, fn)` in snapshot order: invoke `fn(old, new)` via
   the existing closure-call machinery, ignoring its return value.

Firing outside the lock means:

- A watcher can `watch`/`unwatch` the same atom from inside its body
  without deadlock.
- Concurrent `watch`/`unwatch` from other threads can mutate the
  *next* fire's watcher set, but won't affect the snapshot in flight.
- An error from a watcher closure surfaces normally (propagates the
  error value); subsequent watchers in the same fire are skipped.

### Re-entrancy

A watcher's body may itself `reset` or `swap` the atom (or any
atom). That triggers a fresh fire cycle for *that* transition. JACL's
existing call-depth limit (`VM_FRAMES_MAX`) catches accidental
infinite recursion cleanly. We allow re-entrancy because banning it
would require sticky per-atom flags whose semantics get confusing
across threads, and the failure mode is loud (stack overflow with
clear error message) rather than silent.

### Ordering

- Per-atom: watchers fire in *commit order* of swaps, because each
  fire happens synchronously on the thread that won the CAS, before
  that op returns. A reader's `[deref $a]` after `[swap $a fn]`
  observes the new value *and* the watchers have fired.
- Within a single fire: keys/fns are walked in insertion order. Same
  key registered twice replaces, doesn't append, so the order is
  stable.
- Cross-atom: no global ordering guarantee. Two atoms swapped from
  two threads can fire watchers in either order.

## GC

Watcher closures must be reachable from `gc_enumerate_roots` or
otherwise pinned for the duration of their registration. Two pieces:

1. `JaclWatcherList` is a GC-managed object (allocated via
   `gc_alloc(OBJ_WATCHER_LIST, ...)`) with a tracer that visits every
   `keys[i]` and `fns[i]`.
2. The atom's `JaclMutableRef` tracer (existing path) is extended to
   visit the watcher-list pointer in `data[1]` for atoms.

No special handling needed on `unwatch` — removing the entry just
drops the closure from the watcher list; if nothing else references
it, GC collects it on the next cycle.

The watcher list itself is never freed eagerly; it lives until the
owning atom is collected.

## Threading semantics — summary

| Operation                                | Guarantee                                                    |
|------------------------------------------|--------------------------------------------------------------|
| Concurrent `swap` on same atom           | Both commit; both watchers fire; one runs before the other   |
| Concurrent `swap` on different atoms     | Each atom's watchers fire on its own committer thread        |
| `watch`/`unwatch` during a fire          | Affects *next* fire, not the in-flight snapshot              |
| Watcher reads `deref` of same atom       | Always observes a value ≥ the `new` that triggered the fire  |
| Watcher swaps same atom (re-entry)       | Triggers fresh fire; bounded by `VM_FRAMES_MAX`              |
| Watcher errors                           | Error value propagates; remaining watchers in this fire skip |

## Out of scope (v1)

- **Auto-tracked computed values** (alien-signals / SolidJS / Vue
  refs). These auto-register dependencies via a thread-local
  "currently tracking" pointer and propagate invalidations through a
  DAG. They're powerful for UI/reactive systems but require
  significant infrastructure (per-worker tracking, atomic dep-graph
  mutation, cycle detection) and JACL doesn't have a strong driving
  use case. If we want this later, it can sit on top of `watch` as a
  library `derived` form rather than a new VM primitive.
- **Watcher priorities / dependency ordering**. Insertion order is
  the only guarantee.
- **Async / queued watchers**. All watchers fire synchronously on the
  committing thread. If users need async, they `spawn` from inside
  the watcher.
- **Watch on boxes/cells**. Only atoms support `watch`. Boxes/cells
  are typed mutable bindings; adding watchers there would be useful
  but is a separate design (the watcher-list slot only makes sense
  for `type_idx == 0` atoms).
- **`with-env` and `$home`/`$pwd`/`$pid`**. These build on `watch` but
  are tracked under §1 of `NOT_IMPLEMENTED.md` independently.

## Test plan

`test/jacl/atom_*.jacl` (extending the existing fixtures):

- `atom_watch_basic` — register, swap, observe old/new in printed output.
- `atom_watch_reset` — `reset` also fires watchers.
- `atom_watch_replace_key` — re-registering the same key replaces the fn.
- `atom_watch_unwatch` — `unwatch` stops further notifications.
- `atom_watch_multi_key` — two keys, both fire, insertion order.
- `atom_watch_reentry` — watcher body swaps same atom; nested fire works,
  doesn't deadlock.
- `atom_watch_no_watcher_cost` — atom with no watchers never allocates a
  watcher list (asserted via a counter or stat, if exposed).

Plus a chaos / concurrency check:

- `test_chaos_atom_watch` — N threads each swap an atom in a loop; a
  single watcher counts notifications. Asserts notifications equals
  total successful swaps (every CAS commit fires exactly once).

## Open questions for v1

- *Should `unwatch` return whether a watcher was actually removed?*
  Clojure returns the atom; we'll return nil for simplicity. Revisit
  if a test pattern wants the boolean.
- *Should `watch` accept a string-only key for performance?* No —
  `jacl_equal` is cheap for the small key counts we expect, and
  allowing any value keeps the surface uniform.
- *Should the watcher fn signature include the atom itself?* Closures
  can capture the atom via lexical scope; Clojure passes it because
  Clojure's fns are values without lexical scope. Skip.
