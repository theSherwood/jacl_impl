/* Native single-threaded stubs for the SVM __vm_* intrinsics, so the runtime's
 * value/string/collection subset (and the syn_rt codec) can be unit-tested off-SVM.
 * Atomics become plain ops (single-threaded); wait/notify/suspend/gc_roots are inert. */
#include <stdint.h>
long __vm_vcpu_tls_get(void) { return 0; }
long __vm_atomic_add(void *p, long v) { long *q = p; long o = *q; *q += v; return o; }
int  __vm_atomic_add32(void *p, int v) { int *q = p; int o = *q; *q += v; return o; }
void __vm_atomic_store32(void *p, int v) { *(int *)p = v; }
int  __vm_atomic_load32(void *p) { return *(int *)p; }
int  __vm_atomic_cas32(void *p, int e, int d) { int *q = p; int o = *q; if (o == e) *q = d; return o; }
int  __vm_notify(void *p, int n) { (void)p; (void)n; return 0; }
int  __vm_wait32(void *p, int e, long t) { (void)p; (void)e; (void)t; return 1; }
long __vm_fiber_suspend(long v) { (void)v; return 0; }
long __vm_gc_roots(long lo, long hi, long m, void *b, long c) { (void)lo;(void)hi;(void)m;(void)b;(void)c; return 0; }
/* Inert stubs — caps/fibers/threads the codec never invokes (pulled in by the unity). */
int  __vm_cap_resolve(const char *name, long len) { (void)name; (void)len; return -1; }
long __vm_host_call(int h, int op, long a, long b, long c, long d) { (void)h;(void)op;(void)a;(void)b;(void)c;(void)d; return -1; }
int  __vm_thread_spawn(long (*fn)(long), void *stack, long arg) { (void)fn;(void)stack;(void)arg; return -1; }
long __vm_thread_join(int h) { (void)h; return 0; }
long __vm_fiber_new(long (*f)(long), void *stack) { (void)f; (void)stack; return 0; }
long __vm_fiber_resume(long k, long arg, int *done) { (void)k; (void)arg; if (done) *done = 1; return 0; }
