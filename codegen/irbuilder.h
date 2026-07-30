/* irbuilder.h — JACL-owned SVM-IR builder (P2.1).
 *
 * An in-memory SVM-IR module/func/block/inst representation mirroring svm-ir's
 * shape, with a text serializer (`irb_to_text`) emitting the svm-text format that
 * `svm_text::parse_module` accepts. This is the target of JACL's Phase-2 codegen:
 * the AST walk emits into a builder, which serializes to text, which links against
 * the runtime artifact (see runtime/harness/tests/link_artifact.rs) and runs on
 * interp + JIT. A binary (svm-encode) serializer (`irb_to_encoded`) sits behind the same
 * API, round-trip-gated against the text path — the bytes the §22 `Jit` capability consumes.
 *
 * Discipline (carried from Spike-1): SVM is **block-local SSA** — an instruction may
 * reference only its own block's params and earlier results; a value needed in
 * another block is threaded as a branch argument and received as a block param. The
 * builder enforces value *numbering* (params are v0..v{k-1}, then each result-
 * producing instruction takes the next index, per block) and returns each new
 * value's id; the caller is responsible for honoring the cross-block rule (the
 * verifier is the backstop).
 */
#ifndef JACL_IRBUILDER_H
#define JACL_IRBUILDER_H

#include <stdint.h>
#include <stddef.h>

/* svm-ir value types (ValType). */
typedef enum { IRB_I32, IRB_I64, IRB_F32, IRB_F64, IRB_V128, IRB_REF } IrType;

/* Integer binary ops (BinOp), in svm-ir order. */
typedef enum {
  IRB_ADD, IRB_SUB, IRB_MUL, IRB_DIV_S, IRB_DIV_U, IRB_REM_S, IRB_REM_U,
  IRB_AND, IRB_OR, IRB_XOR, IRB_SHL, IRB_SHR_S, IRB_SHR_U, IRB_ROTL, IRB_ROTR
} IrBinOp;

/* Integer comparisons (CmpOp): operands of type `ty`, i32 0/1 result. */
typedef enum {
  IRB_EQ, IRB_NE, IRB_LT_S, IRB_LT_U, IRB_LE_S, IRB_LE_U,
  IRB_GT_S, IRB_GT_U, IRB_GE_S, IRB_GE_U
} IrCmpOp;

/* Memory loads (LoadOp), in svm-ir order. */
typedef enum {
  IRB_LOAD_I32, IRB_LOAD_I64, IRB_LOAD_F32, IRB_LOAD_F64,
  IRB_LOAD_I32_8S, IRB_LOAD_I32_8U, IRB_LOAD_I32_16S, IRB_LOAD_I32_16U,
  IRB_LOAD_I64_8S, IRB_LOAD_I64_8U, IRB_LOAD_I64_16S, IRB_LOAD_I64_16U,
  IRB_LOAD_I64_32S, IRB_LOAD_I64_32U
} IrLoadOp;

/* Memory stores (StoreOp), in svm-ir order. */
typedef enum {
  IRB_STORE_I32, IRB_STORE_I64, IRB_STORE_F32, IRB_STORE_F64,
  IRB_STORE_I32_8, IRB_STORE_I32_16, IRB_STORE_I64_8, IRB_STORE_I64_16, IRB_STORE_I64_32
} IrStoreOp;

/* Width conversions (ConvOp), in svm-ir order. */
typedef enum { IRB_EXTEND_I32S, IRB_EXTEND_I32U, IRB_WRAP_I64 } IrConvOp;

typedef struct IrModule IrModule;
typedef struct IrFunc   IrFunc;

typedef uint32_t IrVal;    /* value id within a block */
typedef int      IrBlock;  /* block index within a func */

/* ---- module / function / block construction ---- */

IrModule *irb_module_new(void);
void      irb_module_free(IrModule *m);

/* Declare a linear memory of 2^size_log2 bytes (svm-text `memory <size_log2>`).
 * Required for load/store to have backing storage. Without it no memory is emitted. */
void      irb_set_memory(IrModule *m, uint8_t size_log2);

/* Append a read-only data segment (e.g. a string literal's bytes) to the window and
 * return its unit-local byte offset. After linking, the segment is relocated by the
 * unit's data base; reference it with irb_data_addr, which emits a link-form address. */
uint64_t  irb_add_data(IrModule *m, const char *bytes, uint32_t len);

/* Append a function with the given signature; returns its handle. Functions are
 * created up front so direct `call`s (which reference a function index) resolve. */
IrFunc   *irb_func_new(IrModule *m, const IrType *params, int nparams,
                       const IrType *results, int nresults);
/* The function's index in the module (the value a `call` references). */
int       irb_func_index(const IrModule *m, const IrFunc *f);

/* Append a block with typed params; returns its index within the function. */
IrBlock   irb_block(IrFunc *f, const IrType *params, int nparams);

