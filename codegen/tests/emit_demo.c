/* emit_demo.c — exercises the IR builder (P2.1). Builds a named module via the
 * irbuilder API and prints its svm-text to stdout, so a Rust harness test can
 * parse → verify → run it on interp + JIT ("Spike-1, but via the builder").
 *
 *   emit_demo <module>
 *
 * Modules: add2, sum_to, call_addone, mem_roundtrip, call_jacl_add. */
#include "../irbuilder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* add2(a, b) = a + b  — intbin + return. */
static IrModule *build_add2(void) {
  IrModule *m = irb_module_new();
  IrType i32[] = {IRB_I32, IRB_I32};
  IrType r[] = {IRB_I32};
  IrFunc *f = irb_func_new(m, i32, 2, r, 1);
  IrBlock b0 = irb_block(f, i32, 2);             /* block0(v0, v1) */
  IrVal sum = irb_intbin(f, b0, IRB_I32, IRB_ADD, 0, 1);
  IrVal ret[] = {sum};
  irb_return(f, b0, ret, 1);
  return m;
}

/* sum_to(n) = 1 + 2 + ... + n  — an SSA loop with block args (block-local SSA).
 * svm value numbering is per block: a block's params are v0..v{k-1}, then each
 * result-producing instruction takes the next local id. Operands reference only
 * this block's params + earlier results; loop-carried state crosses via block args. */
static IrModule *build_sum_to(void) {
  IrModule *m = irb_module_new();
  IrType i32_1[] = {IRB_I32};
  IrType i32_3[] = {IRB_I32, IRB_I32, IRB_I32};
  IrType r[] = {IRB_I32};
  IrFunc *f = irb_func_new(m, i32_1, 1, r, 1);

  IrBlock b0 = irb_block(f, i32_1, 1);           /* block0(n=v0) */
  IrBlock b1 = irb_block(f, i32_3, 3);           /* block1(n=v0, acc=v1, i=v2) */
  IrBlock b2 = irb_block(f, i32_3, 3);           /* block2(n=v0, acc=v1, i=v2) */
  IrBlock b3 = irb_block(f, i32_1, 1);           /* block3(acc=v0) */

  /* block0: acc=0, i=1 -> block1(n, acc, i) */
  IrVal acc0 = irb_const_i32(f, b0, 0);
  IrVal i0 = irb_const_i32(f, b0, 1);
  IrVal e0[] = {0, acc0, i0};
  irb_br(f, b0, b1, e0, 3);

  /* block1: if i<=n -> block2(n, acc, i) else block3(acc) */
  IrVal le = irb_intcmp(f, b1, IRB_I32, IRB_LE_S, /*i=*/2, /*n=*/0);
  IrVal then_args[] = {0, 1, 2};
  IrVal else_args[] = {1};
  irb_br_if(f, b1, le, b2, then_args, 3, b3, else_args, 1);

  /* block2: acc'=acc+i; i'=i+1 -> block1(n, acc', i') */
  IrVal acc1 = irb_intbin(f, b2, IRB_I32, IRB_ADD, /*acc=*/1, /*i=*/2);
  IrVal one = irb_const_i32(f, b2, 1);
  IrVal i1 = irb_intbin(f, b2, IRB_I32, IRB_ADD, /*i=*/2, one);
  IrVal back[] = {0, acc1, i1};
  irb_br(f, b2, b1, back, 3);

  /* block3: return acc */
  IrVal r3[] = {0};
  irb_return(f, b3, r3, 1);
  return m;
}

/* addone (func 0) and call_addone (func 1) — direct intra-module call. */
static IrModule *build_call_addone(void) {
  IrModule *m = irb_module_new();
  IrType i32_1[] = {IRB_I32};
  IrType r[] = {IRB_I32};

  IrFunc *addone = irb_func_new(m, i32_1, 1, r, 1);
  IrBlock a0 = irb_block(addone, i32_1, 1);
  IrVal one = irb_const_i32(addone, a0, 1);
  IrVal s = irb_intbin(addone, a0, IRB_I32, IRB_ADD, 0, one);
  IrVal ar[] = {s};
  irb_return(addone, a0, ar, 1);

  IrFunc *caller = irb_func_new(m, i32_1, 1, r, 1);
  IrBlock c0 = irb_block(caller, i32_1, 1);
  IrVal args[] = {0};
  IrVal got = irb_call(caller, c0, addone, args, 1);
  IrVal cr[] = {got};
  irb_return(caller, c0, cr, 1);
  return m;
}

