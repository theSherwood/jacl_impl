/* emit_jacl.c — drives the P2.2 codegen from real JACL source. Parses a named
 * source snippet through the JACL frontend (lexer + parser), runs svm_codegen_program,
 * and prints the resulting svm-text to stdout so a Rust harness test can link it
 * against the runtime and run it on interp + JIT.
 *
 *   emit_jacl <case>
 *
 * Cases map to small arithmetic programs (see source_for). Built with gcc (the
 * frontend's toolchain); the unity src/jacl.c provides lexer_lex / parser_parse. */
#include "../../src/jacl.c"   /* full JACL frontend (defines the parse pipeline) */

/* Bridge: codegen synthesizes sugar AST nodes (implicit-`it` lambda bodies) and
 * needs head-id interning, which is static inside the frontend unity. */
HeadId jacl_compute_head_id_bridge(AstNode *head) { return ast__compute_head_id(head); }

#include "../codegen.h"
#include "../irbuilder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *source_for(const char *name) {
  if (!strcmp(name, "add"))      return "[+ 1 2]";
  if (!strcmp(name, "nested"))   return "[+ 1 [* 2 3]]";
  if (!strcmp(name, "submul"))   return "[- [* 4 5] 3]";
  if (!strcmp(name, "variadic")) return "[+ 1 2 3 4]";
  if (!strcmp(name, "div_mod"))  return "[+ [/ 20 6] [% 20 6]]";
  /* P2.3 bindings & scope */
  if (!strcmp(name, "bind_read"))    return "def x 5\n[+ $x 10]";
  if (!strcmp(name, "mutate"))       return "mut c 1\nset c 42\n[+ $c 0]";
  if (!strcmp(name, "reassign_expr"))return "mut c 1\nset c [+ $c 41]\n[+ $c 0]";
  if (!strcmp(name, "scoped_block")) return "def x 1\n{ def y 10\n[+ $x $y] }";
  /* error cases (driver exits nonzero) */
  if (!strcmp(name, "shadow_err"))   return "def x 1\ndef x 2";
  if (!strcmp(name, "set_undef_err"))return "set nope 1";
  if (!strcmp(name, "set_immut_err"))return "def x 1\nset x 2";
  /* P2.4 control flow */
  if (!strcmp(name, "if_true"))        return "[if [> 5 3] { 10 } else { 20 }]";
  if (!strcmp(name, "if_false"))       return "[if [> 1 3] { 10 } else { 20 }]";
  if (!strcmp(name, "if_noelse_true")) return "[if [> 5 3] { 7 }]";
  if (!strcmp(name, "if_noelse_false"))return "[if [> 1 3] { 7 }]";
  if (!strcmp(name, "if_sets_outer"))  return "mut x 1\n[if [> 5 3] { set x 100 }]\n[+ $x 0]";
  if (!strcmp(name, "while_sum"))
    return "mut i 1\nmut acc 0\n[while [<= $i 5] { set acc [+ $acc $i]\nset i [+ $i 1] }]\n[+ $acc 0]";
  if (!strcmp(name, "while_countdown"))
    return "mut n 3\nmut c 0\n[while [> $n 0] { set n [- $n 1]\nset c [+ $c 10] }]\n[+ $c 0]";
  if (!strcmp(name, "elif_chain"))
    return "[if [> 1 2] { 1 } elif [> 3 2] { 22 } else { 33 }]";
  if (!strcmp(name, "if_expr_bind"))
    return "def x [if [> 5 3] { 9 } else { 0 }]\n[+ $x 0]";
  if (!strcmp(name, "for_sum"))
    return "mut acc 0\n[for i in [range 0 5] { set acc [+ $acc $i] }]\n[+ $acc 0]";
  if (!strcmp(name, "for_range_cmd"))
    return "mut acc 0\n[for i in [range 1 4] { set acc [+ $acc $i] }]\n[+ $acc 0]";
  if (!strcmp(name, "for_dotdot"))
    return "mut acc 0\nfor i in 0..5 { set acc [+ $acc $i] }\n[+ $acc 0]";
  if (!strcmp(name, "for_with_if"))
    return "mut acc 0\n[for i in [range 0 5] { [if [> $i 2] { set acc [+ $acc $i] }] }]\n[+ $acc 0]";
  /* P2.5 procs & calls */
  if (!strcmp(name, "proc_add"))
    return "proc add {x, y} {+ $x $y}\n[add 40 2]";
  if (!strcmp(name, "proc_typed"))
    return "proc add {i32 x, i32 y} i32 {+ $x $y}\n[add 40 2]";
  if (!strcmp(name, "proc_zero_arg"))
    return "proc answer {} {42}\n[answer]";
  if (!strcmp(name, "proc_calls_proc"))
    return "proc inc {x} {+ $x 1}\nproc dbl {x} {* $x 2}\n[dbl [inc 20]]";
  if (!strcmp(name, "proc_recursion"))
    return "proc fac {n} {[if [<= $n 1] { 1 } else { [* $n [fac [- $n 1]]] }]}\n[fac 5]";
  if (!strcmp(name, "proc_return"))
    return "proc f {n} {return [+ $n 1]}\n[f 41]";
  if (!strcmp(name, "proc_local_loop"))
    return "proc sumto {n} {mut acc 0\nmut i 1\n[while [<= $i $n] { set acc [+ $acc $i]\nset i [+ $i 1] }]\n[+ $acc 0]}\n[sumto 5]";
  if (!strcmp(name, "proc_arity_err"))
    return "proc add {x, y} {+ $x $y}\n[add 1]";
  if (!strcmp(name, "proc_tail_rec"))
    return "proc count {n, acc} {[if [<= $n 0] { $acc } else { [count [- $n 1] [+ $acc 1]] }]}\n[count 5 0]";
  if (!strcmp(name, "proc_tail_deep"))
    return "proc count {n, acc} {[if [<= $n 0] { $acc } else { [count [- $n 1] [+ $acc 1]] }]}\n[count 200000 0]";
  /* P2.6 closures */
  if (!strcmp(name, "closure_basic"))
    return "def f [\\ {x} { [+ $x 1] }]\n[$f 41]";
  if (!strcmp(name, "closure_capture"))
    return "def y 10\ndef f [\\ {x} { [+ $x $y] }]\n[$f 32]";
  if (!strcmp(name, "closure_anon_proc"))
    return "def f [proc {x} { [* $x 2] }]\n[$f 21]";
  if (!strcmp(name, "closure_multi_capture"))
    return "def a 100\ndef b 20\ndef f [\\ {x} { [+ [+ $a $b] $x] }]\n[$f 2]";
  if (!strcmp(name, "closure_returned_from_proc"))
    return "proc adder {n} { [\\ {x} { [+ $x $n] }] }\ndef add5 [adder 5]\n[$add5 37]";
  if (!strcmp(name, "closure_nested"))
    return "def a 1\ndef f [\\ {x} { [\\ {y} { [+ [+ $a $x] $y] }] }]\ndef g [$f 10]\n[$g 31]";
  /* P2.6b mut-capture cells (shared mutation) */
  if (!strcmp(name, "closure_mut_capture"))
    return "mut c 0\ndef inc [\\ {} { set c [+ $c 1] }]\n[$inc]\n[$inc]\n[+ $c 0]";
  if (!strcmp(name, "closure_counter_factory"))
    return "proc make {} { mut n 0\n[\\ {} { set n [+ $n 1] }] }\ndef c [make]\n[$c]\n[$c]\n[$c]";
  if (!strcmp(name, "closure_shared_mut"))
    return "mut x 10\ndef get [\\ {} { [+ $x 0] }]\ndef setx [\\ {v} { set x $v }]\n[$setx 42]\n[$get]";
  /* P2.8 strings + collections (checked via [length …] so the result is an i32) */
  if (!strcmp(name, "str_len"))        return "[length \"hello\"]";
  if (!strcmp(name, "str_concat_len")) return "[length [concat \"ab\" \"cde\"]]";
  if (!strcmp(name, "interp_len"))     return "def n 7\n[length \"val=$n\"]";
  if (!strcmp(name, "vec_len"))        return "[length [vec 1 2 3]]";
  if (!strcmp(name, "vec_len4"))       return "[length [vec 10 20 30 40]]";
  /* P2.10 bring-up — combined programs across the supported subset, each returning an
   * i32 so the result can be diffed against the old VM. */
  if (!strcmp(name, "bring_bindings"))
    return "def x 5\ndef y 37\n[+ $x $y]";
  if (!strcmp(name, "bring_nested_arith"))
    return "[+ 1 [* 2 [- 10 5]]]";
  if (!strcmp(name, "bring_factorial"))  /* braces-only else (canonical 3-arg if) */
    return "proc fac {n} {[if [<= $n 1] { 1 } { [* $n [fac [- $n 1]]] }]}\n[fac 6]";
  if (!strcmp(name, "bring_while_sum"))
    return "mut acc 0\nmut i 1\n[while [<= $i 10] { set acc [+ $acc $i]\nset i [+ $i 1] }]\n[+ $acc 0]";
  if (!strcmp(name, "bring_if"))
    return "def n 7\n[if [> $n 5] { 100 } { 200 }]";
  if (!strcmp(name, "bring_closure"))    /* anonymous proc (canonical) capturing $y */
    return "def y 10\ndef f [proc {x} { [+ $x $y] }]\n[$f 32]";
  if (!strcmp(name, "bring_counter"))    /* anonymous proc capturing a mutable */
    return "mut c 0\ndef inc [proc {} { set c [+ $c 1] }]\n[$inc]\n[$inc]\n[$inc]\n[+ $c 0]";
  if (!strcmp(name, "bring_str"))
    return "[length [concat \"foo\" \"barbaz\"]]";
  if (!strcmp(name, "bring_hof"))
    return "proc apply {f, x} { [$f $x] }\ndef dbl [proc {n} { [* $n 2] }]\n[apply $dbl 21]";
  if (!strcmp(name, "bring_proc_chain"))
    return "proc add {a, b} {+ $a $b}\nproc dbl {n} {* $n 2}\n[dbl [add 19 2]]";
  if (!strcmp(name, "bring_recur_sum")) /* sum 1..n recursively */
    return "proc sumto {n} {[if [<= $n 0] { 0 } { [+ $n [sumto [- $n 1]]] }]}\n[sumto 10]";
  /* P3.1 generators (yield) on fibers, driven by canonical for-over-generator */
  if (!strcmp(name, "gen_count"))
    return "proc upto {n} { mut i 0\n[while [< $i $n] { yield $i\nset i [+ $i 1] }] }\nmut s 0\n[for [upto 5] x { set s [+ $s $x] }]\n[+ $s 0]";
  if (!strcmp(name, "gen_squares"))
    return "proc sq {n} { mut i 1\n[while [<= $i $n] { yield [* $i $i]\nset i [+ $i 1] }] }\nmut s 0\n[for [sq 4] x { set s [+ $s $x] }]\n[+ $s 0]";
  /* P3.2 spawn / await (futures over fibers) */
  if (!strcmp(name, "spawn_await"))
    return "def f [spawn { [+ 40 2] }]\n[await $f]";
  if (!strcmp(name, "spawn_capture"))
    return "def y 30\ndef f [spawn { [+ $y 12] }]\n[await $f]";
  if (!strcmp(name, "spawn_two"))
    return "def a [spawn { [* 6 7] }]\ndef b [spawn { [+ 1 2] }]\n[+ [await $a] [await $b]]";
  if (!strcmp(name, "spawn_await_twice")) /* future resolves once, cached */
    return "def f [spawn { [+ 20 1] }]\n[+ [await $f] [await $f]]";
  /* P3.3 parallel / race */
  if (!strcmp(name, "parallel"))
    return "def [a b] [parallel { [* 6 7] } { [+ 1 2] }]\n[+ $a $b]";
  if (!strcmp(name, "parallel3"))
    return "def [a b c] [parallel { 10 } { 20 } { 12 }]\n[+ [+ $a $b] $c]";
  if (!strcmp(name, "race"))
    return "[race { [+ 40 2] } { [+ 100 100] }]";
  /* nested await: a spawned task awaits another task's future (needs the parking scheduler) */
  if (!strcmp(name, "nested_await"))
    return "def a [spawn { [+ 40 2] }]\ndef b [spawn { [+ [await $a] 0] }]\n[await $b]";
  /* P3.5 host I/O — print through the powerbox */
  if (!strcmp(name, "print_str"))
    return "[print \"hello\"]";
  if (!strcmp(name, "print_int"))
    return "[print [+ 40 2]]";
  if (!strcmp(name, "print_two"))
    return "[print \"hi\"]\n[print 7]";
  return NULL;
}