/* ---- value-producing instructions (return the new value id) ---- */

IrVal irb_const_i32(IrFunc *f, IrBlock b, int32_t c);
IrVal irb_const_i64(IrFunc *f, IrBlock b, int64_t c);
IrVal irb_intbin(IrFunc *f, IrBlock b, IrType ty, IrBinOp op, IrVal a, IrVal x);
IrVal irb_intcmp(IrFunc *f, IrBlock b, IrType ty, IrCmpOp op, IrVal a, IrVal x);
IrVal irb_load(IrFunc *f, IrBlock b, IrLoadOp op, IrVal addr, uint64_t offset, uint8_t align);

/* No-result instruction (no `vN =` binding). */
void  irb_store(IrFunc *f, IrBlock b, IrStoreOp op, IrVal addr, IrVal value,
                uint64_t offset, uint8_t align);

/* Direct call to a function in this module. Returns the result value id when the
 * callee has exactly one result, else 0 (the builder currently models 0/1 results). */
IrVal irb_call(IrFunc *f, IrBlock b, const IrFunc *callee, const IrVal *args, int nargs);

/* `ref.func` — a forgeable i32 funcref (the callee's link-relocated function index),
 * suitable as the index operand of irb_call_indirect. */
IrVal irb_ref_func(IrFunc *f, IrBlock b, const IrFunc *callee);

/* Link-form own-data address (v9 `data.self <local_off>`): an i64 that the linker rewrites
 * to the data segment's final window address (`i64.const dbase + local_off`). Pass the
 * result where the runtime expects a data pointer (e.g. jacl_str_new). */
IrVal irb_data_addr(IrFunc *f, IrBlock b, uint64_t local_off);

/* Width conversion (i64.extend_i32_s/u, i32.wrap_i64). */
IrVal irb_convert(IrFunc *f, IrBlock b, IrConvOp op, IrVal a);

/* `suspend value` — yield `value` out of the current fiber (a generator `yield`); the
 * result is the argument the next resume passes back in. */
IrVal irb_suspend(IrFunc *f, IrBlock b, IrVal value);

/* Indirect call through the function table: `call_indirect (sig) v<idx> (args)`.
 * `idx` is an i32 function index (e.g. from irb_ref_func or a closure's fnref).
 * Single-result (i64 for the JACL closure ABI); returns its value id. */
IrVal irb_call_indirect(IrFunc *f, IrBlock b,
                        const IrType *params, int nparams,
                        const IrType *results, int nresults,
                        IrVal idx, const IrVal *args, int nargs);

/* §7 named import call, name-inline form: `call.import "name" (sig) v<handle> (args)`.
 * The linker resolves `name` against the runtime artifact's exports. `handle` is an
 * i32 value (use irb_const_i32(.., 0) for a linked import). Single-result (i64 for
 * jacl_* runtime calls); returns its value id. */
IrVal irb_call_import(IrFunc *f, IrBlock b, const char *name,
                      const IrType *params, int nparams,
                      const IrType *results, int nresults,
                      IrVal handle, const IrVal *args, int nargs);

/* ---- terminators (one per block) ---- */

void irb_br(IrFunc *f, IrBlock b, IrBlock target, const IrVal *args, int nargs);
void irb_br_if(IrFunc *f, IrBlock b, IrVal cond,
               IrBlock then_blk, const IrVal *then_args, int nthen,
               IrBlock else_blk, const IrVal *else_args, int nelse);
void irb_return(IrFunc *f, IrBlock b, const IrVal *vals, int nvals);

/* Tail call: `return_call callee(args)` — return the callee's result as this
 * function's result (the two must share a result signature). Terminates the block. */
void irb_return_call(IrFunc *f, IrBlock b, const IrFunc *callee, const IrVal *args, int nargs);

/* ---- serialization ---- */

/* Serialize the whole module to svm-text. Returns a malloc'd NUL-terminated string;
 * the caller frees it. Own-data addresses print as `data.self <offset>` (v9). */
char *irb_to_text(const IrModule *m);

/* Serialize the whole module to the svm-encode **binary object** (v9 object dialect, the
 * default `emit_jacl` output and the bytes the §22 `Jit` capability consumes). irb output
 * is always a pre-link unit — it carries `data.self` own-data addresses and unresolved
 * `call.sym`s that `link` resolves — so the object flag is always set. Returns a malloc'd
 * buffer of `*out_len` bytes; the caller frees it. The harness decodes it with
 * `svm_encode::decode_unit` (`decode_module` rejects objects); round-trip-equal to the text
 * path: `decode_unit(irb_to_encoded(m))` equals `svm_text::parse_unit(irb_to_text(m))`. */
uint8_t *irb_to_encoded(const IrModule *m, size_t *out_len);

#endif /* JACL_IRBUILDER_H */
