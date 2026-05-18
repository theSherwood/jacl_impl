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
    total++;                                                            \
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

#define CHECK_OFFSET(type, field, getter)                               \
  do {                                                                  \
    total++;                                                            \
    size_t header_off = offsetof(type, field);                          \
    size_t source_off = getter();                                       \
    if (header_off != source_off) {                                     \
      fprintf(stderr,                                                   \
              "  FAIL: offsetof(%s.%s) mismatch: jacl.h=%zu, "          \
              "source=%zu\n", #type, #field, header_off, source_off);   \
      failures++;                                                       \
    } else {                                                            \
      printf("  %-30s @%-4zu  OK\n", #type "." #field, header_off);    \
    }                                                                   \
  } while (0)

int main(void) {
  int total = 0;

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

  /* Runtime / GC structs (added after AUDIT.md fixes) */
  CHECK_SIZE(WorkerThread,        jacl__sizeof_worker_thread);
  CHECK_SIZE(Runtime,             jacl__sizeof_runtime);
  CHECK_SIZE(RuntimeTask,         jacl__sizeof_runtime_task);
  CHECK_SIZE(ThreadHeap,          jacl__sizeof_thread_heap);
  CHECK_SIZE(GreyBuffer,          jacl__sizeof_grey_buffer);
  CHECK_SIZE(RememberedSet,       jacl__sizeof_remembered_set);

  /* Field offsets — catch struct drift that preserves total size but
   * shifts individual fields (the original WorkerThread bug). Coverage
   * is comprehensive (every field of Runtime + WorkerThread) so that a
   * missing field in either definition fails at compile time
   * ("no member named …") rather than corrupting memory silently. The
   * list lives in src/struct_drift_fields.h and is shared with embed.c
   * — see that header for the update protocol. */
#include "../src/struct_drift_fields.h"
#define JACL_DRIFT_CHECK(STRUCT, FIELD) \
    CHECK_OFFSET(STRUCT, FIELD, jacl__offsetof_##STRUCT##_##FIELD);
  RUNTIME_FIELDS(JACL_DRIFT_CHECK)
  WORKER_FIELDS(JACL_DRIFT_CHECK)
#undef JACL_DRIFT_CHECK

  CHECK_OFFSET(RuntimeTask, gc_root3,
               jacl__offsetof_runtime_task_gc_root3);

  printf("\n%d/%d passed\n", total - failures, total);

  return failures > 0 ? 1 : 0;
}