/* Run `src` through the old bytecode VM and print its i32 result — the reference for
 * the P2.10 diff (the driver already links the whole frontend + VM). */
static int run_old_vm(const char *src) {
  JaclVM *vm = jacl_vm_new();
  JaclVal r = jacl_eval(vm, src);
  int rc = 0;
  /* Accept any integer width — a `dyn` result (closures, recursion, if-exprs) is often
   * i64 in the VM. We compare the numeric value, which the codegen models in i32. */
  if (jacl_is_i32(r))      printf("%d\n", jacl_as_i32(r));
  else if (jacl_is_u32(r)) printf("%lld\n", (long long)(uint32_t)jacl_as_i64(r));
  else if (jacl_is_i64(r)) printf("%lld\n", (long long)jacl_as_i64(r));
  else if (jacl_is_u64(r)) printf("%llu\n", (unsigned long long)jacl_as_u64(r));
  else { fprintf(stderr, "old vm result is not an integer\n"); rc = 3; }
  jacl_vm_free(vm);
  return rc;
}

/* Read a whole source file (for `--file`: the parity runner drives the corpus
 * `test/jacl/*.jacl` through the same pipeline as the named snippets). */
static char *read_whole_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)n + 1);
  if (!buf) { fclose(f); return NULL; }
  if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
  buf[n] = 0;
  fclose(f);
  return buf;
}

