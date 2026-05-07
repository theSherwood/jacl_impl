/*
 * Shared type-error message formatters. Used by compiler.c today and
 * (in Stage 1) the typer too — both passes call these to produce the
 * content of type-mismatch error messages, ensuring the user-facing
 * UX stays consistent regardless of which pass first detects an
 * error. See STATIC_TYPING_PLAN.md decision 4.
 *
 * Each formatter writes to a caller-provided buffer in snprintf
 * style so it composes with the existing compiler__error /
 * typer__error reporting machinery without forcing memory ownership
 * decisions on the helper.
 */

#ifndef JACL_TYPE_ERROR_C
#define JACL_TYPE_ERROR_C

/* "type error: cannot assign <actual> to <target> binding '<name>'"
 * Concrete-to-concrete mismatch. Used at typed def/mut/set sites for
 * locals, upvalues, and globals. */
int jacl_format_assign_mismatch(char* buf, size_t bufsz,
                                JaclType target, JaclType actual,
                                const char* name, uint32_t name_len) {
  return snprintf(buf, bufsz,
                  "type error: cannot assign %s to %s binding '%.*s'",
                  type_name(actual), type_name(target),
                  (int)name_len, name);
}

/* "type error: cannot assign dyn to <target> binding '<name>'"
 * Dyn-into-typed mismatch where the binding name is known. Used for
 * named typed mut/set sites. */
int jacl_format_assign_dyn_named(char* buf, size_t bufsz,
                                 JaclType target,
                                 const char* name, uint32_t name_len) {
  return snprintf(buf, bufsz,
                  "type error: cannot assign dyn to %s binding '%.*s'",
                  type_name(target), (int)name_len, name);
}

/* "type error: cannot assign dyn to <target> binding — use [to <target> $val] to cast"
 * Dyn-into-typed mismatch without a binding name (typed def with the
 * value form, or other anonymous contexts). Includes the cast hint. */
int jacl_format_assign_dyn_unnamed(char* buf, size_t bufsz,
                                   JaclType target) {
  return snprintf(buf, bufsz,
                  "type error: cannot assign dyn to %s binding — "
                  "use [to %s $val] to cast",
                  type_name(target), type_name(target));
}

/* "cannot assign struct value to dyn binding '<name>' — wrap with [box $val]"
 * Special case: struct values can't flow into untyped (dyn) bindings
 * implicitly because the runtime representation differs. */
int jacl_format_assign_struct_to_dyn(char* buf, size_t bufsz,
                                     const char* name, uint32_t name_len) {
  return snprintf(buf, bufsz,
                  "cannot assign struct value to dyn binding '%.*s' — "
                  "wrap with [box $val]",
                  (int)name_len, name);
}

/* "type error: field '<field>' of struct '<struct>' expected <expected>, got <actual>"
 * Struct or ctx field set with mismatched value type. */
int jacl_format_field_mismatch(char* buf, size_t bufsz,
                               const char* struct_name, uint32_t struct_name_len,
                               const char* field_name, uint32_t field_name_len,
                               JaclType expected, JaclType actual) {
  return snprintf(buf, bufsz,
                  "type error: field '%.*s' of struct '%.*s' "
                  "expected %s, got %s",
                  (int)field_name_len, field_name,
                  (int)struct_name_len, struct_name,
                  type_name(expected), type_name(actual));
}

/* "type error: field '<field>' of struct '<struct>' expected <expected>, got dyn — use [to <expected> $val] to cast"
 * Same but for dyn-into-typed-field with cast hint. */
int jacl_format_field_dyn_assign(char* buf, size_t bufsz,
                                 const char* struct_name, uint32_t struct_name_len,
                                 const char* field_name, uint32_t field_name_len,
                                 JaclType expected) {
  return snprintf(buf, bufsz,
                  "type error: field '%.*s' of struct '%.*s' "
                  "expected %s, got dyn — use [to %s $val] to cast",
                  (int)field_name_len, field_name,
                  (int)struct_name_len, struct_name,
                  type_name(expected), type_name(expected));
}