/* mem_roundtrip(addr, val) = { store i64 val@addr; load i64 @addr } — load/store. */
static IrModule *build_mem_roundtrip(void) {
  IrModule *m = irb_module_new();
  irb_set_memory(m, 16);                          /* 64 KiB linear memory */
  IrType i64_2[] = {IRB_I64, IRB_I64};
  IrType r[] = {IRB_I64};
  IrFunc *f = irb_func_new(m, i64_2, 2, r, 1);
  IrBlock b0 = irb_block(f, i64_2, 2);            /* block0(addr=v0, val=v1) */
  irb_store(f, b0, IRB_STORE_I64, 0, 1, 0, 0);
  IrVal got = irb_load(f, b0, IRB_LOAD_I64, 0, 0, 0);
  IrVal ret[] = {got};
  irb_return(f, b0, ret, 1);
  return m;
}

/* call_jacl_add(sp, a, b) = jacl_add(sp, a, b) — links against the runtime. */
static IrModule *build_call_jacl_add(void) {
  IrModule *m = irb_module_new();
  IrType i64_3[] = {IRB_I64, IRB_I64, IRB_I64};
  IrType r[] = {IRB_I64};
  IrFunc *f = irb_func_new(m, i64_3, 3, r, 1);
  IrBlock b0 = irb_block(f, i64_3, 3);            /* block0(sp=v0, a=v1, b=v2) */
  IrVal handle = irb_const_i32(f, b0, 0);
  IrVal args[] = {0, 1, 2};
  IrVal got = irb_call_import(f, b0, "jacl_add", i64_3, 3, r, 1, handle, args, 3);
  IrVal ret[] = {got};
  irb_return(f, b0, ret, 1);
  return m;
}

/* A hand-built closure: build a 1-upval closure over a function that adds its upval
 * to its arg, then call it via call_indirect — exercises ref.func, i32<->i64
 * conversions, the runtime closure helpers, and the closure ABI (sp, self, args). */
static IrModule *build_closure(void) {
  IrModule *m = irb_module_new();
  IrType i64_1[] = {IRB_I64};
  IrType i64_2[] = {IRB_I64, IRB_I64};
  IrType i64_3[] = {IRB_I64, IRB_I64, IRB_I64};
  IrType i64_4[] = {IRB_I64, IRB_I64, IRB_I64, IRB_I64};
  IrType r[] = {IRB_I64};
  IrFunc *entry = irb_func_new(m, i64_1, 1, r, 1);
  IrFunc *clo = irb_func_new(m, i64_3, 3, r, 1); /* (sp, self, x) */

  /* i32 JaclVal helper inline */
  const int64_t TAG_I32 = (int64_t)((uint64_t)0x02 << 56);

  /* entry: init the heap, make closure {fn=clo, upval0=10}, call it with 32 -> 42 */
  IrBlock e0 = irb_block(entry, i64_1, 1);
  IrVal sp = 0;
  IrVal ia[] = {sp};
  (void)irb_call_import(entry, e0, "jacl_heap_init", i64_1, 1, NULL, 0, irb_const_i32(entry, e0, 0), ia, 1);
  IrVal fnref = irb_ref_func(entry, e0, clo);
  IrVal fnref64 = irb_convert(entry, e0, IRB_EXTEND_I32U, fnref);
  IrVal h = irb_const_i32(entry, e0, 0);
  IrVal nupv = irb_const_i64(entry, e0, 1);
  IrVal cargs[] = {sp, fnref64, nupv};
  IrVal clos = irb_call_import(entry, e0, "jacl_closure_new", i64_3, 3, r, 1, h, cargs, 3);
  IrVal idx0 = irb_const_i64(entry, e0, 0);
  IrVal upv0 = irb_const_i64(entry, e0, TAG_I32 | 10);
  IrVal h2 = irb_const_i32(entry, e0, 0);
  IrVal sargs[] = {sp, clos, idx0, upv0};
  (void)irb_call_import(entry, e0, "jacl_closure_set", i64_4, 4, r, 1, h2, sargs, 4);
  IrVal h3 = irb_const_i32(entry, e0, 0);
  IrVal fargs[] = {sp, clos};
  IrVal fn = irb_call_import(entry, e0, "jacl_closure_fn", i64_2, 2, r, 1, h3, fargs, 2);
  IrVal fnw = irb_convert(entry, e0, IRB_WRAP_I64, fn);
  IrVal arg = irb_const_i64(entry, e0, TAG_I32 | 32);
  IrVal iargs[] = {sp, clos, arg};
  IrVal res = irb_call_indirect(entry, e0, i64_3, 3, r, 1, fnw, iargs, 3);
  IrVal eret[] = {res};
  irb_return(entry, e0, eret, 1);

  /* clo(sp, self, x): return x + upval0 */
  IrBlock c0 = irb_block(clo, i64_3, 3);
  IrVal csp = 0, cself = 1, cx = 2;
  IrVal cidx0 = irb_const_i64(clo, c0, 0);
  IrVal ch = irb_const_i32(clo, c0, 0);
  IrVal uargs[] = {csp, cself, cidx0};
  IrVal uv = irb_call_import(clo, c0, "jacl_closure_upval", i64_3, 3, r, 1, ch, uargs, 3);
  IrVal ah = irb_const_i32(clo, c0, 0);
  IrVal aargs[] = {csp, cx, uv};
  IrVal sum = irb_call_import(clo, c0, "jacl_add", i64_3, 3, r, 1, ah, aargs, 3);
  IrVal cret[] = {sum};
  irb_return(clo, c0, cret, 1);
  return m;
}

