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

int main(int argc, char **argv) {
  /* `--oldvm <case>`: print the old VM's i32 result (the P2.10 oracle). */
  if (argc == 3 && !strcmp(argv[1], "--oldvm")) {
    const char *src = source_for(argv[2]);
    if (!src) { fprintf(stderr, "unknown case: %s\n", argv[2]); return 2; }
    return run_old_vm(src);
  }
  if (argc != 2) { fprintf(stderr, "usage: emit_jacl <case> | emit_jacl --oldvm <case>\n"); return 2; }
  const char *src = source_for(argv[1]);
  if (!src) { fprintf(stderr, "unknown case: %s\n", argv[1]); return 2; }

  arena_t arena = {0};
  LexResult toks = lexer_lex(src, &arena);
  if (toks.error_count) { fprintf(stderr, "lex errors: %u\n", toks.error_count); return 1; }
  ParseResult parse = parser_parse(toks, &arena);
  if (parse.error_count) { fprintf(stderr, "parse errors: %u\n", parse.error_count); return 1; }

  /* Type inference: annotates each node's inferred_type, which the codegen uses for
   * type-driven (unboxed) lowering. */
  typer_infer(parse.nodes, parse.count, NULL, NULL, 0, NULL, 0);

  char err[256] = {0};
  IrModule *m = svm_codegen_program(parse.nodes, parse.count, err, sizeof err);
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
