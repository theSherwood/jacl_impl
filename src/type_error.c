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

#endif /* JACL_TYPE_ERROR_C */