/* GC root discipline: build a live 3-element vector, make unreachable garbage in a
 * helper (whose frame is gone on return), force a collection, and return the live
 * vector's length. 3 iff gc.roots found `keep` across the collect (else its nodes are
 * swept and the length is wrong). Exercises conservative roots in codegen-shaped IR. */
static IrModule *build_gc(void) {
  IrModule *m = irb_module_new();
  IrType i64_1[] = {IRB_I64};
  IrType i64_2[] = {IRB_I64, IRB_I64};
  IrType i64_3[] = {IRB_I64, IRB_I64, IRB_I64};
  IrType r[] = {IRB_I64};
  const int64_t TAG_I32 = (int64_t)((uint64_t)0x02 << 56);
  IrFunc *entry = irb_func_new(m, i64_1, 1, r, 1);
  IrFunc *mkjunk = irb_func_new(m, i64_1, 1, r, 1); /* (sp) -> nil, leaks a dead vec */

  /* mkjunk: build a small vector and return nil; it is unreachable on return, so it
   * gives the collector real garbage to reclaim. */
  IrBlock jb = irb_block(mkjunk, i64_1, 1);
  IrVal je[] = {0};
  IrVal jv = irb_call_import(mkjunk, jb, "jacl_vec_empty", i64_1, 1, r, 1, irb_const_i32(mkjunk, jb, 0), je, 1);
  for (int k = 0; k < 3; k++) {
    IrVal jel = irb_const_i64(mkjunk, jb, TAG_I32 | k);
    IrVal ja[] = {0, jv, jel};
    jv = irb_call_import(mkjunk, jb, "jacl_vec_push", i64_3, 3, r, 1, irb_const_i32(mkjunk, jb, 0), ja, 3);
  }
  (void)jv;
  IrVal jnil[] = {irb_const_i64(mkjunk, jb, 0)};
  irb_return(mkjunk, jb, jnil, 1);

  /* entry: init the heap; keep = [111 222 333]; make garbage; collect; length(keep). */
  IrBlock b = irb_block(entry, i64_1, 1);
  IrVal sp = 0;
  IrVal initargs[] = {sp};
  (void)irb_call_import(entry, b, "jacl_heap_init", i64_1, 1, NULL, 0, irb_const_i32(entry, b, 0), initargs, 1);
  IrVal ke[] = {sp};
  IrVal keep = irb_call_import(entry, b, "jacl_vec_empty", i64_1, 1, r, 1, irb_const_i32(entry, b, 0), ke, 1);
  for (int k = 1; k <= 3; k++) {
    IrVal el = irb_const_i64(entry, b, TAG_I32 | (111 * k));
    IrVal a[] = {sp, keep, el};
    keep = irb_call_import(entry, b, "jacl_vec_push", i64_3, 3, r, 1, irb_const_i32(entry, b, 0), a, 3);
  }
  for (int g = 0; g < 6; g++) {
    IrVal a[] = {sp};
    (void)irb_call(entry, b, mkjunk, a, 1);
  }
  IrVal gc[] = {sp};
  (void)irb_call_import(entry, b, "jacl_gc_collect", i64_1, 1, r, 1, irb_const_i32(entry, b, 0), gc, 1);
  IrVal la[] = {sp, keep};
  IrVal len = irb_call_import(entry, b, "jacl_len", i64_2, 2, r, 1, irb_const_i32(entry, b, 0), la, 2);
  IrVal ret[] = {len};
  irb_return(entry, b, ret, 1);
  return m;
}

int main(int argc, char **argv) {
  if (argc != 2) { fprintf(stderr, "usage: emit_demo <module>\n"); return 2; }
  const char *name = argv[1];
  IrModule *m = NULL;
  if (!strcmp(name, "add2")) m = build_add2();
  else if (!strcmp(name, "sum_to")) m = build_sum_to();
  else if (!strcmp(name, "call_addone")) m = build_call_addone();
  else if (!strcmp(name, "mem_roundtrip")) m = build_mem_roundtrip();
  else if (!strcmp(name, "call_jacl_add")) m = build_call_jacl_add();
  else if (!strcmp(name, "closure")) m = build_closure();
  else if (!strcmp(name, "gc")) m = build_gc();
  else { fprintf(stderr, "unknown module: %s\n", name); return 2; }

  char *text = irb_to_text(m);
  fputs(text, stdout);
  free(text);
  irb_module_free(m);
  return 0;
}