/* "type error: proc <name> declared return type <declared>, but body returns <actual>" */
int jacl_format_proc_return_mismatch(char* buf, size_t bufsz,
                                     const char* proc_name, uint32_t proc_name_len,
                                     JaclType declared, JaclType actual) {
  return snprintf(buf, bufsz,
                  "type error: proc %.*s declared return type %s, "
                  "but body returns %s",
                  (int)proc_name_len, proc_name,
                  type_name(declared), type_name(actual));
}

/* "type error: proc <name> declared return type <declared>, but body returns dyn — use [to <declared> $val] at the tail"
 * Dyn-into-typed-return — the proc body's tail value is dyn but the
 * declared return is a concrete type. Per decision 2's commitment-
 * site rule, this requires an explicit cast. */
int jacl_format_proc_return_dyn(char* buf, size_t bufsz,
                                const char* proc_name, uint32_t proc_name_len,
                                JaclType declared) {
  return snprintf(buf, bufsz,
                  "type error: proc %.*s declared return type %s, "
                  "but body returns dyn — use [to %s $val] at the tail",
                  (int)proc_name_len, proc_name,
                  type_name(declared), type_name(declared));
}

/* "type error: await expects a future, got <actual>"
 * Operand has a known concrete non-future type. The runtime trap on
 * non-future values still exists for dyn operands; this catches the
 * statically-knowable case at compile time. */
int jacl_format_await_non_future(char* buf, size_t bufsz, JaclType actual) {
  return snprintf(buf, bufsz,
                  "type error: await expects a future, got %s",
                  type_name(actual));
}

/* "[Vec T]: element <idx> is not a T value (got <actual>)" / "is not a T struct"
 * Used by [Vec T] constructor element-type checks. is_scalar selects the
 * scalar wording (with "got <actual>") vs struct wording. */
int jacl_format_typed_vec_elem(char* buf, size_t bufsz,
                               const char* elem_name, uint32_t elem_name_len,
                               uint32_t idx, bool is_scalar, JaclType actual) {
  if (is_scalar) {
    return snprintf(buf, bufsz,
                    "[Vec %.*s]: element %u is not a %.*s value (got %s)",
                    (int)elem_name_len, elem_name, idx,
                    (int)elem_name_len, elem_name, type_name(actual));
  }
  return snprintf(buf, bufsz,
                  "[Vec %.*s]: element %u is not a %.*s struct",
                  (int)elem_name_len, elem_name, idx,
                  (int)elem_name_len, elem_name);
}

/* "[Map T]: value <idx> is not a T value (got <actual>)" / "is not a T struct"
 * Used by [Map T] (single-type, dyn-key) constructor value-type checks. */
int jacl_format_typed_map_value(char* buf, size_t bufsz,
                                const char* val_name, uint32_t val_name_len,
                                uint32_t idx, bool is_scalar, JaclType actual) {
  if (is_scalar) {
    return snprintf(buf, bufsz,
                    "[Map %.*s]: value %u is not a %.*s value (got %s)",
                    (int)val_name_len, val_name, idx,
                    (int)val_name_len, val_name, type_name(actual));
  }
  return snprintf(buf, bufsz,
                  "[Map %.*s]: value %u is not a %.*s struct",
                  (int)val_name_len, val_name, idx,
                  (int)val_name_len, val_name);
}

/* "[Map K V]: key <idx> is not a K" / "value <idx> is not a V"
 * Used by [Map K V] (explicit key+value type) constructor checks.
 * is_value_slot=false → key check, true → value check. */
int jacl_format_typed_map_kv(char* buf, size_t bufsz,
                             const char* key_name, uint32_t key_name_len,
                             const char* val_name, uint32_t val_name_len,
                             uint32_t idx, bool is_value_slot) {
  const char* slot_word = is_value_slot ? "value" : "key";
  const char* tn  = is_value_slot ? val_name : key_name;
  uint32_t    tnl = is_value_slot ? val_name_len : key_name_len;
  return snprintf(buf, bufsz,
                  "[Map %.*s %.*s]: %s %u is not a %.*s",
                  (int)key_name_len, key_name,
                  (int)val_name_len, val_name,
                  slot_word, idx,
                  (int)tnl, tn);
}

/* --- Pointer-related formatters (Stage 5) --- */

