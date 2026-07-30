/* Staging runtime unity for the **in-guest** guest-JIT path: the jaclrt runtime + the
 * buffer-based staged-macro glue, linked INTO the compiler `.svmb` (build_compiler_svmb.sh) so a
 * codegen'd staged-macro entry can resolve jacl_* / synrt_* by name through the shared
 * call_indirect table (each function's funcref index is its slot; the staging hook builds the
 * {name -> Slot} symtab from `&fn`). Compile with `-I runtime`. See docs/SVM_GUEST_JIT_STAGING.md
 * §3c. Unlike jaclrt_staging.c (the native separate-module path), the glue here is buffer-driven
 * and does not override malloc — the compiler owns the allocator. */
#include "../../../runtime/jaclrt.c"
#include "stage_glue_guest.c"
