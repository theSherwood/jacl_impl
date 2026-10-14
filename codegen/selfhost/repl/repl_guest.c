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
extern int __vm_atomic_cas32(int *p, int expected, int desired);
extern void __vm_atomic_store32(int *p, int v);

/* jaclrt (runtime/), which the frontend's jacl.h does not declare. */
extern uint32_t jacl_str_len(JaclVal s);
extern void jacl_str_bytes(JaclVal s, char *out, uint32_t cap);
extern JaclVal jacl_str_new(const char *s, uint32_t len);
extern JaclVal jacl_str_concat(JaclVal a, JaclVal b);
extern JaclVal jacl_error_new(JaclVal v);

/* The frontend's macro expander over the session's table (codegen/tests/emit_jacl.c). */
extern const char *expand_macros_repl(AstNode **nodes, uint32_t count, arena_t *arena);

/* Every runtime function a unit may import, by name (the generated repl_symtab.c). */
extern size_t jacl_repl_symtab(unsigned char *out, size_t cap);

/* A data-free string literal (codegen's data_free lowering): a unit carries no data segment, so
 * each literal is built from its bytes, here, in the runtime. */
JaclVal synrt_str_empty(void) { return jacl_str_new("", 0); }
JaclVal synrt_str_push_byte(JaclVal s, long b) {
  char c = (char)b;
  return jacl_str_concat(s, jacl_str_new(&c, 1));
}

static JaclVal repl_error(const char *msg) {
  return jacl_error_new(jacl_str_new(msg, (uint32_t)strlen(msg)));
}

#define REPL_MAX_GLOBALS 1024
#define REPL_MAX_STRUCTS 32

static arena_t repl_arena;   /* every line's source and AST, for the session */
static const char *repl_globals[REPL_MAX_GLOBALS];
static uint32_t repl_global_lens[REPL_MAX_GLOBALS];
static uint32_t repl_nglobals;
static AstNode *repl_structs[REPL_MAX_STRUCTS];
static uint32_t repl_nstructs;
static unsigned char *repl_symtab;
static size_t repl_symtab_len;
static uint8_t repl_log2;

static void repl_note_global(const char *s, uint32_t n) {
  for (uint32_t i = 0; i < repl_nglobals; i++)
    if (repl_global_lens[i] == n && memcmp(repl_globals[i], s, n) == 0) return;
  if (repl_nglobals < REPL_MAX_GLOBALS) {
    repl_globals[repl_nglobals] = s;
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
  char *src;
  long slot;
  int leaf;
  const char *why;
  char err[256];
} ReplLine;

/* Compiles a line (a `ReplLine *`) and installs its unit. Runs on a fiber of its own, over
 * `repl_stack`: the compiler's frames would overflow a JACL task's 64 KiB data stack, and a fiber's
 * stack has no bound but the memory after it. */
static long repl_compile(long arg) {
  ReplLine *ln = (ReplLine *)(uintptr_t)arg;
  LexResult toks = lexer_lex(ln->src, &repl_arena);
  if (toks.error_count) { ln->why = "repl-eval: the line does not lex"; return 0; }
  ParseResult parse = parser_parse(toks, &repl_arena);
  if (parse.error_count) { ln->why = "repl-eval: the line does not parse"; return 0; }
  const char *merr = expand_macros_repl(parse.nodes, parse.count, &repl_arena);
  if (merr) { ln->why = merr; return 0; }
  typer_infer(parse.nodes, parse.count, NULL, NULL, 0, NULL, 0);

  TemenReplEnv env = {repl_globals, repl_global_lens, repl_nglobals, repl_structs, repl_nstructs};
  IrModule *m = temen_codegen_repl(parse.nodes, parse.count, &env, ln->err, sizeof ln->err);
  if (!m) { ln->why = ln->err[0] ? ln->err : "repl-eval: the line does not compile"; return 0; }
  int l = repl_window_log2();
  if (l < 0) { irb_module_free(m); ln->why = "repl-eval: no Jit capability"; return 0; }
  irb_set_memory(m, (uint8_t)l);
  ln->leaf = irb_module_is_leaf(m);
  size_t len = 0;
  uint8_t *bytes = irb_to_encoded(m, &len);
  irb_module_free(m);
  if (!bytes) { ln->why = "repl-eval: the unit does not encode"; return 0; }

  if (!repl_symtab) {
    repl_symtab_len = jacl_repl_symtab(NULL, 0);
    repl_symtab = (unsigned char *)malloc(repl_symtab_len);
    if (!repl_symtab) { free(bytes); ln->why = "repl-eval: out of memory"; return 0; }
    jacl_repl_symtab(repl_symtab, repl_symtab_len);
  }
  long code = __vm_jit_compile_linked(bytes, (long)len, repl_symtab, (long)repl_symtab_len);
  free(bytes);
  if (code < 0) { ln->why = "repl-eval: the Jit capability refused the unit"; return 0; }
  ln->slot = __vm_jit_install(code);
  /* The installed unit lives on in its slot. Its handle is a capability, which the line never
   * needs again, and the table holds 256 (temen §22): kept, they would end a session at about
   * that many lines. */
  (void)__vm_jit_release(code);
  if (ln->slot < 0) { ln->why = "repl-eval: no slot to install the unit in"; return 0; }
  repl_note(parse.nodes, parse.count);
  return 0;
}

static __attribute__((aligned(16))) char repl_stack[2u << 20];
static int repl_compiling;   /* repl_stack's one user: a line compiles at a time */

/* `src` is a string (runtime/interpcap.c checks). */
JaclVal jacl_repl_eval_impl(JaclVal src) {
  uint32_t slen = jacl_str_len(src);
  ReplLine ln = {0};
  ln.src = (char *)arena_alloc(&repl_arena, (size_t)slen + 1);   /* the AST points into it */
  if (!ln.src) return repl_error("repl-eval: out of memory");
  jacl_str_bytes(src, ln.src, slen + 1);
  ln.src[slen] = 0;

  if (__vm_atomic_cas32(&repl_compiling, 0, 1) != 0)
    return repl_error("repl-eval: another line is compiling");
  long k = __vm_fiber_new(repl_compile, repl_stack);
  int done = 0;
  (void)__vm_fiber_resume(k, (long)(uintptr_t)&ln, &done);
  __vm_atomic_store32(&repl_compiling, 0);
  if (ln.why) return repl_error(ln.why);
  if (ln.slot <= 0) return repl_error("repl-eval: the compiler did not finish");

  /* The entry has the guest ABI `(sp, _) -> value`: called through a pointer, it runs on this
   * fiber, over this frame's data stack, so a line may await and park as the shell does. */
  long (*line)(long) = (long (*)(long))(uintptr_t)ln.slot;
  JaclVal v = (JaclVal)line(0);
  /* A leaf line has returned, and nothing it made refers to its code: its slot goes back to the
   * table, so a session runs as many such lines as it likes. A line that defines a proc or makes
   * a closure keeps its slot for the session. */
  if (ln.leaf) (void)__vm_jit_uninstall(ln.slot);
  return v;
}