/* "type error: cannot assign pointer to different pointee type to binding '<name>'"
 * Cross-pointee assignment to a typed [Ptr T] binding. Both sides
 * are pointers, just with mismatched pointee. */
int jacl_format_ptr_assign_pointee_mismatch(char* buf, size_t bufsz,
                                            const char* name, uint32_t name_len) {
  return snprintf(buf, bufsz,
                  "type error: cannot assign pointer to different pointee type "
                  "to binding '%.*s'",
                  (int)name_len, name);
}

/* "type error: cannot perform arithmetic on pointer values — use [ptr-offset $p $n] for typed pointer arithmetic"
 * Fires when + - * / % is applied to any pointer operand. */
int jacl_format_ptr_arithmetic(char* buf, size_t bufsz) {
  return snprintf(buf, bufsz,
                  "type error: cannot perform arithmetic on pointer values "
                  "— use [ptr-offset $p $n] for typed pointer arithmetic");
}

/* "type error: cannot compare pointers to different pointee types"
 * Fires for ==/!=/<.../etc. between two TYPE_PTR with different pointees. */
int jacl_format_ptr_compare_pointee_mismatch(char* buf, size_t bufsz) {
  return snprintf(buf, bufsz,
                  "type error: cannot compare pointers to different pointee types");
}

/* "type error: ptr-cast first argument must be [Ptr T]"
 * The first arg to ptr-cast must be a [Ptr T] type annotation. */
int jacl_format_ptr_cast_bad_first_arg(char* buf, size_t bufsz) {
  return snprintf(buf, bufsz,
                  "type error: ptr-cast first argument must be [Ptr T]");
}

/* "type error: ptr-cast value must be u64, got <actual>"
 * The second arg to ptr-cast must be u64 (the raw address bits). */
int jacl_format_ptr_cast_value_not_u64(char* buf, size_t bufsz, JaclType actual) {
  return snprintf(buf, bufsz,
                  "type error: ptr-cast value must be u64, got %s",
                  type_name(actual));
}

/* "type error: <op> expects a pointer, got <actual>"
 * Used by ptr-addr / ptr-deref / ptr-offset for the receiver-arg
 * type check; op_name is the source-level builtin name (e.g.
 * "ptr-addr"). */
int jacl_format_ptr_op_expects_ptr(char* buf, size_t bufsz,
                                   const char* op_name, JaclType actual) {
  return snprintf(buf, bufsz,
                  "type error: %s expects a pointer, got %s",
                  op_name, type_name(actual));
}

/* "type error: ptr-offset offset must be numeric, got <actual>"
 * The second arg to ptr-offset must be a numeric type. */
int jacl_format_ptr_offset_non_numeric(char* buf, size_t bufsz, JaclType actual) {
  return snprintf(buf, bufsz,
                  "type error: ptr-offset offset must be numeric, got %s",
                  type_name(actual));
}

/* "type error: ptr-diff expects two pointers, got <a> and <b>"
 * Both args to ptr-diff must be pointers. Reports the first
 * non-pointer's type. */
int jacl_format_ptr_diff_expects_two(char* buf, size_t bufsz,
                                     JaclType a, JaclType b) {
  return snprintf(buf, bufsz,
                  "type error: ptr-diff expects two pointers, got %s and %s",
                  type_name(a), type_name(b));
}

/* "type error: ptr-diff requires same pointee type on both sides" */
int jacl_format_ptr_diff_pointee_mismatch(char* buf, size_t bufsz) {
  return snprintf(buf, bufsz,
                  "type error: ptr-diff requires same pointee type on both sides");
}

/* "type error: ptr-deref on a struct pointer — use $p->field for field access" */
int jacl_format_ptr_deref_struct(char* buf, size_t bufsz) {
  return snprintf(buf, bufsz,
                  "type error: ptr-deref on a struct pointer — use "
                  "$p->field for field access");
}

/* "type error: ptr-null argument must be [Ptr T]"
 * Single-argument form distinct from ptr-cast (no value follows). */
int jacl_format_ptr_null_bad_arg(char* buf, size_t bufsz) {
  return snprintf(buf, bufsz,
                  "type error: ptr-null argument must be [Ptr T]");
}

#endif /* JACL_TYPE_ERROR_C */
