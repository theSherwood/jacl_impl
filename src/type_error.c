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

#endif /* JACL_TYPE_ERROR_C */
