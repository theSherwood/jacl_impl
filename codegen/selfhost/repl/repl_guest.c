/* repl_guest.c — the JACL shell's REPL (jacl_impl#204), linked into the shell's image with the
 * frontend and codegen (runtime/harness/src/repl.rs). `[repl-eval SRC]` (runtime/interpcap.c)
 * compiles SRC as one line against what earlier lines defined, has the §22 Jit capability compile
 * the unit into this domain and install it, and calls it on the calling fiber, so a line may await,
 * spawn and park as the shell's own code does. Its value is a live JaclVal: nothing is marshalled.
 *
 * The environment is the runtime's global map (temen_codegen_repl), and what the frontend must know
 * of earlier lines: the names they bound, their structs and their macros, whose AST lives in one
 * arena for the session. */
#include "../../../src/jacl.h"   /* the frontend: lexer, parser, typer, AstNode, arena_t */
#include "../../codegen.h"
#include "../../irbuilder.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* §22 Jit capability (temen-llvm binds these to the capability's ops). */
extern long __vm_jit_compile_linked(void *ir, long ir_len, void *symtab, long symtab_len);
extern long __vm_jit_install(long code);
extern long __vm_jit_release(long code);
extern long __vm_jit_uninstall(long slot);
extern long __vm_fiber_new(long (*f)(long), void *stack);
extern long __vm_fiber_resume(long k, long arg, int *done);
extern long __vm_fiber_suspend(long v);
extern int __vm_atomic_cas32(int *p, int expected, int desired);
extern void __vm_atomic_store32(int *p, int v);
extern int __vm_atomic_add32(void *p, int v);
extern int __vm_atomic_load32(void *p);

/* jaclrt (runtime/), which the frontend's jacl.h does not declare. */
extern uint32_t jacl_str_len(JaclVal s);
extern void jacl_str_bytes(JaclVal s, char *out, uint32_t cap);
extern JaclVal jacl_str_new(const char *s, uint32_t len);
extern JaclVal jacl_error_new(JaclVal v);
extern long jacl_gc_collect_noting(uint8_t *seen, uint32_t n);
extern JaclVal jacl_proc_sig_forget(JaclVal fnref);

/* The frontend's macro expander over the session's table (codegen/tests/emit_jacl.c). */
extern const char *expand_macros_repl(AstNode **nodes, uint32_t count, arena_t *arena);

/* Macro staging on this runtime (#214): the bridge (macro_staging/stage_bridge_guest.c) and the
 * glue (stage_glue_guest.c, in repl_rt.c). */
extern void jacl_install_temen_stage_hook_live(const unsigned char *symtab, size_t len);
extern void synrt_set_window_log2(unsigned char n);

/* Every runtime function a unit may import, by name (the generated repl_symtab.c). */
extern size_t jacl_repl_symtab(unsigned char *out, size_t cap);

static JaclVal repl_error(const char *msg) {
  return jacl_error_new(jacl_str_new(msg, (uint32_t)strlen(msg)));
}

#define REPL_MAX_GLOBALS 1024
#define REPL_MAX_STRUCTS 32

static arena_t repl_arena;   /* the names lines define, for the session (each line's own arena is freed) */
static const char *repl_globals[REPL_MAX_GLOBALS];
static uint32_t repl_global_lens[REPL_MAX_GLOBALS];
static uint32_t repl_nglobals;
static AstNode *repl_structs[REPL_MAX_STRUCTS];
static uint32_t repl_nstructs;
static unsigned char *repl_symtab;
static size_t repl_symtab_len;
static uint8_t repl_log2;

/* #215: a unit that names its own functions (not a leaf), and its slots: one for each function,
 * which the unit fills as it compiles (temen §22 unit-own funcrefs) and its entry hands over as it
 * starts (temen_codegen_repl's `reg`), and the one its install takes. They go back to the table
 * once nothing can reach the unit's code: its line has returned, and no closure or generator a
 * collection keeps holds one of its slots. A frame of its code then is not live either: each lies
 * above its line's entry or above one that holds a closure of the unit (codegen.c,
 * emit_keep_self). A unit without a record, past REPL_MAX_UNITS, keeps its slots for the session.
 * Records are made and freed only while a line compiles, so one at a time. */
#define REPL_MAX_UNITS 4096
typedef struct {
  long install;      /* its install's slot; 0 for a free record */
  long *slots;       /* its functions' */
  uint32_t nslots, cap;
  int running;       /* 1 until its line returns */
  int partial;       /* a slot went unrecorded: kept for the session */
  int candidate;     /* repl_reclaim: its line had returned before the collection */
} ReplUnit;
static ReplUnit repl_units[REPL_MAX_UNITS];

