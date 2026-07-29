/* The runtime unity + the staged-macro I/O glue, translated as ONE svm-llvm module so a
 * codegen'd staged-macro entry can resolve synrt_read_arg / synrt_write_result (and the
 * jacl_* runtime) by name at link. Compile with `-I runtime`. */
#include "../../../runtime/jaclrt.c"
#include "stage_glue.c"
