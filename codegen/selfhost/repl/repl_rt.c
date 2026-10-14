/* The shell's runtime (jacl_impl#214): jaclrt and the in-guest staging glue in one unit, so a macro
 * defined at the prompt stages on this runtime, live (stage_bridge_guest.c,
 * jacl_install_temen_stage_hook_live). One unit because the glue's symbol table takes the address
 * of runtime functions jaclrt.h does not declare. Unlike the compiler card's staging runtime
 * (macro_staging/jaclrt_staging_guest.c) nothing is renamed: the shell's image has one jaclrt, and
 * its frontend's clashing copies are the ones renamed (runtime/harness/src/repl.rs, RENAMED). */
#include "../../../runtime/jaclrt.c"
#include "../macro_staging/stage_glue_guest.c"