static long repl_unit_new(long install) {
  for (long i = 0; i < REPL_MAX_UNITS; i++)
    if (!repl_units[i].install) {
      ReplUnit *u = &repl_units[i];
      u->install = install;
      u->nslots = 0;
      u->running = 1;
      u->partial = 0;
      return i + 1;
    }
  return 0;
}

/* `reg`'s call, from the unit's entry as its line starts: one of the unit's slots. */
JaclVal jacl_repl_unit_slot(long unit, long slot) {
  if (unit <= 0 || unit > REPL_MAX_UNITS) return 0;
  ReplUnit *u = &repl_units[unit - 1];
  if (u->nslots == u->cap) {
    uint32_t cap = u->cap ? 2 * u->cap : 8;
    long *s = (long *)realloc(u->slots, cap * sizeof *s);
    if (!s) { u->partial = 1; return 0; }
    u->slots = s;
    u->cap = cap;
  }
  u->slots[u->nslots++] = slot;
  return 0;
}

/* Frees the slots of every unit nothing can reach any more (ReplUnit). Its candidates are the units
 * whose lines had returned before the collection: a closure of one is then a closure the
 * collection keeps, since none of its frames can make one. Returns how many it freed. */
static int repl_reclaim(void) {
  uint32_t n = 0, any = 0;
  for (int i = 0; i < REPL_MAX_UNITS; i++) {
    ReplUnit *u = &repl_units[i];
    u->candidate = u->install && !u->partial && !__vm_atomic_load32(&u->running);
    if (!u->candidate) continue;
    any = 1;
    for (uint32_t k = 0; k < u->nslots; k++)
      if ((uint64_t)u->slots[k] >= n) n = (uint32_t)u->slots[k] + 1;
  }
  if (!any) return 0;
  uint8_t *seen = (uint8_t *)calloc((size_t)n + 1, 1);
  if (!seen) return 0;
  (void)jacl_gc_collect_noting(seen, n);
  int freed = 0;
  for (int i = 0; i < REPL_MAX_UNITS; i++) {
    ReplUnit *u = &repl_units[i];
    if (!u->candidate) continue;
    uint32_t k = 0;
    while (k < u->nslots && !seen[u->slots[k]]) k++;
    if (k < u->nslots) continue;
    for (k = 0; k < u->nslots; k++) {
      (void)__vm_jit_uninstall(u->slots[k]);
      (void)jacl_proc_sig_forget((JaclVal)u->slots[k]);   /* the slot's next function has its own */
    }
    (void)__vm_jit_uninstall(u->install);
    u->install = 0;
    freed++;
  }
  free(seen);
  return freed;
}

static void repl_note_global(const char *s, uint32_t n) {
  for (uint32_t i = 0; i < repl_nglobals; i++)
    if (repl_global_lens[i] == n && memcmp(repl_globals[i], s, n) == 0) return;
  if (repl_nglobals < REPL_MAX_GLOBALS) {
    char *c = (char *)arena_alloc(&repl_arena, n ? n : 1);
    if (!c) return;
    memcpy(c, s, n);
    repl_globals[repl_nglobals] = c;
    repl_global_lens[repl_nglobals++] = n;
  }
}

/* What a line defined, for the lines after it: each top-level `def`/`mut`/`proc` name, and its
 * structs. */
static void repl_note(AstNode **nodes, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    AstNode *n = nodes[i];
    if (n->type == AST_DEFSTRUCT) {
      if (repl_nstructs < REPL_MAX_STRUCTS) repl_structs[repl_nstructs++] = n;
      continue;
    }
    if (n->type != AST_COMMAND) continue;
    uint8_t hid = n->data.command.head_id;
    AstNode **a = n->data.command.args;
    uint32_t ac = n->data.command.arg_count;
    if (hid == HEAD_PROC && ac >= 1 && a[0]->type == AST_LIT_STRING) {
      repl_note_global(a[0]->data.lit_string.value, a[0]->data.lit_string.length);
    } else if ((hid == HEAD_DEF || hid == HEAD_MUT) && ac >= 2) {
      /* a leading type annotation (`def i64 x V`) puts the name second */
      AstNode *nm = (ac >= 3 && a[1]->type == AST_LIT_STRING) ? a[1] : a[0];
      if (nm->type == AST_LIT_STRING)
        repl_note_global(nm->data.lit_string.value, nm->data.lit_string.length);
    }
  }
}