/* Recursively find the first AST_ERROR node's message anywhere in the tree (parser errors
 * are frequently nested inside a command arg, block, return, or interpolation segment). */
static const char *find_ast_error(AstNode *n) {
  if (!n) return NULL;
  if (n->type == AST_ERROR) return n->data.error.message;
  const char *m;
  if (n->type == AST_COMMAND) {
    if ((m = find_ast_error(n->data.command.head))) return m;
    for (uint32_t i = 0; i < n->data.command.arg_count; i++)
      if ((m = find_ast_error(n->data.command.args[i]))) return m;
  } else if (n->type == AST_BLOCK) {
    for (uint32_t i = 0; i < n->data.block.count; i++)
      if ((m = find_ast_error(n->data.block.commands[i]))) return m;
  } else if (n->type == AST_RETURN) {
    if ((m = find_ast_error(n->data.return_stmt.value))) return m;
  } else if (n->type == AST_INTERP_STRING) {
    for (uint32_t i = 0; i < n->data.interp_string.count; i++)
      if ((m = find_ast_error(n->data.interp_string.segments[i]))) return m;
  }
  return NULL;
}

/* Concatenate a compiled program's modules into one flat top-level form list for SVM codegen.
 * The reference module model is a single flat global namespace: every module's procs, globals,
 * and structs share it, disambiguated only by name, and modules execute deps-first (topo order,
 * root last) into that one env. So a destructuring `use "path" {names}` is a no-op for codegen —
 * the imported names are already defined by the (concatenated) dependency — and cross-module
 * calls / shared mutable globals / run-once side-effects all fall out of the concatenation.
 * (`jacl_compile_program` already validated privacy / unknown-export / arity / type errors.)
 * Destructuring `use {names}` nodes are dropped. A whole-module binding `use "path" name` is
 * kept and its `names[]` filled with the dependency's export names (resolved here, where the
 * ProgramResult is in scope) so codegen can materialize the export map. */
