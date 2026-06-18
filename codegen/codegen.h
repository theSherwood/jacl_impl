/* codegen.h — JACL AST → SVM-IR code generation (P2.2 scaffold).
 *
 * The Phase-2 codegen walk: consumes the JACL AST (post-parse, optionally typed)
 * and emits an SVM-IR module via the IR builder (irbuilder.h). The scaffold lowers
 * integer literals and arithmetic operator calls (+ - * / %) into the runtime's
 * dynamic value ops (jacl_add, …) via call.import, threading the data-stack pointer
 * per the §3d ABI, and returns the last top-level expression's value (a JaclVal).
 *
 * Later slices extend the walk (bindings, control flow, procs, closures, type-driven
 * unboxing); the entry point and module shape stay stable.
 */
#ifndef JACL_CODEGEN_H
#define JACL_CODEGEN_H

#include <stdint.h>
#include <stddef.h>

#include "irbuilder.h"

struct AstNode;

/* Compile an array of top-level AST nodes into an SVM-IR module with a single entry
 * function `func (i64 sp) -> (i64)` that evaluates each form in order and returns the
 * last one's JaclVal. The program is meant to be linked against the runtime artifact
 * (see runtime/harness) and run on interp + JIT.
 *
 * Returns the module (caller frees with irb_module_free), or NULL on an unsupported
 * construct — in which case a message is written to `err` (when `errcap > 0`). */
IrModule *svm_codegen_program(struct AstNode **nodes, uint32_t count,
                               char *err, size_t errcap);

#endif /* JACL_CODEGEN_H */