/* The unit's window, log2: the Jit capability compiles only a unit that declares this domain's
 * memory, which nothing reports, so the first line finds it with an empty unit. */
static int repl_window_log2(void) {
  if (repl_log2) return repl_log2;
  unsigned char none[1] = {0};   /* an empty symbol table: a count of 0 */
  for (uint8_t l = 16; l <= 40; l++) {
    IrModule *m = irb_module_new();
    IrType p[] = {IRB_I64, IRB_I64};
    IrType r[] = {IRB_I64};
    IrFunc *f = irb_func_new(m, p, 2, r, 1);
    IrBlock b = irb_block(f, p, 2);
    IrVal z = irb_const_i64(f, b, 0);
    irb_return(f, b, &z, 1);
    irb_set_memory(m, l);
    size_t len = 0;
    uint8_t *bytes = irb_to_encoded(m, &len);
    irb_module_free(m);
    if (!bytes) return -1;
    long code = __vm_jit_compile_linked(bytes, (long)len, none, 1);
    free(bytes);
    if (code >= 0) {
      __vm_jit_release(code);
      repl_log2 = l;
      return l;
    }
  }
  return -1;
}

/* A line to compile, and what compiling it gave: the installed unit's slot, or why not; and
 * whether the unit is a leaf (irb_module_is_leaf), whose slot is free again once the line returns. */
typedef struct {
  /* The line's source, tokens and AST. Freed once the line has compiled, unless a later line's
   * compile needs its AST (`keep`): a struct's declaration or a macro's. A session that kept every
   * line's ran out of memory at about 1,350 lines (#215). */
  arena_t arena;
  int keep;
  char *src;
  long slot;
  int leaf;
  long unit;   /* its ReplUnit, 1-based; 0 for a leaf or none */
  const char *why;
  char err[256];
} ReplLine;

/* Compiles a line (a `ReplLine *`) and installs its unit. Runs on a fiber of its own, over
 * `repl_stack`: the compiler's frames would overflow a JACL task's 64 KiB data stack, and a fiber's
 * stack has no bound but the memory after it. */
static long repl_compile(long arg) {
  ReplLine *ln = (ReplLine *)(uintptr_t)arg;
  int l = repl_window_log2();
  if (l < 0) { ln->why = "repl-eval: no Jit capability"; return 0; }
  if (!repl_symtab) {
    size_t n = jacl_repl_symtab(NULL, 0);
    unsigned char *t = (unsigned char *)malloc(n);
    if (!t) { ln->why = "repl-eval: out of memory"; return 0; }
    jacl_repl_symtab(t, n);
    repl_symtab = t;
    repl_symtab_len = n;
    /* A macro's body runs as the lines that use it compile: a unit of its own, linked against the
     * same table and run on this runtime (#214). */
    synrt_set_window_log2((unsigned char)l);
    jacl_install_temen_stage_hook_live(repl_symtab, repl_symtab_len);
  }
  LexResult toks = lexer_lex(ln->src, &ln->arena);
  if (toks.error_count) { ln->why = "repl-eval: the line does not lex"; return 0; }
  ParseResult parse = parser_parse(toks, &ln->arena);
  if (parse.error_count) { ln->why = "repl-eval: the line does not parse"; return 0; }
  for (uint32_t i = 0; i < parse.count; i++)
    if (parse.nodes[i]->type == AST_DEFSTRUCT || parse.nodes[i]->type == AST_DEFMACRO) ln->keep = 1;
  /* A macro whose body does not compile is refused before the session's table holds it, where it
   * would fail each line that uses it, and the lines after compile as before. */
  for (uint32_t i = 0; i < parse.count; i++) {
    AstNode *d = parse.nodes[i];
    if (d->type != AST_DEFMACRO) continue;
    IrModule *body = temen_codegen_staged_macro((const char **)d->data.defmacro.param_names,
                                                d->data.defmacro.param_name_lens,
                                                d->data.defmacro.param_count,
                                                d->data.defmacro.variadic, d->data.defmacro.body,
                                                /*in_guest=*/2, ln->err, sizeof ln->err);
    if (!body) {
      ln->why = ln->err[0] ? ln->err : "repl-eval: the macro's body does not compile";
      return 0;
    }
    irb_module_free(body);
  }
  const char *merr = expand_macros_repl(parse.nodes, parse.count, &ln->arena);
  if (merr) { ln->why = merr; return 0; }
  typer_infer(parse.nodes, parse.count, NULL, NULL, 0, NULL, 0);

  TemenReplEnv env = {repl_globals, repl_global_lens, repl_nglobals, repl_structs, repl_nstructs};
  IrModule *m = temen_codegen_repl(parse.nodes, parse.count, &env, ln->err, sizeof ln->err);
  if (!m) { ln->why = ln->err[0] ? ln->err : "repl-eval: the line does not compile"; return 0; }
  irb_set_memory(m, (uint8_t)l);
  ln->leaf = irb_module_is_leaf(m);
  size_t len = 0;
  uint8_t *bytes = irb_to_encoded(m, &len);
  irb_module_free(m);
  if (!bytes) { ln->why = "repl-eval: the unit does not encode"; return 0; }

  /* A unit that names its own functions takes a slot for each as it compiles, and its install takes
   * one more: on a full table, free the units nothing reaches any more, and try again (#215). */
  long code = __vm_jit_compile_linked(bytes, (long)len, repl_symtab, (long)repl_symtab_len);
  if (code < 0 && repl_reclaim())
    code = __vm_jit_compile_linked(bytes, (long)len, repl_symtab, (long)repl_symtab_len);
  free(bytes);
  if (code < 0) { ln->why = "repl-eval: the Jit capability refused the unit"; return 0; }
  ln->slot = __vm_jit_install(code);
  if (ln->slot < 0 && repl_reclaim()) ln->slot = __vm_jit_install(code);
  /* The installed unit lives on in its slot. Its handle is a capability, which the line never
   * needs again, and the table holds 256 (temen §22): kept, they would end a session at about
   * that many lines. */
  (void)__vm_jit_release(code);
  if (ln->slot < 0) { ln->why = "repl-eval: no slot to install the unit in"; return 0; }
  if (!ln->leaf) ln->unit = repl_unit_new(ln->slot);
  repl_note(parse.nodes, parse.count);
  return 0;
}

