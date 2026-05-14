/*
 * JACL Public Embedding API
 *
 * Single public header for embedding JACL in C/C++ applications.
 * Self-contained — no transitive includes of internal headers.
 */

#ifndef JACL_H
#define JACL_H

/* --- Version --- */

#define JACL_VERSION_MAJOR 0
#define JACL_VERSION_MINOR 17
#define JACL_VERSION_PATCH 0

/* --- Export/visibility annotation --- */

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef JACL_BUILDING
    #define JACL_API __declspec(dllexport)
  #else
    #define JACL_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #define JACL_API __attribute__((visibility("default")))
#else
  #define JACL_API
#endif

/* --- C++ compatibility --- */

#ifdef __cplusplus
extern "C" {
#endif

/* --- Standard includes (self-contained) --- */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* --- Core value type --- */

typedef uint64_t JaclVal;

/* --- Type enum (mirrors internal JaclType) --- */

typedef enum {
  JACL_TYPE_DYN = 0,
  JACL_TYPE_BOOL,
  JACL_TYPE_NIL,
  JACL_TYPE_I32,
  JACL_TYPE_I64,
  JACL_TYPE_U32,
  JACL_TYPE_U64,
  JACL_TYPE_F32,
  JACL_TYPE_F64,
  JACL_TYPE_STR,
  JACL_TYPE_VEC,
  JACL_TYPE_MAP,
  JACL_TYPE_CLOSURE,
  JACL_TYPE_STRUCT
} JaclType;

/* --- Opaque types --- */

typedef struct JaclVM_s JaclVM;
typedef struct JaclHandle_s { uint32_t index; } JaclHandle;
typedef struct JaclTrampoline_s JaclTrampoline;

/* --- VM configuration --- */

typedef struct {
  size_t   initial_heap_size;
  size_t   max_heap_size;
  uint32_t max_handles;
} JaclConfig;

/* --- Native function signature --- */

typedef JaclVal (*JaclNativeFn)(JaclVM* vm, JaclVal* args, int argc);

/* --- VM lifecycle --- */

JACL_API JaclVM*     jacl_vm_new(void);
JACL_API JaclVM*     jacl_vm_new_ex(const JaclConfig* config);
JACL_API void        jacl_vm_free(JaclVM* vm);

/* --- Eval API --- */

JACL_API JaclVal     jacl_eval(JaclVM* vm, const char* source);
JACL_API JaclVal     jacl_eval_file(JaclVM* vm, const char* path);
JACL_API bool        jacl_is_error(JaclVal val);
JACL_API const char* jacl_error_message(JaclVM* vm, JaclVal err);

/* --- Value constructors --- */

JACL_API JaclVal     jacl_nil(void);
JACL_API JaclVal     jacl_bool_val(bool b);
JACL_API JaclVal     jacl_i32_val(int32_t n);
JACL_API JaclVal     jacl_i64_val(JaclVM* vm, int64_t n);
JACL_API JaclVal     jacl_u32_val(uint32_t n);
JACL_API JaclVal     jacl_u64_val(JaclVM* vm, uint64_t n);
JACL_API JaclVal     jacl_f32_val(float f);
JACL_API JaclVal     jacl_f64_val(JaclVM* vm, double f);
JACL_API JaclVal     jacl_string(JaclVM* vm, const char* s, size_t len);
JACL_API JaclVal     jacl_string_cstr(JaclVM* vm, const char* s);

/* --- Value extractors --- */

JACL_API int32_t     jacl_as_i32_val(JaclVal val);
JACL_API int64_t     jacl_as_i64_val(JaclVal val);
JACL_API uint32_t    jacl_as_u32_val(JaclVal val);
JACL_API uint64_t    jacl_as_u64_val(JaclVal val);
JACL_API float       jacl_as_f32_val(JaclVal val);
JACL_API double      jacl_as_f64_val(JaclVal val);
JACL_API bool        jacl_as_bool_val(JaclVal val);
JACL_API const char* jacl_as_cstr(JaclVM* vm, JaclVal val, size_t* len_out);

/* --- Type query --- */

JACL_API JaclType    jacl_typeof(JaclVal val);

/* --- GC handle API --- */

JACL_API JaclHandle  jacl_handle_new(JaclVM* vm, JaclVal val);
JACL_API JaclVal     jacl_handle_get(JaclHandle h);
JACL_API void        jacl_handle_free(JaclVM* vm, JaclHandle h);

/* --- Native function registration --- */

JACL_API bool        jacl_register_fn(JaclVM* vm, const char* name, JaclNativeFn fn, int arity);

/* --- Calling JACL from C --- */

JACL_API JaclVal     jacl_call(JaclVM* vm, JaclVal fn, JaclVal* args, int argc);
JACL_API JaclVal     jacl_call_named(JaclVM* vm, const char* name, JaclVal* args, int argc);

/* --- Struct interop --- */

JACL_API JaclVal     jacl_struct_new(JaclVM* vm, const char* type_name, JaclVal* fields, int count);
JACL_API JaclVal     jacl_struct_get(JaclVM* vm, JaclVal s, const char* field_name);
JACL_API bool        jacl_struct_set(JaclVM* vm, JaclVal s, const char* field_name, JaclVal value);
JACL_API const char* jacl_struct_type_name(JaclVM* vm, JaclVal s);

/* --- OOM handler ---
 *
 * The handler runs after every allocation tier (GC, expand pool, malloc)
 * has failed. If it returns, the failing allocation returns NULL to the
 * runtime — callers must be prepared for that. The default handler logs
 * heap state and calls abort(), which is the right behavior for most
 * embedded programs but not for hosts that need to recover. Passing
 * NULL restores the default handler. Process-global. */
typedef void (*JaclOomHandler)(size_t request_size, void* user_data);

JACL_API void jacl_set_oom_handler(JaclOomHandler handler, void* user_data);

/* --- Trampoline API (requires libffi) --- */

JACL_API JaclTrampoline* jacl_trampoline_new(JaclVM* vm, JaclVal closure, const char* sig);
JACL_API void*           jacl_trampoline_ptr(JaclTrampoline* t);
JACL_API void            jacl_trampoline_free(JaclVM* vm, JaclTrampoline* t);
JACL_API bool            jacl_has_trampolines(void);

#ifdef __cplusplus
}
#endif

#endif /* JACL_H */