static void fill_module_binding_exports(AstNode *use, ProgramResult *prog,
                                        const char *importer_path, arena_t *arena) {
  const char *resolved = module__resolve_path(importer_path, use->data.use_decl.path, arena);
  if (!resolved) return;
  Module *dep = NULL;
  for (uint32_t i = 0; i < prog->module_count; i++)
    if (prog->modules[i]->path && !strcmp(prog->modules[i]->path, resolved)) { dep = prog->modules[i]; break; }
  if (!dep) return;
  const char **names = (const char **)arena_alloc(arena, (uint32_t)(dep->export_count * sizeof(char *)));
  uint32_t *lens = (uint32_t *)arena_alloc(arena, (uint32_t)(dep->export_count * sizeof(uint32_t)));
  for (uint32_t j = 0; j < dep->export_count; j++) {
    names[j] = dep->exports[j].name;
    lens[j] = dep->exports[j].name_len;
  }
  use->data.use_decl.names = names;
  use->data.use_decl.name_lens = lens;
  use->data.use_decl.name_count = dep->export_count;
}

static AstNode **combine_modules(ProgramResult *prog, arena_t *arena, uint32_t *out_count) {
  ParseResult parses[MODULE_CACHE_MAX];
  uint32_t n = prog->module_count < MODULE_CACHE_MAX ? prog->module_count : MODULE_CACHE_MAX;
  uint32_t total = 0;
  for (uint32_t i = 0; i < n; i++) {
    LexResult t = lexer_lex(prog->modules[i]->source, arena);
    parses[i] = parser_parse(t, arena);
    for (uint32_t j = 0; j < parses[i].count; j++) {
      AstNode *nd = parses[i].nodes[j];
      if (nd->type != AST_USE || nd->data.use_decl.is_module_binding) total++;
    }
  }
  AstNode **out = (AstNode **)arena_alloc(arena, (uint32_t)(total * sizeof(AstNode *)));
  uint32_t k = 0;
  for (uint32_t i = 0; i < n; i++)
    for (uint32_t j = 0; j < parses[i].count; j++) {
      AstNode *nd = parses[i].nodes[j];
      if (nd->type == AST_USE) {
        if (!nd->data.use_decl.is_module_binding) continue;   /* destructuring: no-op, drop */
        fill_module_binding_exports(nd, prog, prog->modules[i]->path, arena);
      }
      out[k++] = nd;
    }
  *out_count = k;
  return out;
}