static __attribute__((aligned(16))) char repl_stack[2u << 20];
static int repl_compiling;   /* repl_stack's one user: a line compiles at a time */

/* `src` is a string (runtime/interpcap.c checks). */
JaclVal jacl_repl_eval_impl(JaclVal src) {
  uint32_t slen = jacl_str_len(src);
  ReplLine ln = {0};
  ln.src = (char *)arena_alloc(&ln.arena, (size_t)slen + 1);   /* the AST points into it */
  if (!ln.src) return repl_error("repl-eval: out of memory");
  jacl_str_bytes(src, ln.src, slen + 1);
  ln.src[slen] = 0;

  if (__vm_atomic_cas32(&repl_compiling, 0, 1) != 0) {
    arena_destroy(&ln.arena);
    return repl_error("repl-eval: another line is compiling");
  }
  long k = __vm_fiber_new(repl_compile, repl_stack);
  int done = 0;
  long r = __vm_fiber_resume(k, (long)(uintptr_t)&ln, &done);
  /* A macro's body allocates on this heap (#214), so a collection can stop the compile at a
   * safepoint, which suspends its fiber with 0. This task stops there in its stead, so the
   * collection scans both fibers (temen GC.md §3.1), and resumes the compile after. Nothing else
   * is passed on: a macro's body that waits leaves the line unfinished. */
  while (!done && r == 0) r = __vm_fiber_resume(k, __vm_fiber_suspend(0), &done);
  __vm_atomic_store32(&repl_compiling, 0);
  const char *why = ln.why ? ln.why : ln.slot <= 0 ? "repl-eval: the compiler did not finish" : 0;
  JaclVal err = why ? repl_error(why) : 0;   /* `why` may lie in the line's arena */
  if (!ln.keep) arena_destroy(&ln.arena);
  if (why) return err;

  /* The entry has the guest ABI `(sp, unit) -> value`: called through a pointer, it runs on this
   * fiber, over this frame's data stack, so a line may await and park as the shell does. */
  long (*line)(long) = (long (*)(long))(uintptr_t)ln.slot;
  JaclVal v = (JaclVal)line(ln.unit);
  /* A leaf line has returned, and nothing it made refers to its code: its slot goes back to the
   * table, so a session runs as many such lines as it likes. Another line's unit keeps its slots
   * until nothing reaches it (repl_reclaim). */
  if (ln.unit) (void)__vm_atomic_add32(&repl_units[ln.unit - 1].running, -1);
  if (ln.leaf) (void)__vm_jit_uninstall(ln.slot);
  return v;
}
