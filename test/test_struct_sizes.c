/* Struct size consistency test.
 *
 * This test detects struct/enum definition drift between .c source files
 * (used by the unity build) and jacl.h (used by separately-compiled tests).
 *
 * If a field is added to a struct in the .c file but not jacl.h (or vice
 * versa), the sizeof() values will differ and this test will fail — instead
 * of silently corrupting memory at runtime.
 */

#include <stdio.h>
#include <stddef.h>
#include "../src/jacl.h"

static int failures = 0;

#define CHECK_SIZE(type, getter)                                        \
  do {                                                                  \
    size_t header_size = sizeof(type);                                  \
    size_t source_size = getter();                                      \
    if (header_size != source_size) {                                   \
      fprintf(stderr,                                                   \
              "  FAIL: sizeof(%s) mismatch: jacl.h=%zu, source=%zu\n",  \
              #type, header_size, source_size);                         \
      failures++;                                                       \
    } else {                                                            \
      printf("  %-30s %4zu bytes  OK\n", #type, header_size);          \
    }                                                                   \
  } while (0)

int main(void) {
  printf("=== Struct size consistency checks ===\n");

  CHECK_SIZE(VM,                  jacl__sizeof_vm);
  CHECK_SIZE(Compiler,            jacl__sizeof_compiler);
  CHECK_SIZE(CompileResult,       jacl__sizeof_compile_result);
  CHECK_SIZE(StructTypeField,     jacl__sizeof_struct_type_field);
  CHECK_SIZE(StructTypeDef,       jacl__sizeof_struct_type_def);
  CHECK_SIZE(StructTypeRegistry,  jacl__sizeof_struct_type_registry);
  CHECK_SIZE(StateLayout,         jacl__sizeof_state_layout);
  CHECK_SIZE(SuspensionAnalysis,  jacl__sizeof_suspension_analysis);
  CHECK_SIZE(CallFrame,           jacl__sizeof_call_frame);
  CHECK_SIZE(JaclCtxPool,         jacl__sizeof_ctx_pool);
  CHECK_SIZE(Environment,         jacl__sizeof_environment);
  CHECK_SIZE(StackTrace,          jacl__sizeof_stack_trace);
  CHECK_SIZE(StackTraceEntry,     jacl__sizeof_stack_trace_entry);

  printf("\n%d/%d passed\n", 13 - failures, 13);

  return failures > 0 ? 1 : 0;
}
