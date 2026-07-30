/* Staging runtime unity for the **in-guest** guest-JIT path: the jaclrt runtime + the
 * buffer-based staged-macro glue, linked INTO the compiler `.svmb` (build_compiler_svmb.sh) so a
 * codegen'd staged-macro entry can resolve jacl_* / synrt_* by name through the shared
 * call_indirect table (each function's funcref index is its slot; the staging hook builds the
 * {name -> Slot} symtab from `&fn`). Compile with `-I runtime`. See docs/SVM_GUEST_JIT_STAGING.md
 * §3c. Unlike jaclrt_staging.c (the native separate-module path), the glue here is buffer-driven
 * and does not override malloc — the compiler owns the allocator.
 *
 * **Symbol separation.** The compiler `.svmb` already embeds the *reference VM's* runtime
 * (`src/collections.c` etc., used live by the frontend's emit path — value.c/compiler.c/syntax.c),
 * whose value ops share names with jaclrt's. Ten names collide (jacl_arr_new, jacl_eq, and the
 * jacl_map_ and jacl_vec_ value ops). Since the two runtimes operate on different heaps and both
 * are live, jaclrt's copies are renamed with a `jrtg_` prefix here (consistently: definition + all
 * in-unit callers), so the compiler keeps its own and jaclrt keeps its own. The `compile_linked`
 * symtab decouples the import name from the function, so the macro's `jacl_vec_empty` import binds
 * to `Slot(&jrtg_jacl_vec_empty)` (see the symtab helper). Keep this list in sync with the symbol
 * collision check in build_compiler_svmb.sh (it fails the build if the set drifts). */
#define jacl_arr_new    jrtg_jacl_arr_new
#define jacl_eq         jrtg_jacl_eq
#define jacl_map_get    jrtg_jacl_map_get
#define jacl_map_has    jrtg_jacl_map_has
#define jacl_map_set    jrtg_jacl_map_set
#define jacl_vec_concat jrtg_jacl_vec_concat
#define jacl_vec_empty  jrtg_jacl_vec_empty
#define jacl_vec_get    jrtg_jacl_vec_get
#define jacl_vec_set    jrtg_jacl_vec_set
#define jacl_vec_slice  jrtg_jacl_vec_slice

#include "../../../runtime/jaclrt.c"
#include "stage_glue_guest.c"
