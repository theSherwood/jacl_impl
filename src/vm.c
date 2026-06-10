/*
 * JACL Virtual Machine
 *
 * Stack-based bytecode interpreter. Executes BytecodeChunk instructions
 * using a fixed-size operand stack.
 */

#ifndef VM_C
#define VM_C

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>

/* §D.1: debug-only sanity check at every unchecked `jacl_as_*`
 * extraction. The compiler/typer is supposed to guarantee tag
 * correctness before emitting these opcodes, but §D.6 demonstrated
 * the trust can break (a compiler-level coordination bug between
 * spawn-body SM-wrapping and inner-call frame management surfaced
 * as a wild-pointer SEGV in OP_SET_STATE_FIELD). This assertion
 * catches the next such regression at the call site rather than as
 * a downstream crash. Compiles to nothing under -DNDEBUG. */
#define JACL_ASSERT_TAG(v, pred) assert(pred(v))

/* §D.2: balance the operand stack on error returns inside an opcode
 * handler. Each opcode that pops or pushes before potentially erroring
 * should declare `uint32_t saved_stack_top = vm->stack_top;` at the
 * top of its case, then use VM_ERROR(vm, ...) for any error return
 * after that point. The macro resets stack_top to the captured value,
 * sets the error message, and returns VM_RUNTIME_ERROR — eliminating
 * the cumulative slot drift documented in AUDIT.md §D.2. */
#define VM_ERROR(vm_, ...) do { \
    (vm_)->stack_top = saved_stack_top; \
    vm__set_error((vm_), __VA_ARGS__); \
    return VM_RUNTIME_ERROR; \
} while (0)

/* --- Stack size --- */

#define VM_STACK_MAX 1024
#define VM_FRAMES_MAX 256

/* --- Environment initial capacity --- */

#define VM_ENV_INIT_CAP 16

/* --- Result codes --- */

typedef enum {
  VM_OK,
  VM_RUNTIME_ERROR,
  VM_STACK_OVERFLOW,
  VM_YIELD          /* generator yielded a value */
} VMResult;

/* --- Print callback --- */

typedef void (*VMPrintFn)(const char* text, uint32_t len, void* ctx);

/* --- Environment --- */

typedef struct {
  JaclVal*  names;    /* inline string names */
  JaclVal*  values;   /* corresponding values */
  uint32_t  count;
  uint32_t  cap;
} Environment;

/* --- Call frame --- */

typedef struct {
  JaclClosure*   closure;     /* closure being executed */
  uint8_t*       return_ip;   /* caller's ip to restore on return */
  uint32_t       stack_base;  /* first slot for this frame's locals */
  BytecodeChunk* chunk;       /* chunk being executed */
} CallFrame;

/* --- Stack trace --- */

#define VM_STACK_TRACE_MAX 32

typedef struct {
  const char* function_name;
  uint32_t    line_number;
} StackTraceEntry;

typedef struct {
  StackTraceEntry entries[VM_STACK_TRACE_MAX];
  uint32_t        count;
} StackTrace;

/* Forward declaration: defined in syntax.c (later in unity build) */
JaclVal jacl_gensym_next(const char *prefix, uint32_t prefix_len,
                         ThreadHeap *heap, JaclInternTable *intern,
                         uint32_t *gensym_counter,
                         uint32_t scope_mark, const char **err);

/* --- Ctx allocation (formerly: lockless free-list pool) ---
 *
 * History: this used to be a per-worker lockless free-list pool that
 * pre-allocated a handful of ctx HeapRecords and recycled them across
 * ctx_fork/ctx_unfork. AUDIT.md §18 (2026-05-14) documented the design
 * flaw: SM-compiled functions persist a pool-backed `HeapRecord*` into
 * a state field across suspensions (compiler.c emits OP_GET_CTX →
 * state field on suspension, OP_SET_CTX restoring on resume). On
 * resume — possibly on a different worker — vm.ctx points into the
 * original worker's pool. If that worker's task body subsequently does
 * ctx_unfork → ctx_pool_free, the slot goes back onto its free list
 * and can be re-issued for an unrelated future ctx. The resumed SM
 * then reads recycled bytes. The GC keeps the memory alive (free list
 * is a root) but does not police pool reuse.
 *
 * Fix: drop the pool. ctx_pool_alloc is now just gc_alloc; the freed
 * memory becomes unreachable when vm.ctx is restored at ctx_unfork
 * and is reclaimed by the GC on the next cycle. The free-list NIL
 * writes and CAS pushes are gone — both their race surface and their
 * (modest) cost. The JaclCtxPool struct stays so callers don't have
 * to thread struct_size/type_idx through their own state, but
 * free_list_head is unused.
 *
 * Perf: ctx_pool_alloc previously did atomic CAS + memset(struct_size).
 * gc_alloc does a bump + a header write; the previously-allocated slot
 * memory is already zeroed (block init memsets the whole 64KB; sweep
 * zeros dead objects). So this should be perf-neutral or a small win
 * in addition to closing the race. Validated by the bench pair in
 * docs/profiles/2026-05-14_drop_ctx_pool_*.
 */

typedef struct {
    uintptr_t free_list_head; /* unused, retained for ABI/struct-size */
    uint32_t struct_size;
    uint32_t type_idx;
    StructTypeDef *sdef;
} JaclCtxPool;

void ctx_pool_init(JaclCtxPool *pool, ThreadHeap *heap,
                   StructTypeRegistry *reg) {
    (void)heap;
    uint32_t idx = reg->ctx_type_idx;
    StructTypeDef *sdef = reg->defs[idx];
    pool->free_list_head = 0;
    pool->struct_size = sdef->total_size;
    pool->type_idx = idx;
    pool->sdef = sdef;
}

HeapRecord *ctx_pool_alloc(JaclCtxPool *pool, ThreadHeap *heap) {
    HeapRecord *s = (HeapRecord *)gc_alloc(heap, OBJ_HEAP_RECORD,
                      sizeof(HeapRecord) + pool->struct_size);
    if (!s) return NULL;
    s->type_idx = pool->type_idx;
    s->total_size = pool->struct_size;
    /* gc_alloc returns zeroed memory (block init memsets new blocks;
     * sweep zeros swept objects) — no explicit memset needed. */
    return s;
}

void ctx_pool_free(JaclCtxPool *pool, HeapRecord *s) {
    (void)pool;
    (void)s;
    /* No-op. The caller has restored vm.ctx to saved_ctx; this
     * HeapRecord is now unreachable from any root and will be
     * reclaimed by the next GC cycle. See §18. */
}

/* --- VM state --- */

typedef struct {
  JaclVal        stack[VM_STACK_MAX];
  uint32_t       stack_top;   /* index of next free slot */
  CallFrame      frames[VM_FRAMES_MAX];
  uint32_t       frame_count;
  uint8_t*       ip;          /* instruction pointer */
  BytecodeChunk* chunk;
  VMPrintFn      print_fn;   /* output callback, defaults to stdout */
  void*          print_ctx;  /* user context for print callback */
  Environment    env;
  arena_t*       arena;
  BlockPool      block_pool;    /* GC block pool (owned by VM) */
  ThreadHeap     heap;          /* GC heap for runtime allocations */
  JaclInternTable* intern_table;  /* shared intern table for concat/interning */
  BytecodeChunk* top_chunk;       /* top-level chunk for GC root scanning */
  GreyBuffer*    grey_buf;       /* write barrier target (NULL in single-threaded) */
  RememberedSet* remembered_set; /* generational write barrier target (NULL in single-threaded) */
  uint32_t  *gc_active_ptr; /* pointer to runtime's gc_active, atomic (NULL in single-threaded) */
  void*          runtime;        /* Runtime pointer for concurrent GC trigger (NULL in single-threaded) */
  int            worker_id;      /* Worker thread ID for task pinning (-1 if not on a worker) */
  const char*    error_message;  /* last error message, or NULL */
  uint32_t       error_line;     /* source line of last error */
  StackTrace     stack_trace;    /* most recent error's trace */
  StructTypeRegistry* struct_registry; /* struct type metadata from compiler */
  JaclCtxPool *ctx_pool;       /* ctx struct pool (NULL until initialized) */
  JaclVal    ctx;              /* current implicit context struct (never NIL during execution) */
  JaclVal    saved_ctx[8];     /* with-ctx save stack for nested forks */
  uint8_t    saved_ctx_count;  /* number of entries in saved_ctx */
  JaclVal*   gc_handle_slots;  /* external GC root handles (owned by embedding layer) */
  uint32_t   gc_handle_count;  /* number of slots in gc_handle_slots */
  /* Native function registry (owned by embedding layer) */
  JaclVal    (*call_native)(void* ctx, uint32_t fn_index, JaclVal* args, int argc);
  void*      native_fn_ctx;     /* JaclVM_s* for dispatch callback */
  int8_t*    native_fn_arities; /* arity per native fn (-1 = variadic) */
  uint32_t   native_fn_count;   /* number of registered native functions */
  /* Spread count side buffer for OP_SPREAD / OP_CALL_SPREAD / OP_FOLD_SPREAD */
  uint32_t   spread_counts[32]; /* small stack of spread element counts */
  uint32_t   spread_count_top;  /* top index in spread_counts */
  /* Generator/yield support */
  JaclVal    yield_value;        /* yielded value (set by OP_YIELD_SM) */
  /* Multi-slot yield channel for struct stream elements (OP_YIELD_SM_WIDE).
   * Raw inline value bytes — user structs hold no heap refs, so this buffer
   * is never GC-traced. Valid until the next yield; every pull consumer
   * copies out immediately. 16 = VM_MAX_STRUCT_SLOTS (defined below). */
  JaclVal    yield_wide[16];
  uint32_t   yield_wide_width;   /* slots valid in yield_wide (0 = none) */
  /* US-009: scope mark for hygiene in staged macro expansion */
  uint32_t   macro_scope_mark;   /* >0 during staged macro eval; make-syntax applies this */
  /* US-010: gensym counter pointer — set by expand__node before staged closure invocation */
  uint32_t  *gensym_counter_ptr; /* points into ExpandState.gensym_counter; NULL outside staged eval */
  /* US-014: bitmap marking stack slots that hold raw inline struct bytes (not GC-traceable JaclVals) */
  uint8_t    inline_slot_bitmap[VM_STACK_MAX / 8];
} VM;

/* --- jacl_context_t: reentrant execution context ---
 *
 * Owns arena, VM (which contains ThreadHeap), intern table, macro table.
 * Child contexts share parent's intern table but have their own arena/heap.
 * Macro expansion state (ExpandState) is per-context, eliminating the
 * file-static reentrancy hazards that previously lived in syntax.c.
 *
 * VM REENTRANCY AUDIT (US-005):
 * Audited vm_exec and all transitively called functions for state that
 * assumes a single in-flight execution. Findings:
 *
 * 1. vm.c: NO file-static mutable state. All VM state (stack, frames, ip,
 *    heap, environment) lives in the VM struct. vm_exec is reentrant as
 *    long as each invocation uses a separate VM instance.
 *
 * 2. compiler.c: NO file-static mutable state.
 *
 * 3. syntax.c: HAD 7 file-static variables (reentrancy hazards):
 *    - expand__error_msg/line/col: macro expansion error state
 *    - expand__frames[256]/frame_top: macro call stack for error reporting
 *    - expand__scope_counter: scope mark for hygiene
 *    - syntax__gensym_counter: unique symbol counter
 *    FIX: All moved into ExpandState struct, passed through expansion
 *    functions. jacl_gensym_next takes uint32_t* counter parameter.
 *
 * 4. runtime.c, string.c, value.c, embed.c: NO file-static mutable state.
 *
 * 5. No signal handlers or global instruction-pointer registers found.
 */

typedef struct jacl_context_s jacl_context_t;
struct jacl_context_s {
    arena_t          arena;
    VM               vm;
    JaclInternTable  intern_table;
    MacroTable       macro_table;
    uint64_t         restriction_set;     /* all-permissive = UINT64_MAX */
    ExpandState      expand;

    /* Parent context (NULL for root) */
    jacl_context_t  *parent;
    bool             owns_intern_table;   /* false when sharing parent's */
};

/* Scoped context switching: saves/restores gc__current_heap and the emergency
 * GC callback so that nested context operations are reentrant.
 *
 * jacl_ctx_save:  snapshot current thread-local GC state (call BEFORE ctx_new)
 * jacl_ctx_enter: save + switch gc__current_heap to a context's heap
 * jacl_ctx_restore: restore a previously saved snapshot */
typedef struct {
    ThreadHeap *heap;
    void       (*gc_fn)(void *);
    void        *gc_ctx;
} jacl_ctx_saved_t;

void jacl_ctx_save    (jacl_ctx_saved_t *saved);
void jacl_ctx_enter   (jacl_context_t *ctx, jacl_ctx_saved_t *saved);
void jacl_ctx_restore (jacl_ctx_saved_t saved);

/* --- JaclError: error out-param for internal run API --- */
#ifndef JACL_ERROR_DEFINED
#define JACL_ERROR_DEFINED
typedef enum {
    JACL_ERROR_NONE = 0,
    JACL_ERROR_COMPILE,
    JACL_ERROR_RUNTIME
} JaclErrorKind;

typedef struct {
    JaclErrorKind kind;
    const char   *message;
    uint32_t      line;
    uint32_t      col;
} JaclError;
#endif

/* Forward declarations for context lifecycle functions, used by OP_INTERPRET
 * and other opcodes before their definitions later in this file. */
jacl_context_t *jacl_ctx_new(jacl_context_t *parent);
void jacl_ctx_destroy(jacl_context_t *ctx);
JaclVal jacl_ctx_run_source(jacl_context_t *ctx, const char *src, size_t len,
                            uint64_t restriction_set, JaclError *err_out);
JaclVal source_to_closure_in_place(const char *src, size_t len,
                                   arena_t *arena, ThreadHeap *heap,
                                   JaclInternTable *intern_table,
                                   ExpandState *expand,
                                   JaclError *err_out,
                                   JaclVal prelude_map);

/* --- API --- */

void     vm_init(VM* vm, arena_t* arena);
void     vm_destroy(VM* vm);
VMResult vm_exec(VM* vm, BytecodeChunk* chunk);

/* --- Pipeline convenience --- */

VMResult jacl_run(const char* source, VM* vm, arena_t* arena);
VMResult jacl_exec_program(ProgramResult* program, VM* vm);

/* --- GC collect (defined in gc_collect.c, after vm.c in unity build) --- */

void gc_collect(ThreadHeap *heap, VM *vm);
void gc_collect_minor(ThreadHeap *heap, VM *vm,
                              RememberedSet *remembered_set);
bool gc_should_major(ThreadHeap *heap);

/* US-015: introspection kind-name helper (defined in syntax.c) */
const char *syntax_kind_name(uint8_t kind);

/* --- Emergency GC callback for single-threaded mode --- */

void vm__emergency_gc_single(void *ctx) {
    VM *vm = (VM *)ctx;
    gc_collect(&vm->heap, vm);
}

/* --- Concurrent GC trigger (defined in runtime.c, after gc_collect.c) --- */

void gc_concurrent_trigger(void *runtime_ptr);

/* --- Runtime helpers (defined in runtime.c, after gc_collect.c) --- */

JaclVal runtime__create_resolve_closure(ThreadHeap *heap, arena_t *arena,
                                                JaclVal future_val);
void runtime__submit_spawn_task(void *runtime_ptr, JaclClosure *closure,
                                        JaclVal future_val, JaclVal parent_ctx);
void runtime__schedule_continuation(void *runtime_ptr,
                                            JaclClosure *continuation,
                                            JaclVal result);
void runtime__schedule_sm_resumption(void *runtime_ptr,
                                             JaclVal state_machine,
                                             JaclVal result);
void runtime__schedule_timer(void *runtime_ptr, uint64_t duration_ns,
                             JaclVal sm_val);
void runtime__schedule_waiters(void *runtime_ptr,
                                       FutureWaiter *waiters,
                                       JaclVal result);
void runtime__submit_parallel_task(void *runtime_ptr,
                                           JaclClosure *closure,
                                           JaclVal agg_val,
                                           uint32_t index,
                                           JaclVal parent_ctx);
void runtime__submit_race_task(void *runtime_ptr,
                                       JaclClosure *closure,
                                       JaclVal agg_val,
                                       JaclVal parent_ctx);
void runtime__complete_parallel_slot(void *runtime_ptr,
                                             VM *vm,
                                             JaclVal agg_val,
                                             uint32_t index,
                                             JaclVal result);
void runtime__complete_race_slot(void *runtime_ptr,
                                         VM *vm,
                                         JaclVal agg_val,
                                         JaclVal result);

/* --- Heap-pointer slot write helper (AUDIT §10/§11) ---
 *
 * Wraps gc_write_barrier + assignment for any heap-pointer slot
 * inside a GC-managed object: stream->cached_value, stream->next_fn,
 * stream->state_machine, sm->sm_closure, sm->fields[i] (non-inline),
 * etc. The barrier handles both sides unconditionally so fresh
 * containers (watermark-protected but never traced) don't lose
 * inserted heap values.
 *
 * Atomic store on the slot: the concurrent GC marker may load this
 * slot at any time during a mark cycle (gc__trace_object reads the
 * matching field via ATOMIC_LOAD_EXPLICIT). The actual SATB ordering
 * — barrier-push happens-before the value becomes unreachable — is
 * provided by the grey buffer's mutex and end-of-mark drain, so a
 * relaxed atomic on the slot itself is sufficient to satisfy the C
 * memory model; whichever value the marker observes is live (old via
 * grey buf, new via direct trace). */
static inline void vm__slot_set(VM *vm, JaclVal *slot, JaclVal new_val) {
    gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, *slot, new_val);
    ATOMIC_STORE_EXPLICIT(slot, new_val, MEM_RELAXED);
}

/* --- Flat typed-arr scalar element conversions (see ARR_DESIGN.md M4d) ---
 * Typed arrays store raw element bytes; these convert between the packed
 * representation and a tagged JaclVal, mirroring the buf get/set per-type
 * switches. Only the typed-collection scalars are supported (the typer/
 * compiler reject others). */

static inline uint32_t vm__arr_scalar_size(JaclType t) {
    switch (t) {
        case TYPE_BOOL: return 1;
        case TYPE_I32: case TYPE_U32: case TYPE_F32: return 4;
        case TYPE_I64: case TYPE_U64: case TYPE_F64: return 8;
        default: return 0;
    }
}

/* Pack a tagged scalar into elem_size raw bytes at dst. Tolerant of literal
 * flex (an i32 literal stored into an i64 array, etc.), like the buf store. */
static inline void vm__arr_scalar_store(JaclType t, JaclVal v, uint8_t *dst) {
    switch (t) {
        case TYPE_BOOL: { uint8_t b = jacl_as_bool(v) ? 1 : 0; memcpy(dst, &b, 1); break; }
        case TYPE_I32: { int32_t x = jacl_is_i32(v) ? jacl_as_i32(v)
                                                     : (int32_t)jacl_as_u32(v);
                         memcpy(dst, &x, 4); break; }
        case TYPE_U32: { uint32_t x = jacl_is_u32(v) ? jacl_as_u32(v)
                                                     : (uint32_t)jacl_as_i32(v);
                         memcpy(dst, &x, 4); break; }
        case TYPE_F32: { float x = jacl_is_f32(v) ? jacl_as_f32(v)
                                                  : (float)jacl_as_f64(v);
                         memcpy(dst, &x, 4); break; }
        case TYPE_I64: { int64_t x = jacl_is_i64(v) ? jacl_as_i64(v)
                                                    : (int64_t)jacl_as_i32(v);
                         memcpy(dst, &x, 8); break; }
        case TYPE_U64: { uint64_t x = jacl_is_u64(v) ? jacl_as_u64(v)
                                                     : (uint64_t)jacl_as_i32(v);
                         memcpy(dst, &x, 8); break; }
        case TYPE_F64: { double x = jacl_is_f64(v) ? jacl_as_f64(v)
                                    : jacl_is_f32(v) ? (double)jacl_as_f32(v)
                                                     : (double)jacl_as_i32(v);
                         memcpy(dst, &x, 8); break; }
        default: break;
    }
}

/* Read elem_size raw bytes at src and produce the on-stack scalar.
 *
 * i32/u32/f32/bool are tagged JaclVals (their only representation). i64/u64/f64
 * are pushed as UNBOXED WIDE bits (raw 8 bytes reinterpreted as a JaclVal),
 * mirroring OP_TYPED_VEC_GET_INLINE's `*ptr` push — NOT heap-boxed. This lets
 * the compiler/typer narrow arr-get/arr-pop on [Arr i64/u64/f64] to the wide
 * scalar type and bridge to dyn via OP_TO_DYN (ensure_boxed) at dyn sinks,
 * exactly as typed-vec does. The `vm` param is unused but kept for call-site
 * symmetry with the boxing builtins. */
static inline JaclVal vm__arr_scalar_load(VM *vm, JaclType t, const uint8_t *src) {
    (void)vm;
    switch (t) {
        case TYPE_BOOL: return jacl_bool(src[0] != 0);
        case TYPE_I32: { int32_t v; memcpy(&v, src, 4); return jacl_i32(v); }
        case TYPE_U32: { uint32_t v; memcpy(&v, src, 4); return jacl_u32(v); }
        case TYPE_F32: { float v; memcpy(&v, src, 4); return jacl_f32(v); }
        /* Unboxed wide: raw 8 bytes straight into the JaclVal slot. */
        case TYPE_I64:
        case TYPE_U64:
        case TYPE_F64: { JaclVal v; memcpy(&v, src, 8); return v; }
        default: return JACL_NIL;
    }
}

/* True if the arr's element encoding is the dyn sentinel (tagged-JaclVal
 * storage, the M3 path). */
static inline bool vm__arr_is_dyn(uint32_t elem_idx) {
    return elem_idx == JACL_SCALAR_TYPE_IDX(TYPE_DYN);
}

/* --- Type name helper for error messages --- */

const char* vm__type_name(JaclVal v) {
  if (jacl_is_nil(v))           return "nil";
  if (jacl_is_bool(v))          return "bool";
  if (jacl_is_i32(v))           return "i32";
  if (jacl_is_u32(v))           return "u32";
  if (jacl_is_i64(v))           return "i64";
  if (jacl_is_u64(v))           return "u64";
  if (jacl_is_f32(v))           return "f32";
  if (jacl_is_f64(v))           return "f64";
  if (jacl_is_string(v))        return "string";
  if (jacl_is_closure(v))       return "closure";
  if (jacl_is_vector(v))        return "vector";
  if (jacl_is_arr(v))           return "arr";
  if (jacl_is_map(v))           return "map";
  if (jacl_is_future(v))        return "future";
  if (jacl_is_stream(v))       return "stream";
  if (jacl_is_native_fn(v))    return "native-fn";
  if (jacl_is_box(v))          return "box";
  if (jacl_is_atom(v))         return "atom";
  if (jacl_is_cell(v))         return "cell";
  if (jacl_is_struct(v))       return "struct";
  if (jacl_is_typed_vector(v)) return "typed-vec";
  if (jacl_is_typed_map(v))    return "typed-map";
  return "unknown";
}

/* --- Error reporting helper --- */

void vm__set_error(VM* vm, const char* fmt, ...) {
  va_list ap;
  char buf[256];
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0) n = 0;
  uint32_t len = (uint32_t)n;
  char* msg = (char*)arena_alloc(vm->arena, len + 1);
  memcpy(msg, buf, len + 1);
  vm->error_message = msg;
}

/* Stack overflow helpers — distinguish operand stack (VM_STACK_MAX slots)
 * from call-frame stack (VM_FRAMES_MAX frames) so users know which limit
 * they hit. §16 in AUDIT.md. */
void vm__set_frame_overflow(VM* vm) {
  vm__set_error(vm, "call depth exceeded (max %d frames) — too much nested recursion",
                (int)VM_FRAMES_MAX);
}
void vm__set_operand_overflow(VM* vm, const char* where) {
  if (where) {
    vm__set_error(vm, "operand stack overflow (max %d slots) at %s — expression too deeply nested",
                  (int)VM_STACK_MAX, where);
  } else {
    vm__set_error(vm, "operand stack overflow (max %d slots) — expression too deeply nested",
                  (int)VM_STACK_MAX);
  }
}

/* --- Default print function: write to stdout --- */

void vm__default_print(const char* text, uint32_t len, void* ctx) {
  (void)ctx;
  fwrite(text, 1, len, stdout);
}

/* --- Truthiness helper --- */

bool vm__is_falsy(JaclVal v) {
  return jacl_is_nil(v) || v == JACL_FALSE;
}

/* --- Stack trace capture --- */

/**
 * Capture the current call frame chain into the VM's stack trace.
 * Walks frames from innermost to outermost.
 */
void vm__capture_trace(VM* vm) {
  vm->stack_trace.count = 0;
  for (uint32_t i = vm->frame_count; i > 0 && vm->stack_trace.count < VM_STACK_TRACE_MAX; i--) {
    CallFrame* f = &vm->frames[i - 1];
    StackTraceEntry* entry = &vm->stack_trace.entries[vm->stack_trace.count++];
    entry->function_name = f->closure ? f->closure->name : NULL;

    if (i == vm->frame_count) {
      /* Current (innermost) frame: use the tracked error_line */
      entry->line_number = vm->error_line;
    } else {
      /* Parent frame: derive line from child's return_ip in this frame's chunk */
      CallFrame* child = &vm->frames[i];
      if (child->return_ip == NULL || f->chunk == NULL) {
        entry->line_number = 0;
      } else {
        uint32_t offset = (uint32_t)(child->return_ip - f->chunk->code);
        if (offset > 0) offset--;
        if (offset < f->chunk->code_count) {
          entry->line_number = f->chunk->lines[offset];
        } else {
          entry->line_number = 0;
        }
      }
    }
  }
}

/**
 * Initialize the VM to a clean state.
 * Arena is used for environment storage.
 */
void vm_init(VM* vm, arena_t* arena) {
  memset(vm->stack, 0, sizeof(vm->stack));
  vm->stack_top = 0;
  vm->ip        = NULL;
  vm->chunk     = NULL;
  vm->print_fn  = vm__default_print;
  vm->print_ctx = NULL;
  vm->arena         = arena;
  vm->intern_table  = NULL;
  vm->top_chunk     = NULL;
  vm->grey_buf      = NULL;
  vm->remembered_set = NULL;
  vm->gc_active_ptr = NULL;
  vm->runtime       = NULL;
  vm->worker_id     = -1;
  vm->frame_count   = 0;
  vm->error_message = NULL;
  vm->error_line    = 0;
  vm->stack_trace.count = 0;
  vm->struct_registry = NULL;
  vm->ctx_pool        = NULL;
  vm->ctx             = JACL_NIL;
  vm->saved_ctx_count = 0;
  vm->gc_handle_slots = NULL;
  vm->gc_handle_count = 0;
  vm->call_native       = NULL;
  vm->native_fn_ctx     = NULL;
  vm->native_fn_arities = NULL;
  vm->native_fn_count   = 0;
  vm->spread_count_top  = 0;
  vm->yield_value        = JACL_NIL;
  vm->yield_wide_width   = 0;
  memset(vm->inline_slot_bitmap, 0, sizeof(vm->inline_slot_bitmap));

  /* Initialize GC heap and make it available for collection templates */
  gc_block_pool_init(&vm->block_pool);
  gc_heap_init(&vm->heap, &vm->block_pool);
  gc__current_heap = &vm->heap;

  /* Set emergency GC callback for single-threaded OOM escalation */
  gc__emergency_gc_fn  = vm__emergency_gc_single;
  gc__emergency_gc_ctx = vm;

  /* Ensure HAMT key handlers are wired up */
  collections__init();

  /* Initialize environment */
  vm->env.count  = 0;
  vm->env.cap    = VM_ENV_INIT_CAP;
  vm->env.names  = (JaclVal*)arena_alloc(arena, VM_ENV_INIT_CAP * sizeof(JaclVal));
  vm->env.values = (JaclVal*)arena_alloc(arena, VM_ENV_INIT_CAP * sizeof(JaclVal));

  /* Pre-populate: true, false, nil */
  vm->env.names[0]  = jacl_inline_string("true", 4);
  vm->env.values[0] = JACL_TRUE;
  vm->env.names[1]  = jacl_inline_string("false", 5);
  vm->env.values[1] = JACL_FALSE;
  vm->env.names[2]  = jacl_inline_string("nil", 3);
  vm->env.values[2] = JACL_NIL;
  vm->env.count = 3;
}

/**
 * Destroy the VM's GC heap and block pool.
 * Call before arena_destroy().
 */
void vm_destroy(VM* vm) {
  gc_heap_destroy(&vm->heap);
  gc_block_pool_destroy(&vm->block_pool);
}

/* --- Stack helpers --- */

VMResult vm__push(VM* vm, JaclVal value) {
  if (vm->stack_top >= VM_STACK_MAX) {
    vm__set_operand_overflow(vm, NULL);
    return VM_STACK_OVERFLOW;
  }
  vm->stack[vm->stack_top++] = value;
  return VM_OK;
}

VMResult vm__pop(VM* vm, JaclVal* out) {
  if (vm->stack_top == 0) {
    vm->error_message = "stack underflow";
    return VM_RUNTIME_ERROR;
  }
  *out = vm->stack[--vm->stack_top];
  return VM_OK;
}

/* --- Instruction pointer helpers --- */

uint8_t vm__read_byte(VM* vm) {
  return *vm->ip++;
}

uint16_t vm__read_u16(VM* vm) {
  uint8_t hi = vm__read_byte(vm);
  uint8_t lo = vm__read_byte(vm);
  return (uint16_t)((hi << 8) | lo);
}

/* --- Typed collection helpers --- */

/* Compute struct slot width from type def */
static inline uint32_t vm__struct_width(StructTypeDef* sdef) {
  return (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
}

/* Mark `width` consecutive stack slots starting at `base` as belonging to
 * an inline struct of type `sdef`. Slots covering ref-elem buf field bytes
 * are cleared (so the GC marker scans the embedded JaclVal); the rest are
 * set (raw bytes, marker skips). For value-type-only structs this is a
 * plain "mark all slots as inline" loop -- the slot_ref_bitmap is all
 * zero in that case. See BUFFER_DESIGN.md (ref-elem bufs as struct fields). */
static inline void vm__mark_struct_inline_slots(VM* vm, uint32_t base,
                                                StructTypeDef* sdef,
                                                uint32_t width) {
  for (uint32_t si = 0; si < width; si++) {
    bool is_ref = (si < 256) &&
                  ((sdef->slot_ref_bitmap[si >> 3] >> (si & 7)) & 1u);
    if (is_ref) {
      BITMAP_CLR(vm->inline_slot_bitmap, base + si);
    } else {
      BITMAP_SET(vm->inline_slot_bitmap, base + si);
    }
  }
}

/* Returns true if any JaclVal slot in `sdef`'s layout is a GC-traced ref
 * slot (i.e. the struct has a ref-elem buf field). Used to decide whether
 * to fall back to the selective-mark helper or just BITMAP_SET the range. */
static inline bool vm__struct_has_ref_slots(StructTypeDef* sdef) {
  for (uint32_t i = 0; i < sizeof(sdef->slot_ref_bitmap); i++) {
    if (sdef->slot_ref_bitmap[i] != 0) return true;
  }
  return false;
}

/* Extract struct raw bytes into a JaclVal slot array for strided push/set.
   Caller must provide slots[] with at least vm__struct_width(sdef) elements. */
static inline void vm__struct_to_slots(StructTypeDef* sdef, HeapRecord* s,
                                       JaclVal* slots, uint32_t width) {
  memset(slots, 0, width * sizeof(JaclVal));
  memcpy(slots, s->data, sdef->total_size);
}

/* Decompose a heap HeapRecord into a flat JaclVal slot array.
   Returns the slot width.  Caller provides out[] (VM_MAX_STRUCT_SLOTS). */
#define VM_MAX_STRUCT_SLOTS 16
static inline uint32_t vm__unpack_struct(VM* vm, uint16_t type_idx,
                                         JaclVal struct_val, JaclVal* out) {
  StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
  uint32_t w = vm__struct_width(sdef);
  vm__struct_to_slots(sdef, jacl_as_heap_record_ptr(struct_val), out, w);
  return w;
}

/* Compute element width from a typed-collection type_idx, dispatching
 * on the scalar sentinel range (0xFF00..0xFFFF = scalar JaclType).
 * Scalars always have width 1; structs use vm__struct_width. */
static inline uint32_t vm__typed_elem_width(VM* vm, uint16_t type_idx) {
  if (type_idx >= 0xFF00) return 1;
  return vm__struct_width(vm->struct_registry->defs[type_idx]);
}

/* Pop a typed-collection element (struct or scalar) into out[].
 * Returns width. */
static inline uint32_t vm__pop_struct(VM* vm, uint16_t type_idx, JaclVal* out);

static inline uint32_t vm__pop_typed_elem(VM* vm, uint16_t type_idx, JaclVal* out) {
  if (type_idx >= 0xFF00) {
    VMResult r = vm__pop(vm, &out[0]);
    (void)r;
    return 1;
  }
  return vm__pop_struct(vm, type_idx, out);
}

/* Pop a struct from TOS into out[]. Handles both representations:
 *   - Inline bytes (width consecutive slots, marked in inline_slot_bitmap)
 *   - Heap HeapRecord pointer (single slot)
 * Returns width. The out buffer must hold at least width slots. */
static inline uint32_t vm__pop_struct(VM* vm, uint16_t type_idx, JaclVal* out) {
  StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
  uint32_t width = vm__struct_width(sdef);
  uint32_t total_size = sdef->total_size;
  uint32_t buf_bytes = width * (uint32_t)sizeof(JaclVal);
  bool is_inline = vm->stack_top > 0 &&
                   BITMAP_GET(vm->inline_slot_bitmap, vm->stack_top - 1);
  if (is_inline) {
    memcpy(out, &vm->stack[vm->stack_top - width], total_size);
    if (buf_bytes > total_size) {
      memset((uint8_t*)out + total_size, 0, buf_bytes - total_size);
    }
    for (uint32_t si = 0; si < width; si++) {
      BITMAP_CLR(vm->inline_slot_bitmap, vm->stack_top - width + si);
    }
    vm->stack_top -= width;
  } else {
    JaclVal heap_val = vm->stack[--vm->stack_top];
    HeapRecord* s = jacl_as_heap_record_ptr(heap_val);
    memcpy(out, s->data, total_size);
    if (buf_bytes > total_size) {
      memset((uint8_t*)out + total_size, 0, buf_bytes - total_size);
    }
  }
  return width;
}

/* --- Environment helpers ---
 *
 * Concurrent access pattern: the owning worker (sole writer) calls
 * vm__env_set/grow/get. The GC root scanner (gc_enumerate_roots, runs
 * on the GC worker thread) reads `env.values` to push roots. Writer-
 * side stores and reader-side loads on count/values pointer/values[i]
 * use release/acquire so TSAN can observe the happens-before. The
 * single-threaded gc_mark paths and same-thread vm__env_get reads stay
 * plain (no cross-thread access). */

void vm__env_grow(VM* vm) {
  uint32_t new_cap = vm->env.cap * 2;
  JaclVal* new_names  = (JaclVal*)arena_alloc(vm->arena, new_cap * sizeof(JaclVal));
  JaclVal* new_values = (JaclVal*)arena_alloc(vm->arena, new_cap * sizeof(JaclVal));
  memcpy(new_names, vm->env.names, vm->env.count * sizeof(JaclVal));
  memcpy(new_values, vm->env.values, vm->env.count * sizeof(JaclVal));
  vm->env.names  = new_names;
  /* Pointer swap races with the GC root scanner's load of vm.env.values.
   * Release-store; the scanner does acquire-load. New array is already
   * populated via memcpy above, so the scanner sees a fully-initialized
   * array regardless of which pointer it observes. */
  ATOMIC_STORE_EXPLICIT(&vm->env.values, new_values, MEM_RELEASE);
  vm->env.cap    = new_cap;
}

void vm__env_set(VM* vm, JaclVal name, JaclVal value) {
  /* Check if name already exists */
  uint32_t count = vm->env.count;  /* sole writer; plain load */
  for (uint32_t i = 0; i < count; i++) {
    if (vm->env.names[i] == name) {
      ATOMIC_STORE_EXPLICIT(&vm->env.values[i], value, MEM_RELEASE);
      return;
    }
  }
  /* New entry */
  if (count >= vm->env.cap) {
    vm__env_grow(vm);
  }
  vm->env.names[count] = name;
  ATOMIC_STORE_EXPLICIT(&vm->env.values[count], value, MEM_RELEASE);
  /* Release-store on count gives the scanner happens-before for the
   * slot write above. Scanner acquire-loads count before iterating. */
  ATOMIC_STORE_EXPLICIT(&vm->env.count, count + 1, MEM_RELEASE);
}

JaclVal vm__env_get(VM* vm, JaclVal name, bool* found) {
  /* Same-thread reader; plain loads. */
  for (uint32_t i = 0; i < vm->env.count; i++) {
    if (vm->env.names[i] == name) {
      *found = true;
      return vm->env.values[i];
    }
  }
  *found = false;
  return JACL_NIL;
}

/* --- Binary numeric operation macro --- */

#define VM__BINARY_NUMERIC_OP(fn_i32, fn_f32, fn_u32, op_name)               \
  do {                                                                        \
    JaclVal b, a;                                                             \
    result = vm__pop(vm, &b); if (result != VM_OK) return result;             \
    result = vm__pop(vm, &a); if (result != VM_OK) return result;             \
    JaclVal res;                                                              \
    if (jacl_is_i32(a) && jacl_is_i32(b)) {                                  \
      res = fn_i32(a, b);                                                     \
    } else if (jacl_is_f32(a) && jacl_is_f32(b)) {                           \
      res = fn_f32(a, b);                                                     \
    } else if (jacl_is_u32(a) && jacl_is_u32(b)) {                           \
      res = fn_u32(a, b);                                                     \
    } else {                                                                  \
      vm__set_error(vm,                                                       \
        "type error in '%s': expected matching numeric types, got %s and %s", \
        op_name, vm__type_name(a), vm__type_name(b));                         \
      return VM_RUNTIME_ERROR;                                                \
    }                                                                         \
    if (jacl_is_error(res) && !jacl_is_error(a) && !jacl_is_error(b))        \
      vm__capture_trace(vm);                                                  \
    result = vm__push(vm, res); if (result != VM_OK) return result;           \
  } while (0)

/* --- Dynamic format buffer for collection display --- */

typedef struct {
  char*     data;
  uint32_t  len;
  uint32_t  cap;
  arena_t*  arena;
  StructTypeRegistry* registry;
} VMFormatBuf;

void vm__fmt_init(VMFormatBuf* buf, arena_t* arena, StructTypeRegistry* registry) {
  buf->arena = arena;
  buf->registry = registry;
  buf->len   = 0;
  buf->cap   = 128;
  buf->data  = (char*)arena_alloc(arena, 128);
}

void vm__fmt_ensure(VMFormatBuf* buf, uint32_t extra) {
  if (buf->len + extra <= buf->cap) return;
  uint32_t new_cap = buf->cap ? buf->cap * 2 : 128;
  while (new_cap < buf->len + extra) new_cap *= 2;
  char* new_data = (char*)arena_alloc(buf->arena, new_cap);
  if (buf->len) memcpy(new_data, buf->data, buf->len);
  buf->data = new_data;
  buf->cap  = new_cap;
}

void vm__fmt_append(VMFormatBuf* buf, const char* str, uint32_t len) {
  vm__fmt_ensure(buf, len);
  memcpy(buf->data + buf->len, str, len);
  buf->len += len;
}

/* Forward declarations for unity-build / wasm strict-prototype mode. */
void vm__fmt_value(VMFormatBuf* buf, JaclVal val);

/* Format a struct ([Name field val field val …]) from raw C-ABI bytes.
 * Reader-symmetric form — the printed shape matches the named
 * constructor.
 * Used by both heap-struct OP_PRINT path and inline OP_PRINT_STRUCT. */
void vm__fmt_struct_bytes(VMFormatBuf* buf, StructTypeDef* sdef, const uint8_t* data) {
  vm__fmt_append(buf, "[", 1);
  vm__fmt_append(buf, sdef->name, sdef->name_len);
  for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
    vm__fmt_append(buf, " ", 1);
    vm__fmt_append(buf, sdef->fields[fi].name, sdef->fields[fi].name_len);
    vm__fmt_append(buf, " ", 1);
    uint32_t off = sdef->fields[fi].offset;
    char fbuf[64];
    int flen;
    switch (sdef->fields[fi].type) {
      case TYPE_I32: { int32_t n; memcpy(&n, data + off, 4);
        flen = snprintf(fbuf, sizeof(fbuf), "%d", n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break; }
      case TYPE_I64: { int64_t n; memcpy(&n, data + off, 8);
        flen = snprintf(fbuf, sizeof(fbuf), "%" PRIi64, n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break; }
      case TYPE_U32: { uint32_t n; memcpy(&n, data + off, 4);
        flen = snprintf(fbuf, sizeof(fbuf), "%u", n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break; }
      case TYPE_U64: { uint64_t n; memcpy(&n, data + off, 8);
        flen = snprintf(fbuf, sizeof(fbuf), "%" PRIu64, n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break; }
      case TYPE_F32: { float f; memcpy(&f, data + off, 4);
        flen = snprintf(fbuf, sizeof(fbuf), "%g", (double)f);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break; }
      case TYPE_F64: { double d; memcpy(&d, data + off, 8);
        flen = snprintf(fbuf, sizeof(fbuf), "%g", d);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break; }
      case TYPE_BOOL: { uint8_t b = data[off];
        vm__fmt_append(buf, b ? "true" : "false", b ? 4 : 5); break; }
      case TYPE_STRUCT: {
        if (buf->registry && sdef->fields[fi].struct_type_idx < buf->registry->count) {
          StructTypeDef* nested = buf->registry->defs[sdef->fields[fi].struct_type_idx];
          if (nested) vm__fmt_struct_bytes(buf, nested, data + off);
          else vm__fmt_append(buf, "<struct>", 8);
        } else {
          vm__fmt_append(buf, "<struct>", 8);
        }
        break;
      }
      default: {
        JaclVal fval; memcpy(&fval, data + off, sizeof(JaclVal));
        vm__fmt_value(buf, fval); break;
      }
    }
  }
  vm__fmt_append(buf, "]", 1);
}

/* Format a single scalar slot from a typed collection (typed vec/map
 * with sentinel element type). The slot is one JaclVal-sized value
 * holding either a tagged inline scalar (i32/u32/f32) or a raw
 * unboxed numeric (i64/u64/f64). */
void vm__fmt_typed_scalar(VMFormatBuf* buf, const JaclVal* ptr, JaclType t) {
  char tmp[64];
  int n = 0;
  switch (t) {
    case TYPE_I32: n = snprintf(tmp, sizeof(tmp), "%d", jacl_as_i32(*ptr)); break;
    case TYPE_U32: n = snprintf(tmp, sizeof(tmp), "%u", jacl_as_u32(*ptr)); break;
    case TYPE_F32: n = snprintf(tmp, sizeof(tmp), "%g", (double)jacl_as_f32(*ptr)); break;
    case TYPE_I64: n = snprintf(tmp, sizeof(tmp), "%" PRId64, (int64_t)*ptr); break;
    case TYPE_U64: n = snprintf(tmp, sizeof(tmp), "%" PRIu64, (uint64_t)*ptr); break;
    case TYPE_F64: { double d; memcpy(&d, ptr, 8); n = snprintf(tmp, sizeof(tmp), "%g", d); break; }
    case TYPE_STR:
    case TYPE_BOOL:
      /* Tagged-stored elements ([Vec str]/[Vec bool] slots hold the tagged
       * JaclVal): format like any tagged value. Reachable since typed
       * collect ([Vec str] from `lines`). */
      vm__fmt_value(buf, *ptr);
      return;
    default: n = snprintf(tmp, sizeof(tmp), "?"); break;
  }
  if (n > 0) vm__fmt_append(buf, tmp, (uint32_t)n);
}

void vm__fmt_value(VMFormatBuf* buf, JaclVal val) {
  char tmp[64];
  int n;

  if (jacl_is_nil(val)) {
    vm__fmt_append(buf, "nil", 3);
  } else if (jacl_is_bool(val)) {
    if (val == JACL_TRUE) vm__fmt_append(buf, "true", 4);
    else vm__fmt_append(buf, "false", 5);
  } else if (jacl_is_i32(val)) {
    n = snprintf(tmp, sizeof(tmp), "%d", (int)jacl_as_i32(val));
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_u32(val)) {
    n = snprintf(tmp, sizeof(tmp), "%u", (unsigned)jacl_as_u32(val));
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_f32(val)) {
    n = snprintf(tmp, sizeof(tmp), "%g", (double)jacl_as_f32(val));
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_i64(val)) {
    n = snprintf(tmp, sizeof(tmp), "%" PRIi64, jacl_as_i64(val));
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_u64(val)) {
    n = snprintf(tmp, sizeof(tmp), "%" PRIu64, jacl_as_u64(val));
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_f64(val)) {
    n = snprintf(tmp, sizeof(tmp), "%g", jacl_as_f64(val));
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_string(val)) {
    uint32_t slen = jacl_string_byte_len(val);
    vm__fmt_append(buf, "\"", 1);
    if (jacl_is_heap_string(val)) {
      JaclHeapString* hs = jacl_as_heap_string(val);
      vm__fmt_append(buf, hs->data, hs->byte_len);
    } else {
      char sbuf[8];
      jacl_string_data(val, sbuf, slen);
      vm__fmt_append(buf, sbuf, slen);
    }
    vm__fmt_append(buf, "\"", 1);
  } else if (jacl_is_vector(val)) {
    jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(val);
    uint32_t count = jacl_vec_count(vec);
    vm__fmt_append(buf, "[vec", 4);
    for (uint32_t i = 0; i < count; i++) {
      vm__fmt_append(buf, " ", 1);
      jacl_vec_get_result gr = jacl_vec_get(vec, i);
      vm__fmt_value(buf, gr.value);
    }
    vm__fmt_append(buf, "]", 1);
  } else if (jacl_is_arr(val)) {
    /* Mutable arr renders its contents like vec ([arr ...]) even though its
     * eq is identity-based (see ARR_DESIGN.md). */
    JaclArr* a = (JaclArr*)jacl_as_ptr(val);
    uint32_t count = a->sa.count;
    bool arr_dyn = (a->elem_idx == JACL_SCALAR_TYPE_IDX(TYPE_DYN));
    bool arr_scalar = !arr_dyn && JACL_IS_SCALAR_TYPE_IDX(a->elem_idx);
    JaclType arr_et = arr_scalar ? JACL_TYPE_IDX_TO_SCALAR(a->elem_idx) : TYPE_DYN;
    vm__fmt_append(buf, "[arr", 4);
    for (uint32_t i = 0; i < count; i++) {
      vm__fmt_append(buf, " ", 1);
      uint8_t* slot = sa_var_get(&a->sa, i);
      if (arr_dyn) {
        vm__fmt_value(buf, slot ? *(JaclVal*)slot : JACL_NIL);
      } else if (arr_scalar && slot) {
        /* Format raw scalar bytes directly (vm__fmt_value can't heap-box an
         * i64/f64 here). Format strings mirror vm__fmt_value exactly. */
        char nb[64]; int nn = 0;
        switch (arr_et) {
          case TYPE_BOOL: vm__fmt_append(buf, slot[0] ? "true" : "false",
                                         slot[0] ? 4 : 5); break;
          case TYPE_I32: { int32_t v; memcpy(&v, slot, 4);
                           nn = snprintf(nb, sizeof(nb), "%d", (int)v); break; }
          case TYPE_U32: { uint32_t v; memcpy(&v, slot, 4);
                           nn = snprintf(nb, sizeof(nb), "%u", (unsigned)v); break; }
          case TYPE_F32: { float v; memcpy(&v, slot, 4);
                           nn = snprintf(nb, sizeof(nb), "%g", (double)v); break; }
          case TYPE_I64: { int64_t v; memcpy(&v, slot, 8);
                           nn = snprintf(nb, sizeof(nb), "%" PRIi64, v); break; }
          case TYPE_U64: { uint64_t v; memcpy(&v, slot, 8);
                           nn = snprintf(nb, sizeof(nb), "%" PRIu64, v); break; }
          case TYPE_F64: { double v; memcpy(&v, slot, 8);
                           nn = snprintf(nb, sizeof(nb), "%g", v); break; }
          default: break;
        }
        if (nn > 0) vm__fmt_append(buf, nb, (uint32_t)nn);
      } else if (slot && buf->registry && a->elem_idx < buf->registry->count) {
        /* struct element: format the inline bytes directly */
        StructTypeDef* esdef = buf->registry->defs[a->elem_idx];
        if (esdef) vm__fmt_struct_bytes(buf, esdef, slot);
      }
    }
    vm__fmt_append(buf, "]", 1);
  } else if (jacl_is_map(val)) {
    jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(val);
    vm__fmt_append(buf, "[map", 4);
    jacl_map_iter it = jacl_map_iter_init(map);
    jacl_map_iter_result ir;
    for (;;) {
      ir = jacl_map_next_leaf(&it);
      if (ir.done) break;
      JaclVal key = jacl_map_key_from_leaf(ir.item);
      JaclVal value = jacl_map_value_from_leaf(ir.item);
      vm__fmt_append(buf, " ", 1);
      vm__fmt_value(buf, key);
      vm__fmt_append(buf, " ", 1);
      vm__fmt_value(buf, value);
    }
    vm__fmt_append(buf, "]", 1);
  } else if (jacl_is_typed_vector(val)) {
    jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(val);
    uint32_t count = jacl_typed_vec_count(tvec);
    n = snprintf(tmp, sizeof(tmp), "<typed-vec %u>", count);
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_typed_map(val)) {
    n = snprintf(tmp, sizeof(tmp), "<typed-map>");
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_closure(val)) {
    JaclClosure* cl = jacl_as_closure(val);
    if (cl->name) {
      n = snprintf(tmp, sizeof(tmp), "<proc %s>", cl->name);
      vm__fmt_append(buf, tmp, (uint32_t)n);
    } else {
      vm__fmt_append(buf, "<closure>", 9);
    }
  } else if (jacl_is_cell(val)) {
    /* Cells are transparent — print the contained value directly */
    JaclMutableRef* ref = jacl_as_cell(val);
    vm__fmt_value(buf, MREF_VAL(ref));
  } else if (jacl_is_box(val)) {
    void* payload = jacl_as_ptr(val);
    JaclMutableRef* ref = (JaclMutableRef*)payload;
    if (jacl_box_is_typed(payload) && buf->registry &&
        ref->type_idx < buf->registry->count) {
      /* Struct box: format with struct name and field values */
      StructTypeDef* sdef = buf->registry->defs[ref->type_idx];
      if (sdef) {
        vm__fmt_append(buf, "<box ", 5);
        vm__fmt_append(buf, sdef->name, sdef->name_len);
        vm__fmt_append(buf, ":", 1);
        for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
          StructTypeField* sf = &sdef->fields[fi];
          vm__fmt_append(buf, " ", 1);
          vm__fmt_append(buf, sf->name, sf->name_len);
          vm__fmt_append(buf, "=", 1);
          /* Read field value from raw data bytes */
          JaclVal fv;
          switch (sf->type) {
            case TYPE_BOOL: { uint8_t b = ref->data[sf->offset]; fv = jacl_bool(b); break; }
            case TYPE_I32: { int32_t n; memcpy(&n, ref->data + sf->offset, 4); fv = jacl_i32(n); break; }
            case TYPE_U32: { uint32_t n; memcpy(&n, ref->data + sf->offset, 4); fv = jacl_u32(n); break; }
            case TYPE_F32: { float f; memcpy(&f, ref->data + sf->offset, 4); fv = jacl_f32(f); break; }
            case TYPE_I64: { int64_t n; memcpy(&n, ref->data + sf->offset, 8);
              char tb[32]; int tn = snprintf(tb, sizeof(tb), "%" PRIi64, n);
              vm__fmt_append(buf, tb, (uint32_t)tn); continue; }
            case TYPE_U64: { uint64_t n; memcpy(&n, ref->data + sf->offset, 8);
              char tb[32]; int tn = snprintf(tb, sizeof(tb), "%" PRIu64, n);
              vm__fmt_append(buf, tb, (uint32_t)tn); continue; }
            case TYPE_F64: { double d; memcpy(&d, ref->data + sf->offset, 8);
              char tb[32]; int tn = snprintf(tb, sizeof(tb), "%g", d);
              vm__fmt_append(buf, tb, (uint32_t)tn); continue; }
            default: { memcpy(&fv, ref->data + sf->offset, sizeof(JaclVal)); break; }
          }
          vm__fmt_value(buf, fv);
        }
        vm__fmt_append(buf, ">", 1);
      } else {
        vm__fmt_append(buf, "<box: unknown-struct>", 21);
      }
    } else {
      vm__fmt_append(buf, "<box: ", 6);
      vm__fmt_value(buf, *jacl_box_untyped_val(payload));
      vm__fmt_append(buf, ">", 1);
    }
  } else if (jacl_is_atom(val)) {
    JaclMutableRef* ref = jacl_as_atom(val);
    vm__fmt_append(buf, "<atom: ", 7);
    vm__fmt_value(buf, MREF_VAL(ref));
    vm__fmt_append(buf, ">", 1);
  } else if (jacl_is_future(val)) {
    JaclFuture* fut = jacl_as_future(val);
    uint32_t state = ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_ACQUIRE);
    if (state == FUTURE_PENDING) {
      vm__fmt_append(buf, "<future: pending>", 17);
    } else if (state == FUTURE_RESOLVED) {
      vm__fmt_append(buf, "<future: resolved ", 18);
      vm__fmt_value(buf, (JaclVal)fut->result);
      vm__fmt_append(buf, ">", 1);
    } else {
      vm__fmt_append(buf, "<future: error ", 15);
      vm__fmt_value(buf, (JaclVal)fut->result);
      vm__fmt_append(buf, ">", 1);
    }
  } else if (jacl_is_stream(val)) {
    vm__fmt_append(buf, "<stream>", 8);
  } else if (jacl_is_native_fn(val)) {
    n = snprintf(tmp, sizeof(tmp), "<native-fn #%u>",
                 jacl_as_native_fn_index(val));
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_syntax(val)) {
    JaclSyntax *syn = jacl_as_syntax(val);
    switch (syn->kind) {
    case SYNTAX_COMMAND: {
      vm__fmt_append(buf, "<syntax:command [", 17);
      vm__fmt_value(buf, syn->data.command.head);
      jacl_vec_root *args = (jacl_vec_root *)jacl_as_ptr(syn->data.command.args);
      uint32_t argc = jacl_vec_count(args);
      for (uint32_t i = 0; i < argc; i++) {
        vm__fmt_append(buf, " ", 1);
        vm__fmt_value(buf, jacl_vec_get(args, i).value);
      }
      vm__fmt_append(buf, "]>", 2);
      break;
    }
    case SYNTAX_LIT_INT:
      n = snprintf(tmp, sizeof(tmp), "<syntax:lit-int %d>", syn->data.lit_int.value);
      vm__fmt_append(buf, tmp, (uint32_t)n);
      break;
    case SYNTAX_LIT_FLOAT:
      n = snprintf(tmp, sizeof(tmp), "<syntax:lit-float %g>", (double)syn->data.lit_float.value);
      vm__fmt_append(buf, tmp, (uint32_t)n);
      break;
    case SYNTAX_LIT_STRING: {
      vm__fmt_append(buf, "<syntax:lit-string ", 19);
      vm__fmt_value(buf, syn->data.lit_string.value);
      vm__fmt_append(buf, ">", 1);
      break;
    }
    case SYNTAX_VAR_REF: {
      vm__fmt_append(buf, "<syntax:var-ref $", 17);
      JaclVal name = syn->data.var_ref.name;
      uint32_t slen = jacl_string_byte_len(name);
      if (jacl_is_heap_string(name)) {
        JaclHeapString *hs = jacl_as_heap_string(name);
        vm__fmt_append(buf, hs->data, hs->byte_len);
      } else {
        char sbuf[8];
        jacl_string_data(name, sbuf, slen);
        vm__fmt_append(buf, sbuf, slen);
      }
      vm__fmt_append(buf, ">", 1);
      break;
    }
    case SYNTAX_BLOCK: {
      vm__fmt_append(buf, "<syntax:block {", 15);
      jacl_vec_root *cmds = (jacl_vec_root *)jacl_as_ptr(syn->data.block.commands);
      uint32_t cnt = jacl_vec_count(cmds);
      for (uint32_t i = 0; i < cnt; i++) {
        if (i > 0) vm__fmt_append(buf, "; ", 2);
        vm__fmt_append(buf, " ", 1);
        vm__fmt_value(buf, jacl_vec_get(cmds, i).value);
      }
      vm__fmt_append(buf, " }>", 3);
      break;
    }
    case SYNTAX_DEFMACRO: {
      vm__fmt_append(buf, "<syntax:defmacro>", 17);
      break;
    }
    case SYNTAX_QUOTE: {
      vm__fmt_append(buf, "<syntax:quote ", 14);
      vm__fmt_value(buf, syn->data.quote.child);
      vm__fmt_append(buf, ">", 1);
      break;
    }
    case SYNTAX_SYNTAX_QUOTE: {
      vm__fmt_append(buf, "<syntax:syntax-quote ", 21);
      vm__fmt_value(buf, syn->data.syntax_quote.child);
      vm__fmt_append(buf, ">", 1);
      break;
    }
    case SYNTAX_UNQUOTE: {
      vm__fmt_append(buf, "<syntax:unquote ", 16);
      vm__fmt_value(buf, syn->data.unquote.child);
      vm__fmt_append(buf, ">", 1);
      break;
    }
    case SYNTAX_UNQUOTE_SPLICING: {
      vm__fmt_append(buf, "<syntax:unquote-splicing ", 24);
      vm__fmt_value(buf, syn->data.unquote_splicing.child);
      vm__fmt_append(buf, ">", 1);
      break;
    }
    default:
      n = snprintf(tmp, sizeof(tmp), "<syntax:%d>", syn->kind);
      vm__fmt_append(buf, tmp, (uint32_t)n);
      break;
    }
  } else {
    vm__fmt_append(buf, "<unknown>", 9);
  }
}

/* Format struct fields from raw data bytes using type def.
 * Reader-symmetric form: [Name field val field val …] — matches the
 * named struct constructor. */
static void vm__fmt_struct_data(VMFormatBuf* buf, StructTypeDef* sdef,
                                const uint8_t* data) {
  char fbuf[32];
  int flen;
  vm__fmt_append(buf, "[", 1);
  vm__fmt_append(buf, sdef->name, sdef->name_len);
  for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
    vm__fmt_append(buf, " ", 1);
    vm__fmt_append(buf, sdef->fields[fi].name, sdef->fields[fi].name_len);
    vm__fmt_append(buf, " ", 1);
    switch (sdef->fields[fi].type) {
      case TYPE_I32: {
        int32_t n; memcpy(&n, data + sdef->fields[fi].offset, 4);
        flen = snprintf(fbuf, sizeof(fbuf), "%d", n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break;
      }
      case TYPE_I64: {
        int64_t n; memcpy(&n, data + sdef->fields[fi].offset, 8);
        flen = snprintf(fbuf, sizeof(fbuf), "%" PRIi64, n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break;
      }
      case TYPE_U32: {
        uint32_t n; memcpy(&n, data + sdef->fields[fi].offset, 4);
        flen = snprintf(fbuf, sizeof(fbuf), "%u", n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break;
      }
      case TYPE_U64: {
        uint64_t n; memcpy(&n, data + sdef->fields[fi].offset, 8);
        flen = snprintf(fbuf, sizeof(fbuf), "%" PRIu64, n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break;
      }
      case TYPE_F32: {
        float f; memcpy(&f, data + sdef->fields[fi].offset, 4);
        flen = snprintf(fbuf, sizeof(fbuf), "%g", (double)f);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break;
      }
      case TYPE_F64: {
        double d; memcpy(&d, data + sdef->fields[fi].offset, 8);
        flen = snprintf(fbuf, sizeof(fbuf), "%g", d);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break;
      }
      case TYPE_BOOL: {
        uint8_t b = data[sdef->fields[fi].offset];
        vm__fmt_append(buf, b ? "true" : "false", b ? 4 : 5); break;
      }
      default: {
        JaclVal fval; memcpy(&fval, data + sdef->fields[fi].offset, sizeof(JaclVal));
        vm__fmt_value(buf, fval); break;
      }
    }
  }
  vm__fmt_append(buf, "]", 1);
}

/* --- Deep structural equality for collections --- */

bool vm__deep_eq(JaclVal a, JaclVal b) {
  return jacl_val_eq(a, b);
}

/* --- Shared struct field marshaling helpers --- */

/* Read a field from struct data and return as JaclVal.
 * Pass heap=NULL for unboxed 64-bit types (raw bits, for typed arithmetic).
 * Pass heap!=NULL for boxed 64-bit types (heap-allocated, for dyn/embed). */
JaclVal vm__heap_record_read_field(ThreadHeap* heap, HeapRecord* s,
                                      uint32_t offset, int field_type) {
  switch ((JaclType)field_type) {
    case TYPE_BOOL: { uint8_t b = s->data[offset]; return jacl_bool(b); }
    case TYPE_I32: { int32_t n; memcpy(&n, s->data + offset, 4); return jacl_i32(n); }
    case TYPE_U32: { uint32_t n; memcpy(&n, s->data + offset, 4); return jacl_u32(n); }
    case TYPE_F32: { float f; memcpy(&f, s->data + offset, 4); return jacl_f32(f); }
    case TYPE_I64: {
      int64_t n; memcpy(&n, s->data + offset, 8);
      return heap ? jacl_i64(heap, n) : (JaclVal)n;
    }
    case TYPE_U64: {
      uint64_t n; memcpy(&n, s->data + offset, 8);
      return heap ? jacl_u64(heap, n) : (JaclVal)n;
    }
    case TYPE_F64: {
      double d; memcpy(&d, s->data + offset, 8);
      if (heap) return jacl_f64(heap, d);
      JaclVal v; memcpy(&v, &d, 8); return v;
    }
    default: {
      JaclVal val; memcpy(&val, s->data + offset, sizeof(JaclVal)); return val;
    }
  }
}

/* Write a JaclVal to a struct field (caller must have already type-checked).
 * For i64/u64/f64, val must contain raw bits (unboxed VM representation). */
void vm__heap_record_write_field(HeapRecord* s, uint32_t offset,
                                    int field_type, JaclVal val) {
  switch ((JaclType)field_type) {
    case TYPE_BOOL: { uint8_t b = jacl_as_bool(val) ? 1 : 0; s->data[offset] = b; break; }
    case TYPE_I32: { int32_t n = jacl_as_i32(val); memcpy(s->data + offset, &n, 4); break; }
    case TYPE_U32: { uint32_t n = jacl_as_u32(val); memcpy(s->data + offset, &n, 4); break; }
    case TYPE_F32: { float f = jacl_as_f32(val); memcpy(s->data + offset, &f, 4); break; }
    case TYPE_I64: { int64_t n = (int64_t)val; memcpy(s->data + offset, &n, 8); break; }
    case TYPE_U64: { uint64_t n = val; memcpy(s->data + offset, &n, 8); break; }
    case TYPE_F64: { double d; memcpy(&d, &val, 8); memcpy(s->data + offset, &d, 8); break; }
    default: { memcpy(s->data + offset, &val, sizeof(JaclVal)); break; }
  }
}

/* Phase 5c: After reifying inline bytes to a heap HeapRecord, fix up any
 * TYPE_STRUCT fields by allocating child heap structs from the raw bytes.
 * This ensures OP_HEAP_RECORD_GET sees tagged pointers (not raw data) for nested
 * struct fields — matching the convention established by OP_HEAP_RECORD_NEW (heap). */
static void vm__reify_nested_heap_records(VM* vm, HeapRecord* s, StructTypeDef* sdef) {
  for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
    if (sdef->fields[fi].type != TYPE_STRUCT) continue;
    uint32_t nidx = sdef->fields[fi].struct_type_idx;
    if (!vm->struct_registry || nidx >= vm->struct_registry->count) continue;
    StructTypeDef* nsdef = vm->struct_registry->defs[nidx];
    gc__current_heap = &vm->heap;
    HeapRecord* ns = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                             sizeof(HeapRecord) + nsdef->total_size);
    ns->type_idx = nidx;
    ns->total_size = nsdef->total_size;
    memcpy(ns->data, s->data + sdef->fields[fi].offset, nsdef->total_size);
    /* Recurse for nested-nested struct fields */
    vm__reify_nested_heap_records(vm, ns, nsdef);
    /* Store tagged pointer in parent's data at field offset */
    JaclVal nval = jacl_heap_record_val(ns);
    memcpy(s->data + sdef->fields[fi].offset, &nval, sizeof(JaclVal));
  }
}

/* --- ctx fork / unfork helpers ---
 *
 * ctx_fork:   allocates a new ctx from the pool, copies data from parent_ctx,
 *             sets vm->ctx to the fork.  Returns the previous vm->ctx.
 *             No-op (returns vm->ctx unchanged) when parent_ctx is nil or
 *             pool is unavailable.
 *
 * ctx_unfork: frees current vm->ctx back to pool (if it differs from
 *             saved_ctx) and restores vm->ctx.
 */
static JaclVal ctx_fork(VM *vm, JaclVal parent_ctx) {
    JaclVal saved = vm->ctx;
    if (parent_ctx == JACL_NIL || !vm->ctx_pool) return saved;
    HeapRecord *src = jacl_as_heap_record_ptr(parent_ctx);
    HeapRecord *dst = ctx_pool_alloc(vm->ctx_pool, &vm->heap);
    if (dst) {
        StructTypeRegistry *reg = vm->struct_registry;
        memcpy(dst->data, src->data, reg->defs[reg->ctx_type_idx]->total_size);
        /* RELEASE: pairs with gc_enumerate_roots' ACQUIRE load of vm.ctx.
         * Without atomic ordering the concurrent GC can see the new pointer
         * but miss the GCHeader fields the allocator wrote first, or trace
         * a torn value. */
        ATOMIC_STORE_EXPLICIT((uint64_t*)&vm->ctx,
                              (uint64_t)jacl_heap_record_val(dst),
                              MEM_RELEASE);
    }
    return saved;
}

static void ctx_unfork(VM *vm, JaclVal saved_ctx) {
    if (vm->ctx != saved_ctx && vm->ctx != JACL_NIL && vm->ctx_pool) {
        ctx_pool_free(vm->ctx_pool, jacl_as_heap_record_ptr(vm->ctx));
    }
    /* RELEASE: see ctx_fork. */
    ATOMIC_STORE_EXPLICIT((uint64_t*)&vm->ctx,
                          (uint64_t)saved_ctx, MEM_RELEASE);
}

/* Forward declaration for recursive call from OP_EACH */
VMResult vm__run(VM* vm, uint32_t min_frame);

/**
 * Execute a bytecode chunk.
 * Returns VM_OK on successful completion (OP_HALT),
 * VM_RUNTIME_ERROR on stack underflow or unknown opcode,
 * VM_STACK_OVERFLOW on stack overflow.
 */
VMResult vm_exec(VM* vm, BytecodeChunk* chunk) {
  vm->error_message = NULL;
  vm->error_line    = 0;

  /* Wrap top-level code in an implicit closure/frame */
  JaclClosure top_closure;
  memset(&top_closure, 0, sizeof(top_closure));
  top_closure.chunk    = *chunk;
  top_closure.variadic = false;

  vm->stack_top = 0;  /* US-009: reset stack after staged macro expansion may have left values */
  vm->frames[0].closure    = &top_closure;
  vm->frames[0].return_ip  = NULL;
  vm->frames[0].stack_base = 0;
  vm->frames[0].chunk      = chunk;
  vm->frame_count   = 1;

  vm->chunk     = chunk;
  vm->top_chunk = chunk;
  vm->ip        = chunk->code;

  return vm__run(vm, 0);
}

/* --- Stream pull helper: unified pull from any stream kind --- */

typedef enum {
    STREAM_PULL_VALUE,     /* Got a value */
    STREAM_PULL_EXHAUSTED, /* Stream is done */
    STREAM_PULL_ERROR      /* Error occurred (vm->error_msg set) */
} StreamPullResult;

/* --- Producer-wide stream rep (task B) ---------------------------------
 * Contract: vm__pull_stream_one yields a WIDE (raw 64-bit) value iff the
 * stream's elem_idx decodes to a wide scalar (i64/u64/f64); otherwise the
 * value is tagged (i32/bool/str/struct/dyn have no wide form). Wide rep is
 * raw bits in the JaclVal slot, exactly as OP_TO_I64 produces. Consumers that
 * feed a dyn/tagged sink use vm__pull_stream_dyn (boxes wide back); consumers
 * that want wide (for-loop, transform mapper body) read the raw value. */
/* A struct-element stream: elem_idx is a struct registry index (below the
 * scalar-sentinel base). Such elements ride the channel as raw inline value
 * bytes (multi-slot) and may only reach typed consumers — see
 * NOT_IMPLEMENTED.md §4.1b. */
static inline bool vm__elem_idx_is_struct(uint32_t elem_idx) {
    return !JACL_IS_SCALAR_TYPE_IDX(elem_idx);
}

static inline bool vm__elem_idx_is_wide(uint32_t elem_idx) {
    if (!JACL_IS_SCALAR_TYPE_IDX(elem_idx)) return false;
    JaclType t = JACL_TYPE_IDX_TO_SCALAR(elem_idx);
    /* i64/u64/f64 all flow wide. The earlier f64/u64 blocker (a tagged element
     * comparing against a differently-tagged literal in a dyn-context predicate)
     * is handled by runtime mixed-numeric promotion in the ordering opcodes
     * (vm__numeric_order) plus yield-time literal narrowing — see
     * LAMBDA_TYPING_PLAN.md. */
    return t == TYPE_I64 || t == TYPE_U64 || t == TYPE_F64;
}

/* Tagged scalar -> wide raw-bits, mirroring OP_TO_I64/U64/F64 with src=dyn. */
static inline JaclVal vm__stream_to_wide(JaclVal v, JaclType t) {
    if (t == TYPE_I64) {
        int64_t i;
        if (jacl_is_i32(v))      i = (int64_t)jacl_as_i32(v);
        else if (jacl_is_u32(v)) i = (int64_t)(uint32_t)jacl_as_u32(v);
        else if (jacl_is_f32(v)) i = (int64_t)jacl_as_f32(v);
        else                     i = jacl_as_i64(v);
        return (JaclVal)(uint64_t)i;
    } else if (t == TYPE_U64) {
        uint64_t u;
        if (jacl_is_i32(v))      u = (uint64_t)(int64_t)jacl_as_i32(v);
        else if (jacl_is_u32(v)) u = (uint64_t)jacl_as_u32(v);
        else                     u = jacl_as_u64(v);
        return (JaclVal)u;
    } else { /* TYPE_F64 */
        double d;
        if (jacl_is_f32(v))      d = (double)jacl_as_f32(v);
        else if (jacl_is_i32(v)) d = (double)jacl_as_i32(v);
        else                     d = jacl_as_f64(v);
        JaclVal out; memcpy(&out, &d, sizeof(double));
        return out;
    }
}

/* Wide raw-bits -> tagged scalar (boxes at a dyn sink). */
static inline JaclVal vm__stream_to_tagged(VM* vm, JaclVal wide, JaclType t) {
    /* Small integers tag as i32 — the canonical small-int dyn rep, and what
     * range/yield emitted pre-flip (so `collect [range 1 5]` stays a vec of
     * i32, matching i32 literals). */
    if (t == TYPE_I64) {
        int64_t i = (int64_t)(uint64_t)wide;
        if (i >= INT32_MIN && i <= INT32_MAX) return jacl_i32((int32_t)i);
        return jacl_i64(&vm->heap, i);
    }
    if (t == TYPE_U64) {
        uint64_t u = (uint64_t)wide;
        if (u <= (uint64_t)INT32_MAX) return jacl_i32((int32_t)u);
        return jacl_u64(&vm->heap, u);
    }
    double d; memcpy(&d, &wide, sizeof(double));
    return jacl_f64(&vm->heap, d);
}

/* --- Mixed-numeric ordering comparison (hybrid contextual-literal support) ---
 * When ordering operands are both numeric but differently tagged (i32/i64/
 * u32/u64/f32/f64), promote and compare rather than erroring. This makes a
 * comparison like `> $it 3.5` work when $it is a tagged f64 stream element and
 * 3.5 is an f32 literal (the dyn-context case where compile-time literal
 * narrowing can't reach), and makes the existing i64-vs-i32 filter comparison
 * principled rather than reliant on i32-for-small box coincidence. Integers
 * compare exactly as int64; any float operand compares as double. */
static inline bool vm__is_numeric_tag(JaclVal v) {
    return jacl_is_i32(v) || jacl_is_u32(v) || jacl_is_f32(v) ||
           jacl_is_i64(v) || jacl_is_u64(v) || jacl_is_f64(v);
}
static inline double vm__as_double_any(JaclVal v) {
    if (jacl_is_f32(v)) return (double)jacl_as_f32(v);
    if (jacl_is_f64(v)) return jacl_as_f64(v);
    if (jacl_is_i32(v)) return (double)jacl_as_i32(v);
    if (jacl_is_u32(v)) return (double)jacl_as_u32(v);
    if (jacl_is_i64(v)) return (double)jacl_as_i64(v);
    return (double)jacl_as_u64(v);
}
static inline int64_t vm__as_i64_any(JaclVal v) {
    if (jacl_is_i32(v)) return (int64_t)jacl_as_i32(v);
    if (jacl_is_u32(v)) return (int64_t)(uint32_t)jacl_as_u32(v);
    if (jacl_is_i64(v)) return jacl_as_i64(v);
    if (jacl_is_u64(v)) return (int64_t)jacl_as_u64(v);
    if (jacl_is_f32(v)) return (int64_t)jacl_as_f32(v);
    return (int64_t)jacl_as_f64(v);
}
/* cmp: 0='>', 1='<', 2='>=', 3='<='. Returns false (caller errors) if either
 * operand is not a numeric scalar. */
static inline bool vm__numeric_order(JaclVal a, JaclVal b, int cmp, JaclVal* out) {
    if (!vm__is_numeric_tag(a) || !vm__is_numeric_tag(b)) return false;
    int c;
    if (jacl_is_f32(a) || jacl_is_f64(a) || jacl_is_f32(b) || jacl_is_f64(b)) {
        double x = vm__as_double_any(a), y = vm__as_double_any(b);
        c = (x < y) ? -1 : (x > y) ? 1 : 0;
    } else {
        int64_t x = vm__as_i64_any(a), y = vm__as_i64_any(b);
        c = (x < y) ? -1 : (x > y) ? 1 : 0;
    }
    bool r = (cmp == 0) ? (c > 0) : (cmp == 1) ? (c < 0)
           : (cmp == 2) ? (c >= 0) : (c <= 0);
    *out = jacl_bool(r);
    return true;
}

/**
 * Pull one element from any stream kind (generator, filter, etc.).
 * Saves and restores VM caller context internally.
 * On STREAM_PULL_VALUE: *out_value = yielded element (WIDE if the stream's
 *   elem_idx is a wide scalar — see the producer-wide contract above).
 * On STREAM_PULL_EXHAUSTED: *out_value = JACL_NIL.
 */
StreamPullResult vm__pull_stream_one(VM* vm, JaclVal stream_val,
                                            JaclVal* out_value) {
    JaclStream* stream = jacl_as_stream(stream_val);

    if (stream->state == STREAM_EXHAUSTED) {
        *out_value = JACL_NIL;
        return STREAM_PULL_EXHAUSTED;
    }

    /* --- Filter stream: pull from source, apply predicate, loop --- */
    if (stream->kind == STREAM_KIND_FILTER) {
        JaclVal source = stream->args[0];
        JaclVal predicate_val = stream->args[1];
        JaclClosure* predicate = jacl_as_closure(predicate_val);

        /* Struct-element streams: the element is N inline value-byte slots.
         * It is pushed to the predicate's by-value struct param (struct HOF
         * monomorphization; compile guard + has_inline_params defense) and,
         * if kept, copied through to the caller's buffer. */
        bool flt_struct = vm__elem_idx_is_struct(stream->elem_idx);
        uint32_t flt_width = 1;
        if (flt_struct) {
            if (!predicate->has_inline_params) {
                vm__set_error(vm, "filter over a struct-element stream "
                                  "requires an inline callback typed against "
                                  "the element");
                return STREAM_PULL_ERROR;
            }
            flt_width = vm__struct_width(
                vm->struct_registry->defs[stream->elem_idx]);
        }

        for (;;) {
            JaclVal elem_buf[VM_MAX_STRUCT_SLOTS];
            StreamPullResult pr = vm__pull_stream_one(vm, source, elem_buf);
            if (pr == STREAM_PULL_EXHAUSTED) {
                stream->state = STREAM_EXHAUSTED;
                *out_value = JACL_NIL;
                return STREAM_PULL_EXHAUSTED;
            }
            if (pr == STREAM_PULL_ERROR) return STREAM_PULL_ERROR;

            /* Call predicate closure with elem. A wide element is boxed back to
             * tagged ONLY when the predicate's param wants tagged (a named/dyn
             * predicate, pred_elem_idx == dyn). When the inline predicate was
             * monomorphized to read the element wide (pred_elem_idx wide,
             * TYPED_CLOSURES_DESIGN.md Phase A) the element is passed wide with
             * no box. The element yielded downstream (*out_value) stays in the
             * source rep. */
            VMResult r;
            r = vm__push(vm, predicate_val);
            if (r != VM_OK) return STREAM_PULL_ERROR;
            if (flt_struct) {
                if (vm->stack_top + flt_width > VM_STACK_MAX) {
                    vm__set_operand_overflow(vm, "filter struct elem");
                    return STREAM_PULL_ERROR;
                }
                memcpy(&vm->stack[vm->stack_top], elem_buf,
                       flt_width * sizeof(JaclVal));
                for (uint32_t si = 0; si < flt_width; si++)
                    BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
                vm->stack_top += flt_width;
            } else {
                JaclVal elem_for_pred = elem_buf[0];
                if (vm__elem_idx_is_wide(stream->elem_idx) &&
                    !vm__elem_idx_is_wide(stream->pred_elem_idx))
                    elem_for_pred = vm__stream_to_tagged(vm, elem_buf[0],
                                      JACL_TYPE_IDX_TO_SCALAR(stream->elem_idx));
                r = vm__push(vm, elem_for_pred);
                if (r != VM_OK) return STREAM_PULL_ERROR;
            }

            if (vm->frame_count >= VM_FRAMES_MAX) {
                vm__set_frame_overflow(vm);
                return STREAM_PULL_ERROR;
            }
            uint32_t cf_count = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = predicate;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - predicate->param_total_slots;
            cf->chunk      = &predicate->chunk;

            uint8_t* save_ip = vm->ip;
            BytecodeChunk* save_chunk = vm->chunk;
            vm->ip    = predicate->chunk.code;
            vm->chunk = &predicate->chunk;

            VMResult inner = vm__run(vm, cf_count);
            if (inner != VM_OK) {
                stream->state = STREAM_ERROR;
                return STREAM_PULL_ERROR;
            }

            JaclVal pred_result;
            r = vm__pop(vm, &pred_result);
            if (r != VM_OK) return STREAM_PULL_ERROR;

            vm->ip    = save_ip;
            vm->chunk = save_chunk;

            if (!vm__is_falsy(pred_result)) {
                memcpy(out_value, elem_buf, flt_width * sizeof(JaclVal));
                return STREAM_PULL_VALUE;
            }
            /* predicate falsy — try next element */
        }
    }

    /* --- Transform stream: pull from source, apply fn, return result --- */
    if (stream->kind == STREAM_KIND_TRANSFORM) {
        JaclVal source = stream->args[0];
        JaclVal fn_val = stream->args[1];
        JaclClosure* fn = jacl_as_closure(fn_val);

        /* Buffer-sized pull: a struct-element SOURCE delivers N inline
         * slots (multi-slot channel); scalar sources write buf[0]. */
        JaclVal elem_buf[VM_MAX_STRUCT_SLOTS];
        StreamPullResult pr = vm__pull_stream_one(vm, source, elem_buf);
        if (pr == STREAM_PULL_EXHAUSTED) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }
        if (pr == STREAM_PULL_ERROR) return STREAM_PULL_ERROR;

        /* Call transform closure with elem. Struct elements are passed as N
         * bitmap-marked inline slots to the mapper's by-value struct param
         * (struct HOF monomorphization — the compiler guarantees only a
         * monomorphized inline mapper reaches a struct source; the
         * has_inline_params check is the dyn-flow defense). Mirrors the
         * eager OP_TYPED_TRANSFORM call loop. */
        uint32_t src_eidx = jacl_is_stream(source)
                            ? jacl_as_stream(source)->elem_idx : 0xFF00u;
        bool src_struct = vm__elem_idx_is_struct(src_eidx);
        VMResult r;
        r = vm__push(vm, fn_val);
        if (r != VM_OK) return STREAM_PULL_ERROR;
        if (src_struct) {
            if (!fn->has_inline_params) {
                vm__set_error(vm, "transform over a struct-element stream "
                                  "requires an inline callback typed against "
                                  "the element");
                return STREAM_PULL_ERROR;
            }
            StructTypeDef* esdef = vm->struct_registry->defs[src_eidx];
            uint32_t ewidth = vm__struct_width(esdef);
            if (vm->stack_top + ewidth > VM_STACK_MAX) {
                vm__set_operand_overflow(vm, "transform struct elem");
                return STREAM_PULL_ERROR;
            }
            memcpy(&vm->stack[vm->stack_top], elem_buf,
                   ewidth * sizeof(JaclVal));
            for (uint32_t si = 0; si < ewidth; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
            vm->stack_top += ewidth;
        } else {
            r = vm__push(vm, elem_buf[0]);
            if (r != VM_OK) return STREAM_PULL_ERROR;
        }

        if (vm->frame_count >= VM_FRAMES_MAX) {
            vm__set_frame_overflow(vm);
            return STREAM_PULL_ERROR;
        }
        uint32_t cf_count = vm->frame_count;
        CallFrame* cf = &vm->frames[vm->frame_count++];
        cf->closure    = fn;
        cf->return_ip  = vm->ip;
        cf->stack_base = vm->stack_top - fn->param_total_slots;
        cf->chunk      = &fn->chunk;

        uint8_t* save_ip = vm->ip;
        BytecodeChunk* save_chunk = vm->chunk;
        vm->ip    = fn->chunk.code;
        vm->chunk = &fn->chunk;

        VMResult inner = vm__run(vm, cf_count);
        if (inner != VM_OK) {
            stream->state = STREAM_ERROR;
            return STREAM_PULL_ERROR;
        }

        /* Struct-returning mapper (multi-slot HOF OUTPUT channel): the
         * stream's elem_idx is the result struct idx; the call left the
         * result as N inline bitmap-marked slots (or a heap ptr —
         * vm__pop_struct handles both). Copy the value bytes into the
         * caller's buffer; downstream consumers ride the same struct
         * channel as generator yields. */
        if (vm__elem_idx_is_struct(stream->elem_idx)) {
            vm__pop_struct(vm, (uint16_t)stream->elem_idx, out_value);
            vm->ip    = save_ip;
            vm->chunk = save_chunk;
            return STREAM_PULL_VALUE;
        }

        JaclVal transformed;
        r = vm__pop(vm, &transformed);
        if (r != VM_OK) return STREAM_PULL_ERROR;

        vm->ip    = save_ip;
        vm->chunk = save_chunk;

        /* Typed-closure return (TYPED_CLOSURES_DESIGN.md Phase A): an inline
         * mapper over a wide-element stream is now compiled with a TYPED wide
         * return (compiler__compile_hof_builtin stashes the enc, HEAD_PROC
         * adopts it), so emit_return leaves the wide tail unboxed and the
         * mapper hands back wide bits directly — no box→unbox round-trip. A
         * wide elem_idx only ever arises from such an inline mapper (var-ref
         * closures infer dyn), so no re-widening is needed here. */
        *out_value = transformed;
        return STREAM_PULL_VALUE;
    }

    /* --- Lines stream: yield one line at a time from source string --- */
    if (stream->kind == STREAM_KIND_LINES) {
        JaclVal src_str = stream->args[0];
        int32_t cur_idx = jacl_as_i32(stream->args[1]);
        uint32_t byte_len = jacl_string_byte_len(src_str);

        if ((uint32_t)cur_idx >= byte_len) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        /* Copy string data to scan for newlines */
        char sbuf[4096];
        char* data = sbuf;
        if (byte_len > sizeof(sbuf)) {
            data = (char*)arena_alloc(vm->arena, byte_len);
        }
        jacl_string_data(src_str, data, byte_len);

        /* Find end of current line */
        uint32_t start = (uint32_t)cur_idx;
        uint32_t end = start;
        while (end < byte_len && data[end] != '\n') {
            end++;
        }
        /* Handle \r\n */
        uint32_t line_end = end;
        if (line_end > start && data[line_end - 1] == '\r') {
            line_end--;
        }
        /* Advance past the newline */
        uint32_t next_idx = end < byte_len ? end + 1 : end;

        /* Skip trailing empty line: if we're at the very end and line is empty */
        if (line_end == start && next_idx >= byte_len) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        stream->args[1] = jacl_i32((int32_t)next_idx);

        /* Create substring */
        JaclVal line_str = jacl_string_new(&vm->heap, vm->intern_table,
                                           data + start, line_end - start);
        *out_value = line_str;
        return STREAM_PULL_VALUE;
    }

    /* --- Take stream: pull from source, decrement remaining count --- */
    if (stream->kind == STREAM_KIND_TAKE) {
        int32_t remaining = jacl_as_i32(stream->args[1]);

        if (remaining <= 0) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        JaclVal source = stream->args[0];
        /* Pull straight into the caller's buffer — rep-agnostic passthrough
         * (a struct-element source writes N slots; scalars write one). */
        StreamPullResult pr = vm__pull_stream_one(vm, source, out_value);
        if (pr == STREAM_PULL_EXHAUSTED) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }
        if (pr == STREAM_PULL_ERROR) return STREAM_PULL_ERROR;

        stream->args[1] = jacl_i32(remaining - 1);
        return STREAM_PULL_VALUE;
    }

    /* --- Exec stream (US-004/US-005): read stdout from spawned process --- */
    if (stream->kind == STREAM_KIND_EXEC) {
        FILE* fp = (FILE*)(uintptr_t)stream->args[0];
        if (!fp) {
            /* Already exhausted - check if we stored an error value */
            if (stream->state == STREAM_ERROR && stream->cached_value != JACL_NIL) {
                *out_value = stream->cached_value;
                vm__slot_set(vm, &stream->cached_value, JACL_NIL);
                return STREAM_PULL_VALUE;  /* Return the error value */
            }
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        /* Read a line from the process output */
        char line_buf[4096];
        if (fgets(line_buf, sizeof(line_buf), fp) == NULL) {
            /* EOF or error - close the pipe and check exit status */
            int pclose_status = pclose(fp);
            stream->args[0] = (JaclVal)0;  /* clear FILE* */

            /* US-005: Check exit code and handle errors */
            int exit_code = WIFEXITED(pclose_status) ? WEXITSTATUS(pclose_status) : 1;

            /* Read and clean up stderr temp file */
            char stderr_path[64];
            if (stream->arg_count >= 3 && jacl_is_string(stream->args[2])) {
                uint32_t path_len = jacl_string_byte_len(stream->args[2]);
                jacl_string_data(stream->args[2], stderr_path, sizeof(stderr_path));
                stderr_path[path_len < sizeof(stderr_path) - 1 ? path_len : sizeof(stderr_path) - 1] = '\0';
            } else {
                stderr_path[0] = '\0';
            }

            /* US-007: Clean up stdin temp file if present */
            if (stream->arg_count >= 4 && jacl_is_string(stream->args[3])) {
                char stdin_path[64];
                uint32_t path_len = jacl_string_byte_len(stream->args[3]);
                jacl_string_data(stream->args[3], stdin_path, sizeof(stdin_path));
                stdin_path[path_len < sizeof(stdin_path) - 1 ? path_len : sizeof(stdin_path) - 1] = '\0';
                unlink(stdin_path);
            }

            if (exit_code != 0) {
                /* Read stderr content */
                char stderr_buf[4096] = "";
                if (stderr_path[0] != '\0') {
                    FILE* stderr_fp = fopen(stderr_path, "r");
                    if (stderr_fp) {
                        size_t nread = fread(stderr_buf, 1, sizeof(stderr_buf) - 1, stderr_fp);
                        stderr_buf[nread] = '\0';
                        fclose(stderr_fp);
                    }
                    unlink(stderr_path);  /* clean up temp file */
                }

                /* Create error value with stderr as message */
                gc__current_heap = &vm->heap;
                JaclVal err_msg;
                if (stderr_buf[0] != '\0') {
                    /* Trim trailing newline */
                    size_t len = strlen(stderr_buf);
                    while (len > 0 && (stderr_buf[len-1] == '\n' || stderr_buf[len-1] == '\r')) {
                        stderr_buf[--len] = '\0';
                    }
                    err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                              stderr_buf, (uint32_t)len);
                } else {
                    /* Default message if no stderr */
                    char default_msg[64];
                    snprintf(default_msg, sizeof(default_msg), "command exited with code %d", exit_code);
                    err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                              default_msg, (uint32_t)strlen(default_msg));
                }

                /* Return error value */
                stream->state = STREAM_ERROR;
                vm__slot_set(vm, &stream->cached_value, JACL_NIL);
                *out_value = jacl_set_error(err_msg);
                return STREAM_PULL_VALUE;  /* Return error as the final value */
            }

            /* Success - clean up stderr temp file */
            if (stderr_path[0] != '\0') {
                unlink(stderr_path);
            }

            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        /* Create string from line (keep newline for now) */
        uint32_t line_len = (uint32_t)strlen(line_buf);
        gc__current_heap = &vm->heap;
        JaclVal line_str = jacl_string_new(&vm->heap, vm->intern_table,
                                           line_buf, line_len);
        *out_value = line_str;
        return STREAM_PULL_VALUE;
    }

    /* --- Exec pipe stream (US-009): read stdout from pipeline of processes --- */
    if (stream->kind == STREAM_KIND_EXEC_PIPE) {
        FILE* fp = (FILE*)(uintptr_t)stream->args[0];
        if (!fp) {
            /* Already exhausted - check if we stored an error value */
            if (stream->state == STREAM_ERROR && stream->cached_value != JACL_NIL) {
                *out_value = stream->cached_value;
                vm__slot_set(vm, &stream->cached_value, JACL_NIL);
                return STREAM_PULL_VALUE;
            }
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        /* Read a line from the pipeline output */
        char line_buf[4096];
        if (fgets(line_buf, sizeof(line_buf), fp) == NULL) {
            /* EOF - close file and wait for all children */
            fclose(fp);
            stream->args[0] = (JaclVal)0;

            /* Parse PIDs from args[1]: "count,pid1,pid2,..." */
            int exit_code = 0;
            if (jacl_is_string(stream->args[1])) {
                char pid_buf[256];
                uint32_t pid_len = jacl_string_byte_len(stream->args[1]);
                jacl_string_data(stream->args[1], pid_buf, sizeof(pid_buf));
                pid_buf[pid_len < sizeof(pid_buf) - 1 ? pid_len : sizeof(pid_buf) - 1] = '\0';

                /* Parse count and PIDs */
                char* p = pid_buf;
                int cmd_count = (int)strtol(p, &p, 10);
                pid_t* pids = (pid_t*)arena_alloc(vm->arena, cmd_count * sizeof(pid_t));
                for (int i = 0; i < cmd_count && *p == ','; i++) {
                    p++;
                    pids[i] = (pid_t)strtol(p, &p, 10);
                }

                /* Wait for all children, get exit status of last */
                for (int i = 0; i < cmd_count; i++) {
                    int status;
                    waitpid(pids[i], &status, 0);
                    if (i == cmd_count - 1) {
                        exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
                    }
                }
            }

            /* Read and clean up stderr temp file */
            char stderr_path[64];
            if (stream->arg_count >= 3 && jacl_is_string(stream->args[2])) {
                uint32_t path_len = jacl_string_byte_len(stream->args[2]);
                jacl_string_data(stream->args[2], stderr_path, sizeof(stderr_path));
                stderr_path[path_len < sizeof(stderr_path) - 1 ? path_len : sizeof(stderr_path) - 1] = '\0';
            } else {
                stderr_path[0] = '\0';
            }

            if (exit_code != 0) {
                /* Read stderr content */
                char stderr_buf[4096] = "";
                if (stderr_path[0] != '\0') {
                    FILE* stderr_fp = fopen(stderr_path, "r");
                    if (stderr_fp) {
                        size_t nread = fread(stderr_buf, 1, sizeof(stderr_buf) - 1, stderr_fp);
                        stderr_buf[nread] = '\0';
                        fclose(stderr_fp);
                    }
                    unlink(stderr_path);
                }

                /* Create error value */
                gc__current_heap = &vm->heap;
                JaclVal err_msg;
                if (stderr_buf[0] != '\0') {
                    size_t len = strlen(stderr_buf);
                    while (len > 0 && (stderr_buf[len-1] == '\n' || stderr_buf[len-1] == '\r')) {
                        stderr_buf[--len] = '\0';
                    }
                    err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                              stderr_buf, (uint32_t)len);
                } else {
                    char default_msg[64];
                    snprintf(default_msg, sizeof(default_msg), "command exited with code %d", exit_code);
                    err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                              default_msg, (uint32_t)strlen(default_msg));
                }

                stream->state = STREAM_ERROR;
                vm__slot_set(vm, &stream->cached_value, JACL_NIL);
                *out_value = jacl_set_error(err_msg);
                return STREAM_PULL_VALUE;
            }

            /* Success - clean up stderr temp file */
            if (stderr_path[0] != '\0') {
                unlink(stderr_path);
            }

            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        /* Create string from line */
        uint32_t line_len = (uint32_t)strlen(line_buf);
        gc__current_heap = &vm->heap;
        JaclVal line_str = jacl_string_new(&vm->heap, vm->intern_table,
                                           line_buf, line_len);
        *out_value = line_str;
        return STREAM_PULL_VALUE;
    }

    /* --- Exec buffer stream (US-006): yield lines from pre-collected stdout --- */
    if (stream->kind == STREAM_KIND_EXEC_BUFFER) {
        JaclVal src_str = stream->args[0];
        int32_t cur_idx = jacl_as_i32(stream->args[1]);
        uint32_t byte_len = jacl_string_byte_len(src_str);

        if ((uint32_t)cur_idx >= byte_len) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        /* Copy string data to scan for newlines */
        char sbuf[4096];
        char* data = sbuf;
        if (byte_len > sizeof(sbuf)) {
            data = (char*)arena_alloc(vm->arena, byte_len);
        }
        jacl_string_data(src_str, data, byte_len);

        /* Find end of current line */
        uint32_t start = (uint32_t)cur_idx;
        uint32_t end = start;
        while (end < byte_len && data[end] != '\n') {
            end++;
        }
        /* Include the newline in the line (like STREAM_KIND_EXEC does) */
        if (end < byte_len) {
            end++;  /* include the \n */
        }
        uint32_t next_idx = end;

        /* Check for end of content */
        if (start >= byte_len || (start == end && next_idx >= byte_len)) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        stream->args[1] = jacl_i32((int32_t)next_idx);

        /* Create substring (includes newline) */
        gc__current_heap = &vm->heap;
        JaclVal line_str = jacl_string_new(&vm->heap, vm->intern_table,
                                           data + start, end - start);
        *out_value = line_str;
        return STREAM_PULL_VALUE;
    }

    /* --- Range stream: yield integers from start to end --- */
    if (stream->kind == STREAM_KIND_RANGE) {
        int64_t current   = jacl_as_i64(stream->args[0]);
        int64_t end_bound = jacl_as_i64(stream->args[1]);
        int32_t inclusive  = jacl_as_i32(stream->args[2]);

        bool in_range = inclusive ? (current <= end_bound) : (current < end_bound);
        if (!in_range) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        /* Advance to next value */
        gc__current_heap = &vm->heap;
        stream->args[0] = jacl_i64(&vm->heap, current + 1);

        /* Producer-wide: range elements are i64 -> yield wide raw bits (no
         * heap-alloc for large values). Falls back to tagged only if the
         * stream is somehow not wide-typed (defensive). */
        if (vm__elem_idx_is_wide(stream->elem_idx)) {
            *out_value = (JaclVal)(uint64_t)current;
        } else if (current >= INT32_MIN && current <= INT32_MAX) {
            *out_value = jacl_i32((int32_t)current);
        } else {
            *out_value = jacl_i64(&vm->heap, current);
        }
        return STREAM_PULL_VALUE;
    }

    /* --- Generator stream (state machine) --- */
    uint32_t caller_stack_top   = vm->stack_top;
    uint32_t caller_frame_count = vm->frame_count;
    uint8_t* caller_ip          = vm->ip;
    BytecodeChunk* caller_chunk = vm->chunk;
    VMResult r;

    /* State machine generator: call sm_closure(state_obj, nil) */
    JaclStateMachine* sm = jacl_as_state_machine(stream->state_machine);
    JaclClosure* sm_cl = jacl_as_closure(sm->sm_closure);

    r = vm__push(vm, sm->sm_closure);
    if (r != VM_OK) return STREAM_PULL_ERROR;
    r = vm__push(vm, stream->state_machine);
    if (r != VM_OK) return STREAM_PULL_ERROR;
    r = vm__push(vm, JACL_NIL);
    if (r != VM_OK) return STREAM_PULL_ERROR;

    if (vm->frame_count >= VM_FRAMES_MAX) {
        vm__set_frame_overflow(vm);
        return STREAM_PULL_ERROR;
    }
    CallFrame* nf = &vm->frames[vm->frame_count++];
    nf->closure    = sm_cl;
    nf->return_ip  = NULL;
    nf->stack_base = vm->stack_top - 2;
    nf->chunk      = &sm_cl->chunk;
    vm->ip    = sm_cl->chunk.code;
    vm->chunk = &sm_cl->chunk;

    VMResult inner = vm__run(vm, caller_frame_count);

    if (inner == VM_YIELD) {
        stream->state        = STREAM_CONSUMED;
        vm__slot_set(vm, &stream->cached_value, vm->yield_value);
        vm->stack_top   = caller_stack_top;
        vm->frame_count = caller_frame_count;
        vm->ip          = caller_ip;
        vm->chunk       = caller_chunk;
        /* Producer-wide: generators yield tagged (cached_value above stays
         * tagged and GC-safe); hand a wide value to the consumer for a
         * wide-scalar element stream. Struct elements: OP_YIELD_SM_WIDE
         * parked the raw inline bytes in vm->yield_wide (yield_value is nil,
         * so the cached_value set above is a harmless nil) — copy them into
         * the caller's buffer (out_value must hold the struct's slot width;
         * the only struct-pulling caller is OP_STREAM_NEXT_INLINE, which
         * passes a VM_MAX_STRUCT_SLOTS buffer). Pure value bytes — no GC
         * rooting needed. */
        if (vm__elem_idx_is_struct(stream->elem_idx)) {
            memcpy(out_value, vm->yield_wide,
                   vm->yield_wide_width * sizeof(JaclVal));
        } else if (vm__elem_idx_is_wide(stream->elem_idx))
            *out_value = vm__stream_to_wide(vm->yield_value,
                            JACL_TYPE_IDX_TO_SCALAR(stream->elem_idx));
        else
            *out_value = vm->yield_value;
        return STREAM_PULL_VALUE;
    } else if (inner == VM_OK) {
        /* Check if SM function returned an error value */
        if (vm->stack_top > caller_stack_top) {
            JaclVal sm_ret = vm->stack[vm->stack_top - 1];
            if (jacl_is_error(sm_ret)) {
                stream->state = STREAM_ERROR;
                vm->stack_top   = caller_stack_top;
                vm->frame_count = caller_frame_count;
                vm->ip          = caller_ip;
                vm->chunk       = caller_chunk;
                return STREAM_PULL_ERROR;
            }
        }
        stream->state        = STREAM_EXHAUSTED;
        vm__slot_set(vm, &stream->cached_value, JACL_NIL);
        vm->stack_top   = caller_stack_top;
        vm->frame_count = caller_frame_count;
        vm->ip          = caller_ip;
        vm->chunk       = caller_chunk;
        *out_value = JACL_NIL;
        return STREAM_PULL_EXHAUSTED;
    } else {
        stream->state = STREAM_ERROR;
        return STREAM_PULL_ERROR;
    }
}

/* Pull one element and normalize to TAGGED rep. For consumers that feed a
 * dyn/tagged sink (collect/spread/each/exec-stdin) — they keep their existing
 * tagged logic; the producer-wide flip is transparent to them. */
static StreamPullResult vm__pull_stream_dyn(VM* vm, JaclVal stream_val,
                                            JaclVal* out_value) {
    JaclStream* stream = jacl_as_stream(stream_val);
    uint32_t eidx = stream->elem_idx;
    /* Struct elements cannot be normalized to a dyn slot — auto-boxing is
     * forbidden (STRUCT_DESIGN.md) and out_value is a single slot. Typed
     * consumers (for-loop via OP_STREAM_NEXT_INLINE) are the only legal
     * sinks; everything else errors here (NOT_IMPLEMENTED.md §4.1b). */
    if (vm__elem_idx_is_struct(eidx)) {
        vm__set_error(vm, "struct-element streams require a typed consumer "
                          "(for-loop); collect/spread/each/stream_next are "
                          "not yet supported");
        return STREAM_PULL_ERROR;
    }
    StreamPullResult pr = vm__pull_stream_one(vm, stream_val, out_value);
    if (pr == STREAM_PULL_VALUE && vm__elem_idx_is_wide(eidx))
        *out_value = vm__stream_to_tagged(vm, *out_value,
                          JACL_TYPE_IDX_TO_SCALAR(eidx));
    return pr;
}

/* Helper: Build shell command string from args vector.
 * Returns pointer to cmd_buf on success, NULL on error (error set in vm).
 * cmd_buf must be at least 4096 bytes. */
static char* vm__exec_build_cmd(VM* vm, JaclVal args_vec, char* cmd_buf, size_t cmd_buf_size) {
  if (!jacl_is_vector(args_vec)) {
    vm__set_error(vm, "exec requires a vector of arguments, got %s",
                 vm__type_name(args_vec));
    return NULL;
  }

  jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(args_vec);
  uint32_t argc = jacl_vec_count(vec);
  if (argc == 0) {
    vm__set_error(vm, "exec requires at least a command name");
    return NULL;
  }

  char* p = cmd_buf;
  char* end = cmd_buf + cmd_buf_size - 1;

  for (uint32_t i = 0; i < argc && p < end; i++) {
    jacl_vec_get_result gr = jacl_vec_get(vec, i);
    JaclVal arg_val = gr.value;
    if (!jacl_is_string(arg_val)) {
      vm__set_error(vm, "exec argument %d must be a string, got %s",
                   (int)i, vm__type_name(arg_val));
      return NULL;
    }
    uint32_t arg_len = jacl_string_byte_len(arg_val);
    char arg_buf[1024];
    if (arg_len >= sizeof(arg_buf)) arg_len = sizeof(arg_buf) - 1;
    jacl_string_data(arg_val, arg_buf, arg_len + 1);
    arg_buf[arg_len] = '\0';

    if (i > 0 && p < end) *p++ = ' ';

    /* Simple quoting: if the arg contains spaces or special chars, quote it */
    int needs_quote = 0;
    for (uint32_t j = 0; j < arg_len; j++) {
      char c = arg_buf[j];
      if (c == ' ' || c == '\t' || c == '"' || c == '\'' ||
          c == '\\' || c == '$' || c == '`' || c == '!' ||
          c == '*' || c == '?' || c == '[' || c == ']' ||
          c == '(' || c == ')' || c == '{' || c == '}' ||
          c == '<' || c == '>' || c == '|' || c == '&' ||
          c == ';' || c == '\n') {
        needs_quote = 1;
        break;
      }
    }

    if (needs_quote) {
      if (p < end) *p++ = '\'';
      for (uint32_t j = 0; j < arg_len && p < end; j++) {
        char c = arg_buf[j];
        if (c == '\'') {
          if (p + 4 <= end) {
            *p++ = '\'';
            *p++ = '\\';
            *p++ = '\'';
            *p++ = '\'';
          }
        } else {
          *p++ = c;
        }
      }
      if (p < end) *p++ = '\'';
    } else {
      for (uint32_t j = 0; j < arg_len && p < end; j++) {
        *p++ = arg_buf[j];
      }
    }
  }
  *p = '\0';
  return cmd_buf;
}

/* Helper: Collect stdin value (string or stream) into a buffer.
 * Returns 0 on success, -1 on error (pushed error to stack for stream errors).
 * On success, *out_buf and *out_len are set. */
static int vm__exec_collect_stdin(VM* vm, JaclVal stdin_val, char** out_buf, size_t* out_len) {
  size_t stdin_cap = 4096;
  size_t stdin_len = 0;
  char* stdin_buf = (char*)arena_alloc(vm->arena, stdin_cap);

  if (jacl_is_string(stdin_val)) {
    stdin_len = jacl_string_byte_len(stdin_val);
    if (stdin_len >= stdin_cap) {
      stdin_cap = stdin_len + 1;
      stdin_buf = (char*)arena_alloc(vm->arena, stdin_cap);
    }
    jacl_string_data(stdin_val, stdin_buf, (uint32_t)(stdin_cap));
  } else if (jacl_is_stream(stdin_val)) {
    JaclStream* src_stream = jacl_as_stream(stdin_val);

    while (src_stream->state != STREAM_EXHAUSTED) {
      JaclVal elem;
      StreamPullResult pr = vm__pull_stream_dyn(vm, stdin_val, &elem);
      if (pr == STREAM_PULL_ERROR) return -1;
      if (pr == STREAM_PULL_EXHAUSTED) break;

      /* Handle error values from upstream */
      if (jacl_is_error(elem)) {
        vm__push(vm, elem);
        return -2; /* special: error value pushed to stack */
      }

      /* Convert element to string */
      const char* elem_data = NULL;
      uint32_t elem_len = 0;
      char numbuf[32];

      if (jacl_is_string(elem)) {
        elem_len = jacl_string_byte_len(elem);
        char* tmp = (char*)arena_alloc(vm->arena, elem_len + 1);
        jacl_string_data(elem, tmp, elem_len + 1);
        elem_data = tmp;
      } else if (jacl_is_i32(elem)) {
        snprintf(numbuf, sizeof(numbuf), "%d", jacl_as_i32(elem));
        elem_data = numbuf;
        elem_len = (uint32_t)strlen(numbuf);
      } else if (jacl_is_f64(elem)) {
        snprintf(numbuf, sizeof(numbuf), "%g", jacl_as_f64(elem));
        elem_data = numbuf;
        elem_len = (uint32_t)strlen(numbuf);
      } else if (jacl_is_nil(elem)) {
        continue;
      } else {
        VMFormatBuf fmt;
        vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
        vm__fmt_value(&fmt, elem);
        elem_data = fmt.data;
        elem_len = fmt.len;
      }

      int needs_newline = (elem_len == 0 || elem_data[elem_len - 1] != '\n');

      while (stdin_len + elem_len + 2 >= stdin_cap) {
        size_t new_cap = stdin_cap * 2;
        char* new_buf = (char*)arena_alloc(vm->arena, new_cap);
        memcpy(new_buf, stdin_buf, stdin_len);
        stdin_buf = new_buf;
        stdin_cap = new_cap;
      }

      memcpy(stdin_buf + stdin_len, elem_data, elem_len);
      stdin_len += elem_len;

      if (needs_newline) {
        stdin_buf[stdin_len++] = '\n';
      }
    }
  } else {
    vm__set_error(vm, "exec stdin must be a string or stream, got %s",
                 vm__type_name(stdin_val));
    return -1;
  }
  stdin_buf[stdin_len] = '\0';
  *out_buf = stdin_buf;
  *out_len = stdin_len;
  return 0;
}

/**
 * Inner dispatch loop. Runs until OP_HALT or until frame_count drops
 * to min_frame (used by OP_EACH to execute closures inline).
 *
 * §13 (2026-05-14): dispatch is direct-threaded (computed-goto) on
 * GCC/Clang non-WASM targets, falling back to a switch on
 * Emscripten / other compilers. The audit measured `vm__run` at
 * 57% of `box_churn` CPU under the central-switch dispatch; pushing
 * the next-opcode jump down into each handler's tail lets the
 * branch predictor learn per-opcode successor patterns. Emscripten
 * lowers `&&label` to a single WASM `br_table` (loses the per-site
 * prediction benefit), so we keep the switch there.
 */
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__EMSCRIPTEN__)
#  define JACL_VM_COMPUTED_GOTO 1
#endif

/* --- Atom watcher fire helper ---
 *
 * Snapshots the watcher list's fn entries onto the operand stack (so they're
 * GC-rooted across the closure calls), then iterates and invokes each as
 * `fn(old, new)`. Watcher fns are validated at registration time to take
 * exactly 2 params; non-conformant entries are skipped defensively.
 *
 * Watcher errors propagate (return the error VMResult); subsequent watchers
 * in this fire are skipped. Re-entry — a watcher that swap/resets the same
 * atom — triggers a fresh fire cycle on a *new* operand-stack snapshot,
 * bounded by VM_FRAMES_MAX. The wl pointer we load is immutable
 * (copy-on-write), so concurrent watch/unwatch on another thread can't
 * mutate our snapshot mid-fire.
 */
static VMResult vm__fire_atom_watchers(VM* vm, JaclMutableRef* ref,
                                        JaclVal old_val, JaclVal new_val) {
  JaclWatcherList* wl = (JaclWatcherList*)ATOMIC_LOAD_EXPLICIT(
      (void**)ATOM_WATCHERS_SLOT(ref), MEM_ACQUIRE);
  if (!wl || wl->count == 0) return VM_OK;

  uint32_t count = wl->count;
  uint32_t snap_base = vm->stack_top;
  if (vm->stack_top + count > VM_STACK_MAX) {
    vm__set_operand_overflow(vm, "atom watcher snapshot");
    return VM_STACK_OVERFLOW;
  }
  for (uint32_t i = 0; i < count; i++) {
    vm->stack[vm->stack_top++] = WATCHER_FN(wl, i);
  }
  /* From here on, the fn snapshot is GC-rooted on the operand stack. */

  uint8_t* saved_ip = vm->ip;
  BytecodeChunk* saved_chunk = vm->chunk;

  for (uint32_t i = 0; i < count; i++) {
    JaclVal fn = vm->stack[snap_base + i];
    if (!jacl_is_closure(fn)) continue;
    JaclClosure* cl = jacl_as_closure(fn);
    if (cl->param_count != 2) continue;

    if (vm->frame_count >= VM_FRAMES_MAX) {
      vm->stack_top = snap_base;
      vm__set_frame_overflow(vm);
      return VM_RUNTIME_ERROR;
    }
    if (vm->stack_top + 3 > VM_STACK_MAX) {
      vm->stack_top = snap_base;
      vm__set_operand_overflow(vm, "atom watcher call");
      return VM_STACK_OVERFLOW;
    }

    vm->stack[vm->stack_top++] = fn;       /* callee */
    vm->stack[vm->stack_top++] = old_val;
    vm->stack[vm->stack_top++] = new_val;

    uint32_t caller_frame_count = vm->frame_count;
    CallFrame* cf = &vm->frames[vm->frame_count++];
    cf->closure    = cl;
    cf->return_ip  = vm->ip;
    cf->stack_base = vm->stack_top - 2;    /* points at old_val; callee one below */
    cf->chunk      = &cl->chunk;
    vm->ip    = cl->chunk.code;
    vm->chunk = &cl->chunk;

    VMResult call_result = vm__run(vm, caller_frame_count);
    if (call_result != VM_OK) {
      vm->stack_top = snap_base;
      return call_result;
    }

    /* Discard the watcher's return value. */
    if (vm->stack_top > snap_base + count) vm->stack_top--;
  }

  vm->ip = saved_ip;
  vm->chunk = saved_chunk;
  vm->stack_top = snap_base;
  return VM_OK;
}

/* --- Atom watcher list COW builder (used by OP_WATCH / OP_UNWATCH) ---
 *
 * Constructs a new watcher list reflecting an add or remove operation,
 * then CAS-publishes it into the atom's slot. Returns true on success,
 * false on a CAS race (caller retries) or allocation failure.
 *
 * If `remove_key` is true, the entry matching `key` is dropped (no-op if
 * absent). Otherwise the entry is added (or its fn replaced if `key` is
 * already present).
 *
 * GC barriers: each new (key, fn) entry is greyed via gc_write_barrier
 * so SATB sees fresh references; the atom is added to the remembered set
 * if old-gen-to-young-gen.
 */
static bool vm__atom_watchers_rebind(VM* vm, JaclMutableRef* ref,
                                      JaclVal atom_val, JaclVal key,
                                      JaclVal fn, bool remove_key) {
  JaclWatcherList* wl_old = (JaclWatcherList*)ATOMIC_LOAD_EXPLICIT(
      (void**)ATOM_WATCHERS_SLOT(ref), MEM_ACQUIRE);
  uint32_t old_count = wl_old ? wl_old->count : 0;

  /* Find existing key */
  uint32_t found = UINT32_MAX;
  for (uint32_t i = 0; i < old_count; i++) {
    if (jacl_val_eq(WATCHER_KEY(wl_old, i), key)) { found = i; break; }
  }

  if (remove_key && found == UINT32_MAX) {
    /* Nothing to remove — already gone */
    return true;
  }

  uint32_t new_count;
  if (remove_key) {
    new_count = old_count - 1;
  } else if (found != UINT32_MAX) {
    new_count = old_count;  /* replacing existing fn */
  } else {
    new_count = old_count + 1;
  }

  JaclWatcherList* new_wl = NULL;
  if (new_count > 0) {
    /* Round capacity up to a power of two (min 4) for amortized growth */
    uint32_t cap = 4;
    while (cap < new_count) cap *= 2;
    gc__current_heap = &vm->heap;
    new_wl = (JaclWatcherList*)gc_alloc(
        &vm->heap, OBJ_WATCHER_LIST,
        sizeof(JaclWatcherList) + (size_t)2 * cap * sizeof(JaclVal));
    if (!new_wl) return false;
    new_wl->count = new_count;
    new_wl->capacity = cap;

    /* Copy from old, skipping removed entry or replacing existing fn */
    uint32_t dst = 0;
    for (uint32_t i = 0; i < old_count; i++) {
      if (remove_key && i == found) continue;
      JaclVal k = WATCHER_KEY(wl_old, i);
      JaclVal f = (found == i && !remove_key) ? fn : WATCHER_FN(wl_old, i);
      WATCHER_KEY(new_wl, dst) = k;
      WATCHER_FN(new_wl, dst)  = f;
      dst++;
    }
    /* Append new entry if not replacing */
    if (!remove_key && found == UINT32_MAX) {
      WATCHER_KEY(new_wl, dst) = key;
      WATCHER_FN(new_wl, dst)  = fn;
      dst++;
    }
    /* SATB: grey each entry so concurrent mark sees them */
    for (uint32_t i = 0; i < new_count; i++) {
      gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                       JACL_NIL, WATCHER_KEY(new_wl, i));
      gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                       JACL_NIL, WATCHER_FN(new_wl, i));
    }
  }

  /* CAS publish: only succeed if slot still equals wl_old */
  JaclWatcherList* expected = wl_old;
  if (!ATOMIC_CAS((void**)ATOM_WATCHERS_SLOT(ref), (void**)&expected,
                  (void*)new_wl, MEM_ACQ_REL, MEM_ACQUIRE)) {
    /* Race: another thread rebound between our load and CAS. Caller retries. */
    return false;
  }

  /* Generational remembered-set barrier: if the atom is old-gen and the
   * new wl is young-gen, the atom must be visited on the next minor GC.
   * Inlined here because the standard barrier expects two JaclVals; we
   * have a JaclVal + a non-tagged heap pointer. */
  if (vm->remembered_set && new_wl && jacl_is_heap_type(atom_val)) {
    GCHeader* atom_hdr = gc_header_of(jacl_as_ptr(atom_val));
    GCHeader* wl_hdr   = gc_header_of(new_wl);
    if (atom_hdr->gen == 1 && wl_hdr->gen == 0) {
      remembered_set_push(vm->remembered_set, atom_val);
    }
  }
  return true;
}

VMResult vm__run(VM* vm, uint32_t min_frame) {
  CallFrame* frame = &vm->frames[vm->frame_count - 1];
  uint8_t   instruction;
  VMResult  result;

  /* Per-dispatch prelude: GC safepoint + line tracking + opcode fetch.
   * Inlined into each DISPATCH() so the indirect jump at the end of
   * every opcode handler stays a one-instruction tail — that's what
   * gives the branch predictor a per-opcode prediction site. */
  #define VM_PRELUDE() do {                                                    \
      if (ATOMIC_LOAD_EXPLICIT(&vm->heap.needs_gc, MEM_RELAXED)) {              \
        if (!vm->runtime) {                                                    \
          if (gc_should_major(&vm->heap)) gc_collect(&vm->heap, vm);           \
          else gc_collect_minor(&vm->heap, vm, vm->remembered_set);            \
        } else {                                                               \
          ATOMIC_STORE_EXPLICIT(&vm->heap.needs_gc, false, MEM_RELAXED);       \
          ATOMIC_STORE_EXPLICIT(&vm->heap.bytes_since_gc, 0, MEM_RELAXED);     \
          gc_concurrent_trigger(vm->runtime);                                  \
        }                                                                      \
      }                                                                        \
      vm->error_line = vm->chunk->lines[(uint32_t)(vm->ip - vm->chunk->code)]; \
      instruction = vm__read_byte(vm);                                         \
    } while (0)

#ifdef JACL_VM_COMPUTED_GOTO
  /* Direct-threaded dispatch table. One entry per opcode in OpCode
   * declaration order. MUST mirror the OpCode enum in `bytecode.c`
   * (the version the unity build sees — jacl.h's mirror is one
   * shorter as of 2026-05-14; pre-existing enum drift, tracked
   * separately). Add a new opcode? Add a row here AND a CASE(op):
   * handler below in the same pass — otherwise dispatch crashes or
   * fails to compile. */
  static void* const dispatch_table[] = {
    [OP_CONST] = &&L_OP_CONST,
    [OP_NIL] = &&L_OP_NIL,
    [OP_TRUE] = &&L_OP_TRUE,
    [OP_FALSE] = &&L_OP_FALSE,
    [OP_POP] = &&L_OP_POP,
    [OP_ADD] = &&L_OP_ADD,
    [OP_SUB] = &&L_OP_SUB,
    [OP_MUL] = &&L_OP_MUL,
    [OP_DIV] = &&L_OP_DIV,
    [OP_MOD] = &&L_OP_MOD,
    [OP_NEG] = &&L_OP_NEG,
    [OP_EQ] = &&L_OP_EQ,
    [OP_LT] = &&L_OP_LT,
    [OP_GT] = &&L_OP_GT,
    [OP_LE] = &&L_OP_LE,
    [OP_GE] = &&L_OP_GE,
    [OP_PRINT] = &&L_OP_PRINT,
    [OP_DEF_GLOBAL] = &&L_OP_DEF_GLOBAL,
    [OP_GET_GLOBAL] = &&L_OP_GET_GLOBAL,
    [OP_GET_LOCAL] = &&L_OP_GET_LOCAL,
    [OP_SET_LOCAL] = &&L_OP_SET_LOCAL,
    [OP_GET_UPVALUE] = &&L_OP_GET_UPVALUE,
    [OP_JUMP] = &&L_OP_JUMP,
    [OP_JUMP_IF_FALSE] = &&L_OP_JUMP_IF_FALSE,
    [OP_LOOP] = &&L_OP_LOOP,
    [OP_CALL] = &&L_OP_CALL,
    [OP_TAIL_CALL] = &&L_OP_TAIL_CALL,
    [OP_RETURN] = &&L_OP_RETURN,
    [OP_RETURN_WIDE] = &&L_OP_RETURN_WIDE,
    [OP_CLOSURE] = &&L_OP_CLOSURE,
    [OP_POP_N] = &&L_OP_POP_N,
    [OP_CONCAT] = &&L_OP_CONCAT,
    [OP_STR_LEN] = &&L_OP_STR_LEN,
    [OP_STR_BYTE_LEN] = &&L_OP_STR_BYTE_LEN,
    [OP_STR_INDEX] = &&L_OP_STR_INDEX,
    [OP_STR_SLICE] = &&L_OP_STR_SLICE,
    [OP_TO_STRING] = &&L_OP_TO_STRING,
    [OP_VEC] = &&L_OP_VEC,
    [OP_VEC_GET] = &&L_OP_VEC_GET,
    [OP_VEC_LEN] = &&L_OP_VEC_LEN,
    [OP_VEC_PUSH] = &&L_OP_VEC_PUSH,
    [OP_VEC_SET] = &&L_OP_VEC_SET,
    [OP_VEC_CONCAT] = &&L_OP_VEC_CONCAT,
    [OP_VEC_SLICE] = &&L_OP_VEC_SLICE,
    [OP_VEC_SPREAD] = &&L_OP_VEC_SPREAD,
    [OP_ARR] = &&L_OP_ARR,
    [OP_ARR_GET] = &&L_OP_ARR_GET,
    [OP_ARR_SET] = &&L_OP_ARR_SET,
    [OP_ARR_PUSH] = &&L_OP_ARR_PUSH,
    [OP_ARR_POP] = &&L_OP_ARR_POP,
    [OP_ARR_LEN] = &&L_OP_ARR_LEN,
    [OP_TYPED_ARR] = &&L_OP_TYPED_ARR,
    [OP_TYPED_ARR_PUSH] = &&L_OP_TYPED_ARR_PUSH,
    [OP_TYPED_ARR_SET] = &&L_OP_TYPED_ARR_SET,
    [OP_MAP] = &&L_OP_MAP,
    [OP_MAP_GET] = &&L_OP_MAP_GET,
    [OP_MAP_HAS] = &&L_OP_MAP_HAS,
    [OP_MAP_LEN] = &&L_OP_MAP_LEN,
    [OP_MAP_SET] = &&L_OP_MAP_SET,
    [OP_MAP_REMOVE] = &&L_OP_MAP_REMOVE,
    [OP_MAP_KEYS] = &&L_OP_MAP_KEYS,
    [OP_MAP_VALS] = &&L_OP_MAP_VALS,
    [OP_EACH] = &&L_OP_EACH,
    [OP_TRANSFORM] = &&L_OP_TRANSFORM,
    [OP_FILTER] = &&L_OP_FILTER,
    [OP_ERROR] = &&L_OP_ERROR,
    [OP_IS_ERROR] = &&L_OP_IS_ERROR,
    [OP_ERROR_VAL] = &&L_OP_ERROR_VAL,
    [OP_CHECK_ERROR] = &&L_OP_CHECK_ERROR,
    [OP_JUMP_IF_ERROR] = &&L_OP_JUMP_IF_ERROR,
    [OP_PANIC] = &&L_OP_PANIC,
    [OP_STACK_TRACE] = &&L_OP_STACK_TRACE,
    [OP_MAKE_CELL] = &&L_OP_MAKE_CELL,
    [OP_GET_CELL_LOCAL] = &&L_OP_GET_CELL_LOCAL,
    [OP_SET_CELL_LOCAL] = &&L_OP_SET_CELL_LOCAL,
    [OP_GET_CELL_UPVALUE] = &&L_OP_GET_CELL_UPVALUE,
    [OP_SET_CELL_UPVALUE] = &&L_OP_SET_CELL_UPVALUE,
    [OP_SET_GLOBAL] = &&L_OP_SET_GLOBAL,
    [OP_BOX] = &&L_OP_BOX,
    [OP_BOX_UNCHECKED] = &&L_OP_BOX_UNCHECKED,
    [OP_BOX_STRUCT] = &&L_OP_BOX_STRUCT,
    [OP_ATOM] = &&L_OP_ATOM,
    [OP_DEREF] = &&L_OP_DEREF,
    [OP_RESET] = &&L_OP_RESET,
    [OP_SWAP] = &&L_OP_SWAP,
    [OP_IS_BOX] = &&L_OP_IS_BOX,
    [OP_IS_BOX_TYPED] = &&L_OP_IS_BOX_TYPED,
    [OP_IS_ATOM] = &&L_OP_IS_ATOM,
    [OP_IS_FUTURE] = &&L_OP_IS_FUTURE,
    [OP_AWAIT] = &&L_OP_AWAIT,
    [OP_SPAWN] = &&L_OP_SPAWN,
    [OP_RESOLVE_FUTURE] = &&L_OP_RESOLVE_FUTURE,
    [OP_PARALLEL] = &&L_OP_PARALLEL,
    [OP_RACE] = &&L_OP_RACE,
    [OP_COMPLETE_PARALLEL] = &&L_OP_COMPLETE_PARALLEL,
    [OP_COMPLETE_RACE] = &&L_OP_COMPLETE_RACE,
    [OP_ADD_I64] = &&L_OP_ADD_I64,
    [OP_SUB_I64] = &&L_OP_SUB_I64,
    [OP_MUL_I64] = &&L_OP_MUL_I64,
    [OP_DIV_I64] = &&L_OP_DIV_I64,
    [OP_MOD_I64] = &&L_OP_MOD_I64,
    [OP_NEG_I64] = &&L_OP_NEG_I64,
    [OP_LT_I64] = &&L_OP_LT_I64,
    [OP_GT_I64] = &&L_OP_GT_I64,
    [OP_LE_I64] = &&L_OP_LE_I64,
    [OP_GE_I64] = &&L_OP_GE_I64,
    [OP_EQ_I64] = &&L_OP_EQ_I64,
    [OP_DIV_U64] = &&L_OP_DIV_U64,
    [OP_MOD_U64] = &&L_OP_MOD_U64,
    [OP_LT_U64] = &&L_OP_LT_U64,
    [OP_GT_U64] = &&L_OP_GT_U64,
    [OP_LE_U64] = &&L_OP_LE_U64,
    [OP_GE_U64] = &&L_OP_GE_U64,
    [OP_ADD_F64] = &&L_OP_ADD_F64,
    [OP_SUB_F64] = &&L_OP_SUB_F64,
    [OP_MUL_F64] = &&L_OP_MUL_F64,
    [OP_DIV_F64] = &&L_OP_DIV_F64,
    [OP_MOD_F64] = &&L_OP_MOD_F64,
    [OP_NEG_F64] = &&L_OP_NEG_F64,
    [OP_LT_F64] = &&L_OP_LT_F64,
    [OP_GT_F64] = &&L_OP_GT_F64,
    [OP_LE_F64] = &&L_OP_LE_F64,
    [OP_GE_F64] = &&L_OP_GE_F64,
    [OP_EQ_F64] = &&L_OP_EQ_F64,
    [OP_TO_I32] = &&L_OP_TO_I32,
    [OP_TO_I64] = &&L_OP_TO_I64,
    [OP_TO_U32] = &&L_OP_TO_U32,
    [OP_TO_U64] = &&L_OP_TO_U64,
    [OP_TO_F32] = &&L_OP_TO_F32,
    [OP_TO_F64] = &&L_OP_TO_F64,
    [OP_TO_DYN] = &&L_OP_TO_DYN,
    [OP_CONST_I64] = &&L_OP_CONST_I64,
    [OP_CONST_U64] = &&L_OP_CONST_U64,
    [OP_CONST_F64] = &&L_OP_CONST_F64,
    [OP_HEAP_RECORD_NEW] = &&L_OP_HEAP_RECORD_NEW,
    [OP_HEAP_RECORD_GET] = &&L_OP_HEAP_RECORD_GET,
    [OP_HEAP_RECORD_SET] = &&L_OP_HEAP_RECORD_SET,
    [OP_HEAP_RECORD_GET_DYN] = &&L_OP_HEAP_RECORD_GET_DYN,
    [OP_HEAP_RECORD_SET_DYN] = &&L_OP_HEAP_RECORD_SET_DYN,
    [OP_HEAP_RECORD_GET_INLINE] = &&L_OP_HEAP_RECORD_GET_INLINE,
    [OP_HEAP_RECORD_SET_INLINE] = &&L_OP_HEAP_RECORD_SET_INLINE,
    [OP_RESET_INLINE] = &&L_OP_RESET_INLINE,
    [OP_STRUCT_NEW_INLINE] = &&L_OP_STRUCT_NEW_INLINE,
    [OP_STRUCT_GET_INLINE] = &&L_OP_STRUCT_GET_INLINE,
    [OP_STRUCT_SET_INLINE] = &&L_OP_STRUCT_SET_INLINE,
    [OP_STRUCT_STORE_INLINE] = &&L_OP_STRUCT_STORE_INLINE,
    [OP_STRUCT_GET_UPVALUE] = &&L_OP_STRUCT_GET_UPVALUE,
    [OP_STRUCT_SET_UPVALUE] = &&L_OP_STRUCT_SET_UPVALUE,
    [OP_LOAD_INLINE_LOCAL] = &&L_OP_LOAD_INLINE_LOCAL,
    [OP_LOAD_INLINE_UPVALUE] = &&L_OP_LOAD_INLINE_UPVALUE,
    [OP_PRINT_STRUCT] = &&L_OP_PRINT_STRUCT,
    [OP_STRUCT_EQ_TOS] = &&L_OP_STRUCT_EQ_TOS,
    [OP_STRUCT_GET_INLINE_TOS] = &&L_OP_STRUCT_GET_INLINE_TOS,
    [OP_STRUCT_EXPAND] = &&L_OP_STRUCT_EXPAND,
    [OP_STRUCT_EQ_INLINE] = &&L_OP_STRUCT_EQ_INLINE,
    [OP_STRUCT_HASH_INLINE] = &&L_OP_STRUCT_HASH_INLINE,
    [OP_HASH] = &&L_OP_HASH,
    [OP_CLOSE_LOOP] = &&L_OP_CLOSE_LOOP,
    [OP_DESTRUCTURE_VEC] = &&L_OP_DESTRUCTURE_VEC,
    [OP_DESTRUCTURE_NAMED] = &&L_OP_DESTRUCTURE_NAMED,
    [OP_DESTRUCTURE_VEC_REST] = &&L_OP_DESTRUCTURE_VEC_REST,
    [OP_DESTRUCTURE_NAMED_REST] = &&L_OP_DESTRUCTURE_NAMED_REST,
    [OP_SPREAD] = &&L_OP_SPREAD,
    [OP_CALL_SPREAD] = &&L_OP_CALL_SPREAD,
    [OP_FOLD_SPREAD] = &&L_OP_FOLD_SPREAD,
    [OP_COLLECT_VARIADIC] = &&L_OP_COLLECT_VARIADIC,
    [OP_YIELD] = &&L_OP_YIELD,
    [OP_STREAM_NEXT] = &&L_OP_STREAM_NEXT,
    [OP_COLLECT] = &&L_OP_COLLECT,
    [OP_IS_STREAM_EXHAUSTED] = &&L_OP_IS_STREAM_EXHAUSTED,
    [OP_COUNT] = &&L_OP_COUNT,
    [OP_TAKE] = &&L_OP_TAKE,
    [OP_FIRST] = &&L_OP_FIRST,
    [OP_LINES] = &&L_OP_LINES,
    [OP_GET_STATE_FIELD] = &&L_OP_GET_STATE_FIELD,
    [OP_SET_STATE_FIELD] = &&L_OP_SET_STATE_FIELD,
    [OP_GET_RESUME_POINT] = &&L_OP_GET_RESUME_POINT,
    [OP_SET_RESUME_POINT] = &&L_OP_SET_RESUME_POINT,
    [OP_YIELD_SM] = &&L_OP_YIELD_SM,
    [OP_AWAIT_SM] = &&L_OP_AWAIT_SM,
    [OP_SLEEP_SM] = &&L_OP_SLEEP_SM,
    [OP_SLEEP_BLOCK] = &&L_OP_SLEEP_BLOCK,
    [OP_CALL_SUSPEND] = &&L_OP_CALL_SUSPEND,
    [OP_GET_STATE_FIELD_CELL] = &&L_OP_GET_STATE_FIELD_CELL,
    [OP_SET_STATE_FIELD_CELL] = &&L_OP_SET_STATE_FIELD_CELL,
    [OP_GET_STATE_FIELD_WIDE] = &&L_OP_GET_STATE_FIELD_WIDE,
    [OP_SET_STATE_FIELD_WIDE] = &&L_OP_SET_STATE_FIELD_WIDE,
    [OP_SYNTAX_SPLICE] = &&L_OP_SYNTAX_SPLICE,
    [OP_SYNTAX_OP] = &&L_OP_SYNTAX_OP,
    [OP_INTERPRET] = &&L_OP_INTERPRET,
    [OP_INTERPRET_PRELUDE] = &&L_OP_INTERPRET_PRELUDE,
    [OP_EXEC] = &&L_OP_EXEC,
    [OP_AWAIT_JOB] = &&L_OP_AWAIT_JOB,
    [OP_SIGNAL] = &&L_OP_SIGNAL,
    [OP_HALT] = &&L_OP_HALT,
    [OP_GET_CTX] = &&L_OP_GET_CTX,
    [OP_CTX_FORK] = &&L_OP_CTX_FORK,
    [OP_CTX_RESTORE] = &&L_OP_CTX_RESTORE,
    [OP_SET_CTX] = &&L_OP_SET_CTX,
    [OP_RANGE] = &&L_OP_RANGE,
    [OP_OPTIONAL_GET] = &&L_OP_OPTIONAL_GET,
    [OP_TYPED_VEC] = &&L_OP_TYPED_VEC,
    [OP_TYPED_VEC_PUSH] = &&L_OP_TYPED_VEC_PUSH,
    [OP_TYPED_VEC_SET] = &&L_OP_TYPED_VEC_SET,
    [OP_TYPED_VEC_LEN] = &&L_OP_TYPED_VEC_LEN,
    [OP_TYPED_MAP] = &&L_OP_TYPED_MAP,
    [OP_TYPED_MAP_SET] = &&L_OP_TYPED_MAP_SET,
    [OP_TYPED_MAP_HAS] = &&L_OP_TYPED_MAP_HAS,
    [OP_TYPED_MAP_REMOVE] = &&L_OP_TYPED_MAP_REMOVE,
    [OP_TYPED_MAP_LEN] = &&L_OP_TYPED_MAP_LEN,
    [OP_TYPED_MAP_KEYS] = &&L_OP_TYPED_MAP_KEYS,
    [OP_TYPED_MAP_VALS] = &&L_OP_TYPED_MAP_VALS,
    [OP_TYPED_VEC_PRINT] = &&L_OP_TYPED_VEC_PRINT,
    [OP_TYPED_MAP_PRINT] = &&L_OP_TYPED_MAP_PRINT,
    [OP_TYPED_VEC_EQ] = &&L_OP_TYPED_VEC_EQ,
    [OP_TYPED_MAP_EQ] = &&L_OP_TYPED_MAP_EQ,
    [OP_IS_BOX_TYPED_VEC] = &&L_OP_IS_BOX_TYPED_VEC,
    [OP_IS_BOX_TYPED_MAP] = &&L_OP_IS_BOX_TYPED_MAP,
    [OP_TYPED_VEC_CONCAT] = &&L_OP_TYPED_VEC_CONCAT,
    [OP_TYPED_VEC_SLICE] = &&L_OP_TYPED_VEC_SLICE,
    [OP_TYPED_EACH] = &&L_OP_TYPED_EACH,
    [OP_TYPED_TRANSFORM] = &&L_OP_TYPED_TRANSFORM,
    [OP_TYPED_FILTER] = &&L_OP_TYPED_FILTER,
    [OP_TYPED_VEC_GET_INLINE] = &&L_OP_TYPED_VEC_GET_INLINE,
    [OP_TYPED_MAP_GET_INLINE] = &&L_OP_TYPED_MAP_GET_INLINE,
    [OP_INLINE_TO_LOCAL] = &&L_OP_INLINE_TO_LOCAL,
    [OP_DEREF_INLINE] = &&L_OP_DEREF_INLINE,
    [OP_YIELD_SM_WIDE] = &&L_OP_YIELD_SM_WIDE,
    [OP_STREAM_NEXT_INLINE] = &&L_OP_STREAM_NEXT_INLINE,
    [OP_PTR_LOAD] = &&L_OP_PTR_LOAD,
    [OP_PTR_STORE] = &&L_OP_PTR_STORE,
    [OP_PTR_OFFSET] = &&L_OP_PTR_OFFSET,
    [OP_PTR_DIFF] = &&L_OP_PTR_DIFF,
    [OP_PTR_ADD_OFFSET] = &&L_OP_PTR_ADD_OFFSET,
    [OP_PTR_LOAD_INLINE] = &&L_OP_PTR_LOAD_INLINE,
    [OP_PTR_STORE_INLINE] = &&L_OP_PTR_STORE_INLINE,
    [OP_PRINT_PTR] = &&L_OP_PRINT_PTR,
    [OP_READ_FILE] = &&L_OP_READ_FILE,
    [OP_WRITE_FILE] = &&L_OP_WRITE_FILE,
    [OP_APPEND_FILE] = &&L_OP_APPEND_FILE,
    [OP_WATCH] = &&L_OP_WATCH,
    [OP_UNWATCH] = &&L_OP_UNWATCH,
    [OP_BUF_ZERO_LOCAL] = &&L_OP_BUF_ZERO_LOCAL,
    [OP_BUF_GET_LOCAL]  = &&L_OP_BUF_GET_LOCAL,
    [OP_BUF_SET_LOCAL]  = &&L_OP_BUF_SET_LOCAL,
    [OP_BUF_ADDR_LOCAL] = &&L_OP_BUF_ADDR_LOCAL,
    [OP_BUF_GET_STRUCT_LOCAL] = &&L_OP_BUF_GET_STRUCT_LOCAL,
    [OP_BUF_SET_STRUCT_LOCAL] = &&L_OP_BUF_SET_STRUCT_LOCAL,
    [OP_BUF_UGET_LOCAL] = &&L_OP_BUF_UGET_LOCAL,
    [OP_BUF_USET_LOCAL] = &&L_OP_BUF_USET_LOCAL,
    [OP_BUF_STORE_OFF]  = &&L_OP_BUF_STORE_OFF,
    [OP_PTR_OFFSET_CHECKED] = &&L_OP_PTR_OFFSET_CHECKED,
    [OP_INLINE_COPY_LOCAL] = &&L_OP_INLINE_COPY_LOCAL,
  };

  #define CASE(op)   L_##op
  #define DISPATCH() do {                                                      \
      VM_PRELUDE();                                                            \
      if ((size_t)instruction >=                                               \
          sizeof(dispatch_table)/sizeof(*dispatch_table))                      \
        goto L_unknown_opcode;                                                 \
      goto *dispatch_table[instruction];                                       \
    } while (0)

  DISPATCH();  /* enter dispatch */
#else
  #define CASE(op)   case op
  #define DISPATCH() break

  for (;;) {
    VM_PRELUDE();

    switch (instruction) {
#endif

      CASE(OP_CONST): {
        uint16_t index = vm__read_u16(vm);
        result = vm__push(vm, vm->chunk->constants[index]);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_NIL): {
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TRUE): {
        result = vm__push(vm, JACL_TRUE);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_FALSE): {
        result = vm__push(vm, JACL_FALSE);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_POP): {
        JaclVal discard;
        result = vm__pop(vm, &discard);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_ADD): {
        VM__BINARY_NUMERIC_OP(jacl_add_i32, jacl_add_f32, jacl_u32_add, "+");
        DISPATCH();
      }

      CASE(OP_SUB): {
        VM__BINARY_NUMERIC_OP(jacl_sub_i32, jacl_sub_f32, jacl_u32_sub, "-");
        DISPATCH();
      }

      CASE(OP_MUL): {
        VM__BINARY_NUMERIC_OP(jacl_mul_i32, jacl_mul_f32, jacl_u32_mul, "*");
        DISPATCH();
      }

      CASE(OP_DIV): {
        VM__BINARY_NUMERIC_OP(jacl_div_i32, jacl_div_f32, jacl_u32_div, "/");
        DISPATCH();
      }

      CASE(OP_MOD): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal b, a;
        result = vm__pop(vm, &b); if (result != VM_OK) return result;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        if (jacl_is_i32(a) && jacl_is_i32(b)) {
          JaclVal mod_res = jacl_mod_i32(a, b);
          if (jacl_is_error(mod_res) && !jacl_is_error(a) && !jacl_is_error(b))
            vm__capture_trace(vm);
          result = vm__push(vm, mod_res);
          if (result != VM_OK) return result;
        } else if (jacl_is_f32(a) && jacl_is_f32(b)) {
          VM_ERROR(vm,
            "type error in '%%': modulo is not supported for f32");
        } else if (jacl_is_u32(a) && jacl_is_u32(b)) {
          JaclVal mod_res = jacl_u32_mod(a, b);
          if (jacl_is_error(mod_res) && !jacl_is_error(a) && !jacl_is_error(b))
            vm__capture_trace(vm);
          result = vm__push(vm, mod_res);
          if (result != VM_OK) return result;
        } else {
          VM_ERROR(vm,
            "type error in '%%': expected matching numeric types, got %s and %s",
            vm__type_name(a), vm__type_name(b));
        }
        DISPATCH();
      }

      CASE(OP_NEG): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal a;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        JaclVal res;
        if (jacl_is_i32(a)) {
          res = jacl_neg_i32(a);
        } else if (jacl_is_f32(a)) {
          res = jacl_neg_f32(a);
        } else if (jacl_is_u32(a)) {
          res = jacl_u32_neg(a);
        } else {
          VM_ERROR(vm,
            "type error in '-': expected numeric type, got %s",
            vm__type_name(a));
        }
        result = vm__push(vm, res); if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_EQ): {
        JaclVal b, a;
        result = vm__pop(vm, &b); if (result != VM_OK) return result;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        JaclVal res = jacl_bool(vm__deep_eq(a, b));
        result = vm__push(vm, res);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_LT): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal b, a;
        result = vm__pop(vm, &b); if (result != VM_OK) return result;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        JaclVal res;
        if (jacl_is_i32(a) && jacl_is_i32(b)) {
          res = jacl_lt_i32(a, b);
        } else if (jacl_is_f32(a) && jacl_is_f32(b)) {
          res = jacl_lt_f32(a, b);
        } else if (jacl_is_u32(a) && jacl_is_u32(b)) {
          res = jacl_u32_lt(a, b);
        } else if (jacl_is_string(a) && jacl_is_string(b)) {
          res = jacl_bool(jacl_string_cmp(a, b) < 0);
        } else if (vm__numeric_order(a, b, 1, &res)) {
          /* mixed numeric tags: promoted compare */
        } else {
          VM_ERROR(vm,
            "type error in '<': expected matching types, got %s and %s",
            vm__type_name(a), vm__type_name(b));
        }
        result = vm__push(vm, res); if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_GT): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal b, a;
        result = vm__pop(vm, &b); if (result != VM_OK) return result;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        JaclVal res;
        if (jacl_is_i32(a) && jacl_is_i32(b)) {
          res = jacl_gt_i32(a, b);
        } else if (jacl_is_f32(a) && jacl_is_f32(b)) {
          res = jacl_gt_f32(a, b);
        } else if (jacl_is_u32(a) && jacl_is_u32(b)) {
          res = jacl_u32_gt(a, b);
        } else if (jacl_is_string(a) && jacl_is_string(b)) {
          res = jacl_bool(jacl_string_cmp(a, b) > 0);
        } else if (vm__numeric_order(a, b, 0, &res)) {
          /* mixed numeric tags: promoted compare */
        } else {
          VM_ERROR(vm,
            "type error in '>': expected matching types, got %s and %s",
            vm__type_name(a), vm__type_name(b));
        }
        result = vm__push(vm, res); if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_LE): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal b, a;
        result = vm__pop(vm, &b); if (result != VM_OK) return result;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        JaclVal res;
        if (jacl_is_i32(a) && jacl_is_i32(b)) {
          res = jacl_le_i32(a, b);
        } else if (jacl_is_f32(a) && jacl_is_f32(b)) {
          res = jacl_le_f32(a, b);
        } else if (jacl_is_u32(a) && jacl_is_u32(b)) {
          res = jacl_u32_le(a, b);
        } else if (jacl_is_string(a) && jacl_is_string(b)) {
          res = jacl_bool(jacl_string_cmp(a, b) <= 0);
        } else if (vm__numeric_order(a, b, 3, &res)) {
          /* mixed numeric tags: promoted compare */
        } else {
          VM_ERROR(vm,
            "type error in '<=': expected matching types, got %s and %s",
            vm__type_name(a), vm__type_name(b));
        }
        result = vm__push(vm, res); if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_GE): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal b, a;
        result = vm__pop(vm, &b); if (result != VM_OK) return result;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        JaclVal res;
        if (jacl_is_i32(a) && jacl_is_i32(b)) {
          res = jacl_ge_i32(a, b);
        } else if (jacl_is_f32(a) && jacl_is_f32(b)) {
          res = jacl_ge_f32(a, b);
        } else if (jacl_is_u32(a) && jacl_is_u32(b)) {
          res = jacl_u32_ge(a, b);
        } else if (jacl_is_string(a) && jacl_is_string(b)) {
          res = jacl_bool(jacl_string_cmp(a, b) >= 0);
        } else if (vm__numeric_order(a, b, 2, &res)) {
          /* mixed numeric tags: promoted compare */
        } else {
          VM_ERROR(vm,
            "type error in '>=': expected matching types, got %s and %s",
            vm__type_name(a), vm__type_name(b));
        }
        result = vm__push(vm, res); if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_PRINT): {
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;

        /* Cells are transparent — dereference before printing */
        if (jacl_is_cell(val)) {
          JaclMutableRef* ref = jacl_as_cell(val);
          val = MREF_VAL(ref);
        }

        char buf[256];
        const char* text;
        uint32_t len;

        if (jacl_is_error(val)) {
          /* Format as <error: PAYLOAD> using the format buffer */
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
          vm__fmt_append(&fmt, "<error: ", 8);
          JaclVal payload = jacl_clear_error(val);
          /* Print payload: strings without quotes, other types with fmt_value */
          if (jacl_is_string(payload)) {
            uint32_t slen = jacl_string_byte_len(payload);
            if (jacl_is_heap_string(payload)) {
              JaclHeapString* hs = jacl_as_heap_string(payload);
              vm__fmt_append(&fmt, hs->data, hs->byte_len);
            } else {
              char sbuf[8];
              jacl_string_data(payload, sbuf, slen);
              vm__fmt_append(&fmt, sbuf, slen);
            }
          } else {
            vm__fmt_value(&fmt, payload);
          }
          vm__fmt_append(&fmt, ">\n", 2);
          vm->print_fn(fmt.data, fmt.len, vm->print_ctx);
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;
          DISPATCH();
        } else if (jacl_is_nil(val)) {
          text = "nil\n";
          len = 4;
        } else if (jacl_is_bool(val)) {
          if (val == JACL_TRUE) { text = "true\n"; len = 5; }
          else { text = "false\n"; len = 6; }
        } else if (jacl_is_i32(val)) {
          int n = snprintf(buf, sizeof(buf), "%d\n", (int)jacl_as_i32(val));
          text = buf;
          len = (uint32_t)n;
        } else if (jacl_is_u32(val)) {
          int n = snprintf(buf, sizeof(buf), "%u\n", (unsigned)jacl_as_u32(val));
          text = buf;
          len = (uint32_t)n;
        } else if (jacl_is_f32(val)) {
          int n = snprintf(buf, sizeof(buf), "%g\n", (double)jacl_as_f32(val));
          text = buf;
          len = (uint32_t)n;
        } else if (jacl_is_i64(val)) {
          int n = snprintf(buf, sizeof(buf), "%" PRIi64 "\n", jacl_as_i64(val));
          text = buf;
          len = (uint32_t)n;
        } else if (jacl_is_u64(val)) {
          int n = snprintf(buf, sizeof(buf), "%" PRIu64 "\n", jacl_as_u64(val));
          text = buf;
          len = (uint32_t)n;
        } else if (jacl_is_f64(val)) {
          int n = snprintf(buf, sizeof(buf), "%g\n", jacl_as_f64(val));
          text = buf;
          len = (uint32_t)n;
        } else if (jacl_is_string(val)) {
          uint32_t slen = jacl_string_byte_len(val);
          if (slen + 1 <= sizeof(buf)) {
            jacl_string_data(val, buf, slen);
            buf[slen] = '\n';
            text = buf;
            len = slen + 1;
          } else {
            /* String too long for stack buffer: print data then newline */
            if (jacl_is_heap_string(val)) {
              JaclHeapString* hs = jacl_as_heap_string(val);
              vm->print_fn(hs->data, hs->byte_len, vm->print_ctx);
            } else if (jacl_is_rope_string(val)) {
              /* Stream rope content leaf-by-leaf via cursor */
              JaclRopeString* rs = jacl_as_rope_string(val);
              rope_cursor cur = rope_cursor_new(rs->r, 0);
              size_t remaining = slen;
              while (remaining > 0) {
                size_t chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);
                size_t got = rope_cursor_read(&cur, (uint8_t*)buf, chunk);
                if (got == 0) break;
                vm->print_fn(buf, (uint32_t)got, vm->print_ctx);
                rope_cursor_advance_bytes(&cur, got);
                remaining -= got;
              }
              rope_cursor_free(cur);
            } else {
              /* Inline string (max 7 bytes, should never reach here) */
              jacl_string_data(val, buf, sizeof(buf));
              vm->print_fn(buf, slen, vm->print_ctx);
            }
            vm->print_fn("\n", 1, vm->print_ctx);
            result = vm__push(vm, JACL_NIL);
            if (result != VM_OK) return result;
            DISPATCH();
          }
        } else if (jacl_is_struct(val)) {
          HeapRecord* s = jacl_as_heap_record_ptr(val);
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
          if (vm->struct_registry && s->type_idx < vm->struct_registry->count) {
            StructTypeDef* sdef = vm->struct_registry->defs[s->type_idx];
            vm__fmt_struct_bytes(&fmt, sdef, s->data);
          } else {
            vm__fmt_append(&fmt, "<struct>", 8);
          }
          vm__fmt_append(&fmt, "\n", 1);
          vm->print_fn(fmt.data, fmt.len, vm->print_ctx);
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;
          DISPATCH();
        } else if (jacl_is_vector(val) || jacl_is_arr(val) || jacl_is_map(val) || jacl_is_box(val) || jacl_is_atom(val) || jacl_is_future(val) || jacl_is_stream(val) || jacl_is_typed_vector(val) || jacl_is_typed_map(val)) {
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
          vm__fmt_value(&fmt, val);
          vm__fmt_append(&fmt, "\n", 1);
          vm->print_fn(fmt.data, fmt.len, vm->print_ctx);
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;
          DISPATCH();
        } else {
          text = "<unknown>\n";
          len = 10;
        }

        vm->print_fn(text, len, vm->print_ctx);

        /* print returns nil */
        result = vm__push(vm, JACL_NIL); if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_DEF_GLOBAL): {
        uint16_t name_idx = vm__read_u16(vm);
        JaclVal name = vm->chunk->constants[name_idx];
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        vm__env_set(vm, name, value);
        /* def returns nil */
        result = vm__push(vm, JACL_NIL); if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_GET_GLOBAL): {
        uint16_t name_idx = vm__read_u16(vm);
        uint8_t* ic_slot_ptr = vm->ip;     /* points at the IC u16 */
        uint16_t cache_slot = vm__read_u16(vm);
        JaclVal name = vm->chunk->constants[name_idx];
        if (cache_slot < vm->env.count &&
            vm->env.names[cache_slot] == name) {
          result = vm__push(vm, vm->env.values[cache_slot]);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        /* Miss — linear scan. */
        bool found;
        JaclVal value = vm__env_get(vm, name, &found);
        if (!found) {
          char name_buf[130];
          uint32_t nlen = jacl_string_data(name, name_buf, sizeof(name_buf) - 1);
          if (nlen >= sizeof(name_buf)) nlen = sizeof(name_buf) - 1;
          name_buf[nlen] = '\0';
          vm__set_error(vm, "undefined variable '$%s'", name_buf);
          return VM_RUNTIME_ERROR;
        }
        /* Patch the cache slot with the resolved env index. */
        for (uint32_t k = 0; k < vm->env.count; k++) {
          if (vm->env.names[k] == name) {
            ic_slot_ptr[0] = (uint8_t)((k >> 8) & 0xFF);
            ic_slot_ptr[1] = (uint8_t)(k & 0xFF);
            break;
          }
        }
        result = vm__push(vm, value); if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_GET_LOCAL): {
        uint8_t slot = vm__read_byte(vm);
        result = vm__push(vm, vm->stack[frame->stack_base + slot]);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_SET_LOCAL): {
        uint8_t slot = vm__read_byte(vm);
        vm->stack[frame->stack_base + slot] = vm->stack[vm->stack_top - 1];
        DISPATCH();
      }

      CASE(OP_BUF_ZERO_LOCAL): {
        /* Zero-init a [Buf N T] local. Marks the spanned frame slots as
         * inline (raw bytes) so the GC walker skips them. See
         * BUFFER_DESIGN.md. */
        uint8_t  base_slot  = vm__read_byte(vm);
        uint16_t byte_count = vm__read_u16(vm);
        uint32_t slot_count = (byte_count + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        memset(&vm->stack[frame->stack_base + base_slot], 0,
               (size_t)slot_count * sizeof(JaclVal));
        for (uint32_t si = 0; si < slot_count; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, frame->stack_base + base_slot + si);
        }
        DISPATCH();
      }

      CASE(OP_BUF_GET_LOCAL): {
        /* Indexed read from a [Buf N T] local. Pops i32 index, bounds-
         * checks against buf_len (in elements), loads element of
         * declared type, widens small ints (i8/i16) to i32, pushes
         * tagged value. See BUFFER_DESIGN.md. */
        uint8_t  base_slot = vm__read_byte(vm);
        uint8_t  elem_type = vm__read_byte(vm);
        uint16_t buf_len   = vm__read_u16(vm);
        JaclVal  idx_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        if (!jacl_is_i32(idx_val)) {
          vm__set_error(vm, "buf-get: index must be i32, got %s",
                        vm__type_name(idx_val));
          return VM_RUNTIME_ERROR;
        }
        int32_t idx = jacl_as_i32(idx_val);
        if (idx < 0 || (uint32_t)idx >= buf_len) {
          vm__set_error(vm, "buf-get: index %d out of bounds for [Buf %u %s]",
                        (int)idx, (unsigned)buf_len,
                        type_name((JaclType)elem_type));
          return VM_RUNTIME_ERROR;
        }
        uint8_t* base = (uint8_t*)&vm->stack[frame->stack_base + base_slot];
        JaclVal  loaded;
        switch ((JaclType)elem_type) {
          case TYPE_BOOL: {
            uint8_t b = base[(uint32_t)idx];
            loaded = jacl_bool(b != 0);
            break;
          }
          case TYPE_I8: {
            int8_t v; memcpy(&v, base + (uint32_t)idx, 1);
            loaded = jacl_i32((int32_t)v); /* sign-extend to i32 */
            break;
          }
          case TYPE_U8: {
            uint8_t v = base[(uint32_t)idx];
            loaded = jacl_i32((int32_t)v); /* zero-extend; surfaces as i32 */
            break;
          }
          case TYPE_I16: {
            int16_t v; memcpy(&v, base + (uint32_t)idx * 2, 2);
            loaded = jacl_i32((int32_t)v);
            break;
          }
          case TYPE_U16: {
            uint16_t v; memcpy(&v, base + (uint32_t)idx * 2, 2);
            loaded = jacl_i32((int32_t)v);
            break;
          }
          case TYPE_I32: {
            int32_t v; memcpy(&v, base + (uint32_t)idx * 4, 4);
            loaded = jacl_i32(v);
            break;
          }
          case TYPE_U32: {
            uint32_t v; memcpy(&v, base + (uint32_t)idx * 4, 4);
            loaded = jacl_u32(v);
            break;
          }
          case TYPE_F32: {
            float v; memcpy(&v, base + (uint32_t)idx * 4, 4);
            loaded = jacl_f32(v);
            break;
          }
          case TYPE_I64: {
            int64_t v; memcpy(&v, base + (uint32_t)idx * 8, 8);
            loaded = jacl_i64(&vm->heap, v);
            break;
          }
          case TYPE_U64: {
            uint64_t v; memcpy(&v, base + (uint32_t)idx * 8, 8);
            loaded = jacl_u64(&vm->heap, v);
            break;
          }
          case TYPE_F64: {
            double v; memcpy(&v, base + (uint32_t)idx * 8, 8);
            loaded = jacl_f64(&vm->heap, v);
            break;
          }
          case TYPE_DYN:
          case TYPE_STR:
          case TYPE_VEC:
          case TYPE_MAP:
          case TYPE_CLOSURE:
          case TYPE_STREAM:
          case TYPE_TYPED_VEC:
          case TYPE_TYPED_MAP:
          case TYPE_PTR:
          case TYPE_FUTURE:
          case TYPE_BOX: {
            /* Ref-element buf (M4.4): one JaclVal slot per index. Each
             * slot is GC-traced via the normal stack walker -- no inline-
             * bitmap entry was set at zero-init, so the marker sees them. */
            loaded = vm->stack[frame->stack_base + base_slot + (uint32_t)idx];
            break;
          }
          default:
            vm__set_error(vm, "buf-get: unsupported element type %u",
                          (unsigned)elem_type);
            return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, loaded); if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_BUF_SET_LOCAL): {
        /* Indexed write to a [Buf N T] local. Pops value then index,
         * bounds-checks, narrows i32 -> u8/i16/etc with the same
         * truncation a C cast would do (no overflow trap in M2 — add
         * a typer constant-fold check later if desired). */
        uint8_t  base_slot = vm__read_byte(vm);
        uint8_t  elem_type = vm__read_byte(vm);
        uint16_t buf_len   = vm__read_u16(vm);
        JaclVal  val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        JaclVal  idx_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        if (!jacl_is_i32(idx_val)) {
          vm__set_error(vm, "buf-set: index must be i32, got %s",
                        vm__type_name(idx_val));
          return VM_RUNTIME_ERROR;
        }
        int32_t idx = jacl_as_i32(idx_val);
        if (idx < 0 || (uint32_t)idx >= buf_len) {
          vm__set_error(vm, "buf-set: index %d out of bounds for [Buf %u %s]",
                        (int)idx, (unsigned)buf_len,
                        type_name((JaclType)elem_type));
          return VM_RUNTIME_ERROR;
        }
        uint8_t* base = (uint8_t*)&vm->stack[frame->stack_base + base_slot];
        switch ((JaclType)elem_type) {
          case TYPE_BOOL: {
            if (!jacl_is_bool(val)) {
              vm__set_error(vm, "buf-set: expected bool, got %s",
                            vm__type_name(val));
              return VM_RUNTIME_ERROR;
            }
            base[(uint32_t)idx] = jacl_as_bool(val) ? 1 : 0;
            break;
          }
          case TYPE_I8: case TYPE_U8: case TYPE_I16: case TYPE_U16:
          case TYPE_I32: case TYPE_U32: {
            int32_t v;
            if (jacl_is_i32(val)) v = jacl_as_i32(val);
            else if (jacl_is_u32(val)) v = (int32_t)jacl_as_u32(val);
            else {
              vm__set_error(vm, "buf-set: expected i32/u32, got %s",
                            vm__type_name(val));
              return VM_RUNTIME_ERROR;
            }
            switch ((JaclType)elem_type) {
              case TYPE_I8:  case TYPE_U8:  base[(uint32_t)idx] = (uint8_t)v; break;
              case TYPE_I16: case TYPE_U16: {
                uint16_t w = (uint16_t)v;
                memcpy(base + (uint32_t)idx * 2, &w, 2);
                break;
              }
              case TYPE_I32: case TYPE_U32: {
                memcpy(base + (uint32_t)idx * 4, &v, 4);
                break;
              }
              default: break;
            }
            break;
          }
          case TYPE_F32: {
            float v;
            if (jacl_is_f32(val)) v = jacl_as_f32(val);
            else if (jacl_is_f64(val)) v = (float)jacl_as_f64(val);
            else {
              vm__set_error(vm, "buf-set: expected f32/f64, got %s",
                            vm__type_name(val));
              return VM_RUNTIME_ERROR;
            }
            memcpy(base + (uint32_t)idx * 4, &v, 4);
            break;
          }
          case TYPE_I64: {
            if (!jacl_is_i64(val) && !jacl_is_i32(val)) {
              vm__set_error(vm, "buf-set: expected i64, got %s",
                            vm__type_name(val));
              return VM_RUNTIME_ERROR;
            }
            int64_t v = jacl_is_i64(val) ? jacl_as_i64(val)
                                          : (int64_t)jacl_as_i32(val);
            memcpy(base + (uint32_t)idx * 8, &v, 8);
            break;
          }
          case TYPE_U64: {
            if (!jacl_is_u64(val)) {
              vm__set_error(vm, "buf-set: expected u64, got %s",
                            vm__type_name(val));
              return VM_RUNTIME_ERROR;
            }
            uint64_t v = jacl_as_u64(val);
            memcpy(base + (uint32_t)idx * 8, &v, 8);
            break;
          }
          case TYPE_F64: {
            if (!jacl_is_f64(val) && !jacl_is_f32(val)) {
              vm__set_error(vm, "buf-set: expected f64, got %s",
                            vm__type_name(val));
              return VM_RUNTIME_ERROR;
            }
            double v = jacl_is_f64(val) ? jacl_as_f64(val)
                                         : (double)jacl_as_f32(val);
            memcpy(base + (uint32_t)idx * 8, &v, 8);
            break;
          }
          case TYPE_DYN:
          case TYPE_STR:
          case TYPE_VEC:
          case TYPE_MAP:
          case TYPE_CLOSURE:
          case TYPE_STREAM:
          case TYPE_TYPED_VEC:
          case TYPE_TYPED_MAP:
          case TYPE_PTR:
          case TYPE_FUTURE:
          case TYPE_BOX: {
            /* Ref-element buf (M4.4): write a tagged JaclVal to slot [idx].
             * Call gc_write_barrier so a concurrently-marking collector
             * sees both the old value (SATB) and the new reference. The
             * slot is a normal stack JaclVal -- the marker scans it. */
            JaclVal* slot = &vm->stack[frame->stack_base + base_slot
                                       + (uint32_t)idx];
            gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, *slot, val);
            *slot = val;
            break;
          }
          default:
            vm__set_error(vm, "buf-set: unsupported element type %u",
                          (unsigned)elem_type);
            return VM_RUNTIME_ERROR;
        }
        DISPATCH();
      }

      CASE(OP_BUF_ADDR_LOCAL): {
        /* Address of buf element: push &frame[base_slot] + byte_offset
         * as a tagged u64. Compile-time bounds + alignment guaranteed
         * the offset is in range. Used by [addr $buf->N]. See
         * BUFFER_DESIGN.md M3. */
        uint8_t  base_slot   = vm__read_byte(vm);
        uint16_t byte_offset = vm__read_u16(vm);
        uint8_t* base = (uint8_t*)&vm->stack[frame->stack_base + base_slot];
        uint64_t addr = (uint64_t)(uintptr_t)(base + byte_offset);
        result = vm__push(vm, jacl_u64(&vm->heap, addr));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_BUF_GET_STRUCT_LOCAL): {
        /* Pop i32 idx, bounds-check against buf_len, push N inline
         * struct slots onto TOS from frame[base_slot] + idx*total_size.
         * See BUFFER_DESIGN.md M4.1. */
        uint8_t  base_slot = vm__read_byte(vm);
        uint16_t type_idx  = vm__read_u16(vm);
        uint16_t buf_len   = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for buf-get",
                        (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        JaclVal idx_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        if (!jacl_is_i32(idx_val)) {
          vm__set_error(vm, "buf-get: index must be i32, got %s",
                        vm__type_name(idx_val));
          return VM_RUNTIME_ERROR;
        }
        int32_t idx = jacl_as_i32(idx_val);
        if (idx < 0 || (uint32_t)idx >= buf_len) {
          vm__set_error(vm, "buf-get: index %d out of bounds for [Buf %u %.*s]",
                        (int)idx, (unsigned)buf_len,
                        (int)sdef->name_len, sdef->name);
          return VM_RUNTIME_ERROR;
        }
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_operand_overflow(vm, "buf-get struct");
          return VM_STACK_OVERFLOW;
        }
        uint8_t* src = (uint8_t*)&vm->stack[frame->stack_base + base_slot]
                       + (uint32_t)idx * sdef->total_size;
        memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], src, sdef->total_size);
        vm__mark_struct_inline_slots(vm, vm->stack_top, sdef, width);
        vm->stack_top += width;
        DISPATCH();
      }

      CASE(OP_BUF_SET_STRUCT_LOCAL): {
        /* Pop N inline struct slots (TOS), pop i32 idx, bounds-check,
         * memcpy bytes into frame[base_slot] + idx*total_size. See
         * BUFFER_DESIGN.md M4.1. */
        uint8_t  base_slot = vm__read_byte(vm);
        uint16_t type_idx  = vm__read_u16(vm);
        uint16_t buf_len   = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for buf-set",
                        (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        if (vm->stack_top < width + 1) {
          vm__set_error(vm, "buf-set struct: stack underflow");
          return VM_RUNTIME_ERROR;
        }
        uint8_t* src = (uint8_t*)&vm->stack[vm->stack_top - width];
        /* Index sits below the struct bytes. */
        JaclVal idx_val = vm->stack[vm->stack_top - width - 1];
        if (!jacl_is_i32(idx_val)) {
          vm__set_error(vm, "buf-set: index must be i32, got %s",
                        vm__type_name(idx_val));
          return VM_RUNTIME_ERROR;
        }
        int32_t idx = jacl_as_i32(idx_val);
        if (idx < 0 || (uint32_t)idx >= buf_len) {
          vm__set_error(vm, "buf-set: index %d out of bounds for [Buf %u %.*s]",
                        (int)idx, (unsigned)buf_len,
                        (int)sdef->name_len, sdef->name);
          return VM_RUNTIME_ERROR;
        }
        uint8_t* dst = (uint8_t*)&vm->stack[frame->stack_base + base_slot]
                       + (uint32_t)idx * sdef->total_size;
        memcpy(dst, src, sdef->total_size);
        /* Clear inline bitmap for the consumed struct slots. */
        for (uint32_t si = 0; si < width; si++) {
          BITMAP_CLR(vm->inline_slot_bitmap, vm->stack_top - width + si);
        }
        vm->stack_top -= (width + 1); /* struct bytes + index */
        DISPATCH();
      }

      CASE(OP_BUF_UGET_LOCAL): {
        /* Unchecked indexed read: same as OP_BUF_GET_LOCAL minus the
         * bounds check. Out-of-range index is undefined behavior — the
         * load is whatever bytes sit at frame[base_slot + idx*sizeof(T)].
         * Caller is responsible for ensuring the index is in range.
         * See BUFFER_DESIGN.md M5. */
        uint8_t  base_slot = vm__read_byte(vm);
        uint8_t  elem_type = vm__read_byte(vm);
        JaclVal  idx_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        if (!jacl_is_i32(idx_val)) {
          vm__set_error(vm, "buf-unchecked-get: index must be i32, got %s",
                        vm__type_name(idx_val));
          return VM_RUNTIME_ERROR;
        }
        int32_t idx = jacl_as_i32(idx_val);
        uint8_t* base = (uint8_t*)&vm->stack[frame->stack_base + base_slot];
        JaclVal  loaded;
        switch ((JaclType)elem_type) {
          case TYPE_BOOL: loaded = jacl_bool(base[(uint32_t)idx] != 0); break;
          case TYPE_I8: {
            int8_t v; memcpy(&v, base + (uint32_t)idx, 1);
            loaded = jacl_i32((int32_t)v); break;
          }
          case TYPE_U8:  loaded = jacl_i32((int32_t)base[(uint32_t)idx]); break;
          case TYPE_I16: {
            int16_t v; memcpy(&v, base + (uint32_t)idx * 2, 2);
            loaded = jacl_i32((int32_t)v); break;
          }
          case TYPE_U16: {
            uint16_t v; memcpy(&v, base + (uint32_t)idx * 2, 2);
            loaded = jacl_i32((int32_t)v); break;
          }
          case TYPE_I32: {
            int32_t v; memcpy(&v, base + (uint32_t)idx * 4, 4);
            loaded = jacl_i32(v); break;
          }
          case TYPE_U32: {
            uint32_t v; memcpy(&v, base + (uint32_t)idx * 4, 4);
            loaded = jacl_u32(v); break;
          }
          case TYPE_F32: {
            float v; memcpy(&v, base + (uint32_t)idx * 4, 4);
            loaded = jacl_f32(v); break;
          }
          case TYPE_I64: {
            int64_t v; memcpy(&v, base + (uint32_t)idx * 8, 8);
            loaded = jacl_i64(&vm->heap, v); break;
          }
          case TYPE_U64: {
            uint64_t v; memcpy(&v, base + (uint32_t)idx * 8, 8);
            loaded = jacl_u64(&vm->heap, v); break;
          }
          case TYPE_F64: {
            double v; memcpy(&v, base + (uint32_t)idx * 8, 8);
            loaded = jacl_f64(&vm->heap, v); break;
          }
          case TYPE_DYN:
          case TYPE_STR:
          case TYPE_VEC:
          case TYPE_MAP:
          case TYPE_CLOSURE:
          case TYPE_STREAM:
          case TYPE_TYPED_VEC:
          case TYPE_TYPED_MAP:
          case TYPE_PTR:
          case TYPE_FUTURE:
          case TYPE_BOX: {
            /* Ref-element buf (M4.4): one JaclVal slot per index. */
            loaded = vm->stack[frame->stack_base + base_slot + (uint32_t)idx];
            break;
          }
          default:
            vm__set_error(vm, "buf-unchecked-get: unsupported element type %u",
                          (unsigned)elem_type);
            return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, loaded); if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_BUF_USET_LOCAL): {
        /* Unchecked indexed write: same as OP_BUF_SET_LOCAL minus the
         * bounds check. See BUFFER_DESIGN.md M5. */
        uint8_t  base_slot = vm__read_byte(vm);
        uint8_t  elem_type = vm__read_byte(vm);
        JaclVal  val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        JaclVal  idx_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        if (!jacl_is_i32(idx_val)) {
          vm__set_error(vm, "buf-unchecked-set: index must be i32, got %s",
                        vm__type_name(idx_val));
          return VM_RUNTIME_ERROR;
        }
        int32_t idx = jacl_as_i32(idx_val);
        uint8_t* base = (uint8_t*)&vm->stack[frame->stack_base + base_slot];
        switch ((JaclType)elem_type) {
          case TYPE_BOOL: {
            if (!jacl_is_bool(val)) {
              vm__set_error(vm, "buf-unchecked-set: expected bool, got %s",
                            vm__type_name(val));
              return VM_RUNTIME_ERROR;
            }
            base[(uint32_t)idx] = jacl_as_bool(val) ? 1 : 0;
            break;
          }
          case TYPE_I8: case TYPE_U8: case TYPE_I16: case TYPE_U16:
          case TYPE_I32: case TYPE_U32: {
            int32_t v;
            if (jacl_is_i32(val)) v = jacl_as_i32(val);
            else if (jacl_is_u32(val)) v = (int32_t)jacl_as_u32(val);
            else {
              vm__set_error(vm, "buf-unchecked-set: expected i32/u32, got %s",
                            vm__type_name(val));
              return VM_RUNTIME_ERROR;
            }
            switch ((JaclType)elem_type) {
              case TYPE_I8:  case TYPE_U8:  base[(uint32_t)idx] = (uint8_t)v; break;
              case TYPE_I16: case TYPE_U16: {
                uint16_t w = (uint16_t)v;
                memcpy(base + (uint32_t)idx * 2, &w, 2);
                break;
              }
              case TYPE_I32: case TYPE_U32: {
                memcpy(base + (uint32_t)idx * 4, &v, 4);
                break;
              }
              default: break;
            }
            break;
          }
          case TYPE_F32: {
            float v;
            if (jacl_is_f32(val)) v = jacl_as_f32(val);
            else if (jacl_is_f64(val)) v = (float)jacl_as_f64(val);
            else {
              vm__set_error(vm, "buf-unchecked-set: expected f32/f64, got %s",
                            vm__type_name(val));
              return VM_RUNTIME_ERROR;
            }
            memcpy(base + (uint32_t)idx * 4, &v, 4);
            break;
          }
          case TYPE_I64: {
            if (!jacl_is_i64(val) && !jacl_is_i32(val)) {
              vm__set_error(vm, "buf-unchecked-set: expected i64, got %s",
                            vm__type_name(val));
              return VM_RUNTIME_ERROR;
            }
            int64_t v = jacl_is_i64(val) ? jacl_as_i64(val)
                                          : (int64_t)jacl_as_i32(val);
            memcpy(base + (uint32_t)idx * 8, &v, 8);
            break;
          }
          case TYPE_U64: {
            if (!jacl_is_u64(val)) {
              vm__set_error(vm, "buf-unchecked-set: expected u64, got %s",
                            vm__type_name(val));
              return VM_RUNTIME_ERROR;
            }
            uint64_t v = jacl_as_u64(val);
            memcpy(base + (uint32_t)idx * 8, &v, 8);
            break;
          }
          case TYPE_F64: {
            if (!jacl_is_f64(val) && !jacl_is_f32(val)) {
              vm__set_error(vm, "buf-unchecked-set: expected f64, got %s",
                            vm__type_name(val));
              return VM_RUNTIME_ERROR;
            }
            double v = jacl_is_f64(val) ? jacl_as_f64(val)
                                         : (double)jacl_as_f32(val);
            memcpy(base + (uint32_t)idx * 8, &v, 8);
            break;
          }
          case TYPE_DYN:
          case TYPE_STR:
          case TYPE_VEC:
          case TYPE_MAP:
          case TYPE_CLOSURE:
          case TYPE_STREAM:
          case TYPE_TYPED_VEC:
          case TYPE_TYPED_MAP:
          case TYPE_PTR:
          case TYPE_FUTURE:
          case TYPE_BOX: {
            /* Ref-element buf (M4.4): tagged JaclVal store + write barrier. */
            JaclVal* slot = &vm->stack[frame->stack_base + base_slot
                                       + (uint32_t)idx];
            gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, *slot, val);
            *slot = val;
            break;
          }
          default:
            vm__set_error(vm, "buf-unchecked-set: unsupported element type %u",
                          (unsigned)elem_type);
            return VM_RUNTIME_ERROR;
        }
        DISPATCH();
      }

      CASE(OP_BUF_STORE_OFF): {
        /* Descriptor-driven leaf store for buf literal init. Addresses by
         * byte offset within frame[base_slot]. leaf_enc is the recursive
         * layout encoding (scalar sentinel or struct registry idx).
         * Unchecked: the init walker proves the offset in range at compile
         * time. See RECURSIVE_LAYOUT_REFACTOR.md Step 3. */
        uint8_t  base_slot = vm__read_byte(vm);
        uint16_t byte_off  = vm__read_u16(vm);
        uint16_t leaf_enc  = vm__read_u16(vm);
        uint8_t* base = (uint8_t*)&vm->stack[frame->stack_base + base_slot];
        if (JACL_IS_SCALAR_TYPE_IDX(leaf_enc)) {
          JaclType elem_type = JACL_TYPE_IDX_TO_SCALAR(leaf_enc);
          JaclVal val;
          result = vm__pop(vm, &val); if (result != VM_OK) return result;
          uint8_t* dst = base + byte_off;
          switch (elem_type) {
            case TYPE_BOOL:
              if (!jacl_is_bool(val)) {
                vm__set_error(vm, "buf-store: expected bool, got %s",
                              vm__type_name(val));
                return VM_RUNTIME_ERROR;
              }
              dst[0] = jacl_as_bool(val) ? 1 : 0;
              break;
            case TYPE_I8: case TYPE_U8: case TYPE_I16: case TYPE_U16:
            case TYPE_I32: case TYPE_U32: {
              int32_t v;
              if (jacl_is_i32(val)) v = jacl_as_i32(val);
              else if (jacl_is_u32(val)) v = (int32_t)jacl_as_u32(val);
              else {
                vm__set_error(vm, "buf-store: expected i32/u32, got %s",
                              vm__type_name(val));
                return VM_RUNTIME_ERROR;
              }
              switch (elem_type) {
                case TYPE_I8: case TYPE_U8: dst[0] = (uint8_t)v; break;
                case TYPE_I16: case TYPE_U16: {
                  uint16_t w = (uint16_t)v; memcpy(dst, &w, 2); break;
                }
                default: memcpy(dst, &v, 4); break;
              }
              break;
            }
            case TYPE_F32: {
              float v;
              if (jacl_is_f32(val)) v = jacl_as_f32(val);
              else if (jacl_is_f64(val)) v = (float)jacl_as_f64(val);
              else {
                vm__set_error(vm, "buf-store: expected f32/f64, got %s",
                              vm__type_name(val));
                return VM_RUNTIME_ERROR;
              }
              memcpy(dst, &v, 4);
              break;
            }
            case TYPE_I64: {
              if (!jacl_is_i64(val) && !jacl_is_i32(val)) {
                vm__set_error(vm, "buf-store: expected i64, got %s",
                              vm__type_name(val));
                return VM_RUNTIME_ERROR;
              }
              int64_t v = jacl_is_i64(val) ? jacl_as_i64(val)
                                            : (int64_t)jacl_as_i32(val);
              memcpy(dst, &v, 8);
              break;
            }
            case TYPE_U64: {
              if (!jacl_is_u64(val)) {
                vm__set_error(vm, "buf-store: expected u64, got %s",
                              vm__type_name(val));
                return VM_RUNTIME_ERROR;
              }
              uint64_t v = jacl_as_u64(val);
              memcpy(dst, &v, 8);
              break;
            }
            case TYPE_F64: {
              if (!jacl_is_f64(val) && !jacl_is_f32(val)) {
                vm__set_error(vm, "buf-store: expected f64, got %s",
                              vm__type_name(val));
                return VM_RUNTIME_ERROR;
              }
              double v = jacl_is_f64(val) ? jacl_as_f64(val)
                                           : (double)jacl_as_f32(val);
              memcpy(dst, &v, 8);
              break;
            }
            case TYPE_DYN: case TYPE_STR: case TYPE_VEC: case TYPE_MAP:
            case TYPE_CLOSURE: case TYPE_STREAM: case TYPE_TYPED_VEC:
            case TYPE_TYPED_MAP: case TYPE_PTR: case TYPE_FUTURE:
            case TYPE_BOX: {
              /* Ref leaf: byte_off is 8-aligned by construction, so dst is
               * a real frame slot. Tagged JaclVal store + write barrier. */
              JaclVal* slot = (JaclVal*)dst;
              gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, *slot, val);
              *slot = val;
              break;
            }
            default:
              vm__set_error(vm, "buf-store: unsupported leaf type %u",
                            (unsigned)elem_type);
              return VM_RUNTIME_ERROR;
          }
        } else {
          /* Struct leaf: ctor left total_size bytes (width slots) on TOS. */
          if (!vm->struct_registry || leaf_enc >= vm->struct_registry->count) {
            vm__set_error(vm, "buf-store: invalid struct type index %u",
                          (unsigned)leaf_enc);
            return VM_RUNTIME_ERROR;
          }
          StructTypeDef* sdef = vm->struct_registry->defs[leaf_enc];
          uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1)
                           / sizeof(JaclVal);
          if (vm->stack_top < width) {
            vm__set_error(vm, "buf-store struct: stack underflow");
            return VM_RUNTIME_ERROR;
          }
          uint8_t* src = (uint8_t*)&vm->stack[vm->stack_top - width];
          memcpy(base + byte_off, src, sdef->total_size);
          for (uint32_t si = 0; si < width; si++) {
            BITMAP_CLR(vm->inline_slot_bitmap, vm->stack_top - width + si);
          }
          vm->stack_top -= width;
        }
        DISPATCH();
      }

      CASE(OP_GET_UPVALUE): {
        uint8_t index = vm__read_byte(vm);
        result = vm__push(vm, frame->closure->upvalues[index]);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_JUMP): {
        uint16_t offset = vm__read_u16(vm);
        vm->ip += offset;
        DISPATCH();
      }

      CASE(OP_JUMP_IF_FALSE): {
        uint16_t offset = vm__read_u16(vm);
        JaclVal condition;
        result = vm__pop(vm, &condition);
        if (result != VM_OK) return result;
        if (vm__is_falsy(condition)) {
          vm->ip += offset;
        }
        DISPATCH();
      }

      CASE(OP_LOOP): {
        uint16_t offset = vm__read_u16(vm);
        vm->ip -= offset;
        DISPATCH();
      }

      CASE(OP_CALL): {
        uint8_t arg_count = vm__read_byte(vm);
        JaclVal callee = vm->stack[vm->stack_top - arg_count - 1];

        if (jacl_is_native_fn(callee)) {
          uint32_t fn_idx = jacl_as_native_fn_index(callee);
          if (fn_idx >= vm->native_fn_count || !vm->call_native) {
            vm__set_error(vm, "invalid native function index %u", fn_idx);
            return VM_RUNTIME_ERROR;
          }
          int8_t arity = vm->native_fn_arities[fn_idx];
          if (arity >= 0 && arg_count != (uint8_t)arity) {
            vm__set_error(vm, "expected %d arguments but got %d",
                         (int)arity, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }
          JaclVal* args = &vm->stack[vm->stack_top - arg_count];
          JaclVal ret = vm->call_native(vm->native_fn_ctx, fn_idx,
                                         args, (int)arg_count);
          vm->stack_top -= (arg_count + 1); /* pop args + callee */
          result = vm__push(vm, ret);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        if (!jacl_is_closure(callee)) {
          vm__set_error(vm, "cannot call %s value", vm__type_name(callee));
          return VM_RUNTIME_ERROR;
        }

        JaclClosure* closure = jacl_as_closure(callee);

        /* Generator: calling a generator proc creates a stream instead of executing.
           Check this BEFORE arity check because generator has hidden __k/SM params. */
        if (closure->is_generator) {
          /* Arity check against min_args (excludes hidden __k/SM params) */
          if (arg_count != closure->min_args) {
            vm__set_error(vm, "expected %d arguments but got %d",
                         (int)closure->min_args, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }
          JaclVal stream_val = jacl_stream(&vm->heap);
          JaclStream* stream = jacl_as_stream(stream_val);
          stream->elem_idx = closure->gen_elem_idx;  /* strict-stream element type */
          vm__slot_set(vm, &stream->next_fn, callee);

          if (closure->is_sm_compiled) {
            /* State machine generator: allocate SM object, copy args into fields */
            JaclVal sm_val = gc_alloc_state_machine(&vm->heap, closure->sm_field_count);
            JaclStateMachine* sm = jacl_as_state_machine(sm_val);
            vm__slot_set(vm, &sm->sm_closure, callee);
            for (uint8_t i = 0; i < arg_count && i < closure->sm_field_count; i++) {
              vm__slot_set(vm, &sm->fields[i], vm->stack[vm->stack_top - arg_count + i]);
            }
            vm__slot_set(vm, &stream->state_machine, sm_val);
            stream->arg_count = 0;
          } else {
            /* CPS generator: save args for deferred first-call */
            stream->arg_count = (uint8_t)(arg_count <= STREAM_MAX_ARGS ? arg_count : STREAM_MAX_ARGS);
            for (uint8_t i = 0; i < stream->arg_count; i++) {
              stream->args[i] = vm->stack[vm->stack_top - arg_count + i];
            }
          }

          vm->stack_top -= (arg_count + 1); /* pop args + callee */
          result = vm__push(vm, stream_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        /* SM-compiled non-generator: transparently wrap user args into SM */
        if (closure->is_sm_compiled && !closure->is_generator) {
          /* Arity check against min_args (user's parameter count) */
          if (arg_count != closure->min_args) {
            vm__set_error(vm, "expected %d arguments but got %d",
                         (int)closure->min_args, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }
          /* Allocate state machine and copy user args into fields */
          JaclVal sm_val = gc_alloc_state_machine(&vm->heap, closure->sm_field_count);
          JaclStateMachine* sm = jacl_as_state_machine(sm_val);
          vm__slot_set(vm, &sm->sm_closure, callee);
          for (uint8_t i = 0; i < arg_count && i < closure->sm_field_count; i++) {
            vm__slot_set(vm, &sm->fields[i], vm->stack[vm->stack_top - arg_count + i]);
          }
          /* Propagate error_k from caller SM to inner SM so that the
             completion callback (resolve_k, parallel_k, race_k) is
             preserved across nested SM-to-SM calls.  This ensures
             the future/aggregator is resolved when the innermost SM
             in the call chain completes. */
          if (frame->closure->is_sm_compiled) {
            JaclVal outer_sm_val = vm->stack[frame->stack_base + 0];
            if (jacl_is_state_machine(outer_sm_val)) {
              JaclStateMachine *outer_sm = jacl_as_state_machine(outer_sm_val);
              vm__slot_set(vm, &sm->error_k, outer_sm->error_k);
            }
          }
          /* Replace stack args with (sm_val, JACL_NIL) */
          uint32_t callee_pos = vm->stack_top - arg_count - 1;
          vm->stack[callee_pos + 1] = sm_val;
          vm->stack[callee_pos + 2] = JACL_NIL;
          vm->stack_top = callee_pos + 3;
          arg_count = 2;
        }

        if (closure->variadic) {
          if (arg_count < closure->min_args) {
            vm__set_error(vm, "expected at least %d arguments but got %d",
                         (int)closure->min_args, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }
        } else if (arg_count != closure->param_total_slots) {
          /* Phase 5a: arity check uses param_total_slots (includes multi-slot struct params) */
          vm__set_error(vm, "expected %d arguments but got %d",
                       (int)closure->param_count, (int)arg_count);
          return VM_RUNTIME_ERROR;
        }

        if (vm->frame_count >= VM_FRAMES_MAX) {
          vm__set_frame_overflow(vm);
          return VM_RUNTIME_ERROR;
        }

        CallFrame* new_frame = &vm->frames[vm->frame_count++];
        new_frame->closure    = closure;
        new_frame->return_ip  = vm->ip;
        new_frame->stack_base = vm->stack_top - arg_count;
        new_frame->chunk      = &closure->chunk;

        frame     = new_frame;
        vm->ip    = frame->chunk->code;
        vm->chunk = frame->chunk;
        DISPATCH();
      }

      CASE(OP_TAIL_CALL): {
        uint8_t arg_count = vm__read_byte(vm);
        JaclVal callee = vm->stack[vm->stack_top - arg_count - 1];

        if (jacl_is_native_fn(callee)) {
          /* Native functions can't be tail-called; degrade to regular call */
          uint32_t fn_idx = jacl_as_native_fn_index(callee);
          if (fn_idx >= vm->native_fn_count || !vm->call_native) {
            vm__set_error(vm, "invalid native function index %u", fn_idx);
            return VM_RUNTIME_ERROR;
          }
          int8_t arity = vm->native_fn_arities[fn_idx];
          if (arity >= 0 && arg_count != (uint8_t)arity) {
            vm__set_error(vm, "expected %d arguments but got %d",
                         (int)arity, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }
          JaclVal* args = &vm->stack[vm->stack_top - arg_count];
          JaclVal ret = vm->call_native(vm->native_fn_ctx, fn_idx,
                                         args, (int)arg_count);
          vm->stack_top -= (arg_count + 1);
          result = vm__push(vm, ret);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        if (!jacl_is_closure(callee)) {
          vm__set_error(vm, "cannot call %s value", vm__type_name(callee));
          return VM_RUNTIME_ERROR;
        }

        JaclClosure* closure = jacl_as_closure(callee);

        /* SM-compiled non-generator: transparently wrap user args into SM */
        if (closure->is_sm_compiled && !closure->is_generator) {
          if (arg_count != closure->min_args) {
            vm__set_error(vm, "expected %d arguments but got %d",
                         (int)closure->min_args, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }
          JaclVal sm_val = gc_alloc_state_machine(&vm->heap, closure->sm_field_count);
          JaclStateMachine* sm = jacl_as_state_machine(sm_val);
          vm__slot_set(vm, &sm->sm_closure, callee);
          for (uint8_t i = 0; i < arg_count && i < closure->sm_field_count; i++) {
            vm__slot_set(vm, &sm->fields[i], vm->stack[vm->stack_top - arg_count + i]);
          }
          uint32_t callee_pos = vm->stack_top - arg_count - 1;
          vm->stack[callee_pos + 1] = sm_val;
          vm->stack[callee_pos + 2] = JACL_NIL;
          vm->stack_top = callee_pos + 3;
          arg_count = 2;
        }

        if (closure->variadic) {
          if (arg_count < closure->min_args) {
            vm__set_error(vm, "expected at least %d arguments but got %d",
                         (int)closure->min_args, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }
        } else if (arg_count != closure->param_total_slots) {
          /* Phase 5a: arity check uses param_total_slots */
          vm__set_error(vm, "expected %d arguments but got %d",
                       (int)closure->param_count, (int)arg_count);
          return VM_RUNTIME_ERROR;
        }

        /* Slide callee + args down to reuse current frame's stack region.
           stack_base - 1 is where the caller's closure sits. */
        uint32_t src = vm->stack_top - arg_count - 1; /* callee position */
        uint32_t dst = frame->stack_base - 1;         /* overwrite current frame's closure */
        /* US-014: clear stale bitmap bits for the old frame's entire range
           before overwriting with new args (which are regular JaclVals). */
        for (uint32_t si = dst; si < vm->stack_top; si++) {
          BITMAP_CLR(vm->inline_slot_bitmap, si);
        }
        for (uint32_t i = 0; i <= (uint32_t)arg_count; i++) {
          vm->stack[dst + i] = vm->stack[src + i];
        }
        vm->stack_top = dst + 1 + arg_count;

        /* Reuse current frame — no frame_count change */
        frame->closure    = closure;
        frame->stack_base = dst + 1;
        frame->chunk      = &closure->chunk;

        vm->ip    = frame->chunk->code;
        vm->chunk = frame->chunk;
        DISPATCH();
      }

      CASE(OP_RETURN): {
        JaclVal return_value;
        result = vm__pop(vm, &return_value);
        if (result != VM_OK) return result;

        uint32_t callee_base = frame->stack_base;
        uint8_t* caller_ip   = frame->return_ip;

        /* US-014: clear inline struct bitmap bits for the callee's entire frame range */
        for (uint32_t si = callee_base; si < vm->stack_top + 1; si++) {
          BITMAP_CLR(vm->inline_slot_bitmap, si);
        }

        vm->frame_count--;

        if (vm->frame_count == 0) {
          /* Returning from top-level */
          vm->stack[0] = return_value;
          vm->stack_top = 1;
          return VM_OK;
        }

        /* Place return value where the callee's closure was */
        vm->stack[callee_base - 1] = return_value;
        vm->stack_top = callee_base;

        frame     = &vm->frames[vm->frame_count - 1];
        vm->ip    = caller_ip;
        vm->chunk = frame->chunk;

        if (vm->frame_count <= min_frame) {
          return VM_OK;
        }
        DISPATCH();
      }

      CASE(OP_RETURN_WIDE): {
        uint8_t width = vm__read_byte(vm);

        /* Read return slots from callee stack top */
        uint32_t ret_base = vm->stack_top - width;

        uint32_t callee_base = frame->stack_base;
        uint8_t* caller_ip   = frame->return_ip;

        /* Clear inline struct bitmap bits for the callee's entire frame range */
        for (uint32_t si = callee_base; si < vm->stack_top; si++) {
          BITMAP_CLR(vm->inline_slot_bitmap, si);
        }

        vm->frame_count--;

        if (vm->frame_count == 0) {
          memmove(&vm->stack[0], &vm->stack[ret_base], width * sizeof(JaclVal));
          for (uint32_t si = 0; si < width; si++)
            BITMAP_SET(vm->inline_slot_bitmap, si);
          vm->stack_top = width;
          return VM_OK;
        }

        /* Copy return slots to where the callee's closure was */
        uint32_t dst = callee_base - 1;
        memmove(&vm->stack[dst], &vm->stack[ret_base], width * sizeof(JaclVal));
        for (uint32_t si = dst; si < dst + width; si++)
          BITMAP_SET(vm->inline_slot_bitmap, si);
        vm->stack_top = dst + width;

        frame     = &vm->frames[vm->frame_count - 1];
        vm->ip    = caller_ip;
        vm->chunk = frame->chunk;

        if (vm->frame_count <= min_frame) {
          return VM_OK;
        }
        DISPATCH();
      }

      CASE(OP_CLOSURE): {
        uint16_t index = vm__read_u16(vm);
        JaclClosure* template = jacl_as_closure(vm->chunk->constants[index]);

        /* US-008: allocate using upvalue_total_slots to support wide struct upvalues */
        uint16_t total_slots = template->upvalue_total_slots;
        if (total_slots == 0) total_slots = template->upvalue_count; /* fallback for pre-US008 */
        size_t uv_bytes = sizeof(JaclVal) * total_slots;
        JaclClosure* cl = (JaclClosure*)gc_alloc(&vm->heap, OBJ_CLOSURE,
                              sizeof(JaclClosure) + uv_bytes);
        cl->chunk        = template->chunk;
        cl->param_count  = template->param_count;
        cl->param_total_slots = template->param_total_slots;
        cl->has_inline_params = template->has_inline_params;
        cl->param_names  = template->param_names;
        cl->name         = template->name;
        cl->upvalue_count = template->upvalue_count;
        cl->upvalue_total_slots = template->upvalue_total_slots;
        cl->min_args     = template->min_args;
        cl->variadic     = template->variadic;
        cl->pinned       = template->pinned;
        cl->is_generator  = template->is_generator;
        cl->is_sm_compiled = template->is_sm_compiled;
        cl->gen_elem_idx  = template->gen_elem_idx;  /* strict-stream elem type */
        cl->sm_field_count = template->sm_field_count;
        /* US-014: initialize upvalue inline bitmap */
        memset(cl->upvalue_inline_bitmap, 0, sizeof(cl->upvalue_inline_bitmap));
        /* All pinned closures run on thread 0 (the main worker thread).
           This ensures all non-local mutable state reads and writes go
           through a single worker, avoiding per-worker env isolation issues. */
        cl->pin_worker_id = template->pinned ? 0 : -1;

        /* Root cl on the VM stack before the upvalue loop.  If a future
           upvalue path calls gc_alloc, emergency GC could sweep an
           unrooted closure.  Rooting early is defensive. */
        result = vm__push(vm, jacl_closure(cl));
        if (result != VM_OK) return result;

        if (cl->upvalue_count > 0) {
          cl->upvalues = (JaclVal*)(cl + 1); /* trailing array */
          /* US-008: zero-fill the upvalue array for wide struct upvalues */
          memset(cl->upvalues, 0, uv_bytes);
          uint16_t uv_slot = 0; /* current write position in upvalue array */
          for (uint8_t i = 0; i < cl->upvalue_count; i++) {
            uint8_t is_local = vm__read_byte(vm);
            uint8_t uv_index = vm__read_byte(vm);
            uint8_t width    = vm__read_byte(vm);
            if (width == 0) width = 1; /* safety fallback */
            if (is_local == 1) {
              /* US-008: copy width slots from enclosing local stack */
              for (uint8_t w = 0; w < width; w++) {
                uint32_t src = frame->stack_base + uv_index + w;
                if (src >= vm->stack_top - 1) {
                  vm__set_error(vm,
                    "OP_CLOSURE: local upvalue index %d+%d out of bounds "
                    "(frame stack_base=%u, stack_top=%u)",
                    uv_index, w, frame->stack_base, vm->stack_top);
                  return VM_RUNTIME_ERROR;
                }
                JaclVal v = vm->stack[src];
                /* AUDIT §10/§11: closure is fresh (watermark-protected,
                 * not in any mark path). Insertion barrier on each heap
                 * upvalue value. Skip inline-struct raw-byte slots
                 * (width > 1 with bitmap set). */
                bool is_raw = (width > 1) &&
                              BITMAP_GET(vm->inline_slot_bitmap, src);
                if (!is_raw) {
                  gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                                   JACL_NIL, v);
                }
                cl->upvalues[uv_slot + w] = v;
              }
              /* US-014: propagate bitmap from stack to closure upvalues */
              if (width > 1) {
                for (uint8_t w = 0; w < width; w++) {
                  uint32_t src = frame->stack_base + uv_index + w;
                  if (BITMAP_GET(vm->inline_slot_bitmap, src)) {
                    BITMAP_SET(cl->upvalue_inline_bitmap, uv_slot + w);
                  }
                }
              }
            } else if (is_local == 2) {
              /* SM state field upvalue: read from the state machine object
                 at slot 0 of the current frame. uv_index is the field index.
                 For mutable fields the SM field already holds a cell pointer,
                 so the closure gets shared identity (FR-5 semantics). */
              JaclVal sm_val = vm->stack[frame->stack_base + 0];
              JaclStateMachine* sm = jacl_as_state_machine(sm_val);
              if (uv_index >= sm->field_count) {
                vm__set_error(vm,
                  "OP_CLOSURE: SM field upvalue index %d out of bounds "
                  "(SM has %d fields)",
                  uv_index, sm->field_count);
                return VM_RUNTIME_ERROR;
              }
              JaclVal v = sm->fields[uv_index];
              /* AUDIT §10/§11: barrier on heap insertion into fresh closure.
               * Skip inline-struct slots (raw bytes per SM bitmap). */
              if (!BITMAP_GET(sm->field_inline_bitmap, uv_index)) {
                gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, JACL_NIL, v);
              }
              cl->upvalues[uv_slot] = v;
              /* US-014: SM field upvalue copies 1 slot only — propagate
                 bitmap from SM, not from stack width */
              if (BITMAP_GET(sm->field_inline_bitmap, uv_index)) {
                BITMAP_SET(cl->upvalue_inline_bitmap, uv_slot);
              }
            } else {
              /* US-008: copy width slots from parent closure upvalues */
              for (uint8_t w = 0; w < width; w++) {
                uint16_t parent_total = frame->closure->upvalue_total_slots
                  ? frame->closure->upvalue_total_slots
                  : frame->closure->upvalue_count;
              if (uv_index + w >= parent_total) {
                  vm__set_error(vm,
                    "OP_CLOSURE: upvalue index %d+%d out of bounds "
                    "(parent has %d upvalue slots)",
                    uv_index, w, frame->closure->upvalue_total_slots);
                  return VM_RUNTIME_ERROR;
                }
                JaclVal v = frame->closure->upvalues[uv_index + w];
                /* AUDIT §10/§11: barrier on heap insertion into fresh
                 * closure. Skip inline-struct slots (width > 1 with
                 * parent bitmap set). */
                bool is_raw = (width > 1) &&
                  BITMAP_GET(frame->closure->upvalue_inline_bitmap,
                             uv_index + w);
                if (!is_raw) {
                  gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                                   JACL_NIL, v);
                }
                cl->upvalues[uv_slot + w] = v;
              }
              /* US-014: propagate bitmap from parent closure upvalues */
              if (width > 1) {
                for (uint8_t w = 0; w < width; w++) {
                  if (BITMAP_GET(frame->closure->upvalue_inline_bitmap, uv_index + w)) {
                    BITMAP_SET(cl->upvalue_inline_bitmap, uv_slot + w);
                  }
                }
              }
            }
            uv_slot += width;
          }
        } else {
          cl->upvalues = NULL;
        }

        /* cl is already on the stack from the root push above */
        DISPATCH();
      }

      CASE(OP_POP_N): {
        uint8_t count = vm__read_byte(vm);
        if (vm->stack_top < count) {
          vm__set_error(vm, "stack underflow");
          return VM_RUNTIME_ERROR;
        }
        /* US-014: clear inline struct bitmap bits for popped slots */
        for (uint32_t si = vm->stack_top - count; si < vm->stack_top; si++) {
          BITMAP_CLR(vm->inline_slot_bitmap, si);
        }
        vm->stack_top -= count;
        DISPATCH();
      }

      CASE(OP_CLOSE_LOOP): {
        /* Pop N values under the top-of-stack value.
           Used by break inside for-loops to clean up hidden locals
           while preserving the break value on top. */
        uint8_t count = vm__read_byte(vm);
        if (count > 0) {
          JaclVal top = vm->stack[vm->stack_top - 1];
          /* US-014: save top's bitmap state, clear removed range, restore */
          uint8_t top_is_inline = BITMAP_GET(vm->inline_slot_bitmap, vm->stack_top - 1);
          for (uint32_t si = vm->stack_top - 1 - count; si < vm->stack_top; si++) {
            BITMAP_CLR(vm->inline_slot_bitmap, si);
          }
          vm->stack_top -= count;
          vm->stack[vm->stack_top - 1] = top;
          if (top_is_inline) {
            BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top - 1);
          }
        }
        DISPATCH();
      }

      CASE(OP_DESTRUCTURE_VEC): {
        uint32_t saved_stack_top = vm->stack_top;
        uint8_t n = vm__read_byte(vm);
        uint8_t skip_mask = vm__read_byte(vm);
        JaclVal vec_val;
        result = vm__pop(vm, &vec_val);
        if (result != VM_OK) return result;
        if (!jacl_is_vector(vec_val)) {
          VM_ERROR(vm,
              "destructuring requires a vector, got %s",
              vm__type_name(vec_val));
        }
        jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(vec_val);
        uint32_t vec_len = jacl_vec_count(vec);
        if (vec_len != n) {
          VM_ERROR(vm,
              "destructuring length mismatch: expected %u elements, got %u",
              (unsigned)n, (unsigned)vec_len);
        }
        /* Push elements 0..N-1 onto stack, skipping positions in skip_mask */
        for (uint8_t i = 0; i < n; i++) {
          if (skip_mask & (1u << i)) continue; /* wildcard position */
          jacl_vec_get_result gr = jacl_vec_get(vec, (uint32_t)i);
          result = vm__push(vm, gr.found ? gr.value : JACL_NIL);
          if (result != VM_OK) return result;
        }
        DISPATCH();
      }

      CASE(OP_DESTRUCTURE_VEC_REST): {
        uint32_t saved_stack_top = vm->stack_top;
        uint8_t n = vm__read_byte(vm);
        JaclVal vec_val;
        result = vm__pop(vm, &vec_val);
        if (result != VM_OK) return result;
        if (!jacl_is_vector(vec_val)) {
          VM_ERROR(vm,
              "destructuring requires a vector, got %s",
              vm__type_name(vec_val));
        }
        jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(vec_val);
        uint32_t vec_len = jacl_vec_count(vec);
        if (vec_len < n) {
          VM_ERROR(vm,
              "destructuring rest: expected at least %u elements, got %u",
              (unsigned)n, (unsigned)vec_len);
        }
        /* Push first N elements individually */
        for (uint8_t i = 0; i < n; i++) {
          jacl_vec_get_result gr = jacl_vec_get(vec, (uint32_t)i);
          result = vm__push(vm, gr.found ? gr.value : JACL_NIL);
          if (result != VM_OK) return result;
        }
        /* Collect remaining elements into a new vector */
        gc__current_heap = &vm->heap;
        jacl_vec_root* rest = jacl_vec_empty();
        for (uint32_t i = n; i < vec_len; i++) {
          jacl_vec_get_result gr = jacl_vec_get(vec, i);
          rest = jacl_vec_push_back(rest, gr.found ? gr.value : JACL_NIL);
        }
        result = vm__push(vm, jacl_vector_ptr(rest));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_DESTRUCTURE_NAMED): {
        uint32_t saved_stack_top = vm->stack_top;
        uint8_t n = vm__read_byte(vm);
        /* Read N constant indices for field names */
        uint16_t name_indices[256];
        for (uint8_t i = 0; i < n; i++) {
          name_indices[i] = vm__read_u16(vm);
        }
        JaclVal src_val;
        result = vm__pop(vm, &src_val);
        if (result != VM_OK) return result;

        if (jacl_is_struct(src_val)) {
          HeapRecord* s = jacl_as_heap_record_ptr(src_val);
          StructTypeDef* sdef = vm->struct_registry->defs[s->type_idx];
          for (uint8_t i = 0; i < n; i++) {
            JaclVal name_val = frame->chunk->constants[name_indices[i]];
            char fname[64]; uint32_t flen;
            flen = jacl_string_data(name_val, fname, sizeof(fname));
            /* Find field by name */
            uint32_t fi;
            for (fi = 0; fi < sdef->field_count; fi++) {
              if (sdef->fields[fi].name_len == flen &&
                  memcmp(sdef->fields[fi].name, fname, flen) == 0) break;
            }
            if (fi == sdef->field_count) {
              VM_ERROR(vm,
                  "destructuring: struct '%.*s' has no field '%.*s'",
                  (int)sdef->name_len, sdef->name, (int)flen, fname);
            }
            JaclVal field_val = vm__heap_record_read_field(&vm->heap, s,
                sdef->fields[fi].offset, sdef->fields[fi].type);
            result = vm__push(vm, field_val);
            if (result != VM_OK) return result;
          }
        } else if (jacl_is_map(src_val)) {
          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(src_val);
          for (uint8_t i = 0; i < n; i++) {
            JaclVal key_val = frame->chunk->constants[name_indices[i]];
            if (!jacl_map_has(map, key_val)) {
              char fname[64]; uint32_t flen;
              flen = jacl_string_data(key_val, fname, sizeof(fname));
              VM_ERROR(vm,
                  "destructuring: map has no key '%.*s'",
                  (int)flen, fname);
            }
            result = vm__push(vm, jacl_map_get(map, key_val));
            if (result != VM_OK) return result;
          }
        } else {
          VM_ERROR(vm,
              "named destructuring requires a struct or map, got %s",
              vm__type_name(src_val));
        }
        DISPATCH();
      }

      CASE(OP_DESTRUCTURE_NAMED_REST): {
        uint32_t saved_stack_top = vm->stack_top;
        uint8_t n = vm__read_byte(vm);
        /* Read N constant indices for explicit field names to extract */
        uint16_t name_indices[256];
        for (uint8_t i = 0; i < n; i++) {
          name_indices[i] = vm__read_u16(vm);
        }
        JaclVal src_val;
        result = vm__pop(vm, &src_val);
        if (result != VM_OK) return result;

        if (jacl_is_struct(src_val)) {
          HeapRecord* s = jacl_as_heap_record_ptr(src_val);
          StructTypeDef* sdef = vm->struct_registry->defs[s->type_idx];

          /* Push N explicit field values */
          for (uint8_t i = 0; i < n; i++) {
            JaclVal fname_val = frame->chunk->constants[name_indices[i]];
            char fname[64]; uint32_t flen;
            flen = jacl_string_data(fname_val, fname, sizeof(fname));
            uint32_t fi;
            for (fi = 0; fi < sdef->field_count; fi++) {
              if (sdef->fields[fi].name_len == flen &&
                  memcmp(sdef->fields[fi].name, fname, flen) == 0) break;
            }
            if (fi == sdef->field_count) {
              VM_ERROR(vm,
                  "destructuring: struct '%.*s' has no field '%.*s'",
                  (int)sdef->name_len, sdef->name, (int)flen, fname);
            }
            JaclVal field_val = vm__heap_record_read_field(&vm->heap, s,
                sdef->fields[fi].offset, sdef->fields[fi].type);
            result = vm__push(vm, field_val);
            if (result != VM_OK) return result;
          }

          /* Build rest map from remaining fields */
          gc__current_heap = &vm->heap;
          jacl_map_node* rest_map = NULL;
          for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
            /* Check if this field is in the explicit list */
            int is_explicit = 0;
            for (uint8_t ei = 0; ei < n; ei++) {
              JaclVal ename_val = frame->chunk->constants[name_indices[ei]];
              char ename[64]; uint32_t elen;
              elen = jacl_string_data(ename_val, ename, sizeof(ename));
              if (sdef->fields[fi].name_len == elen &&
                  memcmp(sdef->fields[fi].name, ename, elen) == 0) {
                is_explicit = 1; break;
              }
            }
            if (!is_explicit) {
              JaclVal key = jacl_inline_string(sdef->fields[fi].name,
                                               sdef->fields[fi].name_len);
              JaclVal val = vm__heap_record_read_field(&vm->heap, s,
                  sdef->fields[fi].offset, sdef->fields[fi].type);
              rest_map = jacl_map_set(rest_map, key, val);
            }
          }
          result = vm__push(vm, jacl_map_ptr(rest_map));
          if (result != VM_OK) return result;

        } else if (jacl_is_map(src_val)) {
          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(src_val);

          /* Push N explicit field values */
          for (uint8_t i = 0; i < n; i++) {
            JaclVal key_val = frame->chunk->constants[name_indices[i]];
            if (!jacl_map_has(map, key_val)) {
              char fname[64]; uint32_t flen;
              flen = jacl_string_data(key_val, fname, sizeof(fname));
              VM_ERROR(vm,
                  "destructuring: map has no key '%.*s'",
                  (int)flen, fname);
            }
            result = vm__push(vm, jacl_map_get(map, key_val));
            if (result != VM_OK) return result;
          }

          /* Build rest map from remaining entries */
          gc__current_heap = &vm->heap;
          jacl_map_node* rest_map = NULL;
          jacl_map_iter it = jacl_map_iter_init(map);
          jacl_map_iter_result ir;
          do {
            ir = jacl_map_next_leaf(&it);
            if (!ir.item) break;
            JaclVal key = jacl_map_key_from_leaf(ir.item);
            /* Check if this key is in the explicit list */
            int is_explicit = 0;
            char kbuf[64]; uint32_t klen;
            klen = jacl_string_data(key, kbuf, sizeof(kbuf));
            for (uint8_t ei = 0; ei < n; ei++) {
              JaclVal ename_val = frame->chunk->constants[name_indices[ei]];
              char ebuf[64]; uint32_t elen;
              elen = jacl_string_data(ename_val, ebuf, sizeof(ebuf));
              if (klen == elen && memcmp(kbuf, ebuf, klen) == 0) {
                is_explicit = 1; break;
              }
            }
            if (!is_explicit) {
              JaclVal val = jacl_map_value_from_leaf(ir.item);
              rest_map = jacl_map_set(rest_map, key, val);
            }
          } while (ir.item);
          result = vm__push(vm, jacl_map_ptr(rest_map));
          if (result != VM_OK) return result;
        } else {
          VM_ERROR(vm,
              "named destructuring requires a struct or map, got %s",
              vm__type_name(src_val));
        }
        DISPATCH();
      }

      CASE(OP_CONCAT): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal b, a;
        result = vm__pop(vm, &b); if (result != VM_OK) return result;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        if (jacl_is_error(a)) { result = vm__push(vm, a); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(b)) { result = vm__push(vm, b); if (result != VM_OK) return result; DISPATCH(); }

        if (!jacl_is_string(a) || !jacl_is_string(b)) {
          VM_ERROR(vm,
            "type error in 'concat': expected strings, got %s and %s",
            vm__type_name(a), vm__type_name(b));
        }

        uint32_t len_a = jacl_string_byte_len(a);
        uint32_t len_b = jacl_string_byte_len(b);
        uint32_t total = len_a + len_b;

        JaclVal res;
        if (total <= 128) {
          /* Small concat: extract bytes and route through jacl_string_new */
          char stack_buf[256];
          char* concat_buf = stack_buf;
          if (total > sizeof(stack_buf)) {
            concat_buf = (char*)arena_alloc(vm->arena, total);
          }
          jacl_string_data(a, concat_buf, len_a);
          jacl_string_data(b, concat_buf + len_a, len_b);
          /* Both inputs are already NFD, so concat is NFD — unicode_is_nfd
           * fast path will skip normalization inside jacl_string_new */
          res = jacl_string_new(&vm->heap, vm->intern_table, concat_buf, total);
        } else {
          /* Large concat: use rope_concat for O(log n) structural sharing */
          rope ra, rb;
          bool free_ra = false, free_rb = false;
          if (jacl_is_rope_string(a)) {
            ra = jacl_as_rope_string(a)->r;
            rope_ref(ra);
          } else {
            char bufa[128];
            jacl_string_data(a, bufa, len_a);
            ra = rope_from_str((const uint8_t*)bufa, len_a);
            free_ra = true;
          }
          if (jacl_is_rope_string(b)) {
            rb = jacl_as_rope_string(b)->r;
            rope_ref(rb);
          } else {
            char bufb[128];
            jacl_string_data(b, bufb, len_b);
            rb = rope_from_str((const uint8_t*)bufb, len_b);
            free_rb = true;
          }
          rope rc = rope_concat(ra, rb);
          /* hash(a ++ b) = continue FNV-1a from hash(a) over b's bytes.
           * jacl_val_hash(a) returns FNV-1a over a's bytes for every
           * string variant (inline / heap / rope), so this avoids
           * rescanning a's bytes — the dominant cost in append-loops. */
          uint32_t hash = rope_string__hash_extend(jacl_val_hash(a), rb);
          JaclRopeString* rs = (JaclRopeString*)gc_alloc(
              &vm->heap, OBJ_ROPE_STRING, sizeof(JaclRopeString));
          rs->hash = hash;
          rs->r    = rc;
          res = jacl_rope_string_ptr(rs);
          /* Release our refs to operand ropes */
          rope_unref(ra);
          rope_unref(rb);
          (void)free_ra; (void)free_rb;
        }

        result = vm__push(vm, res); if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_STR_LEN): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        if (jacl_is_error(val)) { result = vm__push(vm, val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_string(val)) {
          VM_ERROR(vm, "type error in 'length': expected string, got %s",
                       vm__type_name(val));
        }
        result = vm__push(vm, jacl_i32((int32_t)jacl_string_len(val)));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_STR_BYTE_LEN): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        if (jacl_is_error(val)) { result = vm__push(vm, val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_string(val)) {
          VM_ERROR(vm, "type error in 'byte-length': expected string, got %s",
                       vm__type_name(val));
        }
        result = vm__push(vm, jacl_i32((int32_t)jacl_string_byte_len(val)));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_STR_INDEX): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal idx_val, str_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &str_val); if (result != VM_OK) return result;
        if (jacl_is_error(str_val)) { result = vm__push(vm, str_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(idx_val)) { result = vm__push(vm, idx_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_string(str_val)) {
          VM_ERROR(vm, "type error in 'index': expected string, got %s",
                       vm__type_name(str_val));
        }
        if (!jacl_is_i32(idx_val)) {
          VM_ERROR(vm, "type error in 'index': expected i32 index, got %s",
                       vm__type_name(idx_val));
        }
        int32_t idx = jacl_as_i32(idx_val);
        uint32_t glen = jacl_string_len(str_val);
        if (idx < 0 || (uint32_t)idx >= glen) {
          result = vm__push(vm, JACL_NIL);
        } else {
          if (jacl_is_rope_string(str_val)) {
            /* Rope: use rope_grapheme_to_byte for O(log n) seek */
            JaclRopeString* rs = jacl_as_rope_string(str_val);
            rope_offset_result start_res = rope_grapheme_to_byte(rs->r, (size_t)idx);
            rope_offset_result end_res = rope_grapheme_to_byte(rs->r, (size_t)idx + 1);
            size_t byte_start = start_res.found ? start_res.value : 0;
            size_t byte_end = end_res.found ? end_res.value : rope_byte_count(rs->r);
            size_t cluster_len = byte_end - byte_start;
            uint8_t cluster_buf[32];
            rope_slice_to_str(rs->r, byte_start, cluster_len, cluster_buf, sizeof(cluster_buf));
            result = vm__push(vm, jacl_string_new(&vm->heap, vm->intern_table,
                                                   (const char*)cluster_buf, cluster_len));
          } else {
            /* Inline or flat: extract bytes and scan graphemes */
            uint8_t buf[256];
            uint32_t byte_len = jacl_string_data(str_val, (char*)buf, sizeof(buf));
            size_t gs, ge;
            if (jacl_grapheme_nth(buf, byte_len, (size_t)idx, &gs, &ge)) {
              result = vm__push(vm, jacl_string_new(&vm->heap, vm->intern_table,
                                                     (const char*)(buf + gs), ge - gs));
            } else {
              result = vm__push(vm, JACL_NIL);
            }
          }
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_STR_SLICE): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal end_val, start_val, str_val;
        result = vm__pop(vm, &end_val);   if (result != VM_OK) return result;
        result = vm__pop(vm, &start_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &str_val);   if (result != VM_OK) return result;
        if (jacl_is_error(str_val)) { result = vm__push(vm, str_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(start_val)) { result = vm__push(vm, start_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(end_val)) { result = vm__push(vm, end_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_string(str_val)) {
          VM_ERROR(vm, "type error in 'slice': expected string, got %s",
                       vm__type_name(str_val));
        }
        if (!jacl_is_i32(start_val)) {
          VM_ERROR(vm, "type error in 'slice': expected i32 start, got %s",
                       vm__type_name(start_val));
        }
        /* Use grapheme count for bounds, not byte length */
        uint32_t glen = jacl_string_len(str_val);
        int32_t start = jacl_as_i32(start_val);
        int32_t end;
        if (jacl_is_nil(end_val)) {
          end = (int32_t)glen;  /* 2-arg form: slice to end */
        } else if (jacl_is_i32(end_val)) {
          end = jacl_as_i32(end_val);
        } else {
          VM_ERROR(vm, "type error in 'slice': expected i32 end, got %s",
                       vm__type_name(end_val));
        }
        /* Clamp grapheme bounds */
        if (start < 0) start = 0;
        if (end < 0) end = 0;
        if ((uint32_t)start > glen) start = (int32_t)glen;
        if ((uint32_t)end > glen) end = (int32_t)glen;
        if (end < start) end = start;
        uint32_t grapheme_count = (uint32_t)(end - start);

        JaclVal res;
        if (grapheme_count == 0) {
          res = jacl_inline_string("", 0);
        } else if (jacl_is_rope_string(str_val)) {
          /* Rope: use rope_slice_by_graphemes for O(log n) slicing */
          JaclRopeString* rs = jacl_as_rope_string(str_val);
          rope sliced = rope_slice_by_graphemes(rs->r, (size_t)start, grapheme_count);
          size_t sliced_bytes = rope_byte_count(sliced);
          if (sliced_bytes == 0) {
            rope_unref(sliced);
            res = jacl_inline_string("", 0);
          } else if (sliced_bytes <= 128) {
            /* Materialize small rope slices to flat/inline tier */
            uint8_t tmp[128];
            rope_to_str(sliced, tmp, sliced_bytes);
            rope_unref(sliced);
            res = jacl_string_new(&vm->heap, vm->intern_table,
                                  (const char*)tmp, sliced_bytes);
          } else {
            /* Wrap large rope slice in JaclRopeString */
            uint32_t hash = 2166136261u;
            uint8_t hash_buf[256];
            size_t remaining = sliced_bytes;
            size_t offset = 0;
            while (remaining > 0) {
              size_t chunk = remaining < sizeof(hash_buf) ? remaining : sizeof(hash_buf);
              rope_slice_to_str(sliced, offset, chunk, hash_buf, chunk);
              for (size_t i = 0; i < chunk; i++) {
                hash ^= hash_buf[i];
                hash *= 16777619u;
              }
              offset += chunk;
              remaining -= chunk;
            }
            JaclRopeString* new_rs = (JaclRopeString*)gc_alloc(&vm->heap,
                sizeof(JaclRopeString), OBJ_ROPE_STRING);
            new_rs->hash = hash;
            new_rs->r = sliced;
            res = jacl_rope_string_ptr(new_rs);
          }
        } else {
          /* Inline or flat: extract bytes, convert grapheme range to byte range */
          uint8_t buf[256];
          uint32_t byte_len = jacl_string_data(str_val, (char*)buf, sizeof(buf));
          size_t byte_start = unicode_grapheme_byte_offset(buf, byte_len, (size_t)start);
          size_t byte_end = unicode_grapheme_byte_offset(buf, byte_len, (size_t)end);
          size_t slice_bytes = byte_end - byte_start;
          if (slice_bytes == 0) {
            res = jacl_inline_string("", 0);
          } else {
            res = jacl_string_new(&vm->heap, vm->intern_table,
                                  (const char*)(buf + byte_start), slice_bytes);
          }
        }
        result = vm__push(vm, res); if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TO_STRING): {
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        if (jacl_is_error(val)) { result = vm__push(vm, val); if (result != VM_OK) return result; DISPATCH(); }

        /* Cells are transparent — dereference before converting */
        if (jacl_is_cell(val)) {
          JaclMutableRef* ref = jacl_as_cell(val);
          val = MREF_VAL(ref);
        }

        if (jacl_is_string(val)) {
          /* Already a string — push back unchanged */
          result = vm__push(vm, val);
          if (result != VM_OK) return result;
        } else if (jacl_is_vector(val) || jacl_is_arr(val) || jacl_is_map(val) || jacl_is_box(val) || jacl_is_atom(val) || jacl_is_future(val) || jacl_is_stream(val) || jacl_is_typed_vector(val) || jacl_is_typed_map(val)) {
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
          vm__fmt_value(&fmt, val);
          JaclVal str = jacl_string_new(&vm->heap, vm->intern_table,
                                         fmt.data, fmt.len);
          result = vm__push(vm, str);
          if (result != VM_OK) return result;
        } else {
          char buf[64];
          int n = 0;

          if (jacl_is_nil(val)) {
            memcpy(buf, "nil", 3);
            n = 3;
          } else if (jacl_is_bool(val)) {
            if (val == JACL_TRUE) {
              memcpy(buf, "true", 4);
              n = 4;
            } else {
              memcpy(buf, "false", 5);
              n = 5;
            }
          } else if (jacl_is_i32(val)) {
            n = snprintf(buf, sizeof(buf), "%d", (int)jacl_as_i32(val));
          } else if (jacl_is_u32(val)) {
            n = snprintf(buf, sizeof(buf), "%u", (unsigned)jacl_as_u32(val));
          } else if (jacl_is_f32(val)) {
            n = snprintf(buf, sizeof(buf), "%g", (double)jacl_as_f32(val));
          } else if (jacl_is_i64(val)) {
            n = snprintf(buf, sizeof(buf), "%" PRIi64, jacl_as_i64(val));
          } else if (jacl_is_u64(val)) {
            n = snprintf(buf, sizeof(buf), "%" PRIu64, jacl_as_u64(val));
          } else if (jacl_is_f64(val)) {
            n = snprintf(buf, sizeof(buf), "%g", jacl_as_f64(val));
          } else if (jacl_is_closure(val)) {
            JaclClosure* cl = jacl_as_closure(val);
            if (cl->name) {
              n = snprintf(buf, sizeof(buf), "<proc %s>", cl->name);
            } else {
              memcpy(buf, "<closure>", 9);
              n = 9;
            }
          } else {
            memcpy(buf, "<unknown>", 9);
            n = 9;
          }

          if (n < 0) n = 0;
          JaclVal str = jacl_string_new(&vm->heap, vm->intern_table,
                                         buf, (size_t)n);
          result = vm__push(vm, str);
          if (result != VM_OK) return result;
        }
        DISPATCH();
      }

      CASE(OP_VEC): {
        uint8_t count = vm__read_byte(vm);
        gc__current_heap = &vm->heap;
        jacl_vec_root* vec = jacl_vec_empty();
        for (uint8_t i = 0; i < count; i++) {
          JaclVal elem = vm->stack[vm->stack_top - count + i];
          vec = jacl_vec_push_back(vec, elem);
        }
        vm->stack_top -= count;
        result = vm__push(vm, jacl_vector_ptr(vec));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_VEC_SPREAD): {
        uint8_t fixed_args = vm__read_byte(vm);
        uint8_t num_spreads = vm__read_byte(vm);
        uint32_t total_args = fixed_args;
        for (uint8_t i = 0; i < num_spreads; i++) {
          if (vm->spread_count_top == 0) {
            vm__set_error(vm, "spread count underflow");
            return VM_RUNTIME_ERROR;
          }
          total_args += vm->spread_counts[--vm->spread_count_top];
        }
        uint32_t base = vm->stack_top - total_args;
        gc__current_heap = &vm->heap;
        jacl_vec_root* vec = jacl_vec_empty();
        for (uint32_t i = 0; i < total_args; i++) {
          vec = jacl_vec_push_back(vec, vm->stack[base + i]);
        }
        vm->stack_top = base;
        result = vm__push(vm, jacl_vector_ptr(vec));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      /* --- Mutable arr (M3: dyn elements). See ARR_DESIGN.md. Reference
       * semantics: ops mutate the receiver in place. Bounds are lenient
       * (vec-style): get OOB -> nil, set OOB -> grow with nils, pop empty
       * -> nil. Ref stores fire the SATB + generational write barriers,
       * mirroring vm__slot_set / the buf ref-element store path. --- */
      CASE(OP_ARR): {
        uint32_t saved_stack_top = vm->stack_top;
        uint8_t count = vm__read_byte(vm);
        gc__current_heap = &vm->heap;
        JaclArr* a = jacl_arr_new(JACL_SCALAR_TYPE_IDX(TYPE_DYN), sizeof(JaclVal));
        if (!a) VM_ERROR(vm, "out of memory constructing arr");
        for (uint8_t i = 0; i < count; i++) {
          JaclVal elem = vm->stack[vm->stack_top - count + i];
          if (!sa_var_push(&a->sa, &elem))
            VM_ERROR(vm, "out of memory constructing arr");
        }
        vm->stack_top -= count;
        result = vm__push(vm, jacl_arr_ptr(a));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_ARR): {
        /* Flat typed-arr constructor: u16 elem_idx, u8 count. Elements are
         * tagged scalars on the stack (the compiler coerced them); store
         * each as raw bytes. Struct elements are M4d-2. See ARR_DESIGN.md. */
        uint32_t saved_stack_top = vm->stack_top;
        uint16_t elem_idx = vm__read_u16(vm);
        uint8_t  count    = vm__read_byte(vm);
        gc__current_heap = &vm->heap;
        if (JACL_IS_SCALAR_TYPE_IDX(elem_idx)) {
          JaclType elem_t = JACL_TYPE_IDX_TO_SCALAR(elem_idx);
          uint32_t esize  = vm__arr_scalar_size(elem_t);
          if (esize == 0)
            VM_ERROR(vm, "unsupported typed-arr element type %s", type_name(elem_t));
          JaclArr* a = jacl_arr_new(elem_idx, esize);
          if (!a) VM_ERROR(vm, "out of memory constructing arr");
          uint8_t tmp[8];
          for (uint8_t i = 0; i < count; i++) {
            JaclVal elem = vm->stack[vm->stack_top - count + i];
            vm__arr_scalar_store(elem_t, elem, tmp);
            if (!sa_var_push(&a->sa, tmp))
              VM_ERROR(vm, "out of memory constructing arr");
          }
          vm->stack_top -= count;
          result = vm__push(vm, jacl_arr_ptr(a));
          if (result != VM_OK) return result;
          DISPATCH();
        } else {
          /* Struct elements: each occupies `width` inline slots on the stack.
           * Store the wide (width*8-byte) form per element. See ARR_DESIGN.md
           * M4d-2. */
          StructTypeDef* sdef = vm->struct_registry->defs[elem_idx];
          uint32_t width = vm__struct_width(sdef);
          uint32_t esize = width * (uint32_t)sizeof(JaclVal);
          JaclArr* a = jacl_arr_new(elem_idx, esize);
          if (!a) VM_ERROR(vm, "out of memory constructing arr");
          /* Pop elements right-to-left into scratch, then store left-to-right. */
          JaclVal scratch[VM_MAX_STRUCT_SLOTS * 256];
          if ((size_t)count * width > sizeof(scratch) / sizeof(JaclVal))
            VM_ERROR(vm, "typed-arr literal too large");
          for (int32_t i = (int32_t)count - 1; i >= 0; i--) {
            vm__pop_struct(vm, elem_idx, &scratch[(uint32_t)i * width]);
          }
          for (uint8_t i = 0; i < count; i++) {
            if (!sa_var_push(&a->sa, &scratch[(uint32_t)i * width]))
              VM_ERROR(vm, "out of memory constructing arr");
          }
          result = vm__push(vm, jacl_arr_ptr(a));
          if (result != VM_OK) return result;
          DISPATCH();
        }
      }

      CASE(OP_ARR_GET): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal idx_val, arr_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &arr_val); if (result != VM_OK) return result;
        if (jacl_is_error(arr_val)) { result = vm__push(vm, arr_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(idx_val)) { result = vm__push(vm, idx_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_arr(arr_val))
          VM_ERROR(vm, "type error in 'arr-get': expected arr, got %s", vm__type_name(arr_val));
        if (!jacl_is_i32(idx_val))
          VM_ERROR(vm, "type error in 'arr-get': expected i32 index, got %s", vm__type_name(idx_val));
        JaclArr* a = (JaclArr*)jacl_as_ptr(arr_val);
        int32_t idx = jacl_as_i32(idx_val);
        if (vm__arr_is_dyn(a->elem_idx) || JACL_IS_SCALAR_TYPE_IDX(a->elem_idx)) {
          /* dyn / scalar: lenient OOB -> nil, single-slot result. */
          JaclVal out = JACL_NIL;
          if (idx >= 0 && (uint32_t)idx < a->sa.count) {
            uint8_t* slot = sa_var_get(&a->sa, (uint32_t)idx);
            if (vm__arr_is_dyn(a->elem_idx))
              out = *(JaclVal*)slot;
            else
              out = vm__arr_scalar_load(vm, JACL_TYPE_IDX_TO_SCALAR(a->elem_idx), slot);
          }
          result = vm__push(vm, out);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        /* Struct elements: bounds-CHECKED (a missing struct can't be nil
         * inline), push `width` inline slots. Mirrors OP_TYPED_VEC_GET_INLINE. */
        if (idx < 0 || (uint32_t)idx >= a->sa.count)
          VM_ERROR(vm, "arr-get: index %d out of bounds (length %u)",
                   (int)idx, a->sa.count);
        StructTypeDef* sdef = vm->struct_registry->defs[a->elem_idx];
        uint32_t width = vm__struct_width(sdef);
        uint8_t* slot = sa_var_get(&a->sa, (uint32_t)idx);
        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_operand_overflow(vm, "arr get inline");
          return VM_RUNTIME_ERROR;
        }
        memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], slot, sdef->total_size);
        for (uint32_t si = 0; si < width; si++)
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        vm->stack_top += width;
        DISPATCH();
      }

      CASE(OP_ARR_LEN): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal arr_val;
        result = vm__pop(vm, &arr_val); if (result != VM_OK) return result;
        if (jacl_is_error(arr_val)) { result = vm__push(vm, arr_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_arr(arr_val))
          VM_ERROR(vm, "type error in 'arr-len': expected arr, got %s", vm__type_name(arr_val));
        JaclArr* a = (JaclArr*)jacl_as_ptr(arr_val);
        result = vm__push(vm, jacl_i32((int32_t)a->sa.count));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_ARR_PUSH): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal elem, arr_val;
        result = vm__pop(vm, &elem); if (result != VM_OK) return result;
        result = vm__pop(vm, &arr_val); if (result != VM_OK) return result;
        if (jacl_is_error(arr_val)) { result = vm__push(vm, arr_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(elem)) { result = vm__push(vm, elem); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_arr(arr_val))
          VM_ERROR(vm, "type error in 'arr-push': expected arr, got %s", vm__type_name(arr_val));
        JaclArr* a = (JaclArr*)jacl_as_ptr(arr_val);
        if (vm__arr_is_dyn(a->elem_idx)) {
          /* Insertion-side SATB (no old on append) + generational tracking. */
          gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, JACL_NIL, elem);
          gc_remembered_set_barrier(vm->remembered_set, arr_val, elem);
          if (!sa_var_push(&a->sa, &elem))
            VM_ERROR(vm, "out of memory in 'arr-push'");
        } else if (JACL_IS_SCALAR_TYPE_IDX(a->elem_idx)) {
          uint8_t tmp[8];
          vm__arr_scalar_store(JACL_TYPE_IDX_TO_SCALAR(a->elem_idx), elem, tmp);
          if (!sa_var_push(&a->sa, tmp))   /* scalar bytes hold no heap refs */
            VM_ERROR(vm, "out of memory in 'arr-push'");
        } else {
          VM_ERROR(vm, "typed struct arrays not yet supported");
        }
        result = vm__push(vm, jacl_i32((int32_t)a->sa.count));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_ARR_SET): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal elem, idx_val, arr_val;
        result = vm__pop(vm, &elem); if (result != VM_OK) return result;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &arr_val); if (result != VM_OK) return result;
        if (jacl_is_error(arr_val)) { result = vm__push(vm, arr_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(idx_val)) { result = vm__push(vm, idx_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(elem)) { result = vm__push(vm, elem); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_arr(arr_val))
          VM_ERROR(vm, "type error in 'arr-set': expected arr, got %s", vm__type_name(arr_val));
        if (!jacl_is_i32(idx_val))
          VM_ERROR(vm, "type error in 'arr-set': expected i32 index, got %s", vm__type_name(idx_val));
        JaclArr* a = (JaclArr*)jacl_as_ptr(arr_val);
        int32_t idx = jacl_as_i32(idx_val);
        if (idx < 0)
          VM_ERROR(vm, "arr-set: negative index %d", idx);
        uint32_t uidx = (uint32_t)idx;
        if (vm__arr_is_dyn(a->elem_idx)) {
          gc_remembered_set_barrier(vm->remembered_set, arr_val, elem);
          if (uidx < a->sa.count) {
            JaclVal* slot = (JaclVal*)sa_var_get(&a->sa, uidx);
            vm__slot_set(vm, slot, elem);   /* SATB old=*slot + store */
          } else {
            /* Lenient grow: fill the gap with nil (zero == JACL_NIL for the
             * 8-byte dyn slot), then store elem. */
            gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, JACL_NIL, elem);
            while (a->sa.count < uidx) {
              if (!sa_var_push_zero(&a->sa))
                VM_ERROR(vm, "out of memory in 'arr-set'");
            }
            if (!sa_var_push(&a->sa, &elem))
              VM_ERROR(vm, "out of memory in 'arr-set'");
          }
        } else if (JACL_IS_SCALAR_TYPE_IDX(a->elem_idx)) {
          uint8_t tmp[8];
          vm__arr_scalar_store(JACL_TYPE_IDX_TO_SCALAR(a->elem_idx), elem, tmp);
          if (uidx < a->sa.count) {
            sa_var_set(&a->sa, uidx, tmp);   /* in-place raw bytes, no barrier */
          } else {
            while (a->sa.count < uidx) {     /* gap zero-fills (not nil) */
              if (!sa_var_push_zero(&a->sa))
                VM_ERROR(vm, "out of memory in 'arr-set'");
            }
            if (!sa_var_push(&a->sa, tmp))
              VM_ERROR(vm, "out of memory in 'arr-set'");
          }
        } else {
          VM_ERROR(vm, "typed struct arrays not yet supported");
        }
        result = vm__push(vm, arr_val);   /* return the arr for chaining */
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_ARR_POP): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal arr_val;
        result = vm__pop(vm, &arr_val); if (result != VM_OK) return result;
        if (jacl_is_error(arr_val)) { result = vm__push(vm, arr_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_arr(arr_val))
          VM_ERROR(vm, "type error in 'arr-pop': expected arr, got %s", vm__type_name(arr_val));
        JaclArr* a = (JaclArr*)jacl_as_ptr(arr_val);
        if (vm__arr_is_dyn(a->elem_idx) || JACL_IS_SCALAR_TYPE_IDX(a->elem_idx)) {
          JaclVal out = JACL_NIL;
          if (vm__arr_is_dyn(a->elem_idx)) {
            if (sa_var_pop(&a->sa, &out) == 0) {
              /* Deletion-side SATB: the removed value leaves the container. */
              gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, out, JACL_NIL);
            }
          } else {
            uint8_t tmp[8];
            if (sa_var_pop(&a->sa, tmp) == 0)
              out = vm__arr_scalar_load(vm, JACL_TYPE_IDX_TO_SCALAR(a->elem_idx), tmp);
          }
          result = vm__push(vm, out);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        /* Struct elements: pop the wide bytes, push `width` inline slots.
         * Empty -> error (a missing struct can't be nil inline). */
        StructTypeDef* sdef = vm->struct_registry->defs[a->elem_idx];
        uint32_t width = vm__struct_width(sdef);
        JaclVal scratch[VM_MAX_STRUCT_SLOTS];
        if (sa_var_pop(&a->sa, scratch) != 0)
          VM_ERROR(vm, "arr-pop: array is empty");
        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_operand_overflow(vm, "arr pop inline");
          return VM_RUNTIME_ERROR;
        }
        memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], scratch, sdef->total_size);
        for (uint32_t si = 0; si < width; si++)
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        vm->stack_top += width;
        DISPATCH();
      }

      /* Struct-element arr-push: u16 type_idx. Stack [arr, struct-slots].
       * The element width must be known before popping (it sits above the
       * receiver), hence the dedicated opcode. See ARR_DESIGN.md M4d-2. */
      CASE(OP_TYPED_ARR_PUSH): {
        uint32_t saved_stack_top = vm->stack_top;
        uint16_t type_idx = vm__read_u16(vm);
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = vm__struct_width(sdef);
        JaclVal scratch[VM_MAX_STRUCT_SLOTS];
        vm__pop_struct(vm, type_idx, scratch);
        JaclVal arr_val;
        result = vm__pop(vm, &arr_val); if (result != VM_OK) return result;
        if (!jacl_is_arr(arr_val))
          VM_ERROR(vm, "type error in 'arr-push': expected arr, got %s", vm__type_name(arr_val));
        JaclArr* a = (JaclArr*)jacl_as_ptr(arr_val);
        /* Insertion-side barriers for any ref fields in the new struct. */
        for (uint32_t si = 0; si < width; si++) {
          if (sdef->slot_ref_bitmap[si >> 3] & (uint8_t)(1u << (si & 7))) {
            gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, JACL_NIL, scratch[si]);
            gc_remembered_set_barrier(vm->remembered_set, arr_val, scratch[si]);
          }
        }
        if (!sa_var_push(&a->sa, scratch))
          VM_ERROR(vm, "out of memory in 'arr-push'");
        result = vm__push(vm, jacl_i32((int32_t)a->sa.count));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      /* Struct-element arr-set: u16 type_idx. Stack [arr, idx, struct-slots].
       * Lenient grow zero-fills (zero struct = nil ref fields). */
      CASE(OP_TYPED_ARR_SET): {
        uint32_t saved_stack_top = vm->stack_top;
        uint16_t type_idx = vm__read_u16(vm);
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = vm__struct_width(sdef);
        JaclVal scratch[VM_MAX_STRUCT_SLOTS];
        vm__pop_struct(vm, type_idx, scratch);
        JaclVal idx_val, arr_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &arr_val); if (result != VM_OK) return result;
        if (!jacl_is_arr(arr_val))
          VM_ERROR(vm, "type error in 'arr-set': expected arr, got %s", vm__type_name(arr_val));
        if (!jacl_is_i32(idx_val))
          VM_ERROR(vm, "type error in 'arr-set': expected i32 index, got %s", vm__type_name(idx_val));
        JaclArr* a = (JaclArr*)jacl_as_ptr(arr_val);
        int32_t idx = jacl_as_i32(idx_val);
        if (idx < 0)
          VM_ERROR(vm, "arr-set: negative index %d", (int)idx);
        uint32_t uidx = (uint32_t)idx;
        /* SATB deletion-side: grey the overwritten struct's ref fields. */
        if (uidx < a->sa.count) {
          uint8_t* oldslot = sa_var_get(&a->sa, uidx);
          for (uint32_t si = 0; si < width; si++) {
            if (sdef->slot_ref_bitmap[si >> 3] & (uint8_t)(1u << (si & 7))) {
              JaclVal oldref;
              memcpy(&oldref, oldslot + (size_t)si * sizeof(JaclVal), sizeof(JaclVal));
              gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, oldref, JACL_NIL);
            }
          }
        }
        /* Insertion-side barriers for the new struct's ref fields. */
        for (uint32_t si = 0; si < width; si++) {
          if (sdef->slot_ref_bitmap[si >> 3] & (uint8_t)(1u << (si & 7))) {
            gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, JACL_NIL, scratch[si]);
            gc_remembered_set_barrier(vm->remembered_set, arr_val, scratch[si]);
          }
        }
        if (uidx < a->sa.count) {
          sa_var_set(&a->sa, uidx, scratch);
        } else {
          while (a->sa.count < uidx) {   /* gap = zero structs (nil refs) */
            if (!sa_var_push_zero(&a->sa))
              VM_ERROR(vm, "out of memory in 'arr-set'");
          }
          if (!sa_var_push(&a->sa, scratch))
            VM_ERROR(vm, "out of memory in 'arr-set'");
        }
        result = vm__push(vm, arr_val);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_VEC_GET): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal idx_val, vec_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &vec_val); if (result != VM_OK) return result;
        if (jacl_is_error(vec_val)) { result = vm__push(vm, vec_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(idx_val)) { result = vm__push(vm, idx_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_vector(vec_val)) {
          if (jacl_is_stream(vec_val))
            VM_ERROR(vm, "vec-get requires a vector; got stream (use collect to materialize)");
          else
            VM_ERROR(vm, "type error in 'vec-get': expected vector, got %s",
                         vm__type_name(vec_val));
        }
        if (!jacl_is_i32(idx_val)) {
          VM_ERROR(vm, "type error in 'vec-get': expected i32 index, got %s",
                       vm__type_name(idx_val));
        }
        jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(vec_val);
        int32_t idx = jacl_as_i32(idx_val);
        if (idx < 0) {
          result = vm__push(vm, JACL_NIL);
        } else {
          jacl_vec_get_result gr = jacl_vec_get(vec, (uint32_t)idx);
          result = vm__push(vm, gr.found ? gr.value : JACL_NIL);
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_VEC_LEN): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal vec_val;
        result = vm__pop(vm, &vec_val); if (result != VM_OK) return result;
        if (jacl_is_error(vec_val)) { result = vm__push(vm, vec_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_vector(vec_val)) {
          if (jacl_is_stream(vec_val))
            VM_ERROR(vm, "vec-len requires a vector; got stream (use collect to materialize)");
          else
            VM_ERROR(vm, "type error in 'vec-len': expected vector, got %s",
                         vm__type_name(vec_val));
        }
        jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(vec_val);
        result = vm__push(vm, jacl_i32((int32_t)jacl_vec_count(vec)));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_VEC_PUSH): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal elem, vec_val;
        result = vm__pop(vm, &elem); if (result != VM_OK) return result;
        result = vm__pop(vm, &vec_val); if (result != VM_OK) return result;
        if (jacl_is_error(vec_val)) { result = vm__push(vm, vec_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(elem)) { result = vm__push(vm, elem); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_vector(vec_val)) {
          if (jacl_is_stream(vec_val))
            VM_ERROR(vm, "vec-push requires a vector; got stream (use collect to materialize)");
          else
            VM_ERROR(vm, "type error in 'vec-push': expected vector, got %s",
                         vm__type_name(vec_val));
        }
        jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(vec_val);
        gc__current_heap = &vm->heap;
        jacl_vec_root* new_vec = jacl_vec_push_back(vec, elem);
        result = vm__push(vm, jacl_vector_ptr(new_vec));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_VEC_SET): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal elem, idx_val, vec_val;
        result = vm__pop(vm, &elem); if (result != VM_OK) return result;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &vec_val); if (result != VM_OK) return result;
        if (jacl_is_error(vec_val)) { result = vm__push(vm, vec_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(idx_val)) { result = vm__push(vm, idx_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(elem)) { result = vm__push(vm, elem); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_vector(vec_val)) {
          if (jacl_is_stream(vec_val))
            VM_ERROR(vm, "vec-set requires a vector; got stream (use collect to materialize)");
          else
            VM_ERROR(vm, "type error in 'vec-set': expected vector, got %s",
                         vm__type_name(vec_val));
        }
        if (!jacl_is_i32(idx_val)) {
          VM_ERROR(vm, "type error in 'vec-set': expected i32 index, got %s",
                       vm__type_name(idx_val));
        }
        jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(vec_val);
        int32_t idx = jacl_as_i32(idx_val);
        if (idx < 0) {
          VM_ERROR(vm, "vec-set: negative index %d", (int)idx);
        }
        uint32_t count = jacl_vec_count(vec);
        gc__current_heap = &vm->heap;
        if ((uint32_t)idx < count) {
          /* In-bounds: replace element at index */
          jacl_vec_root* new_vec = jacl_vec_set(vec, (uint32_t)idx, elem);
          result = vm__push(vm, jacl_vector_ptr(new_vec));
        } else {
          /* Out-of-bounds: grow vector with nil fill, then set element */
          jacl_vec_root* new_vec = vec;
          for (uint32_t i = count; i < (uint32_t)idx; i++) {
            new_vec = jacl_vec_push_back(new_vec, JACL_NIL);
          }
          jacl_vec_root* final = jacl_vec_push_back(new_vec, elem);
          result = vm__push(vm, jacl_vector_ptr(final));
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_VEC_CONCAT): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal b_val, a_val;
        result = vm__pop(vm, &b_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &a_val); if (result != VM_OK) return result;
        if (jacl_is_error(a_val)) { result = vm__push(vm, a_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(b_val)) { result = vm__push(vm, b_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_vector(a_val)) {
          if (jacl_is_stream(a_val))
            VM_ERROR(vm, "vec-concat requires a vector; got stream (use collect to materialize)");
          else
            VM_ERROR(vm, "type error in 'vec-concat': expected vector, got %s",
                         vm__type_name(a_val));
        }
        if (!jacl_is_vector(b_val)) {
          if (jacl_is_stream(b_val))
            VM_ERROR(vm, "vec-concat requires a vector; got stream (use collect to materialize)");
          else
            VM_ERROR(vm, "type error in 'vec-concat': expected vector, got %s",
                         vm__type_name(b_val));
        }
        jacl_vec_root* va = (jacl_vec_root*)jacl_as_ptr(a_val);
        jacl_vec_root* vb = (jacl_vec_root*)jacl_as_ptr(b_val);
        gc__current_heap = &vm->heap;
        jacl_vec_root* new_vec = jacl_vec_concat(va, vb);
        result = vm__push(vm, jacl_vector_ptr(new_vec));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_VEC_SLICE): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal end_val, start_val, vec_val;
        result = vm__pop(vm, &end_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &start_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &vec_val); if (result != VM_OK) return result;
        if (jacl_is_error(vec_val)) { result = vm__push(vm, vec_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(start_val)) { result = vm__push(vm, start_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(end_val)) { result = vm__push(vm, end_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_vector(vec_val)) {
          if (jacl_is_stream(vec_val))
            VM_ERROR(vm, "vec-slice requires a vector; got stream (use collect to materialize)");
          else
            VM_ERROR(vm, "type error in 'vec-slice': expected vector, got %s",
                         vm__type_name(vec_val));
        }
        if (!jacl_is_i32(start_val)) {
          VM_ERROR(vm, "type error in 'vec-slice': expected i32 start, got %s",
                       vm__type_name(start_val));
        }
        if (!jacl_is_i32(end_val)) {
          VM_ERROR(vm, "type error in 'vec-slice': expected i32 end, got %s",
                       vm__type_name(end_val));
        }
        jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(vec_val);
        int32_t start = jacl_as_i32(start_val);
        int32_t end = jacl_as_i32(end_val);
        uint32_t count = jacl_vec_count(vec);
        /* Clamp bounds */
        if (start < 0) start = 0;
        if (end < 0) end = 0;
        if ((uint32_t)start > count) start = (int32_t)count;
        if ((uint32_t)end > count) end = (int32_t)count;
        gc__current_heap = &vm->heap;
        if (end <= start) {
          result = vm__push(vm, jacl_vector_ptr(jacl_vec_empty()));
        } else {
          jacl_vec_root* new_vec = jacl_vec_slice(vec, (uint32_t)start, (uint32_t)end);
          if (new_vec == NULL) {
            result = vm__push(vm, jacl_vector_ptr(jacl_vec_empty()));
          } else {
            result = vm__push(vm, jacl_vector_ptr(new_vec));
          }
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_MAP): {
        uint8_t pair_count = vm__read_byte(vm);
        jacl_map_node* map = NULL;
        gc__current_heap = &vm->heap;
        for (uint8_t i = 0; i < pair_count; i++) {
          JaclVal key   = vm->stack[vm->stack_top - 2 * pair_count + 2 * i];
          JaclVal value = vm->stack[vm->stack_top - 2 * pair_count + 2 * i + 1];
          jacl_map_node* new_map = jacl_map_set(map, key, value);
          map = new_map;
        }
        vm->stack_top -= 2 * pair_count;
        result = vm__push(vm, jacl_map_ptr(map));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_MAP_GET): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal key_val, map_val;
        result = vm__pop(vm, &key_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &map_val); if (result != VM_OK) return result;
        if (jacl_is_error(map_val)) { result = vm__push(vm, map_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(key_val)) { result = vm__push(vm, key_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_map(map_val)) {
          VM_ERROR(vm, "type error in 'map-get': expected map, got %s",
                       vm__type_name(map_val));
        }
        jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(map_val);
        if (jacl_map_has(map, key_val)) {
          result = vm__push(vm, jacl_map_get(map, key_val));
        } else {
          result = vm__push(vm, JACL_NIL);
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_MAP_HAS): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal key_val, map_val;
        result = vm__pop(vm, &key_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &map_val); if (result != VM_OK) return result;
        if (jacl_is_error(map_val)) { result = vm__push(vm, map_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(key_val)) { result = vm__push(vm, key_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_map(map_val)) {
          VM_ERROR(vm, "type error in 'map-has': expected map, got %s",
                       vm__type_name(map_val));
        }
        jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(map_val);
        result = vm__push(vm, jacl_bool(jacl_map_has(map, key_val)));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_MAP_LEN): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal map_val;
        result = vm__pop(vm, &map_val); if (result != VM_OK) return result;
        if (jacl_is_error(map_val)) { result = vm__push(vm, map_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_map(map_val)) {
          VM_ERROR(vm, "type error in 'map-len': expected map, got %s",
                       vm__type_name(map_val));
        }
        jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(map_val);
        result = vm__push(vm, jacl_i32((int32_t)jacl_map_count(map)));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_MAP_SET): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal val, key_val, map_val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        result = vm__pop(vm, &key_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &map_val); if (result != VM_OK) return result;
        if (jacl_is_error(map_val)) { result = vm__push(vm, map_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(key_val)) { result = vm__push(vm, key_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(val)) { result = vm__push(vm, val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_map(map_val)) {
          VM_ERROR(vm, "type error in 'map-set': expected map, got %s",
                       vm__type_name(map_val));
        }
        jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(map_val);
        gc__current_heap = &vm->heap;
        jacl_map_node* new_map = jacl_map_set(map, key_val, val);
        result = vm__push(vm, jacl_map_ptr(new_map));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_MAP_REMOVE): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal key_val, map_val;
        result = vm__pop(vm, &key_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &map_val); if (result != VM_OK) return result;
        if (jacl_is_error(map_val)) { result = vm__push(vm, map_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(key_val)) { result = vm__push(vm, key_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_map(map_val)) {
          VM_ERROR(vm, "type error in 'map-remove': expected map, got %s",
                       vm__type_name(map_val));
        }
        jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(map_val);
        gc__current_heap = &vm->heap;
        jacl_map_node* new_map = jacl_map_unset(map, key_val);
        result = vm__push(vm, jacl_map_ptr(new_map));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_MAP_KEYS): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal map_val;
        result = vm__pop(vm, &map_val); if (result != VM_OK) return result;
        if (jacl_is_error(map_val)) { result = vm__push(vm, map_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_map(map_val)) {
          VM_ERROR(vm, "type error in 'map-keys': expected map, got %s",
                       vm__type_name(map_val));
        }
        jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(map_val);
        gc__current_heap = &vm->heap;
        jacl_vec_root* vec = jacl_vec_empty();
        jacl_map_iter it = jacl_map_iter_init(map);
        jacl_map_iter_result ir;
        for (;;) {
          ir = jacl_map_next_leaf(&it);
          if (ir.done) break;
          JaclVal key = jacl_map_key_from_leaf(ir.item);
          vec = jacl_vec_push_back(vec, key);
        }
        result = vm__push(vm, jacl_vector_ptr(vec));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_MAP_VALS): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal map_val;
        result = vm__pop(vm, &map_val); if (result != VM_OK) return result;
        if (jacl_is_error(map_val)) { result = vm__push(vm, map_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_map(map_val)) {
          VM_ERROR(vm, "type error in 'map-vals': expected map, got %s",
                       vm__type_name(map_val));
        }
        jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(map_val);
        gc__current_heap = &vm->heap;
        jacl_vec_root* vec = jacl_vec_empty();
        jacl_map_iter it = jacl_map_iter_init(map);
        jacl_map_iter_result ir;
        for (;;) {
          ir = jacl_map_next_leaf(&it);
          if (ir.done) break;
          JaclVal val = jacl_map_value_from_leaf(ir.item);
          vec = jacl_vec_push_back(vec, val);
        }
        result = vm__push(vm, jacl_vector_ptr(vec));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_EACH): {
        /* Operand: rep the callback param expects (wide scalar enc → pass the
         * element wide with no box; 0xFF00 dyn → tagged). Read unconditionally
         * so ip advances; used only in the lazy stream branch. See
         * TYPED_CLOSURES_DESIGN.md Phase A. */
        uint16_t each_cb_elem = vm__read_u16(vm);
        JaclVal closure_val, coll_val;
        JaclVal error_val = JACL_NIL;  /* US-005: track error from stream */
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &coll_val); if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) { result = vm__push(vm, coll_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(closure_val)) { result = vm__push(vm, closure_val); if (result != VM_OK) return result; DISPATCH(); }

        if (!jacl_is_closure(closure_val)) {
          vm__set_error(vm,
            "type error in 'each': expected closure as second argument, got %s",
            vm__type_name(closure_val));
          return VM_RUNTIME_ERROR;
        }

        JaclClosure* closure = jacl_as_closure(closure_val);

        if (jacl_is_vector(coll_val)) {
          if (closure->param_count != 1) {
            vm__set_error(vm,
              "each on vector requires a proc with 1 parameter, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }

          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(coll_val);
          uint32_t count = jacl_vec_count(vec);

          for (uint32_t i = 0; i < count; i++) {
            jacl_vec_get_result gr = jacl_vec_get(vec, i);

            /* Check frame capacity BEFORE pushing args. The original
             * order (push, then check) leaked 2 slots on overflow —
             * see AUDIT.md §D.2 severity-(iii) iterating-opcode leak. */
            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_frame_overflow(vm);
              return VM_RUNTIME_ERROR;
            }

            /* Push closure as callee slot + argument */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, gr.value);
            if (result != VM_OK) return result;

            uint32_t caller_frame_count = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 1;
            cf->chunk      = &closure->chunk;

            /* Switch to closure code and execute */
            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_frame_count);
            if (call_result != VM_OK) return call_result;

            /* Discard return value */
            JaclVal discard;
            result = vm__pop(vm, &discard);
            if (result != VM_OK) return result;

            /* Restore state for next iteration */
            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
        } else if (jacl_is_map(coll_val)) {
          if (closure->param_count != 2) {
            vm__set_error(vm,
              "each on map requires a proc with 2 parameters, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }

          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(coll_val);
          jacl_map_iter it = jacl_map_iter_init(map);
          jacl_map_iter_result ir;

          for (;;) {
            ir = jacl_map_next_leaf(&it);
            if (ir.done) break;

            JaclVal key = jacl_map_key_from_leaf(ir.item);
            JaclVal value = jacl_map_value_from_leaf(ir.item);

            /* Check frame capacity BEFORE pushing args. See AUDIT.md
             * §D.2 — push-then-check leaked 3 slots per overflow. */
            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_frame_overflow(vm);
              return VM_RUNTIME_ERROR;
            }

            /* Push closure as callee slot + key + value */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, key);
            if (result != VM_OK) return result;
            result = vm__push(vm, value);
            if (result != VM_OK) return result;
            uint32_t caller_frame_count = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 2;
            cf->chunk      = &closure->chunk;

            /* Switch to closure code and execute */
            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_frame_count);
            if (call_result != VM_OK) return call_result;

            /* Discard return value */
            JaclVal discard;
            result = vm__pop(vm, &discard);
            if (result != VM_OK) return result;

            /* Restore state for next iteration */
            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
        } else if (jacl_is_stream(coll_val)) {
          if (closure->param_count != 1) {
            vm__set_error(vm,
              "each on stream requires a proc with 1 parameter, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }

          JaclStream* stream = jacl_as_stream(coll_val);

          /* Pull tagged by default (the callback param is dyn). When the typer
           * monomorphized the inline callback to read the element wide
           * (each_cb_elem is a wide scalar enc, TYPED_CLOSURES_DESIGN.md
           * Phase A), pull the raw wide element instead and pass it without a
           * box — the callback body reads it wide. */
          bool each_wide = vm__elem_idx_is_wide(each_cb_elem);
          /* Struct-element streams: pull raw N-slot elements and push them
           * to the callback's by-value struct param (struct HOF
           * monomorphization; compile guard + has_inline_params defense). */
          bool each_struct = vm__elem_idx_is_struct(stream->elem_idx);
          uint32_t each_width = 1;
          if (each_struct) {
            if (!closure->has_inline_params) {
              vm__set_error(vm, "each over a struct-element stream requires "
                                "an inline callback typed against the element");
              return VM_RUNTIME_ERROR;
            }
            each_width = vm__struct_width(
                vm->struct_registry->defs[stream->elem_idx]);
          }
          while (stream->state != STREAM_EXHAUSTED) {
            JaclVal elem_buf[VM_MAX_STRUCT_SLOTS];
            StreamPullResult pr = (each_wide || each_struct)
                ? vm__pull_stream_one(vm, coll_val, elem_buf)
                : vm__pull_stream_dyn(vm, coll_val, elem_buf);
            if (pr == STREAM_PULL_ERROR) return VM_RUNTIME_ERROR;
            if (pr == STREAM_PULL_EXHAUSTED) break;

            /* US-005: If stream yields error value, stop iteration and
             * propagate. Only meaningful for the tagged pull — wide/struct
             * elements carry raw bits, never a heap error value, and raw
             * bits must not be misread as an error tag (cf. transform/
             * filter, which never error-check raw elements). */
            if (!each_wide && !each_struct && jacl_is_error(elem_buf[0])) {
              error_val = elem_buf[0];
              break;
            }

            /* Call callback with elem */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            if (each_struct) {
              if (vm->stack_top + each_width > VM_STACK_MAX) {
                vm__set_operand_overflow(vm, "each struct elem");
                return VM_RUNTIME_ERROR;
              }
              memcpy(&vm->stack[vm->stack_top], elem_buf,
                     each_width * sizeof(JaclVal));
              for (uint32_t si = 0; si < each_width; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += each_width;
            } else {
              result = vm__push(vm, elem_buf[0]);
              if (result != VM_OK) return result;
            }

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_frame_overflow(vm);
              return VM_RUNTIME_ERROR;
            }
            uint32_t cb_fc = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - closure->param_total_slots;
            cf->chunk      = &closure->chunk;

            uint8_t* cb_ip = vm->ip;
            BytecodeChunk* cb_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, cb_fc);
            if (call_result != VM_OK) return call_result;

            JaclVal discard;
            result = vm__pop(vm, &discard);
            if (result != VM_OK) return result;

            vm->ip    = cb_ip;
            vm->chunk = cb_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
        } else {
          vm__set_error(vm,
            "type error in 'each': expected vector, map, or stream, got %s",
            vm__type_name(coll_val));
          return VM_RUNTIME_ERROR;
        }

        /* US-005: each returns error if stream yielded one, otherwise nil */
        if (jacl_is_error(error_val)) {
          result = vm__push(vm, error_val);
        } else {
          result = vm__push(vm, JACL_NIL);
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TRANSFORM): {
        /* Operand: output element-type encoding for the lazy stream case
         * (the mapper's return type; 0xFF00 dyn sentinel when unknown / for
         * the eager vec path). Read unconditionally so ip advances. */
        uint16_t transform_out_elem = vm__read_u16(vm);
        JaclVal closure_val, coll_val;
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &coll_val); if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) { result = vm__push(vm, coll_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(closure_val)) { result = vm__push(vm, closure_val); if (result != VM_OK) return result; DISPATCH(); }

        if (!jacl_is_closure(closure_val)) {
          vm__set_error(vm,
            "type error in 'transform': expected closure as second argument, got %s",
            vm__type_name(closure_val));
          return VM_RUNTIME_ERROR;
        }

        JaclClosure* closure = jacl_as_closure(closure_val);

        if (jacl_is_vector(coll_val)) {
          if (closure->param_count != 1) {
            vm__set_error(vm,
              "transform on vector requires a proc with 1 parameter, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }

          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(coll_val);
          uint32_t count = jacl_vec_count(vec);
          gc__current_heap = &vm->heap;
          jacl_vec_root* result_vec = jacl_vec_empty();

          for (uint32_t i = 0; i < count; i++) {
            jacl_vec_get_result gr = jacl_vec_get(vec, i);

            /* Check frame capacity BEFORE pushing args. The original
             * order (push, then check) leaked 2 slots on overflow —
             * see AUDIT.md §D.2 severity-(iii) iterating-opcode leak. */
            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_frame_overflow(vm);
              return VM_RUNTIME_ERROR;
            }

            /* Push closure as callee slot + argument */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, gr.value);
            if (result != VM_OK) return result;

            uint32_t caller_frame_count = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 1;
            cf->chunk      = &closure->chunk;

            /* Switch to closure code and execute */
            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_frame_count);
            if (call_result != VM_OK) return call_result;

            /* Collect return value */
            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            gc__current_heap = &vm->heap;

            result_vec = jacl_vec_push_back(result_vec, ret);

            /* Restore state for next iteration */
            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }

          result = vm__push(vm, jacl_vector_ptr(result_vec));
          if (result != VM_OK) return result;

        } else if (jacl_is_map(coll_val)) {
          if (closure->param_count != 2) {
            vm__set_error(vm,
              "transform on map requires a proc with 2 parameters, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }

          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(coll_val);
          jacl_map_node* result_map = NULL;
          gc__current_heap = &vm->heap;
          jacl_map_iter it = jacl_map_iter_init(map);
          jacl_map_iter_result ir;

          for (;;) {
            ir = jacl_map_next_leaf(&it);
            if (ir.done) break;

            JaclVal key = jacl_map_key_from_leaf(ir.item);
            JaclVal value = jacl_map_value_from_leaf(ir.item);

            /* Check frame capacity BEFORE pushing args. See AUDIT.md
             * §D.2 — push-then-check leaked 3 slots per overflow. */
            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_frame_overflow(vm);
              return VM_RUNTIME_ERROR;
            }

            /* Push closure as callee slot + key + value */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, key);
            if (result != VM_OK) return result;
            result = vm__push(vm, value);
            if (result != VM_OK) return result;
            uint32_t caller_frame_count = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 2;
            cf->chunk      = &closure->chunk;

            /* Switch to closure code and execute */
            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_frame_count);
            if (call_result != VM_OK) return call_result;

            /* Collect return value — must be a 2-element vector [key new-value] */
            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            if (!jacl_is_vector(ret)) {
              vm__set_error(vm,
                "transform on map: proc must return a 2-element vector, got %s",
                vm__type_name(ret));
              return VM_RUNTIME_ERROR;
            }
            jacl_vec_root* pair = (jacl_vec_root*)jacl_as_ptr(ret);
            if (jacl_vec_count(pair) != 2) {
              vm__set_error(vm,
                "transform on map: proc must return a 2-element vector, got %d elements",
                (int)jacl_vec_count(pair));
              return VM_RUNTIME_ERROR;
            }
            JaclVal new_key = jacl_vec_get(pair, 0).value;
            JaclVal new_val = jacl_vec_get(pair, 1).value;
            jacl_map_node* new_map = jacl_map_set(result_map, new_key, new_val);
            result_map = new_map;

            /* Restore state for next iteration */
            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }

          result = vm__push(vm, jacl_map_ptr(result_map));
          if (result != VM_OK) return result;

        } else if (jacl_is_stream(coll_val)) {
          /* Lazy transform: create a new transform stream wrapping source + fn */
          if (closure->param_count != 1) {
            vm__set_error(vm,
              "transform on stream requires a proc with 1 parameter, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          JaclVal transform_stream_val = jacl_stream(&vm->heap);
          JaclStream* ts = jacl_as_stream(transform_stream_val);
          ts->kind      = STREAM_KIND_TRANSFORM;
          ts->args[0]   = coll_val;     /* source stream */
          ts->args[1]   = closure_val;  /* transform closure */
          ts->arg_count = 2;
          ts->elem_idx  = transform_out_elem; /* mapper return type (B) */
          result = vm__push(vm, transform_stream_val);
          if (result != VM_OK) return result;

        } else {
          vm__set_error(vm,
            "type error in 'transform': expected vector, map, or stream, got %s",
            vm__type_name(coll_val));
          return VM_RUNTIME_ERROR;
        }
        DISPATCH();
      }

      CASE(OP_FILTER): {
        /* Operand: rep the predicate param expects (wide scalar enc → pass the
         * element wide with no box; 0xFF00 dyn → box a wide element for the
         * call). Read unconditionally so ip advances; used only in the lazy
         * stream branch. See TYPED_CLOSURES_DESIGN.md Phase A. */
        uint16_t filter_pred_elem = vm__read_u16(vm);
        JaclVal closure_val, coll_val;
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &coll_val); if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) { result = vm__push(vm, coll_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(closure_val)) { result = vm__push(vm, closure_val); if (result != VM_OK) return result; DISPATCH(); }

        if (!jacl_is_closure(closure_val)) {
          vm__set_error(vm,
            "type error in 'filter': expected closure as second argument, got %s",
            vm__type_name(closure_val));
          return VM_RUNTIME_ERROR;
        }

        JaclClosure* closure = jacl_as_closure(closure_val);

        if (jacl_is_vector(coll_val)) {
          if (closure->param_count != 1) {
            vm__set_error(vm,
              "filter on vector requires a proc with 1 parameter, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }

          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(coll_val);
          uint32_t count = jacl_vec_count(vec);
          gc__current_heap = &vm->heap;
          jacl_vec_root* result_vec = jacl_vec_empty();

          for (uint32_t i = 0; i < count; i++) {
            jacl_vec_get_result gr = jacl_vec_get(vec, i);

            /* Check frame capacity BEFORE pushing args. The original
             * order (push, then check) leaked 2 slots on overflow —
             * see AUDIT.md §D.2 severity-(iii) iterating-opcode leak. */
            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_frame_overflow(vm);
              return VM_RUNTIME_ERROR;
            }

            /* Push closure as callee slot + argument */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, gr.value);
            if (result != VM_OK) return result;

            uint32_t caller_frame_count = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 1;
            cf->chunk      = &closure->chunk;

            /* Switch to closure code and execute */
            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_frame_count);
            if (call_result != VM_OK) return call_result;

            /* Collect return value — keep element if truthy */
            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            if (!vm__is_falsy(ret)) {
              gc__current_heap = &vm->heap;
              result_vec = jacl_vec_push_back(result_vec, gr.value);
            }

            /* Restore state for next iteration */
            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }

          result = vm__push(vm, jacl_vector_ptr(result_vec));
          if (result != VM_OK) return result;

        } else if (jacl_is_map(coll_val)) {
          if (closure->param_count != 2) {
            vm__set_error(vm,
              "filter on map requires a proc with 2 parameters, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }

          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(coll_val);
          jacl_map_node* result_map = NULL;
          gc__current_heap = &vm->heap;
          jacl_map_iter it = jacl_map_iter_init(map);
          jacl_map_iter_result ir;

          for (;;) {
            ir = jacl_map_next_leaf(&it);
            if (ir.done) break;

            JaclVal key = jacl_map_key_from_leaf(ir.item);
            JaclVal value = jacl_map_value_from_leaf(ir.item);

            /* Check frame capacity BEFORE pushing args. See AUDIT.md
             * §D.2 — push-then-check leaked 3 slots per overflow. */
            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_frame_overflow(vm);
              return VM_RUNTIME_ERROR;
            }

            /* Push closure as callee slot + key + value */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, key);
            if (result != VM_OK) return result;
            result = vm__push(vm, value);
            if (result != VM_OK) return result;
            uint32_t caller_frame_count = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 2;
            cf->chunk      = &closure->chunk;

            /* Switch to closure code and execute */
            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_frame_count);
            if (call_result != VM_OK) return call_result;

            /* Collect return value — keep entry if truthy */
            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            if (!vm__is_falsy(ret)) {
              jacl_map_node* new_map = jacl_map_set(result_map, key, value);
              result_map = new_map;
            }

            /* Restore state for next iteration */
            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }

          result = vm__push(vm, jacl_map_ptr(result_map));
          if (result != VM_OK) return result;

        } else if (jacl_is_stream(coll_val)) {
          /* Lazy filter: create a new filter stream wrapping source + predicate */
          if (closure->param_count != 1) {
            vm__set_error(vm,
              "filter on stream requires a proc with 1 parameter, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          JaclVal filter_stream_val = jacl_stream(&vm->heap);
          JaclStream* fs = jacl_as_stream(filter_stream_val);
          fs->kind      = STREAM_KIND_FILTER;
          fs->args[0]   = coll_val;     /* source stream */
          fs->args[1]   = closure_val;  /* predicate closure */
          fs->arg_count = 2;
          /* filter preserves the element type — copy source's elem_idx so the
           * producer-wide rep flows through (B). */
          if (jacl_is_stream(coll_val))
            fs->elem_idx = jacl_as_stream(coll_val)->elem_idx;
          /* Predicate param rep (Phase A): wide enc → the inline predicate
           * reads the element wide, so the pull skips the box-for-call. */
          fs->pred_elem_idx = filter_pred_elem;
          result = vm__push(vm, filter_stream_val);
          if (result != VM_OK) return result;

        } else {
          vm__set_error(vm,
            "type error in 'filter': expected vector, map, or stream, got %s",
            vm__type_name(coll_val));
          return VM_RUNTIME_ERROR;
        }
        DISPATCH();
      }

      CASE(OP_ERROR): {
        /* Peek top-of-stack, set error flag, leave on stack */
        if (vm->stack_top == 0) {
          vm__set_error(vm, "stack underflow");
          return VM_RUNTIME_ERROR;
        }
        vm->stack[vm->stack_top - 1] = jacl_set_error(vm->stack[vm->stack_top - 1]);
        vm__capture_trace(vm);
        DISPATCH();
      }

      CASE(OP_IS_ERROR): {
        /* Pop value, push true if error-flagged, else false */
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        result = vm__push(vm, jacl_bool(jacl_is_error(val)));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_ERROR_VAL): {
        /* Peek top-of-stack, clear error flag, leave on stack */
        if (vm->stack_top == 0) {
          vm__set_error(vm, "stack underflow");
          return VM_RUNTIME_ERROR;
        }
        vm->stack[vm->stack_top - 1] = jacl_clear_error(vm->stack[vm->stack_top - 1]);
        DISPATCH();
      }

      CASE(OP_PANIC): {
        /* Pop one value (the message). Halt unconditionally with the
           provided message. If the value isn't a string, fall back to
           the value's type name. */
        JaclVal msg;
        result = vm__pop(vm, &msg); if (result != VM_OK) return result;
        if (jacl_is_string(msg)) {
          uint32_t mlen = jacl_string_len(msg);
          char mbuf[256];
          uint32_t copy = mlen < sizeof(mbuf) - 1 ? mlen : (uint32_t)(sizeof(mbuf) - 1);
          jacl_string_data(msg, mbuf, copy);
          mbuf[copy] = '\0';
          vm__set_error(vm, "%s", mbuf);
        } else {
          vm__set_error(vm, "panic (non-string message: %s)",
                        vm__type_name(msg));
        }
        return VM_RUNTIME_ERROR;
      }

      CASE(OP_CHECK_ERROR): {
        uint16_t offset = vm__read_u16(vm);
        if (vm->stack_top == 0) {
          vm__set_error(vm, "stack underflow");
          return VM_RUNTIME_ERROR;
        }
        JaclVal top = vm->stack[vm->stack_top - 1];
        if (jacl_is_error(top)) {
          if (offset == 0) {
            /* Return from current frame with the error value */
            JaclVal return_value;
            result = vm__pop(vm, &return_value);
            if (result != VM_OK) return result;

            uint32_t callee_base = frame->stack_base;
            uint8_t* caller_ip   = frame->return_ip;

            vm->frame_count--;

            if (vm->frame_count == 0) {
              /* Returning from top-level */
              vm->stack[0] = return_value;
              vm->stack_top = 1;
              return VM_OK;
            }

            /* Place return value where the callee's closure was */
            vm->stack[callee_base - 1] = return_value;
            vm->stack_top = callee_base;

            frame     = &vm->frames[vm->frame_count - 1];
            vm->ip    = caller_ip;
            vm->chunk = frame->chunk;

            if (vm->frame_count <= min_frame) {
              return VM_OK;
            }
          } else {
            /* Jump to handler (for try, US-004) */
            vm->ip += offset;
          }
        } else {
          /* Not an error: pop and continue */
          vm->stack_top--;
        }
        DISPATCH();
      }

      CASE(OP_JUMP_IF_ERROR): {
        uint16_t offset = vm__read_u16(vm);
        if (vm->stack_top == 0) {
          vm__set_error(vm, "stack underflow");
          return VM_RUNTIME_ERROR;
        }
        JaclVal top = vm->stack[vm->stack_top - 1];
        if (jacl_is_error(top)) {
          vm->ip += offset;
        }
        DISPATCH();
      }

      CASE(OP_STACK_TRACE): {
        if (vm->stack_trace.count == 0) {
          /* No error has been created yet — push empty string */
          result = vm__push(vm, jacl_inline_string("", 0));
          if (result != VM_OK) return result;
        } else {
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
          for (uint32_t i = 0; i < vm->stack_trace.count; i++) {
            StackTraceEntry* e = &vm->stack_trace.entries[i];
            char tmp[128];
            const char* name = e->function_name ? e->function_name : "<main>";
            int n = snprintf(tmp, sizeof(tmp), "  at %s (line %u)",
                             name, (unsigned)e->line_number);
            vm__fmt_append(&fmt, tmp, (uint32_t)n);
            if (i + 1 < vm->stack_trace.count) {
              vm__fmt_append(&fmt, "\n", 1);
            }
          }
          JaclVal trace_str;
          if (fmt.len <= 7) {
            trace_str = jacl_inline_string(fmt.data, fmt.len);
          } else {
            trace_str = jacl_intern(&vm->heap, vm->intern_table, fmt.data, fmt.len);
          }
          result = vm__push(vm, trace_str);
          if (result != VM_OK) return result;
        }
        DISPATCH();
      }

      CASE(OP_MAKE_CELL): {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        JaclMutableRef* ref = (JaclMutableRef*)gc_alloc(&vm->heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef) + sizeof(JaclVal));
        ref->type_idx = 0;
        ref->total_size = sizeof(JaclVal);
        MREF_VAL(ref) = value;
        result = vm__push(vm, jacl_cell_ptr(ref));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_GET_CELL_LOCAL): {
        uint8_t slot = vm__read_byte(vm);
        JaclVal cell = vm->stack[frame->stack_base + slot];
        JaclMutableRef* ref = jacl_as_cell(cell);
        result = vm__push(vm, MREF_VAL(ref));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_SET_CELL_LOCAL): {
        uint8_t slot = vm__read_byte(vm);
        JaclVal new_value;
        result = vm__pop(vm, &new_value); if (result != VM_OK) return result;
        JaclVal cell = vm->stack[frame->stack_base + slot];
        JaclMutableRef* ref = jacl_as_cell(cell);
        gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                         MREF_VAL(ref), new_value);
        gc_remembered_set_barrier(vm->remembered_set, cell, new_value);
        MREF_VAL(ref) = new_value;
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_GET_CELL_UPVALUE): {
        uint8_t index = vm__read_byte(vm);
        JaclVal cell = frame->closure->upvalues[index];
        JaclMutableRef* ref = jacl_as_cell(cell);
        result = vm__push(vm, MREF_VAL(ref));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_SET_CELL_UPVALUE): {
        uint8_t index = vm__read_byte(vm);
        JaclVal new_value;
        result = vm__pop(vm, &new_value); if (result != VM_OK) return result;
        JaclVal cell = frame->closure->upvalues[index];
        JaclMutableRef* ref = jacl_as_cell(cell);
        gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                         MREF_VAL(ref), new_value);
        gc_remembered_set_barrier(vm->remembered_set, cell, new_value);
        MREF_VAL(ref) = new_value;
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_SET_GLOBAL): {
        uint16_t name_idx = vm__read_u16(vm);
        uint8_t* ic_slot_ptr = vm->ip;     /* points at the IC u16 */
        uint16_t cache_slot = vm__read_u16(vm);
        JaclVal name = frame->chunk->constants[name_idx];
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        if (cache_slot < vm->env.count &&
            vm->env.names[cache_slot] == name) {
          /* Release-store env_set uses on its update path so the GC root
           * scanner observes a well-ordered write. */
          ATOMIC_STORE_EXPLICIT(&vm->env.values[cache_slot], value, MEM_RELEASE);
        } else {
          /* Miss — env_set will update-in-place if name exists, else
           * append. Then patch the IC for the next dispatch. Big-endian
           * to match vm__read_u16's hi-then-lo decoding. */
          vm__env_set(vm, name, value);
          for (uint32_t k = 0; k < vm->env.count; k++) {
            if (vm->env.names[k] == name) {
              ic_slot_ptr[0] = (uint8_t)((k >> 8) & 0xFF);
              ic_slot_ptr[1] = (uint8_t)(k & 0xFF);
              break;
            }
          }
        }
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_BOX): {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        if (jacl_is_error(value)) {
          result = vm__push(vm, value); if (result != VM_OK) return result;
          DISPATCH();
        }
        /* Compact untyped-box layout: payload is the JaclVal directly
         * (16 bytes total vs 24 for OBJ_MUTABLE_REF). Struct-typed
         * boxes (OP_BOX_STRUCT below) keep the OBJ_MUTABLE_REF layout
         * since they need type_idx + total_size + raw data tail. */
        JaclVal* slot = (JaclVal*)gc_alloc(&vm->heap, OBJ_BOX_INLINE,
                                            sizeof(JaclVal));
        *slot = value;
        result = vm__push(vm, jacl_box_ptr((JaclMutableRef*)slot));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_BOX_UNCHECKED): {
        /* Same as OP_BOX with the error-propagation branch elided.
         * The compiler emits this variant only when it can prove the
         * operand cannot carry the error flag (literals; arithmetic
         * on i32/i64/f32/f64 of recursively error-free operands —
         * div/mod stay on the checked path since they can produce
         * a div-by-zero error). See
         * compiler__expr_is_error_free for the exact rule. */
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        JaclVal* slot = (JaclVal*)gc_alloc(&vm->heap, OBJ_BOX_INLINE,
                                            sizeof(JaclVal));
        *slot = value;
        result = vm__push(vm, jacl_box_ptr((JaclMutableRef*)slot));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_BOX_STRUCT): {
        /* Wrap a struct value in a JaclMutableRef. Accepts either:
           - N inline struct slots on TOS (marked in inline_slot_bitmap), or
           - a single heap HeapRecord pointer on TOS (legacy global path). */
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "box: invalid struct type index %u", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        if (vm->stack_top < 1) {
          vm__set_error(vm, "box: stack underflow");
          return VM_RUNTIME_ERROR;
        }
        bool tos_is_inline = BITMAP_GET(vm->inline_slot_bitmap, vm->stack_top - 1);
        gc__current_heap = &vm->heap;
        JaclMutableRef* ref = (JaclMutableRef*)gc_alloc(&vm->heap, OBJ_MUTABLE_REF,
                                sizeof(JaclMutableRef) + sdef->total_size);
        ref->type_idx = type_idx;
        ref->total_size = sdef->total_size;
        if (tos_is_inline) {
          if (vm->stack_top < width) {
            vm__set_error(vm, "box: stack underflow for inline struct");
            return VM_RUNTIME_ERROR;
          }
          memcpy(ref->data, &vm->stack[vm->stack_top - width], sdef->total_size);
          for (uint32_t si = 0; si < width; si++) {
            BITMAP_CLR(vm->inline_slot_bitmap, vm->stack_top - width + si);
          }
          vm->stack_top -= width;
        } else {
          JaclVal value = vm->stack[--vm->stack_top];
          if (jacl_is_error(value)) {
            result = vm__push(vm, value); if (result != VM_OK) return result;
            DISPATCH();
          }
          HeapRecord* s = jacl_as_heap_record_ptr(value);
          memcpy(ref->data, s->data, sdef->total_size);
        }
        result = vm__push(vm, jacl_box_ptr(ref));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_ATOM): {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        if (jacl_is_error(value)) {
          result = vm__push(vm, value); if (result != VM_OK) return result;
          DISPATCH();
        }
        /* Atoms get OBJ_ATOM_REF (vs the plain OBJ_MUTABLE_REF used for
         * boxes/cells) and an extra trailing pointer slot for the watcher
         * list, NULL until first [watch ...]. */
        JaclMutableRef* ref = (JaclMutableRef*)gc_alloc(
            &vm->heap, OBJ_ATOM_REF,
            sizeof(JaclMutableRef) + ATOM_REF_DATA_SIZE);
        ref->type_idx = 0;
        ref->total_size = ATOM_REF_DATA_SIZE;
        MREF_VAL(ref) = value;
        *ATOM_WATCHERS_SLOT(ref) = NULL;
        result = vm__push(vm, jacl_atom_ptr(ref));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_IS_BOX): {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        result = vm__push(vm, jacl_bool(jacl_is_box(value)));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_IS_BOX_TYPED): {
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        bool match = false;
        if (jacl_is_box(value)) {
          void* payload = jacl_as_ptr(value);
          uint32_t actual = jacl_box_is_typed(payload)
                              ? ((JaclMutableRef*)payload)->type_idx : 0;
          match = (actual == type_idx);
        }
        result = vm__push(vm, jacl_bool(match));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_IS_BOX_TYPED_VEC): {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        bool match = false;
        if (jacl_is_box(value)) {
          void* payload = jacl_as_ptr(value);
          if (!jacl_box_is_typed(payload)) {
            match = jacl_is_typed_vector(*jacl_box_untyped_val(payload));
          }
        }
        result = vm__push(vm, jacl_bool(match));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_IS_BOX_TYPED_MAP): {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        bool match = false;
        if (jacl_is_box(value)) {
          void* payload = jacl_as_ptr(value);
          if (!jacl_box_is_typed(payload)) {
            match = jacl_is_typed_map(*jacl_box_untyped_val(payload));
          }
        }
        result = vm__push(vm, jacl_bool(match));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_IS_ATOM): {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        result = vm__push(vm, jacl_bool(jacl_is_atom(value)));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_IS_FUTURE): {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        result = vm__push(vm, jacl_bool(jacl_is_future(value)));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_AWAIT): {
        /* CPS await removed — all async functions use OP_AWAIT_SM now */
        vm__set_error(vm, "CPS await not supported (use state machine path)");
        return VM_RUNTIME_ERROR;
      }

      CASE(OP_SPAWN): {
        /* Pop closure, create pending future, execute closure (resolving
           future with result), push future. */
        JaclVal closure_val;
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        if (!jacl_is_closure(closure_val)) {
          vm__set_error(vm, "spawn requires a closure, got %s",
                       vm__type_name(closure_val));
          return VM_RUNTIME_ERROR;
        }
        JaclClosure *cl = jacl_as_closure(closure_val);
        JaclVal f = jacl_future(&vm->heap);
        JaclFuture *fut = jacl_as_future(f);

        if (vm->runtime) {
          /* Runtime mode: submit task to worker thread pool */
          runtime__submit_spawn_task(vm->runtime, cl, f, vm->ctx);
          result = vm__push(vm, f);
          if (result != VM_OK) return result;
        } else {
          /* Single-threaded mode: execute closure synchronously */

          JaclVal saved_ctx = ctx_fork(vm, vm->ctx);

          uint8_t *saved_ip = vm->ip;
          BytecodeChunk *saved_chunk = vm->chunk;
          uint32_t saved_frame_count = vm->frame_count;
          uint32_t saved_stack_top   = vm->stack_top;

          if (cl->is_sm_compiled) {
            /* SM closure: create state machine, call synchronously */
            JaclVal sm_val = gc_alloc_state_machine(&vm->heap, cl->sm_field_count);
            JaclStateMachine *sm = jacl_as_state_machine(sm_val);
            vm__slot_set(vm, &sm->sm_closure, closure_val);

            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, sm_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, JACL_NIL);
            if (result != VM_OK) return result;
          } else {
            /* Non-suspending closure: call directly */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
          }

          if (vm->frame_count >= VM_FRAMES_MAX) {
            vm__set_frame_overflow(vm);
            return VM_STACK_OVERFLOW;
          }
          CallFrame *sf = &vm->frames[vm->frame_count++];
          sf->closure    = cl;
          sf->return_ip  = saved_ip;
          sf->stack_base = vm->stack_top - cl->param_count;
          sf->chunk      = &cl->chunk;
          vm->ip    = cl->chunk.code;
          vm->chunk = &cl->chunk;

          VMResult sub = vm__run(vm, saved_frame_count);

          /* Capture result before restoring VM state.  On error, sub_run
             may leave frame_count/stack_top deep (e.g. an OP_CALL hit
             VM_FRAMES_MAX and bailed without unwinding).  If we don't
             reset them here, the next opcode in the outer chunk runs
             against a stale CallFrame — see §D.6 in AUDIT.md. */
          JaclVal spawn_result = JACL_NIL;
          bool body_ok = (sub == VM_OK && vm->stack_top > saved_stack_top);
          if (body_ok) {
            spawn_result = vm->stack[vm->stack_top - 1];
          }

          vm->frame_count = saved_frame_count;
          vm->stack_top   = saved_stack_top;
          frame = &vm->frames[vm->frame_count - 1];
          vm->ip    = saved_ip;
          vm->chunk = saved_chunk;

          if (body_ok) {
            jacl_future_resolve(fut, spawn_result,
                                vm->grey_buf, vm->gc_active_ptr);
          } else {
            JaclVal err = jacl_set_error(jacl_inline_string("error", 5));
            jacl_future_error(fut, err, vm->grey_buf, vm->gc_active_ptr);
          }

          ctx_unfork(vm, saved_ctx);

          result = vm__push(vm, f);
          if (result != VM_OK) return result;
        }
        DISPATCH();
      }

      CASE(OP_RESOLVE_FUTURE): {
        /* Pop result value, pop future, resolve the future, push nil.
           Used by the resolve_k closure generated for CPS spawn.
           Schedules any registered waiters as tasks (runtime mode). */
        JaclVal resolve_result;
        result = vm__pop(vm, &resolve_result); if (result != VM_OK) return result;
        JaclVal future_val;
        result = vm__pop(vm, &future_val); if (result != VM_OK) return result;
        if (!jacl_is_future(future_val)) {
          vm__set_error(vm, "OP_RESOLVE_FUTURE: expected future, got %s",
                       vm__type_name(future_val));
          return VM_RUNTIME_ERROR;
        }
        JaclFuture *rfut = jacl_as_future(future_val);
        FutureWaiter *waiters = jacl_future_resolve(rfut, resolve_result,
                            vm->grey_buf, vm->gc_active_ptr);
        if (waiters && vm->runtime) {
          runtime__schedule_waiters(vm->runtime, waiters, resolve_result);
        }
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_COMPLETE_PARALLEL): {
        /* Pop result, index (i32), agg_val. Complete parallel slot:
           store result, handle error, increment counter, maybe schedule join.
           Used by parallel_k closures (SM body error_k). */
        JaclVal par_result;
        result = vm__pop(vm, &par_result); if (result != VM_OK) return result;
        JaclVal idx_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        JaclVal par_agg_val;
        result = vm__pop(vm, &par_agg_val); if (result != VM_OK) return result;

        uint32_t par_index = (uint32_t)(idx_val & JACL_PAYLOAD_MASK);

        if (vm->runtime) {
          runtime__complete_parallel_slot(vm->runtime, vm, par_agg_val,
                                          par_index, par_result);
        }

        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_COMPLETE_RACE): {
        /* Pop result, agg_val. CAS-settle race, winner schedules join.
           Used by race_k closures (SM body error_k). */
        JaclVal race_result;
        result = vm__pop(vm, &race_result); if (result != VM_OK) return result;
        JaclVal race_agg_val;
        result = vm__pop(vm, &race_agg_val); if (result != VM_OK) return result;

        if (vm->runtime) {
          runtime__complete_race_slot(vm->runtime, vm, race_agg_val,
                                      race_result);
        }

        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_PARALLEL): {
        /* Parallel: read uint8_t N, pop continuation + N closures.
           Fork N tasks, suspend until all complete, continuation receives
           result vector [r0, r1, ..., r_{N-1}] in input order. */
        uint8_t n = vm__read_byte(vm);

        /* Pop continuation (top of stack) */
        JaclVal continuation;
        result = vm__pop(vm, &continuation); if (result != VM_OK) return result;

        /* Pop N closures (in reverse stack order to get original order) */
        JaclVal closures[256];
        for (int i = (int)n - 1; i >= 0; i--) {
          result = vm__pop(vm, &closures[i]);
          if (result != VM_OK) return result;
        }

        /* Validate types */
        bool par_sm_mode = jacl_is_state_machine(continuation);
        if (!jacl_is_nil(continuation) && !par_sm_mode) {
          vm__set_error(vm, "OP_PARALLEL: continuation must be state machine or nil");
          return VM_RUNTIME_ERROR;
        }
        for (uint8_t i = 0; i < n; i++) {
          if (!jacl_is_closure(closures[i])) {
            vm__set_error(vm, "parallel requires closures, arg %d is %s",
                         (int)i, vm__type_name(closures[i]));
            return VM_RUNTIME_ERROR;
          }
        }

        if (vm->runtime) {
          /* Runtime mode: create aggregate, submit N tasks, suspend */
          JaclVal agg_val = jacl_parallel_agg(&vm->heap, n, continuation);

          for (uint8_t i = 0; i < n; i++) {
            JaclClosure *cl = jacl_as_closure(closures[i]);
            runtime__submit_parallel_task(vm->runtime, cl, agg_val, i, vm->ctx);
          }

          /* Suspend: return from vm__run, worker picks up next task */
          return VM_OK;
        }

        /* Single-threaded mode: run each closure sequentially */
        {
          JaclVal results[256];
          bool has_error = false;
          JaclVal first_error = JACL_NIL;

          for (uint8_t i = 0; i < n; i++) {
            JaclClosure *cl = jacl_as_closure(closures[i]);

            JaclVal par_saved_ctx = ctx_fork(vm, vm->ctx);

            uint8_t *saved_ip = vm->ip;
            BytecodeChunk *saved_chunk = vm->chunk;
            uint32_t saved_frame_count = vm->frame_count;
            uint32_t saved_stack_top = vm->stack_top;

            if (cl->is_sm_compiled) {
              /* Check frame capacity BEFORE allocating SM or pushing
               * args. See AUDIT.md §D.2 — push-then-check leaked 3
               * slots per overflow. */
              if (vm->frame_count >= VM_FRAMES_MAX) {
                vm__set_frame_overflow(vm);
                return VM_STACK_OVERFLOW;
              }

              /* SM: create state machine, call synchronously */
              JaclVal sm_val = gc_alloc_state_machine(&vm->heap, cl->sm_field_count);
              JaclStateMachine *sm = jacl_as_state_machine(sm_val);
              vm__slot_set(vm, &sm->sm_closure, closures[i]);

              result = vm__push(vm, closures[i]);
              if (result != VM_OK) return result;
              result = vm__push(vm, sm_val);
              if (result != VM_OK) return result;
              result = vm__push(vm, JACL_NIL);
              if (result != VM_OK) return result;

              CallFrame *sf = &vm->frames[vm->frame_count++];
              sf->closure    = cl;
              sf->return_ip  = saved_ip;
              sf->stack_base = vm->stack_top - cl->param_count;
              sf->chunk      = &cl->chunk;
              vm->ip    = cl->chunk.code;
              vm->chunk = &cl->chunk;

              VMResult sub = vm__run(vm, saved_frame_count);

              /* Capture result before restoring stack */
              JaclVal body_result = JACL_NIL;
              bool body_ok = (sub == VM_OK && vm->stack_top > saved_stack_top);
              if (body_ok) {
                body_result = vm->stack[vm->stack_top - 1];
              }

              vm->frame_count = saved_frame_count;
              vm->stack_top   = saved_stack_top;
              frame = &vm->frames[vm->frame_count - 1];
              vm->ip    = saved_ip;
              vm->chunk = saved_chunk;

              if (body_ok) {
                results[i] = body_result;
                if (jacl_is_error(results[i]) && !has_error) {
                  has_error = true; first_error = results[i];
                }
              } else if (sub != VM_OK) {
                results[i] = jacl_set_error(jacl_inline_string("error", 5));
                if (!has_error) { has_error = true; first_error = results[i]; }
              } else {
                results[i] = JACL_NIL;
              }
            } else {
              /* Check frame capacity BEFORE pushing args. See AUDIT.md
               * §D.2 — push-then-check leaked 1 slot per overflow. */
              if (vm->frame_count >= VM_FRAMES_MAX) {
                vm__set_frame_overflow(vm);
                return VM_STACK_OVERFLOW;
              }

              /* Non-suspending closure: call directly */
              result = vm__push(vm, closures[i]);
              if (result != VM_OK) return result;

              CallFrame *sf = &vm->frames[vm->frame_count++];
              sf->closure    = cl;
              sf->return_ip  = saved_ip;
              sf->stack_base = vm->stack_top;
              sf->chunk      = &cl->chunk;
              vm->ip    = cl->chunk.code;
              vm->chunk = &cl->chunk;

              VMResult sub = vm__run(vm, saved_frame_count);

              /* Capture result before restoring stack */
              JaclVal body_result = JACL_NIL;
              bool body_ok = (sub == VM_OK && vm->stack_top > saved_stack_top);
              if (body_ok) {
                body_result = vm->stack[vm->stack_top - 1];
              }

              /* Restore VM state: frame_count and stack may not have been
                 properly unwound if the body errored before OP_RETURN */
              vm->frame_count = saved_frame_count;
              vm->stack_top   = saved_stack_top;
              frame = &vm->frames[vm->frame_count - 1];
              vm->ip    = saved_ip;
              vm->chunk = saved_chunk;

              if (body_ok) {
                results[i] = body_result;
                if (jacl_is_error(results[i]) && !has_error) {
                  has_error = true;
                  first_error = results[i];
                }
              } else {
                results[i] = jacl_set_error(jacl_inline_string("error", 5));
                if (!has_error) { has_error = true; first_error = results[i]; }
              }
            }

            ctx_unfork(vm, par_saved_ctx);
          }

          /* Build continuation argument */
          JaclVal cont_arg;
          if (has_error) {
            cont_arg = first_error;
          } else {
            gc__current_heap = &vm->heap;
            jacl_vec_root *vec = jacl_vec_empty();
            for (uint8_t i = 0; i < n; i++) {
              vec = jacl_vec_push_back(vec, results[i]);
            }
            cont_arg = jacl_vector_ptr(vec);
          }

          /* Push result directly (SM mode or nil continuation) */
          result = vm__push(vm, cont_arg);
          if (result != VM_OK) return result;
        }
        DISPATCH();
      }

      CASE(OP_RACE): {
        /* Race: read uint8_t N, pop continuation + N closures.
           Fork N tasks, first to complete wins, continuation receives
           winner's result (or error). */
        uint8_t n = vm__read_byte(vm);

        /* Pop continuation (top of stack) */
        JaclVal continuation;
        result = vm__pop(vm, &continuation); if (result != VM_OK) return result;

        /* Pop N closures (in reverse stack order to get original order) */
        JaclVal closures[256];
        for (int i = (int)n - 1; i >= 0; i--) {
          result = vm__pop(vm, &closures[i]);
          if (result != VM_OK) return result;
        }

        /* Validate types */
        bool race_sm_mode = jacl_is_state_machine(continuation);
        if (!jacl_is_nil(continuation) && !race_sm_mode) {
          vm__set_error(vm, "OP_RACE: continuation must be state machine or nil");
          return VM_RUNTIME_ERROR;
        }
        for (uint8_t i = 0; i < n; i++) {
          if (!jacl_is_closure(closures[i])) {
            vm__set_error(vm, "race requires closures, arg %d is %s",
                         (int)i, vm__type_name(closures[i]));
            return VM_RUNTIME_ERROR;
          }
        }

        if (vm->runtime) {
          /* Runtime mode: create race aggregate, submit N tasks, suspend */
          JaclVal agg_val = jacl_race_agg(&vm->heap, continuation);

          for (uint8_t i = 0; i < n; i++) {
            JaclClosure *cl = jacl_as_closure(closures[i]);
            runtime__submit_race_task(vm->runtime, cl, agg_val, vm->ctx);
          }

          /* Suspend: return from vm__run, worker picks up next task */
          return VM_OK;
        }

        /* Single-threaded mode: run each closure sequentially, first result wins */
        {
          JaclVal winner_result = JACL_NIL;
          bool have_winner = false;

          for (uint8_t i = 0; i < n; i++) {
            JaclClosure *cl = jacl_as_closure(closures[i]);

            JaclVal race_saved_ctx = ctx_fork(vm, vm->ctx);

            uint8_t *saved_ip = vm->ip;
            BytecodeChunk *saved_chunk = vm->chunk;
            uint32_t saved_frame_count = vm->frame_count;
            uint32_t saved_stack_top   = vm->stack_top;

            if (cl->is_sm_compiled) {
              /* Check frame capacity BEFORE allocating SM or pushing
               * args. See AUDIT.md §D.2 — push-then-check leaked 3
               * slots per overflow. */
              if (vm->frame_count >= VM_FRAMES_MAX) {
                vm__set_frame_overflow(vm);
                return VM_STACK_OVERFLOW;
              }

              /* SM: create state machine, call synchronously */
              JaclVal sm_val = gc_alloc_state_machine(&vm->heap, cl->sm_field_count);
              JaclStateMachine *sm = jacl_as_state_machine(sm_val);
              vm__slot_set(vm, &sm->sm_closure, closures[i]);

              result = vm__push(vm, closures[i]);
              if (result != VM_OK) return result;
              result = vm__push(vm, sm_val);
              if (result != VM_OK) return result;
              result = vm__push(vm, JACL_NIL);
              if (result != VM_OK) return result;

              CallFrame *sf = &vm->frames[vm->frame_count++];
              sf->closure    = cl;
              sf->return_ip  = saved_ip;
              sf->stack_base = vm->stack_top - cl->param_count;
              sf->chunk      = &cl->chunk;
              vm->ip    = cl->chunk.code;
              vm->chunk = &cl->chunk;

              VMResult sub = vm__run(vm, saved_frame_count);

              /* Capture result before restoring VM state (see §D.6). */
              JaclVal body_result = JACL_NIL;
              bool body_ok = (sub == VM_OK && vm->stack_top > saved_stack_top);
              if (body_ok) body_result = vm->stack[vm->stack_top - 1];

              vm->frame_count = saved_frame_count;
              vm->stack_top   = saved_stack_top;
              frame = &vm->frames[vm->frame_count - 1];
              vm->ip    = saved_ip;
              vm->chunk = saved_chunk;

              if (!have_winner) {
                have_winner = true;
                winner_result = body_ok ? body_result
                  : jacl_set_error(jacl_inline_string("error", 5));
              }
            } else {
              /* Check frame capacity BEFORE pushing args. See AUDIT.md
               * §D.2 — push-then-check leaked 1 slot per overflow. */
              if (vm->frame_count >= VM_FRAMES_MAX) {
                vm__set_frame_overflow(vm);
                return VM_STACK_OVERFLOW;
              }

              /* Non-suspending closure: call directly */
              result = vm__push(vm, closures[i]);
              if (result != VM_OK) return result;

              CallFrame *sf = &vm->frames[vm->frame_count++];
              sf->closure    = cl;
              sf->return_ip  = saved_ip;
              sf->stack_base = vm->stack_top;
              sf->chunk      = &cl->chunk;
              vm->ip    = cl->chunk.code;
              vm->chunk = &cl->chunk;

              VMResult sub = vm__run(vm, saved_frame_count);

              /* Capture result before restoring VM state (see §D.6). */
              JaclVal body_result = JACL_NIL;
              bool body_ok = (sub == VM_OK && vm->stack_top > saved_stack_top);
              if (body_ok) body_result = vm->stack[vm->stack_top - 1];

              vm->frame_count = saved_frame_count;
              vm->stack_top   = saved_stack_top;
              frame = &vm->frames[vm->frame_count - 1];
              vm->ip    = saved_ip;
              vm->chunk = saved_chunk;

              if (!have_winner) {
                have_winner = true;
                winner_result = body_ok ? body_result
                  : jacl_set_error(jacl_inline_string("error", 5));
              }
            }

            ctx_unfork(vm, race_saved_ctx);
          }

          /* Push result directly (SM mode or nil continuation) */
          result = vm__push(vm, winner_result);
          if (result != VM_OK) return result;
        }
        DISPATCH();
      }

      CASE(OP_DEREF): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal container;
        result = vm__pop(vm, &container); if (result != VM_OK) return result;
        if (jacl_is_error(container)) {
          result = vm__push(vm, container); if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_box(container) && !jacl_is_atom(container)) {
          VM_ERROR(vm, "deref: expected box or atom, got %s",
                       vm__type_name(container));
        }
        void* payload = jacl_as_ptr(container);
        /* jacl_box_is_typed handles both layouts; for atoms (always
         * OBJ_ATOM_REF / type_idx == 0) it correctly returns false. */
        if (jacl_box_is_typed(payload)) {
          /* Struct box: materialize data[] into a heap HeapRecord */
          JaclMutableRef* ref = (JaclMutableRef*)payload;
          if (!vm->struct_registry || ref->type_idx >= vm->struct_registry->count) {
            VM_ERROR(vm, "deref: invalid struct type index %u", (unsigned)ref->type_idx);
          }
          StructTypeDef* sdef = vm->struct_registry->defs[ref->type_idx];
          gc__current_heap = &vm->heap;
          HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                  sizeof(HeapRecord) + sdef->total_size);
          s->type_idx = ref->type_idx;
          s->total_size = sdef->total_size;
          memcpy(s->data, ref->data, sdef->total_size);
          result = vm__push(vm, jacl_heap_record_val(s));
          if (result != VM_OK) return result;
        } else {
          JaclVal* slot = jacl_box_untyped_val(payload);
          JaclVal deref_val;
          if (jacl_is_atom(container)) {
            deref_val = ATOMIC_LOAD_EXPLICIT((uint64_t*)slot, MEM_ACQUIRE);
          } else {
            deref_val = *slot;
          }
          result = vm__push(vm, deref_val);
          if (result != VM_OK) return result;
        }
        DISPATCH();
      }

      CASE(OP_RESET): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal new_val, container;
        result = vm__pop(vm, &new_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &container); if (result != VM_OK) return result;
        if (jacl_is_error(container)) {
          result = vm__push(vm, container); if (result != VM_OK) return result;
          DISPATCH();
        }
        if (jacl_is_error(new_val)) {
          result = vm__push(vm, new_val); if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_box(container) && !jacl_is_atom(container)) {
          VM_ERROR(vm, "reset!: expected box or atom, got %s",
                       vm__type_name(container));
        }
        void* payload = jacl_as_ptr(container);
        if (jacl_box_is_typed(payload)) {
          /* Struct box: copy new struct data into box */
          JaclMutableRef* ref = (JaclMutableRef*)payload;
          HeapRecord* s = jacl_as_heap_record_ptr(new_val);
          if (!s || s->type_idx != ref->type_idx) {
            VM_ERROR(vm, "reset!: struct type mismatch in box");
          }
          StructTypeDef* sdef = vm->struct_registry->defs[ref->type_idx];
          memcpy(ref->data, s->data, sdef->total_size);
          /* No GC write barrier needed — struct data has no GC references */
          result = vm__push(vm, new_val);
        } else if (jacl_is_atom(container)) {
          /* Atoms always use OBJ_ATOM_REF (never OBJ_BOX_INLINE), so the
           * JaclMutableRef cast is safe here. */
          JaclMutableRef* ref = (JaclMutableRef*)payload;
          JaclVal reset_old = ATOMIC_LOAD_EXPLICIT(&MREF_VAL(ref), MEM_ACQUIRE);
          gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                           reset_old, new_val);
          gc_remembered_set_barrier(vm->remembered_set, container, new_val);
          ATOMIC_STORE_EXPLICIT(&MREF_VAL(ref), new_val, MEM_RELEASE);
          /* Fire watchers after the store commits. The reset_old/new_val
           * pair is unambiguous — reset is an unconditional store, so the
           * load just before it is what we replaced. (Concurrent reset on
           * another thread could fire its own watchers with a different
           * old; that's still a valid total ordering.) */
          VMResult fr = vm__fire_atom_watchers(vm, ref, reset_old, new_val);
          if (fr != VM_OK) return fr;
          frame = &vm->frames[vm->frame_count - 1];
          result = vm__push(vm, new_val);
        } else {
          JaclVal* slot = jacl_box_untyped_val(payload);
          gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                           *slot, new_val);
          gc_remembered_set_barrier(vm->remembered_set, container, new_val);
          *slot = new_val;
          result = vm__push(vm, new_val);
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_RESET_INLINE): {
        /* Struct-box reset with inline new bytes.
           Operand: u16 type_idx.
           Stack before: [..., box, s0, s1, ..., s{N-1}]
           Stack after:  [..., s0, s1, ..., s{N-1}]
           (the new bytes overwrite the box slot, so reset's return is the
           new struct value as inline TOS — symmetric with OP_RESET). */
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "reset_inline: invalid struct type index %u", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        if (vm->stack_top < width + 1) {
          vm__set_error(vm, "reset_inline: stack underflow");
          return VM_RUNTIME_ERROR;
        }
        uint32_t box_pos = vm->stack_top - width - 1;
        JaclVal box_val = vm->stack[box_pos];
        if (jacl_is_error(box_val)) {
          /* Drop inline slots; leave the error JaclVal at TOS. */
          for (uint32_t si = 0; si < width; si++) {
            BITMAP_CLR(vm->inline_slot_bitmap, vm->stack_top - width + si);
          }
          vm->stack_top -= width;
          DISPATCH();
        }
        if (!jacl_is_box(box_val)) {
          vm__set_error(vm, "reset_inline: expected struct box, got %s",
                       vm__type_name(box_val));
          return VM_RUNTIME_ERROR;
        }
        void* reset_payload = jacl_as_ptr(box_val);
        if (!jacl_box_is_typed(reset_payload)) {
          vm__set_error(vm, "reset_inline: struct type mismatch in box");
          return VM_RUNTIME_ERROR;
        }
        JaclMutableRef* ref = (JaclMutableRef*)reset_payload;
        if (ref->type_idx != type_idx) {
          vm__set_error(vm, "reset_inline: struct type mismatch in box");
          return VM_RUNTIME_ERROR;
        }
        /* Copy new bytes into box->data BEFORE shuffling (shuffle would
           overwrite the box-slot but the bytes source is above it). */
        memcpy(ref->data, &vm->stack[vm->stack_top - width], sdef->total_size);
        /* Shuffle inline slots down by 1 to overwrite the box slot. */
        for (uint32_t si = 0; si < width; si++) {
          uint32_t src_idx = vm->stack_top - width + si;
          uint32_t dst_idx = box_pos + si;
          vm->stack[dst_idx] = vm->stack[src_idx];
          if (BITMAP_GET(vm->inline_slot_bitmap, src_idx)) {
            BITMAP_SET(vm->inline_slot_bitmap, dst_idx);
          } else {
            BITMAP_CLR(vm->inline_slot_bitmap, dst_idx);
          }
        }
        BITMAP_CLR(vm->inline_slot_bitmap, vm->stack_top - 1);
        vm->stack_top -= 1;
        DISPATCH();
      }

      CASE(OP_SWAP): {
        uint32_t saved_stack_top = vm->stack_top;
        JaclVal closure_val, container;
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &container); if (result != VM_OK) return result;
        if (jacl_is_error(container)) {
          result = vm__push(vm, container); if (result != VM_OK) return result;
          DISPATCH();
        }
        if (jacl_is_error(closure_val)) {
          result = vm__push(vm, closure_val); if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_box(container) && !jacl_is_atom(container)) {
          VM_ERROR(vm, "swap!: expected box or atom, got %s",
                       vm__type_name(container));
        }
        if (!jacl_is_closure(closure_val)) {
          VM_ERROR(vm, "swap!: expected closure as second argument, got %s",
                       vm__type_name(closure_val));
        }
        void* payload = jacl_as_ptr(container);
        JaclMutableRef* ref = (JaclMutableRef*)payload;
        JaclClosure* closure = jacl_as_closure(closure_val);

        if (closure->param_count != 1) {
          VM_ERROR(vm,
            "swap!: closure must take 1 parameter, got %d",
            (int)closure->param_count);
        }

        bool swap_is_atom = jacl_is_atom(container);
        uint8_t* saved_ip = vm->ip;
        BytecodeChunk* saved_chunk = vm->chunk;
        JaclVal swap_result;
        JaclVal committed_old = JACL_NIL;  /* set by the atom CAS-success branch */

        if (jacl_box_is_typed(payload)) {
          /* Struct box swap: pass current value to closure, copy result back.
             Atoms cannot hold structs (compile error), so this is always non-atomic. */
          StructTypeDef* sdef = vm->struct_registry->defs[ref->type_idx];
          uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);

          /* Phase 5d: push inline bytes if closure expects struct param,
             otherwise materialize to heap for dyn param */
          result = vm__push(vm, closure_val);
          if (result != VM_OK) return result;

          if (closure->param_total_slots > closure->param_count) {
            /* Closure expects inline struct param — push raw bytes */
            if (vm->stack_top + width > VM_STACK_MAX) {
              vm__set_operand_overflow(vm, "swap inline");
              return VM_STACK_OVERFLOW;
            }
            memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
            memcpy(&vm->stack[vm->stack_top], ref->data, sdef->total_size);
            for (uint32_t si = 0; si < width; si++)
              BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
            vm->stack_top += width;
          } else {
            /* Closure expects dyn/heap param — materialize */
            gc__current_heap = &vm->heap;
            HeapRecord* old_s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                        sizeof(HeapRecord) + sdef->total_size);
            old_s->type_idx = ref->type_idx;
            old_s->total_size = sdef->total_size;
            memcpy(old_s->data, ref->data, sdef->total_size);
            result = vm__push(vm, jacl_heap_record_val(old_s));
            if (result != VM_OK) return result;
          }

          if (vm->frame_count >= VM_FRAMES_MAX) {
            vm__set_frame_overflow(vm);
            return VM_RUNTIME_ERROR;
          }
          uint32_t caller_frame_count = vm->frame_count;
          CallFrame* cf = &vm->frames[vm->frame_count++];
          cf->closure    = closure;
          cf->return_ip  = vm->ip;
          cf->stack_base = vm->stack_top - closure->param_total_slots;
          cf->chunk      = &closure->chunk;
          vm->ip    = closure->chunk.code;
          vm->chunk = &closure->chunk;

          VMResult call_result = vm__run(vm, caller_frame_count);
          if (call_result != VM_OK) return call_result;

          /* Closure may return inline bytes (OP_RETURN_WIDE) or heap ptr.
             Check bitmap to determine which. */
          {
            uint32_t rb = vm->stack_top - width;
            if (BITMAP_GET(vm->inline_slot_bitmap, rb)) {
              /* Inline bytes — copy directly from stack to box */
              memcpy(ref->data, &vm->stack[rb], sdef->total_size);
              for (uint32_t si = 0; si < width; si++)
                BITMAP_CLR(vm->inline_slot_bitmap, rb + si);
              vm->stack_top = rb;
              /* swap_result = nil for struct swap (result is the box itself) */
              swap_result = JACL_NIL;
            } else {
              result = vm__pop(vm, &swap_result);
              if (result != VM_OK) return result;
              HeapRecord* new_s = jacl_as_heap_record_ptr(swap_result);
              if (!new_s || new_s->type_idx != ref->type_idx) {
                vm__set_error(vm, "swap!: struct type mismatch in box");
                return VM_RUNTIME_ERROR;
              }
              memcpy(ref->data, new_s->data, sdef->total_size);
            }
          }
        } else {
          /* Untyped box or atom. Atoms always use OBJ_ATOM_REF (so MREF_VAL
           * is correct); untyped boxes may use OBJ_BOX_INLINE — use the
           * dispatch helper for both reads and writes. */
          JaclVal* slot = swap_is_atom
            ? &MREF_VAL(ref)
            : jacl_box_untyped_val(payload);
          for (;;) {
            /* Read current value (atomic for atoms, plain for boxes) */
            JaclVal swap_old_val = swap_is_atom
              ? ATOMIC_LOAD_EXPLICIT((uint64_t*)slot, MEM_ACQUIRE)
              : *slot;

            /* Push closure as callee slot + current value as argument */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, swap_old_val);
            if (result != VM_OK) return result;

            /* Set up call frame */
            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_frame_overflow(vm);
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_frame_count = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 1;
            cf->chunk      = &closure->chunk;

            /* Switch to closure code and execute */
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_frame_count);
            if (call_result != VM_OK) return call_result;

            /* Pop return value */
            result = vm__pop(vm, &swap_result);
            if (result != VM_OK) return result;

            if (swap_is_atom) {
              /* CAS loop: try to store result, retry if value changed */
              JaclVal expected = swap_old_val;
              if (ATOMIC_CAS((uint64_t*)slot, &expected, swap_result,
                             MEM_ACQ_REL, MEM_ACQUIRE)) {
                /* CAS succeeded — fire write barrier */
                gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                                 swap_old_val, swap_result);
                gc_remembered_set_barrier(vm->remembered_set,
                                          container, swap_result);
                committed_old = swap_old_val;
                break; /* exit retry loop */
              }
              /* CAS failed — swap_result becomes garbage, retry */
            } else {
              /* Box: non-atomic store, write barrier always fires */
              gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                               swap_old_val, swap_result);
              gc_remembered_set_barrier(vm->remembered_set,
                                        container, swap_result);
              *slot = swap_result;
              break; /* no retry for boxes */
            }
          }
        }

        /* Restore state */
        vm->ip    = saved_ip;
        vm->chunk = saved_chunk;
        frame = &vm->frames[vm->frame_count - 1];

        /* Fire watchers for atom mutations now that the CAS committed.
         * Only atoms carry watchers (OBJ_ATOM_REF); plain box / struct-box
         * paths skip this entirely. */
        if (swap_is_atom) {
          JaclMutableRef* atom_ref = (JaclMutableRef*)jacl_as_ptr(container);
          VMResult fr = vm__fire_atom_watchers(vm, atom_ref,
                                                committed_old, swap_result);
          if (fr != VM_OK) return fr;
          frame = &vm->frames[vm->frame_count - 1];
        }

        result = vm__push(vm, swap_result);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      /* --- OP_WATCH / OP_UNWATCH — atom watcher registration ---
       *
       * Stack on entry:
       *   OP_WATCH:   ..., atom, key, fn       (pops 3)
       *   OP_UNWATCH: ..., atom, key           (pops 2)
       * Always pushes nil on success. Type errors are VM_RUNTIME_ERROR
       * (programmer bug). Allocation/CAS proceeds via the COW helper
       * vm__atom_watchers_rebind, which retries on lost CAS races. */

      CASE(OP_WATCH): {
        JaclVal fn_val, key_val, atom_val;
        result = vm__pop(vm, &fn_val);   if (result != VM_OK) return result;
        result = vm__pop(vm, &key_val);  if (result != VM_OK) return result;
        result = vm__pop(vm, &atom_val); if (result != VM_OK) return result;
        if (!jacl_is_atom(atom_val)) {
          vm__set_error(vm, "watch: first argument must be an atom, got %s",
                       vm__type_name(atom_val));
          return VM_RUNTIME_ERROR;
        }
        if (!jacl_is_closure(fn_val)) {
          vm__set_error(vm, "watch: third argument must be a closure, got %s",
                       vm__type_name(fn_val));
          return VM_RUNTIME_ERROR;
        }
        JaclClosure* cl = jacl_as_closure(fn_val);
        if (cl->param_count != 2) {
          vm__set_error(vm,
            "watch: closure must take 2 parameters (old, new), got %d",
            (int)cl->param_count);
          return VM_RUNTIME_ERROR;
        }
        JaclMutableRef* ref = jacl_as_atom(atom_val);
        for (int spin = 0; spin < 64; spin++) {
          if (vm__atom_watchers_rebind(vm, ref, atom_val, key_val,
                                        fn_val, false)) {
            break;
          }
          if (spin == 63) {
            vm__set_error(vm, "watch: CAS contention too high (give up after 64 retries)");
            return VM_RUNTIME_ERROR;
          }
        }
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_UNWATCH): {
        JaclVal key_val, atom_val;
        result = vm__pop(vm, &key_val);  if (result != VM_OK) return result;
        result = vm__pop(vm, &atom_val); if (result != VM_OK) return result;
        if (!jacl_is_atom(atom_val)) {
          vm__set_error(vm, "unwatch: first argument must be an atom, got %s",
                       vm__type_name(atom_val));
          return VM_RUNTIME_ERROR;
        }
        JaclMutableRef* ref = jacl_as_atom(atom_val);
        for (int spin = 0; spin < 64; spin++) {
          if (vm__atom_watchers_rebind(vm, ref, atom_val, key_val,
                                        JACL_NIL, true)) {
            break;
          }
          if (spin == 63) {
            vm__set_error(vm, "unwatch: CAS contention too high");
            return VM_RUNTIME_ERROR;
          }
        }
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      /* --- VM-internal macros for typed arithmetic opcodes (vm.c only) --- */

/* i64 binary arithmetic: pop b then a, apply op, push raw i64 result */
#define VM__I64_BINOP(op) do {                                               \
    JaclVal raw_b_, raw_a_;                                                   \
    result = vm__pop(vm, &raw_b_); if (result != VM_OK) return result;        \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    int64_t a_ = (int64_t)raw_a_, b_ = (int64_t)raw_b_;                      \
    result = vm__push(vm, (uint64_t)(a_ op b_));                              \
    if (result != VM_OK) return result;                                       \
} while(0)

/* i64 div/mod: pop b then a, zero-check b, apply op, push raw i64 result */
#define VM__I64_DIVOP(op) do {                                               \
    JaclVal raw_b_, raw_a_;                                                   \
    result = vm__pop(vm, &raw_b_); if (result != VM_OK) return result;        \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    int64_t a_ = (int64_t)raw_a_, b_ = (int64_t)raw_b_;                      \
    if (b_ == 0) { vm__set_error(vm, "division by zero"); return VM_RUNTIME_ERROR; } \
    result = vm__push(vm, (uint64_t)(a_ op b_));                              \
    if (result != VM_OK) return result;                                       \
} while(0)

/* i64 unary negation: pop a, push -a */
#define VM__I64_NEG() do {                                                   \
    JaclVal raw_a_;                                                           \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    result = vm__push(vm, (uint64_t)(-(int64_t)raw_a_));                      \
    if (result != VM_OK) return result;                                       \
} while(0)

/* i64 comparison: pop b then a, apply op, push jacl_bool result */
#define VM__I64_CMPOP(op) do {                                               \
    JaclVal raw_b_, raw_a_;                                                   \
    result = vm__pop(vm, &raw_b_); if (result != VM_OK) return result;        \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    int64_t a_ = (int64_t)raw_a_, b_ = (int64_t)raw_b_;                      \
    result = vm__push(vm, a_ op b_ ? JACL_TRUE : JACL_FALSE);                \
    if (result != VM_OK) return result;                                       \
} while(0)

/* f64 binary arithmetic: pop b then a via memcpy, apply op, push raw f64 result */
#define VM__F64_BINOP(op) do {                                               \
    JaclVal raw_b_, raw_a_;                                                   \
    result = vm__pop(vm, &raw_b_); if (result != VM_OK) return result;        \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    double a_, b_;                                                            \
    memcpy(&a_, &raw_a_, sizeof(double)); memcpy(&b_, &raw_b_, sizeof(double));\
    double r_ = a_ op b_;                                                    \
    uint64_t raw_r_; memcpy(&raw_r_, &r_, sizeof(uint64_t));                 \
    result = vm__push(vm, raw_r_);                                           \
    if (result != VM_OK) return result;                                      \
} while(0)

/* f64 div: pop b then a, zero-check b, divide, push raw f64 result */
#define VM__F64_DIVOP() do {                                                 \
    JaclVal raw_b_, raw_a_;                                                   \
    result = vm__pop(vm, &raw_b_); if (result != VM_OK) return result;        \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    double a_, b_;                                                            \
    memcpy(&a_, &raw_a_, sizeof(double)); memcpy(&b_, &raw_b_, sizeof(double));\
    if (b_ == 0.0) { vm__set_error(vm, "division by zero"); return VM_RUNTIME_ERROR; } \
    double r_ = a_ / b_;                                                     \
    uint64_t raw_r_; memcpy(&raw_r_, &r_, sizeof(uint64_t));                 \
    result = vm__push(vm, raw_r_);                                           \
    if (result != VM_OK) return result;                                      \
} while(0)

/* f64 mod: pop b then a, zero-check b, fmod, push raw f64 result */
#define VM__F64_MODOP() do {                                                 \
    JaclVal raw_b_, raw_a_;                                                   \
    result = vm__pop(vm, &raw_b_); if (result != VM_OK) return result;        \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    double a_, b_;                                                            \
    memcpy(&a_, &raw_a_, sizeof(double)); memcpy(&b_, &raw_b_, sizeof(double));\
    if (b_ == 0.0) { vm__set_error(vm, "division by zero"); return VM_RUNTIME_ERROR; } \
    double r_ = fmod(a_, b_);                                                \
    uint64_t raw_r_; memcpy(&raw_r_, &r_, sizeof(uint64_t));                 \
    result = vm__push(vm, raw_r_);                                           \
    if (result != VM_OK) return result;                                      \
} while(0)

/* f64 unary negation: pop a via memcpy, push -a as raw f64 */
#define VM__F64_NEG() do {                                                   \
    JaclVal raw_a_;                                                           \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    double a_; memcpy(&a_, &raw_a_, sizeof(double));                         \
    double r_ = -a_;                                                         \
    uint64_t raw_r_; memcpy(&raw_r_, &r_, sizeof(uint64_t));                 \
    result = vm__push(vm, raw_r_);                                           \
    if (result != VM_OK) return result;                                      \
} while(0)

/* f64 comparison: pop b then a via memcpy, apply op, push jacl_bool result */
#define VM__F64_CMPOP(op) do {                                               \
    JaclVal raw_b_, raw_a_;                                                   \
    result = vm__pop(vm, &raw_b_); if (result != VM_OK) return result;        \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    double a_, b_;                                                            \
    memcpy(&a_, &raw_a_, sizeof(double)); memcpy(&b_, &raw_b_, sizeof(double));\
    result = vm__push(vm, a_ op b_ ? JACL_TRUE : JACL_FALSE);                \
    if (result != VM_OK) return result;                                      \
} while(0)

      /* --- M11: i64 typed arithmetic/comparison opcodes --- */

      CASE(OP_ADD_I64): { VM__I64_BINOP(+); DISPATCH(); }
      CASE(OP_SUB_I64): { VM__I64_BINOP(-); DISPATCH(); }
      CASE(OP_MUL_I64): { VM__I64_BINOP(*); DISPATCH(); }
      CASE(OP_DIV_I64): { VM__I64_DIVOP(/); DISPATCH(); }
      CASE(OP_MOD_I64): { VM__I64_DIVOP(%); DISPATCH(); }
      CASE(OP_NEG_I64): { VM__I64_NEG(); DISPATCH(); }
      CASE(OP_LT_I64):  { VM__I64_CMPOP(<); DISPATCH(); }
      CASE(OP_GT_I64):  { VM__I64_CMPOP(>); DISPATCH(); }
      CASE(OP_LE_I64):  { VM__I64_CMPOP(<=); DISPATCH(); }
      CASE(OP_GE_I64):  { VM__I64_CMPOP(>=); DISPATCH(); }
      CASE(OP_EQ_I64):  { VM__I64_CMPOP(==); DISPATCH(); }

      /* --- M11: f64 typed arithmetic/comparison opcodes --- */

      CASE(OP_ADD_F64): { VM__F64_BINOP(+); DISPATCH(); }
      CASE(OP_SUB_F64): { VM__F64_BINOP(-); DISPATCH(); }
      CASE(OP_MUL_F64): { VM__F64_BINOP(*); DISPATCH(); }
      CASE(OP_DIV_F64): { VM__F64_DIVOP(); DISPATCH(); }
      CASE(OP_MOD_F64): { VM__F64_MODOP(); DISPATCH(); }
      CASE(OP_NEG_F64): { VM__F64_NEG(); DISPATCH(); }
      CASE(OP_LT_F64):  { VM__F64_CMPOP(<); DISPATCH(); }
      CASE(OP_GT_F64):  { VM__F64_CMPOP(>); DISPATCH(); }
      CASE(OP_LE_F64):  { VM__F64_CMPOP(<=); DISPATCH(); }
      CASE(OP_GE_F64):  { VM__F64_CMPOP(>=); DISPATCH(); }
      CASE(OP_EQ_F64):  { VM__F64_CMPOP(==); DISPATCH(); }

      /* --- M11: u64 unsigned-specific opcodes --- */

      CASE(OP_DIV_U64): {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        uint64_t a = raw_a;
        uint64_t b = raw_b;
        if (b == 0) {
          vm__set_error(vm, "division by zero");
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, a / b);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_MOD_U64): {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        uint64_t a = raw_a;
        uint64_t b = raw_b;
        if (b == 0) {
          vm__set_error(vm, "division by zero");
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, a % b);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_LT_U64): {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        result = vm__push(vm, raw_a < raw_b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_GT_U64): {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        result = vm__push(vm, raw_a > raw_b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_LE_U64): {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        result = vm__push(vm, raw_a <= raw_b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_GE_U64): {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        result = vm__push(vm, raw_a >= raw_b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      /* --- M11: Type conversion opcodes --- */

      CASE(OP_TO_I32): {
        uint32_t saved_stack_top = vm->stack_top;
        uint8_t src_type = vm__read_byte(vm);
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        int32_t i;
        switch ((JaclType)src_type) {
          case TYPE_I32: { result = vm__push(vm, val); break; }
          case TYPE_U32: { i = (int32_t)jacl_as_u32(val); result = vm__push(vm, jacl_i32(i)); break; }
          case TYPE_I64: { i = (int32_t)(int64_t)val; result = vm__push(vm, jacl_i32(i)); break; }
          case TYPE_U64: { i = (int32_t)(uint64_t)val; result = vm__push(vm, jacl_i32(i)); break; }
          case TYPE_F32: { i = (int32_t)jacl_as_f32(val); result = vm__push(vm, jacl_i32(i)); break; }
          case TYPE_F64: { double d; memcpy(&d, &val, sizeof(double)); i = (int32_t)d; result = vm__push(vm, jacl_i32(i)); break; }
          case TYPE_DYN: {
            if (jacl_is_i32(val)) { result = vm__push(vm, jacl_i32(jacl_as_i32(val))); }
            else if (jacl_is_u32(val)) { result = vm__push(vm, jacl_i32((int32_t)jacl_as_u32(val))); }
            else if (jacl_is_i64(val)) { result = vm__push(vm, jacl_i32((int32_t)jacl_as_i64(val))); }
            else if (jacl_is_u64(val)) { result = vm__push(vm, jacl_i32((int32_t)jacl_as_u64(val))); }
            else if (jacl_is_f32(val)) { result = vm__push(vm, jacl_i32((int32_t)jacl_as_f32(val))); }
            else if (jacl_is_f64(val)) { result = vm__push(vm, jacl_i32((int32_t)jacl_as_f64(val))); }
            else { VM_ERROR(vm, "cannot convert %s to i32", vm__type_name(val)); }
            break;
          }
          default: { VM_ERROR(vm, "invalid source type for to-i32"); }
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TO_I64): {
        uint32_t saved_stack_top = vm->stack_top;
        uint8_t src_type = vm__read_byte(vm);
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        int64_t i;
        switch ((JaclType)src_type) {
          case TYPE_I32: { i = (int64_t)jacl_as_i32(val); result = vm__push(vm, (uint64_t)i); break; }
          case TYPE_U32: { i = (int64_t)(uint32_t)jacl_as_u32(val); result = vm__push(vm, (uint64_t)i); break; }
          case TYPE_I64: { result = vm__push(vm, val); break; }
          case TYPE_U64: { result = vm__push(vm, val); break; }
          case TYPE_F32: { i = (int64_t)jacl_as_f32(val); result = vm__push(vm, (uint64_t)i); break; }
          case TYPE_F64: { double d; memcpy(&d, &val, sizeof(double)); i = (int64_t)d; result = vm__push(vm, (uint64_t)i); break; }
          case TYPE_DYN: {
            if (jacl_is_i32(val)) { i = (int64_t)jacl_as_i32(val); }
            else if (jacl_is_u32(val)) { i = (int64_t)(uint32_t)jacl_as_u32(val); }
            else if (jacl_is_i64(val)) { i = jacl_as_i64(val); }
            else if (jacl_is_u64(val)) { i = (int64_t)jacl_as_u64(val); }
            else if (jacl_is_f32(val)) { i = (int64_t)jacl_as_f32(val); }
            else if (jacl_is_f64(val)) { i = (int64_t)jacl_as_f64(val); }
            else { VM_ERROR(vm, "cannot convert %s to i64", vm__type_name(val)); }
            result = vm__push(vm, (uint64_t)i);
            break;
          }
          default: { VM_ERROR(vm, "invalid source type for to-i64"); }
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TO_U32): {
        uint32_t saved_stack_top = vm->stack_top;
        uint8_t src_type = vm__read_byte(vm);
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        uint32_t u;
        switch ((JaclType)src_type) {
          case TYPE_I32: { u = (uint32_t)jacl_as_i32(val); result = vm__push(vm, jacl_u32(u)); break; }
          case TYPE_U32: { result = vm__push(vm, val); break; }
          case TYPE_I64: { u = (uint32_t)(int64_t)val; result = vm__push(vm, jacl_u32(u)); break; }
          case TYPE_U64: { u = (uint32_t)(uint64_t)val; result = vm__push(vm, jacl_u32(u)); break; }
          case TYPE_F32: { u = (uint32_t)jacl_as_f32(val); result = vm__push(vm, jacl_u32(u)); break; }
          case TYPE_F64: { double d; memcpy(&d, &val, sizeof(double)); u = (uint32_t)d; result = vm__push(vm, jacl_u32(u)); break; }
          case TYPE_DYN: {
            if (jacl_is_i32(val)) { u = (uint32_t)jacl_as_i32(val); }
            else if (jacl_is_u32(val)) { u = jacl_as_u32(val); }
            else if (jacl_is_i64(val)) { u = (uint32_t)jacl_as_i64(val); }
            else if (jacl_is_u64(val)) { u = (uint32_t)jacl_as_u64(val); }
            else if (jacl_is_f32(val)) { u = (uint32_t)jacl_as_f32(val); }
            else if (jacl_is_f64(val)) { u = (uint32_t)jacl_as_f64(val); }
            else { VM_ERROR(vm, "cannot convert %s to u32", vm__type_name(val)); }
            result = vm__push(vm, jacl_u32(u));
            break;
          }
          default: { VM_ERROR(vm, "invalid source type for to-u32"); }
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TO_U64): {
        uint32_t saved_stack_top = vm->stack_top;
        uint8_t src_type = vm__read_byte(vm);
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        uint64_t u;
        switch ((JaclType)src_type) {
          case TYPE_I32: { u = (uint64_t)(int64_t)jacl_as_i32(val); result = vm__push(vm, u); break; }
          case TYPE_U32: { u = (uint64_t)jacl_as_u32(val); result = vm__push(vm, u); break; }
          case TYPE_I64: { result = vm__push(vm, val); break; }
          case TYPE_U64: { result = vm__push(vm, val); break; }
          case TYPE_F32: { u = (uint64_t)jacl_as_f32(val); result = vm__push(vm, u); break; }
          case TYPE_F64: { double d; memcpy(&d, &val, sizeof(double)); u = (uint64_t)d; result = vm__push(vm, u); break; }
          case TYPE_DYN: {
            if (jacl_is_i32(val)) { u = (uint64_t)(int64_t)jacl_as_i32(val); }
            else if (jacl_is_u32(val)) { u = (uint64_t)jacl_as_u32(val); }
            else if (jacl_is_i64(val)) { u = (uint64_t)jacl_as_i64(val); }
            else if (jacl_is_u64(val)) { u = jacl_as_u64(val); }
            else if (jacl_is_f32(val)) { u = (uint64_t)jacl_as_f32(val); }
            else if (jacl_is_f64(val)) { u = (uint64_t)jacl_as_f64(val); }
            else { VM_ERROR(vm, "cannot convert %s to u64", vm__type_name(val)); }
            result = vm__push(vm, u);
            break;
          }
          default: { VM_ERROR(vm, "invalid source type for to-u64"); }
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TO_F32): {
        uint32_t saved_stack_top = vm->stack_top;
        uint8_t src_type = vm__read_byte(vm);
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        float f;
        switch ((JaclType)src_type) {
          case TYPE_I32: { f = (float)jacl_as_i32(val); result = vm__push(vm, jacl_f32(f)); break; }
          case TYPE_U32: { f = (float)jacl_as_u32(val); result = vm__push(vm, jacl_f32(f)); break; }
          case TYPE_I64: { f = (float)(int64_t)val; result = vm__push(vm, jacl_f32(f)); break; }
          case TYPE_U64: { f = (float)(uint64_t)val; result = vm__push(vm, jacl_f32(f)); break; }
          case TYPE_F32: { result = vm__push(vm, val); break; }
          case TYPE_F64: { double d; memcpy(&d, &val, sizeof(double)); f = (float)d; result = vm__push(vm, jacl_f32(f)); break; }
          case TYPE_DYN: {
            if (jacl_is_i32(val)) { f = (float)jacl_as_i32(val); }
            else if (jacl_is_u32(val)) { f = (float)jacl_as_u32(val); }
            else if (jacl_is_i64(val)) { f = (float)jacl_as_i64(val); }
            else if (jacl_is_u64(val)) { f = (float)jacl_as_u64(val); }
            else if (jacl_is_f32(val)) { f = jacl_as_f32(val); }
            else if (jacl_is_f64(val)) { f = (float)jacl_as_f64(val); }
            else { VM_ERROR(vm, "cannot convert %s to f32", vm__type_name(val)); }
            result = vm__push(vm, jacl_f32(f));
            break;
          }
          default: { VM_ERROR(vm, "invalid source type for to-f32"); }
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TO_F64): {
        uint32_t saved_stack_top = vm->stack_top;
        uint8_t src_type = vm__read_byte(vm);
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        double d = 0.0;
        bool need_push = true;
        switch ((JaclType)src_type) {
          case TYPE_I32: { d = (double)jacl_as_i32(val); break; }
          case TYPE_U32: { d = (double)jacl_as_u32(val); break; }
          case TYPE_I64: { d = (double)(int64_t)val; break; }
          case TYPE_U64: { d = (double)(uint64_t)val; break; }
          case TYPE_F32: { d = (double)jacl_as_f32(val); break; }
          case TYPE_F64: { result = vm__push(vm, val); need_push = false; break; }
          case TYPE_DYN: {
            if (jacl_is_i32(val)) { d = (double)jacl_as_i32(val); }
            else if (jacl_is_u32(val)) { d = (double)jacl_as_u32(val); }
            else if (jacl_is_i64(val)) { d = (double)jacl_as_i64(val); }
            else if (jacl_is_u64(val)) { d = (double)jacl_as_u64(val); }
            else if (jacl_is_f32(val)) { d = (double)jacl_as_f32(val); }
            else if (jacl_is_f64(val)) { d = jacl_as_f64(val); }
            else { VM_ERROR(vm, "cannot convert %s to f64", vm__type_name(val)); }
            break;
          }
          default: { VM_ERROR(vm, "invalid source type for to-f64"); }
        }
        if (need_push) {
          uint64_t raw;
          memcpy(&raw, &d, sizeof(uint64_t));
          result = vm__push(vm, raw);
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TO_DYN): {
        uint8_t src_type = vm__read_byte(vm);
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        switch ((JaclType)src_type) {
          case TYPE_I32:
          case TYPE_U32:
          case TYPE_F32:
          case TYPE_BOOL:
          case TYPE_NIL:
          case TYPE_STR:
          case TYPE_DYN: {
            /* Already tagged — push as-is */
            result = vm__push(vm, val);
            break;
          }
          case TYPE_I64: {
            int64_t i = (int64_t)val;
            result = vm__push(vm, jacl_i64(&vm->heap, i));
            break;
          }
          case TYPE_U64: {
            uint64_t u = val;
            result = vm__push(vm, jacl_u64(&vm->heap, u));
            break;
          }
          case TYPE_F64: {
            double d;
            memcpy(&d, &val, sizeof(double));
            result = vm__push(vm, jacl_f64(&vm->heap, d));
            break;
          }
          default: {
            result = vm__push(vm, val);
            break;
          }
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      /* --- M11: Typed constant opcodes --- */

      CASE(OP_CONST_I64):
      CASE(OP_CONST_U64):
      CASE(OP_CONST_F64): {
        uint16_t idx = vm__read_u16(vm);
        /* Push raw 64-bit value from constant pool (no tag) */
        result = vm__push(vm, vm->chunk->constants[idx]);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_HEAP_RECORD_GET): {
        uint16_t field_offset = vm__read_u16(vm);
        uint8_t field_type = vm__read_byte(vm);
        JaclVal struct_val;
        result = vm__pop(vm, &struct_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(struct_val)) {
          result = vm__push(vm, struct_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_struct(struct_val)) {
          vm__set_error(vm, "field access on non-struct value");
          return VM_RUNTIME_ERROR;
        }
        HeapRecord* s = jacl_as_heap_record_ptr(struct_val);
        /* NULL heap → unboxed 64-bit (raw bits for typed arithmetic) */
        JaclVal field_val = vm__heap_record_read_field(NULL, s, field_offset, field_type);
        result = vm__push(vm, field_val);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_HEAP_RECORD_SET): {
        uint16_t field_offset = vm__read_u16(vm);
        uint8_t field_type = vm__read_byte(vm);
        JaclVal new_val;
        result = vm__pop(vm, &new_val);
        if (result != VM_OK) return result;
        JaclVal struct_val;
        result = vm__pop(vm, &struct_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(struct_val)) {
          result = vm__push(vm, struct_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_struct(struct_val)) {
          vm__set_error(vm, "field mutation on non-struct value");
          return VM_RUNTIME_ERROR;
        }
        HeapRecord* s = jacl_as_heap_record_ptr(struct_val);
        /* Write barrier for reference-type fields during active GC */
        if (field_type == TYPE_DYN || field_type == TYPE_STR ||
            field_type == TYPE_STRUCT || field_type == TYPE_MAP) {
          JaclVal old_val;
          memcpy(&old_val, s->data + field_offset, sizeof(JaclVal));
          gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                           old_val, new_val);
        }
        vm__heap_record_write_field(s, field_offset, field_type, new_val);
        /* Push struct value back (for chaining) */
        result = vm__push(vm, struct_val);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_HEAP_RECORD_GET_INLINE): {
        /* Pop a heap record, push N inline slots from data+offset.
           Operands: u16 byte_offset, u16 sub_type_idx. */
        uint16_t field_offset = vm__read_u16(vm);
        uint16_t sub_type_idx = vm__read_u16(vm);
        JaclVal struct_val;
        result = vm__pop(vm, &struct_val); if (result != VM_OK) return result;
        if (jacl_is_error(struct_val)) {
          result = vm__push(vm, struct_val); if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_struct(struct_val)) {
          vm__set_error(vm, "field access on non-struct value");
          return VM_RUNTIME_ERROR;
        }
        if (!vm->struct_registry || sub_type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid sub-struct type index %u", (unsigned)sub_type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sub_sdef = vm->struct_registry->defs[sub_type_idx];
        uint32_t sub_width = (sub_sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        if (vm->stack_top + sub_width > VM_STACK_MAX) {
          vm__set_operand_overflow(vm, "heap_record_get_inline");
          return VM_STACK_OVERFLOW;
        }
        HeapRecord* s = jacl_as_heap_record_ptr(struct_val);
        memset(&vm->stack[vm->stack_top], 0, sub_width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], s->data + field_offset, sub_sdef->total_size);
        for (uint32_t si = 0; si < sub_width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        }
        vm->stack_top += sub_width;
        DISPATCH();
      }

      CASE(OP_HEAP_RECORD_SET_INLINE): {
        /* Pop N inline struct slots, pop heap record, copy bytes to
           data+offset, push record back (for chaining).
           Operands: u16 byte_offset, u16 sub_type_idx. */
        uint16_t field_offset = vm__read_u16(vm);
        uint16_t sub_type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || sub_type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid sub-struct type index %u for heap_record_set_inline", (unsigned)sub_type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sub_sdef = vm->struct_registry->defs[sub_type_idx];
        uint32_t sub_width = (sub_sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        if (vm->stack_top < sub_width + 1) {
          vm__set_error(vm, "heap_record_set_inline: stack underflow");
          return VM_RUNTIME_ERROR;
        }
        /* Inline slots are above the heap record on stack: [..., record, s0, s1, ...] */
        uint8_t* inline_src = (uint8_t*)&vm->stack[vm->stack_top - sub_width];
        JaclVal struct_val = vm->stack[vm->stack_top - sub_width - 1];
        if (jacl_is_error(struct_val)) {
          /* Pop the inline slots and propagate error. */
          for (uint32_t si = 0; si < sub_width; si++) {
            BITMAP_CLR(vm->inline_slot_bitmap, vm->stack_top - sub_width + si);
          }
          vm->stack_top -= sub_width;
          /* error JaclVal already on TOS where record was; leave it */
          DISPATCH();
        }
        if (!jacl_is_struct(struct_val)) {
          vm__set_error(vm, "field mutation on non-struct value");
          return VM_RUNTIME_ERROR;
        }
        HeapRecord* s = jacl_as_heap_record_ptr(struct_val);
        memcpy(s->data + field_offset, inline_src, sub_sdef->total_size);
        /* Pop the inline slots; the heap record stays on TOS. */
        for (uint32_t si = 0; si < sub_width; si++) {
          BITMAP_CLR(vm->inline_slot_bitmap, vm->stack_top - sub_width + si);
        }
        vm->stack_top -= sub_width;
        DISPATCH();
      }

      CASE(OP_PTR_LOAD): {
        /* Pop a u64 pointer (typed [Ptr T] from typer's perspective),
         * read N bytes at *ptr+offset interpreted as field_type, push
         * the resulting value. Used for typed pointer field reads
         * ($p->x) and scalar pointer derefs ([ptr-deref $p]).
         * Operands: u16 byte_offset, u8 field_type. */
        uint16_t byte_offset = vm__read_u16(vm);
        uint8_t  field_type  = vm__read_byte(vm);
        JaclVal ptr_val;
        result = vm__pop(vm, &ptr_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(ptr_val)) {
          result = vm__push(vm, ptr_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_u64(ptr_val)) {
          vm__set_error(vm, "ptr-load: expected pointer (u64), got non-pointer value");
          return VM_RUNTIME_ERROR;
        }
        uint8_t* base = (uint8_t*)(uintptr_t)jacl_as_u64(ptr_val);
        if (!base) {
          vm__set_error(vm, "ptr-load: null pointer dereference");
          return VM_RUNTIME_ERROR;
        }
        JaclVal field_val;
        switch ((JaclType)field_type) {
          case TYPE_BOOL: field_val = jacl_bool(base[byte_offset]); break;
          case TYPE_I8:  { int8_t   n; memcpy(&n, base + byte_offset, 1); field_val = jacl_i32((int32_t)n); break; }
          case TYPE_U8:  { uint8_t  n = base[byte_offset]; field_val = jacl_i32((int32_t)n); break; }
          case TYPE_I16: { int16_t  n; memcpy(&n, base + byte_offset, 2); field_val = jacl_i32((int32_t)n); break; }
          case TYPE_U16: { uint16_t n; memcpy(&n, base + byte_offset, 2); field_val = jacl_i32((int32_t)n); break; }
          case TYPE_I32: { int32_t  n; memcpy(&n, base + byte_offset, 4); field_val = jacl_i32(n); break; }
          case TYPE_U32: { uint32_t n; memcpy(&n, base + byte_offset, 4); field_val = jacl_u32(n); break; }
          case TYPE_F32: { float    f; memcpy(&f, base + byte_offset, 4); field_val = jacl_f32(f); break; }
          case TYPE_I64: { int64_t  n; memcpy(&n, base + byte_offset, 8); field_val = jacl_i64(&vm->heap, n); break; }
          case TYPE_U64: { uint64_t n; memcpy(&n, base + byte_offset, 8); field_val = jacl_u64(&vm->heap, n); break; }
          case TYPE_F64: { double   d; memcpy(&d, base + byte_offset, 8); field_val = jacl_f64(&vm->heap, d); break; }
          case TYPE_DYN: case TYPE_STR: case TYPE_VEC: case TYPE_MAP:
          case TYPE_CLOSURE: case TYPE_STREAM: {
            /* Ref-elem buf field slot: the byte range at base+offset
             * holds a tagged JaclVal that the GC walker / inline-slot
             * bitmap treat as live. Load it directly. See BUFFER_DESIGN.md
             * (ref-elem bufs as struct fields). */
            memcpy(&field_val, base + byte_offset, sizeof(JaclVal));
            break;
          }
          default:
            vm__set_error(vm, "ptr-load: unsupported field type %u", (unsigned)field_type);
            return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, field_val);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_PTR_STORE): {
        /* Pop value, pop pointer, write value at *ptr+offset
         * interpreted as field_type. Pushes the pointer back so set
         * forms can chain. Operands: u16 byte_offset, u8 field_type. */
        uint16_t byte_offset = vm__read_u16(vm);
        uint8_t  field_type  = vm__read_byte(vm);
        JaclVal new_val;
        result = vm__pop(vm, &new_val);
        if (result != VM_OK) return result;
        JaclVal ptr_val;
        result = vm__pop(vm, &ptr_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(ptr_val)) {
          result = vm__push(vm, ptr_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (jacl_is_error(new_val)) {
          result = vm__push(vm, new_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_u64(ptr_val)) {
          vm__set_error(vm, "ptr-store: expected pointer (u64), got non-pointer value");
          return VM_RUNTIME_ERROR;
        }
        uint8_t* base = (uint8_t*)(uintptr_t)jacl_as_u64(ptr_val);
        if (!base) {
          vm__set_error(vm, "ptr-store: null pointer dereference");
          return VM_RUNTIME_ERROR;
        }
        switch ((JaclType)field_type) {
          case TYPE_BOOL: { base[byte_offset] = (uint8_t)(jacl_is_bool(new_val) ? jacl_as_bool(new_val) : 0); break; }
          case TYPE_I8: case TYPE_U8: {
            int32_t  n = jacl_is_i32(new_val) ? jacl_as_i32(new_val)
                       : (jacl_is_u32(new_val) ? (int32_t)jacl_as_u32(new_val) : 0);
            base[byte_offset] = (uint8_t)n;
            break;
          }
          case TYPE_I16: case TYPE_U16: {
            int32_t  n = jacl_is_i32(new_val) ? jacl_as_i32(new_val)
                       : (jacl_is_u32(new_val) ? (int32_t)jacl_as_u32(new_val) : 0);
            uint16_t w = (uint16_t)n;
            memcpy(base + byte_offset, &w, 2);
            break;
          }
          case TYPE_I32: { int32_t  n = jacl_is_i32(new_val) ? jacl_as_i32(new_val) : 0;             memcpy(base + byte_offset, &n, 4); break; }
          case TYPE_U32: { uint32_t n = jacl_is_u32(new_val) ? jacl_as_u32(new_val) : 0;             memcpy(base + byte_offset, &n, 4); break; }
          case TYPE_F32: { float    f = jacl_is_f32(new_val) ? jacl_as_f32(new_val) : 0.0f;          memcpy(base + byte_offset, &f, 4); break; }
          case TYPE_I64: { int64_t  n = jacl_is_i64(new_val) ? jacl_as_i64(new_val) : 0;             memcpy(base + byte_offset, &n, 8); break; }
          case TYPE_U64: { uint64_t n = jacl_is_u64(new_val) ? jacl_as_u64(new_val) : 0;             memcpy(base + byte_offset, &n, 8); break; }
          case TYPE_F64: { double   d = jacl_is_f64(new_val) ? jacl_as_f64(new_val) : 0.0;           memcpy(base + byte_offset, &d, 8); break; }
          case TYPE_DYN: case TYPE_STR: case TYPE_VEC: case TYPE_MAP:
          case TYPE_CLOSURE: case TYPE_STREAM: {
            /* Ref-elem buf field slot store. Fire the SATB write barrier
             * with the old slot value + new value before publishing the
             * new pointer, then store via release to pair with the
             * marker's acquire-load in gc__trace_object. See
             * BUFFER_DESIGN.md (ref-elem bufs as struct fields). */
            JaclVal old_val;
            memcpy(&old_val, base + byte_offset, sizeof(JaclVal));
            gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, old_val, new_val);
            ATOMIC_STORE_EXPLICIT((uint64_t*)(base + byte_offset),
                                  (uint64_t)new_val, MEM_RELEASE);
            break;
          }
          default:
            vm__set_error(vm, "ptr-store: unsupported field type %u", (unsigned)field_type);
            return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, ptr_val);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_PTR_OFFSET): {
        /* Typed pointer arithmetic: pop n (signed), pop u64 p, push
         * p + n * elem_size. Used by [ptr-offset $p $n] for walking
         * arrays. The compiler bakes elem_size in based on the
         * pointee's static type. n may be i32 or i64. */
        uint16_t elem_size = vm__read_u16(vm);
        JaclVal n_val;
        result = vm__pop(vm, &n_val);
        if (result != VM_OK) return result;
        JaclVal ptr_val;
        result = vm__pop(vm, &ptr_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(ptr_val)) {
          result = vm__push(vm, ptr_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (jacl_is_error(n_val)) {
          result = vm__push(vm, n_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_u64(ptr_val)) {
          vm__set_error(vm, "ptr-offset: expected pointer (u64) base");
          return VM_RUNTIME_ERROR;
        }
        int64_t n;
        if      (jacl_is_i32(n_val)) n = (int64_t)jacl_as_i32(n_val);
        else if (jacl_is_i64(n_val)) n = jacl_as_i64(n_val);
        else if (jacl_is_u32(n_val)) n = (int64_t)jacl_as_u32(n_val);
        else if (jacl_is_u64(n_val)) n = (int64_t)jacl_as_u64(n_val);
        else {
          vm__set_error(vm, "ptr-offset: expected integer offset");
          return VM_RUNTIME_ERROR;
        }
        uint64_t base = jacl_as_u64(ptr_val);
        uint64_t out  = base + (uint64_t)(n * (int64_t)elem_size);
        result = vm__push(vm, jacl_u64(&vm->heap, out));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_PTR_OFFSET_CHECKED): {
        /* Bounds-checked typed pointer arithmetic. Pops i32 index, pops
         * u64 ptr, traps if idx < 0 || idx >= dim_size, otherwise pushes
         * ptr + idx*elem_size. Used by nested-buf dynamic-arrow chains
         * to bounds-check each dimension at runtime. */
        uint16_t elem_size = vm__read_u16(vm);
        uint16_t dim_size  = vm__read_u16(vm);
        JaclVal idx_val;
        result = vm__pop(vm, &idx_val);
        if (result != VM_OK) return result;
        JaclVal ptr_val;
        result = vm__pop(vm, &ptr_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(ptr_val)) {
          result = vm__push(vm, ptr_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (jacl_is_error(idx_val)) {
          result = vm__push(vm, idx_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_u64(ptr_val)) {
          vm__set_error(vm, "buf arrow chain: expected pointer (u64) base");
          return VM_RUNTIME_ERROR;
        }
        int32_t n;
        if      (jacl_is_i32(idx_val)) n = jacl_as_i32(idx_val);
        else if (jacl_is_u32(idx_val)) n = (int32_t)jacl_as_u32(idx_val);
        else {
          vm__set_error(vm, "buf arrow chain: index must be i32");
          return VM_RUNTIME_ERROR;
        }
        if (n < 0 || (uint32_t)n >= (uint32_t)dim_size) {
          vm__set_error(vm,
              "buf arrow chain: index %d out of bounds for dimension size %u",
              (int)n, (unsigned)dim_size);
          return VM_RUNTIME_ERROR;
        }
        uint64_t base = jacl_as_u64(ptr_val);
        uint64_t out  = base + (uint64_t)n * (uint64_t)elem_size;
        result = vm__push(vm, jacl_u64(&vm->heap, out));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_INLINE_COPY_LOCAL): {
        /* Copy `width` consecutive frame slots onto TOS, marking the
         * destination slots as inline raw bytes. Used by by-value
         * [Buf N T] proc-param call sites: the caller's buf bytes are
         * memcpy'd into the operand-stack argument region, the callee
         * binds the param as a TYPE_BUF local backed by those slots.
         * Source slots are unmodified. See BUFFER_DESIGN.md Tier 2. */
        uint8_t src_slot = vm__read_byte(vm);
        uint8_t width    = vm__read_byte(vm);
        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_operand_overflow(vm, "inline buf copy");
          return VM_STACK_OVERFLOW;
        }
        memcpy(&vm->stack[vm->stack_top],
               &vm->stack[frame->stack_base + src_slot],
               (size_t)width * sizeof(JaclVal));
        for (uint32_t si = 0; si < width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        }
        vm->stack_top += width;
        DISPATCH();
      }

      CASE(OP_PTR_ADD_OFFSET): {
        /* Pop u64 ptr, push ptr+offset. Used for taking the address
         * of a nested struct field via [addr $p->inner->...]. */
        uint16_t byte_offset = vm__read_u16(vm);
        JaclVal ptr_val;
        result = vm__pop(vm, &ptr_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(ptr_val)) {
          result = vm__push(vm, ptr_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_u64(ptr_val)) {
          vm__set_error(vm, "ptr-add-offset: expected pointer (u64)");
          return VM_RUNTIME_ERROR;
        }
        uint64_t base = jacl_as_u64(ptr_val);
        result = vm__push(vm, jacl_u64(&vm->heap, base + byte_offset));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_PTR_LOAD_INLINE): {
        /* Pop a u64 pointer, push N inline JaclVal slots copied from
         * *ptr+offset (interpreted as inline struct bytes). N is
         * derived from the sub_type_idx's total_size. Mirrors
         * OP_HEAP_RECORD_GET_INLINE but reads from raw memory. */
        uint16_t byte_offset = vm__read_u16(vm);
        uint16_t sub_type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || sub_type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "ptr-load-inline: invalid sub-struct type %u", (unsigned)sub_type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sub_sdef = vm->struct_registry->defs[sub_type_idx];
        uint32_t sub_width = (sub_sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        JaclVal ptr_val;
        result = vm__pop(vm, &ptr_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(ptr_val)) {
          result = vm__push(vm, ptr_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_u64(ptr_val)) {
          vm__set_error(vm, "ptr-load-inline: expected pointer (u64)");
          return VM_RUNTIME_ERROR;
        }
        uint8_t* base = (uint8_t*)(uintptr_t)jacl_as_u64(ptr_val);
        if (!base) {
          vm__set_error(vm, "ptr-load-inline: null pointer dereference");
          return VM_RUNTIME_ERROR;
        }
        if (vm->stack_top + sub_width > VM_STACK_MAX) {
          vm__set_operand_overflow(vm, "ptr-load-inline");
          return VM_STACK_OVERFLOW;
        }
        memset(&vm->stack[vm->stack_top], 0, sub_width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], base + byte_offset, sub_sdef->total_size);
        for (uint32_t si = 0; si < sub_width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        }
        vm->stack_top += sub_width;
        DISPATCH();
      }

      CASE(OP_PTR_STORE_INLINE): {
        /* Pop N inline JaclVal slots, pop a u64 pointer, copy bytes
         * to *ptr+offset. Pushes the pointer back for chaining.
         * Mirrors OP_HEAP_RECORD_SET_INLINE but writes raw memory. */
        uint16_t byte_offset = vm__read_u16(vm);
        uint16_t sub_type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || sub_type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "ptr-store-inline: invalid sub-struct type %u", (unsigned)sub_type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sub_sdef = vm->struct_registry->defs[sub_type_idx];
        uint32_t sub_width = (sub_sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        if (vm->stack_top < sub_width + 1) {
          vm__set_error(vm, "ptr-store-inline: stack underflow");
          return VM_RUNTIME_ERROR;
        }
        uint8_t* inline_src = (uint8_t*)&vm->stack[vm->stack_top - sub_width];
        JaclVal ptr_val = vm->stack[vm->stack_top - sub_width - 1];
        if (jacl_is_error(ptr_val)) {
          for (uint32_t si = 0; si < sub_width; si++) {
            BITMAP_CLR(vm->inline_slot_bitmap, vm->stack_top - sub_width + si);
          }
          vm->stack_top -= sub_width;
          DISPATCH();  /* error stays on TOS where ptr was */
        }
        if (!jacl_is_u64(ptr_val)) {
          vm__set_error(vm, "ptr-store-inline: expected pointer (u64)");
          return VM_RUNTIME_ERROR;
        }
        uint8_t* base = (uint8_t*)(uintptr_t)jacl_as_u64(ptr_val);
        if (!base) {
          vm__set_error(vm, "ptr-store-inline: null pointer dereference");
          return VM_RUNTIME_ERROR;
        }
        memcpy(base + byte_offset, inline_src, sub_sdef->total_size);
        /* Pop the inline slots; the pointer stays on TOS. */
        for (uint32_t si = 0; si < sub_width; si++) {
          BITMAP_CLR(vm->inline_slot_bitmap, vm->stack_top - sub_width + si);
        }
        vm->stack_top -= sub_width;
        DISPATCH();
      }

      CASE(OP_PTR_DIFF): {
        /* Typed pointer subtraction: pop u64 b, pop u64 a, push
         * (i64)(a-b)/elem_size. elem_size is baked in from the
         * pointee's static type. */
        uint16_t elem_size = vm__read_u16(vm);
        if (elem_size == 0) {
          vm__set_error(vm, "ptr-diff: zero element size");
          return VM_RUNTIME_ERROR;
        }
        JaclVal b_val;
        result = vm__pop(vm, &b_val);
        if (result != VM_OK) return result;
        JaclVal a_val;
        result = vm__pop(vm, &a_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(a_val)) { result = vm__push(vm, a_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(b_val)) { result = vm__push(vm, b_val); if (result != VM_OK) return result; DISPATCH(); }
        if (!jacl_is_u64(a_val) || !jacl_is_u64(b_val)) {
          vm__set_error(vm, "ptr-diff: expected two pointers (u64)");
          return VM_RUNTIME_ERROR;
        }
        int64_t a = (int64_t)jacl_as_u64(a_val);
        int64_t b = (int64_t)jacl_as_u64(b_val);
        int64_t diff = (a - b) / (int64_t)elem_size;
        result = vm__push(vm, jacl_i64(&vm->heap, diff));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_HEAP_RECORD_GET_DYN): {
        uint16_t name_idx = vm__read_u16(vm);
        JaclVal struct_val;
        result = vm__pop(vm, &struct_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(struct_val)) {
          result = vm__push(vm, struct_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        /* Handle maps: field access becomes map lookup */
        if (jacl_is_map(struct_val)) {
          JaclVal name_val = frame->chunk->constants[name_idx];
          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(struct_val);
          if (jacl_map_has(map, name_val)) {
            result = vm__push(vm, jacl_map_get(map, name_val));
          } else {
            result = vm__push(vm, JACL_NIL);
          }
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_struct(struct_val)) {
          vm__set_error(vm, "field access requires struct or map");
          return VM_RUNTIME_ERROR;
        }
        HeapRecord* s = jacl_as_heap_record_ptr(struct_val);
        if (!vm->struct_registry || s->type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index");
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[s->type_idx];
        JaclVal name_val = frame->chunk->constants[name_idx];
        char fname[64]; uint32_t flen;
        flen = jacl_string_data(name_val, fname, sizeof(fname));
        uint32_t fi;
        for (fi = 0; fi < sdef->field_count; fi++) {
          if (sdef->fields[fi].name_len == flen &&
              memcmp(sdef->fields[fi].name, fname, flen) == 0) break;
        }
        if (fi == sdef->field_count) {
          vm__set_error(vm, "struct '%.*s' has no field '%.*s'",
                        (int)sdef->name_len, sdef->name, (int)flen, fname);
          return VM_RUNTIME_ERROR;
        }
        /* heap != NULL → boxed 64-bit types (always boxed for dyn context) */
        uint16_t foff = sdef->fields[fi].offset;
        JaclVal field_val = vm__heap_record_read_field(&vm->heap, s, foff,
                                                   (int)sdef->fields[fi].type);
        result = vm__push(vm, field_val);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_OPTIONAL_GET): {
        uint16_t name_idx = vm__read_u16(vm);
        JaclVal struct_val;
        result = vm__pop(vm, &struct_val);
        if (result != VM_OK) return result;
        /* Nil-safe: if nil, just push nil and skip field access */
        if (jacl_is_nil(struct_val)) {
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        /* Error propagation */
        if (jacl_is_error(struct_val)) {
          result = vm__push(vm, struct_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        /* Handle maps */
        if (jacl_is_map(struct_val)) {
          JaclVal name_val = frame->chunk->constants[name_idx];
          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(struct_val);
          if (jacl_map_has(map, name_val)) {
            result = vm__push(vm, jacl_map_get(map, name_val));
          } else {
            result = vm__push(vm, JACL_NIL);
          }
          if (result != VM_OK) return result;
          DISPATCH();
        }
        /* Structs must use -> not ?. */
        vm__set_error(vm, "?. cannot be used on %s; use -> for struct field access",
                      jacl_is_struct(struct_val) ? "structs" : "this type");
        return VM_RUNTIME_ERROR;
      }

      CASE(OP_HEAP_RECORD_SET_DYN): {
        uint16_t name_idx = vm__read_u16(vm);
        JaclVal new_val;
        result = vm__pop(vm, &new_val);
        if (result != VM_OK) return result;
        JaclVal struct_val;
        result = vm__pop(vm, &struct_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(struct_val)) {
          result = vm__push(vm, struct_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        /* Handle maps: field set becomes map-set (returns new map) */
        if (jacl_is_map(struct_val)) {
          JaclVal name_val = frame->chunk->constants[name_idx];
          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(struct_val);
          gc__current_heap = &vm->heap;
          jacl_map_node* new_map = jacl_map_set(map, name_val, new_val);
          result = vm__push(vm, jacl_map_ptr(new_map));
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_struct(struct_val)) {
          vm__set_error(vm, "field mutation requires struct or map");
          return VM_RUNTIME_ERROR;
        }
        HeapRecord* sd = jacl_as_heap_record_ptr(struct_val);
        if (!vm->struct_registry || sd->type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index");
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef2 = vm->struct_registry->defs[sd->type_idx];
        JaclVal name_val2 = frame->chunk->constants[name_idx];
        char fname2[64]; uint32_t flen2;
        flen2 = jacl_string_data(name_val2, fname2, sizeof(fname2));
        uint32_t fi2;
        for (fi2 = 0; fi2 < sdef2->field_count; fi2++) {
          if (sdef2->fields[fi2].name_len == flen2 &&
              memcmp(sdef2->fields[fi2].name, fname2, flen2) == 0) break;
        }
        if (fi2 == sdef2->field_count) {
          vm__set_error(vm, "struct '%.*s' has no field '%.*s'",
                        (int)sdef2->name_len, sdef2->name, (int)flen2, fname2);
          return VM_RUNTIME_ERROR;
        }
        if (!sdef2->fields[fi2].is_mutable) {
          vm__set_error(vm, "cannot mutate immutable field '%.*s' on struct '%.*s'",
                        (int)flen2, fname2, (int)sdef2->name_len, sdef2->name);
          return VM_RUNTIME_ERROR;
        }
        uint16_t foff2 = sdef2->fields[fi2].offset;
        vm__heap_record_write_field(sd, foff2, (int)sdef2->fields[fi2].type, new_val);
        result = vm__push(vm, struct_val);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_HEAP_RECORD_NEW): {
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t field_count = sdef->field_count;

        /* Allocate struct on GC heap */
        gc__current_heap = &vm->heap;
        HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                sizeof(HeapRecord) + sdef->total_size);
        s->type_idx = type_idx;
        s->total_size = sdef->total_size;
        memset(s->data, 0, sdef->total_size);

        /* Store each field value from the stack into struct data */
        for (uint32_t i = 0; i < field_count; i++) {
          JaclVal val = vm->stack[vm->stack_top - field_count + i];
          uint32_t off = sdef->fields[i].offset;
          switch (sdef->fields[i].type) {
            case TYPE_BOOL: {
              JACL_ASSERT_TAG(val, jacl_is_bool);
              uint8_t b = jacl_as_bool(val) ? 1 : 0;
              s->data[off] = b;
              break;
            }
            case TYPE_I32: {
              JACL_ASSERT_TAG(val, jacl_is_i32);
              int32_t n = jacl_as_i32(val);
              memcpy(s->data + off, &n, 4);
              break;
            }
            case TYPE_U32: {
              JACL_ASSERT_TAG(val, jacl_is_u32);
              uint32_t n = jacl_as_u32(val);
              memcpy(s->data + off, &n, 4);
              break;
            }
            case TYPE_F32: {
              JACL_ASSERT_TAG(val, jacl_is_f32);
              float f = jacl_as_f32(val);
              memcpy(s->data + off, &f, 4);
              break;
            }
            case TYPE_I64: {
              /* Raw i64 on stack (unboxed) */
              int64_t n = (int64_t)val;
              memcpy(s->data + off, &n, 8);
              break;
            }
            case TYPE_U64: {
              uint64_t n = val;
              memcpy(s->data + off, &n, 8);
              break;
            }
            case TYPE_F64: {
              double d;
              memcpy(&d, &val, 8);
              memcpy(s->data + off, &d, 8);
              break;
            }
            default: {
              /* str, vec, map, closure, dyn, struct — store full JaclVal */
              memcpy(s->data + off, &val, sizeof(JaclVal));
              break;
            }
          }
        }

        vm->stack_top -= field_count;
        result = vm__push(vm, jacl_heap_record_val(s));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_STRUCT_NEW_INLINE): {
        /* Construct an inline struct from field arguments on the stack.
           Primitive field args occupy 1 stack slot each (JaclVal).
           Nested struct field args occupy width inline slots (raw bytes).
           Total input slots = sum over fields. */
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t field_count = sdef->field_count;
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);

        /* Compute total input slots and remember each field's stack-byte
           offset within the input region. TYPE_BUF fields consume no
           input (auto-zero-init via scratch memset); their offset is
           UINT32_MAX as a sentinel so the per-field switch below knows
           to skip the input read. See BUFFER_DESIGN.md M4.3. */
        uint32_t total_input_slots = 0;
        uint32_t in_byte_offsets[256];
        for (uint32_t i = 0; i < field_count && i < 256; i++) {
          if (sdef->fields[i].type == TYPE_BUF) {
            in_byte_offsets[i] = UINT32_MAX;
            continue;
          }
          in_byte_offsets[i] = total_input_slots * sizeof(JaclVal);
          if (sdef->fields[i].type == TYPE_STRUCT) {
            StructTypeDef* sub = vm->struct_registry->defs[sdef->fields[i].struct_type_idx];
            total_input_slots += (sub->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
          } else {
            total_input_slots += 1;
          }
        }
        if (vm->stack_top < total_input_slots) {
          vm__set_error(vm, "struct_new_inline: stack underflow");
          return VM_RUNTIME_ERROR;
        }

        /* Build the struct in a scratch buffer (input and output overlap). */
        uint8_t scratch[VM_MAX_STRUCT_SLOTS * sizeof(JaclVal)];
        if (sdef->total_size > sizeof(scratch)) {
          vm__set_error(vm, "struct too large for inline construction");
          return VM_RUNTIME_ERROR;
        }
        memset(scratch, 0, sdef->total_size);
        uint8_t* in = (uint8_t*)&vm->stack[vm->stack_top - total_input_slots];

        for (uint32_t i = 0; i < field_count; i++) {
          if (in_byte_offsets[i] == UINT32_MAX) continue; /* buf field: scratch already zero */
          uint32_t off = sdef->fields[i].offset;
          uint8_t* src = in + in_byte_offsets[i];
          switch (sdef->fields[i].type) {
            case TYPE_BOOL: {
              JaclVal v; memcpy(&v, src, sizeof(JaclVal));
              JACL_ASSERT_TAG(v, jacl_is_bool);
              scratch[off] = jacl_as_bool(v) ? 1 : 0;
              break;
            }
            case TYPE_I32: {
              JaclVal v; memcpy(&v, src, sizeof(JaclVal));
              JACL_ASSERT_TAG(v, jacl_is_i32);
              int32_t n = jacl_as_i32(v);
              memcpy(scratch + off, &n, 4);
              break;
            }
            case TYPE_U32: {
              JaclVal v; memcpy(&v, src, sizeof(JaclVal));
              JACL_ASSERT_TAG(v, jacl_is_u32);
              uint32_t n = jacl_as_u32(v);
              memcpy(scratch + off, &n, 4);
              break;
            }
            case TYPE_F32: {
              JaclVal v; memcpy(&v, src, sizeof(JaclVal));
              JACL_ASSERT_TAG(v, jacl_is_f32);
              float f = jacl_as_f32(v);
              memcpy(scratch + off, &f, 4);
              break;
            }
            case TYPE_I64: {
              JaclVal v; memcpy(&v, src, sizeof(JaclVal));
              int64_t n = (int64_t)v;
              memcpy(scratch + off, &n, 8);
              break;
            }
            case TYPE_U64: {
              JaclVal v; memcpy(&v, src, sizeof(JaclVal));
              uint64_t n = v;
              memcpy(scratch + off, &n, 8);
              break;
            }
            case TYPE_F64: {
              JaclVal v; memcpy(&v, src, sizeof(JaclVal));
              double d; memcpy(&d, &v, 8);
              memcpy(scratch + off, &d, 8);
              break;
            }
            case TYPE_STRUCT: {
              /* Nested struct field: copy raw inline bytes from input. */
              memcpy(scratch + off, src, sdef->fields[i].size);
              break;
            }
            default: {
              JaclVal v; memcpy(&v, src, sizeof(JaclVal));
              memcpy(scratch + off, &v, sizeof(JaclVal));
              break;
            }
          }
        }

        /* Clear bitmap for popped input slots, decrement stack_top. */
        for (uint32_t si = 0; si < total_input_slots; si++) {
          BITMAP_CLR(vm->inline_slot_bitmap, vm->stack_top - total_input_slots + si);
        }
        vm->stack_top -= total_input_slots;

        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_operand_overflow(vm, "inline struct");
          return VM_STACK_OVERFLOW;
        }

        uint32_t base = vm->stack_top;
        memset(&vm->stack[base], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[base], scratch, sdef->total_size);
        vm__mark_struct_inline_slots(vm, base, sdef, width);
        vm->stack_top = base + width;
        DISPATCH();
      }

      CASE(OP_STRUCT_GET_INLINE): {
        /* Read a field from a stack-resident inline struct.
           Operands: uint8_t base_slot, uint16_t byte_offset, uint8_t field_type.
           The struct occupies consecutive stack slots starting at
           frame->stack_base + base_slot; we interpret them as raw bytes. */
        uint8_t base_slot = vm__read_byte(vm);
        uint16_t byte_offset = vm__read_u16(vm);
        uint8_t field_type = vm__read_byte(vm);
        uint8_t* struct_base = (uint8_t*)&vm->stack[frame->stack_base + base_slot];
        JaclVal field_val;
        bool pushed_inline = false;
        switch ((JaclType)field_type) {
          case TYPE_BOOL: { uint8_t b = struct_base[byte_offset]; field_val = jacl_bool(b); break; }
          case TYPE_I32: { int32_t n; memcpy(&n, struct_base + byte_offset, 4); field_val = jacl_i32(n); break; }
          case TYPE_U32: { uint32_t n; memcpy(&n, struct_base + byte_offset, 4); field_val = jacl_u32(n); break; }
          case TYPE_F32: { float f; memcpy(&f, struct_base + byte_offset, 4); field_val = jacl_f32(f); break; }
          case TYPE_I64: { int64_t n; memcpy(&n, struct_base + byte_offset, 8); field_val = (JaclVal)n; break; }
          case TYPE_U64: { uint64_t n; memcpy(&n, struct_base + byte_offset, 8); field_val = (JaclVal)n; break; }
          case TYPE_F64: { double d; memcpy(&d, struct_base + byte_offset, 8); memcpy(&field_val, &d, 8); break; }
          case TYPE_STRUCT: {
            /* Sub-struct field: read additional uint16_t type_idx; copy
               bytes onto stack as N inline slots (no heap allocation). */
            uint16_t sub_type_idx = vm__read_u16(vm);
            if (!vm->struct_registry || sub_type_idx >= vm->struct_registry->count) {
              vm__set_error(vm, "invalid struct type index %u for inline get", (unsigned)sub_type_idx);
              return VM_RUNTIME_ERROR;
            }
            StructTypeDef* sub_sdef = vm->struct_registry->defs[sub_type_idx];
            uint32_t sub_width = (sub_sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
            if (vm->stack_top + sub_width > VM_STACK_MAX) {
              vm__set_operand_overflow(vm, "struct_get_inline nested");
              return VM_STACK_OVERFLOW;
            }
            /* Note: struct_base is a pointer into vm->stack; it remains
               valid as long as the inline local at base_slot lives, which
               is the entire scope of this op. The destination slots are
               above stack_top and don't overlap with the source. */
            memset(&vm->stack[vm->stack_top], 0, sub_width * sizeof(JaclVal));
            memcpy(&vm->stack[vm->stack_top], struct_base + byte_offset, sub_sdef->total_size);
            vm__mark_struct_inline_slots(vm, vm->stack_top, sub_sdef, sub_width);
            vm->stack_top += sub_width;
            pushed_inline = true;
            break;
          }
          default: { memcpy(&field_val, struct_base + byte_offset, sizeof(JaclVal)); break; }
        }
        if (!pushed_inline) {
          result = vm__push(vm, field_val);
          if (result != VM_OK) return result;
        }
        DISPATCH();
      }

      CASE(OP_STRUCT_SET_INLINE): {
        /* Write a field to a stack-resident inline struct.
           Operands: uint8_t base_slot, uint16_t byte_offset, uint8_t field_type.
           Pops the new value from stack, writes to the struct's byte region,
           pushes nil (mutation is in-place on the stack slots). */
        uint8_t base_slot = vm__read_byte(vm);
        uint16_t byte_offset = vm__read_u16(vm);
        uint8_t field_type = vm__read_byte(vm);
        JaclVal new_val;
        result = vm__pop(vm, &new_val);
        if (result != VM_OK) return result;
        uint8_t* struct_base = (uint8_t*)&vm->stack[frame->stack_base + base_slot];
        switch ((JaclType)field_type) {
          case TYPE_BOOL: { JACL_ASSERT_TAG(new_val, jacl_is_bool); uint8_t b = jacl_as_bool(new_val) ? 1 : 0; struct_base[byte_offset] = b; break; }
          case TYPE_I32: { JACL_ASSERT_TAG(new_val, jacl_is_i32); int32_t n = jacl_as_i32(new_val); memcpy(struct_base + byte_offset, &n, 4); break; }
          case TYPE_U32: { JACL_ASSERT_TAG(new_val, jacl_is_u32); uint32_t n = jacl_as_u32(new_val); memcpy(struct_base + byte_offset, &n, 4); break; }
          case TYPE_F32: { JACL_ASSERT_TAG(new_val, jacl_is_f32); float f = jacl_as_f32(new_val); memcpy(struct_base + byte_offset, &f, 4); break; }
          case TYPE_I64: { int64_t n = (int64_t)new_val; memcpy(struct_base + byte_offset, &n, 8); break; }
          case TYPE_U64: { uint64_t n = new_val; memcpy(struct_base + byte_offset, &n, 8); break; }
          case TYPE_F64: { double d; memcpy(&d, &new_val, 8); memcpy(struct_base + byte_offset, &d, 8); break; }
          default: { memcpy(struct_base + byte_offset, &new_val, sizeof(JaclVal)); break; }
        }
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_STRUCT_STORE_INLINE): {
        /* De-materialize a heap HeapRecord into N consecutive stack slots.
           Operands: uint8_t base_slot, uint16_t type_idx.
           Reads heap struct pointer from stack[base_slot], writes raw bytes
           across N slots starting at base_slot, adjusts stack_top. */
        uint8_t base_slot = vm__read_byte(vm);
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for store_inline", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        uint32_t abs_base = frame->stack_base + base_slot;
        /* Read the heap struct pointer from the base slot */
        JaclVal heap_val = vm->stack[abs_base];
        if (!jacl_is_struct(heap_val)) {
          vm__set_error(vm, "OP_STRUCT_STORE_INLINE expects struct at slot %u", (unsigned)base_slot);
          return VM_RUNTIME_ERROR;
        }
        HeapRecord* src = jacl_as_heap_record_ptr(heap_val);
        /* Zero-fill N slots then copy raw struct bytes. For structs whose
         * layout includes ref-elem buf fields, the slot_ref_bitmap directs
         * us to leave those slots as GC-traceable (the zeroed scratch gives
         * them JACL_NIL, which the marker treats as no-op until a real
         * value is written). */
        memset(&vm->stack[abs_base], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[abs_base], src->data, sdef->total_size);
        vm->stack_top = abs_base + width;
        vm__mark_struct_inline_slots(vm, abs_base, sdef, width);
        DISPATCH();
      }

      CASE(OP_STRUCT_GET_UPVALUE): {
        /* US-008: Read a field from a closure-captured inline struct.
           Same as OP_STRUCT_GET_INLINE but base is frame->closure->upvalues[base_uv_slot]. */
        uint8_t base_uv_slot = vm__read_byte(vm);
        uint16_t byte_offset = vm__read_u16(vm);
        uint8_t field_type = vm__read_byte(vm);
        uint8_t* struct_base = (uint8_t*)&frame->closure->upvalues[base_uv_slot];
        JaclVal field_val;
        bool pushed_inline = false;
        switch ((JaclType)field_type) {
          case TYPE_BOOL: { uint8_t b = struct_base[byte_offset]; field_val = jacl_bool(b); break; }
          case TYPE_I32: { int32_t n; memcpy(&n, struct_base + byte_offset, 4); field_val = jacl_i32(n); break; }
          case TYPE_U32: { uint32_t n; memcpy(&n, struct_base + byte_offset, 4); field_val = jacl_u32(n); break; }
          case TYPE_F32: { float f; memcpy(&f, struct_base + byte_offset, 4); field_val = jacl_f32(f); break; }
          case TYPE_I64: { int64_t n; memcpy(&n, struct_base + byte_offset, 8); field_val = (JaclVal)n; break; }
          case TYPE_U64: { uint64_t n; memcpy(&n, struct_base + byte_offset, 8); field_val = (JaclVal)n; break; }
          case TYPE_F64: { double d; memcpy(&d, struct_base + byte_offset, 8); memcpy(&field_val, &d, 8); break; }
          case TYPE_STRUCT: {
            uint16_t sub_type_idx = vm__read_u16(vm);
            if (!vm->struct_registry || sub_type_idx >= vm->struct_registry->count) {
              vm__set_error(vm, "invalid struct type index %u for upvalue get", (unsigned)sub_type_idx);
              return VM_RUNTIME_ERROR;
            }
            StructTypeDef* sub_sdef = vm->struct_registry->defs[sub_type_idx];
            uint32_t sub_width = (sub_sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
            if (vm->stack_top + sub_width > VM_STACK_MAX) {
              vm__set_operand_overflow(vm, "struct_get_upvalue nested");
              return VM_STACK_OVERFLOW;
            }
            memset(&vm->stack[vm->stack_top], 0, sub_width * sizeof(JaclVal));
            memcpy(&vm->stack[vm->stack_top], struct_base + byte_offset, sub_sdef->total_size);
            vm__mark_struct_inline_slots(vm, vm->stack_top, sub_sdef, sub_width);
            vm->stack_top += sub_width;
            pushed_inline = true;
            break;
          }
          default: { memcpy(&field_val, struct_base + byte_offset, sizeof(JaclVal)); break; }
        }
        if (!pushed_inline) {
          result = vm__push(vm, field_val);
          if (result != VM_OK) return result;
        }
        DISPATCH();
      }

      CASE(OP_STRUCT_SET_UPVALUE): {
        /* US-008: Write a field to a closure-captured inline struct.
           Same as OP_STRUCT_SET_INLINE but base is frame->closure->upvalues[base_uv_slot]. */
        uint8_t base_uv_slot = vm__read_byte(vm);
        uint16_t byte_offset = vm__read_u16(vm);
        uint8_t field_type = vm__read_byte(vm);
        JaclVal new_val;
        result = vm__pop(vm, &new_val);
        if (result != VM_OK) return result;
        uint8_t* struct_base = (uint8_t*)&frame->closure->upvalues[base_uv_slot];
        switch ((JaclType)field_type) {
          case TYPE_BOOL: { JACL_ASSERT_TAG(new_val, jacl_is_bool); uint8_t b = jacl_as_bool(new_val) ? 1 : 0; struct_base[byte_offset] = b; break; }
          case TYPE_I32: { JACL_ASSERT_TAG(new_val, jacl_is_i32); int32_t n = jacl_as_i32(new_val); memcpy(struct_base + byte_offset, &n, 4); break; }
          case TYPE_U32: { JACL_ASSERT_TAG(new_val, jacl_is_u32); uint32_t n = jacl_as_u32(new_val); memcpy(struct_base + byte_offset, &n, 4); break; }
          case TYPE_F32: { JACL_ASSERT_TAG(new_val, jacl_is_f32); float f = jacl_as_f32(new_val); memcpy(struct_base + byte_offset, &f, 4); break; }
          case TYPE_I64: { int64_t n = (int64_t)new_val; memcpy(struct_base + byte_offset, &n, 8); break; }
          case TYPE_U64: { uint64_t n = new_val; memcpy(struct_base + byte_offset, &n, 8); break; }
          case TYPE_F64: { double d; memcpy(&d, &new_val, 8); memcpy(struct_base + byte_offset, &d, 8); break; }
          default: { memcpy(struct_base + byte_offset, &new_val, sizeof(JaclVal)); break; }
        }
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_LOAD_INLINE_LOCAL): {
        /* Copy N inline struct slots from a local to TOS, no heap alloc.
           Operands: uint8_t base_slot, uint16_t type_idx. */
        uint8_t base_slot = vm__read_byte(vm);
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for load_inline_local", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_operand_overflow(vm, "load_inline_local");
          return VM_STACK_OVERFLOW;
        }
        uint8_t* src = (uint8_t*)&vm->stack[frame->stack_base + base_slot];
        memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], src, sdef->total_size);
        for (uint32_t si = 0; si < width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        }
        vm->stack_top += width;
        DISPATCH();
      }

      CASE(OP_STRUCT_GET_INLINE_TOS): {
        /* Pop an inline struct from TOS, push field value.
           Operands: u16 type_idx, u16 byte_offset, u8 field_type
           [+ u16 sub_type_idx if field_type == TYPE_STRUCT]. */
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t byte_offset = vm__read_u16(vm);
        uint8_t field_type = vm__read_byte(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for get_inline_tos", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        JaclVal slots[VM_MAX_STRUCT_SLOTS];
        vm__pop_struct(vm, type_idx, slots);
        uint8_t* base = (uint8_t*)slots;
        JaclVal field_val;
        switch ((JaclType)field_type) {
          case TYPE_BOOL: field_val = jacl_bool(base[byte_offset]); break;
          case TYPE_I32: { int32_t n; memcpy(&n, base + byte_offset, 4); field_val = jacl_i32(n); break; }
          case TYPE_U32: { uint32_t n; memcpy(&n, base + byte_offset, 4); field_val = jacl_u32(n); break; }
          case TYPE_F32: { float f; memcpy(&f, base + byte_offset, 4); field_val = jacl_f32(f); break; }
          case TYPE_I64: { int64_t n; memcpy(&n, base + byte_offset, 8); field_val = (JaclVal)n; break; }
          case TYPE_U64: { uint64_t n; memcpy(&n, base + byte_offset, 8); field_val = (JaclVal)n; break; }
          case TYPE_F64: { double d; memcpy(&d, base + byte_offset, 8); memcpy(&field_val, &d, 8); break; }
          case TYPE_STRUCT: {
            uint16_t sub_type_idx = vm__read_u16(vm);
            if (sub_type_idx >= vm->struct_registry->count) {
              vm__set_error(vm, "invalid sub-struct type index %u", (unsigned)sub_type_idx);
              return VM_RUNTIME_ERROR;
            }
            StructTypeDef* sub = vm->struct_registry->defs[sub_type_idx];
            uint32_t sub_width = (sub->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
            if (vm->stack_top + sub_width > VM_STACK_MAX) {
              vm__set_operand_overflow(vm, "get_inline_tos nested");
              return VM_STACK_OVERFLOW;
            }
            memset(&vm->stack[vm->stack_top], 0, sub_width * sizeof(JaclVal));
            memcpy(&vm->stack[vm->stack_top], base + byte_offset, sub->total_size);
            for (uint32_t si = 0; si < sub_width; si++) {
              BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
            }
            vm->stack_top += sub_width;
            break;  /* nothing else to push */
          }
          default: { memcpy(&field_val, base + byte_offset, sizeof(JaclVal)); break; }
        }
        if ((JaclType)field_type != TYPE_STRUCT) {
          result = vm__push(vm, field_val);
          if (result != VM_OK) return result;
        }
        (void)sdef;
        DISPATCH();
      }

      CASE(OP_STRUCT_EQ_TOS): {
        /* Pop two structs from TOS (rhs first, then lhs), memcmp, push bool.
           Each can be inline or heap — vm__pop_struct dispatches.
           Operand: uint16_t type_idx. */
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for struct_eq", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        JaclVal rhs[VM_MAX_STRUCT_SLOTS];
        JaclVal lhs[VM_MAX_STRUCT_SLOTS];
        vm__pop_struct(vm, type_idx, rhs);
        vm__pop_struct(vm, type_idx, lhs);
        bool eq = (memcmp(lhs, rhs, sdef->total_size) == 0);
        result = vm__push(vm, jacl_bool(eq));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_PRINT_STRUCT): {
        /* Pop N inline struct slots, format Name{f: v, ...}, print + newline.
           Operand: uint16_t type_idx. */
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for print_struct", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint8_t scratch[VM_MAX_STRUCT_SLOTS * sizeof(JaclVal)];
        if (sdef->total_size > sizeof(scratch)) {
          vm__set_error(vm, "print: struct too large");
          return VM_RUNTIME_ERROR;
        }
        JaclVal slots[VM_MAX_STRUCT_SLOTS];
        vm__pop_struct(vm, type_idx, slots);
        memcpy(scratch, slots, sdef->total_size);
        VMFormatBuf fmt;
        vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
        vm__fmt_struct_bytes(&fmt, sdef, scratch);
        vm__fmt_append(&fmt, "\n", 1);
        vm->print_fn(fmt.data, fmt.len, vm->print_ctx);
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_PRINT_PTR): {
        /* Pop a tagged u64 (the [Ptr T] value) and format as
         * "Ptr<Name>(0xADDR)". The pointee idx encodes either a
         * scalar JaclType (via JACL_SCALAR_TYPE_IDX) or a struct
         * registry index. Compiler falls back to OP_PRINT when
         * the pointee is unknown, so this handler always has a
         * resolvable name. */
        uint16_t pointee_idx = vm__read_u16(vm);
        JaclVal val;
        result = vm__pop(vm, &val);
        if (result != VM_OK) return result;
        VMFormatBuf fmt;
        vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
        vm__fmt_append(&fmt, "Ptr<", 4);
        if (JACL_IS_SCALAR_TYPE_IDX(pointee_idx)) {
          JaclType t = (JaclType)JACL_TYPE_IDX_TO_SCALAR(pointee_idx);
          const char* nm = type_name(t);
          vm__fmt_append(&fmt, nm, (uint32_t)strlen(nm));
        } else if (vm->struct_registry &&
                   pointee_idx < vm->struct_registry->count &&
                   vm->struct_registry->defs[pointee_idx]) {
          StructTypeDef* sdef = vm->struct_registry->defs[pointee_idx];
          vm__fmt_append(&fmt, sdef->name, sdef->name_len);
        } else {
          vm__fmt_append(&fmt, "?", 1);
        }
        char addr_buf[40];
        uint64_t addr = jacl_is_u64(val) ? jacl_as_u64(val) : 0;
        int n = snprintf(addr_buf, sizeof(addr_buf),
                         ">(0x%" PRIx64 ")", addr);
        vm__fmt_append(&fmt, addr_buf, (uint32_t)n);
        vm__fmt_append(&fmt, "\n", 1);
        vm->print_fn(fmt.data, fmt.len, vm->print_ctx);
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      /* --- File I/O builtins ---
       * read-file:   [read-file PATH]            → string contents (or error)
       * write-file:  [write-file CONTENT PATH]   → nil (or error)
       * append-file: [append-file CONTENT PATH]  → nil (or error)
       *
       * Errors surface as JACL error values (try/catch-able), not VM
       * runtime errors, so callers can `[read-file p] | catch {fallback}`.
       * VM_RUNTIME_ERROR is reserved for type-mismatch / malformed args. */

      CASE(OP_READ_FILE): {
        JaclVal path_val;
        result = vm__pop(vm, &path_val);
        if (result != VM_OK) return result;
        if (!jacl_is_string(path_val)) {
          vm__set_error(vm, "read-file: path must be a string, got %s",
                       vm__type_name(path_val));
          return VM_RUNTIME_ERROR;
        }
        char path_buf[PATH_MAX + 1];
        uint32_t path_len = jacl_string_byte_len(path_val);
        if (path_len > PATH_MAX) {
          vm__set_error(vm, "read-file: path too long (%u bytes)", path_len);
          return VM_RUNTIME_ERROR;
        }
        jacl_string_data(path_val, path_buf, sizeof(path_buf));
        path_buf[path_len] = '\0';

        gc__current_heap = &vm->heap;
        int fd = open(path_buf, O_RDONLY);
        if (fd < 0) {
          char msg[256];
          snprintf(msg, sizeof(msg), "read-file: %s: %s",
                   path_buf, strerror(errno));
          JaclVal err = jacl_string_new(&vm->heap, vm->intern_table,
                                        msg, strlen(msg));
          if (err == JACL_NIL) err = jacl_inline_string("read error", 10);
          result = vm__push(vm, jacl_set_error(err));
          if (result != VM_OK) return result;
          DISPATCH();
        }
        /* Slurp into a growing buffer. Avoid stat() so pipes/devices work. */
        size_t cap = 4096;
        size_t len = 0;
        char* buf = (char*)malloc(cap);
        if (!buf) { close(fd); vm__set_error(vm, "read-file: out of memory"); return VM_RUNTIME_ERROR; }
        for (;;) {
          if (len == cap) {
            size_t new_cap = cap * 2;
            char* new_buf = (char*)realloc(buf, new_cap);
            if (!new_buf) { free(buf); close(fd); vm__set_error(vm, "read-file: out of memory"); return VM_RUNTIME_ERROR; }
            buf = new_buf; cap = new_cap;
          }
          ssize_t n = read(fd, buf + len, cap - len);
          if (n < 0) {
            if (errno == EINTR) continue;
            char msg[256];
            snprintf(msg, sizeof(msg), "read-file: %s: %s",
                     path_buf, strerror(errno));
            free(buf); close(fd);
            JaclVal err = jacl_string_new(&vm->heap, vm->intern_table,
                                          msg, strlen(msg));
            if (err == JACL_NIL) err = jacl_inline_string("read error", 10);
            result = vm__push(vm, jacl_set_error(err));
            if (result != VM_OK) return result;
            DISPATCH();
          }
          if (n == 0) break;
          len += (size_t)n;
        }
        close(fd);

        JaclVal s = jacl_string_new(&vm->heap, vm->intern_table, buf, len);
        free(buf);
        if (s == JACL_NIL) {
          /* UTF-8 validation failed — surface as error value */
          const char* m = "read-file: invalid UTF-8 in file contents";
          JaclVal err = jacl_string_new(&vm->heap, vm->intern_table,
                                        m, strlen(m));
          if (err == JACL_NIL) err = jacl_inline_string("bad utf8", 8);
          result = vm__push(vm, jacl_set_error(err));
          if (result != VM_OK) return result;
          DISPATCH();
        }
        result = vm__push(vm, s);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_WRITE_FILE):
      CASE(OP_APPEND_FILE): {
        bool append = (instruction == OP_APPEND_FILE);
        const char* op_name = append ? "append-file" : "write-file";

        JaclVal path_val, content_val;
        result = vm__pop(vm, &path_val);
        if (result != VM_OK) return result;
        result = vm__pop(vm, &content_val);
        if (result != VM_OK) return result;

        if (!jacl_is_string(path_val)) {
          vm__set_error(vm, "%s: path must be a string, got %s",
                       op_name, vm__type_name(path_val));
          return VM_RUNTIME_ERROR;
        }
        char path_buf[PATH_MAX + 1];
        uint32_t path_len = jacl_string_byte_len(path_val);
        if (path_len > PATH_MAX) {
          vm__set_error(vm, "%s: path too long (%u bytes)", op_name, path_len);
          return VM_RUNTIME_ERROR;
        }
        jacl_string_data(path_val, path_buf, sizeof(path_buf));
        path_buf[path_len] = '\0';

        /* Collect content via the shared stdin collector — accepts both
         * strings and streams, joining stream elements with newlines just
         * like exec stdin. */
        char* content_buf = NULL;
        size_t content_len = 0;
        int cr = vm__exec_collect_stdin(vm, content_val, &content_buf, &content_len);
        if (cr == -1) return VM_RUNTIME_ERROR;
        if (cr == -2) {
          /* Error from upstream stream was pushed; propagate it. */
          frame = &vm->frames[vm->frame_count - 1];
          DISPATCH();
        }

        gc__current_heap = &vm->heap;
        int flags_open = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
        int fd = open(path_buf, flags_open, 0644);
        if (fd < 0) {
          char msg[256];
          snprintf(msg, sizeof(msg), "%s: %s: %s",
                   op_name, path_buf, strerror(errno));
          JaclVal err = jacl_string_new(&vm->heap, vm->intern_table,
                                        msg, strlen(msg));
          if (err == JACL_NIL) err = jacl_inline_string("write err", 9);
          result = vm__push(vm, jacl_set_error(err));
          if (result != VM_OK) return result;
          DISPATCH();
        }
        size_t written = 0;
        while (written < content_len) {
          ssize_t n = write(fd, content_buf + written, content_len - written);
          if (n < 0) {
            if (errno == EINTR) continue;
            char msg[256];
            snprintf(msg, sizeof(msg), "%s: %s: %s",
                     op_name, path_buf, strerror(errno));
            close(fd);
            JaclVal err = jacl_string_new(&vm->heap, vm->intern_table,
                                          msg, strlen(msg));
            if (err == JACL_NIL) err = jacl_inline_string("write err", 9);
            result = vm__push(vm, jacl_set_error(err));
            if (result != VM_OK) return result;
            DISPATCH();
          }
          written += (size_t)n;
        }
        close(fd);
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_LOAD_INLINE_UPVALUE): {
        /* Copy N inline struct slots from a closure upvalue to TOS, no heap alloc.
           Operands: uint8_t base_uv_slot, uint16_t type_idx. */
        uint8_t base_uv_slot = vm__read_byte(vm);
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for load_inline_upvalue", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_operand_overflow(vm, "load_inline_upvalue");
          return VM_STACK_OVERFLOW;
        }
        uint8_t* src = (uint8_t*)&frame->closure->upvalues[base_uv_slot];
        memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], src, sdef->total_size);
        for (uint32_t si = 0; si < width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        }
        vm->stack_top += width;
        DISPATCH();
      }

      CASE(OP_STRUCT_EXPAND): {
        /* Phase 5a: Pop heap HeapRecord from stack top, push raw bytes as N inline slots.
           Operand: uint16_t type_idx. */
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for expand", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        /* Pop the heap struct pointer */
        JaclVal heap_val = vm->stack[--vm->stack_top];
        if (!jacl_is_struct(heap_val)) {
          vm__set_error(vm, "OP_STRUCT_EXPAND expects struct value");
          return VM_RUNTIME_ERROR;
        }
        HeapRecord* src = jacl_as_heap_record_ptr(heap_val);
        /* Push N inline slots (zero-fill then copy raw bytes) */
        memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], src->data, sdef->total_size);
        for (uint32_t si = 0; si < width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        }
        vm->stack_top += width;
        DISPATCH();
      }

      CASE(OP_STRUCT_EQ_INLINE): {
        /* US-013: Compare two stack-resident inline structs via memcmp.
           Operands: uint8_t base_a, uint8_t base_b, uint16_t total_size.
           Pushes bool result. */
        uint8_t base_a = vm__read_byte(vm);
        uint8_t base_b = vm__read_byte(vm);
        uint16_t total_size = vm__read_u16(vm);
        uint8_t* bytes_a = (uint8_t*)&vm->stack[frame->stack_base + base_a];
        uint8_t* bytes_b = (uint8_t*)&vm->stack[frame->stack_base + base_b];
        bool eq = (memcmp(bytes_a, bytes_b, total_size) == 0);
        result = vm__push(vm, jacl_bool(eq));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_STRUCT_HASH_INLINE): {
        /* US-013: Hash a stack-resident inline struct's raw bytes.
           Operands: uint8_t base_slot, uint16_t total_size, uint16_t type_idx.
           Pushes i32 hash result. */
        uint8_t base_slot = vm__read_byte(vm);
        uint16_t total_size = vm__read_u16(vm);
        uint16_t type_idx = vm__read_u16(vm);
        uint8_t* bytes = (uint8_t*)&vm->stack[frame->stack_base + base_slot];
        uint32_t h = (uint32_t)type_idx * 0x9E3779B9u;
        for (uint16_t i = 0; i < total_size; i++) {
          h = h * 31 + bytes[i];
        }
        result = vm__push(vm, jacl_i32((int32_t)h));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_HASH): {
        /* US-013: Generic hash — pop any value, push i32 hash. */
        JaclVal val;
        result = vm__pop(vm, &val);
        if (result != VM_OK) return result;
        uint32_t h = jacl_val_hash(val);
        result = vm__push(vm, jacl_i32((int32_t)h));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_SPREAD): {
        JaclVal spread_val;
        result = vm__pop(vm, &spread_val);
        if (result != VM_OK) return result;

        uint32_t len = 0;

        if (jacl_is_vector(spread_val)) {
          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(spread_val);
          len = jacl_vec_count(vec);
          if (vm->stack_top + len >= VM_STACK_MAX) {
            vm__set_error(vm, "spread exceeds stack capacity");
            return VM_RUNTIME_ERROR;
          }
          for (uint32_t i = 0; i < len; i++) {
            jacl_vec_get_result gr = jacl_vec_get(vec, i);
            result = vm__push(vm, gr.found ? gr.value : JACL_NIL);
            if (result != VM_OK) return result;
          }
        } else if (jacl_is_stream(spread_val)) {
          /* Eagerly consume stream, collect into vector, then push elements */
          JaclStream* stream = jacl_as_stream(spread_val);

          /* Struct elements are inline value bytes — they cannot be spread
           * into dyn slots (NOT_IMPLEMENTED.md §4.1b). */
          if (vm__elem_idx_is_struct(stream->elem_idx)) {
            vm__set_error(vm, "struct-element streams require a typed "
                              "consumer (for-loop); spread is not yet "
                              "supported");
            return VM_RUNTIME_ERROR;
          }

          if (stream->kind != STREAM_KIND_GENERATOR) {
            /* Derived stream: use pull helper */
            gc__current_heap = &vm->heap;
            jacl_vec_root* tmp_vec = jacl_vec_empty();
            while (stream->state != STREAM_EXHAUSTED) {
              JaclVal elem;
              StreamPullResult pr = vm__pull_stream_dyn(vm, spread_val, &elem);
              if (pr == STREAM_PULL_ERROR) return VM_RUNTIME_ERROR;
              if (pr == STREAM_PULL_EXHAUSTED) break;
              gc__current_heap = &vm->heap;
              tmp_vec = jacl_vec_push_back(tmp_vec, elem);
            }
            frame = &vm->frames[vm->frame_count - 1];
            len = jacl_vec_count(tmp_vec);
            if (vm->stack_top + len >= VM_STACK_MAX) {
              vm__set_error(vm, "spread exceeds stack capacity");
              return VM_RUNTIME_ERROR;
            }
            for (uint32_t i = 0; i < len; i++) {
              jacl_vec_get_result gr = jacl_vec_get(tmp_vec, i);
              result = vm__push(vm, gr.found ? gr.value : JACL_NIL);
              if (result != VM_OK) return result;
            }
          } else {
            /* Generator stream: pull all values */
            if (jacl_is_nil(stream->state_machine)) {
              vm__set_error(vm, "CPS generator not supported (use state machine path)");
              return VM_RUNTIME_ERROR;
            }
            gc__current_heap = &vm->heap;
            jacl_vec_root* tmp_vec = jacl_vec_empty();

            while (stream->state != STREAM_EXHAUSTED) {
              uint32_t caller_stack_top = vm->stack_top;
              uint32_t caller_frame_count = vm->frame_count;
              uint8_t* caller_ip = vm->ip;
              BytecodeChunk* caller_chunk = vm->chunk;

              /* SM generator: call sm_closure(state_obj, nil) each time */
              JACL_ASSERT_TAG(stream->state_machine, jacl_is_state_machine);
              JaclStateMachine* sm = jacl_as_state_machine(stream->state_machine);
              JACL_ASSERT_TAG(sm->sm_closure, jacl_is_closure);
              JaclClosure* sm_cl = jacl_as_closure(sm->sm_closure);

              /* Check frame capacity BEFORE pushing args. See AUDIT.md
               * §D.2 — push-then-check leaked 3 slots per overflow. */
              if (vm->frame_count >= VM_FRAMES_MAX) {
                vm__set_frame_overflow(vm);
                return VM_RUNTIME_ERROR;
              }

              result = vm__push(vm, sm->sm_closure);
              if (result != VM_OK) return result;
              result = vm__push(vm, stream->state_machine);
              if (result != VM_OK) return result;
              result = vm__push(vm, JACL_NIL);
              if (result != VM_OK) return result;
              CallFrame* new_frame = &vm->frames[vm->frame_count++];
              new_frame->closure    = sm_cl;
              new_frame->return_ip  = NULL;
              new_frame->stack_base = vm->stack_top - 2;
              new_frame->chunk      = &sm_cl->chunk;
              vm->ip    = sm_cl->chunk.code;
              vm->chunk = &sm_cl->chunk;

              VMResult inner = vm__run(vm, caller_frame_count);

              if (inner == VM_YIELD) {
                stream->state = STREAM_CONSUMED;
                vm__slot_set(vm, &stream->cached_value, vm->yield_value);
                vm->stack_top   = caller_stack_top;
                vm->frame_count = caller_frame_count;
                vm->ip    = caller_ip;
                vm->chunk = caller_chunk;
                frame = &vm->frames[vm->frame_count - 1];
                gc__current_heap = &vm->heap;
                tmp_vec = jacl_vec_push_back(tmp_vec, vm->yield_value);
              } else if (inner == VM_OK) {
                stream->state = STREAM_EXHAUSTED;
                vm__slot_set(vm, &stream->next_fn, JACL_NIL);
                vm__slot_set(vm, &stream->cached_value, JACL_NIL);
                vm->stack_top   = caller_stack_top;
                vm->frame_count = caller_frame_count;
                vm->ip    = caller_ip;
                vm->chunk = caller_chunk;
                frame = &vm->frames[vm->frame_count - 1];
              } else {
                stream->state = STREAM_ERROR;
                vm__slot_set(vm, &stream->next_fn, JACL_NIL);
                return inner;
              }
            }

            len = jacl_vec_count(tmp_vec);
            if (vm->stack_top + len >= VM_STACK_MAX) {
              vm__set_error(vm, "spread exceeds stack capacity");
              return VM_RUNTIME_ERROR;
            }
            for (uint32_t i = 0; i < len; i++) {
              jacl_vec_get_result gr = jacl_vec_get(tmp_vec, i);
              result = vm__push(vm, gr.found ? gr.value : JACL_NIL);
              if (result != VM_OK) return result;
            }
          }
        } else {
          vm__set_error(vm, "spread requires a vector or stream, got %s",
                       vm__type_name(spread_val));
          return VM_RUNTIME_ERROR;
        }

        /* Save count in side buffer */
        if (vm->spread_count_top >= 32) {
          vm__set_error(vm, "too many nested spread operations");
          return VM_RUNTIME_ERROR;
        }
        vm->spread_counts[vm->spread_count_top++] = len;
        DISPATCH();
      }

      CASE(OP_CALL_SPREAD): {
        uint8_t fixed_args = vm__read_byte(vm);
        uint8_t num_spreads = vm__read_byte(vm);
        /* Compute total args */
        uint32_t total_args = fixed_args;
        for (uint8_t i = 0; i < num_spreads; i++) {
          if (vm->spread_count_top == 0) {
            vm__set_error(vm, "spread count underflow");
            return VM_RUNTIME_ERROR;
          }
          total_args += vm->spread_counts[--vm->spread_count_top];
        }
        /* Locate callee */
        JaclVal callee = vm->stack[vm->stack_top - total_args - 1];

        if (jacl_is_native_fn(callee)) {
          uint32_t fn_idx = jacl_as_native_fn_index(callee);
          if (fn_idx >= vm->native_fn_count || !vm->call_native) {
            vm__set_error(vm, "invalid native function index %u", fn_idx);
            return VM_RUNTIME_ERROR;
          }
          int8_t arity = vm->native_fn_arities[fn_idx];
          if (arity >= 0 && total_args != (uint32_t)arity) {
            vm__set_error(vm, "expected %d arguments but got %d",
                         (int)arity, (int)total_args);
            return VM_RUNTIME_ERROR;
          }
          JaclVal* args = &vm->stack[vm->stack_top - total_args];
          JaclVal ret = vm->call_native(vm->native_fn_ctx, fn_idx,
                                         args, (int)total_args);
          vm->stack_top -= (total_args + 1);
          result = vm__push(vm, ret);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        if (!jacl_is_closure(callee)) {
          vm__set_error(vm, "cannot call %s value", vm__type_name(callee));
          return VM_RUNTIME_ERROR;
        }

        JaclClosure* closure = jacl_as_closure(callee);
        if (closure->variadic) {
          if (total_args < closure->min_args) {
            vm__set_error(vm, "expected at least %d arguments but got %d",
                         (int)closure->min_args, (int)total_args);
            return VM_RUNTIME_ERROR;
          }
        } else if (total_args != closure->param_count) {
          vm__set_error(vm, "expected %d arguments but got %d",
                       (int)closure->param_count, (int)total_args);
          return VM_RUNTIME_ERROR;
        }
        if (vm->frame_count >= VM_FRAMES_MAX) {
          vm__set_frame_overflow(vm);
          return VM_RUNTIME_ERROR;
        }

        CallFrame* new_frame = &vm->frames[vm->frame_count++];
        new_frame->closure    = closure;
        new_frame->return_ip  = vm->ip;
        new_frame->stack_base = vm->stack_top - total_args;
        new_frame->chunk      = &closure->chunk;
        frame     = new_frame;
        vm->ip    = frame->chunk->code;
        vm->chunk = frame->chunk;
        DISPATCH();
      }

      CASE(OP_FOLD_SPREAD): {
        uint8_t op_id = vm__read_byte(vm);
        uint8_t fixed_args = vm__read_byte(vm);
        uint8_t num_spreads = vm__read_byte(vm);
        /* Compute total args */
        uint32_t total_args = fixed_args;
        for (uint8_t i = 0; i < num_spreads; i++) {
          if (vm->spread_count_top == 0) {
            vm__set_error(vm, "spread count underflow");
            return VM_RUNTIME_ERROR;
          }
          total_args += vm->spread_counts[--vm->spread_count_top];
        }
        if (total_args < 1) {
          vm__set_error(vm, "fold requires at least 1 argument");
          return VM_RUNTIME_ERROR;
        }
        /* Fold values on stack left-to-right */
        uint32_t base = vm->stack_top - total_args;
        JaclVal acc = vm->stack[base];
        for (uint32_t i = 1; i < total_args; i++) {
          JaclVal val = vm->stack[base + i];
          JaclVal res;
          if (jacl_is_i32(acc) && jacl_is_i32(val)) {
            switch (op_id) {
              case 0: res = jacl_add_i32(acc, val); break;
              case 1: res = jacl_sub_i32(acc, val); break;
              case 2: res = jacl_mul_i32(acc, val); break;
              case 3: res = jacl_div_i32(acc, val); break;
              default: vm__set_error(vm, "unknown fold op"); return VM_RUNTIME_ERROR;
            }
          } else if (jacl_is_f32(acc) && jacl_is_f32(val)) {
            switch (op_id) {
              case 0: res = jacl_add_f32(acc, val); break;
              case 1: res = jacl_sub_f32(acc, val); break;
              case 2: res = jacl_mul_f32(acc, val); break;
              case 3: res = jacl_div_f32(acc, val); break;
              default: vm__set_error(vm, "unknown fold op"); return VM_RUNTIME_ERROR;
            }
          } else if (jacl_is_u32(acc) && jacl_is_u32(val)) {
            switch (op_id) {
              case 0: res = jacl_u32_add(acc, val); break;
              case 1: res = jacl_u32_sub(acc, val); break;
              case 2: res = jacl_u32_mul(acc, val); break;
              case 3: res = jacl_u32_div(acc, val); break;
              default: vm__set_error(vm, "unknown fold op"); return VM_RUNTIME_ERROR;
            }
          } else {
            const char* op_names[] = { "+", "-", "*", "/" };
            vm__set_error(vm,
              "type error in '%s': expected matching numeric types, got %s and %s",
              op_names[op_id < 4 ? op_id : 0], vm__type_name(acc), vm__type_name(val));
            return VM_RUNTIME_ERROR;
          }
          if (jacl_is_error(res)) {
            vm__capture_trace(vm);
          }
          acc = res;
        }
        vm->stack_top = base;
        result = vm__push(vm, acc);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_YIELD): {
        /* CPS yield removed — all generators use OP_YIELD_SM now */
        vm__set_error(vm, "CPS yield not supported (use state machine path)");
        return VM_RUNTIME_ERROR;
      }

      CASE(OP_STREAM_NEXT): {
        /* stream_next: pulls next value from a generator stream.
           Supports both CPS-based and state-machine-based generators. */
        JaclVal stream_val;
        result = vm__pop(vm, &stream_val);
        if (result != VM_OK) return result;
        if (!jacl_is_stream(stream_val)) {
          vm__set_error(vm, "stream_next requires a stream, got %s",
                       vm__type_name(stream_val));
          return VM_RUNTIME_ERROR;
        }
        JaclStream* stream = jacl_as_stream(stream_val);
        if (stream->state == STREAM_EXHAUSTED) {
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        /* Struct elements: only the typed for-loop (OP_STREAM_NEXT_INLINE)
         * may pull them — this dyn-returning path cannot (§4.1b). */
        if (vm__elem_idx_is_struct(stream->elem_idx)) {
          vm__set_error(vm, "struct-element streams require a typed consumer "
                            "(for-loop); stream_next is not yet supported");
          return VM_RUNTIME_ERROR;
        }

        /* Derived streams (filter, etc.) use the unified helper */
        if (stream->kind != STREAM_KIND_GENERATOR) {
          JaclVal pulled;
          StreamPullResult pr = vm__pull_stream_dyn(vm, stream_val, &pulled);
          if (pr == STREAM_PULL_ERROR) return VM_RUNTIME_ERROR;
          result = vm__push(vm, pulled);
          if (result != VM_OK) return result;
          frame = &vm->frames[vm->frame_count - 1];
          DISPATCH();
        }

        /* Save caller context */
        uint32_t caller_stack_top = vm->stack_top;
        uint32_t caller_frame_count = vm->frame_count;
        uint8_t* caller_ip = vm->ip;
        BytecodeChunk* caller_chunk = vm->chunk;

        /* --- State machine generator path --- */
        if (!jacl_is_nil(stream->state_machine)) {
          JACL_ASSERT_TAG(stream->state_machine, jacl_is_state_machine);
          JaclStateMachine* sm = jacl_as_state_machine(stream->state_machine);
          JACL_ASSERT_TAG(sm->sm_closure, jacl_is_closure);
          JaclClosure* sm_cl = jacl_as_closure(sm->sm_closure);

          /* SM generators always call the same way: sm_closure(state_obj, nil) */
          result = vm__push(vm, sm->sm_closure);
          if (result != VM_OK) return result;
          result = vm__push(vm, stream->state_machine);
          if (result != VM_OK) return result;
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;

          if (vm->frame_count >= VM_FRAMES_MAX) {
            vm__set_frame_overflow(vm);
            return VM_RUNTIME_ERROR;
          }
          CallFrame* new_frame = &vm->frames[vm->frame_count++];
          new_frame->closure    = sm_cl;
          new_frame->return_ip  = NULL;
          new_frame->stack_base = vm->stack_top - 2; /* 2 params: state_obj + resume_value */
          new_frame->chunk      = &sm_cl->chunk;
          vm->ip    = sm_cl->chunk.code;
          vm->chunk = &sm_cl->chunk;

          VMResult inner = vm__run(vm, caller_frame_count);

          if (inner == VM_YIELD) {
            stream->state = STREAM_CONSUMED;
            vm__slot_set(vm, &stream->cached_value, vm->yield_value);
            vm->stack_top   = caller_stack_top;
            vm->frame_count = caller_frame_count;
            vm->ip    = caller_ip;
            vm->chunk = caller_chunk;
            frame = &vm->frames[vm->frame_count - 1];
            result = vm__push(vm, vm->yield_value);
            if (result != VM_OK) return result;
            DISPATCH();
          } else if (inner == VM_OK) {
            /* Check if SM function returned an error value (error propagated
               through OP_CHECK_ERROR frame unwinding returns VM_OK). */
            JaclVal sm_ret = JACL_NIL;
            if (vm->stack_top > caller_stack_top) {
              sm_ret = vm->stack[vm->stack_top - 1];
            }
            if (jacl_is_error(sm_ret)) {
              stream->state = STREAM_ERROR;
              vm->stack_top   = caller_stack_top;
              vm->frame_count = caller_frame_count;
              vm->ip    = caller_ip;
              vm->chunk = caller_chunk;
              frame = &vm->frames[vm->frame_count - 1];
              result = vm__push(vm, sm_ret);
              if (result != VM_OK) return result;
              DISPATCH();
            }
            stream->state = STREAM_EXHAUSTED;
            vm__slot_set(vm, &stream->cached_value, JACL_NIL);
            vm->stack_top   = caller_stack_top;
            vm->frame_count = caller_frame_count;
            vm->ip    = caller_ip;
            vm->chunk = caller_chunk;
            frame = &vm->frames[vm->frame_count - 1];
            result = vm__push(vm, JACL_NIL);
            if (result != VM_OK) return result;
            DISPATCH();
          } else {
            stream->state = STREAM_ERROR;
            return inner;
          }
        }

        /* All generators use state machine path (CPS removed) */
        vm__set_error(vm, "generator stream missing state machine object");
        return VM_RUNTIME_ERROR;
      }

      CASE(OP_COLLECT): {
        /* collect: materialize a stream into a vector, or identity on vectors */
        JaclVal coll_val;
        result = vm__pop(vm, &coll_val);
        if (result != VM_OK) return result;

        if (jacl_is_vector(coll_val)) {
          /* Identity: return vector unchanged */
          result = vm__push(vm, coll_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        if (!jacl_is_stream(coll_val)) {
          vm__set_error(vm, "collect requires a stream or vector, got %s",
                       vm__type_name(coll_val));
          return VM_RUNTIME_ERROR;
        }

        JaclStream* stream = jacl_as_stream(coll_val);

        /* US-004/US-005: Exec streams collect to a single string (concatenated stdout)
         * or return an error value if the command fails */
        if (stream->kind == STREAM_KIND_EXEC) {
          FILE* fp = (FILE*)(uintptr_t)stream->args[0];
          if (!fp) {
            /* Already exhausted - check for cached error */
            if (stream->state == STREAM_ERROR && stream->cached_value != JACL_NIL) {
              result = vm__push(vm, stream->cached_value);
              vm__slot_set(vm, &stream->cached_value, JACL_NIL);
              if (result != VM_OK) return result;
              DISPATCH();
            }
            result = vm__push(vm, jacl_inline_string("", 0));
            if (result != VM_OK) return result;
            DISPATCH();
          }

          /* Read all remaining output into arena-allocated buffer */
          size_t capacity = 4096;
          size_t length = 0;
          char* buffer = (char*)arena_alloc(vm->arena, capacity);

          while (1) {
            size_t avail = capacity - length - 1;
            if (avail < 256) {
              /* Grow buffer */
              size_t new_cap = capacity * 2;
              char* new_buf = (char*)arena_alloc(vm->arena, new_cap);
              memcpy(new_buf, buffer, length);
              buffer = new_buf;
              capacity = new_cap;
              avail = capacity - length - 1;
            }
            size_t nread = fread(buffer + length, 1, avail, fp);
            if (nread == 0) break;
            length += nread;
          }
          buffer[length] = '\0';

          /* Close the pipe and check exit status */
          int pclose_status = pclose(fp);
          stream->args[0] = (JaclVal)0;

          /* US-005: Check exit code and handle errors */
          int exit_code = WIFEXITED(pclose_status) ? WEXITSTATUS(pclose_status) : 1;

          /* Read and clean up stderr temp file */
          char stderr_path[64];
          if (stream->arg_count >= 3 && jacl_is_string(stream->args[2])) {
            uint32_t path_len = jacl_string_byte_len(stream->args[2]);
            jacl_string_data(stream->args[2], stderr_path, sizeof(stderr_path));
            stderr_path[path_len < sizeof(stderr_path) - 1 ? path_len : sizeof(stderr_path) - 1] = '\0';
          } else {
            stderr_path[0] = '\0';
          }

          /* US-007: Clean up stdin temp file if present */
          if (stream->arg_count >= 4 && jacl_is_string(stream->args[3])) {
            char stdin_path[64];
            uint32_t path_len = jacl_string_byte_len(stream->args[3]);
            jacl_string_data(stream->args[3], stdin_path, sizeof(stdin_path));
            stdin_path[path_len < sizeof(stdin_path) - 1 ? path_len : sizeof(stdin_path) - 1] = '\0';
            unlink(stdin_path);
          }

          if (exit_code != 0) {
            /* Read stderr content */
            char stderr_buf[4096] = "";
            if (stderr_path[0] != '\0') {
              FILE* stderr_fp = fopen(stderr_path, "r");
              if (stderr_fp) {
                size_t nread = fread(stderr_buf, 1, sizeof(stderr_buf) - 1, stderr_fp);
                stderr_buf[nread] = '\0';
                fclose(stderr_fp);
              }
              unlink(stderr_path);  /* clean up temp file */
            }

            /* Create error value with stderr as message */
            gc__current_heap = &vm->heap;
            JaclVal err_msg;
            if (stderr_buf[0] != '\0') {
              /* Trim trailing newline */
              size_t len = strlen(stderr_buf);
              while (len > 0 && (stderr_buf[len-1] == '\n' || stderr_buf[len-1] == '\r')) {
                stderr_buf[--len] = '\0';
              }
              err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                        stderr_buf, (uint32_t)len);
            } else {
              /* Default message if no stderr */
              char default_msg[64];
              snprintf(default_msg, sizeof(default_msg), "command exited with code %d", exit_code);
              err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                        default_msg, (uint32_t)strlen(default_msg));
            }

            stream->state = STREAM_ERROR;
            frame = &vm->frames[vm->frame_count - 1];
            result = vm__push(vm, jacl_set_error(err_msg));
            if (result != VM_OK) return result;
            DISPATCH();
          }

          /* Success - clean up stderr temp file */
          if (stderr_path[0] != '\0') {
            unlink(stderr_path);
          }

          stream->state = STREAM_EXHAUSTED;

          /* Create the result string */
          gc__current_heap = &vm->heap;
          JaclVal result_str;
          if (length == 0) {
            result_str = jacl_inline_string("", 0);
          } else {
            result_str = jacl_string_new(&vm->heap, vm->intern_table,
                                         buffer, (uint32_t)length);
          }
          frame = &vm->frames[vm->frame_count - 1];
          result = vm__push(vm, result_str);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        /* US-006: Exec buffer streams collect to the full string (not lines vector) */
        if (stream->kind == STREAM_KIND_EXEC_BUFFER) {
          /* args[0] contains the pre-collected stdout string */
          JaclVal stdout_str = stream->args[0];
          stream->state = STREAM_EXHAUSTED;
          frame = &vm->frames[vm->frame_count - 1];
          result = vm__push(vm, stdout_str);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        /* US-009: Exec pipe streams collect all output from pipeline */
        if (stream->kind == STREAM_KIND_EXEC_PIPE) {
          FILE* fp = (FILE*)(uintptr_t)stream->args[0];
          if (!fp) {
            /* Already exhausted - check for cached error */
            if (stream->state == STREAM_ERROR && stream->cached_value != JACL_NIL) {
              result = vm__push(vm, stream->cached_value);
              vm__slot_set(vm, &stream->cached_value, JACL_NIL);
              if (result != VM_OK) return result;
              DISPATCH();
            }
            result = vm__push(vm, jacl_inline_string("", 0));
            if (result != VM_OK) return result;
            DISPATCH();
          }

          /* Read all remaining output */
          size_t capacity = 4096;
          size_t length = 0;
          char* buffer = (char*)arena_alloc(vm->arena, capacity);

          while (1) {
            size_t avail = capacity - length - 1;
            if (avail < 256) {
              size_t new_cap = capacity * 2;
              char* new_buf = (char*)arena_alloc(vm->arena, new_cap);
              memcpy(new_buf, buffer, length);
              buffer = new_buf;
              capacity = new_cap;
              avail = capacity - length - 1;
            }
            size_t nread = fread(buffer + length, 1, avail, fp);
            if (nread == 0) break;
            length += nread;
          }
          buffer[length] = '\0';

          /* Close file and wait for all children */
          fclose(fp);
          stream->args[0] = (JaclVal)0;

          /* Parse PIDs and wait */
          int exit_code = 0;
          if (jacl_is_string(stream->args[1])) {
            char pid_buf[256];
            uint32_t pid_len = jacl_string_byte_len(stream->args[1]);
            jacl_string_data(stream->args[1], pid_buf, sizeof(pid_buf));
            pid_buf[pid_len < sizeof(pid_buf) - 1 ? pid_len : sizeof(pid_buf) - 1] = '\0';

            char* p = pid_buf;
            int cmd_count = (int)strtol(p, &p, 10);
            for (int i = 0; i < cmd_count && *p == ','; i++) {
              p++;
              pid_t pid = (pid_t)strtol(p, &p, 10);
              int status;
              waitpid(pid, &status, 0);
              if (i == cmd_count - 1) {
                exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
              }
            }
          }

          /* Handle stderr temp file */
          char stderr_path[64];
          if (stream->arg_count >= 3 && jacl_is_string(stream->args[2])) {
            uint32_t path_len = jacl_string_byte_len(stream->args[2]);
            jacl_string_data(stream->args[2], stderr_path, sizeof(stderr_path));
            stderr_path[path_len < sizeof(stderr_path) - 1 ? path_len : sizeof(stderr_path) - 1] = '\0';
          } else {
            stderr_path[0] = '\0';
          }

          if (exit_code != 0) {
            char stderr_buf[4096] = "";
            if (stderr_path[0] != '\0') {
              FILE* stderr_fp = fopen(stderr_path, "r");
              if (stderr_fp) {
                size_t nread = fread(stderr_buf, 1, sizeof(stderr_buf) - 1, stderr_fp);
                stderr_buf[nread] = '\0';
                fclose(stderr_fp);
              }
              unlink(stderr_path);
            }

            gc__current_heap = &vm->heap;
            JaclVal err_msg;
            if (stderr_buf[0] != '\0') {
              size_t len = strlen(stderr_buf);
              while (len > 0 && (stderr_buf[len-1] == '\n' || stderr_buf[len-1] == '\r')) {
                stderr_buf[--len] = '\0';
              }
              err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                        stderr_buf, (uint32_t)len);
            } else {
              char default_msg[64];
              snprintf(default_msg, sizeof(default_msg), "command exited with code %d", exit_code);
              err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                        default_msg, (uint32_t)strlen(default_msg));
            }

            stream->state = STREAM_ERROR;
            frame = &vm->frames[vm->frame_count - 1];
            result = vm__push(vm, jacl_set_error(err_msg));
            if (result != VM_OK) return result;
            DISPATCH();
          }

          /* Success */
          if (stderr_path[0] != '\0') {
            unlink(stderr_path);
          }

          stream->state = STREAM_EXHAUSTED;
          gc__current_heap = &vm->heap;
          JaclVal result_str;
          if (length == 0) {
            result_str = jacl_inline_string("", 0);
          } else {
            result_str = jacl_string_new(&vm->heap, vm->intern_table,
                                         buffer, (uint32_t)length);
          }
          frame = &vm->frames[vm->frame_count - 1];
          result = vm__push(vm, result_str);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        /* Typed collect (STREAM_TYPING_DEBT item 2): a TYPED stream
         * materializes into a typed vec [Vec T]. Wide scalar elements
         * (i64/u64/f64) arrive raw from the pull and are stored raw — the
         * i32-for-small box-back is gone. Struct elements arrive as N
         * inline slots and store flat. Tagged-fit scalars (i32/str/bool/
         * f32) store their tagged JaclVal. Keyed off the runtime elem_idx,
         * so dyn-flow and typed-flow agree; the typer stamps the matching
         * static [Vec T] on typed receivers. Routes generators and derived
         * streams uniformly through vm__pull_stream_one. */
        {
          uint32_t ce = stream->elem_idx;
          bool ce_struct = vm__elem_idx_is_struct(ce);
          /* Value-type scalars only (mirrors the [Vec T] constructor rule):
           * typed-vec storage is GC-opaque, so str (a heap pointer) must
           * stay in a plain traced vec. */
          bool ce_typed_scalar = JACL_IS_SCALAR_TYPE_IDX(ce) &&
                                 JACL_TYPE_IDX_TO_SCALAR(ce) != TYPE_DYN &&
                                 JACL_TYPE_IDX_TO_SCALAR(ce) != TYPE_STR;
          if (ce_struct || ce_typed_scalar) {
            bool ce_raw = ce_struct || vm__elem_idx_is_wide(ce);
            uint32_t stride = ce_struct
                ? vm__struct_width(vm->struct_registry->defs[ce]) : 1;
            gc__current_heap = &vm->heap;
            jacl_typed_vec_root* tvec = jacl_typed_vec_empty_strided(stride);
            JaclVal terr = JACL_NIL;
            while (stream->state != STREAM_EXHAUSTED) {
              JaclVal buf[VM_MAX_STRUCT_SLOTS];
              StreamPullResult pr = vm__pull_stream_one(vm, coll_val, buf);
              if (pr == STREAM_PULL_ERROR) return VM_RUNTIME_ERROR;
              if (pr == STREAM_PULL_EXHAUSTED) break;
              /* US-005: a tagged element may be a yielded error value; raw
               * (wide/struct) bits must never be inspected as a tag. */
              if (!ce_raw && jacl_is_error(buf[0])) { terr = buf[0]; break; }
              gc__current_heap = &vm->heap;
              tvec = jacl_typed_vec_push_back_wide(tvec, buf);
            }
            frame = &vm->frames[vm->frame_count - 1];
            result = vm__push(vm, jacl_is_error(terr)
                                  ? terr : jacl_typed_vector_ptr(tvec));
            if (result != VM_OK) return result;
            DISPATCH();
          }
        }

        /* Derived streams (filter, etc.) use the unified pull helper */
        if (stream->kind != STREAM_KIND_GENERATOR) {
          gc__current_heap = &vm->heap;
          jacl_vec_root* collect_vec = jacl_vec_empty();
          while (stream->state != STREAM_EXHAUSTED) {
            JaclVal elem;
            StreamPullResult pr = vm__pull_stream_dyn(vm, coll_val, &elem);
            if (pr == STREAM_PULL_ERROR) return VM_RUNTIME_ERROR;
            if (pr == STREAM_PULL_EXHAUSTED) break;
            gc__current_heap = &vm->heap;
            collect_vec = jacl_vec_push_back(collect_vec, elem);
          }
          frame = &vm->frames[vm->frame_count - 1];
          result = vm__push(vm, jacl_vector_ptr(collect_vec));
          if (result != VM_OK) return result;
          DISPATCH();
        }

        gc__current_heap = &vm->heap;
        jacl_vec_root* collect_vec = jacl_vec_empty();
        JaclVal sm_error_val = JACL_NIL;

        while (stream->state != STREAM_EXHAUSTED) {
          /* Save caller context */
          uint32_t caller_stack_top = vm->stack_top;
          uint32_t caller_frame_count = vm->frame_count;
          uint8_t* caller_ip = vm->ip;
          BytecodeChunk* caller_chunk = vm->chunk;

          /* State machine generator: call sm_closure(state_obj, nil) */
          JACL_ASSERT_TAG(stream->state_machine, jacl_is_state_machine);
          JaclStateMachine* sm = jacl_as_state_machine(stream->state_machine);
          JACL_ASSERT_TAG(sm->sm_closure, jacl_is_closure);
          JaclClosure* sm_cl = jacl_as_closure(sm->sm_closure);

          result = vm__push(vm, sm->sm_closure);
          if (result != VM_OK) return result;
          result = vm__push(vm, stream->state_machine);
          if (result != VM_OK) return result;
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;

          if (vm->frame_count >= VM_FRAMES_MAX) {
            vm__set_frame_overflow(vm);
            return VM_RUNTIME_ERROR;
          }
          CallFrame* new_frame = &vm->frames[vm->frame_count++];
          new_frame->closure    = sm_cl;
          new_frame->return_ip  = NULL;
          new_frame->stack_base = vm->stack_top - 2;
          new_frame->chunk      = &sm_cl->chunk;
          vm->ip    = sm_cl->chunk.code;
          vm->chunk = &sm_cl->chunk;

          VMResult inner = vm__run(vm, caller_frame_count);

          if (inner == VM_YIELD) {
            stream->state = STREAM_CONSUMED;
            vm__slot_set(vm, &stream->cached_value, vm->yield_value);
            vm->stack_top   = caller_stack_top;
            vm->frame_count = caller_frame_count;
            vm->ip    = caller_ip;
            vm->chunk = caller_chunk;
            frame = &vm->frames[vm->frame_count - 1];
            gc__current_heap = &vm->heap;
            collect_vec = jacl_vec_push_back(collect_vec, vm->yield_value);
          } else if (inner == VM_OK) {
            /* Check if SM function returned an error value */
            if (vm->stack_top > caller_stack_top) {
              JaclVal sm_ret = vm->stack[vm->stack_top - 1];
              if (jacl_is_error(sm_ret)) {
                stream->state = STREAM_ERROR;
                sm_error_val = sm_ret;
                vm->stack_top   = caller_stack_top;
                vm->frame_count = caller_frame_count;
                vm->ip    = caller_ip;
                vm->chunk = caller_chunk;
                frame = &vm->frames[vm->frame_count - 1];
                break;
              }
            }
            stream->state = STREAM_EXHAUSTED;
            vm__slot_set(vm, &stream->cached_value, JACL_NIL);
            vm->stack_top   = caller_stack_top;
            vm->frame_count = caller_frame_count;
            vm->ip    = caller_ip;
            vm->chunk = caller_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          } else {
            stream->state = STREAM_ERROR;
            return inner;
          }
        }

        if (!jacl_is_nil(sm_error_val)) {
          result = vm__push(vm, sm_error_val);
        } else {
          result = vm__push(vm, jacl_vector_ptr(collect_vec));
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_IS_STREAM_EXHAUSTED): {
        /* Pop a value. Push true if it's an exhausted stream, else false. */
        JaclVal sv;
        result = vm__pop(vm, &sv);
        if (result != VM_OK) return result;
        bool exhausted = false;
        if (jacl_is_stream(sv)) {
          JaclStream* s = jacl_as_stream(sv);
          exhausted = (s->state == STREAM_EXHAUSTED);
        }
        result = vm__push(vm, exhausted ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_COUNT): {
        JaclVal coll_val;
        result = vm__pop(vm, &coll_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) {
          result = vm__push(vm, coll_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        if (jacl_is_vector(coll_val)) {
          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(coll_val);
          result = vm__push(vm, jacl_i32((int32_t)jacl_vec_count(vec)));
          if (result != VM_OK) return result;
          DISPATCH();
        }

        if (jacl_is_stream(coll_val)) {
          JaclStream* stream = jacl_as_stream(coll_val);
          int32_t cnt = 0;

          /* Use unified pull helper for all stream kinds */
          while (stream->state != STREAM_EXHAUSTED) {
            JaclVal elem;
            StreamPullResult pr = vm__pull_stream_one(vm, coll_val, &elem);
            if (pr == STREAM_PULL_ERROR) return VM_RUNTIME_ERROR;
            if (pr == STREAM_PULL_EXHAUSTED) break;
            cnt++;
          }
          frame = &vm->frames[vm->frame_count - 1];
          result = vm__push(vm, jacl_i32(cnt));
          if (result != VM_OK) return result;
          DISPATCH();
        }

        vm__set_error(vm, "count requires a vector or stream, got %s",
                     vm__type_name(coll_val));
        return VM_RUNTIME_ERROR;
      }

      CASE(OP_TAKE): {
        JaclVal n_val, coll_val;
        result = vm__pop(vm, &n_val);
        if (result != VM_OK) return result;
        result = vm__pop(vm, &coll_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) {
          result = vm__push(vm, coll_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (jacl_is_error(n_val)) {
          result = vm__push(vm, n_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        if (!jacl_is_i32(n_val)) {
          vm__set_error(vm, "take requires an integer count, got %s",
                       vm__type_name(n_val));
          return VM_RUNTIME_ERROR;
        }
        int32_t n = jacl_as_i32(n_val);
        if (n < 0) n = 0;

        if (jacl_is_vector(coll_val)) {
          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(coll_val);
          uint32_t len = jacl_vec_count(vec);
          uint32_t take_n = (uint32_t)n < len ? (uint32_t)n : len;
          if (take_n == 0) {
            gc__current_heap = &vm->heap;
            result = vm__push(vm, jacl_vector_ptr(jacl_vec_empty()));
          } else {
            gc__current_heap = &vm->heap;
            jacl_vec_root* new_vec = jacl_vec_slice(vec, 0, take_n);
            if (new_vec == NULL) {
              result = vm__push(vm, jacl_vector_ptr(jacl_vec_empty()));
            } else {
              result = vm__push(vm, jacl_vector_ptr(new_vec));
            }
          }
          if (result != VM_OK) return result;
          DISPATCH();
        }

        if (jacl_is_stream(coll_val)) {
          JaclVal take_stream_val = jacl_stream(&vm->heap);
          JaclStream* ts = jacl_as_stream(take_stream_val);
          ts->kind      = STREAM_KIND_TAKE;
          ts->args[0]   = coll_val;     /* source stream */
          ts->args[1]   = jacl_i32(n);  /* remaining count */
          ts->arg_count  = 2;
          /* take preserves the element type — copy source's elem_idx (B). */
          if (jacl_is_stream(coll_val))
            ts->elem_idx = jacl_as_stream(coll_val)->elem_idx;
          result = vm__push(vm, take_stream_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        vm__set_error(vm, "take requires a vector or stream, got %s",
                     vm__type_name(coll_val));
        return VM_RUNTIME_ERROR;
      }

      CASE(OP_FIRST): {
        JaclVal coll_val;
        result = vm__pop(vm, &coll_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) {
          result = vm__push(vm, coll_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        if (jacl_is_vector(coll_val)) {
          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(coll_val);
          if (jacl_vec_count(vec) == 0) {
            result = vm__push(vm, JACL_NIL);
          } else {
            jacl_vec_get_result gr = jacl_vec_get(vec, 0);
            result = vm__push(vm, gr.found ? gr.value : JACL_NIL);
          }
          if (result != VM_OK) return result;
          DISPATCH();
        }

        if (jacl_is_stream(coll_val)) {
          JaclStream* stream = jacl_as_stream(coll_val);

          if (stream->state == STREAM_EXHAUSTED) {
            result = vm__push(vm, JACL_NIL);
            if (result != VM_OK) return result;
            DISPATCH();
          }

          /* Use unified pull helper for all stream kinds */
          {
            JaclVal pulled;
            StreamPullResult pr = vm__pull_stream_dyn(vm, coll_val, &pulled);
            if (pr == STREAM_PULL_ERROR) return VM_RUNTIME_ERROR;
            if (pr == STREAM_PULL_EXHAUSTED) pulled = JACL_NIL;
            frame = &vm->frames[vm->frame_count - 1];
            result = vm__push(vm, pulled);
            if (result != VM_OK) return result;
          }
          DISPATCH();
        }

        vm__set_error(vm, "first requires a vector or stream, got %s",
                     vm__type_name(coll_val));
        return VM_RUNTIME_ERROR;
      }

      CASE(OP_LINES): {
        JaclVal str_val;
        result = vm__pop(vm, &str_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(str_val)) {
          result = vm__push(vm, str_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        if (!jacl_is_string(str_val)) {
          vm__set_error(vm, "lines requires a string, got %s",
                       vm__type_name(str_val));
          return VM_RUNTIME_ERROR;
        }

        JaclVal lines_stream_val = jacl_stream(&vm->heap);
        JaclStream* ls = jacl_as_stream(lines_stream_val);
        ls->kind      = STREAM_KIND_LINES;
        ls->elem_idx  = JACL_SCALAR_TYPE_IDX(TYPE_STR);  /* lines yields str */
        ls->args[0]   = str_val;       /* source string */
        ls->args[1]   = jacl_i32(0);   /* current byte index */
        ls->arg_count  = 2;
        result = vm__push(vm, lines_stream_val);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_COLLECT_VARIADIC): {
        uint8_t min_arity = vm__read_byte(vm);
        uint32_t actual_count = vm->stack_top - frame->stack_base;
        uint32_t extra = actual_count > min_arity ? actual_count - min_arity : 0;
        /* Create vector from excess args */
        jacl_vec_root* vec = jacl_vec_empty();
        for (uint32_t i = 0; i < extra; i++) {
          vec = jacl_vec_push_back(vec, vm->stack[frame->stack_base + min_arity + i]);
        }
        /* Collapse: set rest param slot and adjust stack_top */
        vm->stack[frame->stack_base + min_arity] = jacl_vector_ptr(vec);
        vm->stack_top = frame->stack_base + min_arity + 1;
        DISPATCH();
      }

      CASE(OP_GET_STATE_FIELD): {
        uint8_t field_index = vm__read_byte(vm);
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JACL_ASSERT_TAG(state_val, jacl_is_state_machine);
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        result = vm__push(vm, sm->fields[field_index]);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_SET_STATE_FIELD): {
        uint8_t field_index = vm__read_byte(vm);
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JACL_ASSERT_TAG(state_val, jacl_is_state_machine);
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        JaclVal value;
        result = vm__pop(vm, &value);
        if (result != VM_OK) return result;
        /* AUDIT §10/§11: heap-pointer write on a published GC object.
         * Inline-struct slots (raw bytes) are written by OP_SET_STATE_FIELD_WIDE
         * and never reach here, so no bitmap check needed. Routed through
         * vm__slot_set so the concurrent GC trace at gc_collect.c sees an
         * atomic store paired with its atomic load. */
        vm__slot_set(vm, &sm->fields[field_index], value);
        DISPATCH();
      }

      CASE(OP_GET_STATE_FIELD_CELL): {
        uint8_t field_index = vm__read_byte(vm);
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JACL_ASSERT_TAG(state_val, jacl_is_state_machine);
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        JaclVal cell = sm->fields[field_index];
        JaclMutableRef *ref = jacl_as_cell(cell);
        result = vm__push(vm, MREF_VAL(ref));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_SET_STATE_FIELD_CELL): {
        uint8_t field_index = vm__read_byte(vm);
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JACL_ASSERT_TAG(state_val, jacl_is_state_machine);
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        JaclVal cell = sm->fields[field_index];
        JaclMutableRef *ref = jacl_as_cell(cell);
        JaclVal new_val;
        result = vm__pop(vm, &new_val);
        if (result != VM_OK) return result;
        gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                         MREF_VAL(ref), new_val);
        gc_remembered_set_barrier(vm->remembered_set, cell, new_val);
        MREF_VAL(ref) = new_val;
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_GET_STATE_FIELD_WIDE): {
        /* Copy N consecutive JaclVal slots from SM fields to the stack.
           Used to load a struct stored inline across multiple SM slots. */
        uint8_t base_idx = vm__read_byte(vm);
        uint8_t width    = vm__read_byte(vm);
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JACL_ASSERT_TAG(state_val, jacl_is_state_machine);
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        uint32_t push_base = vm->stack_top;
        for (uint8_t i = 0; i < width; i++) {
          result = vm__push(vm, sm->fields[base_idx + i]);
          if (result != VM_OK) return result;
        }
        /* US-014: mark pushed slots as raw struct bytes */
        for (uint32_t si = push_base; si < push_base + width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, si);
        }
        DISPATCH();
      }

      CASE(OP_SET_STATE_FIELD_WIDE): {
        /* Copy N consecutive JaclVal slots from stack top into SM fields.
           The top-of-stack holds the raw struct data across width slots.
           Byte-offset addressing: sm->fields[base_idx..base_idx+width-1]
           map to the struct's raw data as JaclVal-sized chunks. */
        uint8_t base_idx = vm__read_byte(vm);
        uint8_t width    = vm__read_byte(vm);
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JACL_ASSERT_TAG(state_val, jacl_is_state_machine);
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        /* Copy from stack (bottom of the N-slot range first), clearing the
         * stack's inline bits as we vacate the slots — vm__push doesn't
         * touch the bitmap, so a stale bit would make a later tagged value
         * at the same depth read as raw inline bytes. */
        for (uint8_t i = 0; i < width; i++) {
          sm->fields[base_idx + i] = vm->stack[vm->stack_top - width + i];
          BITMAP_CLR(vm->inline_slot_bitmap, vm->stack_top - width + i);
        }
        /* US-014: mark SM field slots as raw struct bytes */
        for (uint8_t i = 0; i < width; i++) {
          BITMAP_SET(sm->field_inline_bitmap, base_idx + i);
        }
        vm->stack_top -= width;
        DISPATCH();
      }

      CASE(OP_GET_RESUME_POINT): {
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JACL_ASSERT_TAG(state_val, jacl_is_state_machine);
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        result = vm__push(vm, jacl_i32((int32_t)sm->resume_point));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_SET_RESUME_POINT): {
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JACL_ASSERT_TAG(state_val, jacl_is_state_machine);
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        JaclVal value;
        result = vm__pop(vm, &value);
        if (result != VM_OK) return result;
        sm->resume_point = (uint32_t)jacl_as_i32(value);
        DISPATCH();
      }

      CASE(OP_YIELD_SM): {
        /* State machine yield: pop yielded value, return VM_YIELD.
           No continuation closure — the SM function is re-entered via
           the dispatch table on the next stream_next call. */
        JaclVal value;
        result = vm__pop(vm, &value);
        if (result != VM_OK) return result;
        vm->yield_value = value;
        return VM_YIELD;
      }

      CASE(OP_YIELD_SM_WIDE): {
        /* Struct-element yield (multi-slot stream channel). Pop the struct —
         * inline N slots or heap pointer, vm__pop_struct dispatches — into
         * the VM yield buffer as raw value bytes. yield_value is set to nil
         * so any dyn reader (guarded collect/spread loops) sees nil, never
         * garbage. The generator pull copies yield_wide to the consumer. */
        uint16_t type_idx = vm__read_u16(vm);
        vm->yield_wide_width = vm__pop_struct(vm, type_idx, vm->yield_wide);
        vm->yield_value = JACL_NIL;
        return VM_YIELD;
      }

      CASE(OP_AWAIT_SM): {
        /* State machine await: pop value from stack.
           - If it's a Job map: do blocking wait and return result
           - If it's a Future: handle via SM suspension semantics */
        JaclVal val;
        result = vm__pop(vm, &val);
        if (result != VM_OK) return result;

        /* US-010: Check if it's a Job map first */
        if (jacl_is_map(val)) {
          jacl_map_node* m = (jacl_map_node*)jacl_as_ptr(val);
          gc__current_heap = &vm->heap;
          JaclVal marker_key = jacl_intern(&vm->heap, vm->intern_table, "_is_job", 7);
          JaclVal marker_val = jacl_map_get(m, marker_key);

          if (marker_val == JACL_TRUE) {
            /* It's a Job - extract pid and wait (blocking) */
            JaclVal pid_key = jacl_intern(&vm->heap, vm->intern_table, "pid", 3);
            JaclVal pid_val = jacl_map_get(m, pid_key);
            if (!jacl_is_i32(pid_val)) {
              vm__set_error(vm, "invalid job: missing pid");
              return VM_RUNTIME_ERROR;
            }
            pid_t pid = (pid_t)jacl_as_i32(pid_val);

            /* Wait for process to complete */
            int status;
            pid_t waited = waitpid(pid, &status, 0);
            if (waited < 0) {
              vm__set_error(vm, "waitpid failed: %s", strerror(errno));
              return VM_RUNTIME_ERROR;
            }

            int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

            /* Read stdout from temp file */
            JaclVal stdout_key = jacl_intern(&vm->heap, vm->intern_table, "_stdout_path", 12);
            JaclVal stdout_path_val = jacl_map_get(m, stdout_key);
            char stdout_path[128] = {0};
            if (jacl_is_string(stdout_path_val)) {
              uint32_t len = jacl_string_byte_len(stdout_path_val);
              if (len < sizeof(stdout_path)) {
                jacl_string_data(stdout_path_val, stdout_path, len + 1);
              }
            }

            char* stdout_buf = NULL;
            size_t stdout_len = 0;
            if (stdout_path[0]) {
              FILE* fp = fopen(stdout_path, "r");
              if (fp) {
                fseek(fp, 0, SEEK_END);
                stdout_len = (size_t)ftell(fp);
                fseek(fp, 0, SEEK_SET);
                stdout_buf = (char*)arena_alloc(vm->arena, (uint32_t)(stdout_len + 1));
                fread(stdout_buf, 1, stdout_len, fp);
                stdout_buf[stdout_len] = '\0';
                fclose(fp);
              }
              unlink(stdout_path);
            }

            /* Read stderr from temp file */
            JaclVal stderr_key = jacl_intern(&vm->heap, vm->intern_table, "_stderr_path", 12);
            JaclVal stderr_path_val = jacl_map_get(m, stderr_key);
            char stderr_path[128] = {0};
            if (jacl_is_string(stderr_path_val)) {
              uint32_t len = jacl_string_byte_len(stderr_path_val);
              if (len < sizeof(stderr_path)) {
                jacl_string_data(stderr_path_val, stderr_path, len + 1);
              }
            }

            char* stderr_buf = NULL;
            size_t stderr_len = 0;
            if (stderr_path[0]) {
              FILE* fp = fopen(stderr_path, "r");
              if (fp) {
                fseek(fp, 0, SEEK_END);
                stderr_len = (size_t)ftell(fp);
                fseek(fp, 0, SEEK_SET);
                stderr_buf = (char*)arena_alloc(vm->arena, (uint32_t)(stderr_len + 1));
                fread(stderr_buf, 1, stderr_len, fp);
                stderr_buf[stderr_len] = '\0';
                fclose(fp);
              }
              unlink(stderr_path);
            }

            /* Create result map {stdout, stderr, exit} */
            jacl_map_node* result_map = NULL;

            /* Create stdout as a stream (like OP_EXEC_FULL) */
            JaclVal stdout_stream_val = jacl_stream(&vm->heap);
            JaclStream* stdout_stream = jacl_as_stream(stdout_stream_val);
            stdout_stream->kind = STREAM_KIND_EXEC_BUFFER;
            stdout_stream->state = STREAM_PENDING;
            if (stdout_buf && stdout_len > 0) {
              stdout_stream->args[0] = jacl_intern(&vm->heap, vm->intern_table,
                                                   stdout_buf, (uint32_t)stdout_len);
            } else {
              stdout_stream->args[0] = jacl_intern(&vm->heap, vm->intern_table, "", 0);
            }
            stdout_stream->args[1] = jacl_i32(0); /* position index */
            stdout_stream->arg_count = 2;

            JaclVal out_key = jacl_intern(&vm->heap, vm->intern_table, "stdout", 6);
            result_map = jacl_map_set(result_map, out_key, stdout_stream_val);

            JaclVal err_key = jacl_intern(&vm->heap, vm->intern_table, "stderr", 6);
            JaclVal err_val = stderr_buf && stderr_len > 0
                ? jacl_intern(&vm->heap, vm->intern_table, stderr_buf, (uint32_t)stderr_len)
                : jacl_intern(&vm->heap, vm->intern_table, "", 0);
            result_map = jacl_map_set(result_map, err_key, err_val);

            JaclVal exit_key = jacl_intern(&vm->heap, vm->intern_table, "exit", 4);
            result_map = jacl_map_set(result_map, exit_key, jacl_i32(exit_code));

            result = vm__push(vm, jacl_map_ptr(result_map));
            if (result != VM_OK) return result;
            DISPATCH();
          }
        }

        /* Not a job - must be a future */
        if (!jacl_is_future(val)) {
          vm__set_error(vm, "await requires a future or job, got %s",
                       vm__type_name(val));
          return VM_RUNTIME_ERROR;
        }

        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JaclFuture *fut = jacl_as_future(val);
        uint32_t fstate = ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_ACQUIRE);

        if (fstate == FUTURE_RESOLVED || fstate == FUTURE_ERROR) {
          /* Already settled — push result and continue inline. */
          JaclVal await_result = (JaclVal)fut->result;
          result = vm__push(vm, await_result);
          if (result != VM_OK) return result;
        } else if (vm->runtime) {
          /* PENDING in runtime mode — register state machine as waiter
             on the future and suspend by returning VM_OK. */
          bool added = jacl_future_add_waiter(fut, state_val, &vm->heap,
                                              vm->grey_buf, vm->gc_active_ptr);
          if (!added) {
            /* Race: future resolved between our check and add_waiter.
               Push result and continue inline. */
            JaclVal await_result = (JaclVal)fut->result;
            result = vm__push(vm, await_result);
            if (result != VM_OK) return result;
          } else {
            /* Successfully registered — suspend current task. */
            return VM_OK;
          }
        } else {
          /* PENDING in single-threaded mode — shouldn't happen since
             spawn resolves synchronously.  Push nil as fallback. */
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;
        }
        DISPATCH();
      }

      CASE(OP_SLEEP_SM): {
        /* SM sleep: pop seconds, register a timer with the runtime, and
           suspend by returning VM_OK with the frame intact. The compiler
           emits OP_SET_RESUME_POINT immediately before this op, so when
           the timer fires and the SM is resumed via
           runtime__schedule_sm_resumption, the dispatch table at SM entry
           jumps to the resume label (which pushes __rv = nil onto the
           stack as the sleep expression's value). */
        JaclVal dur_val;
        result = vm__pop(vm, &dur_val);
        if (result != VM_OK) return result;

        double secs;
        if (jacl_is_i32(dur_val))      secs = (double)jacl_as_i32(dur_val);
        else if (jacl_is_i64(dur_val)) secs = (double)jacl_as_i64(dur_val);
        else if (jacl_is_f32(dur_val)) secs = (double)jacl_as_f32(dur_val);
        else if (jacl_is_f64(dur_val)) secs = jacl_as_f64(dur_val);
        else {
          vm__set_error(vm, "sleep requires a number, got %s",
                       vm__type_name(dur_val));
          return VM_RUNTIME_ERROR;
        }
        if (!(secs >= 0.0)) secs = 0.0;  /* clamp negative + NaN to 0 */

        if (vm->runtime) {
          /* Concurrent: register the deadline with the runtime and suspend.
             An idle worker fires the wakeup via runtime__poll_timers. */
          JaclVal state_val = vm->stack[frame->stack_base + 0];
          uint64_t duration_ns = (uint64_t)(secs * 1e9);
          runtime__schedule_timer(vm->runtime, duration_ns, state_val);
          return VM_OK;
        }
        /* Single-threaded fallback: there is no scheduler to wake us, so
           block this thread. Acceptable because non-runtime mode has no
           other work to run anyway. */
        {
          struct timespec ts;
          ts.tv_sec  = (time_t)secs;
          ts.tv_nsec = (long)((secs - (double)ts.tv_sec) * 1e9);
          if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
          nanosleep(&ts, NULL);
        }
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_SLEEP_BLOCK): {
        /* Toplevel/non-SM sleep: pop seconds, nanosleep on the current
           thread, push nil. Always blocking — used when the surrounding
           proc isn't compiled as a state machine. */
        JaclVal dur_val;
        result = vm__pop(vm, &dur_val);
        if (result != VM_OK) return result;

        double secs;
        if (jacl_is_i32(dur_val))      secs = (double)jacl_as_i32(dur_val);
        else if (jacl_is_i64(dur_val)) secs = (double)jacl_as_i64(dur_val);
        else if (jacl_is_f32(dur_val)) secs = (double)jacl_as_f32(dur_val);
        else if (jacl_is_f64(dur_val)) secs = jacl_as_f64(dur_val);
        else {
          vm__set_error(vm, "sleep requires a number, got %s",
                       vm__type_name(dur_val));
          return VM_RUNTIME_ERROR;
        }
        if (!(secs >= 0.0)) secs = 0.0;

        struct timespec ts;
        ts.tv_sec  = (time_t)secs;
        ts.tv_nsec = (long)((secs - (double)ts.tv_sec) * 1e9);
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        nanosleep(&ts, NULL);

        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_CALL_SUSPEND): {
        /* Like OP_CALL, but for calls to known suspending procs in SM context.
           In concurrent mode: spawn the inner SM as a separate task with a
           future, push the future (OP_AWAIT_SM follows to handle suspension).
           In single-threaded mode: fall through to synchronous call. */
        uint8_t arg_count = vm__read_byte(vm);
        JaclVal callee = vm->stack[vm->stack_top - arg_count - 1];

        if (!jacl_is_closure(callee)) {
          vm__set_error(vm, "call_suspend requires a closure, got %s",
                       vm__type_name(callee));
          return VM_RUNTIME_ERROR;
        }

        JaclClosure *closure = jacl_as_closure(callee);

        if (vm->runtime && closure->is_sm_compiled && !closure->is_generator) {
          /* Concurrent mode with SM callee: spawn as separate task */

          /* Arity check against user parameter count */
          if (arg_count != closure->min_args) {
            vm__set_error(vm, "expected %d arguments but got %d",
                         (int)closure->min_args, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }

          /* Allocate all GC objects first, rooting each on the stack
             to survive GC. Only write to them after all allocs complete,
             since GC evacuation can move objects and stale C pointers. */

          /* 1. Allocate inner SM (root on stack) */
          JaclVal sm_val = gc_alloc_state_machine(&vm->heap, closure->sm_field_count);
          result = vm__push(vm, sm_val);
          if (result != VM_OK) return result;

          /* 2. Allocate future (root on stack) */
          JaclVal future_val = jacl_future(&vm->heap);
          result = vm__push(vm, future_val);
          if (result != VM_OK) return result;

          /* 3. Allocate resolve_k (sm_val + future_val rooted on stack) */
          JaclVal resolve_k = runtime__create_resolve_closure(
              &vm->heap, vm->arena, future_val);

          /* All allocs done — now safe to write via re-read pointers.
             GC may have moved objects, so re-derive C pointers from
             tagged values (which the GC updates on the stack). */
          sm_val = vm->stack[vm->stack_top - 2];     /* re-read after GC */
          future_val = vm->stack[vm->stack_top - 1];  /* re-read after GC */
          JaclStateMachine *inner_sm = jacl_as_state_machine(sm_val);
          vm__slot_set(vm, &inner_sm->sm_closure, callee);
          vm__slot_set(vm, &inner_sm->error_k, resolve_k);
          for (uint8_t i = 0; i < arg_count && i < closure->sm_field_count; i++) {
            /* args at: stack_top - 2(roots) - arg_count + i */
            vm__slot_set(vm, &inner_sm->fields[i], vm->stack[vm->stack_top - 2 - arg_count + i]);
          }

          /* 4. Submit inner SM as a task */
          runtime__schedule_sm_resumption(vm->runtime, sm_val, JACL_NIL);

          /* 5. Pop rooted values, pop callee + args, push future */
          vm->stack_top -= 2; /* pop future_val, sm_val roots */
          vm->stack_top -= (arg_count + 1); /* pop callee + args */
          result = vm__push(vm, future_val);
          if (result != VM_OK) return result;
        } else {
          /* Single-threaded or non-SM callee: execute synchronously,
             wrap result in a resolved future (OP_AWAIT_SM follows). */
          uint8_t *saved_ip = vm->ip;
          BytecodeChunk *saved_chunk = vm->chunk;
          uint32_t saved_frame_count = vm->frame_count;

          /* SM-compiled non-generator: wrap args into SM */
          if (closure->is_sm_compiled && !closure->is_generator) {
            if (arg_count != closure->min_args) {
              vm__set_error(vm, "expected %d arguments but got %d",
                           (int)closure->min_args, (int)arg_count);
              return VM_RUNTIME_ERROR;
            }
            JaclVal sm_val = gc_alloc_state_machine(&vm->heap, closure->sm_field_count);
            JaclStateMachine* sm = jacl_as_state_machine(sm_val);
            vm__slot_set(vm, &sm->sm_closure, callee);
            for (uint8_t i = 0; i < arg_count && i < closure->sm_field_count; i++) {
              vm__slot_set(vm, &sm->fields[i], vm->stack[vm->stack_top - arg_count + i]);
            }
            uint32_t callee_pos = vm->stack_top - arg_count - 1;
            vm->stack[callee_pos + 1] = sm_val;
            vm->stack[callee_pos + 2] = JACL_NIL;
            vm->stack_top = callee_pos + 3;
            arg_count = 2;
          }

          if (vm->frame_count >= VM_FRAMES_MAX) {
            vm__set_frame_overflow(vm);
            return VM_STACK_OVERFLOW;
          }
          CallFrame *sf = &vm->frames[vm->frame_count++];
          sf->closure    = closure;
          sf->return_ip  = saved_ip;
          sf->stack_base = vm->stack_top - (closure->is_sm_compiled ? 2 : arg_count);
          sf->chunk      = &closure->chunk;
          vm->ip    = closure->chunk.code;
          vm->chunk = &closure->chunk;

          VMResult sub = vm__run(vm, saved_frame_count);

          frame = &vm->frames[vm->frame_count - 1];
          vm->ip    = saved_ip;
          vm->chunk = saved_chunk;

          /* Wrap result in a resolved future for OP_AWAIT_SM */
          JaclVal call_result = JACL_NIL;
          if (sub == VM_OK && vm->stack_top > 0) {
            call_result = vm->stack[--vm->stack_top];
          } else if (sub != VM_OK) {
            /* Keep the inner error message — overwriting it hid the real
             * failure behind a generic wrapper. */
            if (!vm->error_message || vm->error_message[0] == '\0')
              vm__set_error(vm, "call_suspend: inner call failed");
            return VM_RUNTIME_ERROR;
          }

          JaclVal f = jacl_future(&vm->heap);
          JaclFuture *fut = jacl_as_future(f);
          jacl_future_resolve(fut, call_result,
                              vm->grey_buf, vm->gc_active_ptr);
          result = vm__push(vm, f);
          if (result != VM_OK) return result;
        }
        DISPATCH();
      }

      CASE(OP_SYNTAX_SPLICE): {
        vm__set_error(vm, "OP_SYNTAX_SPLICE is no longer supported");
        return VM_RUNTIME_ERROR;
      }

      CASE(OP_SYNTAX_OP): {
        /* US-015: syntax introspection. US-016: syntax construction.
         * US-017: user-raised syntax errors. Subops:
         *   0  = syntax-kind      (syntax → string)
         *   1  = syntax-datum     (literal/var-ref → raw datum)
         *   2  = syntax-head      (command → syntax)
         *   3  = syntax-args      (command → vec of syntax)
         *   4  = syntax-commands  (block → vec of syntax)
         *   5  = syntax-pos       (syntax → map {line, col})
         *   6  = syntax->string   (syntax → pretty-printed string)
         *   7  = make-syntax lit-int    (i32 → syntax)
         *   8  = make-syntax lit-float  (f32 → syntax)
         *   9  = make-syntax lit-string (string → syntax)
         *   10 = make-syntax var-ref    (string → syntax)
         *   11 = make-syntax command    (head, args-vec → syntax)
         *   12 = make-syntax block      (commands-vec → syntax)
         *   13 = syntax-error message       (string → halt)
         *   14 = syntax-error message+syn   (string, syntax → halt)
         *   15 = make-syntax var-ref-caret  (string → syntax, scope_mark=0, is_caret=1)
         *   16 = gensym                     (prefix-string → syntax var-ref, is_gensym=1)
         *   17 = validate-unquote           (peek TOS: must be syntax object)
         *   18 = validate-unquote-splice    (peek TOS: must be vec of syntax objects) */
        uint8_t subop = vm__read_byte(vm);
        gc__current_heap = &vm->heap;

        /* US-017: syntax-error — halt execution with a custom message and
         * (optionally) a source position from the provided syntax object.
         * Dispatched before both construction and introspection because
         * its operand types are string (or string+syntax), not a bare
         * syntax object on top of the stack. */
        if (subop == 13) {  /* syntax-error message */
          JaclVal msg;
          result = vm__pop(vm, &msg); if (result != VM_OK) return result;
          if (jacl_is_error(msg)) {
            result = vm__push(vm, msg);
            if (result != VM_OK) return result;
            DISPATCH();
          }
          if (!jacl_is_string(msg)) {
            vm__set_error(vm, "type error in 'syntax-error': expected string message, got %s",
                         vm__type_name(msg));
            return VM_RUNTIME_ERROR;
          }
          char mbuf[192];
          uint32_t mlen = jacl_string_data(msg, mbuf, sizeof(mbuf) - 1);
          mbuf[mlen] = '\0';
          vm__set_error(vm, "syntax-error: %s", mbuf);
          return VM_RUNTIME_ERROR;
        }
        if (subop == 14) {  /* syntax-error message + syntax */
          JaclVal syn_val, msg;
          result = vm__pop(vm, &syn_val); if (result != VM_OK) return result;
          result = vm__pop(vm, &msg);     if (result != VM_OK) return result;
          if (jacl_is_error(syn_val)) {
            result = vm__push(vm, syn_val);
            if (result != VM_OK) return result;
            DISPATCH();
          }
          if (jacl_is_error(msg)) {
            result = vm__push(vm, msg);
            if (result != VM_OK) return result;
            DISPATCH();
          }
          if (!jacl_is_string(msg)) {
            vm__set_error(vm, "type error in 'syntax-error': expected string message, got %s",
                         vm__type_name(msg));
            return VM_RUNTIME_ERROR;
          }
          if (!jacl_is_syntax(syn_val)) {
            vm__set_error(vm, "type error in 'syntax-error': expected syntax object as second argument, got %s",
                         vm__type_name(syn_val));
            return VM_RUNTIME_ERROR;
          }
          JaclSyntax *syn = jacl_as_syntax(syn_val);
          char mbuf[192];
          uint32_t mlen = jacl_string_data(msg, mbuf, sizeof(mbuf) - 1);
          mbuf[mlen] = '\0';
          vm__set_error(vm, "syntax-error at %u:%u: %s",
                       syn->pos_line, syn->pos_col, mbuf);
          return VM_RUNTIME_ERROR;
        }

        /* US-016: construction subops — each kind pops its own operands,
         * allocates a fresh JaclSyntax, and pushes it. Dispatched before
         * the introspection path because construction ops don't expect
         * a syntax object as the operand on top of the stack. */
        if ((subop >= 7 && subop <= 12) || subop == 15 || subop == 16 ||
            subop == 17 || subop == 18 || subop == 19) {
          static const char *mk_names[] = {
            "make-syntax lit-int",    "make-syntax lit-float",
            "make-syntax lit-string", "make-syntax var-ref",
            "make-syntax command",    "make-syntax block"
          };
          const char *mk_name = (subop <= 12) ? mk_names[subop - 7]
                               : (subop == 16) ? "gensym"
                                              : "make-syntax var-ref-caret";

          switch (subop) {
          case 7: {  /* make-syntax lit-int */
            JaclVal v;
            result = vm__pop(vm, &v); if (result != VM_OK) return result;
            if (jacl_is_error(v)) { result = vm__push(vm, v); if (result != VM_OK) return result; break; }
            if (!jacl_is_i32(v)) {
              vm__set_error(vm, "type error in '%s': expected i32, got %s",
                            mk_name, vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_LIT_INT;
            rsyn->data.lit_int.value = jacl_as_i32(v);
            rsyn->scope_mark = vm->macro_scope_mark;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 8: {  /* make-syntax lit-float */
            JaclVal v;
            result = vm__pop(vm, &v); if (result != VM_OK) return result;
            if (jacl_is_error(v)) { result = vm__push(vm, v); if (result != VM_OK) return result; break; }
            if (!jacl_is_f32(v)) {
              vm__set_error(vm, "type error in '%s': expected f32, got %s",
                            mk_name, vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_LIT_FLOAT;
            rsyn->data.lit_float.value = jacl_as_f32(v);
            rsyn->scope_mark = vm->macro_scope_mark;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 9: {  /* make-syntax lit-string */
            JaclVal v;
            result = vm__pop(vm, &v); if (result != VM_OK) return result;
            if (jacl_is_error(v)) { result = vm__push(vm, v); if (result != VM_OK) return result; break; }
            if (!jacl_is_string(v)) {
              vm__set_error(vm, "type error in '%s': expected string, got %s",
                            mk_name, vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_LIT_STRING;
            rsyn->data.lit_string.value = v;
            rsyn->scope_mark = vm->macro_scope_mark;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 10: {  /* make-syntax var-ref */
            JaclVal v;
            result = vm__pop(vm, &v); if (result != VM_OK) return result;
            if (jacl_is_error(v)) { result = vm__push(vm, v); if (result != VM_OK) return result; break; }
            if (!jacl_is_string(v)) {
              vm__set_error(vm, "type error in '%s': expected string, got %s",
                            mk_name, vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_VAR_REF;
            rsyn->data.var_ref.name = v;
            rsyn->scope_mark = vm->macro_scope_mark;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 15: {  /* make-syntax var-ref-caret (^name: scope_mark=0, is_caret=1) */
            JaclVal v;
            result = vm__pop(vm, &v); if (result != VM_OK) return result;
            if (jacl_is_error(v)) { result = vm__push(vm, v); if (result != VM_OK) return result; break; }
            if (!jacl_is_string(v)) {
              vm__set_error(vm, "type error in '%s': expected string, got %s",
                            mk_name, vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_VAR_REF;
            rsyn->data.var_ref.name = v;
            rsyn->scope_mark = 0;  /* caret: bypass macro mark */
            rsyn->is_caret = 1;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 16: {  /* US-010: gensym (prefix-string → syntax var-ref) */
            JaclVal v;
            result = vm__pop(vm, &v); if (result != VM_OK) return result;
            if (jacl_is_error(v)) { result = vm__push(vm, v); if (result != VM_OK) return result; break; }
            if (!jacl_is_string(v)) {
              vm__set_error(vm, "type error in '%s': expected string, got %s",
                            mk_name, vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            if (!vm->gensym_counter_ptr) {
              vm__set_error(vm, "gensym: no gensym counter available (not in macro expansion)");
              return VM_RUNTIME_ERROR;
            }
            char prefix_buf[65];
            uint32_t plen = jacl_string_byte_len(v);
            if (plen > 64) {
              vm__set_error(vm, "gensym: prefix too long (max 64 bytes)");
              return VM_RUNTIME_ERROR;
            }
            jacl_string_data(v, prefix_buf, sizeof(prefix_buf));
            prefix_buf[plen] = '\0';
            const char *gerr = NULL;
            JaclVal out = jacl_gensym_next(prefix_buf, plen,
                                           &vm->heap, vm->intern_table,
                                           vm->gensym_counter_ptr,
                                           vm->macro_scope_mark, &gerr);
            if (gerr) {
              vm__set_error(vm, "gensym: %s", gerr);
              return VM_RUNTIME_ERROR;
            }
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 17: {  /* US-012: validate-unquote (top-of-stack must be syntax) */
            JaclVal v = vm->stack[vm->stack_top - 1];
            if (!jacl_is_nil(v) && !jacl_is_syntax(v)) {
              vm__set_error(vm, "~ (unquote) requires a syntax object, got %s",
                            vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            break;
          }
          case 18: {  /* US-012: validate-unquote-splice (top-of-stack must be vec of syntax) */
            JaclVal v = vm->stack[vm->stack_top - 1];
            if (!jacl_is_vector(v)) {
              vm__set_error(vm, "~@ (unquote-splicing) requires a vec of syntax objects, got %s",
                            vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            jacl_vec_root *vr = (jacl_vec_root *)jacl_as_ptr(v);
            uint32_t vc = jacl_vec_count(vr);
            for (uint32_t i = 0; i < vc; i++) {
              JaclVal elem = jacl_vec_get(vr, i).value;
              if (!jacl_is_syntax(elem)) {
                vm__set_error(vm, "~@ (unquote-splicing) requires a vec of syntax objects, element %u is %s",
                              i, vm__type_name(elem));
                return VM_RUNTIME_ERROR;
              }
            }
            break;
          }
          case 19: {  /* make-syntax lit-string-caret (scope_mark=0 for anaphoric introduction) */
            JaclVal v;
            result = vm__pop(vm, &v); if (result != VM_OK) return result;
            if (jacl_is_error(v)) { result = vm__push(vm, v); if (result != VM_OK) return result; break; }
            if (!jacl_is_string(v)) {
              vm__set_error(vm, "type error in 'make-syntax lit-string-caret': expected string, got %s",
                            vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_LIT_STRING;
            rsyn->data.lit_string.value = v;
            rsyn->scope_mark = 0;  /* caret: bypass macro mark */
            rsyn->is_caret = 1;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 11: {  /* make-syntax command (head, args-vec) */
            JaclVal args_vec, head_val;
            result = vm__pop(vm, &args_vec); if (result != VM_OK) return result;
            result = vm__pop(vm, &head_val); if (result != VM_OK) return result;
            if (jacl_is_error(args_vec)) { result = vm__push(vm, args_vec); if (result != VM_OK) return result; break; }
            if (jacl_is_error(head_val)) { result = vm__push(vm, head_val); if (result != VM_OK) return result; break; }
            if (!jacl_is_syntax(head_val)) {
              vm__set_error(vm, "type error in '%s': head must be a syntax object, got %s",
                            mk_name, vm__type_name(head_val));
              return VM_RUNTIME_ERROR;
            }
            if (!jacl_is_vector(args_vec)) {
              vm__set_error(vm, "type error in '%s': args must be a vec of syntax, got %s",
                            mk_name, vm__type_name(args_vec));
              return VM_RUNTIME_ERROR;
            }
            jacl_vec_root *vroot = (jacl_vec_root *)jacl_as_ptr(args_vec);
            uint32_t vcount = jacl_vec_count(vroot);
            for (uint32_t i = 0; i < vcount; i++) {
              JaclVal elem = jacl_vec_get(vroot, i).value;
              if (!jacl_is_syntax(elem)) {
                vm__set_error(vm, "type error in '%s': args must be a vec of syntax, element %u is %s",
                              mk_name, i, vm__type_name(elem));
                return VM_RUNTIME_ERROR;
              }
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_COMMAND;
            rsyn->data.command.head = head_val;
            rsyn->data.command.args = args_vec;
            rsyn->scope_mark = vm->macro_scope_mark;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 12: {  /* make-syntax block (commands-vec) */
            JaclVal cmds_vec;
            result = vm__pop(vm, &cmds_vec); if (result != VM_OK) return result;
            if (jacl_is_error(cmds_vec)) { result = vm__push(vm, cmds_vec); if (result != VM_OK) return result; break; }
            if (!jacl_is_vector(cmds_vec)) {
              vm__set_error(vm, "type error in '%s': commands must be a vec of syntax, got %s",
                            mk_name, vm__type_name(cmds_vec));
              return VM_RUNTIME_ERROR;
            }
            jacl_vec_root *vroot = (jacl_vec_root *)jacl_as_ptr(cmds_vec);
            uint32_t vcount = jacl_vec_count(vroot);
            for (uint32_t i = 0; i < vcount; i++) {
              JaclVal elem = jacl_vec_get(vroot, i).value;
              if (!jacl_is_syntax(elem)) {
                vm__set_error(vm, "type error in '%s': commands must be a vec of syntax, element %u is %s",
                              mk_name, i, vm__type_name(elem));
                return VM_RUNTIME_ERROR;
              }
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_BLOCK;
            rsyn->data.block.commands = cmds_vec;
            rsyn->scope_mark = vm->macro_scope_mark;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          }
          DISPATCH();
        }

        /* Introspection path (subops 0-6): pop one syntax value, dispatch. */
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        if (jacl_is_error(val)) {
          result = vm__push(vm, val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_syntax(val)) {
          static const char *names[] = {
            "syntax-kind", "syntax-datum", "syntax-head", "syntax-args",
            "syntax-commands", "syntax-pos", "syntax-str"
          };
          const char *nm = (subop < 7) ? names[subop] : "syntax-op";
          vm__set_error(vm, "type error in '%s': expected syntax object, got %s",
                       nm, vm__type_name(val));
          return VM_RUNTIME_ERROR;
        }
        JaclSyntax *syn = jacl_as_syntax(val);

        switch (subop) {
        case 0: {  /* syntax-kind */
          const char *kn = syntax_kind_name(syn->kind);
          JaclVal s = jacl_string_new(&vm->heap, vm->intern_table,
                                      kn, strlen(kn));
          result = vm__push(vm, s);
          if (result != VM_OK) return result;
          break;
        }
        case 1: {  /* syntax-datum */
          JaclVal out;
          switch ((SyntaxKind)syn->kind) {
          case SYNTAX_LIT_INT:
            out = jacl_i32(syn->data.lit_int.value);
            break;
          case SYNTAX_LIT_FLOAT:
            out = jacl_f32(syn->data.lit_float.value);
            break;
          case SYNTAX_LIT_STRING:
            out = syn->data.lit_string.value;
            break;
          case SYNTAX_VAR_REF:
            out = syn->data.var_ref.name;
            break;
          default:
            vm__set_error(vm,
              "type error in 'syntax-datum': syntax object of kind '%s' has no datum",
              syntax_kind_name(syn->kind));
            return VM_RUNTIME_ERROR;
          }
          result = vm__push(vm, out);
          if (result != VM_OK) return result;
          break;
        }
        case 2: {  /* syntax-head */
          if (syn->kind != SYNTAX_COMMAND) {
            vm__set_error(vm,
              "type error in 'syntax-head': expected command syntax, got '%s'",
              syntax_kind_name(syn->kind));
            return VM_RUNTIME_ERROR;
          }
          result = vm__push(vm, syn->data.command.head);
          if (result != VM_OK) return result;
          break;
        }
        case 3: {  /* syntax-args */
          if (syn->kind != SYNTAX_COMMAND) {
            vm__set_error(vm,
              "type error in 'syntax-args': expected command syntax, got '%s'",
              syntax_kind_name(syn->kind));
            return VM_RUNTIME_ERROR;
          }
          result = vm__push(vm, syn->data.command.args);
          if (result != VM_OK) return result;
          break;
        }
        case 4: {  /* syntax-commands */
          if (syn->kind != SYNTAX_BLOCK) {
            vm__set_error(vm,
              "type error in 'syntax-commands': expected block syntax, got '%s'",
              syntax_kind_name(syn->kind));
            return VM_RUNTIME_ERROR;
          }
          result = vm__push(vm, syn->data.block.commands);
          if (result != VM_OK) return result;
          break;
        }
        case 5: {  /* syntax-pos — map {line, col} */
          jacl_map_node *m = NULL;
          JaclVal k_line = jacl_inline_string("line", 4);
          JaclVal k_col  = jacl_inline_string("col", 3);
          m = jacl_map_set(m, k_line, jacl_i32((int32_t)syn->pos_line));
          m = jacl_map_set(m, k_col,  jacl_i32((int32_t)syn->pos_col));
          result = vm__push(vm, jacl_map_ptr(m));
          if (result != VM_OK) return result;
          break;
        }
        case 6: {  /* syntax->string — pretty-printed */
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
          vm__fmt_value(&fmt, val);
          JaclVal s = jacl_string_new(&vm->heap, vm->intern_table,
                                       fmt.data, fmt.len);
          result = vm__push(vm, s);
          if (result != VM_OK) return result;
          break;
        }
        default:
          vm__set_error(vm, "unknown OP_SYNTAX_OP subop %u", subop);
          return VM_RUNTIME_ERROR;
        }
        DISPATCH();
      }

      CASE(OP_INTERPRET): {
        /* Read arity byte: 1 = [interpret $src], 2 = [interpret $prelude $src] */
        uint8_t interp_arity = *vm->ip++;

        /* Pop source string (always top of stack). */
        JaclVal src_val;
        result = vm__pop(vm, &src_val);
        if (result != VM_OK) return result;

        /* Pop prelude map for 2-arg form. */
        JaclVal prelude_val = JACL_NIL;
        if (interp_arity == 2) {
          result = vm__pop(vm, &prelude_val);
          if (result != VM_OK) return result;
        }

        /* Error passthrough */
        if (jacl_is_error(src_val)) {
          result = vm__push(vm, src_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (interp_arity == 2 && jacl_is_error(prelude_val)) {
          result = vm__push(vm, prelude_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        /* Type-check: source must be string — return error value, not exception */
        if (!jacl_is_string(src_val)) {
          vm__set_error(vm, "interpret: expected string, got %s",
                        vm__type_name(src_val));
          result = vm__push(vm, jacl_set_error(JACL_NIL));
          if (result != VM_OK) return result;
          DISPATCH();
        }

        /* Type-check: prelude must be map or nil — return error value, not exception */
        if (interp_arity == 2 && !jacl_is_nil(prelude_val) && !jacl_is_map(prelude_val)) {
          vm__set_error(vm, "interpret: expected map for prelude, got %s",
                        vm__type_name(prelude_val));
          result = vm__push(vm, jacl_set_error(JACL_NIL));
          if (result != VM_OK) return result;
          DISPATCH();
        }

        /* Extract source bytes into arena. */
        uint32_t slen = jacl_string_byte_len(src_val);
        char *src_buf = (char *)arena_alloc(vm->arena, slen + 1);
        jacl_string_data(src_val, src_buf, slen + 1);
        src_buf[slen] = '\0';

        /* For 2-arg form: populate VM env with prelude entries so
         * OP_GET_GLOBAL resolves them at runtime. Save env.count for
         * cleanup after execution. */
        uint32_t saved_env_count = vm->env.count;
        if (interp_arity == 2 && jacl_is_map(prelude_val)) {
          gc__current_heap = &vm->heap;
          jacl_map_node *pmap = (jacl_map_node *)jacl_as_ptr(prelude_val);
          jacl_map_iter pit = jacl_map_iter_init(pmap);
          jacl_map_iter_result pir;
          for (;;) {
            pir = jacl_map_next_leaf(&pit);
            if (!pir.item) break;
            JaclVal mkey = jacl_map_key_from_leaf(pir.item);
            JaclVal mval = jacl_map_value_from_leaf(pir.item);
            /* Prelude keys must be strings */
            if (!jacl_is_string(mkey)) {
              vm->error_message = "interpret: prelude map key must be a string";
              result = vm__push(vm, jacl_set_error(JACL_NIL));
              if (result != VM_OK) return result;
              goto interpret_done;
            }
            /* Derive env key matching compiler's representation:
             * ≤7 bytes → jacl_inline_string, >7 → jacl_intern */
            uint32_t klen = jacl_string_byte_len(mkey);
            if (klen == 0 || klen > 128) {
              vm->error_message = klen == 0
                ? "interpret: prelude map key cannot be empty"
                : "interpret: prelude map key exceeds 128-byte limit";
              result = vm__push(vm, jacl_set_error(JACL_NIL));
              if (result != VM_OK) return result;
              goto interpret_done;
            }
            char kbuf[128];
            jacl_string_data(mkey, kbuf, sizeof(kbuf));
            JaclVal env_key;
            if (klen <= 7) {
              env_key = jacl_inline_string(kbuf, klen);
            } else {
              env_key = jacl_intern(&vm->heap, vm->intern_table, kbuf, klen);
            }
            vm__env_set(vm, env_key, mval);
          }
        }

        /* Compile source into a closure on the parent VM's heap (in-place).
         * Pass prelude_val so the compiler enforces closed-world names. */
        ExpandState iexpand;
        memset(&iexpand, 0, sizeof(iexpand));
        JaclError ierr;
        JaclVal compile_prelude = (interp_arity == 2) ? prelude_val : JACL_NIL;
        JaclVal closure_val = source_to_closure_in_place(
            src_buf, slen, vm->arena, &vm->heap,
            vm->intern_table, &iexpand, &ierr, compile_prelude);

        if (ierr.kind != JACL_ERROR_NONE) {
          /* Compile/parse error → restore env and push error value. */
          vm->env.count = saved_env_count;
          vm->error_message = ierr.message ? ierr.message
                                           : "interpret error";
          result = vm__push(vm, jacl_set_error(JACL_NIL));
          if (result != VM_OK) return result;
          DISPATCH();
        }

        /* Execute the compiled closure on the parent VM. */
        JaclClosure *icl = jacl_as_closure(closure_val);

        /* Save caller state. */
        uint32_t saved_stack_top   = vm->stack_top;
        uint32_t saved_frame_count = vm->frame_count;
        uint8_t *saved_ip          = vm->ip;
        BytecodeChunk *saved_chunk = vm->chunk;
        BytecodeChunk *saved_top   = vm->top_chunk;

        /* Push dummy callee slot so OP_RETURN / OP_CHECK_ERROR
           frame-unwinding writes into a safe position. */
        result = vm__push(vm, closure_val);
        if (result != VM_OK) return result;

        if (vm->frame_count >= VM_FRAMES_MAX) {
          vm->stack_top = saved_stack_top;
          vm->env.count = saved_env_count;
          vm__set_frame_overflow(vm);
          return VM_RUNTIME_ERROR;
        }

        JaclClosure interpret_wrapper;
        memset(&interpret_wrapper, 0, sizeof(interpret_wrapper));
        interpret_wrapper.chunk = icl->chunk;

        CallFrame *ifrm = &vm->frames[vm->frame_count++];
        ifrm->closure    = &interpret_wrapper;
        ifrm->return_ip  = NULL;
        ifrm->stack_base = vm->stack_top;
        ifrm->chunk      = &icl->chunk;

        vm->ip        = icl->chunk.code;
        vm->chunk     = &icl->chunk;
        vm->top_chunk = &icl->chunk;
        vm->error_message = NULL;

        VMResult inner = vm__run(vm, saved_frame_count);

        JaclVal out;
        if (inner != VM_OK) {
          /* Runtime error during interpreted execution. */
          out = jacl_set_error(JACL_NIL);
          /* vm->error_message already set by vm__run */
        } else {
          /* Success — result is on top of stack. */
          out = (vm->stack_top > saved_stack_top)
              ? vm->stack[vm->stack_top - 1]
              : JACL_NIL;
        }

        /* Restore caller state (including env for sandbox cleanup). */
        vm->stack_top   = saved_stack_top;
        vm->frame_count = saved_frame_count;
        vm->ip          = saved_ip;
        vm->chunk       = saved_chunk;
        vm->top_chunk   = saved_top;
        vm->env.count   = saved_env_count;
        frame = &vm->frames[vm->frame_count - 1];

        result = vm__push(vm, out);
        if (result != VM_OK) return result;
interpret_done:
        DISPATCH();
      }

      /* US-004: [interpret-prelude] — returns the default permissive prelude
       * map containing an entry for every non-core builtin.  Core language
       * forms (arithmetic, comparison, control flow, binding, destructuring,
       * immutable data ops, vec/map/set ops, string ops, error handling,
       * type checks, syntax-quote introspection) are NOT included.
       *
       * Non-core / capability-sensitive builtins (the default key list):
       *   print, interpret, interpret-prelude,
       *   spawn, await, parallel, race, yield,
       *   make-syntax, syntax-error,
       *   box, atom, deref, reset, swap,
       *   lines, stream_next,
       *   exec, signal, cancel,
       *   read-file, write-file, append-file
       */
      CASE(OP_INTERPRET_PRELUDE): {
        gc__current_heap = &vm->heap;
        jacl_map_node *m = NULL;

        /* Use shared non-core list from compiler.c.
         * Values are native fn refs — callable first-class function references
         * that the compiler can recognize for direct opcode emission. */
        for (int i = 0; jacl_non_core_builtins[i]; i++) {
          const char *name = jacl_non_core_builtins[i];
          uint32_t len = (uint32_t)strlen(name);
          JaclVal key;
          if (len <= 7) {
            key = jacl_inline_string(name, len);
          } else {
            key = jacl_intern(&vm->heap, vm->intern_table, name, len);
          }
          /* Native fn ref with index i — enables compile-time opcode emission */
          m = jacl_map_set(m, key, jacl_native_fn((uint32_t)i));
        }

        result = vm__push(vm, jacl_map_ptr(m));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      /* Unified OP_EXEC with flags byte dispatch
       * Flags: EXEC_FLAG_FULL=0x01, EXEC_FLAG_STDIN=0x02, EXEC_FLAG_BG=0x04, EXEC_FLAG_PIPE=0x08
       * If EXEC_FLAG_PIPE, next byte is command count.
       */
      CASE(OP_EXEC): {
        uint8_t flags = vm__read_byte(vm);

        /* === PIPE MODE (flags & 0x08) === */
        if (flags & EXEC_FLAG_PIPE) {
          uint8_t cmd_count = vm__read_byte(vm);
          if (cmd_count < 2) {
            vm__set_error(vm, "exec pipe requires at least 2 commands");
            return VM_RUNTIME_ERROR;
          }

          /* Pop command vectors from stack (last command on top) */
          JaclVal* cmd_vecs = (JaclVal*)arena_alloc(vm->arena, cmd_count * sizeof(JaclVal));
          for (int i = cmd_count - 1; i >= 0; i--) {
            result = vm__pop(vm, &cmd_vecs[i]);
            if (result != VM_OK) return result;
            if (!jacl_is_vector(cmd_vecs[i])) {
              vm__set_error(vm, "exec pipe: command %d must be a vector", i);
              return VM_RUNTIME_ERROR;
            }
          }

          /* Build command string arrays for each process */
          char*** cmd_argv = (char***)arena_alloc(vm->arena, cmd_count * sizeof(char**));
          for (uint32_t i = 0; i < cmd_count; i++) {
            jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(cmd_vecs[i]);
            uint32_t argc = jacl_vec_count(vec);
            if (argc == 0) {
              vm__set_error(vm, "exec pipe: command %d has no arguments", (int)i);
              return VM_RUNTIME_ERROR;
            }
            cmd_argv[i] = (char**)arena_alloc(vm->arena, (argc + 1) * sizeof(char*));
            for (uint32_t j = 0; j < argc; j++) {
              jacl_vec_get_result gr = jacl_vec_get(vec, j);
              JaclVal arg_val = gr.value;
              if (!jacl_is_string(arg_val)) {
                vm__set_error(vm, "exec pipe: command %d arg %d must be string", (int)i, (int)j);
                return VM_RUNTIME_ERROR;
              }
              uint32_t arg_len = jacl_string_byte_len(arg_val);
              char* arg_buf = (char*)arena_alloc(vm->arena, arg_len + 1);
              jacl_string_data(arg_val, arg_buf, arg_len + 1);
              cmd_argv[i][j] = arg_buf;
            }
            cmd_argv[i][argc] = NULL;
          }

          /* Create pipes */
          int (*pipes)[2] = (int(*)[2])arena_alloc(vm->arena, cmd_count * sizeof(int[2]));
          for (uint32_t i = 0; i < cmd_count; i++) {
            if (pipe(pipes[i]) < 0) {
              vm__set_error(vm, "exec pipe: failed to create pipe");
              return VM_RUNTIME_ERROR;
            }
          }

          char stderr_path[64];
          snprintf(stderr_path, sizeof(stderr_path), "/tmp/jacl_stderr_%d_%lu",
                   (int)getpid(), (unsigned long)time(NULL));

          pid_t* pids = (pid_t*)arena_alloc(vm->arena, cmd_count * sizeof(pid_t));
          for (uint32_t i = 0; i < cmd_count; i++) {
            pids[i] = fork();
            if (pids[i] < 0) {
              vm__set_error(vm, "exec pipe: fork failed");
              return VM_RUNTIME_ERROR;
            }
            if (pids[i] == 0) {
              if (i > 0) dup2(pipes[i-1][0], STDIN_FILENO);
              dup2(pipes[i][1], STDOUT_FILENO);
              if (i == cmd_count - 1) {
                int stderr_fd = open(stderr_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (stderr_fd >= 0) { dup2(stderr_fd, STDERR_FILENO); close(stderr_fd); }
              }
              for (uint32_t j = 0; j < cmd_count; j++) { close(pipes[j][0]); close(pipes[j][1]); }
              execvp(cmd_argv[i][0], cmd_argv[i]);
              _exit(127);
            }
          }

          for (uint32_t i = 0; i < cmd_count; i++) {
            close(pipes[i][1]);
            if (i < cmd_count - 1) close(pipes[i][0]);
          }

          int stdout_fd = pipes[cmd_count - 1][0];
          FILE* fp = fdopen(stdout_fd, "r");
          if (!fp) { close(stdout_fd); vm__set_error(vm, "exec pipe: fdopen failed"); return VM_RUNTIME_ERROR; }

          JaclVal stream_val = jacl_stream(&vm->heap);
          JaclStream* stream = jacl_as_stream(stream_val);
          stream->kind = STREAM_KIND_EXEC_PIPE;
          stream->state = STREAM_PENDING;
          stream->args[0] = (JaclVal)(uintptr_t)fp;
          gc__current_heap = &vm->heap;

          char pid_buf[256];
          int pid_offset = snprintf(pid_buf, sizeof(pid_buf), "%d", (int)cmd_count);
          for (uint32_t i = 0; i < cmd_count; i++) {
            pid_offset += snprintf(pid_buf + pid_offset, sizeof(pid_buf) - pid_offset, ",%d", (int)pids[i]);
          }
          stream->args[1] = jacl_intern(&vm->heap, vm->intern_table, pid_buf, (uint32_t)strlen(pid_buf));
          stream->args[2] = jacl_intern(&vm->heap, vm->intern_table, stderr_path, (uint32_t)strlen(stderr_path));
          stream->args[3] = JACL_NIL;
          stream->arg_count = 4;

          result = vm__push(vm, stream_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        /* For non-PIPE modes, handle stdin first if EXEC_FLAG_STDIN */
        JaclVal stdin_val = JACL_NIL;
        char* stdin_buf = NULL;
        size_t stdin_len = 0;
        if (flags & EXEC_FLAG_STDIN) {
          result = vm__pop(vm, &stdin_val);
          if (result != VM_OK) return result;
        }

        /* Pop args vector */
        JaclVal args_vec;
        result = vm__pop(vm, &args_vec);
        if (result != VM_OK) return result;

        /* === BACKGROUND MODE (flags & 0x04) === */
        if (flags & EXEC_FLAG_BG) {
          if (!jacl_is_vector(args_vec)) {
            vm__set_error(vm, "exec requires a vector of arguments, got %s", vm__type_name(args_vec));
            return VM_RUNTIME_ERROR;
          }

          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(args_vec);
          uint32_t argc = jacl_vec_count(vec);
          if (argc == 0) { vm__set_error(vm, "exec requires at least a command name"); return VM_RUNTIME_ERROR; }

          char** argv = (char**)arena_alloc(vm->arena, sizeof(char*) * (argc + 1));
          for (uint32_t i = 0; i < argc; i++) {
            jacl_vec_get_result gr = jacl_vec_get(vec, i);
            JaclVal arg_val = gr.value;
            if (!jacl_is_string(arg_val)) {
              vm__set_error(vm, "exec argument %d must be a string, got %s", (int)i, vm__type_name(arg_val));
              return VM_RUNTIME_ERROR;
            }
            uint32_t arg_len = jacl_string_byte_len(arg_val);
            char* arg_buf = (char*)arena_alloc(vm->arena, arg_len + 1);
            jacl_string_data(arg_val, arg_buf, arg_len + 1);
            argv[i] = arg_buf;
          }
          argv[argc] = NULL;

          char stdout_path[64], stderr_path[64];
          unsigned long hash = (unsigned long)time(NULL) ^ (unsigned long)(uintptr_t)argv;
          snprintf(stdout_path, sizeof(stdout_path), "/tmp/jacl_bg_out_%d_%lu", (int)getpid(), hash);
          snprintf(stderr_path, sizeof(stderr_path), "/tmp/jacl_bg_err_%d_%lu", (int)getpid(), hash);

          pid_t pid = fork();
          if (pid < 0) { vm__set_error(vm, "fork failed: %s", strerror(errno)); return VM_RUNTIME_ERROR; }

          if (pid == 0) {
            int stdout_fd = open(stdout_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            int stderr_fd = open(stderr_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (stdout_fd >= 0) { dup2(stdout_fd, STDOUT_FILENO); close(stdout_fd); }
            if (stderr_fd >= 0) { dup2(stderr_fd, STDERR_FILENO); close(stderr_fd); }
            execvp(argv[0], argv);
            fprintf(stderr, "execvp failed: %s\n", strerror(errno));
            _exit(127);
          }

          gc__current_heap = &vm->heap;
          jacl_map_node* m = NULL;
          m = jacl_map_set(m, jacl_intern(&vm->heap, vm->intern_table, "pid", 3), jacl_i32((int32_t)pid));
          m = jacl_map_set(m, jacl_intern(&vm->heap, vm->intern_table, "_is_job", 7), JACL_TRUE);
          m = jacl_map_set(m, jacl_intern(&vm->heap, vm->intern_table, "_stdout_path", 12),
                          jacl_intern(&vm->heap, vm->intern_table, stdout_path, (uint32_t)strlen(stdout_path)));
          m = jacl_map_set(m, jacl_intern(&vm->heap, vm->intern_table, "_stderr_path", 12),
                          jacl_intern(&vm->heap, vm->intern_table, stderr_path, (uint32_t)strlen(stderr_path)));

          result = vm__push(vm, jacl_map_ptr(m));
          if (result != VM_OK) return result;
          DISPATCH();
        }

        /* === BUILD COMMAND STRING (shared by remaining modes) === */
        char cmd_buf[4096];
        if (!vm__exec_build_cmd(vm, args_vec, cmd_buf, sizeof(cmd_buf))) {
          return VM_RUNTIME_ERROR;
        }

        /* Collect stdin if needed */
        if (flags & EXEC_FLAG_STDIN) {
          int collect_result = vm__exec_collect_stdin(vm, stdin_val, &stdin_buf, &stdin_len);
          if (collect_result == -1) return VM_RUNTIME_ERROR;
          if (collect_result == -2) {
            /* Error value pushed to stack */
            frame = &vm->frames[vm->frame_count - 1];
            DISPATCH();
          }
        }

        char stderr_path[64];
        snprintf(stderr_path, sizeof(stderr_path), "/tmp/jacl_stderr_%d_%lu",
                 (int)getpid(), (unsigned long)time(NULL) ^ (unsigned long)cmd_buf);

        char full_cmd[4256];
        if (flags & EXEC_FLAG_STDIN) {
          char stdin_path[64];
          snprintf(stdin_path, sizeof(stdin_path), "/tmp/jacl_stdin_%d_%lu",
                   (int)getpid(), (unsigned long)time(NULL) ^ (unsigned long)cmd_buf);
          FILE* stdin_fp = fopen(stdin_path, "w");
          if (!stdin_fp) { vm__set_error(vm, "exec: failed to create stdin temp file"); return VM_RUNTIME_ERROR; }
          fwrite(stdin_buf, 1, stdin_len, stdin_fp);
          fclose(stdin_fp);
          snprintf(full_cmd, sizeof(full_cmd), "%s <%s 2>%s", cmd_buf, stdin_path, stderr_path);

          FILE* fp = popen(full_cmd, "r");
          if (!fp) { unlink(stdin_path); unlink(stderr_path); vm__set_error(vm, "exec: failed to spawn process '%s'", cmd_buf); return VM_RUNTIME_ERROR; }

          JaclVal stream_val = jacl_stream(&vm->heap);
          JaclStream* stream = jacl_as_stream(stream_val);
          stream->kind = STREAM_KIND_EXEC;
          stream->state = STREAM_PENDING;
          stream->args[0] = (JaclVal)(uintptr_t)fp;
          gc__current_heap = &vm->heap;
          stream->args[1] = jacl_intern(&vm->heap, vm->intern_table, cmd_buf, (uint32_t)strlen(cmd_buf));
          stream->args[2] = jacl_intern(&vm->heap, vm->intern_table, stderr_path, (uint32_t)strlen(stderr_path));
          stream->args[3] = jacl_intern(&vm->heap, vm->intern_table, stdin_path, (uint32_t)strlen(stdin_path));
          stream->arg_count = 4;

          result = vm__push(vm, stream_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        snprintf(full_cmd, sizeof(full_cmd), "%s 2>%s", cmd_buf, stderr_path);
        FILE* fp = popen(full_cmd, "r");
        if (!fp) { unlink(stderr_path); vm__set_error(vm, "exec: failed to spawn process '%s'", cmd_buf); return VM_RUNTIME_ERROR; }

        /* === FULL MODE (flags & 0x01) === */
        if (flags & EXEC_FLAG_FULL) {
          /* Read all stdout */
          char* stdout_buf = NULL;
          size_t stdout_len = 0;
          size_t stdout_cap = 0;
          char read_buf[4096];
          size_t n;
          while ((n = fread(read_buf, 1, sizeof(read_buf), fp)) > 0) {
            if (stdout_len + n > stdout_cap) {
              size_t new_cap = stdout_cap == 0 ? 4096 : stdout_cap * 2;
              while (new_cap < stdout_len + n) new_cap *= 2;
              char* new_buf = (char*)arena_alloc(vm->arena, (uint32_t)new_cap);
              if (stdout_buf) memcpy(new_buf, stdout_buf, stdout_len);
              stdout_buf = new_buf;
              stdout_cap = new_cap;
            }
            memcpy(stdout_buf + stdout_len, read_buf, n);
            stdout_len += n;
          }

          int status = pclose(fp);
          int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

          char stderr_buf_data[4096];
          size_t stderr_len = 0;
          FILE* stderr_fp = fopen(stderr_path, "r");
          if (stderr_fp) { stderr_len = fread(stderr_buf_data, 1, sizeof(stderr_buf_data) - 1, stderr_fp); fclose(stderr_fp); }
          stderr_buf_data[stderr_len] = '\0';
          unlink(stderr_path);

          gc__current_heap = &vm->heap;
          JaclVal stdout_str = jacl_intern(&vm->heap, vm->intern_table, stdout_buf ? stdout_buf : "", (uint32_t)stdout_len);
          JaclVal stdout_stream_val = jacl_stream(&vm->heap);
          JaclStream* stdout_stream = jacl_as_stream(stdout_stream_val);
          stdout_stream->kind = STREAM_KIND_EXEC_BUFFER;
          stdout_stream->state = STREAM_PENDING;
          stdout_stream->args[0] = stdout_str;
          stdout_stream->args[1] = jacl_i32(0);
          stdout_stream->arg_count = 2;

          jacl_map_node* result_map = jacl_map_set(NULL, jacl_intern(&vm->heap, vm->intern_table, "stdout", 6), stdout_stream_val);
          result_map = jacl_map_set(result_map, jacl_intern(&vm->heap, vm->intern_table, "stderr", 6),
                                    jacl_intern(&vm->heap, vm->intern_table, stderr_buf_data, (uint32_t)stderr_len));
          result_map = jacl_map_set(result_map, jacl_intern(&vm->heap, vm->intern_table, "exit", 4), jacl_i32(exit_code));

          result = vm__push(vm, jacl_map_ptr(result_map));
          if (result != VM_OK) return result;
          DISPATCH();
        }

        /* === BASIC MODE (flags == 0) === */
        {
          JaclVal stream_val = jacl_stream(&vm->heap);
          JaclStream* stream = jacl_as_stream(stream_val);
          stream->kind = STREAM_KIND_EXEC;
          stream->state = STREAM_PENDING;
          stream->args[0] = (JaclVal)(uintptr_t)fp;
          gc__current_heap = &vm->heap;
          stream->args[1] = jacl_intern(&vm->heap, vm->intern_table, cmd_buf, (uint32_t)strlen(cmd_buf));
          stream->args[2] = jacl_intern(&vm->heap, vm->intern_table, stderr_path, (uint32_t)strlen(stderr_path));
          stream->arg_count = 3;

          result = vm__push(vm, stream_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
      }

      /* US-010: Await job or future result.
       * Stack: [value] -> [result]
       * If value is a Job map (_is_job=true): waitpid, read files, return {stdout, stderr, exit}
       * If value is a resolved future: return result
       * If value is a pending future: error (non-SM context can't suspend)
       */
      CASE(OP_AWAIT_JOB): {
        JaclVal val;
        result = vm__pop(vm, &val);
        if (result != VM_OK) return result;

        /* Check if it's a Job map */
        if (jacl_is_map(val)) {
          jacl_map_node* m = (jacl_map_node*)jacl_as_ptr(val);
          JaclVal marker_key = jacl_intern(&vm->heap, vm->intern_table, "_is_job", 7);
          JaclVal marker_val = jacl_map_get(m, marker_key);

          if (marker_val == JACL_TRUE) {
            /* It's a Job - extract pid and wait */
            JaclVal pid_key = jacl_intern(&vm->heap, vm->intern_table, "pid", 3);
            JaclVal pid_val = jacl_map_get(m, pid_key);
            if (!jacl_is_i32(pid_val)) {
              vm__set_error(vm, "invalid job: missing pid");
              return VM_RUNTIME_ERROR;
            }
            pid_t pid = (pid_t)jacl_as_i32(pid_val);

            /* Wait for process to complete */
            int status;
            pid_t waited = waitpid(pid, &status, 0);
            if (waited < 0) {
              vm__set_error(vm, "waitpid failed: %s", strerror(errno));
              return VM_RUNTIME_ERROR;
            }

            int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

            /* Read stdout from temp file */
            JaclVal stdout_key = jacl_intern(&vm->heap, vm->intern_table, "_stdout_path", 12);
            JaclVal stdout_path_val = jacl_map_get(m, stdout_key);
            char stdout_path[128] = {0};
            if (jacl_is_string(stdout_path_val)) {
              uint32_t len = jacl_string_byte_len(stdout_path_val);
              if (len < sizeof(stdout_path)) {
                jacl_string_data(stdout_path_val, stdout_path, len + 1);
              }
            }

            char* stdout_buf = NULL;
            size_t stdout_len = 0;
            if (stdout_path[0]) {
              FILE* fp = fopen(stdout_path, "r");
              if (fp) {
                fseek(fp, 0, SEEK_END);
                stdout_len = (size_t)ftell(fp);
                fseek(fp, 0, SEEK_SET);
                stdout_buf = (char*)arena_alloc(vm->arena, (uint32_t)(stdout_len + 1));
                fread(stdout_buf, 1, stdout_len, fp);
                stdout_buf[stdout_len] = '\0';
                fclose(fp);
              }
              unlink(stdout_path);
            }

            /* Read stderr from temp file */
            JaclVal stderr_key = jacl_intern(&vm->heap, vm->intern_table, "_stderr_path", 12);
            JaclVal stderr_path_val = jacl_map_get(m, stderr_key);
            char stderr_path[128] = {0};
            if (jacl_is_string(stderr_path_val)) {
              uint32_t len = jacl_string_byte_len(stderr_path_val);
              if (len < sizeof(stderr_path)) {
                jacl_string_data(stderr_path_val, stderr_path, len + 1);
              }
            }

            char* stderr_buf = NULL;
            size_t stderr_len = 0;
            if (stderr_path[0]) {
              FILE* fp = fopen(stderr_path, "r");
              if (fp) {
                fseek(fp, 0, SEEK_END);
                stderr_len = (size_t)ftell(fp);
                fseek(fp, 0, SEEK_SET);
                stderr_buf = (char*)arena_alloc(vm->arena, (uint32_t)(stderr_len + 1));
                fread(stderr_buf, 1, stderr_len, fp);
                stderr_buf[stderr_len] = '\0';
                fclose(fp);
              }
              unlink(stderr_path);
            }

            /* Create result map {stdout, stderr, exit} */
            gc__current_heap = &vm->heap;
            jacl_map_node* result_map = NULL;

            /* Create stdout as a stream (like OP_EXEC_FULL) */
            JaclVal stdout_stream_val = jacl_stream(&vm->heap);
            JaclStream* stdout_stream = jacl_as_stream(stdout_stream_val);
            stdout_stream->kind = STREAM_KIND_EXEC_BUFFER;
            stdout_stream->state = STREAM_PENDING;
            if (stdout_buf && stdout_len > 0) {
              stdout_stream->args[0] = jacl_intern(&vm->heap, vm->intern_table,
                                                   stdout_buf, (uint32_t)stdout_len);
            } else {
              stdout_stream->args[0] = jacl_intern(&vm->heap, vm->intern_table, "", 0);
            }
            stdout_stream->args[1] = jacl_i32(0); /* position index */
            stdout_stream->arg_count = 2;

            JaclVal out_key = jacl_intern(&vm->heap, vm->intern_table, "stdout", 6);
            result_map = jacl_map_set(result_map, out_key, stdout_stream_val);

            JaclVal err_key = jacl_intern(&vm->heap, vm->intern_table, "stderr", 6);
            JaclVal err_val = stderr_buf && stderr_len > 0
                ? jacl_intern(&vm->heap, vm->intern_table, stderr_buf, (uint32_t)stderr_len)
                : jacl_intern(&vm->heap, vm->intern_table, "", 0);
            result_map = jacl_map_set(result_map, err_key, err_val);

            JaclVal exit_key = jacl_intern(&vm->heap, vm->intern_table, "exit", 4);
            result_map = jacl_map_set(result_map, exit_key, jacl_i32(exit_code));

            result = vm__push(vm, jacl_map_ptr(result_map));
            if (result != VM_OK) return result;
            DISPATCH();
          }
        }

        /* Check if it's a Future */
        if (jacl_is_future(val)) {
          JaclFuture* fut = jacl_as_future(val);
          uint32_t fstate = ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_ACQUIRE);

          if (fstate == FUTURE_RESOLVED) {
            result = vm__push(vm, (JaclVal)fut->result);
            if (result != VM_OK) return result;
            DISPATCH();
          } else if (fstate == FUTURE_ERROR) {
            JaclVal err = (JaclVal)fut->result;
            result = vm__push(vm, err | JACL_FLAG_ERROR);
            if (result != VM_OK) return result;
            DISPATCH();
          } else {
            vm__set_error(vm, "cannot await pending future outside async context");
            return VM_RUNTIME_ERROR;
          }
        }

        vm__set_error(vm, "await requires a Job or Future, got %s",
                     vm__type_name(val));
        return VM_RUNTIME_ERROR;
      }

      /* US-011: Send signal to a background job.
       * Stack: [job_map, signal_name] -> [bool]
       * Returns $true if signal sent, $false if process already exited.
       */
      CASE(OP_SIGNAL): {
        JaclVal sig_name_val;
        result = vm__pop(vm, &sig_name_val);
        if (result != VM_OK) return result;

        JaclVal job_val;
        result = vm__pop(vm, &job_val);
        if (result != VM_OK) return result;

        /* Validate signal name is a string */
        if (!jacl_is_string(sig_name_val)) {
          vm__set_error(vm, "signal requires a signal name string, got %s",
                       vm__type_name(sig_name_val));
          return VM_RUNTIME_ERROR;
        }

        /* Validate job is a map with _is_job marker */
        if (!jacl_is_map(job_val)) {
          vm__set_error(vm, "signal requires a Job map, got %s",
                       vm__type_name(job_val));
          return VM_RUNTIME_ERROR;
        }

        jacl_map_node* m = (jacl_map_node*)jacl_as_ptr(job_val);
        gc__current_heap = &vm->heap;
        JaclVal marker_key = jacl_intern(&vm->heap, vm->intern_table, "_is_job", 7);
        JaclVal marker_val = jacl_map_get(m, marker_key);

        if (marker_val != JACL_TRUE) {
          vm__set_error(vm, "signal requires a Job map (missing _is_job marker)");
          return VM_RUNTIME_ERROR;
        }

        /* Extract PID */
        JaclVal pid_key = jacl_intern(&vm->heap, vm->intern_table, "pid", 3);
        JaclVal pid_val = jacl_map_get(m, pid_key);
        if (!jacl_is_i32(pid_val)) {
          vm__set_error(vm, "invalid job: missing pid");
          return VM_RUNTIME_ERROR;
        }
        pid_t pid = (pid_t)jacl_as_i32(pid_val);

        /* Parse signal name */
        char sig_name[32];
        uint32_t sig_len = jacl_string_byte_len(sig_name_val);
        if (sig_len >= sizeof(sig_name)) {
          vm__set_error(vm, "signal name too long");
          return VM_RUNTIME_ERROR;
        }
        jacl_string_data(sig_name_val, sig_name, sig_len + 1);
        sig_name[sig_len] = '\0';

        int signum = -1;
        if (strcmp(sig_name, "SIGTERM") == 0) signum = SIGTERM;
        else if (strcmp(sig_name, "SIGKILL") == 0) signum = SIGKILL;
        else if (strcmp(sig_name, "SIGINT") == 0) signum = SIGINT;
        else if (strcmp(sig_name, "SIGHUP") == 0) signum = SIGHUP;
        else if (strcmp(sig_name, "SIGUSR1") == 0) signum = SIGUSR1;
        else if (strcmp(sig_name, "SIGUSR2") == 0) signum = SIGUSR2;
        else {
          vm__set_error(vm, "unknown signal: %s", sig_name);
          return VM_RUNTIME_ERROR;
        }

        /* Check if process still exists and send signal */
        int kill_result = kill(pid, signum);
        if (kill_result == 0) {
          /* Signal sent successfully */
          result = vm__push(vm, JACL_TRUE);
        } else if (errno == ESRCH) {
          /* Process already exited - return $false (no-op) */
          result = vm__push(vm, JACL_FALSE);
        } else {
          vm__set_error(vm, "kill failed: %s", strerror(errno));
          return VM_RUNTIME_ERROR;
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_HALT): {
        return VM_OK;
      }

      CASE(OP_GET_CTX): {
        result = vm__push(vm, vm->ctx);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_CTX_FORK): {
        if (!vm->ctx_pool || vm->ctx == JACL_NIL) {
          vm__set_error(vm, "with-ctx: no ctx available");
          return VM_RUNTIME_ERROR;
        }
        if (vm->saved_ctx_count >= 8) {
          vm__set_error(vm, "with-ctx: nesting too deep (max 8)");
          return VM_RUNTIME_ERROR;
        }
        JaclVal old_ctx = ctx_fork(vm, vm->ctx);
        if (vm->ctx == old_ctx) {
          vm__set_error(vm, "with-ctx: failed to allocate ctx");
          return VM_RUNTIME_ERROR;
        }
        /* RELEASE: pairs with gc_enumerate_roots' ACQUIRE on saved_ctx[i]. */
        ATOMIC_STORE_EXPLICIT(
            (uint64_t*)&vm->saved_ctx[vm->saved_ctx_count++],
            (uint64_t)old_ctx, MEM_RELEASE);
        gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, old_ctx, vm->ctx);
        DISPATCH();
      }

      CASE(OP_CTX_RESTORE): {
        /* Pop saved ctx from VM save stack, free forked ctx to pool, restore */
        if (vm->saved_ctx_count == 0) {
          vm__set_error(vm, "ctx restore: no saved ctx");
          return VM_RUNTIME_ERROR;
        }
        ctx_unfork(vm, vm->saved_ctx[--vm->saved_ctx_count]);
        DISPATCH();
      }

      CASE(OP_SET_CTX): {
        /* Pop value from stack and store in vm->ctx (used by SM resume) */
        JaclVal new_ctx;
        result = vm__pop(vm, &new_ctx);
        if (result != VM_OK) return result;
        gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, vm->ctx, new_ctx);
        /* RELEASE: see ctx_fork. */
        ATOMIC_STORE_EXPLICIT((uint64_t*)&vm->ctx,
                              (uint64_t)new_ctx, MEM_RELEASE);
        DISPATCH();
      }

      CASE(OP_RANGE): {
        /* Create a range stream: pop end, pop start, push stream.
         * Next byte: 0 = exclusive (..<), 1 = inclusive (..=) */
        uint8_t inclusive = *vm->ip++;
        JaclVal end_val, start_val;
        result = vm__pop(vm, &end_val);
        if (result != VM_OK) return result;
        result = vm__pop(vm, &start_val);
        if (result != VM_OK) return result;

        /* Coerce to i64 for range bounds */
        int64_t start_i, end_i;
        if (jacl_is_i32(start_val)) start_i = (int64_t)jacl_as_i32(start_val);
        else if (jacl_is_i64(start_val)) start_i = jacl_as_i64(start_val);
        else {
          vm__set_error(vm, "range start must be an integer, got %s",
                       vm__type_name(start_val));
          return VM_RUNTIME_ERROR;
        }
        if (jacl_is_i32(end_val)) end_i = (int64_t)jacl_as_i32(end_val);
        else if (jacl_is_i64(end_val)) end_i = jacl_as_i64(end_val);
        else {
          vm__set_error(vm, "range end must be an integer, got %s",
                       vm__type_name(end_val));
          return VM_RUNTIME_ERROR;
        }

        JaclVal range_stream = jacl_stream(&vm->heap);
        JaclStream* rs = jacl_as_stream(range_stream);
        rs->kind      = STREAM_KIND_RANGE;
        rs->elem_idx  = JACL_SCALAR_TYPE_IDX(TYPE_I64);  /* range yields i64 */
        rs->args[0]   = jacl_i64(&vm->heap, start_i);  /* current value */
        rs->args[1]   = jacl_i64(&vm->heap, end_i);    /* end bound */
        rs->args[2]   = jacl_i32((int32_t)inclusive); /* 0=excl, 1=incl */
        rs->arg_count = 3;
        result = vm__push(vm, range_stream);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      /* --- Typed vector operations --- */

      CASE(OP_TYPED_VEC): {
        uint16_t type_idx = vm__read_u16(vm);
        uint8_t count = vm__read_byte(vm);

        gc__current_heap = &vm->heap;

        /* Scalar element type (sentinel range 0xFF00..0xFFFF): width=1,
         * each element is a single JaclVal slot popped via vm__pop. */
        if (type_idx >= 0xFF00) {
          jacl_typed_vec_root* tvec = jacl_typed_vec_empty_strided(1);
          JaclVal scratch[256];
          if (count > 256) {
            vm__set_error(vm, "typed-vec literal too large");
            return VM_RUNTIME_ERROR;
          }
          for (int32_t i = (int32_t)count - 1; i >= 0; i--) {
            result = vm__pop(vm, &scratch[i]);
            if (result != VM_OK) return result;
          }
          for (uint8_t i = 0; i < count; i++) {
            tvec = jacl_typed_vec_push_back_wide(tvec, &scratch[i]);
          }
          result = vm__push(vm, jacl_typed_vector_ptr(tvec));
          if (result != VM_OK) return result;
          DISPATCH();
        }

        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = vm__struct_width(sdef);

        jacl_typed_vec_root* tvec = jacl_typed_vec_empty_strided(width);

        /* Pop elements right-to-left into a buffer, then push left-to-right. */
        JaclVal scratch[VM_MAX_STRUCT_SLOTS * 256];
        if ((size_t)count * width > sizeof(scratch) / sizeof(JaclVal)) {
          vm__set_error(vm, "typed-vec literal too large");
          return VM_RUNTIME_ERROR;
        }
        for (int32_t i = (int32_t)count - 1; i >= 0; i--) {
          vm__pop_struct(vm, type_idx, &scratch[(uint32_t)i * width]);
        }
        for (uint8_t i = 0; i < count; i++) {
          tvec = jacl_typed_vec_push_back_wide(tvec, &scratch[i * width]);
        }
        result = vm__push(vm, jacl_typed_vector_ptr(tvec));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_VEC_GET_INLINE): {
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal idx_val, tvec_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &tvec_val); if (result != VM_OK) return result;

        if (!jacl_is_i32(idx_val)) {
          vm__set_error(vm, "vec-get: expected i32 index, got %s",
                       vm__type_name(idx_val));
          return VM_RUNTIME_ERROR;
        }

        JACL_ASSERT_TAG(tvec_val, jacl_is_typed_vector);
        jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(tvec_val);
        int32_t idx = jacl_as_i32(idx_val);

        if (idx < 0 || (uint32_t)idx >= jacl_typed_vec_count(tvec)) {
          vm__set_error(vm, "vec-get: index %d out of bounds (length %u)",
                       idx, jacl_typed_vec_count(tvec));
          return VM_RUNTIME_ERROR;
        }

        /* Scalar element type — single-slot copy. */
        if (type_idx >= 0xFF00) {
          const JaclVal* ptr = jacl_typed_vec_get_ptr(tvec, (uint32_t)idx);
          result = vm__push(vm, *ptr);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = vm__struct_width(sdef);

        const JaclVal* ptr = jacl_typed_vec_get_ptr(tvec, (uint32_t)idx);
        /* Push width inline slots directly from RRB leaf data */
        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_operand_overflow(vm, "typed vec get inline");
          return VM_RUNTIME_ERROR;
        }
        memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], ptr, sdef->total_size);
        for (uint32_t si = 0; si < width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        }
        vm->stack_top += width;
        DISPATCH();
      }

      CASE(OP_STREAM_NEXT_INLINE): {
        /* Typed pull of one STRUCT element (multi-slot stream channel).
         * Emitted only by the for-loop over a struct-element stream — the
         * loop knows the element type statically. On a value: push the
         * struct's width inline slots (bitmap-marked raw value bytes). On
         * exhaustion: push a single nil (the loop's exhausted arm pops 1),
         * mirroring OP_STREAM_NEXT. */
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal stream_val;
        result = vm__pop(vm, &stream_val); if (result != VM_OK) return result;
        if (jacl_is_error(stream_val)) {
          result = vm__push(vm, stream_val);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        if (!jacl_is_stream(stream_val)) {
          vm__set_error(vm, "for: expected stream, got %s",
                        vm__type_name(stream_val));
          return VM_RUNTIME_ERROR;
        }
        JaclVal buf[VM_MAX_STRUCT_SLOTS];
        StreamPullResult pr = vm__pull_stream_one(vm, stream_val, buf);
        frame = &vm->frames[vm->frame_count - 1];
        if (pr == STREAM_PULL_ERROR) return VM_RUNTIME_ERROR;
        if (pr == STREAM_PULL_EXHAUSTED) {
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        StructTypeDef* sni_sdef = vm->struct_registry->defs[type_idx];
        uint32_t sni_width = vm__struct_width(sni_sdef);
        if (vm->stack_top + sni_width > VM_STACK_MAX) {
          vm__set_operand_overflow(vm, "stream next inline");
          return VM_RUNTIME_ERROR;
        }
        memcpy(&vm->stack[vm->stack_top], buf, sni_width * sizeof(JaclVal));
        for (uint32_t si = 0; si < sni_width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        }
        vm->stack_top += sni_width;
        DISPATCH();
      }

      CASE(OP_INLINE_TO_LOCAL): {
        uint8_t base_slot = vm__read_byte(vm);
        uint16_t type_idx = vm__read_u16(vm);
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = vm__struct_width(sdef);
        uint32_t abs_base = frame->stack_base + base_slot;
        /* Copy width inline slots from TOS to local range */
        memcpy(&vm->stack[abs_base], &vm->stack[vm->stack_top - width],
               width * sizeof(JaclVal));
        for (uint32_t si = 0; si < width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, abs_base + si);
        }
        /* Pop the width slots from stack */
        for (uint32_t si = vm->stack_top - width; si < vm->stack_top; si++) {
          BITMAP_CLR(vm->inline_slot_bitmap, si);
        }
        vm->stack_top -= width;
        DISPATCH();
      }

      CASE(OP_TYPED_VEC_PUSH): {
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal slots[VM_MAX_STRUCT_SLOTS];
        if (type_idx >= 0xFF00) {
          /* Scalar element: pop one slot */
          result = vm__pop(vm, &slots[0]);
          if (result != VM_OK) return result;
        } else {
          vm__pop_struct(vm, type_idx, slots);
        }
        JaclVal tvec_val;
        result = vm__pop(vm, &tvec_val); if (result != VM_OK) return result;

        JACL_ASSERT_TAG(tvec_val, jacl_is_typed_vector);
        jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(tvec_val);
        gc__current_heap = &vm->heap;
        jacl_typed_vec_root* new_tvec = jacl_typed_vec_push_back_wide(tvec, slots);
        result = vm__push(vm, jacl_typed_vector_ptr(new_tvec));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_VEC_SET): {
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal slots[VM_MAX_STRUCT_SLOTS];
        if (type_idx >= 0xFF00) {
          result = vm__pop(vm, &slots[0]);
          if (result != VM_OK) return result;
        } else {
          vm__pop_struct(vm, type_idx, slots);
        }
        JaclVal idx_val, tvec_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &tvec_val); if (result != VM_OK) return result;

        if (!jacl_is_i32(idx_val)) {
          vm__set_error(vm, "vec-set: expected i32 index, got %s",
                       vm__type_name(idx_val));
          return VM_RUNTIME_ERROR;
        }

        JACL_ASSERT_TAG(tvec_val, jacl_is_typed_vector);
        jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(tvec_val);
        int32_t idx = jacl_as_i32(idx_val);

        gc__current_heap = &vm->heap;
        jacl_typed_vec_root* new_tvec = jacl_typed_vec_set_wide(tvec, (uint32_t)idx, slots);
        if (!new_tvec) {
          vm__set_error(vm, "vec-set: index %d out of bounds", idx);
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, jacl_typed_vector_ptr(new_tvec));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_VEC_LEN): {
        JaclVal tvec_val;
        result = vm__pop(vm, &tvec_val); if (result != VM_OK) return result;
        JACL_ASSERT_TAG(tvec_val, jacl_is_typed_vector);
        jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(tvec_val);
        result = vm__push(vm, jacl_i32((int32_t)jacl_typed_vec_count(tvec)));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_VEC_CONCAT): {
        uint16_t type_idx = vm__read_u16(vm);
        (void)type_idx;
        JaclVal b_val, a_val;
        result = vm__pop(vm, &b_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &a_val); if (result != VM_OK) return result;
        JACL_ASSERT_TAG(a_val, jacl_is_typed_vector);
        JACL_ASSERT_TAG(b_val, jacl_is_typed_vector);
        jacl_typed_vec_root* a = (jacl_typed_vec_root*)jacl_as_ptr(a_val);
        jacl_typed_vec_root* b = (jacl_typed_vec_root*)jacl_as_ptr(b_val);
        gc__current_heap = &vm->heap;
        jacl_typed_vec_root* new_tvec = jacl_typed_vec_concat(a, b);
        result = vm__push(vm, jacl_typed_vector_ptr(new_tvec));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_VEC_SLICE): {
        uint16_t type_idx = vm__read_u16(vm);
        (void)type_idx;
        JaclVal end_val, start_val, tvec_val;
        result = vm__pop(vm, &end_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &start_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &tvec_val); if (result != VM_OK) return result;
        if (!jacl_is_i32(start_val)) {
          vm__set_error(vm, "vec-slice: expected i32 start, got %s", vm__type_name(start_val));
          return VM_RUNTIME_ERROR;
        }
        if (!jacl_is_i32(end_val)) {
          vm__set_error(vm, "vec-slice: expected i32 end, got %s", vm__type_name(end_val));
          return VM_RUNTIME_ERROR;
        }
        JACL_ASSERT_TAG(tvec_val, jacl_is_typed_vector);
        jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(tvec_val);
        int32_t start = jacl_as_i32(start_val);
        int32_t end = jacl_as_i32(end_val);
        uint32_t count = jacl_typed_vec_count(tvec);
        if (start < 0) start = 0;
        if (end < 0) end = 0;
        if ((uint32_t)start > count) start = (int32_t)count;
        if ((uint32_t)end > count) end = (int32_t)count;
        if (start > end) start = end;
        gc__current_heap = &vm->heap;
        jacl_typed_vec_root* new_tvec = jacl_typed_vec_slice(tvec, (uint32_t)start, (uint32_t)end);
        result = vm__push(vm, jacl_typed_vector_ptr(new_tvec));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      /* ===== Typed HOF opcodes ===== */

      CASE(OP_TYPED_EACH): {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal closure_val, coll_val;
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &coll_val); if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) { result = vm__push(vm, coll_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(closure_val)) { result = vm__push(vm, closure_val); if (result != VM_OK) return result; DISPATCH(); }

        if (!jacl_is_closure(closure_val)) {
          vm__set_error(vm, "type error in 'each': expected closure, got %s",
                       vm__type_name(closure_val));
          return VM_RUNTIME_ERROR;
        }
        JaclClosure* closure = jacl_as_closure(closure_val);
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];

        if (jacl_is_typed_vector(coll_val)) {
          if (closure->param_count != 1) {
            vm__set_error(vm, "each on typed vector requires a proc with 1 parameter, got %d",
                         (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          JACL_ASSERT_TAG(coll_val, jacl_is_typed_vector);
          jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(coll_val);
          uint32_t count = jacl_typed_vec_count(tvec);
          uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
          /* Phase 5a: push inline if closure has inline struct params */
          bool push_inline = closure->has_inline_params;

          for (uint32_t i = 0; i < count; i++) {
            const JaclVal* ptr = jacl_typed_vec_get_ptr(tvec, i);

            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            if (push_inline) {
              memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], ptr, sdef->total_size);
              for (uint32_t si = 0; si < width; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += width;
            } else {
              /* Untyped callback (e.g. lambda $it): materialize to heap struct */
              gc__current_heap = &vm->heap;
              HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                     sizeof(HeapRecord) + sdef->total_size);
              s->type_idx = type_idx;
              s->total_size = sdef->total_size;
              memcpy(s->data, ptr, sdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(s));
              if (result != VM_OK) return result;
            }

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_frame_overflow(vm);
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_fc = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - closure->param_total_slots;
            cf->chunk      = &closure->chunk;

            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_fc);
            if (call_result != VM_OK) return call_result;

            JaclVal discard;
            result = vm__pop(vm, &discard);
            if (result != VM_OK) return result;

            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
        } else if (jacl_is_typed_map(coll_val)) {
          if (closure->param_count != 2) {
            vm__set_error(vm, "each on typed map requires a proc with 2 parameters, got %d",
                         (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          JACL_ASSERT_TAG(coll_val, jacl_is_typed_map);
          jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(coll_val);
          StructTypeDef* kdef = (key_type_idx != 0xFFFF) ? vm->struct_registry->defs[key_type_idx] : NULL;
          uint32_t kwidth = kdef ? ((kdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal)) : 0;
          uint32_t vwidth = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
          bool push_inline = closure->has_inline_params;
          jacl_typed_map_iter it = jacl_typed_map_iter_init(tmap);
          jacl_typed_map_iter_result ir;

          for (;;) {
            ir = jacl_typed_map_next_leaf(&it);
            if (ir.done) break;

            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            /* Push key: inline for struct-typed param, heap struct or dyn otherwise */
            if (push_inline && kdef) {
              memset(&vm->stack[vm->stack_top], 0, kwidth * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], ir.item->slots, kdef->total_size);
              for (uint32_t si = 0; si < kwidth; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += kwidth;
            } else if (kdef) {
              gc__current_heap = &vm->heap;
              HeapRecord* ks = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                      sizeof(HeapRecord) + kdef->total_size);
              ks->type_idx = key_type_idx;
              ks->total_size = kdef->total_size;
              memcpy(ks->data, ir.item->slots, kdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(ks));
              if (result != VM_OK) return result;
            } else {
              result = vm__push(vm, jacl_typed_map_key_from_leaf(ir.item));
              if (result != VM_OK) return result;
            }
            /* Push value: inline for struct-typed param, heap struct otherwise */
            const JaclVal* val_ptr = jacl_typed_map_value_ptr_from_leaf(ir.item);
            if (push_inline) {
              memset(&vm->stack[vm->stack_top], 0, vwidth * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], val_ptr, sdef->total_size);
              for (uint32_t si = 0; si < vwidth; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += vwidth;
            } else {
              gc__current_heap = &vm->heap;
              HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                     sizeof(HeapRecord) + sdef->total_size);
              s->type_idx = type_idx;
              s->total_size = sdef->total_size;
              memcpy(s->data, val_ptr, sdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(s));
              if (result != VM_OK) return result;
            }

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_frame_overflow(vm);
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_fc = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - closure->param_total_slots;
            cf->chunk      = &closure->chunk;

            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_fc);
            if (call_result != VM_OK) return call_result;

            JaclVal discard;
            result = vm__pop(vm, &discard);
            if (result != VM_OK) return result;

            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
        } else {
          vm__set_error(vm, "type error in 'each': expected typed vector or map, got %s",
                       vm__type_name(coll_val));
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_TRANSFORM): {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal closure_val, coll_val;
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &coll_val); if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) { result = vm__push(vm, coll_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(closure_val)) { result = vm__push(vm, closure_val); if (result != VM_OK) return result; DISPATCH(); }

        if (!jacl_is_closure(closure_val)) {
          vm__set_error(vm, "type error in 'transform': expected closure, got %s",
                       vm__type_name(closure_val));
          return VM_RUNTIME_ERROR;
        }
        JaclClosure* closure = jacl_as_closure(closure_val);
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];

        if (jacl_is_typed_vector(coll_val)) {
          if (closure->param_count != 1) {
            vm__set_error(vm, "transform on typed vector requires a proc with 1 parameter, got %d",
                         (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          JACL_ASSERT_TAG(coll_val, jacl_is_typed_vector);
          jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(coll_val);
          uint32_t count = jacl_typed_vec_count(tvec);
          uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
          bool push_inline = closure->has_inline_params;
          gc__current_heap = &vm->heap;
          jacl_vec_root* result_vec = jacl_vec_empty();

          for (uint32_t i = 0; i < count; i++) {
            const JaclVal* ptr = jacl_typed_vec_get_ptr(tvec, i);

            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            if (push_inline) {
              memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], ptr, sdef->total_size);
              for (uint32_t si = 0; si < width; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += width;
            } else {
              gc__current_heap = &vm->heap;
              HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                     sizeof(HeapRecord) + sdef->total_size);
              s->type_idx = type_idx;
              s->total_size = sdef->total_size;
              memcpy(s->data, ptr, sdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(s));
              if (result != VM_OK) return result;
            }

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_frame_overflow(vm);
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_fc = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - closure->param_total_slots;
            cf->chunk      = &closure->chunk;

            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_fc);
            if (call_result != VM_OK) return call_result;

            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            gc__current_heap = &vm->heap;
            result_vec = jacl_vec_push_back(result_vec, ret);

            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
          result = vm__push(vm, jacl_vector_ptr(result_vec));
          if (result != VM_OK) return result;

        } else if (jacl_is_typed_map(coll_val)) {
          if (closure->param_count != 2) {
            vm__set_error(vm, "transform on typed map requires a proc with 2 parameters, got %d",
                         (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          JACL_ASSERT_TAG(coll_val, jacl_is_typed_map);
          jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(coll_val);
          StructTypeDef* kdef = (key_type_idx != 0xFFFF) ? vm->struct_registry->defs[key_type_idx] : NULL;
          uint32_t kwidth = kdef ? ((kdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal)) : 0;
          uint32_t vwidth = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
          bool push_inline = closure->has_inline_params;
          jacl_typed_map_iter it = jacl_typed_map_iter_init(tmap);
          jacl_typed_map_iter_result ir;
          gc__current_heap = &vm->heap;
          jacl_vec_root* result_vec = jacl_vec_empty();

          for (;;) {
            ir = jacl_typed_map_next_leaf(&it);
            if (ir.done) break;

            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            if (push_inline && kdef) {
              memset(&vm->stack[vm->stack_top], 0, kwidth * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], ir.item->slots, kdef->total_size);
              for (uint32_t si = 0; si < kwidth; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += kwidth;
            } else if (kdef) {
              gc__current_heap = &vm->heap;
              HeapRecord* ks = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                      sizeof(HeapRecord) + kdef->total_size);
              ks->type_idx = key_type_idx;
              ks->total_size = kdef->total_size;
              memcpy(ks->data, ir.item->slots, kdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(ks));
              if (result != VM_OK) return result;
            } else {
              result = vm__push(vm, jacl_typed_map_key_from_leaf(ir.item));
              if (result != VM_OK) return result;
            }
            const JaclVal* val_ptr = jacl_typed_map_value_ptr_from_leaf(ir.item);
            if (push_inline) {
              memset(&vm->stack[vm->stack_top], 0, vwidth * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], val_ptr, sdef->total_size);
              for (uint32_t si = 0; si < vwidth; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += vwidth;
            } else {
              gc__current_heap = &vm->heap;
              HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                     sizeof(HeapRecord) + sdef->total_size);
              s->type_idx = type_idx;
              s->total_size = sdef->total_size;
              memcpy(s->data, val_ptr, sdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(s));
              if (result != VM_OK) return result;
            }

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_frame_overflow(vm);
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_fc = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - closure->param_total_slots;
            cf->chunk      = &closure->chunk;

            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_fc);
            if (call_result != VM_OK) return call_result;

            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            gc__current_heap = &vm->heap;
            result_vec = jacl_vec_push_back(result_vec, ret);

            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
          result = vm__push(vm, jacl_vector_ptr(result_vec));
          if (result != VM_OK) return result;

        } else {
          vm__set_error(vm, "type error in 'transform': expected typed vector or map, got %s",
                       vm__type_name(coll_val));
          return VM_RUNTIME_ERROR;
        }
        DISPATCH();
      }

      CASE(OP_TYPED_FILTER): {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal closure_val, coll_val;
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &coll_val); if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) { result = vm__push(vm, coll_val); if (result != VM_OK) return result; DISPATCH(); }
        if (jacl_is_error(closure_val)) { result = vm__push(vm, closure_val); if (result != VM_OK) return result; DISPATCH(); }

        if (!jacl_is_closure(closure_val)) {
          vm__set_error(vm, "type error in 'filter': expected closure, got %s",
                       vm__type_name(closure_val));
          return VM_RUNTIME_ERROR;
        }
        JaclClosure* closure = jacl_as_closure(closure_val);
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = vm__struct_width(sdef);

        if (jacl_is_typed_vector(coll_val)) {
          if (closure->param_count != 1) {
            vm__set_error(vm, "filter on typed vector requires a proc with 1 parameter, got %d",
                         (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          JACL_ASSERT_TAG(coll_val, jacl_is_typed_vector);
          jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(coll_val);
          uint32_t count = jacl_typed_vec_count(tvec);
          bool push_inline = closure->has_inline_params;
          gc__current_heap = &vm->heap;
          jacl_typed_vec_root* result_tvec = jacl_typed_vec_empty_strided(width);

          for (uint32_t i = 0; i < count; i++) {
            const JaclVal* ptr = jacl_typed_vec_get_ptr(tvec, i);

            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            if (push_inline) {
              memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], ptr, sdef->total_size);
              for (uint32_t si = 0; si < width; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += width;
            } else {
              gc__current_heap = &vm->heap;
              HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                     sizeof(HeapRecord) + sdef->total_size);
              s->type_idx = type_idx;
              s->total_size = sdef->total_size;
              memcpy(s->data, ptr, sdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(s));
              if (result != VM_OK) return result;
            }

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_frame_overflow(vm);
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_fc = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - closure->param_total_slots;
            cf->chunk      = &closure->chunk;

            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_fc);
            if (call_result != VM_OK) return call_result;

            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            if (!vm__is_falsy(ret)) {
              const JaclVal* ptr2 = jacl_typed_vec_get_ptr(tvec, i);
              gc__current_heap = &vm->heap;
              result_tvec = jacl_typed_vec_push_back_wide(result_tvec, ptr2);
            }

            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
          result = vm__push(vm, jacl_typed_vector_ptr(result_tvec));
          if (result != VM_OK) return result;

        } else if (jacl_is_typed_map(coll_val)) {
          if (closure->param_count != 2) {
            vm__set_error(vm, "filter on typed map requires a proc with 2 parameters, got %d",
                         (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          JACL_ASSERT_TAG(coll_val, jacl_is_typed_map);
          jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(coll_val);
          StructTypeDef* kdef = (key_type_idx != 0xFFFF) ? vm->struct_registry->defs[key_type_idx] : NULL;
          uint32_t kwidth = kdef ? vm__struct_width(kdef) : 0;
          uint32_t key_stride = kdef ? kwidth : 1;
          bool push_inline = closure->has_inline_params;
          jacl_typed_map_iter it = jacl_typed_map_iter_init(tmap);
          jacl_typed_map_iter_result ir;
          gc__current_heap = &vm->heap;
          jacl_typed_map_node* result_tmap = NULL;

          for (;;) {
            ir = jacl_typed_map_next_leaf(&it);
            if (ir.done) break;

            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            if (push_inline && kdef) {
              memset(&vm->stack[vm->stack_top], 0, kwidth * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], ir.item->slots, kdef->total_size);
              for (uint32_t si = 0; si < kwidth; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += kwidth;
            } else if (kdef) {
              gc__current_heap = &vm->heap;
              HeapRecord* ks = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                      sizeof(HeapRecord) + kdef->total_size);
              ks->type_idx = key_type_idx;
              ks->total_size = kdef->total_size;
              memcpy(ks->data, ir.item->slots, kdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(ks));
              if (result != VM_OK) return result;
            } else {
              result = vm__push(vm, jacl_typed_map_key_from_leaf(ir.item));
              if (result != VM_OK) return result;
            }
            const JaclVal* val_ptr = jacl_typed_map_value_ptr_from_leaf(ir.item);
            if (push_inline) {
              memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], val_ptr, sdef->total_size);
              for (uint32_t si = 0; si < width; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += width;
            } else {
              gc__current_heap = &vm->heap;
              HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                     sizeof(HeapRecord) + sdef->total_size);
              s->type_idx = type_idx;
              s->total_size = sdef->total_size;
              memcpy(s->data, val_ptr, sdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(s));
              if (result != VM_OK) return result;
            }

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_frame_overflow(vm);
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_fc = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - closure->param_total_slots;
            cf->chunk      = &closure->chunk;

            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_fc);
            if (call_result != VM_OK) return call_result;

            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            if (!vm__is_falsy(ret)) {
              const JaclVal* key_data = ir.item->slots;
              gc__current_heap = &vm->heap;
              result_tmap = jacl_typed_map_set_wide(result_tmap, key_data, key_stride, val_ptr, width);
            }

            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
          result = vm__push(vm, jacl_typed_map_ptr(result_tmap));
          if (result != VM_OK) return result;

        } else {
          vm__set_error(vm, "type error in 'filter': expected typed vector or map, got %s",
                       vm__type_name(coll_val));
          return VM_RUNTIME_ERROR;
        }
        DISPATCH();
      }

      /* ===== Typed Map opcodes ===== */

      CASE(OP_TYPED_MAP): {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        uint8_t pair_count = vm__read_byte(vm);
        gc__current_heap = &vm->heap;
        jacl_typed_map_node* tmap = NULL;

        /* Pop pairs right-to-left into scratch buffers. */
        uint32_t vw = vm__typed_elem_width(vm, type_idx);
        uint32_t kw = (key_type_idx == 0xFFFF) ? 1 : vm__typed_elem_width(vm, key_type_idx);
        if ((size_t)pair_count * (kw + vw) > VM_MAX_STRUCT_SLOTS * 256) {
          vm__set_error(vm, "typed-map literal too large");
          return VM_RUNTIME_ERROR;
        }
        JaclVal scratch[VM_MAX_STRUCT_SLOTS * 256];
        for (int32_t i = (int32_t)pair_count - 1; i >= 0; i--) {
          uint32_t off = (uint32_t)i * (kw + vw);
          vm__pop_typed_elem(vm, type_idx, &scratch[off + kw]);
          if (key_type_idx == 0xFFFF) {
            JaclVal k;
            result = vm__pop(vm, &k); if (result != VM_OK) return result;
            scratch[off] = k;
          } else {
            vm__pop_typed_elem(vm, key_type_idx, &scratch[off]);
          }
        }
        for (uint8_t i = 0; i < pair_count; i++) {
          uint32_t off = (uint32_t)i * (kw + vw);
          tmap = jacl_typed_map_set_wide(tmap, &scratch[off], kw,
                                         &scratch[off + kw], vw);
        }
        result = vm__push(vm, jacl_typed_map_ptr(tmap));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_MAP_GET_INLINE): {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        jacl_typed_map_leaf* leaf;
        JaclVal key_slots[VM_MAX_STRUCT_SLOTS];
        uint32_t kw;
        if (key_type_idx == 0xFFFF) {
          result = vm__pop(vm, &key_slots[0]); if (result != VM_OK) return result;
          kw = 1;
        } else {
          kw = vm__pop_typed_elem(vm, key_type_idx, key_slots);
        }
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;
        JACL_ASSERT_TAG(tmap_val, jacl_is_typed_map);
        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);
        leaf = jacl_typed_map_get_leaf(tmap, key_slots, kw);

        if (!leaf) {
          vm__set_error(vm, "map-get: key not found in typed map");
          return VM_RUNTIME_ERROR;
        }

        const JaclVal* val_ptr = jacl_typed_map_value_ptr_from_leaf(leaf);
        if (type_idx >= 0xFF00) {
          /* Scalar value — single-slot copy. */
          result = vm__push(vm, *val_ptr);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = vm__struct_width(sdef);
        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_operand_overflow(vm, "typed map get inline");
          return VM_RUNTIME_ERROR;
        }
        memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], val_ptr, sdef->total_size);
        for (uint32_t si = 0; si < width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        }
        vm->stack_top += width;
        DISPATCH();
      }

      CASE(OP_TYPED_MAP_SET): {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal val_slots[VM_MAX_STRUCT_SLOTS];
        uint32_t vw = vm__pop_typed_elem(vm, type_idx, val_slots);
        JaclVal key_slots[VM_MAX_STRUCT_SLOTS];
        uint32_t kw;
        if (key_type_idx == 0xFFFF) {
          result = vm__pop(vm, &key_slots[0]); if (result != VM_OK) return result;
          kw = 1;
        } else {
          kw = vm__pop_typed_elem(vm, key_type_idx, key_slots);
        }
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;
        JACL_ASSERT_TAG(tmap_val, jacl_is_typed_map);
        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);

        gc__current_heap = &vm->heap;
        jacl_typed_map_node* new_tmap = jacl_typed_map_set_wide(tmap, key_slots, kw, val_slots, vw);
        result = vm__push(vm, jacl_typed_map_ptr(new_tmap));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_MAP_HAS): {
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal key_slots[VM_MAX_STRUCT_SLOTS];
        uint32_t kw;
        if (key_type_idx == 0xFFFF) {
          result = vm__pop(vm, &key_slots[0]); if (result != VM_OK) return result;
          kw = 1;
        } else {
          kw = vm__pop_typed_elem(vm, key_type_idx, key_slots);
        }
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;
        JACL_ASSERT_TAG(tmap_val, jacl_is_typed_map);
        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);
        bool found = (key_type_idx == 0xFFFF)
                       ? jacl_typed_map_has(tmap, key_slots[0])
                       : jacl_typed_map_has_wide(tmap, key_slots, kw);
        result = vm__push(vm, jacl_bool(found));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_MAP_REMOVE): {
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal key_slots[VM_MAX_STRUCT_SLOTS];
        uint32_t kw;
        if (key_type_idx == 0xFFFF) {
          result = vm__pop(vm, &key_slots[0]); if (result != VM_OK) return result;
          kw = 1;
        } else {
          kw = vm__pop_typed_elem(vm, key_type_idx, key_slots);
        }
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;
        JACL_ASSERT_TAG(tmap_val, jacl_is_typed_map);
        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);
        gc__current_heap = &vm->heap;
        jacl_typed_map_node* new_tmap = (key_type_idx == 0xFFFF)
                       ? jacl_typed_map_unset(tmap, key_slots[0])
                       : jacl_typed_map_unset_wide(tmap, key_slots, kw);
        result = vm__push(vm, jacl_typed_map_ptr(new_tmap));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_MAP_LEN): {
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;
        JACL_ASSERT_TAG(tmap_val, jacl_is_typed_map);
        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);
        result = vm__push(vm, jacl_i32((int32_t)jacl_typed_map_count(tmap)));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_MAP_KEYS): {
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;

        JACL_ASSERT_TAG(tmap_val, jacl_is_typed_map);
        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);
        gc__current_heap = &vm->heap;
        jacl_typed_map_iter it = jacl_typed_map_iter_init(tmap);
        jacl_typed_map_iter_result ir;

        if (key_type_idx == 0xFFFF ||
            key_type_idx == (uint16_t)(0xFF00 + TYPE_STR)) {
          /* Dyn or str keys → plain traced vec. Keys are one tagged slot;
           * str pointers must live in traced leaves (typed-vec storage is
           * GC-opaque), so a str-keyed map's keys vec is a plain vec — the
           * static type is [Vec str] via the typer stamp. */
          jacl_vec_root* vec = jacl_vec_empty();
          for (;;) {
            ir = jacl_typed_map_next_leaf(&it);
            if (ir.done) break;
            JaclVal key = jacl_typed_map_key_from_leaf(ir.item);
            vec = jacl_vec_push_back(vec, key);
          }
          result = vm__push(vm, jacl_vector_ptr(vec));
        } else {
          /* Typed keys (struct or scalar) → typed vec with same elem type */
          uint32_t kw = vm__typed_elem_width(vm, key_type_idx);
          jacl_typed_vec_root* tvec = jacl_typed_vec_empty_strided(kw);
          for (;;) {
            ir = jacl_typed_map_next_leaf(&it);
            if (ir.done) break;
            const JaclVal* key_ptr = ir.item->slots;  /* raw key slots */
            tvec = jacl_typed_vec_push_back_wide(tvec, key_ptr);
          }
          result = vm__push(vm, jacl_typed_vector_ptr(tvec));
        }
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_MAP_VALS): {
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;

        JACL_ASSERT_TAG(tmap_val, jacl_is_typed_map);
        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);
        uint32_t width = vm__typed_elem_width(vm, type_idx);

        gc__current_heap = &vm->heap;
        jacl_typed_vec_root* tvec = jacl_typed_vec_empty_strided(width);
        jacl_typed_map_iter it = jacl_typed_map_iter_init(tmap);
        jacl_typed_map_iter_result ir;
        for (;;) {
          ir = jacl_typed_map_next_leaf(&it);
          if (ir.done) break;
          const JaclVal* val_ptr = jacl_typed_map_value_ptr_from_leaf(ir.item);
          tvec = jacl_typed_vec_push_back_wide(tvec, val_ptr);
        }
        result = vm__push(vm, jacl_typed_vector_ptr(tvec));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_VEC_PRINT): {
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal tvec_val;
        result = vm__pop(vm, &tvec_val); if (result != VM_OK) return result;

        /* Print NIL when the slot is uninitialized -- e.g. a [Buf N
         * [Vec T]] element that's been zero-init'd but never written.
         * Must push NIL as the return value so the next op finds the
         * right TOS (the non-nil path below pushes JACL_NIL too). */
        if (jacl_is_nil(tvec_val)) {
          vm->print_fn("nil\n", 4, vm->print_ctx);
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        JACL_ASSERT_TAG(tvec_val, jacl_is_typed_vector);
        jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(tvec_val);
        uint32_t count = jacl_typed_vec_count(tvec);

        VMFormatBuf fmt;
        vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
        vm__fmt_append(&fmt, "[", 1);
        if (type_idx >= 0xFF00) {
          JaclType elem_t = (JaclType)(type_idx - 0xFF00);
          for (uint32_t i = 0; i < count; i++) {
            if (i > 0) vm__fmt_append(&fmt, ", ", 2);
            vm__fmt_typed_scalar(&fmt, jacl_typed_vec_get_ptr(tvec, i), elem_t);
          }
        } else {
          StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
          for (uint32_t i = 0; i < count; i++) {
            if (i > 0) vm__fmt_append(&fmt, ", ", 2);
            const JaclVal* ptr = jacl_typed_vec_get_ptr(tvec, i);
            vm__fmt_struct_data(&fmt, sdef, (const uint8_t*)ptr);
          }
        }
        vm__fmt_append(&fmt, "]\n", 2);
        vm->print_fn(fmt.data, fmt.len, vm->print_ctx);
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_MAP_PRINT): {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;

        /* Print NIL when the slot is uninitialized -- e.g. a [Buf N
         * [Map K V]] element that's been zero-init'd but never written.
         * Mirrors OP_TYPED_VEC_PRINT (Phase 2). Must push NIL as the
         * return value so the next op finds the right TOS. */
        if (jacl_is_nil(tmap_val)) {
          vm->print_fn("nil\n", 4, vm->print_ctx);
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;
          DISPATCH();
        }
        JACL_ASSERT_TAG(tmap_val, jacl_is_typed_map);
        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);
        bool val_is_scalar = (type_idx >= 0xFF00);
        bool key_is_scalar = (key_type_idx >= 0xFF00 && key_type_idx != 0xFFFF);
        StructTypeDef* sdef = val_is_scalar ? NULL : vm->struct_registry->defs[type_idx];
        StructTypeDef* kdef = (key_type_idx != 0xFFFF && !key_is_scalar)
                                ? vm->struct_registry->defs[key_type_idx] : NULL;
        JaclType val_t = val_is_scalar ? (JaclType)(type_idx - 0xFF00) : TYPE_DYN;
        JaclType key_t = key_is_scalar ? (JaclType)(key_type_idx - 0xFF00) : TYPE_DYN;

        VMFormatBuf fmt;
        vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
        vm__fmt_append(&fmt, "{", 1);
        jacl_typed_map_iter it = jacl_typed_map_iter_init(tmap);
        jacl_typed_map_iter_result ir;
        uint32_t idx = 0;
        for (;;) {
          ir = jacl_typed_map_next_leaf(&it);
          if (ir.done) break;
          if (idx > 0) vm__fmt_append(&fmt, ", ", 2);
          /* Format key */
          if (kdef) {
            const JaclVal* key_ptr = ir.item->slots;
            vm__fmt_struct_data(&fmt, kdef, (const uint8_t*)key_ptr);
          } else if (key_is_scalar) {
            vm__fmt_typed_scalar(&fmt, ir.item->slots, key_t);
          } else {
            JaclVal key = jacl_typed_map_key_from_leaf(ir.item);
            vm__fmt_value(&fmt, key);
          }
          vm__fmt_append(&fmt, ": ", 2);
          /* Format value */
          const JaclVal* val_ptr = jacl_typed_map_value_ptr_from_leaf(ir.item);
          if (val_is_scalar) {
            vm__fmt_typed_scalar(&fmt, val_ptr, val_t);
          } else {
            vm__fmt_struct_data(&fmt, sdef, (const uint8_t*)val_ptr);
          }
          idx++;
        }
        vm__fmt_append(&fmt, "}\n", 2);
        vm->print_fn(fmt.data, fmt.len, vm->print_ctx);
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_VEC_EQ): {
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal b_val, a_val;
        result = vm__pop(vm, &b_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &a_val); if (result != VM_OK) return result;

        JACL_ASSERT_TAG(a_val, jacl_is_typed_vector);
        JACL_ASSERT_TAG(b_val, jacl_is_typed_vector);
        jacl_typed_vec_root* a = (jacl_typed_vec_root*)jacl_as_ptr(a_val);
        jacl_typed_vec_root* b = (jacl_typed_vec_root*)jacl_as_ptr(b_val);

        if (a == b) {
          result = vm__push(vm, JACL_TRUE);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        uint32_t count = jacl_typed_vec_count(a);
        if (count != jacl_typed_vec_count(b)) {
          result = vm__push(vm, JACL_FALSE);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        bool equal = true;
        if (type_idx >= 0xFF00) {
          /* Scalar elements (reachable since typed collect): wide scalars
           * (i64/u64/f64) hold raw bits — compare bytes; tagged-stored
           * scalars (i32/str/bool/...) compare as values (strings need
           * deep equality, not pointer identity). */
          JaclType eq_et = (JaclType)(type_idx - 0xFF00);
          bool eq_raw = (eq_et == TYPE_I64 || eq_et == TYPE_U64 ||
                         eq_et == TYPE_F64);
          for (uint32_t i = 0; i < count; i++) {
            const JaclVal* pa = jacl_typed_vec_get_ptr(a, i);
            const JaclVal* pb = jacl_typed_vec_get_ptr(b, i);
            if (eq_raw ? (memcmp(pa, pb, sizeof(JaclVal)) != 0)
                       : !vm__deep_eq(*pa, *pb)) {
              equal = false;
              break;
            }
          }
        } else {
          StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
          for (uint32_t i = 0; i < count; i++) {
            const JaclVal* pa = jacl_typed_vec_get_ptr(a, i);
            const JaclVal* pb = jacl_typed_vec_get_ptr(b, i);
            if (memcmp(pa, pb, sdef->total_size) != 0) {
              equal = false;
              break;
            }
          }
        }
        result = vm__push(vm, jacl_bool(equal));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_TYPED_MAP_EQ): {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal b_val, a_val;
        result = vm__pop(vm, &b_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &a_val); if (result != VM_OK) return result;

        JACL_ASSERT_TAG(a_val, jacl_is_typed_map);
        JACL_ASSERT_TAG(b_val, jacl_is_typed_map);
        jacl_typed_map_node* a = (jacl_typed_map_node*)jacl_as_ptr(a_val);
        jacl_typed_map_node* b = (jacl_typed_map_node*)jacl_as_ptr(b_val);

        if (a == b) {
          result = vm__push(vm, JACL_TRUE);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        /* Scalar sentinels (>= 0xFF00) must NOT index the struct registry.
         * Scalar map values are stored as 1 tagged slot (vm__pop_typed_elem)
         * — compare via deep-eq, which also covers heap-boxed wide ints.
         * Scalar keys are likewise 1 tagged slot. */
        bool val_is_scalar = (type_idx >= 0xFF00);
        StructTypeDef* sdef = val_is_scalar ? NULL
                              : vm->struct_registry->defs[type_idx];
        if (jacl_typed_map_count(a) != jacl_typed_map_count(b)) {
          result = vm__push(vm, JACL_FALSE);
          if (result != VM_OK) return result;
          DISPATCH();
        }

        uint32_t key_stride = 1;
        if (key_type_idx != 0xFFFF && key_type_idx < 0xFF00) {
          StructTypeDef* kdef = vm->struct_registry->defs[key_type_idx];
          key_stride = vm__struct_width(kdef);
        }

        bool equal = true;
        jacl_typed_map_iter it = jacl_typed_map_iter_init(a);
        jacl_typed_map_iter_result ir;
        for (;;) {
          ir = jacl_typed_map_next_leaf(&it);
          if (ir.done) break;
          jacl_typed_map_leaf* b_leaf;
          if (key_type_idx == 0xFFFF) {
            JaclVal key = jacl_typed_map_key_from_leaf(ir.item);
            b_leaf = jacl_typed_map_get_leaf(b, &key, 1);
          } else {
            const JaclVal* key_ptr = ir.item->slots;
            b_leaf = jacl_typed_map_get_leaf(b, key_ptr, key_stride);
          }
          if (!b_leaf) { equal = false; break; }
          const JaclVal* va = jacl_typed_map_value_ptr_from_leaf(ir.item);
          const JaclVal* vb = jacl_typed_map_value_ptr_from_leaf(b_leaf);
          bool v_eq = val_is_scalar
                        ? vm__deep_eq(*va, *vb)
                        : (memcmp(va, vb, sdef->total_size) == 0);
          if (!v_eq) {
            equal = false;
            break;
          }
        }
        result = vm__push(vm, jacl_bool(equal));
        if (result != VM_OK) return result;
        DISPATCH();
      }

      CASE(OP_DEREF_INLINE): {
        /* Phase 5d: Deref a struct box, push inline bytes directly.
           Operand: uint16_t type_idx.
           Avoids heap allocation (unlike OP_DEREF for struct boxes). */
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal container;
        result = vm__pop(vm, &container);
        if (result != VM_OK) return result;
        if (!jacl_is_box(container)) {
          vm__set_error(vm, "deref: expected box, got %s", vm__type_name(container));
          return VM_RUNTIME_ERROR;
        }
        JaclMutableRef* ref = (JaclMutableRef*)jacl_as_ptr(container);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "deref: invalid struct type index %u", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_operand_overflow(vm, "deref inline");
          return VM_STACK_OVERFLOW;
        }
        memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], ref->data, sdef->total_size);
        for (uint32_t si = 0; si < width; si++)
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        vm->stack_top += width;
        DISPATCH();
      }

#ifdef JACL_VM_COMPUTED_GOTO
  L_unknown_opcode:
    vm__set_error(vm, "unknown opcode %d", (int)instruction);
    return VM_RUNTIME_ERROR;
#else
      default: {
        vm__set_error(vm, "unknown opcode %d", (int)instruction);
        return VM_RUNTIME_ERROR;
      }
    }  /* end switch */
  }    /* end for */
#endif

  #undef CASE
  #undef DISPATCH
  #undef VM_PRELUDE
}

#undef VM__BINARY_NUMERIC_OP

/* --- Multi-module execution: jacl_exec_program --- */

/* --- ctx subsystem startup ---
 *
 * Initialize the ctx pool and allocate the initial ctx struct for a VM.
 * Sets vm->ctx_pool and vm->ctx.  pool_storage must remain valid for
 * the VM's lifetime (typically stack-allocated by the caller).
 */
void ctx__init_vm(VM *vm, JaclCtxPool *pool_storage) {
    StructTypeRegistry *reg = vm->struct_registry;
    if (!reg || reg->ctx_type_idx == 0) return;

    ctx_pool_init(pool_storage, &vm->heap, reg);
    vm->ctx_pool = pool_storage;

    /* Allocate initial ctx from pool */
    HeapRecord *ctx_struct = ctx_pool_alloc(pool_storage, &vm->heap);
    StructTypeDef *sdef = reg->defs[reg->ctx_type_idx];

    /* Initialize all fields with their compile-time defaults */
    for (uint32_t i = 0; i < sdef->field_count; i++) {
        if (sdef->fields[i].default_val != JACL_NIL) {
            vm__heap_record_write_field(ctx_struct, sdef->fields[i].offset,
                                   sdef->fields[i].type,
                                   sdef->fields[i].default_val);
        }
    }

    /* Override built-in pwd field (field 0) with actual getcwd() */
    char cwd_buf[4096];
    if (getcwd(cwd_buf, sizeof(cwd_buf))) {
        size_t len = strlen(cwd_buf);
        JaclVal pwd_str;
        if (len <= 7) {
            pwd_str = jacl_inline_string(cwd_buf, len);
        } else {
            pwd_str = jacl_rope_string_create(&vm->heap,
                                              (const uint8_t *)cwd_buf, len);
        }
        vm__heap_record_write_field(ctx_struct, sdef->fields[0].offset,
                               sdef->fields[0].type, pwd_str);
    }

    vm->ctx = jacl_heap_record_val(ctx_struct);
}

/**
 * Execute a compiled multi-module program.
 * Initializes modules in topological order (dependencies first),
 * then executes the root module (last in the array).
 * For CPS-transformed root modules, handles continuation setup.
 */
VMResult jacl_exec_program(ProgramResult* program, VM* vm) {
  if (!program || program->module_count == 0) {
    vm->error_message = "no modules to execute";
    return VM_RUNTIME_ERROR;
  }

  vm->struct_registry = program->struct_registry;

  /* Initialize ctx subsystem (pool + initial ctx with pwd) */
  JaclCtxPool exec_ctx_pool;
  ctx__init_vm(vm, &exec_ctx_pool);

  /* Execute each module's chunk in topological order.
     Dependencies come first, root module is last. */
  uint32_t last = program->module_count - 1;

  /* Execute dependency modules (non-root) */
  for (uint32_t i = 0; i < last; i++) {
    Module* mod = program->modules[i];
    vm->stack_top = 0;
    VMResult r = vm_exec(vm, mod->chunk);
    if (r != VM_OK) {
      /* Wrap error with module context */
      const char* slash = strrchr(mod->path, '/');
      const char* basename = slash ? slash + 1 : mod->path;
      char buf[256];
      snprintf(buf, sizeof(buf), "error in module '%s': %s",
               basename,
               vm->error_message ? vm->error_message : "unknown error");
      char* msg = (char*)arena_alloc(vm->arena, (uint32_t)(strlen(buf) + 1));
      memcpy(msg, buf, strlen(buf) + 1);
      vm->error_message = msg;
      return r;
    }
  }

  /* Execute root module */
  Module* root = program->modules[last];
  vm->stack_top = 0;

  if (program->suspending) {
    /* Suspending root: chunk produces __main closure on stack.
       Execute chunk to get the closure, then call it. */
    VMResult r = vm_exec(vm, root->chunk);
    if (r != VM_OK) return r;

    JaclVal main_cl_val = vm->stack[0];
    if (!jacl_is_closure(main_cl_val)) {
      vm->error_message = "internal error: suspending top-level did not produce closure";
      return VM_RUNTIME_ERROR;
    }
    JaclClosure *main_cl = jacl_as_closure(main_cl_val);

    JaclClosure top_closure_wrapper;
    memset(&top_closure_wrapper, 0, sizeof(top_closure_wrapper));
    top_closure_wrapper.chunk = *root->chunk;

    if (main_cl->is_sm_compiled) {
      /* SM main closure: create state machine, call with (sm_val, nil) */
      JaclVal sm_val = gc_alloc_state_machine(&vm->heap, main_cl->sm_field_count);
      JaclStateMachine *sm = jacl_as_state_machine(sm_val);
      vm__slot_set(vm, &sm->sm_closure, main_cl_val);

      vm->stack_top = 0;
      vm->stack[0]  = main_cl_val;
      vm->stack[1]  = sm_val;
      vm->stack[2]  = JACL_NIL;
      vm->stack_top = 3;

      vm->frames[0].closure    = &top_closure_wrapper;
      vm->frames[0].return_ip  = NULL;
      vm->frames[0].stack_base = 0;
      vm->frames[0].chunk      = root->chunk;
      vm->frame_count = 1;

      vm->frames[1].closure    = main_cl;
      vm->frames[1].return_ip  = NULL;
      vm->frames[1].stack_base = 1; /* 2 args (__sm, __rv) */
      vm->frames[1].chunk      = &main_cl->chunk;
      vm->frame_count = 2;
      vm->ip    = main_cl->chunk.code;
      vm->chunk = &main_cl->chunk;
      vm->top_chunk = &main_cl->chunk;

      return vm__run(vm, 1);
    }

    /* CPS fallback */
    JaclVal completion = jacl_future(&vm->heap);
    JaclVal resolve_k = runtime__create_resolve_closure(&vm->heap, vm->arena,
                                                         completion);

    vm->stack_top = 0;
    vm->stack[0]  = main_cl_val;
    vm->stack[1]  = resolve_k;
    vm->stack_top = 2;

    vm->frames[0].closure    = &top_closure_wrapper;
    vm->frames[0].return_ip  = NULL;
    vm->frames[0].stack_base = 0;
    vm->frames[0].chunk      = root->chunk;
    vm->frame_count = 1;

    vm->frames[1].closure    = main_cl;
    vm->frames[1].return_ip  = NULL;
    vm->frames[1].stack_base = 1;
    vm->frames[1].chunk      = &main_cl->chunk;
    vm->frame_count = 2;
    vm->ip    = main_cl->chunk.code;
    vm->chunk = &main_cl->chunk;
    vm->top_chunk = &main_cl->chunk;

    r = vm__run(vm, 1);

    JaclFuture *cfut = jacl_as_future(completion);
    uint32_t state = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_RELAXED);
    if (state == FUTURE_RESOLVED) {
      vm->stack[0] = (JaclVal)cfut->result;
      vm->stack_top = 1;
    } else if (state == FUTURE_ERROR) {
      vm->stack[0] = (JaclVal)cfut->result;
      vm->stack_top = 1;
    }
    return r;
  }

  return vm_exec(vm, root->chunk);
}

/* --- Context lifecycle --- */

jacl_context_t *jacl_ctx_new(jacl_context_t *parent) {
    jacl_context_t *ctx = (jacl_context_t *)calloc(1, sizeof(jacl_context_t));
    if (!ctx) return NULL;

    ctx->parent = parent;
    ctx->restriction_set = UINT64_MAX;  /* all-permissive */

    /* Arena: zero-initialized by calloc (uses default allocator) */

    /* VM */
    vm_init(&ctx->vm, &ctx->arena);

    if (parent) {
        /* Child: share parent's intern table */
        ctx->owns_intern_table = false;
        ctx->vm.intern_table = &parent->intern_table;
    } else {
        /* Root: own intern table */
        ctx->owns_intern_table = true;
        intern_table_init(&ctx->intern_table, &ctx->arena);
        ctx->vm.intern_table = &ctx->intern_table;
    }

    macro_table_init(&ctx->macro_table);

    /* Expansion state is zero-initialized by calloc */

    return ctx;
}

void jacl_ctx_destroy(jacl_context_t *ctx) {
    if (!ctx) return;
    /* Free the defs pointer array in the struct registry (StructTypeDefs are in the arena) */
    if (ctx->vm.struct_registry)
        struct_registry__destroy(ctx->vm.struct_registry);
    vm_destroy(&ctx->vm);
    if (ctx->owns_intern_table)
        intern_table_destroy(&ctx->intern_table);
    arena_destroy(&ctx->arena);
    free(ctx);
}

/* --- Scoped context switching --- */

void jacl_ctx_save(jacl_ctx_saved_t *saved) {
    saved->heap   = gc__current_heap;
    saved->gc_fn  = gc__emergency_gc_fn;
    saved->gc_ctx = gc__emergency_gc_ctx;
}

void jacl_ctx_enter(jacl_context_t *ctx, jacl_ctx_saved_t *saved) {
    jacl_ctx_save(saved);
    gc__current_heap     = &ctx->vm.heap;
    gc__emergency_gc_fn  = vm__emergency_gc_single;
    gc__emergency_gc_ctx = &ctx->vm;
}

void jacl_ctx_restore(jacl_ctx_saved_t saved) {
    gc__current_heap     = saved.heap;
    gc__emergency_gc_fn  = saved.gc_fn;
    gc__emergency_gc_ctx = saved.gc_ctx;
}

/* --- source_to_closure_in_place: compile source into a closure on caller's heap --- */

JaclVal source_to_closure_in_place(const char *src, size_t len,
                                   arena_t *arena, ThreadHeap *heap,
                                   JaclInternTable *intern_table,
                                   ExpandState *expand,
                                   JaclError *err_out,
                                   JaclVal prelude_map) {
    if (err_out) {
        err_out->kind = JACL_ERROR_NONE;
        err_out->message = NULL;
        err_out->line = 0;
        err_out->col = 0;
    }
    if (!src || len == 0) {
        if (err_out) {
            err_out->kind = JACL_ERROR_COMPILE;
            err_out->message = "empty source";
        }
        return JACL_NIL;
    }

    /* Copy source to arena so it's NUL-terminated for the lexer */
    char *buf = (char *)arena_alloc(arena, (uint32_t)(len + 1));
    if (!buf) {
        if (err_out) {
            err_out->kind = JACL_ERROR_COMPILE;
            err_out->message = "out of memory";
        }
        return JACL_NIL;
    }
    memcpy(buf, src, len);
    buf[len] = '\0';

    /* Lex */
    LexResult tokens = lexer_lex(buf, arena);

    /* Parse */
    ParseResult parse = parser_parse(tokens, arena);
    if (parse.error_count > 0) {
        const char *first_msg = "parse error";
        uint32_t err_line = 1, err_col = 1;
        for (uint32_t i = 0; i < parse.count; i++) {
            if (parse.nodes[i] && parse.nodes[i]->type == AST_ERROR) {
                first_msg = parse.nodes[i]->data.error.message;
                err_line = parse.nodes[i]->start.line;
                err_col = parse.nodes[i]->start.column;
                break;
            }
        }
        if (err_out) {
            /* Format with location like compile errors */
            char buf[256];
            int n = snprintf(buf, sizeof(buf), "line %u, col %u: %s",
                             err_line, err_col, first_msg);
            if (n < 0) n = 0;
            char *msg = (char *)arena_alloc(arena, (uint32_t)n + 1);
            if (msg) {
                memcpy(msg, buf, (uint32_t)n + 1);
                err_out->message = msg;
            } else {
                err_out->message = first_msg;
            }
            err_out->kind = JACL_ERROR_COMPILE;
            err_out->line = err_line;
            err_out->col = err_col;
        }
        return JACL_NIL;
    }

    /* Compile (macro expansion happens inside compiler_compile) */
    CompileResult cr = compiler_compile(parse, arena, intern_table,
                                        heap, NULL, expand, prelude_map,
                                        NULL, NULL);
    if (cr.error_count > 0) {
        if (err_out) {
            err_out->kind = JACL_ERROR_COMPILE;
            err_out->message = cr.error_message ? cr.error_message : "compile error";
        }
        return JACL_NIL;
    }

    /* Wrap the compiled bytecode into a 0-arg closure on the arena */
    JaclClosure *closure = (JaclClosure *)arena_alloc(arena, sizeof(JaclClosure));
    if (!closure) {
        if (err_out) {
            err_out->kind = JACL_ERROR_COMPILE;
            err_out->message = "out of memory";
        }
        return JACL_NIL;
    }
    memset(closure, 0, sizeof(JaclClosure));
    closure->chunk       = cr.chunk;
    closure->param_count = 0;
    closure->min_args    = 0;
    closure->variadic    = false;
    closure->name        = "<interpret>";

    return jacl_closure(closure);
}

/* --- jacl_ctx_run_source / jacl_ctx_run_closure (US-006) --- */

JaclVal jacl_ctx_run_source(jacl_context_t *ctx, const char *src, size_t len,
                            uint64_t restriction_set, JaclError *err_out) {
    if (err_out) { err_out->kind = JACL_ERROR_NONE; err_out->message = NULL; err_out->line = 0; err_out->col = 0; }
    if (!ctx || !src) return JACL_NIL;

    ctx->restriction_set = restriction_set;

    /* Copy source to arena so it's NUL-terminated for the lexer */
    char *buf = (char *)arena_alloc(&ctx->arena, (uint32_t)(len + 1));
    memcpy(buf, src, len);
    buf[len] = '\0';

    /* Lex */
    LexResult tokens = lexer_lex(buf, &ctx->arena);

    /* Parse */
    ParseResult parse = parser_parse(tokens, &ctx->arena);
    if (parse.error_count > 0) {
        const char *first_err = "parse error";
        for (uint32_t i = 0; i < parse.count; i++) {
            if (parse.nodes[i] && parse.nodes[i]->type == AST_ERROR) {
                first_err = parse.nodes[i]->data.error.message;
                break;
            }
        }
        if (err_out) {
            err_out->kind = JACL_ERROR_COMPILE;
            err_out->message = first_err;
        }
        return JACL_NIL;
    }

    /* Compile */
    JaclInternTable *itab = ctx->owns_intern_table ? &ctx->intern_table
                                                   : ctx->vm.intern_table;
    ctx->expand.ctx = ctx;
    CompileResult cr = compiler_compile(parse, &ctx->arena, itab,
                                        &ctx->vm.heap, NULL, &ctx->expand,
                                        JACL_NIL, NULL, NULL);
    if (cr.error_count > 0) {
        if (err_out) {
            err_out->kind = JACL_ERROR_COMPILE;
            err_out->message = cr.error_message ? cr.error_message : "compile error";
        }
        return JACL_NIL;
    }

    /* Execute */
    ctx->vm.intern_table   = itab;
    ctx->vm.struct_registry = cr.struct_registry;

    /* Initialize ctx subsystem (pool + initial ctx with pwd) */
    JaclCtxPool ctx_run_pool;
    ctx__init_vm(&ctx->vm, &ctx_run_pool);

    VMResult r = vm_exec(&ctx->vm, &cr.chunk);
    if (r != VM_OK) {
        if (err_out) {
            err_out->kind    = JACL_ERROR_RUNTIME;
            err_out->message = ctx->vm.error_message ? ctx->vm.error_message : "runtime error";
            err_out->line    = ctx->vm.error_line;
        }
        return JACL_NIL;
    }

    return (ctx->vm.stack_top > 0) ? ctx->vm.stack[0] : JACL_NIL;
}

JaclVal jacl_ctx_run_closure(jacl_context_t *ctx, JaclClosure *closure,
                             JaclVal *args, uint32_t arg_count,
                             JaclError *err_out) {
    if (err_out) { err_out->kind = JACL_ERROR_NONE; err_out->message = NULL; err_out->line = 0; err_out->col = 0; }
    if (!ctx || !closure) return JACL_NIL;

    VM *vm = &ctx->vm;

    /* Set up stack: [closure_val, args...] */
    JaclVal closure_val = jacl_closure_ptr(closure);
    vm->stack[0] = closure_val;
    for (uint32_t i = 0; i < arg_count; i++)
        vm->stack[1 + i] = args[i];
    vm->stack_top = 1 + arg_count;

    /* Set up call frame */
    vm->frames[0].closure    = closure;
    vm->frames[0].return_ip  = NULL;
    vm->frames[0].stack_base = 1;
    vm->frames[0].chunk      = &closure->chunk;
    vm->frame_count = 1;

    vm->ip        = closure->chunk.code;
    vm->chunk     = &closure->chunk;
    vm->top_chunk = &closure->chunk;

    VMResult r = vm__run(vm, 0);

    if (r != VM_OK) {
        if (err_out) {
            err_out->kind    = JACL_ERROR_RUNTIME;
            err_out->message = vm->error_message ? vm->error_message : "runtime error";
            err_out->line    = vm->error_line;
        }
        return JACL_NIL;
    }

    return (vm->stack_top > 0) ? vm->stack[0] : JACL_NIL;
}

/* --- Pipeline convenience: jacl_run --- */

/**
 * Source-to-execution pipeline.
 * Chains: lexer_lex -> parser_parse -> compiler_compile -> vm_exec.
 * Uses a stack-local ExpandState for reentrancy-safe macro expansion.
 * Returns VM_RUNTIME_ERROR on parse or compile errors (message in vm->error_message).
 */
static const char* jacl__find_parse_error(AstNode* node) {
  if (!node) return NULL;
  if (node->type == AST_ERROR) return node->data.error.message;
  /* Search inside blocks */
  if (node->type == AST_BLOCK) {
    for (uint32_t i = 0; i < node->data.block.count; i++) {
      const char* msg = jacl__find_parse_error(node->data.block.commands[i]);
      if (msg) return msg;
    }
  }
  /* Search inside commands (proc bodies, etc.) */
  if (node->type == AST_COMMAND) {
    const char* msg = jacl__find_parse_error(node->data.command.head);
    if (msg) return msg;
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
      msg = jacl__find_parse_error(node->data.command.args[i]);
      if (msg) return msg;
    }
  }
  /* Search inside interpolated string segments — a `$[...]` / removed
     `$(...)` failure lives here, not at the top level. */
  if (node->type == AST_INTERP_STRING) {
    for (uint32_t i = 0; i < node->data.interp_string.count; i++) {
      const char* msg = jacl__find_parse_error(node->data.interp_string.segments[i]);
      if (msg) return msg;
    }
  }
  return NULL;
}

VMResult jacl_run(const char* source, VM* vm, arena_t* arena) {
  LexResult tokens = lexer_lex(source, arena);
  ParseResult parse = parser_parse(tokens, arena);
  if (parse.error_count > 0) {
    /* Extract first parse error message from AST_ERROR nodes (recursive) */
    const char* parse_err = NULL;
    for (uint32_t i = 0; i < parse.count && !parse_err; i++) {
      parse_err = jacl__find_parse_error(parse.nodes[i]);
    }
    if (parse_err) {
      /* Build "parse error: <detail>" message in arena */
      size_t prefix_len = 13; /* "parse error: " */
      size_t msg_len = strlen(parse_err);
      char* buf = (char*)arena_alloc(arena, (uint32_t)(prefix_len + msg_len + 1));
      memcpy(buf, "parse error: ", prefix_len);
      memcpy(buf + prefix_len, parse_err, msg_len + 1);
      vm->error_message = buf;
    } else {
      vm->error_message = "parse error";
    }
    return VM_RUNTIME_ERROR;
  }

  /* Allocate the intern table in the arena, not on the stack: jacl_run
   * stores its address in vm->intern_table and callers may read it back
   * after we return (e.g. to intern a key for a map produced by the run).
   * A stack-local would leave vm->intern_table dangling once this frame
   * unwinds — silent in normal mode, but TSAN catches the post-return
   * pthread_mutex_lock as use-of-destroyed-mutex. The pthread handle
   * itself is leaked until arena_destroy; on Unix it's pure userspace
   * state. */
  JaclInternTable *intern_table = (JaclInternTable*)arena_alloc(arena, sizeof(JaclInternTable));
  intern_table_init(intern_table, arena);

  ExpandState es;
  memset(&es, 0, sizeof(es));

  /* Create a temporary context for macro closure execution. */
  jacl_ctx_saved_t macro_saved;
  jacl_ctx_save(&macro_saved);
  jacl_context_t *macro_ctx = jacl_ctx_new(NULL);
  es.ctx = macro_ctx;

  CompileResult cr = compiler_compile(parse, arena, intern_table, &vm->heap, NULL, &es, JACL_NIL, NULL, NULL);

  jacl_ctx_destroy(macro_ctx);
  es.ctx = NULL;
  jacl_ctx_restore(macro_saved);

  if (cr.error_count > 0) {
    vm->error_message = cr.error_message ? cr.error_message : "compile error";
    struct_registry__destroy(cr.struct_registry);
    return VM_RUNTIME_ERROR;
  }

  vm->intern_table = intern_table;
  vm->struct_registry = cr.struct_registry;

  /* Initialize ctx subsystem (pool + initial ctx with pwd) */
  JaclCtxPool run_ctx_pool;
  ctx__init_vm(vm, &run_ctx_pool);

  if (cr.suspending) {
    /* Top-level code contains suspension. The chunk contains OP_CLOSURE + OP_HALT
       which produces the main closure on the stack. Execute the chunk to
       get the closure, then call it. */
    VMResult r = vm_exec(vm, &cr.chunk);
    if (r != VM_OK) {
      struct_registry__destroy(cr.struct_registry);
      return r;
    }

    JaclVal main_cl_val = vm->stack[0];
    if (!jacl_is_closure(main_cl_val)) {
      vm->error_message = "internal error: suspending top-level did not produce closure";
      struct_registry__destroy(cr.struct_registry);
      return VM_RUNTIME_ERROR;
    }
    JaclClosure *main_cl = jacl_as_closure(main_cl_val);

    JaclClosure top_closure_wrapper;
    memset(&top_closure_wrapper, 0, sizeof(top_closure_wrapper));
    top_closure_wrapper.chunk = cr.chunk;

    if (main_cl->is_sm_compiled) {
      /* SM main closure: create state machine, call with (sm_val, nil) */
      JaclVal sm_val = gc_alloc_state_machine(&vm->heap, main_cl->sm_field_count);
      JaclStateMachine *sm = jacl_as_state_machine(sm_val);
      vm__slot_set(vm, &sm->sm_closure, main_cl_val);

      vm->stack_top = 0;
      vm->stack[0]  = main_cl_val;
      vm->stack[1]  = sm_val;
      vm->stack[2]  = JACL_NIL;
      vm->stack_top = 3;

      vm->frames[0].closure    = &top_closure_wrapper;
      vm->frames[0].return_ip  = NULL;
      vm->frames[0].stack_base = 0;
      vm->frames[0].chunk      = &cr.chunk;
      vm->frame_count = 1;

      vm->frames[1].closure    = main_cl;
      vm->frames[1].return_ip  = NULL;
      vm->frames[1].stack_base = 1; /* 2 args (__sm, __rv) */
      vm->frames[1].chunk      = &main_cl->chunk;
      vm->frame_count = 2;
      vm->ip    = main_cl->chunk.code;
      vm->chunk = &main_cl->chunk;
      vm->top_chunk = &main_cl->chunk;

      r = vm__run(vm, 1);
      struct_registry__destroy(cr.struct_registry);
      return r;
    }

    /* CPS fallback: call with resolve_k */
    JaclVal completion = jacl_future(&vm->heap);
    JaclVal resolve_k = runtime__create_resolve_closure(&vm->heap, arena,
                                                         completion);

    vm->stack_top = 0;
    vm->stack[0]  = main_cl_val;
    vm->stack[1]  = resolve_k;
    vm->stack_top = 2;

    vm->frames[0].closure    = &top_closure_wrapper;
    vm->frames[0].return_ip  = NULL;
    vm->frames[0].stack_base = 0;
    vm->frames[0].chunk      = &cr.chunk;
    vm->frame_count = 1;

    vm->frames[1].closure    = main_cl;
    vm->frames[1].return_ip  = NULL;
    vm->frames[1].stack_base = 1;
    vm->frames[1].chunk      = &main_cl->chunk;
    vm->frame_count = 2;
    vm->ip    = main_cl->chunk.code;
    vm->chunk = &main_cl->chunk;
    vm->top_chunk = &main_cl->chunk;

    r = vm__run(vm, 1);

    JaclFuture *cfut = jacl_as_future(completion);
    uint32_t state = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_RELAXED);
    if (state == FUTURE_RESOLVED) {
      vm->stack[0] = (JaclVal)cfut->result;
      vm->stack_top = 1;
    } else if (state == FUTURE_ERROR) {
      vm->stack[0] = (JaclVal)cfut->result;
      vm->stack_top = 1;
    }
    struct_registry__destroy(cr.struct_registry);
    return r;
  }

  VMResult result = vm_exec(vm, &cr.chunk);
  struct_registry__destroy(cr.struct_registry);
  return result;
}

#endif /* VM_C */