int main(int argc, char **argv) {
  /* `--oldvm <case>`: print the old VM's i32 result (the P2.10 oracle). */
  if (argc == 3 && !strcmp(argv[1], "--oldvm")) {
    const char *src = source_for(argv[2]);
    if (!src) { fprintf(stderr, "unknown case: %s\n", argv[2]); return 2; }
    return run_old_vm(src);
  }
  const char *src = NULL;
  const char *file_path = NULL;   /* set in --file mode; enables module-aware compilation */
  if (argc == 3 && !strcmp(argv[1], "--file")) {
    src = read_whole_file(argv[2]);
    if (!src) { fprintf(stderr, "cannot read: %s\n", argv[2]); return 2; }
    file_path = argv[2];
  } else if (argc == 2) {
    src = source_for(argv[1]);
    if (!src) { fprintf(stderr, "unknown case: %s\n", argv[1]); return 2; }
  } else {
    fprintf(stderr, "usage: emit_jacl <case> | emit_jacl --file <path> | emit_jacl --oldvm <case>\n");
    return 2;
  }

  arena_t arena = {0};
  LexResult toks = lexer_lex(src, &arena);
  if (toks.error_count) { fprintf(stderr, "lex errors: %u\n", toks.error_count); return 1; }
  ParseResult parse = parser_parse(toks, &arena);
  if (parse.error_count) {
    /* Surface the first AST_ERROR's message (as jacl_eval does) so `# expect-error`
     * oracles for parse-level rejections match, instead of a generic "parse errors: N".
     * The error node is often NESTED (a ctx decl inside a block, a `(` inside an
     * expression), so search recursively rather than only the top-level forms. */
    const char *first_err = NULL;
    for (uint32_t i = 0; i < parse.count && !first_err; i++)
      first_err = find_ast_error(parse.nodes[i]);
    /* Prefix `parse error:` — the corpus's parse-level oracles are written that way. */
    if (first_err) fprintf(stderr, "parse error: %s\n", first_err);
    else fprintf(stderr, "parse errors: %u\n", parse.error_count);
    return 1;
  }

  /* A program with a top-level `use` is a module program: its error oracle must be the
   * module-aware compiler (import-graph resolution, circular-import + cross-module type/arity
   * checks), not the single-file pass, which rejects any `use` as "requires module context". */
  AstNode **cg_nodes = parse.nodes;   /* what codegen compiles: the root file, or (for a module */
  uint32_t cg_count = parse.count;    /* program) all modules' forms concatenated in topo order */
  int is_module_program = 0;
  for (uint32_t i = 0; i < parse.count; i++)
    if (parse.nodes[i]->type == AST_USE) { is_module_program = 1; break; }

  if (is_module_program && file_path) {
    char abs_path[4096];
    if (!realpath(file_path, abs_path)) {
      fprintf(stderr, "module not found: %s\n", file_path);
      return 1;
    }
    JaclVM *mvm = jacl_vm_new();
    ProgramResult prog =
        jacl_compile_program(abs_path, &mvm->arena, &mvm->intern_table, &mvm->vm.heap);
    if (prog.error_count > 0) {
      fprintf(stderr, "%s\n", prog.error_message ? prog.error_message : "compile error");
      return 1;   /* mvm leaked deliberately: its arena may back prog.error_message */
    }
    /* Compile the whole import graph as one flat program (deps first, root last). */
    cg_nodes = combine_modules(&prog, &arena, &cg_count);
  } else
  /* Static-error oracle: run the old VM's full compiler purely for its compile-time
   * diagnostics (typed def/set/arith/proc-arg/convert mismatches, struct/ctx/buf/
   * generator/typed-closure conformance, …). Every corpus program is written for the
   * old VM, so the compiler accepts each passing case and rejects each `# expect-error`
   * case with its canonical message — exactly the accept/reject the SVM backend should
   * mirror. On rejection we surface the message and stop before codegen.
   *
   * The compiler runs on its OWN re-lexed/re-parsed AST (a second parse into chk's
   * arena): both the compiler's conformance pass and the SVM codegen mutate their AST
   * in place, so sharing one tree cross-contaminates them. Isolating the two parses
   * lets the compiler give its canonical message while codegen still sees pristine
   * nodes. */
  {
    JaclVM *chk = jacl_vm_new();
    LexResult ctoks = lexer_lex(src, &chk->arena);
    ParseResult cparse = parser_parse(ctoks, &chk->arena);
    if (!ctoks.error_count && !cparse.error_count) {
      ExpandState es;
      memset(&es, 0, sizeof(es));
      jacl_ctx_saved_t macro_saved;
      jacl_ctx_save(&macro_saved);
      jacl_context_t *macro_ctx = jacl_ctx_new(NULL);
      es.ctx = macro_ctx;
      CompileResult cr = compiler_compile(cparse, &chk->arena, &chk->intern_table,
                                          &chk->vm.heap, chk->persistent_struct_registry,
                                          &es, JACL_NIL, &chk->persistent_arities, &chk->arena);
      jacl_ctx_destroy(macro_ctx);
      es.ctx = NULL;
      jacl_ctx_restore(macro_saved);
      if (cr.error_count > 0) {
        fprintf(stderr, "%s\n", cr.error_message ? cr.error_message : "compile error");
        return 1;   /* chk leaked deliberately: its arena may back cr.error_message */
      }
    }
  }

  /* Type inference: annotates each node's inferred_type, which the codegen uses for
   * type-driven (unboxed) lowering. */
  typer_infer(cg_nodes, cg_count, NULL, NULL, 0, NULL, 0);

  char err[256] = {0};
  IrModule *m = svm_codegen_program(cg_nodes, cg_count, is_module_program, err, sizeof err);
  if (!m) { fprintf(stderr, "%s\n", err); arena_destroy(&arena); return 1; }

  char *text = irb_to_text(m);
  fputs(text, stdout);
  free(text);
  /* Data relocations follow the module after a sentinel (the harness splits on it and
   * feeds them to svm_ir::link as SelfData relocs). */
  char *relocs = irb_relocs_text(m);
  if (relocs[0]) { fputs("%%RELOCS%%\n", stdout); fputs(relocs, stdout); }
  free(relocs);
  irb_module_free(m);
  arena_destroy(&arena);
  return 0;
}
