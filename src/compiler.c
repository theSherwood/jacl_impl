/*
 * JACL Compiler
 *
 * Translates AST (from parser) into bytecode chunks for the VM.
 */

#ifndef COMPILER_C
#define COMPILER_C

#include <string.h>

/* --- Forward declarations for struct types --- */
typedef struct StructTypeRegistry StructTypeRegistry;

/* --- Compile Result --- */

typedef struct {
  BytecodeChunk chunk;
  uint32_t      error_count;
  const char*   error_message;  /* first error message, or NULL */
  bool          suspending;     /* true if top-level code is CPS-transformed */
  StructTypeRegistry* struct_registry; /* struct type metadata for VM */
} CompileResult;

/* --- API --- */

static CompileResult compiler_compile(ParseResult parse, arena_t* arena,
                                      JaclInternTable* intern_table,
                                      ThreadHeap* heap,
                                      StructTypeRegistry* seed_registry);

/* jacl_compile_program forward-declared after ProgramResult (below) */

/* --- Type system --- */

typedef enum {
  TYPE_DYN = 0,
  TYPE_BOOL,
  TYPE_NIL,
  TYPE_I32,
  TYPE_I64,
  TYPE_U32,
  TYPE_U64,
  TYPE_F32,
  TYPE_F64,
  TYPE_STR,
  TYPE_VEC,
  TYPE_MAP,
  TYPE_CLOSURE,
  TYPE_STRUCT,
  TYPE_STREAM
} JaclType;

static bool is_type_keyword(const char* word, size_t len) {
  if (len == 3) {
    if (memcmp(word, "i32", 3) == 0) return true;
    if (memcmp(word, "i64", 3) == 0) return true;
    if (memcmp(word, "u32", 3) == 0) return true;
    if (memcmp(word, "u64", 3) == 0) return true;
    if (memcmp(word, "f32", 3) == 0) return true;
    if (memcmp(word, "f64", 3) == 0) return true;
    if (memcmp(word, "str", 3) == 0) return true;
    if (memcmp(word, "dyn", 3) == 0) return true;
  } else if (len == 4) {
    if (memcmp(word, "bool", 4) == 0) return true;
  } else if (len == 6) {
    if (memcmp(word, "stream", 6) == 0) return true;
  }
  return false;
}

static JaclType type_from_keyword(const char* word, size_t len) {
  if (len == 3) {
    if (memcmp(word, "i32", 3) == 0) return TYPE_I32;
    if (memcmp(word, "i64", 3) == 0) return TYPE_I64;
    if (memcmp(word, "u32", 3) == 0) return TYPE_U32;
    if (memcmp(word, "u64", 3) == 0) return TYPE_U64;
    if (memcmp(word, "f32", 3) == 0) return TYPE_F32;
    if (memcmp(word, "f64", 3) == 0) return TYPE_F64;
    if (memcmp(word, "str", 3) == 0) return TYPE_STR;
    if (memcmp(word, "dyn", 3) == 0) return TYPE_DYN;
  } else if (len == 4) {
    if (memcmp(word, "bool", 4) == 0) return TYPE_BOOL;
  } else if (len == 6) {
    if (memcmp(word, "stream", 6) == 0) return TYPE_STREAM;
  }
  return TYPE_DYN;
}

static const char* type_name(JaclType t) {
  switch (t) {
    case TYPE_DYN:     return "dyn";
    case TYPE_BOOL:    return "bool";
    case TYPE_NIL:     return "nil";
    case TYPE_I32:     return "i32";
    case TYPE_I64:     return "i64";
    case TYPE_U32:     return "u32";
    case TYPE_U64:     return "u64";
    case TYPE_F32:     return "f32";
    case TYPE_F64:     return "f64";
    case TYPE_STR:     return "str";
    case TYPE_VEC:     return "vec";
    case TYPE_MAP:     return "map";
    case TYPE_CLOSURE: return "closure";
    case TYPE_STRUCT:  return "struct";
    case TYPE_STREAM:  return "stream";
  }
  return "unknown";
}

static bool is_numeric_type(JaclType t) {
  return t == TYPE_I32 || t == TYPE_I64 || t == TYPE_U32 ||
         t == TYPE_U64 || t == TYPE_F32 || t == TYPE_F64;
}

static bool is_unboxed_type(JaclType t) {
  return t == TYPE_I64 || t == TYPE_U64 || t == TYPE_F64;
}

/* --- Struct type registry --- */

#define STRUCT_REGISTRY_MAX 32
#define STRUCT_MAX_FIELDS   64

typedef struct {
  const char* name;
  uint32_t    name_len;
  JaclVal     name_val;       /* inline string (for global_arities lookup) */
  struct {
    const char* name;
    uint32_t    name_len;
    JaclType    type;
    uint32_t    struct_type_idx; /* index into registry if type==TYPE_STRUCT */
    uint32_t    offset;          /* byte offset in struct memory (C-ABI) */
    uint32_t    size;            /* field size in bytes (C-ABI) */
  } fields[STRUCT_MAX_FIELDS];
  uint32_t field_count;
  uint32_t total_size;         /* total size including trailing padding */
  uint32_t alignment;          /* max alignment of all fields */
} StructTypeDef;

struct StructTypeRegistry {
  StructTypeDef defs[STRUCT_REGISTRY_MAX];
  uint32_t count;
};
/* typedef already forward-declared above */

/* C-ABI size and alignment for a JaclType */
static uint32_t struct__type_size(JaclType t, StructTypeRegistry* reg, uint32_t struct_idx) {
  switch (t) {
    case TYPE_BOOL:    return 1;
    case TYPE_NIL:     return 0;
    case TYPE_I32:
    case TYPE_U32:
    case TYPE_F32:     return 4;
    case TYPE_I64:
    case TYPE_U64:
    case TYPE_F64:     return 8;
    case TYPE_STR:
    case TYPE_VEC:
    case TYPE_MAP:
    case TYPE_CLOSURE:
    case TYPE_DYN:
    case TYPE_STREAM:  return 8; /* JaclVal / pointer */
    case TYPE_STRUCT:
      if (reg && struct_idx < reg->count) {
        return reg->defs[struct_idx].total_size;
      }
      return 8; /* fallback */
  }
  return 8;
}

static uint32_t struct__type_align(JaclType t, StructTypeRegistry* reg, uint32_t struct_idx) {
  switch (t) {
    case TYPE_BOOL:    return 1;
    case TYPE_NIL:     return 1;
    case TYPE_I32:
    case TYPE_U32:
    case TYPE_F32:     return 4;
    case TYPE_I64:
    case TYPE_U64:
    case TYPE_F64:     return 8;
    case TYPE_STR:
    case TYPE_VEC:
    case TYPE_MAP:
    case TYPE_CLOSURE:
    case TYPE_DYN:
    case TYPE_STREAM:  return 8;
    case TYPE_STRUCT:
      if (reg && struct_idx < reg->count) {
        return reg->defs[struct_idx].alignment;
      }
      return 8;
  }
  return 8;
}

static uint32_t struct__align_up(uint32_t offset, uint32_t align) {
  return (offset + align - 1) & ~(align - 1);
}

/* Look up a struct type by name in the registry. Returns index or UINT32_MAX if not found. */
static uint32_t struct_registry__find(StructTypeRegistry* reg, const char* name, uint32_t name_len) {
  if (!reg) return UINT32_MAX;
  for (uint32_t i = 0; i < reg->count; i++) {
    if (reg->defs[i].name_len == name_len &&
        memcmp(reg->defs[i].name, name, name_len) == 0) {
      return i;
    }
  }
  return UINT32_MAX;
}

/* Register an inline anonymous struct type from a canonical string like "struct{x:i32,y:i32}".
   Returns registry index or UINT32_MAX on error. Uses structural equivalence: if an identical
   canonical string already exists in the registry, returns that index. */
static uint32_t compiler__register_inline_struct(
    StructTypeRegistry* reg, const char* spec, uint32_t spec_len) {
  if (!reg) return UINT32_MAX;

  /* Check for structural equivalence (same canonical string) */
  uint32_t existing = struct_registry__find(reg, spec, spec_len);
  if (existing != UINT32_MAX) return existing;

  if (reg->count >= STRUCT_REGISTRY_MAX) return UINT32_MAX;

  /* Parse the canonical string: struct{name:type,name:type,...} */
  if (spec_len < 9 || memcmp(spec, "struct{", 7) != 0 || spec[spec_len - 1] != '}')
    return UINT32_MAX;

  StructTypeDef* sdef = &reg->defs[reg->count];
  sdef->name     = spec;
  sdef->name_len = spec_len;
  sdef->name_val = JACL_NIL; /* anonymous — no constructor */
  sdef->field_count = 0;

  /* Parse fields from the inner content between { and } */
  const char* p = spec + 7;
  const char* end = spec + spec_len - 1;
  uint32_t offset = 0;
  uint32_t max_align = 1;

  while (p < end) {
    if (sdef->field_count >= STRUCT_MAX_FIELDS) return UINT32_MAX;

    /* Parse field name (up to ':') */
    const char* colon = p;
    while (colon < end && *colon != ':') colon++;
    if (colon >= end) return UINT32_MAX;

    uint32_t fname_len = (uint32_t)(colon - p);
    const char* fname = p;

    /* Parse field type (up to ',' or end, handling nested struct{} braces) */
    const char* tstart = colon + 1;
    const char* tp = tstart;
    int depth = 0;
    while (tp < end) {
      if (*tp == '{') depth++;
      else if (*tp == '}') { if (depth == 0) break; depth--; }
      else if (*tp == ',' && depth == 0) break;
      tp++;
    }
    uint32_t tlen = (uint32_t)(tp - tstart);

    /* Resolve field type */
    JaclType ftype = TYPE_DYN;
    uint32_t f_struct_idx = 0;

    if (is_type_keyword(tstart, tlen)) {
      ftype = type_from_keyword(tstart, tlen);
    } else if (tlen > 7 && memcmp(tstart, "struct{", 7) == 0) {
      /* Nested inline struct — recursive registration */
      uint32_t nested_idx = compiler__register_inline_struct(reg, tstart, tlen);
      if (nested_idx == UINT32_MAX) return UINT32_MAX;
      ftype = TYPE_STRUCT;
      f_struct_idx = nested_idx;
    } else {
      /* Named struct type */
      uint32_t idx = struct_registry__find(reg, tstart, tlen);
      if (idx == UINT32_MAX) return UINT32_MAX;
      ftype = TYPE_STRUCT;
      f_struct_idx = idx;
    }

    /* Compute C-ABI layout */
    uint32_t fsize  = struct__type_size(ftype, reg, f_struct_idx);
    uint32_t falign = struct__type_align(ftype, reg, f_struct_idx);
    offset = struct__align_up(offset, falign);

    sdef->fields[sdef->field_count].name           = fname;
    sdef->fields[sdef->field_count].name_len       = fname_len;
    sdef->fields[sdef->field_count].type           = ftype;
    sdef->fields[sdef->field_count].struct_type_idx = f_struct_idx;
    sdef->fields[sdef->field_count].offset         = offset;
    sdef->fields[sdef->field_count].size           = fsize;
    sdef->field_count++;

    offset += fsize;
    if (falign > max_align) max_align = falign;

    /* Skip comma separator */
    p = tp;
    if (p < end && *p == ',') p++;
  }

  if (sdef->field_count == 0) return UINT32_MAX;

  sdef->total_size = struct__align_up(offset, max_align);
  sdef->alignment  = max_align;

  uint32_t idx = reg->count;
  reg->count++;
  return idx;
}

/* --- Module system structs --- */

#define COMPILER_MAX_PROC_PARAMS 16
#define MODULE_CACHE_MAX 64
#define MODULE_EXPORTS_MAX 64

typedef struct {
  const char* name;
  uint32_t    name_len;
  int16_t     arity;          /* -1 for non-procs (def/mut values) */
  bool        is_mutable;     /* true if exported as a box */
  bool        suspends;       /* true if proc is suspending */
  JaclType    type;           /* value type or return type for procs */
  JaclType    return_type;    /* proc return type (TYPE_DYN for non-procs) */
  JaclType    param_types[COMPILER_MAX_PROC_PARAMS];
  uint32_t    param_count;
} ExportEntry;

typedef struct {
  BytecodeChunk* chunk;        /* compiled bytecode for this module */
  const char*    path;         /* canonical (absolute) path */
  const char*    source;       /* original source text */
  ExportEntry*   exports;      /* array of exported names */
  uint32_t       export_count; /* number of exports */
  uint32_t       topo_order;   /* post-order index for topological sort */
  bool           compiled;     /* true once compilation is complete */
} Module;

/* --- Program Result (multi-module) --- */

typedef struct {
  Module**    modules;       /* modules in topological order (root last) */
  uint32_t    module_count;
  uint32_t    error_count;
  const char* error_message; /* first error message, or NULL */
  bool        suspending;    /* true if root module is CPS-transformed */
  StructTypeRegistry* struct_registry; /* struct type metadata for VM */
} ProgramResult;

static ProgramResult jacl_compile_program(const char* root_path,
                                          arena_t* arena,
                                          JaclInternTable* intern_table,
                                          ThreadHeap* heap);

typedef struct {
  Module*  modules[MODULE_CACHE_MAX]; /* compiled modules by slot */
  char*    paths[MODULE_CACHE_MAX];   /* canonical path per slot */
  uint32_t count;
  uint32_t topo_counter;              /* monotonic counter for post-order assignment */
  arena_t* arena;
} ModuleCache;

static void module_cache__init(ModuleCache* cache, arena_t* arena) {
  cache->count = 0;
  cache->topo_counter = 0;
  cache->arena = arena;
  for (uint32_t i = 0; i < MODULE_CACHE_MAX; i++) {
    cache->modules[i] = NULL;
    cache->paths[i]   = NULL;
  }
}

static Module* module_cache__find(ModuleCache* cache, const char* canonical_path) {
  for (uint32_t i = 0; i < cache->count; i++) {
    if (cache->paths[i] && strcmp(cache->paths[i], canonical_path) == 0) {
      return cache->modules[i];
    }
  }
  return NULL;
}

static Module* module_cache__add(ModuleCache* cache, const char* canonical_path) {
  if (cache->count >= MODULE_CACHE_MAX) return NULL;
  uint32_t idx = cache->count++;
  size_t path_len = strlen(canonical_path);
  char* path_copy = (char*)arena_alloc(cache->arena, path_len + 1);
  memcpy(path_copy, canonical_path, path_len + 1);
  cache->paths[idx] = path_copy;

  Module* mod = (Module*)arena_alloc(cache->arena, sizeof(Module));
  mod->chunk        = NULL;
  mod->path         = path_copy;
  mod->source       = NULL;
  mod->exports      = NULL;
  mod->export_count = 0;
  mod->topo_order   = 0;
  mod->compiled     = false;
  cache->modules[idx] = mod;
  return mod;
}

/* --- Module path resolution and circular import detection --- */

#define MODULE_IMPORT_STACK_MAX 32

typedef struct {
  const char* paths[MODULE_IMPORT_STACK_MAX]; /* canonical paths being compiled */
  uint32_t    count;
} ImportStack;

static void import_stack__init(ImportStack* stack) {
  stack->count = 0;
}

static bool import_stack__contains(ImportStack* stack, const char* canonical_path) {
  for (uint32_t i = 0; i < stack->count; i++) {
    if (strcmp(stack->paths[i], canonical_path) == 0) return true;
  }
  return false;
}

static bool import_stack__push(ImportStack* stack, const char* canonical_path) {
  if (stack->count >= MODULE_IMPORT_STACK_MAX) return false;
  stack->paths[stack->count++] = canonical_path;
  return true;
}

static void import_stack__pop(ImportStack* stack) {
  if (stack->count > 0) stack->count--;
}

/* Build a circular import chain string: "A -> B -> C -> A"
   The cycle_path is the path that was found again in the stack. */
static const char* import_stack__chain_str(ImportStack* stack,
                                           const char* cycle_path,
                                           arena_t* arena) {
  /* Find the start of the cycle in the stack */
  uint32_t start = 0;
  for (uint32_t i = 0; i < stack->count; i++) {
    if (strcmp(stack->paths[i], cycle_path) == 0) {
      start = i;
      break;
    }
  }

  /* Calculate total length: filenames joined by " -> " plus final " -> first" */
  size_t total = 0;
  for (uint32_t i = start; i < stack->count; i++) {
    /* Extract basename from path for readability */
    const char* p = stack->paths[i];
    const char* slash = strrchr(p, '/');
    const char* base = slash ? slash + 1 : p;
    total += strlen(base);
    if (i > start) total += 4; /* " -> " */
  }
  /* Add " -> first" at the end to show the cycle */
  const char* first_slash = strrchr(stack->paths[start], '/');
  const char* first_base = first_slash ? first_slash + 1 : stack->paths[start];
  total += 4 + strlen(first_base); /* " -> basename" */

  char* buf = (char*)arena_alloc(arena, (uint32_t)(total + 1));
  size_t pos = 0;
  for (uint32_t i = start; i < stack->count; i++) {
    if (i > start) {
      memcpy(buf + pos, " -> ", 4);
      pos += 4;
    }
    const char* p = stack->paths[i];
    const char* slash = strrchr(p, '/');
    const char* base = slash ? slash + 1 : p;
    size_t len = strlen(base);
    memcpy(buf + pos, base, len);
    pos += len;
  }
  memcpy(buf + pos, " -> ", 4);
  pos += 4;
  size_t flen = strlen(first_base);
  memcpy(buf + pos, first_base, flen);
  pos += flen;
  buf[pos] = '\0';
  return buf;
}

/* Resolve a use-declaration path relative to the importing file's directory.
   importer_path: canonical path of the file containing the `use` statement
   use_path:      the relative path string from the `use` declaration
   arena:         for allocating the result string
   Returns: canonical path on success, NULL on failure (file not found) */
static const char* module__resolve_path(const char* importer_path,
                                        const char* use_path,
                                        arena_t* arena) {
  /* Find the directory of the importing file */
  const char* last_slash = strrchr(importer_path, '/');
  size_t dir_len = last_slash ? (size_t)(last_slash - importer_path) : 0;

  /* Build the joined path: dir/use_path */
  size_t use_len = strlen(use_path);
  size_t joined_len = dir_len + 1 + use_len;
  char joined[1024];
  if (joined_len >= sizeof(joined)) return NULL;

  if (dir_len > 0) {
    memcpy(joined, importer_path, dir_len);
    joined[dir_len] = '/';
    memcpy(joined + dir_len + 1, use_path, use_len + 1);
  } else {
    memcpy(joined, use_path, use_len + 1);
  }

  /* Canonicalize with realpath() — resolves .., ., and symlinks */
  char resolved[1024];
  if (!realpath(joined, resolved)) return NULL; /* file not found */

  /* Copy into arena */
  size_t rlen = strlen(resolved);
  char* result = (char*)arena_alloc(arena, (uint32_t)(rlen + 1));
  memcpy(result, resolved, rlen + 1);
  return result;
}

/* --- Module privacy: underscore-prefix convention --- */

/* Returns true if a top-level name is private (underscore-prefixed).
   Private names are excluded from module export lists and cannot be
   imported by other modules. */
static bool module__is_private(const char* name, uint32_t name_len) {
  return name_len > 0 && name[0] == '_';
}

/* Read a file into arena-allocated memory. Returns NULL on failure. */
static char* module__read_file(const char* path, arena_t* arena) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size < 0) { fclose(f); return NULL; }
  char* buf = (char*)arena_alloc(arena, (uint32_t)(size + 1));
  size_t nread = fread(buf, 1, (size_t)size, f);
  buf[nread] = '\0';
  fclose(f);
  return buf;
}

/* --- Internal: Local variable tracking --- */

#define COMPILER_LOCALS_MAX 256
#define COMPILER_UPVALUES_MAX 256
#define COMPILER_TRY_PATCHES_MAX 128
/* COMPILER_MAX_PROC_PARAMS defined above with module structs */

typedef struct {
  JaclVal   name;         /* inline string name */
  int       depth;        /* scope depth when declared */
  int16_t   known_arity;  /* arity if bound to a proc, -1 = unknown */
  bool      is_mutable;   /* true if declared with mut */
  bool      is_param;     /* true if this is a function parameter */
  bool      suspends;     /* true if bound to a suspending proc */
  bool      captures_mutable; /* true if bound to a closure that captures mutable state */
  JaclType  type;         /* compile-time type (default TYPE_DYN) */
  uint32_t  struct_type_idx; /* struct registry index when type==TYPE_STRUCT */
  JaclType  return_type;  /* proc return type (TYPE_DYN for non-procs) */
  JaclType* param_types;  /* proc param types (NULL for non-procs, arena-allocated) */
} Local;

/* --- Internal: Global arity tracking --- */

#define COMPILER_GLOBAL_ARITIES_MAX 64

typedef struct {
  JaclVal   name;
  int16_t   known_arity;
  bool      is_mutable;   /* true if declared with mut */
  bool      suspends;     /* true if this is a suspending proc */
  bool      captures_mutable; /* true if bound to a closure that captures mutable state */
  JaclType  type;         /* compile-time type (default TYPE_DYN) */
  uint32_t  struct_type_idx; /* struct registry index when type==TYPE_STRUCT */
  JaclType  return_type;  /* proc return type (TYPE_DYN for non-procs) */
  JaclType  param_types[COMPILER_MAX_PROC_PARAMS]; /* proc param types */
} GlobalArity;

typedef struct {
  uint8_t   index;    /* local slot (if is_local) or parent upvalue index */
  uint8_t   is_local; /* 1 = capture from enclosing locals, 0 = from parent upvalues */
  JaclVal   name;     /* for debug/lookup */
  bool      is_mutable; /* true if capturing a mut binding */
  bool      suspends;   /* true if capturing a suspending proc */
  bool      captures_mutable; /* true if capturing a closure that captures mutable state */
  JaclType  type;     /* compile-time type (default TYPE_DYN) */
  uint32_t  struct_type_idx; /* struct registry index when type==TYPE_STRUCT */
} Upvalue;

/* --- Internal: Suspension analysis --- */

#define SUSPENSION_MAP_MAX 256
#define SUSPENSION_CALLEES_MAX 64

typedef struct {
  JaclVal name;
  bool    suspends;
  bool    is_generator; /* true if proc contains yield (calling returns stream) */
} SuspensionEntry;

typedef struct {
  SuspensionEntry entries[SUSPENSION_MAP_MAX];
  uint32_t count;
} SuspensionMap;

static bool suspension_map_lookup(SuspensionMap* map, JaclVal name) {
  for (uint32_t i = 0; i < map->count; i++) {
    if (map->entries[i].name == name) {
      return map->entries[i].suspends;
    }
  }
  return false;
}

static bool suspension_map_is_generator(SuspensionMap* map, JaclVal name) {
  for (uint32_t i = 0; i < map->count; i++) {
    if (map->entries[i].name == name) {
      return map->entries[i].is_generator;
    }
  }
  return false;
}

static void suspension_map_set(SuspensionMap* map, JaclVal name,
                               bool suspends, bool is_generator) {
  for (uint32_t i = 0; i < map->count; i++) {
    if (map->entries[i].name == name) {
      map->entries[i].suspends = suspends;
      map->entries[i].is_generator = is_generator;
      return;
    }
  }
  if (map->count < SUSPENSION_MAP_MAX) {
    map->entries[map->count].name = name;
    map->entries[map->count].suspends = suspends;
    map->entries[map->count].is_generator = is_generator;
    map->count++;
  }
}

/* Info collected per proc during suspension analysis */
typedef struct {
  JaclVal  name;
  bool     direct_suspends;   /* directly contains await/parallel/race */
  bool     has_yield;          /* directly contains yield */
  bool     has_indirect_call; /* calls through $var (unknown closure) */
  JaclVal  callees[SUSPENSION_CALLEES_MAX];
  uint32_t callee_count;
} ProcSuspendInfo;

#define MAX_PROC_INFOS 256

typedef struct {
  ProcSuspendInfo procs[MAX_PROC_INFOS];
  uint32_t count;
} ProcSuspendInfoList;

/* Walk an AST subtree within a proc body to find suspension points and callees.
   Does NOT recurse into nested proc definitions (they have their own scope). */
static void analyze__walk_body(AstNode* node, ProcSuspendInfo* info) {
  if (!node) return;

  switch (node->type) {
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING) {
        const char* name = head->data.lit_string.value;
        uint32_t len = head->data.lit_string.length;

        /* Direct suspension points */
        if ((len == 5 && memcmp(name, "await", 5) == 0) ||
            (len == 8 && memcmp(name, "parallel", 8) == 0) ||
            (len == 4 && memcmp(name, "race", 4) == 0)) {
          info->direct_suspends = true;
          /* Still recurse into args (they might contain calls) */
          for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
            analyze__walk_body(node->data.command.args[i], info);
          }
          return;
        }

        /* Yield is a suspension point (needs CPS) and marks proc as generator */
        if (len == 5 && memcmp(name, "yield", 5) == 0) {
          info->direct_suspends = true;
          info->has_yield = true;
          /* Still recurse into args (they might contain calls) */
          for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
            analyze__walk_body(node->data.command.args[i], info);
          }
          return;
        }

        /* Skip recursion INTO nested proc bodies (they have their own scope) */
        if (len == 4 && memcmp(name, "proc", 4) == 0) {
          return;
        }

        /* spawn is NOT a suspension point; its block arg is a separate
           closure scope — skip recursion (like proc) */
        if (len == 5 && memcmp(name, "spawn", 5) == 0) {
          return;
        }

        /* Record callee name for named calls (for transitive propagation) */
        if (len <= 7) {
          JaclVal callee_name = jacl_inline_string(name, len);
          if (info->callee_count < SUSPENSION_CALLEES_MAX) {
            info->callees[info->callee_count++] = callee_name;
          }
        }
      } else if (head->type == AST_VAR_REF) {
        /* Indirect call through variable ($f ...) */
        info->has_indirect_call = true;
      }

      /* Recurse into arguments */
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        analyze__walk_body(node->data.command.args[i], info);
      }
      break;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        analyze__walk_body(node->data.block.commands[i], info);
      }
      break;
    }
    case AST_INTERP_STRING: {
      for (uint32_t i = 0; i < node->data.interp_string.count; i++) {
        analyze__walk_body(node->data.interp_string.segments[i], info);
      }
      break;
    }
    case AST_BREAK: {
      if (node->data.break_stmt.value) {
        analyze__walk_body(node->data.break_stmt.value, info);
      }
      break;
    }
    case AST_RETURN: {
      if (node->data.return_stmt.value) {
        analyze__walk_body(node->data.return_stmt.value, info);
      }
      break;
    }
    default:
      break;
  }
}

/* Recursively collect proc definitions from AST, analyzing each body */
static void analyze__collect_procs(AstNode* node, ProcSuspendInfoList* list) {
  if (!node) return;

  switch (node->type) {
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      uint32_t argc = node->data.command.arg_count;
      AstNode** args = node->data.command.args;

      if (head->type == AST_LIT_STRING &&
          head->data.lit_string.length == 4 &&
          memcmp(head->data.lit_string.value, "proc", 4) == 0) {

        /* Determine name and body indices based on argc */
        uint32_t name_idx, body_idx;
        if (argc == 4)      { name_idx = 1; body_idx = 3; }
        else if (argc == 3) { name_idx = 0; body_idx = 2; }
        else goto recurse_args;

        if (args[name_idx]->type != AST_LIT_STRING) goto recurse_args;
        uint32_t name_len = args[name_idx]->data.lit_string.length;
        if (name_len > 7) goto recurse_args;

        JaclVal proc_name = jacl_inline_string(
            args[name_idx]->data.lit_string.value, name_len);

        if (list->count < MAX_PROC_INFOS) {
          ProcSuspendInfo* info = &list->procs[list->count++];
          info->name = proc_name;
          info->direct_suspends = false;
          info->has_yield = false;
          info->has_indirect_call = false;
          info->callee_count = 0;

          /* Walk the body to find suspension points and callees */
          if (args[body_idx]->type == AST_BLOCK) {
            analyze__walk_body(args[body_idx], info);
          }
        }

        /* Recurse into body to find nested procs */
        if (args[body_idx]->type == AST_BLOCK) {
          analyze__collect_procs(args[body_idx], list);
        }
        return;
      }

      recurse_args:
      for (uint32_t i = 0; i < argc; i++) {
        analyze__collect_procs(args[i], list);
      }
      break;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        analyze__collect_procs(node->data.block.commands[i], list);
      }
      break;
    }
    case AST_BREAK: {
      if (node->data.break_stmt.value) {
        analyze__collect_procs(node->data.break_stmt.value, list);
      }
      break;
    }
    case AST_RETURN: {
      if (node->data.return_stmt.value) {
        analyze__collect_procs(node->data.return_stmt.value, list);
      }
      break;
    }
    default:
      break;
  }
}

/* Pre-compilation suspension analysis: walk AST to determine which procs suspend.
   Returns a SuspensionMap that the compiler consults during code generation. */
static SuspensionMap compiler__analyze_suspension(AstNode** nodes, uint32_t count) {
  SuspensionMap map;
  ProcSuspendInfoList proc_list;
  memset(&map, 0, sizeof(map));
  memset(&proc_list, 0, sizeof(proc_list));

  /* Step 1: Collect all proc definitions and analyze bodies */
  for (uint32_t i = 0; i < count; i++) {
    analyze__collect_procs(nodes[i], &proc_list);
  }

  /* Step 2: Initialize suspension map from direct suspension */
  for (uint32_t i = 0; i < proc_list.count; i++) {
    suspension_map_set(&map, proc_list.procs[i].name,
                       proc_list.procs[i].direct_suspends,
                       proc_list.procs[i].has_yield);
  }

  /* Step 3: Fixpoint propagation.
     Rule 1: proc calling a suspending proc becomes suspending.
     Rule 2: if any suspending proc exists in program, a proc with
             indirect calls ($var) is conservatively suspending. */
  bool changed = true;
  while (changed) {
    changed = false;

    bool any_suspending = false;
    for (uint32_t i = 0; i < map.count; i++) {
      if (map.entries[i].suspends) { any_suspending = true; break; }
    }

    for (uint32_t i = 0; i < proc_list.count; i++) {
      if (suspension_map_lookup(&map, proc_list.procs[i].name)) continue;

      /* Rule 1: direct call to suspending proc (skip generators —
         calling a generator just creates a stream, doesn't suspend caller) */
      for (uint32_t j = 0; j < proc_list.procs[i].callee_count; j++) {
        if (suspension_map_lookup(&map, proc_list.procs[i].callees[j]) &&
            !suspension_map_is_generator(&map, proc_list.procs[i].callees[j])) {
          suspension_map_set(&map, proc_list.procs[i].name, true, false);
          changed = true;
          break;
        }
      }

      /* Rule 2: indirect call when suspending procs exist */
      if (any_suspending &&
          !suspension_map_lookup(&map, proc_list.procs[i].name) &&
          proc_list.procs[i].has_indirect_call) {
        suspension_map_set(&map, proc_list.procs[i].name, true, false);
        changed = true;
      }
    }
  }

  return map;
}

/* Check if an AST subtree contains any suspension points.
   When map is non-NULL, also checks if named proc calls are suspending. */
static bool ast__contains_suspension(AstNode* node, SuspensionMap* map) {
  if (!node) return false;

  switch (node->type) {
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING) {
        const char* name = head->data.lit_string.value;
        uint32_t len = head->data.lit_string.length;
        if ((len == 5 && memcmp(name, "await", 5) == 0) ||
            (len == 8 && memcmp(name, "parallel", 8) == 0) ||
            (len == 4 && memcmp(name, "race", 4) == 0) ||
            (len == 5 && memcmp(name, "yield", 5) == 0)) {
          return true;
        }
        /* Don't recurse into nested proc or spawn definitions
           (their block args are separate closure scopes) */
        if ((len == 4 && memcmp(name, "proc", 4) == 0) ||
            (len == 5 && memcmp(name, "spawn", 5) == 0)) {
          return false;
        }
        /* Check if this is a call to a known suspending proc.
           Generator calls return a stream immediately — they don't suspend. */
        if (map && len <= 7) {
          JaclVal name_val = jacl_inline_string(name, len);
          if (suspension_map_lookup(map, name_val) &&
              !suspension_map_is_generator(map, name_val)) return true;
        }
      }
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        if (ast__contains_suspension(node->data.command.args[i], map))
          return true;
      }
      return false;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        if (ast__contains_suspension(node->data.block.commands[i], map))
          return true;
      }
      return false;
    }
    case AST_BREAK: {
      if (node->data.break_stmt.value) {
        return ast__contains_suspension(node->data.break_stmt.value, map);
      }
      return false;
    }
    case AST_RETURN: {
      if (node->data.return_stmt.value) {
        return ast__contains_suspension(node->data.return_stmt.value, map);
      }
      return false;
    }
    default:
      return false;
  }
}

/* Check if an AST subtree contains any set! calls (mutable global mutation).
   Skips nested proc/spawn/parallel/race definitions since those are separate
   closure scopes with independent pinning decisions. */
/**
 * Collect all mut declaration names directly in this AST subtree.
 * Skips nested proc/spawn/parallel/race scopes (they are separate bodies).
 */
#define AST_LOCAL_MUTS_MAX 64
static void ast__collect_local_muts(AstNode* node, JaclVal* names,
                                     uint32_t* count) {
  if (!node || *count >= AST_LOCAL_MUTS_MAX) return;

  switch (node->type) {
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING) {
        const char* hname = head->data.lit_string.value;
        uint32_t hlen = head->data.lit_string.length;
        /* Record mut declarations */
        if (hlen == 3 && memcmp(hname, "mut", 3) == 0) {
          uint32_t argc = node->data.command.arg_count;
          if (argc >= 2 && node->data.command.args[0]->type == AST_LIT_STRING) {
            AstNode* name_node = node->data.command.args[0];
            names[*count] = jacl_inline_string(
                name_node->data.lit_string.value,
                name_node->data.lit_string.length);
            (*count)++;
          }
          return;
        }
        /* Skip nested scope boundaries */
        if ((hlen == 4 && memcmp(hname, "proc", 4) == 0) ||
            (hlen == 5 && memcmp(hname, "spawn", 5) == 0) ||
            (hlen == 8 && memcmp(hname, "parallel", 8) == 0) ||
            (hlen == 4 && memcmp(hname, "race", 4) == 0)) {
          return;
        }
      }
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        ast__collect_local_muts(node->data.command.args[i], names, count);
      }
      break;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        ast__collect_local_muts(node->data.block.commands[i], names, count);
      }
      break;
    }
    default:
      break;
  }
}

/**
 * Check if an AST subtree contains set! targeting a non-local variable.
 * A "local" here means a mut declared within the same body scope.
 *
 * NOTE: This is a conservative syntactic analysis, NOT full escape analysis.
 * It covers:
 *   - Direct set! on non-local mutable variables
 *   - $var references to mut/box bindings from enclosing scopes (US-002)
 *   - Transitive capture: closure capturing another closure that captures
 *     a mutable binding (US-003). Detected via captures_mutable flag on
 *     Local/Upvalue/GlobalArity — set after proc compilation if any upvalue
 *     is_mutable or captures_mutable. Propagated through upvalue resolution,
 *     continuation eager capture, and checked for both $var refs and
 *     function call targets in concurrent bodies.
 * Out of scope:
 *   - Box references stored in collections then retrieved on another thread
 *   - Dynamic box creation passed indirectly through data structures
 */
static bool ast__contains_nonlocal_set_impl(AstNode* node,
                                             JaclVal* local_muts,
                                             uint32_t local_mut_count) {
  if (!node) return false;

  switch (node->type) {
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING) {
        const char* name = head->data.lit_string.value;
        uint32_t len = head->data.lit_string.length;
        if (len == 3 && memcmp(name, "set", 3) == 0) {
          /* Check if the target is a local mut */
          uint32_t argc = node->data.command.arg_count;
          if (argc >= 1 && node->data.command.args[0]->type == AST_LIT_STRING) {
            AstNode* target = node->data.command.args[0];
            JaclVal target_name = jacl_inline_string(
                target->data.lit_string.value,
                target->data.lit_string.length);
            for (uint32_t i = 0; i < local_mut_count; i++) {
              if (local_muts[i] == target_name) return false; /* local mut */
            }
          }
          return true; /* non-local or unresolved — needs pinning */
        }
        /* Skip nested scope boundaries — they get their own pinning */
        if ((len == 4 && memcmp(name, "proc", 4) == 0) ||
            (len == 5 && memcmp(name, "spawn", 5) == 0) ||
            (len == 8 && memcmp(name, "parallel", 8) == 0) ||
            (len == 4 && memcmp(name, "race", 4) == 0)) {
          return false;
        }
      }
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        if (ast__contains_nonlocal_set_impl(node->data.command.args[i],
                                             local_muts, local_mut_count))
          return true;
      }
      return false;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        if (ast__contains_nonlocal_set_impl(node->data.block.commands[i],
                                             local_muts, local_mut_count))
          return true;
      }
      return false;
    }
    default:
      return false;
  }
}

/**
 * Check if a block AST contains set! targeting a non-local mutable variable.
 * First collects all mut declarations in the block, then checks if any set!
 * targets a name not in that set. Skips nested proc/spawn/parallel/race scopes.
 *
 * Used to decide whether a concurrent body (spawn/parallel/race) needs to be
 * pinned to thread 0. Bodies with only local mutations can run on any worker.
 */
static bool ast__contains_nonlocal_set(AstNode* block) {
  JaclVal local_muts[AST_LOCAL_MUTS_MAX];
  uint32_t local_mut_count = 0;

  /* First pass: collect all mut names declared in this body */
  ast__collect_local_muts(block, local_muts, &local_mut_count);

  /* Second pass: check if any set! targets a non-local name */
  return ast__contains_nonlocal_set_impl(block, local_muts, local_mut_count);
}

/* --- Internal: Loop context for break/continue --- */

#define COMPILER_LOOP_DEPTH_MAX 16
#define COMPILER_BREAK_PATCHES_MAX 32
#define COMPILER_CONTINUE_PATCHES_MAX 32

typedef struct {
  uint32_t loop_start;     /* bytecode offset of loop condition (for OP_LOOP) */
  uint32_t break_patches[COMPILER_BREAK_PATCHES_MAX];
  uint32_t break_patch_count;
  uint32_t continue_patches[COMPILER_CONTINUE_PATCHES_MAX];
  uint32_t continue_patch_count;
  uint32_t local_count_at_loop; /* local_count before loop locals were pushed */
  bool     is_for_loop;         /* true for inlined for-loops */
} LoopContext;

/* --- Internal: Compiler state --- */

typedef struct Compiler Compiler;
struct Compiler {
  BytecodeChunk*   chunk;
  arena_t*         arena;
  ThreadHeap*      heap;          /* GC heap for string interning */
  JaclInternTable* intern_table;  /* shared intern table for heap strings */
  uint32_t         error_count;
  const char*      first_error;
  Local            locals[COMPILER_LOCALS_MAX];
  uint32_t         local_count;
  int              scope_depth;
  Upvalue          upvalues[COMPILER_UPVALUES_MAX];
  uint32_t         upvalue_count;
  Compiler*        enclosing;  /* parent compiler for upvalue resolution */
  GlobalArity      global_arities[COMPILER_GLOBAL_ARITIES_MAX];
  uint32_t         global_arity_count;
  uint32_t         try_patches[COMPILER_TRY_PATCHES_MAX];
  uint32_t         try_patch_count;
  bool             in_try_body;
  bool             in_non_suspending_callback; /* error if suspension inside */
  SuspensionMap*   suspension_map;  /* pre-computed suspension analysis */
  bool             is_cps;          /* true if this proc is CPS-transformed */
  bool             in_concurrent_body; /* true inside spawn/parallel/race body */
  bool             pin_all_closures;  /* true when concurrent body touches mutable globals */
  bool             force_global_procs; /* procs emit OP_DEF_GLOBAL even at scope>0 */
  JaclType         expected_type;   /* contextual type hint for RHS compilation */
  JaclType         last_expr_type;  /* type of the last compiled expression */
  uint32_t         last_struct_idx; /* struct type index when last_expr_type==TYPE_STRUCT */
  JaclType         return_type;     /* declared return type for current function */
  ModuleCache*     module_cache;    /* shared cache of compiled modules */
  Module*          current_module;  /* module currently being compiled */
  ImportStack*     import_stack;    /* shared import stack for circular detection */
  const char*      module_prefix;   /* "basename::" for namespace-prefixed globals */
  uint32_t         module_prefix_len;
  StructTypeRegistry* struct_registry; /* shared struct type registry (root compiler owns) */
  LoopContext          loop_stack[COMPILER_LOOP_DEPTH_MAX];
  uint32_t             loop_depth;     /* current nesting depth (0 = not in loop) */
  bool                 has_yield;      /* true if current proc body contains yield */
};

static void compiler__init(Compiler* c, BytecodeChunk* chunk, arena_t* arena,
                           JaclInternTable* intern_table, ThreadHeap* heap) {
  c->chunk         = chunk;
  c->arena         = arena;
  c->heap          = heap;
  c->intern_table  = intern_table;
  c->error_count   = 0;
  c->first_error   = NULL;
  c->local_count   = 0;
  c->scope_depth   = 0;
  c->upvalue_count = 0;
  c->enclosing     = NULL;
  c->global_arity_count = 0;
  c->try_patch_count = 0;
  c->in_try_body     = false;
  c->in_non_suspending_callback = false;
  c->suspension_map  = NULL;
  c->is_cps          = false;
  c->in_concurrent_body = false;
  c->pin_all_closures   = false;
  c->force_global_procs = false;
  c->expected_type   = TYPE_DYN;
  c->last_expr_type  = TYPE_DYN;
  c->last_struct_idx = UINT32_MAX;
  c->return_type     = TYPE_DYN;
  c->module_cache    = NULL;
  c->current_module  = NULL;
  c->import_stack    = NULL;
  c->module_prefix     = NULL;
  c->module_prefix_len = 0;
  c->struct_registry   = NULL;
  c->loop_depth        = 0;
  c->has_yield         = false;
}

/* Forward declarations for module compilation (defined after compiler_compile) */
static bool compiler__compile_module(const char* canonical_path,
                                     Compiler* importer,
                                     uint32_t line, uint32_t col);

/* Build "basename::" prefix string for a module path (arena-allocated). */
static const char* module__build_prefix(const char* canonical_path, arena_t* arena,
                                         uint32_t* out_len) {
  const char* slash = strrchr(canonical_path, '/');
  const char* basename = slash ? slash + 1 : canonical_path;
  uint32_t blen = (uint32_t)strlen(basename);
  uint32_t plen = blen + 2; /* "basename::" */
  char* prefix = (char*)arena_alloc(arena, plen + 1);
  memcpy(prefix, basename, blen);
  prefix[blen] = ':';
  prefix[blen + 1] = ':';
  prefix[plen] = '\0';
  *out_len = plen;
  return prefix;
}

/* Create a namespace-prefixed global name constant.
   In module context, returns interned "prefix::name".
   Outside module context, returns inline string. */
static JaclVal compiler__global_name_val(Compiler* c, const char* name,
                                          uint32_t name_len) {
  /* Walk to root compiler to find module prefix */
  Compiler* root = c;
  while (root->enclosing) root = root->enclosing;

  if (root->module_prefix) {
    /* Build prefixed name and intern it */
    char buf[256];
    uint32_t total = root->module_prefix_len + name_len;
    if (total >= sizeof(buf)) total = sizeof(buf) - 1;
    memcpy(buf, root->module_prefix, root->module_prefix_len);
    memcpy(buf + root->module_prefix_len, name,
           total - root->module_prefix_len);
    buf[total] = '\0';
    return jacl_intern(c->heap, c->intern_table, buf, total);
  }

  return jacl_inline_string(name, name_len);
}

/* --- Internal: Emit helpers --- */

static void compiler__emit_byte(Compiler* c, uint8_t byte, uint32_t line) {
  chunk_write(c->chunk, byte, line);
}

static void compiler__emit_u16(Compiler* c, uint16_t value, uint32_t line) {
  chunk_write_u16(c->chunk, value, line);
}

static void compiler__emit_constant(Compiler* c, JaclVal value, uint32_t line) {
  uint16_t index = chunk_add_constant(c->chunk, value);
  compiler__emit_byte(c, OP_CONST, line);
  compiler__emit_u16(c, index, line);
}

/* --- Internal: Error reporting --- */

static void compiler__error(Compiler* c, uint32_t line, uint32_t col,
                            const char* message) {
  c->error_count++;
  if (!c->first_error) {
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "line %u, col %u: %s", line, col, message);
    if (n < 0) n = 0;
    char* msg = (char*)arena_alloc(c->arena, (uint32_t)n + 1);
    memcpy(msg, buf, (uint32_t)n + 1);
    c->first_error = msg;
  }
}

/* --- Internal: Scope and local variable helpers --- */

static void compiler__begin_scope(Compiler* c) {
  c->scope_depth++;
}

static void compiler__end_scope(Compiler* c, uint32_t line) {
  c->scope_depth--;
  uint32_t pop_count = 0;
  while (c->local_count > 0 &&
         c->locals[c->local_count - 1].depth > c->scope_depth) {
    c->local_count--;
    pop_count++;
  }
  if (pop_count > 0) {
    compiler__emit_byte(c, OP_POP_N, line);
    compiler__emit_byte(c, (uint8_t)pop_count, line);
  }
}

static void compiler__add_local(Compiler* c, JaclVal name,
                                uint32_t line, uint32_t col) {
  if (c->local_count >= COMPILER_LOCALS_MAX) {
    compiler__error(c, line, col, "too many local variables in function");
    return;
  }
  Local* local = &c->locals[c->local_count++];
  local->name        = name;
  local->depth       = c->scope_depth;
  local->known_arity = -1;
  local->is_mutable  = false;
  local->is_param    = false;
  local->suspends    = false;
  local->captures_mutable = false;
  local->type        = TYPE_DYN;
  local->return_type = TYPE_DYN;
  local->param_types = NULL;
}

static int compiler__resolve_local(Compiler* c, JaclVal name) {
  for (int i = (int)c->local_count - 1; i >= 0; i--) {
    if (c->locals[i].name == name) {
      return i;
    }
  }
  return -1;
}

/* --- Internal: Global arity helpers --- */

static GlobalArity* compiler__find_global(Compiler* c, JaclVal name) {
  Compiler* root = c;
  while (root->enclosing) root = root->enclosing;
  for (uint32_t i = 0; i < root->global_arity_count; i++) {
    if (root->global_arities[i].name == name) {
      return &root->global_arities[i];
    }
  }
  return NULL;
}

static void compiler__set_global_arity(Compiler* c, JaclVal name, int16_t arity) {
  for (uint32_t i = 0; i < c->global_arity_count; i++) {
    if (c->global_arities[i].name == name) {
      c->global_arities[i].known_arity = arity;
      return;
    }
  }
  if (c->global_arity_count < COMPILER_GLOBAL_ARITIES_MAX) {
    GlobalArity* ga = &c->global_arities[c->global_arity_count];
    ga->name = name;
    ga->known_arity = arity;
    ga->is_mutable = false;
    ga->suspends = false;
    ga->captures_mutable = false;
    ga->type = TYPE_DYN;
    ga->return_type = TYPE_DYN;
    memset(ga->param_types, 0, sizeof(ga->param_types));
    c->global_arity_count++;
  }
}

/* --- Internal: Struct type registry access --- */

static StructTypeRegistry* compiler__get_struct_registry(Compiler* c) {
  Compiler* root = c;
  while (root->enclosing) root = root->enclosing;
  return root->struct_registry;
}

/* Resolve a type annotation string to a JaclType.
   Handles built-in types and named struct types.
   Returns true if resolved, false if unknown type. */
static bool compiler__resolve_type(Compiler* c, const char* word, uint32_t len,
                                    JaclType* out_type) {
  if (is_type_keyword(word, len)) {
    *out_type = type_from_keyword(word, len);
    return true;
  }
  /* Check for inline struct type string */
  if (len > 7 && memcmp(word, "struct{", 7) == 0) {
    *out_type = TYPE_STRUCT;
    return true;
  }
  /* Check struct registry */
  StructTypeRegistry* reg = compiler__get_struct_registry(c);
  if (reg && struct_registry__find(reg, word, len) != UINT32_MAX) {
    *out_type = TYPE_STRUCT;
    return true;
  }
  return false;
}

/* Check if a string is a valid type annotation (built-in or struct name) */
static bool compiler__is_type_annotation(Compiler* c, const char* word, uint32_t len) {
  JaclType dummy;
  return compiler__resolve_type(c, word, len, &dummy);
}

/* --- Internal: Upvalue resolution --- */

static int compiler__add_upvalue(Compiler* c, uint8_t index, uint8_t is_local,
                                  JaclVal name) {
  /* Check if this upvalue already exists */
  for (uint32_t i = 0; i < c->upvalue_count; i++) {
    if (c->upvalues[i].index == index &&
        c->upvalues[i].is_local == is_local) {
      return (int)i;
    }
  }
  if (c->upvalue_count >= COMPILER_UPVALUES_MAX) {
    return -1;
  }
  c->upvalues[c->upvalue_count].index      = index;
  c->upvalues[c->upvalue_count].is_local   = is_local;
  c->upvalues[c->upvalue_count].name       = name;
  c->upvalues[c->upvalue_count].is_mutable = false;
  c->upvalues[c->upvalue_count].suspends   = false;
  c->upvalues[c->upvalue_count].captures_mutable = false;
  c->upvalues[c->upvalue_count].type       = TYPE_DYN;
  return (int)c->upvalue_count++;
}

static int compiler__resolve_upvalue(Compiler* c, JaclVal name) {
  if (!c->enclosing) return -1;

  /* Check if the variable is a local in the enclosing scope */
  int local = compiler__resolve_local(c->enclosing, name);
  if (local != -1) {
    int uv = compiler__add_upvalue(c, (uint8_t)local, 1, name);
    if (uv != -1) {
      if (c->enclosing->locals[local].is_mutable)
        c->upvalues[uv].is_mutable = true;
      c->upvalues[uv].captures_mutable = c->enclosing->locals[local].captures_mutable;
      c->upvalues[uv].suspends = c->enclosing->locals[local].suspends;
      c->upvalues[uv].type = c->enclosing->locals[local].type;
      c->upvalues[uv].struct_type_idx = c->enclosing->locals[local].struct_type_idx;
    }
    return uv;
  }

  /* Check if it's an upvalue in the enclosing scope (transitive capture) */
  int upvalue = compiler__resolve_upvalue(c->enclosing, name);
  if (upvalue != -1) {
    int uv = compiler__add_upvalue(c, (uint8_t)upvalue, 0, name);
    if (uv != -1) {
      if (c->enclosing->upvalues[upvalue].is_mutable)
        c->upvalues[uv].is_mutable = true;
      c->upvalues[uv].captures_mutable = c->enclosing->upvalues[upvalue].captures_mutable;
      c->upvalues[uv].suspends = c->enclosing->upvalues[upvalue].suspends;
      c->upvalues[uv].type = c->enclosing->upvalues[upvalue].type;
      c->upvalues[uv].struct_type_idx = c->enclosing->upvalues[upvalue].struct_type_idx;
    }
    return uv;
  }

  return -1;
}

/**
 * Collect all locally declared names (def + mut) in an AST body.
 * Used by compiler__body_captures_mutable to distinguish local vs captured vars.
 * Skips nested proc/spawn/parallel/race scopes (they are separate bodies).
 */
#define AST_LOCAL_NAMES_MAX 128
static void ast__collect_local_names(AstNode* node, JaclVal* names,
                                      uint32_t* count) {
  if (!node || *count >= AST_LOCAL_NAMES_MAX) return;

  switch (node->type) {
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING) {
        const char* hname = head->data.lit_string.value;
        uint32_t hlen = head->data.lit_string.length;
        /* Record def and mut declarations */
        if ((hlen == 3 && memcmp(hname, "def", 3) == 0) ||
            (hlen == 3 && memcmp(hname, "mut", 3) == 0)) {
          uint32_t argc = node->data.command.arg_count;
          if (argc >= 2 && node->data.command.args[0]->type == AST_LIT_STRING) {
            AstNode* name_node = node->data.command.args[0];
            uint32_t nlen = name_node->data.lit_string.length;
            if (nlen <= 7) {
              names[*count] = jacl_inline_string(
                  name_node->data.lit_string.value, nlen);
              (*count)++;
            }
          }
          return;
        }
        /* Skip nested scope boundaries */
        if ((hlen == 4 && memcmp(hname, "proc", 4) == 0) ||
            (hlen == 5 && memcmp(hname, "spawn", 5) == 0) ||
            (hlen == 8 && memcmp(hname, "parallel", 8) == 0) ||
            (hlen == 4 && memcmp(hname, "race", 4) == 0)) {
          return;
        }
      }
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        ast__collect_local_names(node->data.command.args[i], names, count);
      }
      break;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        ast__collect_local_names(node->data.block.commands[i], names, count);
      }
      break;
    }
    default:
      break;
  }
}

/**
 * Check if an AST body contains $var references to mutable bindings
 * from an enclosing compiler scope, or references to closures that
 * transitively capture mutable state (US-003).
 *
 * Conservative: skips spawn/parallel/race scopes (they get own pinning)
 * but does NOT skip nested proc scopes (captures propagate upward).
 * May have false positives from proc-local parameter shadowing.
 */

/* Helper: check if a name resolves to a binding with is_mutable or
   captures_mutable in the enclosing scope chain. */
static bool compiler__name_touches_mutable(Compiler* enclosing, JaclVal name) {
  for (Compiler* cc = enclosing; cc; cc = cc->enclosing) {
    int slot = compiler__resolve_local(cc, name);
    if (slot != -1)
      return cc->locals[slot].is_mutable || cc->locals[slot].captures_mutable;
    for (uint32_t i = 0; i < cc->upvalue_count; i++) {
      if (cc->upvalues[i].name == name)
        return cc->upvalues[i].is_mutable || cc->upvalues[i].captures_mutable;
    }
  }
  /* Check global arities */
  GlobalArity* ga = compiler__find_global(enclosing, name);
  if (ga) return ga->is_mutable || ga->captures_mutable;
  return false;
}

static bool ast__refs_nonlocal_mutable_impl(AstNode* node,
                                             JaclVal* local_names,
                                             uint32_t local_name_count,
                                             Compiler* enclosing) {
  if (!node) return false;

  switch (node->type) {
    case AST_VAR_REF: {
      uint32_t len = node->data.var_ref.length;
      if (len > 7) return false;
      JaclVal name = jacl_inline_string(node->data.var_ref.name, len);
      /* Check if locally declared in the body */
      for (uint32_t i = 0; i < local_name_count; i++) {
        if (local_names[i] == name) return false;
      }
      /* Not local — check if mutable or captures_mutable in enclosing scope */
      return compiler__name_touches_mutable(enclosing, name);
    }
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING) {
        const char* hname = head->data.lit_string.value;
        uint32_t hlen = head->data.lit_string.length;
        /* Skip nested concurrent scopes — they get their own pinning */
        if ((hlen == 5 && memcmp(hname, "spawn", 5) == 0) ||
            (hlen == 8 && memcmp(hname, "parallel", 8) == 0) ||
            (hlen == 4 && memcmp(hname, "race", 4) == 0)) {
          return false;
        }
        /* Check if function call target is a non-local closure that
           transitively captures mutable state (US-003). */
        if (hlen <= 7) {
          JaclVal fname = jacl_inline_string(hname, hlen);
          bool is_local_name = false;
          for (uint32_t i = 0; i < local_name_count; i++) {
            if (local_names[i] == fname) { is_local_name = true; break; }
          }
          if (!is_local_name &&
              compiler__name_touches_mutable(enclosing, fname))
            return true;
        }
      }
      /* Check head (for $var calls) */
      if (ast__refs_nonlocal_mutable_impl(head, local_names,
                                           local_name_count, enclosing))
        return true;
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        if (ast__refs_nonlocal_mutable_impl(node->data.command.args[i],
                                             local_names, local_name_count,
                                             enclosing))
          return true;
      }
      return false;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        if (ast__refs_nonlocal_mutable_impl(node->data.block.commands[i],
                                             local_names, local_name_count,
                                             enclosing))
          return true;
      }
      return false;
    }
    default:
      return false;
  }
}

/**
 * Check if a concurrent body captures a mutable (mut/box) binding from
 * an enclosing scope via $var references.
 *
 * Used alongside ast__contains_nonlocal_set() to decide pinning.
 * If either check is true, the body is pinned to thread 0.
 */
static bool compiler__body_captures_mutable(Compiler* enclosing,
                                             AstNode* body_block) {
  JaclVal local_names[AST_LOCAL_NAMES_MAX];
  uint32_t local_name_count = 0;
  ast__collect_local_names(body_block, local_names, &local_name_count);
  return ast__refs_nonlocal_mutable_impl(body_block, local_names,
                                          local_name_count, enclosing);
}

/* Forward declaration for compile_block_expr */
static void compiler__compile_node(Compiler* c, AstNode* node);

/* --- Internal: Jump patching helpers --- */

static uint32_t compiler__emit_jump(Compiler* c, uint8_t instruction,
                                     uint32_t line) {
  compiler__emit_byte(c, instruction, line);
  compiler__emit_byte(c, 0xFF, line);  /* placeholder high byte */
  compiler__emit_byte(c, 0xFF, line);  /* placeholder low byte */
  return c->chunk->code_count - 2;
}

static void compiler__patch_jump(Compiler* c, uint32_t offset) {
  uint32_t jump = c->chunk->code_count - offset - 2;
  c->chunk->code[offset]     = (uint8_t)((jump >> 8) & 0xFF);
  c->chunk->code[offset + 1] = (uint8_t)(jump & 0xFF);
}

/* --- Internal: Emit OP_CHECK_ERROR with offset 0 (return from frame) --- */

static void compiler__emit_check_error(Compiler* c, uint32_t line) {
  compiler__emit_byte(c, OP_CHECK_ERROR, line);
  if (c->in_try_body) {
    /* Record position for later patching to jump to try handler */
    if (c->try_patch_count < COMPILER_TRY_PATCHES_MAX) {
      c->try_patches[c->try_patch_count++] = c->chunk->code_count;
    }
    compiler__emit_u16(c, 0xFFFF, line);  /* placeholder */
  } else {
    compiler__emit_u16(c, 0, line);
  }
}

/* --- Internal: CPS transform helpers --- */

/* Forward declarations for CPS compilation */
static void compiler__compile_cps_stmts(Compiler* c, AstNode** stmts,
                                         uint32_t count, uint32_t line);
static void compiler__compile_node(Compiler* c, AstNode* node);
static int  compiler__head_matches(AstNode* head, const char* name, uint32_t len);
static void compiler__emit_check_error(Compiler* c, uint32_t line);
static void compiler__compile_command(Compiler* c, AstNode* node);
static void compiler__compile_block_expr(Compiler* c, AstNode* block_node);

/**
 * Check if an AST node IS a suspension point or CONTAINS one.
 * Suspension points: await, parallel, race, or calls to known-suspending procs.
 * Does NOT recurse into nested proc/fn definitions.
 */
static bool compiler__node_is_suspension(Compiler* c, AstNode* node) {
  if (!node) return false;

  if (node->type == AST_COMMAND) {
    AstNode* head = node->data.command.head;
    uint32_t argc = node->data.command.arg_count;
    AstNode** args = node->data.command.args;

    if (head->type == AST_LIT_STRING) {
      const char* name = head->data.lit_string.value;
      uint32_t len = head->data.lit_string.length;

      /* Direct suspension points */
      if ((len == 5 && memcmp(name, "await", 5) == 0) ||
          (len == 8 && memcmp(name, "parallel", 8) == 0) ||
          (len == 4 && memcmp(name, "race", 4) == 0) ||
          (len == 5 && memcmp(name, "yield", 5) == 0)) {
        return true;
      }

      /* Skip nested proc definitions — they have their own CPS */
      if (len == 4 && memcmp(name, "proc", 4) == 0) {
        return false;
      }

      /* Call to known-suspending proc (exclude generators — calling a
         generator just creates a stream, doesn't suspend the caller) */
      if (len <= 7 && c->suspension_map) {
        JaclVal name_val = jacl_inline_string(name, len);
        /* Skip if callee is a generator */
        if (suspension_map_is_generator(c->suspension_map, name_val)) {
          /* Not a suspension point — fall through to recurse into args */
        } else {
          /* Check locals first */
          int slot = compiler__resolve_local(c, name_val);
          if (slot != -1 && c->locals[slot].suspends) return true;
          /* Check globals */
          GlobalArity* ga = compiler__find_global(c, name_val);
          if (ga && ga->suspends) return true;
          /* Check suspension map */
          if (suspension_map_lookup(c->suspension_map, name_val)) return true;
        }
      }
    }

    /* Check if callee is a $var reference to a suspending closure */
    if (head->type == AST_VAR_REF) {
      uint32_t vlen = head->data.var_ref.length;
      if (vlen <= 7) {
        JaclVal vname = jacl_inline_string(head->data.var_ref.name, vlen);
        int slot = compiler__resolve_local(c, vname);
        if (slot != -1 && c->locals[slot].suspends) return true;
        int uv = compiler__resolve_upvalue(c, vname);
        if (uv != -1 && c->upvalues[uv].suspends) return true;
      }
    }

    /* Recurse into arguments (but skip proc definitions handled above) */
    for (uint32_t i = 0; i < argc; i++) {
      if (compiler__node_is_suspension(c, args[i])) return true;
    }
  }

  if (node->type == AST_BLOCK) {
    for (uint32_t i = 0; i < node->data.block.count; i++) {
      if (compiler__node_is_suspension(c, node->data.block.commands[i]))
        return true;
    }
  }

  return false;
}

/**
 * Emit code to push __k (continuation parameter) onto the stack.
 * __k is a local in the outermost CPS proc, accessed via local or upvalue.
 */
static void compiler__emit_get_k(Compiler* c, uint32_t line) {
  JaclVal k_name = jacl_inline_string("__k", 3);
  int local = compiler__resolve_local(c, k_name);
  if (local != -1) {
    compiler__emit_byte(c, OP_GET_LOCAL, line);
    compiler__emit_byte(c, (uint8_t)local, line);
    return;
  }
  int uv = compiler__resolve_upvalue(c, k_name);
  if (uv != -1) {
    compiler__emit_byte(c, OP_GET_UPVALUE, line);
    compiler__emit_byte(c, (uint8_t)uv, line);
    return;
  }
  compiler__error(c, line, 0, "internal error: __k not found in CPS context");
}

/**
 * Create a continuation closure for remaining statements and push it on the stack.
 * The continuation takes 1 parameter (the result of the suspension point).
 * It captures live variables from the enclosing scope via upvalues.
 */
static void compiler__emit_continuation(Compiler* c,
                                         JaclVal param_name,
                                         AstNode** remaining_stmts,
                                         uint32_t remaining_count,
                                         uint32_t line) {
  /* Allocate closure template */
  JaclClosure* cont = (JaclClosure*)arena_alloc(c->arena, sizeof(JaclClosure));
  chunk_init(&cont->chunk, c->arena);
  cont->param_count  = 1;
  cont->upvalue_count = 0;
  cont->upvalues     = NULL;
  cont->name         = "__cont";
  cont->min_args     = 1;
  cont->variadic     = false;
  cont->pinned       = c->pin_all_closures;
  cont->pin_worker_id = -1;

  JaclVal* pnames = (JaclVal*)arena_alloc(c->arena, sizeof(JaclVal));
  pnames[0] = param_name;
  cont->param_names = pnames;

  /* Create nested body compiler */
  Compiler cont_compiler;
  compiler__init(&cont_compiler, &cont->chunk, c->arena, c->intern_table, c->heap);
  cont_compiler.scope_depth    = 1;
  cont_compiler.enclosing      = c;
  cont_compiler.suspension_map = c->suspension_map;
  cont_compiler.is_cps         = true;
  cont_compiler.in_concurrent_body = c->in_concurrent_body;
  cont_compiler.pin_all_closures   = c->pin_all_closures;

  /* Copy global arities from parent for suspension lookups */
  {
    Compiler* root = c;
    while (root->enclosing) root = root->enclosing;
    memcpy(cont_compiler.global_arities, root->global_arities,
           sizeof(GlobalArity) * root->global_arity_count);
    cont_compiler.global_arity_count = root->global_arity_count;
  }

  /* Add parameter as local (slot 0) */
  compiler__add_local(&cont_compiler, param_name, line, 0);
  cont_compiler.locals[cont_compiler.local_count - 1].is_param = true;

  /* Pre-resolve __k as upvalue 0 so runtime can find it for error recovery */
  {
    JaclVal k_name = jacl_inline_string("__k", 3);
    compiler__resolve_upvalue(&cont_compiler, k_name);
  }

  /* Compile remaining statements with CPS */
  compiler__compile_cps_stmts(&cont_compiler, remaining_stmts, remaining_count, line);

  /* Emit OP_RETURN at end of continuation */
  compiler__emit_byte(&cont_compiler, OP_RETURN, line);

  /* Propagate errors from continuation compiler */
  c->error_count += cont_compiler.error_count;
  if (!c->first_error && cont_compiler.first_error) {
    c->first_error = cont_compiler.first_error;
  }

  /* Eagerly capture all live locals (depth >= 1) from parent scope into the
     continuation's upvalue array. This ensures nested closures (parallel bodies,
     race bodies, spawn bodies) compiled inside the continuation can access
     any parent variable through the transitive upvalue chain, even if the
     continuation body doesn't directly reference it. */
  for (uint32_t i = 0; i < c->local_count; i++) {
    if (c->locals[i].depth < 1) continue;
    int uv = compiler__add_upvalue(&cont_compiler, (uint8_t)i, 1,
                                    c->locals[i].name);
    if (uv != -1) {
      cont_compiler.upvalues[uv].is_mutable = c->locals[i].is_mutable;
      cont_compiler.upvalues[uv].captures_mutable = c->locals[i].captures_mutable;
      cont_compiler.upvalues[uv].suspends   = c->locals[i].suspends;
      cont_compiler.upvalues[uv].type       = c->locals[i].type;
      cont_compiler.upvalues[uv].struct_type_idx = c->locals[i].struct_type_idx;
    }
  }
  /* Also capture parent's upvalues transitively, so variables from
     grandparent+ scopes are available to nested closures. */
  for (uint32_t i = 0; i < c->upvalue_count; i++) {
    int uv = compiler__add_upvalue(&cont_compiler, (uint8_t)i, 0,
                                    c->upvalues[i].name);
    if (uv != -1) {
      cont_compiler.upvalues[uv].is_mutable = c->upvalues[i].is_mutable;
      cont_compiler.upvalues[uv].captures_mutable = c->upvalues[i].captures_mutable;
      cont_compiler.upvalues[uv].suspends   = c->upvalues[i].suspends;
      cont_compiler.upvalues[uv].type       = c->upvalues[i].type;
      cont_compiler.upvalues[uv].struct_type_idx = c->upvalues[i].struct_type_idx;
    }
  }

  /* Set upvalue count on closure */
  cont->upvalue_count = (uint8_t)cont_compiler.upvalue_count;

  /* Store closure in parent's constant pool */
  uint16_t closure_idx = chunk_add_constant(c->chunk, jacl_closure(cont));

  /* Emit OP_CLOSURE followed by upvalue descriptors */
  compiler__emit_byte(c, OP_CLOSURE, line);
  compiler__emit_u16(c, closure_idx, line);
  for (uint32_t i = 0; i < cont_compiler.upvalue_count; i++) {
    compiler__emit_byte(c, cont_compiler.upvalues[i].is_local, line);
    compiler__emit_byte(c, cont_compiler.upvalues[i].index, line);
  }
}

/**
 * Extract information from a def-await pattern: [def name [await expr]]
 * Returns true if the statement matches the pattern.
 */
static bool compiler__is_def_with_suspension(Compiler* c, AstNode* node,
                                              JaclVal* out_name,
                                              AstNode** out_value_node) {
  if (node->type != AST_COMMAND) return false;
  AstNode* head = node->data.command.head;
  if (!compiler__head_matches(head, "def", 3)) return false;

  uint32_t argc = node->data.command.arg_count;
  uint32_t name_idx = 0;
  uint32_t value_idx = 1;

  if (argc == 3) {
    /* Typed def: [def TYPE name value] */
    name_idx = 1;
    value_idx = 2;
  } else if (argc != 2) {
    return false;
  }

  AstNode** args = node->data.command.args;
  if (args[name_idx]->type != AST_LIT_STRING) return false;

  AstNode* value_node = args[value_idx];
  if (!compiler__node_is_suspension(c, value_node)) return false;

  uint32_t name_len = args[name_idx]->data.lit_string.length;
  if (name_len > 7) return false;

  *out_name = jacl_inline_string(args[name_idx]->data.lit_string.value, name_len);
  *out_value_node = value_node;
  return true;
}

/**
 * Check if a statement is a direct [await expr] call.
 */
static bool compiler__is_direct_yield(AstNode* node, AstNode** out_value_expr) {
  if (node->type != AST_COMMAND) return false;
  AstNode* head = node->data.command.head;
  if (!compiler__head_matches(head, "yield", 5)) return false;
  if (node->data.command.arg_count != 1) return false;
  *out_value_expr = node->data.command.args[0];
  return true;
}

static bool compiler__is_direct_await(AstNode* node, AstNode** out_future_expr) {
  if (node->type != AST_COMMAND) return false;
  AstNode* head = node->data.command.head;
  if (!compiler__head_matches(head, "await", 5)) return false;
  if (node->data.command.arg_count != 1) return false;
  *out_future_expr = node->data.command.args[0];
  return true;
}

/**
 * Check if a statement is a direct [parallel body1 body2 ...] call.
 */
static bool compiler__is_direct_parallel(AstNode* node) {
  if (node->type != AST_COMMAND) return false;
  AstNode* head = node->data.command.head;
  return compiler__head_matches(head, "parallel", 8);
}

static bool compiler__is_direct_race(AstNode* node) {
  if (node->type != AST_COMMAND) return false;
  AstNode* head = node->data.command.head;
  return compiler__head_matches(head, "race", 4);
}

/**
 * Compile a parallel body block as a closure (same pattern as spawn body).
 * Each body becomes a zero-arg closure (non-CPS) or 1-arg closure (CPS with __k).
 * Pushes the closure onto the stack.
 */
static void compiler__compile_parallel_body(Compiler* c, AstNode* body_block,
                                             uint32_t line, uint32_t col) {
  if (body_block->type != AST_BLOCK) {
    compiler__error(c, line, col, "parallel body must be a block");
    return;
  }

  uint32_t stmt_count = body_block->data.block.count;
  AstNode** stmts = body_block->data.block.commands;

  /* Check if the body contains suspension points */
  bool body_suspends = ast__contains_suspension(body_block, c->suspension_map);

  /* Allocate anonymous closure for the parallel body */
  JaclClosure* closure = (JaclClosure*)arena_alloc(c->arena, sizeof(JaclClosure));
  chunk_init(&closure->chunk, c->arena);
  closure->name         = "<parallel>";
  closure->upvalue_count = 0;
  closure->upvalues     = NULL;
  closure->param_names  = NULL;
  closure->min_args     = 0;
  closure->variadic     = false;
  closure->pin_worker_id = -1;

  /* Pin this body to thread 0 if it mutates non-local variables
     OR captures a mutable (mut/box) binding from an enclosing scope.
     Per-worker VM isolation means OP_SET_GLOBAL only modifies the local
     worker's env — pinning to thread 0 ensures all mutable state reads
     and writes go through a single worker for consistency.
     Bodies with only local mutations can safely run on any worker. */
  bool needs_pinning = ast__contains_nonlocal_set(body_block)
                    || compiler__body_captures_mutable(c, body_block);
  closure->pinned = needs_pinning;

  if (body_suspends) {
    /* CPS parallel body: hidden __k parameter */
    closure->param_count = 1;
    JaclVal* pnames = (JaclVal*)arena_alloc(c->arena, sizeof(JaclVal));
    pnames[0] = jacl_inline_string("__k", 3);
    closure->param_names = pnames;
  } else {
    closure->param_count = 0;
  }

  /* Create body compiler */
  Compiler body_compiler;
  compiler__init(&body_compiler, &closure->chunk, c->arena, c->intern_table, c->heap);
  body_compiler.scope_depth    = 1;
  body_compiler.enclosing      = c;
  body_compiler.suspension_map = c->suspension_map;
  body_compiler.pin_all_closures = needs_pinning;

  /* Copy global arities for suspension lookups */
  {
    Compiler* root = c;
    while (root->enclosing) root = root->enclosing;
    memcpy(body_compiler.global_arities, root->global_arities,
           sizeof(GlobalArity) * root->global_arity_count);
    body_compiler.global_arity_count = root->global_arity_count;
  }

  if (body_suspends) {
    /* Add __k as local (slot 0) */
    compiler__add_local(&body_compiler, jacl_inline_string("__k", 3), line, col);
    body_compiler.locals[body_compiler.local_count - 1].is_param = true;
    body_compiler.is_cps = true;
    body_compiler.in_concurrent_body = true;

    if (stmt_count == 0) {
      compiler__emit_get_k(&body_compiler, line);
      compiler__emit_byte(&body_compiler, OP_NIL, line);
      compiler__emit_byte(&body_compiler, OP_TAIL_CALL, line);
      compiler__emit_byte(&body_compiler, 1, line);
    } else {
      compiler__compile_cps_stmts(&body_compiler, stmts, stmt_count, line);
    }
    compiler__emit_byte(&body_compiler, OP_RETURN, line);
  } else {
    /* Non-suspending body: compile as block expression */
    body_compiler.in_concurrent_body = true;
    compiler__compile_block_expr(&body_compiler, body_block);
    compiler__emit_byte(&body_compiler, OP_RETURN, line);
  }

  /* Propagate errors */
  c->error_count += body_compiler.error_count;
  if (!c->first_error && body_compiler.first_error) {
    c->first_error = body_compiler.first_error;
  }

  closure->upvalue_count = (uint8_t)body_compiler.upvalue_count;

  /* Emit OP_CLOSURE + upvalue descriptors */
  uint16_t closure_idx = chunk_add_constant(c->chunk, jacl_closure(closure));
  compiler__emit_byte(c, OP_CLOSURE, line);
  compiler__emit_u16(c, closure_idx, line);
  for (uint32_t i = 0; i < body_compiler.upvalue_count; i++) {
    compiler__emit_byte(c, body_compiler.upvalues[i].is_local, line);
    compiler__emit_byte(c, body_compiler.upvalues[i].index, line);
  }
}

/**
 * Check if a statement is a call to a known-suspending proc at the top level.
 * Returns true and sets out_node for the call.
 */
static bool compiler__is_suspending_call(Compiler* c, AstNode* node) {
  if (node->type != AST_COMMAND) return false;
  AstNode* head = node->data.command.head;

  if (head->type == AST_LIT_STRING) {
    const char* name = head->data.lit_string.value;
    uint32_t len = head->data.lit_string.length;
    /* Skip built-in suspension points and generators handled separately */
    if ((len == 5 && memcmp(name, "await", 5) == 0) ||
        (len == 8 && memcmp(name, "parallel", 8) == 0) ||
        (len == 4 && memcmp(name, "race", 4) == 0) ||
        (len == 5 && memcmp(name, "yield", 5) == 0) ||
        (len == 3 && memcmp(name, "def", 3) == 0) ||
        (len == 4 && memcmp(name, "proc", 4) == 0)) {
      return false;
    }
    if (len <= 7 && c->suspension_map) {
      JaclVal name_val = jacl_inline_string(name, len);
      int slot = compiler__resolve_local(c, name_val);
      if (slot != -1 && c->locals[slot].suspends) return true;
      GlobalArity* ga = compiler__find_global(c, name_val);
      if (ga && ga->suspends) return true;
      if (suspension_map_lookup(c->suspension_map, name_val)) return true;
    }
  }

  /* Variable reference calling a suspending closure */
  if (head->type == AST_VAR_REF) {
    uint32_t vlen = head->data.var_ref.length;
    if (vlen <= 7) {
      JaclVal vname = jacl_inline_string(head->data.var_ref.name, vlen);
      int slot = compiler__resolve_local(c, vname);
      if (slot != -1 && c->locals[slot].suspends) return true;
      int uv = compiler__resolve_upvalue(c, vname);
      if (uv != -1 && c->upvalues[uv].suspends) return true;
    }
  }

  return false;
}

/**
 * Compile a call to a suspending proc, passing a continuation as __k.
 * The continuation captures the remaining statements.
 */
static void compiler__compile_suspending_call_cps(Compiler* c,
                                                    AstNode* call_node,
                                                    JaclVal param_name,
                                                    AstNode** remaining_stmts,
                                                    uint32_t remaining_count,
                                                    uint32_t line) {
  AstNode* head = call_node->data.command.head;
  uint32_t argc = call_node->data.command.arg_count;
  AstNode** args = call_node->data.command.args;

  /* Push callee onto stack */
  if (head->type == AST_LIT_STRING) {
    uint32_t name_len = head->data.lit_string.length;
    JaclVal name_val = jacl_inline_string(head->data.lit_string.value, name_len);
    int local_slot = compiler__resolve_local(c, name_val);
    if (local_slot != -1) {
      if (c->locals[local_slot].is_mutable) {
        compiler__emit_byte(c, OP_GET_CELL_LOCAL, line);
      } else {
        compiler__emit_byte(c, OP_GET_LOCAL, line);
      }
      compiler__emit_byte(c, (uint8_t)local_slot, line);
    } else {
      int uv = compiler__resolve_upvalue(c, name_val);
      if (uv != -1) {
        if (c->upvalues[uv].is_mutable) {
          compiler__emit_byte(c, OP_GET_CELL_UPVALUE, line);
        } else {
          compiler__emit_byte(c, OP_GET_UPVALUE, line);
        }
        compiler__emit_byte(c, (uint8_t)uv, line);
      } else {
        JaclVal gkey = compiler__global_name_val(c,
            head->data.lit_string.value, name_len);
        uint16_t name_idx = chunk_add_constant(c->chunk, gkey);
        compiler__emit_byte(c, OP_GET_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
      }
    }
  } else {
    /* Variable reference or expression as callee */
    compiler__compile_node(c, head);
  }

  /* Push regular arguments */
  for (uint32_t i = 0; i < argc; i++) {
    compiler__compile_node(c, args[i]);
  }

  /* Push continuation as __k (or pass parent __k directly for tail calls) */
  if (remaining_count == 0) {
    /* Tail call: pass __k directly so callee returns result to parent __k */
    compiler__emit_get_k(c, line);
  } else {
    compiler__emit_continuation(c, param_name, remaining_stmts, remaining_count, line);
  }

  /* Tail call with argc + 1 (extra __k param) — reuses frame */
  compiler__emit_byte(c, OP_TAIL_CALL, line);
  compiler__emit_byte(c, (uint8_t)(argc + 1), line);
}

/**
 * Check if an AST node is a [while cond body] with suspension in the body.
 */
static bool compiler__is_while_with_suspension(Compiler* c, AstNode* node) {
  if (node->type != AST_COMMAND) return false;
  AstNode* head = node->data.command.head;
  if (!compiler__head_matches(head, "while", 5)) return false;
  uint32_t argc = node->data.command.arg_count;
  if (argc != 2) return false;
  AstNode* body = node->data.command.args[1];
  return body->type == AST_BLOCK && compiler__node_is_suspension(c, body);
}

/**
 * CPS-compile a while loop whose body contains suspension points (yield).
 *
 * Transforms:   while cond { ...yield... }; remaining...
 * Into:         loop_cell = cell(nil)
 *               loop_fn = proc(__k) { if cond: body-CPS(loopback); else: __k(nil) }
 *               store loop_fn in loop_cell
 *               tail-call loop_fn(remaining_continuation)
 *
 * The loopback closure (used as __k inside body) calls loop_fn(__k) again
 * via the self-referencing cell, achieving recursion without stack growth.
 */
static void compiler__compile_cps_while(Compiler* c, AstNode* while_node,
                                         AstNode** remaining_stmts,
                                         uint32_t remaining_count,
                                         uint32_t line) {
  AstNode* cond_node = while_node->data.command.args[0];
  AstNode* body_block = while_node->data.command.args[1];
  uint32_t body_count = body_block->data.block.count;
  AstNode** body_stmts = body_block->data.block.commands;

  /* --- Step 1: Allocate cell for loop function self-reference --- */
  compiler__emit_byte(c, OP_NIL, line);
  compiler__emit_byte(c, OP_MAKE_CELL, line);
  JaclVal loop_cell_name = jacl_inline_string("__wlp", 5);
  compiler__add_local(c, loop_cell_name, line, 0);

  /* --- Step 2: Build the loop closure --- */
  JaclClosure* loop_cl = (JaclClosure*)arena_alloc(c->arena, sizeof(JaclClosure));
  chunk_init(&loop_cl->chunk, c->arena);
  loop_cl->param_count   = 1;  /* __k = post-while continuation */
  loop_cl->upvalue_count = 0;
  loop_cl->upvalues      = NULL;
  loop_cl->name          = "__while_loop";
  loop_cl->min_args      = 1;
  loop_cl->variadic      = false;
  loop_cl->pinned        = false;
  loop_cl->pin_worker_id = -1;
  loop_cl->is_generator  = false;
  JaclVal k_param = jacl_inline_string("__k", 3);
  loop_cl->param_names = (JaclVal*)arena_alloc(c->arena, sizeof(JaclVal));
  loop_cl->param_names[0] = k_param;

  Compiler loop_compiler;
  compiler__init(&loop_compiler, &loop_cl->chunk, c->arena, c->intern_table, c->heap);
  loop_compiler.scope_depth    = 1;
  loop_compiler.enclosing      = c;
  loop_compiler.suspension_map = c->suspension_map;
  loop_compiler.is_cps         = true;
  loop_compiler.has_yield      = true;  /* propagate generator flag */
  { Compiler* root = c; while (root->enclosing) root = root->enclosing;
    memcpy(loop_compiler.global_arities, root->global_arities,
           sizeof(GlobalArity) * root->global_arity_count);
    loop_compiler.global_arity_count = root->global_arity_count;
  }

  /* __k parameter at slot 0 */
  compiler__add_local(&loop_compiler, k_param, line, 0);
  loop_compiler.locals[loop_compiler.local_count - 1].is_param = true;

  /* Compile condition */
  compiler__compile_node(&loop_compiler, cond_node);
  uint32_t exit_jump = compiler__emit_jump(&loop_compiler, OP_JUMP_IF_FALSE, line);

  /* --- True branch: compile body with CPS, looping back at end --- */

  /* Build loopback closure: proc __r { get_cell __wlp; get __k; tail_call 1 }
     This closure is called at the end of the body, and it re-invokes the loop
     function with the same post-while continuation (__k). */
  JaclClosure* lb_cl = (JaclClosure*)arena_alloc(c->arena, sizeof(JaclClosure));
  chunk_init(&lb_cl->chunk, c->arena);
  lb_cl->param_count   = 1;
  lb_cl->upvalue_count = 0;
  lb_cl->upvalues      = NULL;
  lb_cl->name          = "__loop_back";
  lb_cl->min_args      = 1;
  lb_cl->variadic      = false;
  lb_cl->pinned        = false;
  lb_cl->pin_worker_id = -1;
  lb_cl->is_generator  = false;
  JaclVal r_param = jacl_inline_string("__r", 3);
  lb_cl->param_names = (JaclVal*)arena_alloc(c->arena, sizeof(JaclVal));
  lb_cl->param_names[0] = r_param;

  Compiler lb_compiler;
  compiler__init(&lb_compiler, &lb_cl->chunk, c->arena, c->intern_table, c->heap);
  lb_compiler.scope_depth = 1;
  lb_compiler.enclosing   = &loop_compiler;
  /* Add __r param at slot 0 */
  compiler__add_local(&lb_compiler, r_param, line, 0);
  lb_compiler.locals[lb_compiler.local_count - 1].is_param = true;

  /* Body: get loop_fn from cell, get __k from enclosing, tail-call loop_fn(__k) */
  JaclVal wlp_name = jacl_inline_string("__wlp", 5);
  int uv_loop = compiler__resolve_upvalue(&lb_compiler, wlp_name);
  compiler__emit_byte(&lb_compiler, OP_GET_CELL_UPVALUE, line);
  compiler__emit_byte(&lb_compiler, (uint8_t)uv_loop, line);

  int uv_k = compiler__resolve_upvalue(&lb_compiler, k_param);
  compiler__emit_byte(&lb_compiler, OP_GET_UPVALUE, line);
  compiler__emit_byte(&lb_compiler, (uint8_t)uv_k, line);

  compiler__emit_byte(&lb_compiler, OP_TAIL_CALL, line);
  compiler__emit_byte(&lb_compiler, 1, line);

  /* Propagate errors */
  c->error_count += lb_compiler.error_count;
  if (!c->first_error && lb_compiler.first_error) c->first_error = lb_compiler.first_error;

  /* Emit OP_CLOSURE for loopback in loop_compiler */
  lb_cl->upvalue_count = (uint8_t)lb_compiler.upvalue_count;
  uint16_t lb_idx = chunk_add_constant(loop_compiler.chunk, jacl_closure(lb_cl));
  compiler__emit_byte(&loop_compiler, OP_CLOSURE, line);
  compiler__emit_u16(&loop_compiler, lb_idx, line);
  for (uint32_t i = 0; i < lb_compiler.upvalue_count; i++) {
    compiler__emit_byte(&loop_compiler, lb_compiler.upvalues[i].is_local, line);
    compiler__emit_byte(&loop_compiler, lb_compiler.upvalues[i].index, line);
  }

  /* Shadow __k with the loopback closure so body CPS tail-calls it */
  uint32_t saved_local_count = loop_compiler.local_count;
  compiler__add_local(&loop_compiler, jacl_inline_string("__k", 3), line, 0);

  /* Compile body statements with CPS (yield becomes suspension point) */
  compiler__begin_scope(&loop_compiler);
  compiler__compile_cps_stmts(&loop_compiler, body_stmts, body_count, line);
  compiler__end_scope(&loop_compiler, line);

  /* Restore local count (shadow __k is cleaned up — unreachable code follows) */
  loop_compiler.local_count = saved_local_count;

  /* --- False branch (exit): tail-call __k(nil) --- */
  compiler__patch_jump(&loop_compiler, exit_jump);
  /* Access original __k at slot 0 (the parameter) directly */
  compiler__emit_byte(&loop_compiler, OP_GET_LOCAL, line);
  compiler__emit_byte(&loop_compiler, 0, line);
  compiler__emit_byte(&loop_compiler, OP_NIL, line);
  compiler__emit_byte(&loop_compiler, OP_TAIL_CALL, line);
  compiler__emit_byte(&loop_compiler, 1, line);

  /* Propagate errors from loop compiler */
  c->error_count += loop_compiler.error_count;
  if (!c->first_error && loop_compiler.first_error) c->first_error = loop_compiler.first_error;

  /* --- Step 3: Emit OP_CLOSURE for loop_fn in parent and store in cell --- */
  loop_cl->upvalue_count = (uint8_t)loop_compiler.upvalue_count;
  uint16_t loop_idx = chunk_add_constant(c->chunk, jacl_closure(loop_cl));
  compiler__emit_byte(c, OP_CLOSURE, line);
  compiler__emit_u16(c, loop_idx, line);
  for (uint32_t i = 0; i < loop_compiler.upvalue_count; i++) {
    compiler__emit_byte(c, loop_compiler.upvalues[i].is_local, line);
    compiler__emit_byte(c, loop_compiler.upvalues[i].index, line);
  }

  /* Store loop closure in the cell */
  int loop_cell_slot = compiler__resolve_local(c, loop_cell_name);
  compiler__emit_byte(c, OP_SET_CELL_LOCAL, line);
  compiler__emit_byte(c, (uint8_t)loop_cell_slot, line);

  /* --- Step 4: Call loop_fn(post_while_continuation) --- */
  /* Push loop_fn (get from cell) */
  compiler__emit_byte(c, OP_GET_CELL_LOCAL, line);
  compiler__emit_byte(c, (uint8_t)loop_cell_slot, line);

  /* Push __k for loop_fn: either remaining_continuation or enclosing __k */
  if (remaining_count == 0) {
    compiler__emit_get_k(c, line);
  } else {
    JaclVal cont_param = jacl_inline_string("__r", 3);
    compiler__emit_continuation(c, cont_param, remaining_stmts, remaining_count, line);
  }

  /* Tail-call loop_fn(post_while_continuation) */
  compiler__emit_byte(c, OP_TAIL_CALL, line);
  compiler__emit_byte(c, 1, line);
}

/**
 * Check if an AST node is an [if ...] command with suspension in any branch.
 * Does NOT check the condition — only the then/else blocks.
 */
static bool compiler__is_if_with_suspension(Compiler* c, AstNode* node) {
  if (node->type != AST_COMMAND) return false;
  AstNode* head = node->data.command.head;
  if (!compiler__head_matches(head, "if", 2)) return false;
  uint32_t argc = node->data.command.arg_count;
  AstNode** args = node->data.command.args;
  if (argc < 2) return false;
  if (args[1]->type == AST_BLOCK && compiler__node_is_suspension(c, args[1]))
    return true;
  if (argc >= 3 && args[2]->type == AST_BLOCK &&
      compiler__node_is_suspension(c, args[2]))
    return true;
  return false;
}

/**
 * Compile one branch of a CPS if statement.
 * If need_join is true, jk_name is a local holding the join continuation;
 * branches that suspend use it as __k, non-suspending branches call it directly.
 * If need_join is false, branches use the enclosing __k.
 */
static void compiler__compile_cps_branch(Compiler* c, AstNode* block,
                                          bool need_join, JaclVal jk_name,
                                          uint32_t line) {
  uint32_t bcount = block->data.block.count;
  AstNode** bstmts = block->data.block.commands;
  bool branch_suspends = compiler__node_is_suspension(c, block);

  if (branch_suspends) {
    uint32_t saved_local_count = c->local_count;
    if (need_join) {
      /* Shadow __k with join continuation so CPS stmts route through it */
      int jk = compiler__resolve_local(c, jk_name);
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, (uint8_t)jk, line);
      compiler__add_local(c, jacl_inline_string("__k", 3), line, 0);
    }
    compiler__compile_cps_stmts(c, bstmts, bcount, line);
    /* Remove shadow __k from compiler tracking without emitting POP.
       The stack slot is cleaned up by the eventual OP_RETURN. */
    c->local_count = saved_local_count;
  } else {
    /* Non-suspending: compile stmts normally, then call k(result) */
    for (uint32_t i = 0; i + 1 < bcount; i++) {
      compiler__compile_node(c, bstmts[i]);
      compiler__emit_check_error(c, line);
    }
    /* Push the continuation to call */
    if (need_join) {
      int jk = compiler__resolve_local(c, jk_name);
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, (uint8_t)jk, line);
    } else {
      compiler__emit_get_k(c, line);
    }
    /* Push result value */
    if (bcount > 0) {
      compiler__compile_node(c, bstmts[bcount - 1]);
    } else {
      compiler__emit_byte(c, OP_NIL, line);
    }
    /* Tail call k(result) — reuses frame */
    compiler__emit_byte(c, OP_TAIL_CALL, line);
    compiler__emit_byte(c, 1, line);
  }
}

/**
 * Compile an if-with-suspension in CPS mode.
 * Creates a join continuation for remaining_stmts (if any) and routes
 * each branch's result through it. Non-suspending branches tail-call
 * the join continuation directly.
 *
 * join_param is the parameter name for the join continuation
 * (e.g., "__if_r" for anonymous, or the def name when used for def-with-if).
 */
static void compiler__compile_cps_if(Compiler* c, AstNode* if_node,
                                      JaclVal join_param,
                                      AstNode** remaining_stmts,
                                      uint32_t remaining_count,
                                      uint32_t line) {
  AstNode** args = if_node->data.command.args;
  uint32_t argc = if_node->data.command.arg_count;

  bool need_join = (remaining_count > 0);
  JaclVal jk_name = jacl_inline_string("__jk", 4);

  if (need_join) {
    /* Create join continuation for remaining stmts */
    compiler__emit_continuation(c, join_param, remaining_stmts,
                                 remaining_count, line);
    compiler__add_local(c, jk_name, line, 0);
  }

  /* Compile condition */
  compiler__compile_node(c, args[0]);

  /* JUMP_IF_FALSE -> else */
  uint32_t then_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);

  /* Then branch */
  compiler__compile_cps_branch(c, args[1], need_join, jk_name, line);

  /* JUMP -> end (skip else) */
  uint32_t else_jump = compiler__emit_jump(c, OP_JUMP, line);

  /* Patch JUMP_IF_FALSE to here */
  compiler__patch_jump(c, then_jump);

  /* Else branch */
  if (argc >= 3) {
    compiler__compile_cps_branch(c, args[2], need_join, jk_name, line);
  } else {
    /* No else: call k(nil) */
    if (need_join) {
      int jk = compiler__resolve_local(c, jk_name);
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, (uint8_t)jk, line);
    } else {
      compiler__emit_get_k(c, line);
    }
    compiler__emit_byte(c, OP_NIL, line);
    compiler__emit_byte(c, OP_TAIL_CALL, line);
    compiler__emit_byte(c, 1, line);
  }

  /* Patch JUMP to here */
  compiler__patch_jump(c, else_jump);
}

/**
 * Check if a command has non-block arguments that contain suspension points.
 * Used to determine if argument extraction is needed.
 */
static bool compiler__has_suspending_non_block_args(Compiler* c, AstNode* node) {
  if (node->type != AST_COMMAND) return false;
  AstNode** args = node->data.command.args;
  uint32_t argc = node->data.command.arg_count;
  for (uint32_t i = 0; i < argc; i++) {
    if (args[i]->type != AST_BLOCK &&
        compiler__node_is_suspension(c, args[i])) {
      return true;
    }
  }
  return false;
}

/**
 * Shared helper: for each suspending non-block arg in args[0..argc), create a
 * synthetic [def __aN arg] statement and replace the arg with a $__aN var ref.
 * out_new_args must be caller-allocated with argc slots (pre-copied from args).
 * out_defs must be caller-allocated with argc slots (upper bound on susp count).
 * Returns the number of synthetic defs emitted (= number of suspending args).
 */
static uint32_t compiler__cps_extract_args_helper(
    Compiler* c, AstNode** args, uint32_t argc,
    AstNode** out_new_args, AstNode** out_defs) {
  memcpy(out_new_args, args, sizeof(AstNode*) * argc);
  uint32_t susp_count = 0;
  for (uint32_t i = 0; i < argc; i++) {
    if (args[i]->type == AST_BLOCK || !compiler__node_is_suspension(c, args[i]))
      continue;

    /* Generate temp name __a0, __a1, etc. */
    char tmp_name[8];
    snprintf(tmp_name, sizeof(tmp_name), "__a%u", susp_count);
    uint32_t name_len = (uint32_t)strlen(tmp_name);
    char* name_copy = (char*)arena_alloc(c->arena, name_len + 1);
    memcpy(name_copy, tmp_name, name_len + 1);

    /* Var ref node to replace the suspending arg */
    AstNode* var_ref = ast_alloc(c->arena);
    var_ref->type = AST_VAR_REF;
    var_ref->start = args[i]->start;
    var_ref->end = args[i]->end;
    var_ref->data.var_ref.name = name_copy;
    var_ref->data.var_ref.length = name_len;

    /* Name literal node for def */
    AstNode* name_node = ast_alloc(c->arena);
    name_node->type = AST_LIT_STRING;
    name_node->start = args[i]->start;
    name_node->end = args[i]->end;
    name_node->data.lit_string.value = name_copy;
    name_node->data.lit_string.length = name_len;

    /* def head */
    AstNode* def_head = ast_alloc(c->arena);
    def_head->type = AST_LIT_STRING;
    def_head->start = args[i]->start;
    def_head->end = args[i]->end;
    def_head->data.lit_string.value = "def";
    def_head->data.lit_string.length = 3;

    /* [def name original_arg] command */
    AstNode** def_args = ast_alloc_array(c->arena, 2);
    def_args[0] = name_node;
    def_args[1] = args[i];

    AstNode* def_cmd = ast_alloc(c->arena);
    def_cmd->type = AST_COMMAND;
    def_cmd->start = args[i]->start;
    def_cmd->end = args[i]->end;
    def_cmd->data.command.head = def_head;
    def_cmd->data.command.args = def_args;
    def_cmd->data.command.arg_count = 2;

    out_defs[susp_count] = def_cmd;
    out_new_args[i] = var_ref;
    susp_count++;
  }
  return susp_count;
}

/**
 * Extract suspending non-block arguments from a command into temp defs.
 * Creates synthetic AST: [def __a0 susp_arg0]; [def __a1 susp_arg1]; ...;
 * [modified_cmd with $__a0 $__a1 ...]; remaining_stmts.
 * Then compiles the full list through compiler__compile_cps_stmts.
 */
static void compiler__compile_cps_extract_args(Compiler* c, AstNode* cmd_node,
                                                AstNode** remaining_stmts,
                                                uint32_t remaining_count,
                                                uint32_t line) {
  uint32_t argc = cmd_node->data.command.arg_count;
  AstNode** new_args = ast_alloc_array(c->arena, argc);
  AstNode** defs = ast_alloc_array(c->arena, argc);
  uint32_t susp_count = compiler__cps_extract_args_helper(
      c, cmd_node->data.command.args, argc, new_args, defs);

  uint32_t total = susp_count + 1 + remaining_count;
  AstNode** new_stmts = ast_alloc_array(c->arena, total);
  for (uint32_t i = 0; i < susp_count; i++) new_stmts[i] = defs[i];

  AstNode* mod_cmd = ast_alloc(c->arena);
  *mod_cmd = *cmd_node;
  mod_cmd->data.command.args = new_args;
  new_stmts[susp_count] = mod_cmd;

  for (uint32_t i = 0; i < remaining_count; i++)
    new_stmts[susp_count + 1 + i] = remaining_stmts[i];

  compiler__compile_cps_stmts(c, new_stmts, total, line);
}

/**
 * Extract suspending args from a def's value expression and compile via CPS.
 * Turns [def x [f [await $a] [await $b]]] into:
 *   [def __a0 [await $a]]; [def __a1 [await $b]]; [def x [f $__a0 $__a1]]
 */
static void compiler__compile_cps_extract_def_value(
    Compiler* c, AstNode* def_stmt, JaclVal def_name, AstNode* value_node,
    AstNode** remaining_stmts, uint32_t remaining_count, uint32_t line) {
  (void)def_name;
  uint32_t v_argc = value_node->data.command.arg_count;
  AstNode** new_v_args = ast_alloc_array(c->arena, v_argc);
  AstNode** defs = ast_alloc_array(c->arena, v_argc);
  uint32_t susp_count = compiler__cps_extract_args_helper(
      c, value_node->data.command.args, v_argc, new_v_args, defs);

  uint32_t total = susp_count + 1 + remaining_count;
  AstNode** new_stmts = ast_alloc_array(c->arena, total);
  for (uint32_t i = 0; i < susp_count; i++) new_stmts[i] = defs[i];

  AstNode* mod_value = ast_alloc(c->arena);
  *mod_value = *value_node;
  mod_value->data.command.args = new_v_args;

  AstNode* mod_def = ast_alloc(c->arena);
  *mod_def = *def_stmt;
  uint32_t d_argc = def_stmt->data.command.arg_count;
  AstNode** mod_def_args = ast_alloc_array(c->arena, d_argc);
  memcpy(mod_def_args, def_stmt->data.command.args, sizeof(AstNode*) * d_argc);
  mod_def_args[d_argc - 1] = mod_value;
  mod_def->data.command.args = mod_def_args;
  new_stmts[susp_count] = mod_def;

  for (uint32_t i = 0; i < remaining_count; i++)
    new_stmts[susp_count + 1 + i] = remaining_stmts[i];

  compiler__compile_cps_stmts(c, new_stmts, total, line);
}

/**
 * CPS-aware compilation of a statement list.
 * Finds the first suspension point, compiles code before it normally,
 * creates a continuation for code after it, and emits OP_AWAIT or CPS call.
 * When no more suspension points exist, calls __k with the final result.
 */
static void compiler__compile_cps_stmts(Compiler* c, AstNode** stmts,
                                         uint32_t count, uint32_t line) {
  if (count == 0) {
    /* No statements: tail-call __k(nil) */
    compiler__emit_get_k(c, line);
    compiler__emit_byte(c, OP_NIL, line);
    compiler__emit_byte(c, OP_TAIL_CALL, line);
    compiler__emit_byte(c, 1, line);
    return;
  }

  /* Find the first statement containing a suspension point */
  uint32_t susp_idx = UINT32_MAX;
  for (uint32_t i = 0; i < count; i++) {
    if (compiler__node_is_suspension(c, stmts[i])) {
      susp_idx = i;
      break;
    }
  }

  if (susp_idx == UINT32_MAX) {
    /* No suspension points — compile all statements normally,
       then tail-call __k with the block result */

    /* All statements except the last: compile + pop */
    for (uint32_t i = 0; i + 1 < count; i++) {
      compiler__compile_node(c, stmts[i]);
      compiler__emit_check_error(c, line);
    }

    /* Last statement: push __k first (for tail-call), then compile expression */
    compiler__emit_get_k(c, line);
    compiler__compile_node(c, stmts[count - 1]);

    /* Tail-call __k(result) */
    compiler__emit_byte(c, OP_TAIL_CALL, line);
    compiler__emit_byte(c, 1, line);
    return;
  }

  /* Compile statements before the suspension point normally */
  for (uint32_t i = 0; i < susp_idx; i++) {
    compiler__compile_node(c, stmts[i]);
    compiler__emit_check_error(c, line);
  }

  AstNode* susp_stmt = stmts[susp_idx];
  AstNode** remaining = &stmts[susp_idx + 1];
  uint32_t remaining_count = count - susp_idx - 1;

  /* Determine the continuation parameter name */
  JaclVal cont_param = jacl_inline_string("__r", 3); /* default for unnamed results */

  /* Case 1: Direct [await expr] */
  AstNode* future_expr = NULL;
  if (compiler__is_direct_await(susp_stmt, &future_expr)) {
    /* Compile the future expression */
    compiler__compile_node(c, future_expr);

    if (remaining_count == 0) {
      /* Tail await: pass __k directly as continuation so OP_AWAIT calls
         __k(result) without an intermediate wrapper. */
      compiler__emit_get_k(c, line);
    } else {
      /* Create continuation for remaining statements */
      compiler__emit_continuation(c, cont_param, remaining, remaining_count, line);
    }

    /* Emit OP_AWAIT */
    compiler__emit_byte(c, OP_AWAIT, susp_stmt->start.line);
    return;
  }

  /* Case 1b: Direct [parallel body1 body2 ...] */
  if (compiler__is_direct_parallel(susp_stmt)) {
    uint32_t par_argc = susp_stmt->data.command.arg_count;
    AstNode** par_args = susp_stmt->data.command.args;

    /* Compile each body as a closure (like spawn bodies) */
    for (uint32_t i = 0; i < par_argc; i++) {
      compiler__compile_parallel_body(c, par_args[i],
                                       susp_stmt->start.line,
                                       susp_stmt->start.column);
    }

    /* Emit continuation or __k */
    if (remaining_count == 0) {
      compiler__emit_get_k(c, line);
    } else {
      compiler__emit_continuation(c, cont_param, remaining, remaining_count, line);
    }

    compiler__emit_byte(c, OP_PARALLEL, susp_stmt->start.line);
    compiler__emit_byte(c, (uint8_t)par_argc, susp_stmt->start.line);
    return;
  }

  /* Case 1c: Direct [race body1 body2 ...] */
  if (compiler__is_direct_race(susp_stmt)) {
    uint32_t race_argc = susp_stmt->data.command.arg_count;
    AstNode** race_args = susp_stmt->data.command.args;

    for (uint32_t i = 0; i < race_argc; i++) {
      compiler__compile_parallel_body(c, race_args[i],
                                       susp_stmt->start.line,
                                       susp_stmt->start.column);
    }

    if (remaining_count == 0) {
      compiler__emit_get_k(c, line);
    } else {
      compiler__emit_continuation(c, cont_param, remaining, remaining_count, line);
    }

    compiler__emit_byte(c, OP_RACE, susp_stmt->start.line);
    compiler__emit_byte(c, (uint8_t)race_argc, susp_stmt->start.line);
    return;
  }

  /* Case 1d: Direct [yield expr] — generator CPS suspension */
  AstNode* yield_value_expr = NULL;
  if (compiler__is_direct_yield(susp_stmt, &yield_value_expr)) {
    /* Compile the value to yield */
    compiler__compile_node(c, yield_value_expr);

    /* Push continuation for remaining stmts (or __k if tail position) */
    if (remaining_count == 0) {
      compiler__emit_get_k(c, line);
    } else {
      compiler__emit_continuation(c, cont_param, remaining, remaining_count, line);
    }

    /* OP_YIELD: pops continuation then value, stores both, returns VM_YIELD */
    compiler__emit_byte(c, OP_YIELD, susp_stmt->start.line);
    c->has_yield = true;
    return;
  }

  /* Case 1e: [while cond { ... yield ... }] — CPS while loop */
  if (compiler__is_while_with_suspension(c, susp_stmt)) {
    compiler__compile_cps_while(c, susp_stmt, remaining, remaining_count,
                                 susp_stmt->start.line);
    c->has_yield = true;
    return;
  }

  /* Case 2: [def name [await expr]] */
  JaclVal def_name;
  AstNode* value_node = NULL;
  if (compiler__is_def_with_suspension(c, susp_stmt, &def_name, &value_node)) {
    /* Check if the value is a direct await */
    AstNode* def_future_expr = NULL;
    if (compiler__is_direct_await(value_node, &def_future_expr)) {
      /* [def name [await expr]] — name becomes continuation param */
      compiler__compile_node(c, def_future_expr);
      compiler__emit_continuation(c, def_name, remaining, remaining_count, line);
      compiler__emit_byte(c, OP_AWAIT, susp_stmt->start.line);
      return;
    }

    /* [def name [parallel ...]] — name becomes continuation param */
    if (compiler__is_direct_parallel(value_node)) {
      uint32_t par_argc = value_node->data.command.arg_count;
      AstNode** par_args = value_node->data.command.args;
      for (uint32_t i = 0; i < par_argc; i++) {
        compiler__compile_parallel_body(c, par_args[i],
                                         susp_stmt->start.line,
                                         susp_stmt->start.column);
      }
      compiler__emit_continuation(c, def_name, remaining, remaining_count, line);
      compiler__emit_byte(c, OP_PARALLEL, susp_stmt->start.line);
      compiler__emit_byte(c, (uint8_t)par_argc, susp_stmt->start.line);
      return;
    }

    /* [def name [race ...]] — name becomes continuation param */
    if (compiler__is_direct_race(value_node)) {
      uint32_t race_argc = value_node->data.command.arg_count;
      AstNode** race_args = value_node->data.command.args;
      for (uint32_t i = 0; i < race_argc; i++) {
        compiler__compile_parallel_body(c, race_args[i],
                                         susp_stmt->start.line,
                                         susp_stmt->start.column);
      }
      compiler__emit_continuation(c, def_name, remaining, remaining_count, line);
      compiler__emit_byte(c, OP_RACE, susp_stmt->start.line);
      compiler__emit_byte(c, (uint8_t)race_argc, susp_stmt->start.line);
      return;
    }

    /* [def name [suspending_call ...]] — name becomes continuation param */
    if (compiler__is_suspending_call(c, value_node)) {
      compiler__compile_suspending_call_cps(c, value_node, def_name,
                                             remaining, remaining_count,
                                             susp_stmt->start.line);
      return;
    }

    /* Value is an if-with-suspension — compile CPS if with def name as param */
    if (compiler__is_if_with_suspension(c, value_node)) {
      compiler__compile_cps_if(c, value_node, def_name, remaining,
                                remaining_count, susp_stmt->start.line);
      return;
    }

    /* Value has extractable suspending arguments — extract and retry */
    if (value_node->type == AST_COMMAND &&
        compiler__has_suspending_non_block_args(c, value_node)) {
      compiler__compile_cps_extract_def_value(c, susp_stmt, def_name,
                                               value_node, remaining,
                                               remaining_count, line);
      return;
    }

    /* Fallback: compile def normally and continue with remaining stmts */
    compiler__compile_node(c, susp_stmt);
    compiler__emit_check_error(c, line);
    compiler__compile_cps_stmts(c, remaining, remaining_count, line);
    return;
  }

  /* Case 3: Top-level call to suspending proc */
  if (compiler__is_suspending_call(c, susp_stmt)) {
    compiler__compile_suspending_call_cps(c, susp_stmt, cont_param,
                                           remaining, remaining_count,
                                           susp_stmt->start.line);
    return;
  }

  /* Case 4: if-with-suspension in branches */
  if (compiler__is_if_with_suspension(c, susp_stmt)) {
    compiler__compile_cps_if(c, susp_stmt, cont_param, remaining,
                              remaining_count, susp_stmt->start.line);
    return;
  }

  /* Case 5: Statement has extractable suspending arguments */
  if (compiler__has_suspending_non_block_args(c, susp_stmt)) {
    compiler__compile_cps_extract_args(c, susp_stmt, remaining,
                                        remaining_count, line);
    return;
  }

  /* Case 6: Fallback — compile normally and continue */
  compiler__compile_node(c, susp_stmt);
  compiler__emit_check_error(c, line);
  compiler__compile_cps_stmts(c, remaining, remaining_count, line);
}

/* --- Internal: Compile block as expression (last stmt value stays on stack) --- */

static void compiler__compile_block_expr(Compiler* c, AstNode* block_node) {
  uint32_t line  = block_node->start.line;
  uint32_t count = block_node->data.block.count;
  uint32_t scope_start_locals = c->local_count;

  compiler__begin_scope(c);

  if (count == 0) {
    compiler__end_scope(c, line);
    compiler__emit_byte(c, OP_NIL, line);
    return;
  }

  for (uint32_t i = 0; i < count - 1; i++) {
    compiler__compile_node(c, block_node->data.block.commands[i]);
    compiler__emit_check_error(c, line);
  }
  /* For the last statement, apply return type context if declared */
  if (c->return_type != TYPE_DYN) {
    c->expected_type = c->return_type;
  }
  compiler__compile_node(c, block_node->data.block.commands[count - 1]);

  /* Clean up locals while preserving the result on the stack top */
  uint32_t pop_count = c->local_count - scope_start_locals;
  c->scope_depth--;
  c->local_count = scope_start_locals;

  if (pop_count > 0) {
    /* Result is on top, locals are below it. Save result into the first
       local's slot, then POP_N removes the rest plus the old top copy. */
    compiler__emit_byte(c, OP_SET_LOCAL, line);
    compiler__emit_byte(c, (uint8_t)scope_start_locals, line);
    compiler__emit_byte(c, OP_POP_N, line);
    compiler__emit_byte(c, (uint8_t)pop_count, line);
  }
}

/* --- Internal: Command head matching --- */

static int compiler__head_matches(AstNode* head, const char* name, uint32_t len) {
  return head->type == AST_LIT_STRING &&
         head->data.lit_string.length == len &&
         memcmp(head->data.lit_string.value, name, len) == 0;
}

/* --- Internal: Determine known arity of an AST expression --- */

static int16_t compiler__node_known_arity(Compiler* c, AstNode* node) {
  if (node->type == AST_VAR_REF) {
    uint32_t name_len = node->data.var_ref.length;
    if (name_len <= 7) {
      JaclVal name_val = jacl_inline_string(node->data.var_ref.name, name_len);
      int slot = compiler__resolve_local(c, name_val);
      if (slot != -1) {
        return c->locals[slot].known_arity;
      }
      GlobalArity* ga = compiler__find_global(c, name_val);
      return ga ? ga->known_arity : -1;
    }
  }
  if (node->type == AST_LIT_STRING) {
    uint32_t name_len = node->data.lit_string.length;
    if (name_len <= 7) {
      JaclVal name_val = jacl_inline_string(node->data.lit_string.value, name_len);
      GlobalArity* ga = compiler__find_global(c, name_val);
      return ga ? ga->known_arity : -1;
    }
  }
  return -1;
}

/* --- Internal: Builtin arity error helper --- */

static void compiler__builtin_arity_error(Compiler* c, uint32_t line,
                                           uint32_t col, const char* name,
                                           const char* expected_desc,
                                           uint32_t got) {
  char err_msg[128];
  snprintf(err_msg, sizeof(err_msg),
           "builtin '%s' expects %s but got %d",
           name, expected_desc, (int)got);
  compiler__error(c, line, col, err_msg);
}

/* --- Internal: Auto-box unboxed types (emit OP_TO_DYN if needed) --- */

static void compiler__ensure_boxed(Compiler* c, uint32_t line) {
  if (is_unboxed_type(c->last_expr_type)) {
    compiler__emit_byte(c, OP_TO_DYN, line);
    compiler__emit_byte(c, (uint8_t)c->last_expr_type, line);
    c->last_expr_type = TYPE_DYN;
  }
}

/* --- Internal: Map dynamic opcode to typed opcode for a given type --- */

static uint8_t compiler__typed_op(uint8_t dyn_op, JaclType type) {
  if (type == TYPE_I64) {
    switch (dyn_op) {
      case OP_ADD: return OP_ADD_I64;
      case OP_SUB: return OP_SUB_I64;
      case OP_MUL: return OP_MUL_I64;
      case OP_DIV: return OP_DIV_I64;
      case OP_MOD: return OP_MOD_I64;
      case OP_LT:  return OP_LT_I64;
      case OP_GT:  return OP_GT_I64;
      case OP_LE:  return OP_LE_I64;
      case OP_GE:  return OP_GE_I64;
      case OP_EQ:  return OP_EQ_I64;
      default: return dyn_op;
    }
  }
  if (type == TYPE_U64) {
    switch (dyn_op) {
      case OP_ADD: return OP_ADD_I64;  /* u64 reuses i64 add */
      case OP_SUB: return OP_SUB_I64;
      case OP_MUL: return OP_MUL_I64;
      case OP_DIV: return OP_DIV_U64;
      case OP_MOD: return OP_MOD_U64;
      case OP_LT:  return OP_LT_U64;
      case OP_GT:  return OP_GT_U64;
      case OP_LE:  return OP_LE_U64;
      case OP_GE:  return OP_GE_U64;
      case OP_EQ:  return OP_EQ_I64;  /* u64 reuses i64 eq */
      default: return dyn_op;
    }
  }
  if (type == TYPE_F64) {
    switch (dyn_op) {
      case OP_ADD: return OP_ADD_F64;
      case OP_SUB: return OP_SUB_F64;
      case OP_MUL: return OP_MUL_F64;
      case OP_DIV: return OP_DIV_F64;
      case OP_MOD: return OP_MOD_F64;
      case OP_LT:  return OP_LT_F64;
      case OP_GT:  return OP_GT_F64;
      case OP_LE:  return OP_LE_F64;
      case OP_GE:  return OP_GE_F64;
      case OP_EQ:  return OP_EQ_F64;
      default: return dyn_op;
    }
  }
  return dyn_op;
}

/* --- Internal: Compile a typed binary operation --- */

static void compiler__compile_binary(Compiler* c, AstNode** args,
                                     uint8_t op, const char* op_verb,
                                     uint32_t line, uint32_t col) {
  /* Compile LHS */
  compiler__compile_node(c, args[0]);
  JaclType lhs_type = c->last_expr_type;

  /* Set contextual type for RHS (enables literal typing like [+ $a 1]) */
  if (lhs_type != TYPE_DYN) {
    c->expected_type = lhs_type;
  }

  /* Compile RHS */
  compiler__compile_node(c, args[1]);
  JaclType rhs_type = c->last_expr_type;
  c->expected_type = TYPE_DYN;

  /* Type checking — only enforced when unboxed types (i64/u64/f64) are involved,
     since unboxed values can't go through dynamic dispatch */
  if (is_unboxed_type(lhs_type) || is_unboxed_type(rhs_type)) {
    if (lhs_type != rhs_type) {
      char err[128];
      snprintf(err, sizeof(err), "type error: cannot %s %s and %s",
               op_verb, type_name(lhs_type), type_name(rhs_type));
      compiler__error(c, line, col, err);
      return;
    }
    /* Both same unboxed type — emit typed opcode */
    compiler__emit_byte(c, compiler__typed_op(op, lhs_type), line);
    bool is_cmp = (op == OP_EQ || op == OP_LT || op == OP_GT ||
                   op == OP_LE || op == OP_GE);
    c->last_expr_type = is_cmp ? TYPE_DYN : lhs_type;
  } else {
    /* Both boxed/dyn — generic dispatch */
    compiler__emit_byte(c, op, line);
    c->last_expr_type = TYPE_DYN;
  }
}

/* --- Internal: Shared HOF builtin compilation (transform/each/filter) --- */

static void compiler__compile_hof_builtin(Compiler* c, const char* name,
                                           AstNode** args, uint32_t argc,
                                           uint8_t opcode,
                                           uint32_t line, uint32_t col) {
  if (argc != 2) {
    compiler__builtin_arity_error(c, line, col, name, "2 arguments", argc);
    return;
  }
  /* Check if callback is a known suspending proc ($var reference) */
  if (args[1]->type == AST_VAR_REF && args[1]->data.var_ref.length <= 7) {
    JaclVal cb_name = jacl_inline_string(args[1]->data.var_ref.name,
                                          args[1]->data.var_ref.length);
    char err_msg[128];
    snprintf(err_msg, sizeof(err_msg),
             "cannot pass suspending closure to non-suspending builtin '%s'",
             name);
    int slot = compiler__resolve_local(c, cb_name);
    if (slot != -1 && c->locals[slot].suspends) {
      compiler__error(c, line, col, err_msg);
      return;
    }
    GlobalArity* ga = compiler__find_global(c, cb_name);
    if (ga && ga->suspends) {
      compiler__error(c, line, col, err_msg);
      return;
    }
  }
  /* Check if callback block contains suspension points */
  if (ast__contains_suspension(args[1], c->suspension_map)) {
    compiler__error(c, line, col,
        "cannot suspend inside non-suspending callback");
    return;
  }
  compiler__compile_node(c, args[0]);
  JaclType col_type = c->last_expr_type;
  {
    bool saved = c->in_non_suspending_callback;
    c->in_non_suspending_callback = true;
    compiler__compile_node(c, args[1]);
    c->in_non_suspending_callback = saved;
  }
  compiler__emit_byte(c, opcode, line);
  /* Preserve collection type: stream→stream, vec→vec */
  c->last_expr_type = col_type;
}

/* --- Internal: Compile a vector destructuring binding ---
 *
 * Handles both def and mut forms. Compiles the value expression,
 * emits OP_DESTRUCTURE_VEC or OP_DESTRUCTURE_VEC_REST, and creates bindings.
 *
 * For def (immutable): OP_DESTRUCTURE_VEC pushes all elements, each becomes a local.
 * For mut (mutable): elements extracted individually and wrapped in cells.
 * For globals (scope_depth==0): each element stored with OP_DEF_GLOBAL.
 * rest_name/rest_name_len: if non-NULL, collects remaining elements into a vector.
 */
static void compiler__compile_destructure_vec(
    Compiler* c,
    const char** d_names, uint32_t* d_name_lens,
    const char** d_types, uint32_t* d_type_lens,
    uint32_t d_count,
    const char* rest_name, uint32_t rest_name_len,
    AstNode* value_expr,
    bool is_mutable,
    uint32_t line, uint32_t col)
{
  int has_rest = (rest_name != NULL && rest_name_len > 0);

  /* Validate rest name length */
  if (has_rest && rest_name_len > 7) {
    compiler__error(c, line, col,
                    "variable name exceeds 7-byte inline limit");
    return;
  }

  /* Compute wildcard skip mask and validate binding names */
  uint8_t skip_mask = 0;
  for (uint32_t i = 0; i < d_count; i++) {
    if (d_name_lens[i] == 1 && d_names[i][0] == '_') {
      skip_mask |= (uint8_t)(1u << i);
    } else if (d_name_lens[i] > 7) {
      compiler__error(c, line, col,
                      "variable name exceeds 7-byte inline limit");
      return;
    }
  }

  /* Rest pattern is incompatible with wildcards in skip_mask (simplification) */

  /* Compile RHS — pushes one value (should be a vector) onto stack */
  compiler__compile_node(c, value_expr);

  if (has_rest) {
    /* --- Rest pattern: use OP_DESTRUCTURE_VEC_REST ---
       Pushes d_count individual elements + 1 rest vector onto stack */
    if (c->scope_depth > 0) {
      /* --- Local scope --- */
      if (!is_mutable) {
        /* def: OP_DESTRUCTURE_VEC_REST pushes N elements + rest vector */
        compiler__emit_byte(c, OP_DESTRUCTURE_VEC_REST, line);
        compiler__emit_byte(c, (uint8_t)d_count, line);
        /* Register locals for positional elements */
        for (uint32_t i = 0; i < d_count; i++) {
          if (d_name_lens[i] == 1 && d_names[i][0] == '_') {
            /* Wildcard: still on stack from OP_DESTRUCTURE_VEC_REST,
               but we need a placeholder local to keep stack alignment */
            JaclVal wc_name = jacl_inline_string("", 0);
            compiler__add_local(c, wc_name, line, col);
          } else {
            JaclVal name_val = jacl_inline_string(d_names[i], d_name_lens[i]);
            compiler__add_local(c, name_val, line, col);
            if (d_types && d_types[i]) {
              JaclType t;
              if (compiler__resolve_type(c, d_types[i], d_type_lens[i], &t)) {
                c->locals[c->local_count - 1].type = t;
              }
            }
          }
        }
        /* Register local for rest vector */
        JaclVal rest_val = jacl_inline_string(rest_name, rest_name_len);
        compiler__add_local(c, rest_val, line, col);
        c->locals[c->local_count - 1].type = TYPE_VEC;
      } else {
        /* mut: store vec as temp, extract elements + rest individually */
        JaclVal temp_name = jacl_inline_string("", 0);
        compiler__add_local(c, temp_name, line, col);
        uint32_t vec_slot = c->local_count - 1;

        for (uint32_t i = 0; i < d_count; i++) {
          if (d_name_lens[i] == 1 && d_names[i][0] == '_') continue;
          compiler__emit_byte(c, OP_GET_LOCAL, line);
          compiler__emit_byte(c, (uint8_t)vec_slot, line);
          uint16_t idx = chunk_add_constant(c->chunk, jacl_i32((int32_t)i));
          compiler__emit_byte(c, OP_CONST, line);
          compiler__emit_u16(c, idx, line);
          compiler__emit_byte(c, OP_VEC_GET, line);
          compiler__emit_byte(c, OP_MAKE_CELL, line);
          JaclVal name_val = jacl_inline_string(d_names[i], d_name_lens[i]);
          compiler__add_local(c, name_val, line, col);
          c->locals[c->local_count - 1].is_mutable = true;
          if (d_types && d_types[i]) {
            JaclType t;
            if (compiler__resolve_type(c, d_types[i], d_type_lens[i], &t)) {
              c->locals[c->local_count - 1].type = t;
            }
          }
        }
        /* Rest: use OP_DESTRUCTURE_VEC_REST on a copy to get the rest vector,
           or manually build it with vec-slice. Simpler: push vec, emit
           OP_DESTRUCTURE_VEC_REST, pop the N elements, keep the rest vector. */
        /* Actually, use OP_VEC_SLICE to create rest vector: vec[d_count..] */
        compiler__emit_byte(c, OP_GET_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)vec_slot, line);
        /* Push start index (d_count) */
        uint16_t start_idx = chunk_add_constant(c->chunk, jacl_i32((int32_t)d_count));
        compiler__emit_byte(c, OP_CONST, line);
        compiler__emit_u16(c, start_idx, line);
        /* Push end index — use vec-len for "to end" */
        compiler__emit_byte(c, OP_GET_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)vec_slot, line);
        compiler__emit_byte(c, OP_VEC_LEN, line);
        /* Emit OP_VEC_SLICE */
        compiler__emit_byte(c, OP_VEC_SLICE, line);
        compiler__emit_byte(c, OP_MAKE_CELL, line);
        JaclVal rest_val = jacl_inline_string(rest_name, rest_name_len);
        compiler__add_local(c, rest_val, line, col);
        c->locals[c->local_count - 1].is_mutable = true;
        c->locals[c->local_count - 1].type = TYPE_VEC;
      }
      compiler__emit_byte(c, OP_NIL, line);
    } else {
      /* --- Global scope with rest --- */
      compiler__emit_byte(c, OP_DESTRUCTURE_VEC_REST, line);
      compiler__emit_byte(c, (uint8_t)d_count, line);
      /* Stack: elem0 ... elemN-1 rest_vec (bottom to top)
         Process in reverse: rest first, then elements. */
      /* Define rest global (top of stack) */
      if (is_mutable && c->current_module) {
        compiler__emit_byte(c, OP_BOX, line);
      }
      JaclVal rest_val = jacl_inline_string(rest_name, rest_name_len);
      JaclVal rest_gkey = compiler__global_name_val(c, rest_name, rest_name_len);
      uint16_t rest_idx = chunk_add_constant(c->chunk, rest_gkey);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, rest_idx, line);
      compiler__set_global_arity(c, rest_val, -1);
      if (is_mutable) {
        Compiler* root = c;
        while (root->enclosing) root = root->enclosing;
        for (uint32_t j = 0; j < root->global_arity_count; j++) {
          if (root->global_arities[j].name == rest_val) {
            root->global_arities[j].is_mutable = true;
            break;
          }
        }
      }
      /* Now define positional elements in reverse order */
      for (int i = (int)d_count - 1; i >= 0; i--) {
        compiler__emit_byte(c, OP_POP, line); /* pop nil from previous OP_DEF_GLOBAL */
        if (d_name_lens[i] == 1 && d_names[i][0] == '_') {
          /* Wildcard: just pop the value */
          compiler__emit_byte(c, OP_POP, line);
          compiler__emit_byte(c, OP_NIL, line); /* push nil placeholder */
          continue;
        }
        if (is_mutable && c->current_module) {
          compiler__emit_byte(c, OP_BOX, line);
        }
        JaclVal name_val = jacl_inline_string(d_names[i], d_name_lens[i]);
        JaclVal global_key = compiler__global_name_val(c, d_names[i],
                                                        d_name_lens[i]);
        uint16_t name_idx = chunk_add_constant(c->chunk, global_key);
        compiler__emit_byte(c, OP_DEF_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
        compiler__set_global_arity(c, name_val, -1);
        if (is_mutable) {
          Compiler* root = c;
          while (root->enclosing) root = root->enclosing;
          for (uint32_t j = 0; j < root->global_arity_count; j++) {
            if (root->global_arities[j].name == name_val) {
              root->global_arities[j].is_mutable = true;
              break;
            }
          }
        }
      }
    }
  } else {
    /* --- No rest pattern: original logic --- */

    if (c->scope_depth > 0) {
      /* --- Local scope --- */
      if (!is_mutable) {
        /* def: OP_DESTRUCTURE_VEC pushes non-wildcard elements as locals */
        compiler__emit_byte(c, OP_DESTRUCTURE_VEC, line);
        compiler__emit_byte(c, (uint8_t)d_count, line);
        compiler__emit_byte(c, skip_mask, line);
        for (uint32_t i = 0; i < d_count; i++) {
          if (skip_mask & (1u << i)) continue; /* wildcard: no local */
          JaclVal name_val = jacl_inline_string(d_names[i], d_name_lens[i]);
          compiler__add_local(c, name_val, line, col);
          if (d_types && d_types[i]) {
            JaclType t;
            if (compiler__resolve_type(c, d_types[i], d_type_lens[i], &t)) {
              c->locals[c->local_count - 1].type = t;
            }
          }
        }
      } else {
        /* mut: extract each element individually, wrap in cell.
           Store vec as temporary local to allow repeated access. */
        JaclVal temp_name = jacl_inline_string("", 0);
        compiler__add_local(c, temp_name, line, col);
        uint32_t vec_slot = c->local_count - 1;

        for (uint32_t i = 0; i < d_count; i++) {
          if (skip_mask & (1u << i)) continue; /* wildcard: skip */
          /* Push the vector again from temp local */
          compiler__emit_byte(c, OP_GET_LOCAL, line);
          compiler__emit_byte(c, (uint8_t)vec_slot, line);
          /* Push index */
          uint16_t idx = chunk_add_constant(c->chunk, jacl_i32((int32_t)i));
          compiler__emit_byte(c, OP_CONST, line);
          compiler__emit_u16(c, idx, line);
          /* Extract element */
          compiler__emit_byte(c, OP_VEC_GET, line);
          /* Wrap in cell for mutable binding */
          compiler__emit_byte(c, OP_MAKE_CELL, line);
          /* Register local */
          JaclVal name_val = jacl_inline_string(d_names[i], d_name_lens[i]);
          compiler__add_local(c, name_val, line, col);
          c->locals[c->local_count - 1].is_mutable = true;
          if (d_types && d_types[i]) {
            JaclType t;
            if (compiler__resolve_type(c, d_types[i], d_type_lens[i], &t)) {
              c->locals[c->local_count - 1].type = t;
            }
          }
        }
      }
      /* def/mut returns nil */
      compiler__emit_byte(c, OP_NIL, line);
    } else {
      /* --- Global scope --- */
      compiler__emit_byte(c, OP_DESTRUCTURE_VEC, line);
      compiler__emit_byte(c, (uint8_t)d_count, line);
      compiler__emit_byte(c, skip_mask, line);
      /* Non-skipped elements are on stack (bottom to top).
         Process in reverse so we consume from top of stack.
         Only process non-wildcard positions. */
      int first_non_wildcard = 1;
      for (int i = (int)d_count - 1; i >= 0; i--) {
        if (skip_mask & (1u << i)) continue; /* wildcard: skip */
        if (!first_non_wildcard) {
          /* Pop the nil pushed by previous OP_DEF_GLOBAL */
          compiler__emit_byte(c, OP_POP, line);
        }
        first_non_wildcard = 0;
        if (is_mutable && c->current_module) {
          compiler__emit_byte(c, OP_BOX, line);
        }
        JaclVal name_val = jacl_inline_string(d_names[i], d_name_lens[i]);
        JaclVal global_key = compiler__global_name_val(c, d_names[i],
                                                        d_name_lens[i]);
        uint16_t name_idx = chunk_add_constant(c->chunk, global_key);
        compiler__emit_byte(c, OP_DEF_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
        /* OP_DEF_GLOBAL pops value, pushes nil */
        compiler__set_global_arity(c, name_val, -1);
        if (is_mutable) {
          Compiler* root = c;
          while (root->enclosing) root = root->enclosing;
          for (uint32_t j = 0; j < root->global_arity_count; j++) {
            if (root->global_arities[j].name == name_val) {
              root->global_arities[j].is_mutable = true;
              break;
            }
          }
        }
      }
      /* If all positions were wildcards, push nil as return value */
      if (first_non_wildcard) {
        compiler__emit_byte(c, OP_NIL, line);
      }
    }
  }

  c->last_expr_type = TYPE_NIL;
}

/* -------------------------------------------------------------------------
 * Compile named struct/map destructuring: def {x, y} $expr or mut {x, y} $expr
 *
 * For known struct types: emits OP_STRUCT_GET per field (compile-time resolved).
 * For dyn/map types: emits OP_DESTRUCTURE_NAMED (runtime resolved).
 * For mut: wraps each extracted value in a cell.
 * For globals: defines each extracted value as a global.
 * rest_name/rest_name_len: if non-NULL, collects remaining fields into a map.
 */
static void compiler__compile_destructure_named(
    Compiler* c,
    const char** d_names, uint32_t* d_name_lens,
    const char** d_types, uint32_t* d_type_lens,
    uint32_t d_count,
    const char* rest_name, uint32_t rest_name_len,
    int spread_all,
    AstNode* value_expr,
    bool is_mutable,
    uint32_t line, uint32_t col)
{
  int has_rest = (rest_name != NULL && rest_name_len > 0);

  /* Validate rest name length */
  if (has_rest && rest_name_len > 7) {
    compiler__error(c, line, col,
                    "variable name exceeds 7-byte inline limit");
    return;
  }

  /* Validate binding names */
  for (uint32_t i = 0; i < d_count; i++) {
    if (d_name_lens[i] == 1 && d_names[i][0] == '_') {
      compiler__error(c, line, col,
                      "'_' is meaningless in named destructuring; just omit the field");
      return;
    }
    if (d_name_lens[i] > 7) {
      compiler__error(c, line, col,
                      "variable name exceeds 7-byte inline limit");
      return;
    }
  }

  /* Compile RHS — pushes one value (struct or map) onto stack */
  compiler__compile_node(c, value_expr);
  JaclType rhs_type = c->last_expr_type;
  uint32_t rhs_struct_idx = c->last_struct_idx;

  /* Determine if we can use compile-time struct field resolution */
  int use_struct_path = 0;
  StructTypeDef* sdef = NULL;

  if (rhs_type == TYPE_STRUCT && rhs_struct_idx != UINT32_MAX) {
    StructTypeRegistry* reg = compiler__get_struct_registry(c);
    if (reg && rhs_struct_idx < reg->count) {
      sdef = &reg->defs[rhs_struct_idx];
      use_struct_path = 1;
      /* Validate all field names at compile time */
      for (uint32_t i = 0; i < d_count; i++) {
        uint32_t fi;
        for (fi = 0; fi < sdef->field_count; fi++) {
          if (sdef->fields[fi].name_len == d_name_lens[i] &&
              memcmp(sdef->fields[fi].name, d_names[i], d_name_lens[i]) == 0)
            break;
        }
        if (fi == sdef->field_count) {
          char err_msg[128];
          snprintf(err_msg, sizeof(err_msg),
                   "struct '%.*s' has no field '%.*s'",
                   (int)sdef->name_len, sdef->name,
                   (int)d_name_lens[i], d_names[i]);
          compiler__error(c, line, col, err_msg);
          return;
        }
      }
    }
  }

  /* --- Spread-all expansion: {..} or {x, ..} --- */
  const char* exp_names[STRUCT_MAX_FIELDS];
  uint32_t    exp_name_lens[STRUCT_MAX_FIELDS];
  const char* exp_types[STRUCT_MAX_FIELDS];
  uint32_t    exp_type_lens[STRUCT_MAX_FIELDS];
  uint32_t    exp_count = 0;

  if (spread_all) {
    if (!use_struct_path || !sdef) {
      compiler__error(c, line, col,
                      "spread-all {..} requires a known struct type; got dyn");
      return;
    }
    /* Build expanded field list: explicit fields + all remaining struct fields */

    /* Copy explicit fields first */
    for (uint32_t i = 0; i < d_count; i++) {
      exp_names[exp_count]     = d_names[i];
      exp_name_lens[exp_count] = d_name_lens[i];
      exp_types[exp_count]     = (d_types ? d_types[i] : NULL);
      exp_type_lens[exp_count] = (d_type_lens ? d_type_lens[i] : 0);
      exp_count++;
    }

    /* Add remaining struct fields not already listed */
    for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
      int already_listed = 0;
      for (uint32_t i = 0; i < d_count; i++) {
        if (sdef->fields[fi].name_len == d_name_lens[i] &&
            memcmp(sdef->fields[fi].name, d_names[i], d_name_lens[i]) == 0) {
          already_listed = 1;
          break;
        }
      }
      if (!already_listed) {
        if (sdef->fields[fi].name_len > 7) {
          compiler__error(c, line, col,
                          "variable name exceeds 7-byte inline limit");
          return;
        }
        exp_names[exp_count]     = sdef->fields[fi].name;
        exp_name_lens[exp_count] = sdef->fields[fi].name_len;
        exp_types[exp_count]     = NULL;
        exp_type_lens[exp_count] = 0;
        exp_count++;
      }
    }

    /* Check for same-scope shadowing */
    if (c->scope_depth > 0) {
      for (uint32_t i = 0; i < exp_count; i++) {
        JaclVal check_name = jacl_inline_string(exp_names[i], exp_name_lens[i]);
        for (int j = (int)c->local_count - 1; j >= 0; j--) {
          if (c->locals[j].depth < c->scope_depth) break;
          if (c->locals[j].name == check_name) {
            char err_msg[128];
            snprintf(err_msg, sizeof(err_msg),
                     "spread-all would shadow existing variable '%.*s'",
                     (int)exp_name_lens[i], exp_names[i]);
            compiler__error(c, line, col, err_msg);
            return;
          }
        }
      }
    }

    /* Replace parameters with expanded list */
    d_names     = exp_names;
    d_name_lens = exp_name_lens;
    d_types     = exp_types;
    d_type_lens = exp_type_lens;
    d_count     = exp_count;
  }

  if (c->scope_depth > 0) {
    /* --- Local scope --- */
    /* Store source value in temp local for repeated access */
    JaclVal temp_name = jacl_inline_string("", 0);
    compiler__add_local(c, temp_name, line, col);
    uint32_t src_slot = c->local_count - 1;
    if (rhs_type == TYPE_STRUCT) {
      c->locals[src_slot].type = TYPE_STRUCT;
      c->locals[src_slot].struct_type_idx = rhs_struct_idx;
    }

    if (use_struct_path) {
      /* Struct path: extract each field with compile-time resolved offsets */
      for (uint32_t i = 0; i < d_count; i++) {
        compiler__emit_byte(c, OP_GET_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)src_slot, line);

        uint32_t fi;
        for (fi = 0; fi < sdef->field_count; fi++) {
          if (sdef->fields[fi].name_len == d_name_lens[i] &&
              memcmp(sdef->fields[fi].name, d_names[i], d_name_lens[i]) == 0)
            break;
        }
        compiler__emit_byte(c, OP_STRUCT_GET, line);
        compiler__emit_u16(c, (uint16_t)sdef->fields[fi].offset, line);
        compiler__emit_byte(c, (uint8_t)sdef->fields[fi].type, line);

        if (is_mutable) {
          compiler__emit_byte(c, OP_MAKE_CELL, line);
        }

        JaclVal name_val = jacl_inline_string(d_names[i], d_name_lens[i]);
        compiler__add_local(c, name_val, line, col);
        if (is_mutable)
          c->locals[c->local_count - 1].is_mutable = true;
        if (d_types && d_types[i]) {
          JaclType t;
          if (compiler__resolve_type(c, d_types[i], d_type_lens[i], &t)) {
            c->locals[c->local_count - 1].type = t;
          }
        } else {
          c->locals[c->local_count - 1].type = sdef->fields[fi].type;
          if (sdef->fields[fi].type == TYPE_STRUCT)
            c->locals[c->local_count - 1].struct_type_idx = sdef->fields[fi].struct_type_idx;
        }
      }

      /* Rest: build map from remaining struct fields */
      if (has_rest) {
        compiler__emit_byte(c, OP_GET_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)src_slot, line);
        /* Emit OP_DESTRUCTURE_NAMED_REST with explicit field names to exclude */
        compiler__emit_byte(c, OP_DESTRUCTURE_NAMED_REST, line);
        compiler__emit_byte(c, (uint8_t)d_count, line);
        for (uint32_t i = 0; i < d_count; i++) {
          JaclVal key_val = jacl_inline_string(d_names[i], d_name_lens[i]);
          uint16_t key_idx = chunk_add_constant(c->chunk, key_val);
          compiler__emit_u16(c, key_idx, line);
        }
        if (is_mutable) {
          compiler__emit_byte(c, OP_MAKE_CELL, line);
        }
        JaclVal rest_val = jacl_inline_string(rest_name, rest_name_len);
        compiler__add_local(c, rest_val, line, col);
        if (is_mutable)
          c->locals[c->local_count - 1].is_mutable = true;
        c->locals[c->local_count - 1].type = TYPE_MAP;
      }
    } else {
      /* Dyn/map path: extract each field one at a time with runtime resolution.
         Use OP_DESTRUCTURE_NAMED with count=1 per field so missing-key errors
         are caught, and mutable wrapping works naturally. */
      for (uint32_t i = 0; i < d_count; i++) {
        compiler__emit_byte(c, OP_GET_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)src_slot, line);

        compiler__emit_byte(c, OP_DESTRUCTURE_NAMED, line);
        compiler__emit_byte(c, 1, line);
        JaclVal key_val = jacl_inline_string(d_names[i], d_name_lens[i]);
        uint16_t key_idx = chunk_add_constant(c->chunk, key_val);
        compiler__emit_u16(c, key_idx, line);

        if (is_mutable) {
          compiler__emit_byte(c, OP_MAKE_CELL, line);
        }

        JaclVal name_val = jacl_inline_string(d_names[i], d_name_lens[i]);
        compiler__add_local(c, name_val, line, col);
        if (is_mutable)
          c->locals[c->local_count - 1].is_mutable = true;
        if (d_types && d_types[i]) {
          JaclType t;
          if (compiler__resolve_type(c, d_types[i], d_type_lens[i], &t)) {
            c->locals[c->local_count - 1].type = t;
          }
        }
      }

      /* Rest: build map from remaining fields */
      if (has_rest) {
        compiler__emit_byte(c, OP_GET_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)src_slot, line);
        /* Emit OP_DESTRUCTURE_NAMED_REST with explicit field names to exclude */
        compiler__emit_byte(c, OP_DESTRUCTURE_NAMED_REST, line);
        compiler__emit_byte(c, (uint8_t)d_count, line);
        for (uint32_t i = 0; i < d_count; i++) {
          JaclVal key_val = jacl_inline_string(d_names[i], d_name_lens[i]);
          uint16_t key_idx = chunk_add_constant(c->chunk, key_val);
          compiler__emit_u16(c, key_idx, line);
        }
        if (is_mutable) {
          compiler__emit_byte(c, OP_MAKE_CELL, line);
        }
        JaclVal rest_val = jacl_inline_string(rest_name, rest_name_len);
        compiler__add_local(c, rest_val, line, col);
        if (is_mutable)
          c->locals[c->local_count - 1].is_mutable = true;
        c->locals[c->local_count - 1].type = TYPE_MAP;
      }
    }

    /* def/mut returns nil */
    compiler__emit_byte(c, OP_NIL, line);
  } else {
    /* --- Global scope --- */
    /* NOTE: The RHS value is already on the stack from compile_node above. */
    if (!has_rest) {
      compiler__emit_byte(c, OP_DESTRUCTURE_NAMED, line);
      compiler__emit_byte(c, (uint8_t)d_count, line);
      for (uint32_t i = 0; i < d_count; i++) {
        JaclVal key_val = jacl_inline_string(d_names[i], d_name_lens[i]);
        uint16_t key_idx = chunk_add_constant(c->chunk, key_val);
        compiler__emit_u16(c, key_idx, line);
      }
      /* Elements are on stack: elem0 (bottom) ... elemN-1 (top).
         Process in reverse so we consume from top of stack. */
      for (int i = (int)d_count - 1; i >= 0; i--) {
        if (is_mutable && c->current_module) {
          compiler__emit_byte(c, OP_BOX, line);
        }
        JaclVal name_val = jacl_inline_string(d_names[i], d_name_lens[i]);
        JaclVal global_key = compiler__global_name_val(c, d_names[i],
                                                        d_name_lens[i]);
        uint16_t name_idx = chunk_add_constant(c->chunk, global_key);
        compiler__emit_byte(c, OP_DEF_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
        compiler__set_global_arity(c, name_val, -1);
        if (is_mutable) {
          Compiler* root = c;
          while (root->enclosing) root = root->enclosing;
          for (uint32_t j = 0; j < root->global_arity_count; j++) {
            if (root->global_arities[j].name == name_val) {
              root->global_arities[j].is_mutable = true;
              break;
            }
          }
        }
        if (i > 0) {
          compiler__emit_byte(c, OP_POP, line);
        }
      }
    } else {
      /* Rest path: OP_DESTRUCTURE_NAMED_REST pushes N fields + 1 rest map */
      compiler__emit_byte(c, OP_DESTRUCTURE_NAMED_REST, line);
      compiler__emit_byte(c, (uint8_t)d_count, line);
      for (uint32_t i = 0; i < d_count; i++) {
        JaclVal key_val = jacl_inline_string(d_names[i], d_name_lens[i]);
        uint16_t key_idx = chunk_add_constant(c->chunk, key_val);
        compiler__emit_u16(c, key_idx, line);
      }
      /* Stack: elem0 ... elemN-1 rest_map (bottom to top)
         Process in reverse: rest first, then elements. */
      /* Define rest global (top of stack) */
      if (is_mutable && c->current_module) {
        compiler__emit_byte(c, OP_BOX, line);
      }
      JaclVal rest_val = jacl_inline_string(rest_name, rest_name_len);
      JaclVal rest_gkey = compiler__global_name_val(c, rest_name, rest_name_len);
      uint16_t rest_idx = chunk_add_constant(c->chunk, rest_gkey);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, rest_idx, line);
      compiler__set_global_arity(c, rest_val, -1);
      if (is_mutable) {
        Compiler* root = c;
        while (root->enclosing) root = root->enclosing;
        for (uint32_t j = 0; j < root->global_arity_count; j++) {
          if (root->global_arities[j].name == rest_val) {
            root->global_arities[j].is_mutable = true;
            break;
          }
        }
      }
      /* Now define positional elements in reverse order */
      for (int i = (int)d_count - 1; i >= 0; i--) {
        compiler__emit_byte(c, OP_POP, line); /* pop nil from previous OP_DEF_GLOBAL */
        if (is_mutable && c->current_module) {
          compiler__emit_byte(c, OP_BOX, line);
        }
        JaclVal name_val = jacl_inline_string(d_names[i], d_name_lens[i]);
        JaclVal global_key = compiler__global_name_val(c, d_names[i],
                                                        d_name_lens[i]);
        uint16_t name_idx = chunk_add_constant(c->chunk, global_key);
        compiler__emit_byte(c, OP_DEF_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
        compiler__set_global_arity(c, name_val, -1);
        if (is_mutable) {
          Compiler* root = c;
          while (root->enclosing) root = root->enclosing;
          for (uint32_t j = 0; j < root->global_arity_count; j++) {
            if (root->global_arities[j].name == name_val) {
              root->global_arities[j].is_mutable = true;
              break;
            }
          }
        }
      }
    }
  }

  c->last_expr_type = TYPE_NIL;
}

/* --- Internal: Compile a command invocation --- */

static void compiler__compile_command(Compiler* c, AstNode* node) {
  AstNode* head = node->data.command.head;
  uint32_t argc = node->data.command.arg_count;
  AstNode** args = node->data.command.args;
  uint32_t line = node->start.line;
  uint32_t col  = node->start.column;

  /* Reset expected_type so sub-expressions don't inherit parent context.
     Individual handlers (e.g. typed def) set it explicitly for their RHS. */
  c->expected_type = TYPE_DYN;

  /* Check if any arg is a spread expression */
  int has_spread = 0;
  for (uint32_t i = 0; i < argc; i++) {
    if (args[i]->type == AST_SPREAD) { has_spread = 1; break; }
  }

  /* --- Spread call path: handles both builtins and user procs --- */
  if (has_spread) {
    /* Check for known binary builtins → use OP_FOLD_SPREAD */
    int fold_op = -1;
    if (compiler__head_matches(head, "+", 1))      fold_op = 0;
    else if (compiler__head_matches(head, "*", 1)) fold_op = 2;
    else if (compiler__head_matches(head, "-", 1)) fold_op = 1;
    else if (compiler__head_matches(head, "/", 1)) fold_op = 3;

    if (fold_op >= 0) {
      /* Compile all args (fixed + spread) onto stack */
      uint8_t fixed_args = 0;
      uint8_t num_spreads = 0;
      for (uint32_t i = 0; i < argc; i++) {
        if (args[i]->type == AST_SPREAD) {
          compiler__compile_node(c, args[i]->data.spread.expr);
          compiler__emit_byte(c, OP_SPREAD, line);
          num_spreads++;
        } else {
          compiler__compile_node(c, args[i]);
          fixed_args++;
        }
      }
      compiler__emit_byte(c, OP_FOLD_SPREAD, line);
      compiler__emit_byte(c, (uint8_t)fold_op, line);
      compiler__emit_byte(c, fixed_args, line);
      compiler__emit_byte(c, num_spreads, line);
      c->last_expr_type = TYPE_DYN;
      return;
    }

    /* Generic spread call: resolve head as callable, args, then OP_CALL_SPREAD */
    if (head->type == AST_LIT_STRING) {
      uint32_t name_len = head->data.lit_string.length;
      if (name_len > 7) {
        compiler__error(c, line, col, "command name exceeds 7-byte inline limit");
        return;
      }
      JaclVal name_val = jacl_inline_string(head->data.lit_string.value, name_len);
      int local_slot = compiler__resolve_local(c, name_val);
      if (local_slot != -1) {
        if (c->locals[local_slot].is_mutable) {
          compiler__emit_byte(c, OP_GET_CELL_LOCAL, line);
        } else {
          compiler__emit_byte(c, OP_GET_LOCAL, line);
        }
        compiler__emit_byte(c, (uint8_t)local_slot, line);
      } else {
        JaclVal gkey = compiler__global_name_val(c,
            head->data.lit_string.value, name_len);
        uint16_t name_idx = chunk_add_constant(c->chunk, gkey);
        compiler__emit_byte(c, OP_GET_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
      }
    } else {
      compiler__compile_node(c, head);
    }
    uint8_t fixed_args = 0;
    uint8_t num_spreads = 0;
    for (uint32_t i = 0; i < argc; i++) {
      if (args[i]->type == AST_SPREAD) {
        compiler__compile_node(c, args[i]->data.spread.expr);
        compiler__emit_byte(c, OP_SPREAD, line);
        num_spreads++;
      } else {
        compiler__compile_node(c, args[i]);
        fixed_args++;
      }
    }
    compiler__emit_byte(c, OP_CALL_SPREAD, line);
    compiler__emit_byte(c, fixed_args, line);
    compiler__emit_byte(c, num_spreads, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* Arithmetic builtins */
  if (compiler__head_matches(head, "+", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "+", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_ADD, "add", line, col);
    return;
  }
  if (compiler__head_matches(head, "-", 1)) {
    if (argc == 1) {
      compiler__compile_node(c, args[0]);
      JaclType arg_type = c->last_expr_type;
      if (arg_type == TYPE_U64) {
        compiler__error(c, line, col, "type error: cannot negate u64");
        return;
      }
      if (arg_type == TYPE_I64) {
        compiler__emit_byte(c, OP_NEG_I64, line);
      } else if (arg_type == TYPE_F64) {
        compiler__emit_byte(c, OP_NEG_F64, line);
      } else {
        compiler__emit_byte(c, OP_NEG, line);
      }
      c->last_expr_type = arg_type;
    } else if (argc == 2) {
      compiler__compile_binary(c, args, OP_SUB, "subtract", line, col);
    } else {
      compiler__builtin_arity_error(c, line, col, "-", "1 or 2 arguments", argc);
    }
    return;
  }
  if (compiler__head_matches(head, "*", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "*", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_MUL, "multiply", line, col);
    return;
  }
  if (compiler__head_matches(head, "/", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "/", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_DIV, "divide", line, col);
    return;
  }
  if (compiler__head_matches(head, "%", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "%", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_MOD, "modulo", line, col);
    return;
  }

  /* Comparison builtins */
  if (compiler__head_matches(head, "==", 2)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "==", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_EQ, "compare", line, col);
    return;
  }
  if (compiler__head_matches(head, "<", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "<", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_LT, "compare", line, col);
    return;
  }
  if (compiler__head_matches(head, ">", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, ">", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_GT, "compare", line, col);
    return;
  }
  if (compiler__head_matches(head, "<=", 2)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "<=", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_LE, "compare", line, col);
    return;
  }
  if (compiler__head_matches(head, ">=", 2)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, ">=", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_GE, "compare", line, col);
    return;
  }

  /* Print builtin */
  if (compiler__head_matches(head, "print", 5)) {
    if (argc != 1) { compiler__builtin_arity_error(c, line, col, "print", "1 argument", argc); return; }
    compiler__compile_node(c, args[0]);
    compiler__ensure_boxed(c, line);
    compiler__emit_byte(c, OP_PRINT, line);
    return;
  }

  /* length builtin */
  if (compiler__head_matches(head, "length", 6)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "length", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_STR_LEN, line);
    return;
  }

  /* byte-length builtin */
  if (compiler__head_matches(head, "byte-length", 11)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "byte-length", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_STR_BYTE_LEN, line);
    return;
  }

  /* index builtin */
  if (compiler__head_matches(head, "index", 5)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "index", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_STR_INDEX, line);
    return;
  }

  /* slice builtin (2 or 3 args) */
  if (compiler__head_matches(head, "slice", 5)) {
    if (argc != 2 && argc != 3) {
      compiler__builtin_arity_error(c, line, col, "slice", "2 or 3 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    if (argc == 3) {
      compiler__compile_node(c, args[2]);
    } else {
      /* 2-arg form: nil sentinel means "to end of string" */
      compiler__emit_byte(c, OP_NIL, line);
    }
    compiler__emit_byte(c, OP_STR_SLICE, line);
    return;
  }

  /* concat builtin (variadic: 2+ args) */
  if (compiler__head_matches(head, "concat", 6)) {
    if (argc < 2) {
      compiler__builtin_arity_error(c, line, col, "concat", "at least 2 arguments", argc);
      return;
    }
    /* Compile first two args, emit OP_CONCAT */
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_CONCAT, line);
    /* Each subsequent arg: compile, emit OP_CONCAT (pairwise chain) */
    for (uint32_t i = 2; i < argc; i++) {
      compiler__compile_node(c, args[i]);
      compiler__emit_byte(c, OP_CONCAT, line);
    }
    return;
  }

  /* mut — mutable local binding with cell auto-boxing */
  if (compiler__head_matches(head, "mut", 3)) {
    /* --- Destructuring: [mut [a b c] value] or [mut DESTRUCTURE_VEC value] --- */
    if (argc == 2 && args[0]->type == AST_DESTRUCTURE_VEC) {
      compiler__compile_destructure_vec(
          c,
          args[0]->data.destructure_vec.names,
          args[0]->data.destructure_vec.name_lens,
          args[0]->data.destructure_vec.types,
          args[0]->data.destructure_vec.type_lens,
          args[0]->data.destructure_vec.count,
          args[0]->data.destructure_vec.rest_name,
          args[0]->data.destructure_vec.rest_name_len,
          args[1], true, line, col);
      return;
    }
    if (argc == 2 && args[0]->type == AST_COMMAND) {
      AstNode* pat = args[0];
      uint32_t d_count = 1 + pat->data.command.arg_count;
      if (d_count > 255) {
        compiler__error(c, line, col, "too many bindings in destructuring");
        return;
      }
      const char* d_names[256];
      uint32_t d_name_lens[256];
      if (pat->data.command.head->type != AST_LIT_STRING) {
        compiler__error(c, line, col,
                        "destructuring pattern elements must be names");
        return;
      }
      d_names[0] = pat->data.command.head->data.lit_string.value;
      d_name_lens[0] = pat->data.command.head->data.lit_string.length;
      for (uint32_t i = 0; i < pat->data.command.arg_count; i++) {
        if (pat->data.command.args[i]->type != AST_LIT_STRING) {
          compiler__error(c, line, col,
                          "destructuring pattern elements must be names");
          return;
        }
        d_names[1 + i] = pat->data.command.args[i]->data.lit_string.value;
        d_name_lens[1 + i] = pat->data.command.args[i]->data.lit_string.length;
      }
      compiler__compile_destructure_vec(
          c, d_names, d_name_lens, NULL, NULL, d_count,
          NULL, 0,
          args[1], true, line, col);
      return;
    }
    /* --- Named destructuring: [mut {x, y} value] --- */
    if (argc == 2 && args[0]->type == AST_DESTRUCTURE_NAMED) {
      compiler__compile_destructure_named(
          c,
          args[0]->data.destructure_named.names,
          args[0]->data.destructure_named.name_lens,
          args[0]->data.destructure_named.types,
          args[0]->data.destructure_named.type_lens,
          args[0]->data.destructure_named.count,
          args[0]->data.destructure_named.rest_name,
          args[0]->data.destructure_named.rest_name_len,
          args[0]->data.destructure_named.spread_all,
          args[1], true, line, col);
      return;
    }
    /* --- Named destructuring from block: [mut {x, y} value] (keyword form) --- */
    if (argc == 2 && args[0]->type == AST_BLOCK) {
      AstNode* blk = args[0];
      uint32_t d_count = blk->data.block.count;
      if (d_count == 0 || d_count > 255) {
        compiler__error(c, line, col, "invalid destructuring pattern");
        return;
      }
      const char* d_names_arr[256];
      uint32_t d_name_lens_arr[256];
      int valid = 1;
      for (uint32_t i = 0; i < d_count; i++) {
        AstNode* cmd = blk->data.block.commands[i];
        if (cmd->type == AST_COMMAND && cmd->data.command.arg_count == 0 &&
            cmd->data.command.head->type == AST_LIT_STRING) {
          d_names_arr[i] = cmd->data.command.head->data.lit_string.value;
          d_name_lens_arr[i] = cmd->data.command.head->data.lit_string.length;
        } else {
          valid = 0; break;
        }
      }
      if (valid) {
        compiler__compile_destructure_named(
            c, d_names_arr, d_name_lens_arr, NULL, NULL, d_count,
            NULL, 0, 0,
            args[1], true, line, col);
        return;
      }
    }

    JaclType declared_type = TYPE_DYN;
    uint32_t name_arg_idx  = 0;
    uint32_t value_arg_idx = 1;

    if (argc == 3) {
      /* Typed mut: [mut TYPE name value] */
      if (args[0]->type != AST_LIT_STRING) {
        compiler__error(c, line, col, "mut type must be a keyword");
        return;
      }
      uint32_t first_len = args[0]->data.lit_string.length;
      const char* first_str = args[0]->data.lit_string.value;
      if (!compiler__resolve_type(c, first_str, first_len, &declared_type)) {
        compiler__error(c, line, col, "mut with 3 arguments requires type keyword as first argument");
        return;
      }
      name_arg_idx   = 1;
      value_arg_idx  = 2;
    } else if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "mut", "2 or 3 arguments", argc);
      return;
    }

    if (args[name_arg_idx]->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "mut name must be a string");
      return;
    }
    uint32_t name_len = args[name_arg_idx]->data.lit_string.length;
    if (name_len > 7) {
      compiler__error(c, line, col, "variable name exceeds 7-byte inline limit");
      return;
    }

    /* Compile the value expression with type context */
    c->expected_type = declared_type;
    compiler__compile_node(c, args[value_arg_idx]);
    c->expected_type = TYPE_DYN;
    JaclType rhs_type = c->last_expr_type;

    /* Type check for typed mut */
    if (declared_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != declared_type) {
      char err_msg[128];
      snprintf(err_msg, sizeof(err_msg), "type error: expected %s, got %s",
               type_name(declared_type), type_name(rhs_type));
      compiler__error(c, line, col, err_msg);
      return;
    }

    /* Determine effective type: declared type wins, else infer unboxed/struct from RHS */
    JaclType effective_type;
    if (declared_type != TYPE_DYN) {
      effective_type = declared_type;
    } else if (is_unboxed_type(rhs_type) || rhs_type == TYPE_STRUCT ||
               rhs_type == TYPE_STREAM) {
      effective_type = rhs_type;
    } else {
      effective_type = TYPE_DYN;
    }

    JaclVal name_val = jacl_inline_string(args[name_arg_idx]->data.lit_string.value, name_len);

    if (c->scope_depth > 0) {
      /* Local scope: box unboxed types for cell storage, then wrap in cell */
      if (is_unboxed_type(effective_type)) {
        compiler__emit_byte(c, OP_TO_DYN, line);
        compiler__emit_byte(c, (uint8_t)effective_type, line);
      }
      compiler__emit_byte(c, OP_MAKE_CELL, line);
      compiler__add_local(c, name_val, line, col);
      c->locals[c->local_count - 1].is_mutable = true;
      c->locals[c->local_count - 1].type = effective_type;
      if (effective_type == TYPE_STRUCT)
        c->locals[c->local_count - 1].struct_type_idx = c->last_struct_idx;
      /* mut returns nil */
      compiler__emit_byte(c, OP_NIL, line);
    } else {
      /* Global scope: box unboxed types before storage */
      if (is_unboxed_type(effective_type)) {
        compiler__emit_byte(c, OP_TO_DYN, line);
        compiler__emit_byte(c, (uint8_t)effective_type, line);
      }
      /* In module context, wrap mutable globals in a box so they can be
         shared across module boundaries as live references. */
      if (c->current_module) {
        compiler__emit_byte(c, OP_BOX, line);
      }
      JaclVal global_key = compiler__global_name_val(c,
          args[name_arg_idx]->data.lit_string.value, name_len);
      uint16_t name_idx = chunk_add_constant(c->chunk, global_key);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, name_idx, line);
      /* Record as mutable in global info with type (use plain name for metadata) */
      compiler__set_global_arity(c, name_val, -1);
      /* Walk to the entry we just set and mark mutable + type */
      {
        Compiler* root = c;
        while (root->enclosing) root = root->enclosing;
        for (uint32_t i = 0; i < root->global_arity_count; i++) {
          if (root->global_arities[i].name == name_val) {
            root->global_arities[i].is_mutable = true;
            root->global_arities[i].type = effective_type;
            if (effective_type == TYPE_STRUCT)
              root->global_arities[i].struct_type_idx = c->last_struct_idx;
            break;
          }
        }
      }
    }
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* set — reassign mutable binding */
  if (compiler__head_matches(head, "set", 3)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "set", "2 arguments", argc); return; }
    if (args[0]->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "set first argument must be a name");
      return;
    }
    uint32_t name_len = args[0]->data.lit_string.length;
    if (name_len > 7) {
      compiler__error(c, line, col, "variable name exceeds 7-byte inline limit");
      return;
    }
    JaclVal name_val = jacl_inline_string(args[0]->data.lit_string.value, name_len);
    char err_msg[128];

    /* Resolve local */
    int local_slot = compiler__resolve_local(c, name_val);
    if (local_slot != -1) {
      if (c->locals[local_slot].is_mutable) {
        JaclType target_type = c->locals[local_slot].type;
        c->expected_type = target_type;
        compiler__compile_node(c, args[1]);
        c->expected_type = TYPE_DYN;
        JaclType rhs_type = c->last_expr_type;
        /* Type check */
        if (target_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != target_type) {
          snprintf(err_msg, sizeof(err_msg),
                   "type error: cannot assign %s to %s binding '%.*s'",
                   type_name(rhs_type), type_name(target_type),
                   (int)name_len, args[0]->data.lit_string.value);
          compiler__error(c, line, col, err_msg);
          return;
        }
        if (target_type != TYPE_DYN && rhs_type == TYPE_DYN) {
          snprintf(err_msg, sizeof(err_msg),
                   "type error: cannot assign dyn to %s binding '%.*s'",
                   type_name(target_type),
                   (int)name_len, args[0]->data.lit_string.value);
          compiler__error(c, line, col, err_msg);
          return;
        }
        /* Box unboxed types for cell storage */
        if (is_unboxed_type(target_type)) {
          compiler__emit_byte(c, OP_TO_DYN, line);
          compiler__emit_byte(c, (uint8_t)target_type, line);
        }
        compiler__emit_byte(c, OP_SET_CELL_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)local_slot, line);
        return;
      }
      if (c->locals[local_slot].is_param) {
        snprintf(err_msg, sizeof(err_msg), "cannot mutate parameter '%.*s'",
                 (int)name_len, args[0]->data.lit_string.value);
      } else {
        snprintf(err_msg, sizeof(err_msg), "cannot mutate immutable binding '%.*s'",
                 (int)name_len, args[0]->data.lit_string.value);
      }
      compiler__error(c, line, col, err_msg);
      return;
    }

    /* Resolve upvalue */
    int upvalue_idx = compiler__resolve_upvalue(c, name_val);
    if (upvalue_idx != -1) {
      if (c->upvalues[upvalue_idx].is_mutable) {
        JaclType target_type = c->upvalues[upvalue_idx].type;
        c->expected_type = target_type;
        compiler__compile_node(c, args[1]);
        c->expected_type = TYPE_DYN;
        JaclType rhs_type = c->last_expr_type;
        /* Type check */
        if (target_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != target_type) {
          snprintf(err_msg, sizeof(err_msg),
                   "type error: cannot assign %s to %s binding '%.*s'",
                   type_name(rhs_type), type_name(target_type),
                   (int)name_len, args[0]->data.lit_string.value);
          compiler__error(c, line, col, err_msg);
          return;
        }
        if (target_type != TYPE_DYN && rhs_type == TYPE_DYN) {
          snprintf(err_msg, sizeof(err_msg),
                   "type error: cannot assign dyn to %s binding '%.*s'",
                   type_name(target_type),
                   (int)name_len, args[0]->data.lit_string.value);
          compiler__error(c, line, col, err_msg);
          return;
        }
        /* Box unboxed types for cell storage */
        if (is_unboxed_type(target_type)) {
          compiler__emit_byte(c, OP_TO_DYN, line);
          compiler__emit_byte(c, (uint8_t)target_type, line);
        }
        compiler__emit_byte(c, OP_SET_CELL_UPVALUE, line);
        compiler__emit_byte(c, (uint8_t)upvalue_idx, line);
        return;
      }
      snprintf(err_msg, sizeof(err_msg), "cannot mutate immutable binding '%.*s'",
               (int)name_len, args[0]->data.lit_string.value);
      compiler__error(c, line, col, err_msg);
      return;
    }

    /* Resolve global */
    GlobalArity* set_ga = compiler__find_global(c, name_val);
    if (set_ga) {
      bool global_mutable = set_ga->is_mutable;
      if (global_mutable) {
        JaclType target_type = set_ga->type;
        if (c->current_module) {
          /* In module context, mutable globals are boxes — use reset!
             semantics to update the box in place (shared reference).
             Emit: GET_GLOBAL (push box), compile RHS, OP_RESET */
          JaclVal set_key = compiler__global_name_val(c,
              args[0]->data.lit_string.value, name_len);
          uint16_t name_idx = chunk_add_constant(c->chunk, set_key);
          compiler__emit_byte(c, OP_GET_GLOBAL, line);
          compiler__emit_u16(c, name_idx, line);
          c->expected_type = target_type;
          compiler__compile_node(c, args[1]);
          c->expected_type = TYPE_DYN;
          JaclType rhs_type = c->last_expr_type;
          if (target_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != target_type) {
            snprintf(err_msg, sizeof(err_msg),
                     "type error: cannot assign %s to %s binding '%.*s'",
                     type_name(rhs_type), type_name(target_type),
                     (int)name_len, args[0]->data.lit_string.value);
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (target_type != TYPE_DYN && rhs_type == TYPE_DYN) {
            snprintf(err_msg, sizeof(err_msg),
                     "type error: cannot assign dyn to %s binding '%.*s'",
                     type_name(target_type),
                     (int)name_len, args[0]->data.lit_string.value);
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (is_unboxed_type(target_type)) {
            compiler__emit_byte(c, OP_TO_DYN, line);
            compiler__emit_byte(c, (uint8_t)target_type, line);
          }
          compiler__emit_byte(c, OP_RESET, line);
        } else {
          c->expected_type = target_type;
          compiler__compile_node(c, args[1]);
          c->expected_type = TYPE_DYN;
          JaclType rhs_type = c->last_expr_type;
          /* Type check */
          if (target_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != target_type) {
            snprintf(err_msg, sizeof(err_msg),
                     "type error: cannot assign %s to %s binding '%.*s'",
                     type_name(rhs_type), type_name(target_type),
                     (int)name_len, args[0]->data.lit_string.value);
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (target_type != TYPE_DYN && rhs_type == TYPE_DYN) {
            snprintf(err_msg, sizeof(err_msg),
                     "type error: cannot assign dyn to %s binding '%.*s'",
                     type_name(target_type),
                     (int)name_len, args[0]->data.lit_string.value);
            compiler__error(c, line, col, err_msg);
            return;
          }
          /* Box unboxed types for global storage */
          if (is_unboxed_type(target_type)) {
            compiler__emit_byte(c, OP_TO_DYN, line);
            compiler__emit_byte(c, (uint8_t)target_type, line);
          }
          uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
          compiler__emit_byte(c, OP_SET_GLOBAL, line);
          compiler__emit_u16(c, name_idx, line);
        }
        return;
      }
      snprintf(err_msg, sizeof(err_msg), "cannot mutate immutable binding '%.*s'",
               (int)name_len, args[0]->data.lit_string.value);
      compiler__error(c, line, col, err_msg);
      return;
    }

    /* Not found anywhere */
    snprintf(err_msg, sizeof(err_msg), "undefined variable '%.*s'",
             (int)name_len, args[0]->data.lit_string.value);
    compiler__error(c, line, col, err_msg);
    return;
  }

  /* def builtin — supports [def name value] and [def TYPE name value]
     and [def [a b c] value] for vector destructuring */
  if (compiler__head_matches(head, "def", 3)) {
    /* --- Destructuring: [def [a b c] value] or [def DESTRUCTURE_VEC value] --- */
    if (argc == 2 && args[0]->type == AST_DESTRUCTURE_VEC) {
      compiler__compile_destructure_vec(
          c,
          args[0]->data.destructure_vec.names,
          args[0]->data.destructure_vec.name_lens,
          args[0]->data.destructure_vec.types,
          args[0]->data.destructure_vec.type_lens,
          args[0]->data.destructure_vec.count,
          args[0]->data.destructure_vec.rest_name,
          args[0]->data.destructure_vec.rest_name_len,
          args[1], false, line, col);
      return;
    }
    if (argc == 2 && args[0]->type == AST_COMMAND) {
      /* keyword form: def [a b c] expr — convert AST_COMMAND to name arrays */
      AstNode* pat = args[0];
      uint32_t d_count = 1 + pat->data.command.arg_count; /* head + args */
      if (d_count > 255) {
        compiler__error(c, line, col, "too many bindings in destructuring");
        return;
      }
      const char* d_names[256];
      uint32_t d_name_lens[256];
      /* head is first name */
      if (pat->data.command.head->type != AST_LIT_STRING) {
        compiler__error(c, line, col,
                        "destructuring pattern elements must be names");
        return;
      }
      d_names[0] = pat->data.command.head->data.lit_string.value;
      d_name_lens[0] = pat->data.command.head->data.lit_string.length;
      for (uint32_t i = 0; i < pat->data.command.arg_count; i++) {
        if (pat->data.command.args[i]->type != AST_LIT_STRING) {
          compiler__error(c, line, col,
                          "destructuring pattern elements must be names");
          return;
        }
        d_names[1 + i] = pat->data.command.args[i]->data.lit_string.value;
        d_name_lens[1 + i] = pat->data.command.args[i]->data.lit_string.length;
      }
      compiler__compile_destructure_vec(
          c, d_names, d_name_lens, NULL, NULL, d_count,
          NULL, 0,
          args[1], false, line, col);
      return;
    }
    /* --- Named destructuring: [def {x, y} value] --- */
    if (argc == 2 && args[0]->type == AST_DESTRUCTURE_NAMED) {
      compiler__compile_destructure_named(
          c,
          args[0]->data.destructure_named.names,
          args[0]->data.destructure_named.name_lens,
          args[0]->data.destructure_named.types,
          args[0]->data.destructure_named.type_lens,
          args[0]->data.destructure_named.count,
          args[0]->data.destructure_named.rest_name,
          args[0]->data.destructure_named.rest_name_len,
          args[0]->data.destructure_named.spread_all,
          args[1], false, line, col);
      return;
    }
    /* --- Named destructuring from block: [def {x, y} value] (keyword form) --- */
    if (argc == 2 && args[0]->type == AST_BLOCK) {
      AstNode* blk = args[0];
      uint32_t d_count = blk->data.block.count;
      if (d_count == 0 || d_count > 255) {
        compiler__error(c, line, col, "invalid destructuring pattern");
        return;
      }
      const char* d_names_arr[256];
      uint32_t d_name_lens_arr[256];
      int valid = 1;
      for (uint32_t i = 0; i < d_count; i++) {
        AstNode* cmd = blk->data.block.commands[i];
        if (cmd->type == AST_COMMAND && cmd->data.command.arg_count == 0 &&
            cmd->data.command.head->type == AST_LIT_STRING) {
          d_names_arr[i] = cmd->data.command.head->data.lit_string.value;
          d_name_lens_arr[i] = cmd->data.command.head->data.lit_string.length;
        } else {
          valid = 0; break;
        }
      }
      if (valid) {
        compiler__compile_destructure_named(
            c, d_names_arr, d_name_lens_arr, NULL, NULL, d_count,
            NULL, 0, 0,
            args[1], false, line, col);
        return;
      }
    }

    JaclType declared_type = TYPE_DYN;
    uint32_t name_arg_idx  = 0;
    uint32_t value_arg_idx = 1;

    if (argc == 3) {
      /* Typed def: [def TYPE name value] */
      if (args[0]->type != AST_LIT_STRING) {
        compiler__error(c, line, col, "def type must be a keyword");
        return;
      }
      uint32_t first_len = args[0]->data.lit_string.length;
      const char* first_str = args[0]->data.lit_string.value;
      if (!compiler__resolve_type(c, first_str, first_len, &declared_type)) {
        compiler__error(c, line, col, "def with 3 arguments requires type keyword as first argument");
        return;
      }
      name_arg_idx   = 1;
      value_arg_idx  = 2;
    } else if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "def", "2 or 3 arguments", argc);
      return;
    }

    if (args[name_arg_idx]->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "def name must be a string");
      return;
    }
    uint32_t name_len = args[name_arg_idx]->data.lit_string.length;
    if (name_len > 7) {
      compiler__error(c, line, col, "variable name exceeds 7-byte inline limit");
      return;
    }

    /* Compile the value expression with type context */
    c->expected_type = declared_type;
    compiler__compile_node(c, args[value_arg_idx]);
    c->expected_type = TYPE_DYN;
    JaclType rhs_type = c->last_expr_type;

    /* Type check for typed def */
    if (declared_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != declared_type) {
      char err_msg[128];
      snprintf(err_msg, sizeof(err_msg), "type error: expected %s, got %s",
               type_name(declared_type), type_name(rhs_type));
      compiler__error(c, line, col, err_msg);
      return;
    }

    JaclVal name_val = jacl_inline_string(args[name_arg_idx]->data.lit_string.value, name_len);

    /* Determine effective type: declared type wins, else infer unboxed/struct from RHS */
    JaclType effective_type;
    if (declared_type != TYPE_DYN) {
      effective_type = declared_type;
    } else if (is_unboxed_type(rhs_type) || rhs_type == TYPE_STRUCT ||
               rhs_type == TYPE_STREAM) {
      /* Infer unboxed types, struct types, and stream types from RHS */
      effective_type = rhs_type;
    } else {
      effective_type = TYPE_DYN;
    }

    int16_t rhs_arity = compiler__node_known_arity(c, args[value_arg_idx]);

    if (c->scope_depth > 0) {
      /* Local variable: value is on stack as the local slot */
      compiler__add_local(c, name_val, line, col);
      c->locals[c->local_count - 1].known_arity = rhs_arity;
      c->locals[c->local_count - 1].type = effective_type;
      if (effective_type == TYPE_STRUCT)
        c->locals[c->local_count - 1].struct_type_idx = c->last_struct_idx;
      /* def returns nil */
      compiler__emit_byte(c, OP_NIL, line);
    } else {
      /* Global variable: box unboxed types before storage */
      if (is_unboxed_type(effective_type)) {
        compiler__emit_byte(c, OP_TO_DYN, line);
        compiler__emit_byte(c, (uint8_t)effective_type, line);
      }
      JaclVal def_key = compiler__global_name_val(c,
          args[name_arg_idx]->data.lit_string.value, name_len);
      uint16_t name_idx = chunk_add_constant(c->chunk, def_key);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, name_idx, line);
      /* Register global with arity and type (use plain name for metadata) */
      compiler__set_global_arity(c, name_val, rhs_arity);
      {
        Compiler* root = c;
        while (root->enclosing) root = root->enclosing;
        for (uint32_t i = 0; i < root->global_arity_count; i++) {
          if (root->global_arities[i].name == name_val) {
            root->global_arities[i].type = effective_type;
            if (effective_type == TYPE_STRUCT)
              root->global_arities[i].struct_type_idx = c->last_struct_idx;
            break;
          }
        }
      }
    }
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* proc definition */
  if (compiler__head_matches(head, "proc", 4)) {
    /* Disambiguate: 4 args + first is type keyword → has return type.
       3 args → no return type (existing). */
    JaclType proc_return_type = TYPE_DYN;
    uint32_t name_arg_idx, params_arg_idx, body_arg_idx;

    if (argc == 4) {
      /* [proc TYPE name params body] */
      if (args[0]->type != AST_LIT_STRING ||
          !compiler__resolve_type(c, args[0]->data.lit_string.value,
                                  args[0]->data.lit_string.length,
                                  &proc_return_type)) {
        compiler__error(c, line, col,
            "proc with 4 arguments requires type keyword as first argument");
        return;
      }
      name_arg_idx   = 1;
      params_arg_idx = 2;
      body_arg_idx   = 3;
    } else if (argc == 3) {
      name_arg_idx   = 0;
      params_arg_idx = 1;
      body_arg_idx   = 2;
    } else {
      compiler__builtin_arity_error(c, line, col, "proc", "3 or 4 arguments", argc);
      return;
    }

    if (args[name_arg_idx]->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "proc name must be a string");
      return;
    }
    if (args[params_arg_idx]->type != AST_COMMAND) {
      compiler__error(c, line, col, "proc params must be a bracketed list");
      return;
    }
    if (args[body_arg_idx]->type != AST_BLOCK) {
      compiler__error(c, line, col, "proc body must be a block");
      return;
    }

    /* Get proc name */
    const char* proc_name = args[name_arg_idx]->data.lit_string.value;
    uint32_t proc_name_len = args[name_arg_idx]->data.lit_string.length;
    if (proc_name_len > 7) {
      compiler__error(c, line, col, "proc name exceeds 7-byte inline limit");
      return;
    }

    /* Parse parameters with optional types from command node.
       Walk flat list: head + children. Type keywords interleaved:
       [i64 a i64 b] → a:i64, b:i64. [a b] → a:dyn, b:dyn. */
    AstNode* params_node = args[params_arg_idx];
    AstNode* params_head = params_node->data.command.head;

    /* Build flat element list from head + args */
    uint32_t flat_count = 0;
    AstNode* flat_elems[COMPILER_MAX_PROC_PARAMS * 2 + 2]; /* generous */

    if (params_head->data.lit_string.length > 0) {
      flat_elems[flat_count++] = params_head;
      for (uint32_t i = 0; i < params_node->data.command.arg_count; i++) {
        if (flat_count < sizeof(flat_elems)/sizeof(flat_elems[0]))
          flat_elems[flat_count++] = params_node->data.command.args[i];
      }
    }

    /* Walk flat list to extract (type, name) pairs */
    JaclVal param_names_arr[COMPILER_MAX_PROC_PARAMS];
    JaclType param_types_arr[COMPILER_MAX_PROC_PARAMS];
    uint8_t param_count = 0;
    bool is_variadic = false;

    for (uint32_t fi = 0; fi < flat_count; fi++) {
      AstNode* elem = flat_elems[fi];
      if (elem->type != AST_LIT_STRING) {
        compiler__error(c, line, col, "proc parameter must be a name or type keyword");
        return;
      }
      const char* word = elem->data.lit_string.value;
      uint32_t wlen = elem->data.lit_string.length;

      /* Check for variadic marker & or .. */
      if ((wlen == 1 && word[0] == '&') ||
          (wlen == 2 && word[0] == '.' && word[1] == '.')) {
        if (is_variadic) {
          compiler__error(c, line, col, "multiple rest parameters not allowed");
          return;
        }
        is_variadic = true;
        fi++;
        if (fi >= flat_count) {
          compiler__error(c, line, col, "expected parameter name after rest marker");
          return;
        }
        /* Check for optional type annotation: ..type name */
        elem = flat_elems[fi];
        JaclType rest_type = TYPE_DYN;
        if (elem->type == AST_LIT_STRING && fi + 1 < flat_count) {
          JaclType maybe_type;
          if (compiler__resolve_type(c, elem->data.lit_string.value,
                                     elem->data.lit_string.length, &maybe_type)) {
            rest_type = maybe_type;
            fi++;
            elem = flat_elems[fi];
          }
        }
        if (elem->type != AST_LIT_STRING || elem->data.lit_string.length > 7) {
          compiler__error(c, line, col, "proc parameter name invalid");
          return;
        }
        /* rest param must be last */
        if (fi != flat_count - 1) {
          compiler__error(c, line, col, "rest parameter must be last");
          return;
        }
        if (param_count >= COMPILER_MAX_PROC_PARAMS) {
          compiler__error(c, line, col, "too many proc parameters");
          return;
        }
        param_names_arr[param_count] = jacl_inline_string(
            elem->data.lit_string.value, elem->data.lit_string.length);
        param_types_arr[param_count] = rest_type;
        param_count++;
        continue;
      }

      /* Check if current element is a type keyword (including struct names) */
      JaclType ptype;
      if (compiler__resolve_type(c, word, wlen, &ptype) && fi + 1 < flat_count) {
        /* Type annotation followed by param name → typed param */
        fi++;
        elem = flat_elems[fi];
        if (elem->type != AST_LIT_STRING || elem->data.lit_string.length > 7) {
          compiler__error(c, line, col, "proc parameter name invalid");
          return;
        }
        if (param_count >= COMPILER_MAX_PROC_PARAMS) {
          compiler__error(c, line, col, "too many proc parameters");
          return;
        }
        param_names_arr[param_count] = jacl_inline_string(
            elem->data.lit_string.value, elem->data.lit_string.length);
        param_types_arr[param_count] = ptype;
        param_count++;
      } else {
        /* Untyped param name */
        if (wlen > 7) {
          compiler__error(c, line, col, "proc parameter name invalid");
          return;
        }
        if (param_count >= COMPILER_MAX_PROC_PARAMS) {
          compiler__error(c, line, col, "too many proc parameters");
          return;
        }
        param_names_arr[param_count] = jacl_inline_string(word, wlen);
        param_types_arr[param_count] = TYPE_DYN;
        param_count++;
      }
    }

    uint8_t min_args = is_variadic ? (uint8_t)(param_count - 1) : param_count;

    /* Check if this proc needs CPS transformation */
    JaclVal name_val_check = jacl_inline_string(proc_name, proc_name_len);
    bool proc_suspends_early = false;
    if (c->suspension_map) {
      proc_suspends_early = suspension_map_lookup(c->suspension_map, name_val_check);
    }

    /* For CPS-transformed procs, add __k as hidden last parameter */
    uint8_t user_param_count = param_count; /* original param count for callers */
    if (proc_suspends_early) {
      if (param_count >= COMPILER_MAX_PROC_PARAMS) {
        compiler__error(c, line, col, "too many proc parameters for CPS transform");
        return;
      }
      param_names_arr[param_count] = jacl_inline_string("__k", 3);
      param_types_arr[param_count] = TYPE_DYN;
      param_count++;
    }

    /* Allocate closure */
    JaclClosure* closure = (JaclClosure*)arena_alloc(c->arena, sizeof(JaclClosure));
    chunk_init(&closure->chunk, c->arena);
    closure->param_count  = param_count;
    closure->upvalue_count = 0;
    closure->upvalues     = NULL;
    /* Copy proc name to a null-terminated arena string */
    char* name_copy = (char*)arena_alloc(c->arena, proc_name_len + 1);
    memcpy(name_copy, proc_name, proc_name_len);
    name_copy[proc_name_len] = '\0';
    closure->name         = name_copy;
    closure->min_args     = min_args;
    closure->variadic     = is_variadic;
    closure->pinned       = false;
    closure->pin_worker_id = -1;
    closure->is_generator  = false; /* set after body compilation */

    /* Allocate and fill param_names from parsed array */
    if (param_count > 0) {
      closure->param_names = (JaclVal*)arena_alloc(c->arena,
                                sizeof(JaclVal) * param_count);
      memcpy(closure->param_names, param_names_arr,
             sizeof(JaclVal) * param_count);
    } else {
      closure->param_names = NULL;
    }

    /* Create body compiler with function-level scope */
    Compiler body_compiler;
    compiler__init(&body_compiler, &closure->chunk, c->arena, c->intern_table, c->heap);
    body_compiler.scope_depth    = 1;
    body_compiler.enclosing      = c;
    body_compiler.return_type    = proc_return_type;
    body_compiler.suspension_map = c->suspension_map;

    /* Copy global arities to body compiler for suspension lookups */
    {
      Compiler* root = c;
      while (root->enclosing) root = root->enclosing;
      memcpy(body_compiler.global_arities, root->global_arities,
             sizeof(GlobalArity) * root->global_arity_count);
      body_compiler.global_arity_count = root->global_arity_count;
    }

    /* Add params as locals in body compiler (slots 0..N-1) with types */
    for (uint8_t i = 0; i < param_count; i++) {
      compiler__add_local(&body_compiler, closure->param_names[i], line, col);
      body_compiler.locals[body_compiler.local_count - 1].is_param = true;
      body_compiler.locals[body_compiler.local_count - 1].type = param_types_arr[i];
    }

    /* For variadic procs, emit OP_COLLECT_VARIADIC as first instruction */
    if (is_variadic) {
      compiler__emit_byte(&body_compiler, OP_COLLECT_VARIADIC, line);
      compiler__emit_byte(&body_compiler, min_args, line);
    }

    if (proc_suspends_early) {
      /* CPS-transformed proc: compile body with CPS */
      body_compiler.is_cps = true;

      AstNode* body_block = args[body_arg_idx];
      uint32_t stmt_count = body_block->data.block.count;
      AstNode** stmts = body_block->data.block.commands;

      if (stmt_count == 0) {
        /* Empty body: tail-call __k(nil) */
        compiler__emit_get_k(&body_compiler, line);
        compiler__emit_byte(&body_compiler, OP_NIL, line);
        compiler__emit_byte(&body_compiler, OP_TAIL_CALL, line);
        compiler__emit_byte(&body_compiler, 1, line);
      } else {
        compiler__compile_cps_stmts(&body_compiler, stmts, stmt_count, line);
      }

      /* CPS procs don't use OP_RETURN for the result — they call __k.
         Emit OP_RETURN after the CPS chain as cleanup (callee returns to caller
         after __k call completes). */
      compiler__emit_byte(&body_compiler, OP_RETURN, line);
    } else {
      /* Normal non-suspending proc */
      compiler__compile_block_expr(&body_compiler, args[body_arg_idx]);

      /* Return type checking: body's last expression type must match declared */
      if (proc_return_type != TYPE_DYN) {
        JaclType body_type = body_compiler.last_expr_type;
        if (body_type != TYPE_DYN && body_type != proc_return_type) {
          char err_msg[128];
          snprintf(err_msg, sizeof(err_msg),
                   "type error: proc %.*s declared return type %s, but body returns %s",
                   (int)proc_name_len, proc_name,
                   type_name(proc_return_type), type_name(body_type));
          compiler__error(c, line, col, err_msg);
        }
      }

      /* Emit implicit return */
      compiler__emit_byte(&body_compiler, OP_RETURN, line);
    }

    /* Propagate errors from body compiler */
    c->error_count += body_compiler.error_count;
    if (!c->first_error && body_compiler.first_error) {
      c->first_error = body_compiler.first_error;
    }

    /* Set upvalue count and generator flag on the closure */
    closure->upvalue_count = (uint8_t)body_compiler.upvalue_count;
    closure->is_generator  = body_compiler.has_yield;

    /* Generators return streams — override return type for type tracking */
    if (body_compiler.has_yield && proc_return_type == TYPE_DYN) {
      proc_return_type = TYPE_STREAM;
    }

    /* Store closure in parent's constant pool */
    uint16_t closure_idx = chunk_add_constant(c->chunk, jacl_closure(closure));

    /* Emit OP_CLOSURE to push the closure value, followed by upvalue descriptors */
    compiler__emit_byte(c, OP_CLOSURE, line);
    compiler__emit_u16(c, closure_idx, line);
    for (uint32_t i = 0; i < body_compiler.upvalue_count; i++) {
      compiler__emit_byte(c, body_compiler.upvalues[i].is_local, line);
      compiler__emit_byte(c, body_compiler.upvalues[i].index, line);
    }

    /* Anonymous lambda (empty name): closure is already on stack, done */
    if (proc_name_len == 0) {
      c->last_expr_type = TYPE_CLOSURE;
      return;
    }

    /* Arena-allocate param_types array for binding */
    JaclType* stored_param_types = NULL;
    if (param_count > 0) {
      stored_param_types = (JaclType*)arena_alloc(c->arena,
                              sizeof(JaclType) * param_count);
      memcpy(stored_param_types, param_types_arr, sizeof(JaclType) * param_count);
    }

    /* Look up suspension status from analysis map */
    JaclVal name_val = jacl_inline_string(proc_name, proc_name_len);
    bool proc_suspends = false;
    if (c->suspension_map) {
      proc_suspends = suspension_map_lookup(c->suspension_map, name_val);
    }

    /* Determine if this proc transitively captures mutable state (US-003).
       A proc captures_mutable if any of its upvalues is_mutable or
       captures_mutable (transitive through nested closures). */
    bool proc_captures_mutable = false;
    for (uint32_t i = 0; i < body_compiler.upvalue_count; i++) {
      if (body_compiler.upvalues[i].is_mutable ||
          body_compiler.upvalues[i].captures_mutable) {
        proc_captures_mutable = true;
        break;
      }
    }

    /* Bind the name — use user_param_count (excludes hidden __k) for arity checks */
    if (c->scope_depth > 0 && !c->force_global_procs) {
      /* Local scope: closure is on stack as local */
      compiler__add_local(c, name_val, line, col);
      c->locals[c->local_count - 1].known_arity = is_variadic ? -1 : (int16_t)user_param_count;
      c->locals[c->local_count - 1].return_type = proc_return_type;
      c->locals[c->local_count - 1].param_types = stored_param_types;
      c->locals[c->local_count - 1].suspends    = proc_suspends;
      c->locals[c->local_count - 1].captures_mutable = proc_captures_mutable;
      /* proc returns the closure value (enables make-adder pattern) */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, (uint8_t)(c->local_count - 1), line);
    } else {
      /* Global scope */
      JaclVal proc_key = compiler__global_name_val(c, proc_name, proc_name_len);
      uint16_t name_idx = chunk_add_constant(c->chunk, proc_key);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, name_idx, line);
      compiler__set_global_arity(c, name_val, is_variadic ? -1 : (int16_t)user_param_count);
      /* Store param types, return type, and suspension in GlobalArity */
      {
        GlobalArity* ga = compiler__find_global(c, name_val);
        if (ga) {
          ga->return_type = proc_return_type;
          ga->suspends    = proc_suspends;
          ga->captures_mutable = proc_captures_mutable;
          for (uint8_t i = 0; i < user_param_count && i < COMPILER_MAX_PROC_PARAMS; i++) {
            ga->param_types[i] = param_types_arr[i];
          }
        }
      }
    }
    c->last_expr_type = TYPE_CLOSURE;
    return;
  }

  /* if conditional */
  if (compiler__head_matches(head, "if", 2)) {
    if (argc != 2 && argc != 3) {
      compiler__builtin_arity_error(c, line, col, "if", "2 or 3 arguments", argc);
      return;
    }
    if (args[1]->type != AST_BLOCK) {
      compiler__error(c, line, col, "if then-branch must be a block");
      return;
    }
    if (argc == 3 && args[2]->type != AST_BLOCK) {
      compiler__error(c, line, col, "if else-branch must be a block");
      return;
    }

    /* Compile condition */
    compiler__compile_node(c, args[0]);

    /* OP_JUMP_IF_FALSE over then-body */
    uint32_t then_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);

    /* Compile then-body as expression */
    compiler__compile_block_expr(c, args[1]);

    /* OP_JUMP over else-body */
    uint32_t else_jump = compiler__emit_jump(c, OP_JUMP, line);

    /* Patch JUMP_IF_FALSE to here */
    compiler__patch_jump(c, then_jump);

    if (argc == 3) {
      /* Compile else-body as expression */
      compiler__compile_block_expr(c, args[2]);
    } else {
      /* No else: push nil */
      compiler__emit_byte(c, OP_NIL, line);
    }

    /* Patch JUMP to here */
    compiler__patch_jump(c, else_jump);
    return;
  }

  /* while loop */
  if (compiler__head_matches(head, "while", 5)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "while", "2 arguments", argc);
      return;
    }
    if (args[1]->type != AST_BLOCK) {
      compiler__error(c, line, col, "while body must be a block");
      return;
    }

    /* Push loop context for break/continue */
    if (c->loop_depth >= COMPILER_LOOP_DEPTH_MAX) {
      compiler__error(c, line, col, "too many nested loops");
      return;
    }
    LoopContext* lctx = &c->loop_stack[c->loop_depth++];
    lctx->break_patch_count = 0;
    lctx->continue_patch_count = 0;
    lctx->local_count_at_loop = c->local_count;
    lctx->is_for_loop = false;

    /* Loop-start label */
    uint32_t loop_start = c->chunk->code_count;
    lctx->loop_start = loop_start;

    /* Compile condition */
    compiler__compile_node(c, args[0]);

    /* OP_JUMP_IF_FALSE to exit */
    uint32_t exit_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);

    /* Scope for while body: ensures locals (def) are cleaned up each iteration */
    compiler__begin_scope(c);
    uint32_t body_count = args[1]->data.block.count;
    for (uint32_t i = 0; i < body_count; i++) {
      compiler__compile_node(c, args[1]->data.block.commands[i]);
      compiler__emit_check_error(c, line);
    }
    compiler__end_scope(c, line);

    /* OP_LOOP back to loop_start */
    compiler__emit_byte(c, OP_LOOP, line);
    uint32_t offset = c->chunk->code_count - loop_start + 2;
    compiler__emit_byte(c, (uint8_t)((offset >> 8) & 0xFF), line);
    compiler__emit_byte(c, (uint8_t)(offset & 0xFF), line);

    /* Patch exit jump to here */
    compiler__patch_jump(c, exit_jump);

    /* Normal exit: while returns nil */
    compiler__emit_byte(c, OP_NIL, line);

    /* Jump over the break landing zone */
    uint32_t skip_break = compiler__emit_jump(c, OP_JUMP, line);

    /* Patch all break jumps to here (break value already on stack) */
    for (uint32_t i = 0; i < lctx->break_patch_count; i++) {
      compiler__patch_jump(c, lctx->break_patches[i]);
    }

    /* Patch skip_break to here */
    compiler__patch_jump(c, skip_break);

    /* Pop loop context */
    c->loop_depth--;
    return;
  }

  /* for — collection-based iteration with inlined body
     Forms:
       [for {init; cond; step} { body }]   — C-style counted loop
       [for $collection { body }]           — implicit $it binding
       [for $collection name { body }]      — explicit binding
       [for $collection $callback]           — HOF via OP_EACH
  */
  if (compiler__head_matches(head, "for", 3)) {
    /* C-style for: [for {init; cond; step} { body }] */
    if (argc == 2 && args[0]->type == AST_BLOCK && args[1]->type == AST_BLOCK) {
      AstNode* ctrl       = args[0];
      AstNode* body_block = args[1];

      if (ctrl->data.block.count != 3) {
        compiler__error(c, line, col,
            "C-style for control block must have exactly 3 parts: init; cond; step");
        return;
      }

      AstNode* init_node = ctrl->data.block.commands[0];
      AstNode* cond_node = ctrl->data.block.commands[1];
      AstNode* step_node = ctrl->data.block.commands[2];

      if (c->loop_depth >= COMPILER_LOOP_DEPTH_MAX) {
        compiler__error(c, line, col, "too many nested loops");
        return;
      }

      /* Begin scope for init variable(s) — not visible after loop */
      compiler__begin_scope(c);
      uint32_t saved_local_count = c->local_count;

      /* Compile init (runs once before the loop) */
      compiler__compile_node(c, init_node);
      compiler__emit_check_error(c, line);

      /* Push loop context — is_for_loop=true for forward-jump continue */
      LoopContext* lctx = &c->loop_stack[c->loop_depth++];
      lctx->break_patch_count    = 0;
      lctx->continue_patch_count = 0;
      lctx->local_count_at_loop  = saved_local_count;
      lctx->is_for_loop          = true;

      /* Loop start: condition check */
      uint32_t loop_start = c->chunk->code_count;
      lctx->loop_start = loop_start;

      /* Compile condition */
      compiler__compile_node(c, cond_node);

      /* JUMP_IF_FALSE → exit */
      uint32_t exit_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);

      /* Compile body statements inline */
      uint32_t body_count = body_block->data.block.count;
      for (uint32_t i = 0; i < body_count; i++) {
        compiler__compile_node(c, body_block->data.block.commands[i]);
        compiler__emit_check_error(c, line);
      }

      /* Continue target: patch all continue forward jumps here */
      for (uint32_t i = 0; i < lctx->continue_patch_count; i++) {
        compiler__patch_jump(c, lctx->continue_patches[i]);
      }

      /* Compile step expression */
      compiler__compile_node(c, step_node);
      compiler__emit_check_error(c, line);

      /* Loop back to condition */
      compiler__emit_byte(c, OP_LOOP, line);
      uint32_t back_offset = c->chunk->code_count - loop_start + 2;
      compiler__emit_byte(c, (uint8_t)((back_offset >> 8) & 0xFF), line);
      compiler__emit_byte(c, (uint8_t)(back_offset & 0xFF), line);

      /* Exit: patch conditional jump */
      compiler__patch_jump(c, exit_jump);

      /* End scope: pop init variable(s) */
      compiler__end_scope(c, line);

      /* Normal exit: push nil */
      compiler__emit_byte(c, OP_NIL, line);

      /* Jump over break landing zone */
      uint32_t skip_break = compiler__emit_jump(c, OP_JUMP, line);

      /* Break landing zone (break value already on stack via OP_CLOSE_LOOP) */
      for (uint32_t i = 0; i < lctx->break_patch_count; i++) {
        compiler__patch_jump(c, lctx->break_patches[i]);
      }

      /* Convergence point */
      compiler__patch_jump(c, skip_break);

      /* Pop loop context */
      c->loop_depth--;
      return;
    }

    /* Detect HOF callback form: [for $collection $callback] */
    if (argc == 2 && args[1]->type == AST_VAR_REF) {
      compiler__compile_hof_builtin(c, "each", args, argc, OP_EACH, line, col);
      return;
    }

    /* Determine binding name and body block */
    const char* bind_name = "it";
    uint32_t bind_name_len = 2;
    AstNode* body_block = NULL;

    if (argc == 2 && args[1]->type == AST_BLOCK) {
      /* [for $collection { body }] — implicit $it */
      body_block = args[1];
    } else if (argc == 3 && args[1]->type == AST_LIT_STRING &&
               args[2]->type == AST_BLOCK) {
      /* [for $collection name { body }] — explicit binding */
      bind_name = args[1]->data.lit_string.value;
      bind_name_len = args[1]->data.lit_string.length;
      body_block = args[2];
    } else if (argc == 2 && args[1]->type == AST_COMMAND) {
      /* [for $collection [\ body]] — lambda callback via OP_EACH */
      compiler__compile_hof_builtin(c, "each", args, argc, OP_EACH, line, col);
      return;
    } else {
      compiler__error(c, line, col,
          "for requires: $collection { body }, $collection name { body }, "
          "or $collection $callback");
      return;
    }

    if (bind_name_len > 7) {
      compiler__error(c, line, col, "for binding name exceeds 7-byte inline limit");
      return;
    }

    /* Check for suspension in block body (inlined for still can't suspend) */
    if (ast__contains_suspension(body_block, c->suspension_map)) {
      compiler__error(c, line, col,
          "cannot suspend inside non-suspending callback");
      return;
    }

    /* Check loop depth */
    if (c->loop_depth >= COMPILER_LOOP_DEPTH_MAX) {
      compiler__error(c, line, col, "too many nested loops");
      return;
    }

    /* Begin scope for hidden locals */
    compiler__begin_scope(c);
    uint32_t saved_local_count = c->local_count;

    /* Compile collection → local __col */
    compiler__compile_node(c, args[0]);
    JaclType col_type = c->last_expr_type;
    compiler__add_local(c, jacl_inline_string("__col", 5), line, col);

    uint8_t col_slot = (uint8_t)(saved_local_count);

    if (col_type == TYPE_STREAM) {
      /* ====== Stream-specific inlined for loop ======
         Hidden locals: __col (stream), elem
         Loop: STREAM_NEXT → check exhausted → bind → body → LOOP */

      /* Element placeholder → local $it/name (starts as nil) */
      compiler__emit_byte(c, OP_NIL, line);
      JaclVal bind_val = jacl_inline_string(bind_name, bind_name_len);
      compiler__add_local(c, bind_val, line, col);
      uint8_t elem_slot = (uint8_t)(saved_local_count + 1);

      /* Push loop context */
      LoopContext* lctx = &c->loop_stack[c->loop_depth++];
      lctx->break_patch_count = 0;
      lctx->continue_patch_count = 0;
      lctx->local_count_at_loop = saved_local_count;
      lctx->is_for_loop = true;

      /* --- Loop start --- */
      uint32_t loop_start = c->chunk->code_count;
      lctx->loop_start = loop_start;

      /* Pull next element: push stream, OP_STREAM_NEXT */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, col_slot, line);
      compiler__emit_byte(c, OP_STREAM_NEXT, line);

      /* Check exhaustion: push stream, OP_IS_STREAM_EXHAUSTED */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, col_slot, line);
      compiler__emit_byte(c, OP_IS_STREAM_EXHAUSTED, line);

      /* If NOT exhausted, jump to body */
      uint32_t not_done_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);

      /* Exhausted path: pop nil from STREAM_NEXT, jump to normal exit */
      compiler__emit_byte(c, OP_POP, line);
      uint32_t exit_jump = compiler__emit_jump(c, OP_JUMP, line);

      /* --- Body start (not exhausted) --- */
      compiler__patch_jump(c, not_done_jump);

      /* Bind element: SET_LOCAL + POP */
      compiler__emit_byte(c, OP_SET_LOCAL, line);
      compiler__emit_byte(c, elem_slot, line);
      compiler__emit_byte(c, OP_POP, line);

      /* Compile body statements inline */
      uint32_t body_count = body_block->data.block.count;
      for (uint32_t i = 0; i < body_count; i++) {
        compiler__compile_node(c, body_block->data.block.commands[i]);
        compiler__emit_check_error(c, line);
      }

      /* --- Continue target --- */
      for (uint32_t i = 0; i < lctx->continue_patch_count; i++) {
        compiler__patch_jump(c, lctx->continue_patches[i]);
      }

      /* Loop back to start */
      compiler__emit_byte(c, OP_LOOP, line);
      uint32_t back_offset = c->chunk->code_count - loop_start + 2;
      compiler__emit_byte(c, (uint8_t)((back_offset >> 8) & 0xFF), line);
      compiler__emit_byte(c, (uint8_t)(back_offset & 0xFF), line);

      /* --- Normal exit --- */
      compiler__patch_jump(c, exit_jump);

      /* End scope: pop hidden locals (__col, elem) */
      compiler__end_scope(c, line);

      /* Normal exit: push nil */
      compiler__emit_byte(c, OP_NIL, line);

      /* Jump over break landing zone */
      uint32_t skip_break = compiler__emit_jump(c, OP_JUMP, line);

      /* Break landing zone */
      for (uint32_t i = 0; i < lctx->break_patch_count; i++) {
        compiler__patch_jump(c, lctx->break_patches[i]);
      }

      /* Convergence */
      compiler__patch_jump(c, skip_break);

      /* Pop loop context */
      c->loop_depth--;
      return;
    }

    /* ====== Vector-based inlined for loop (original path) ======
       Hidden locals: __col, __len, __idx, $it/name */

    /* Compute length → local __len */
    compiler__emit_byte(c, OP_GET_LOCAL, line);
    compiler__emit_byte(c, (uint8_t)(c->local_count - 1), line);
    compiler__emit_byte(c, OP_VEC_LEN, line);
    compiler__add_local(c, jacl_inline_string("__len", 5), line, col);

    /* Counter → local __idx (starts at 0) */
    compiler__emit_constant(c, jacl_i32(0), line);
    compiler__add_local(c, jacl_inline_string("__idx", 5), line, col);

    /* Element placeholder → local $it/name (starts as nil) */
    compiler__emit_byte(c, OP_NIL, line);
    JaclVal bind_val = jacl_inline_string(bind_name, bind_name_len);
    compiler__add_local(c, bind_val, line, col);

    uint8_t len_slot = (uint8_t)(saved_local_count + 1);
    uint8_t idx_slot = (uint8_t)(saved_local_count + 2);
    uint8_t elem_slot = (uint8_t)(saved_local_count + 3);

    /* Push loop context */
    LoopContext* lctx = &c->loop_stack[c->loop_depth++];
    lctx->break_patch_count = 0;
    lctx->continue_patch_count = 0;
    lctx->local_count_at_loop = saved_local_count;
    lctx->is_for_loop = true;

    /* --- Condition check (loop start for OP_LOOP backward jumps) --- */
    uint32_t loop_start = c->chunk->code_count;
    lctx->loop_start = loop_start;

    /* __idx < __len */
    compiler__emit_byte(c, OP_GET_LOCAL, line);
    compiler__emit_byte(c, idx_slot, line);
    compiler__emit_byte(c, OP_GET_LOCAL, line);
    compiler__emit_byte(c, len_slot, line);
    compiler__emit_byte(c, OP_LT, line);

    /* Exit if false */
    uint32_t exit_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);

    /* Update element: $it = __col[__idx] */
    compiler__emit_byte(c, OP_GET_LOCAL, line);
    compiler__emit_byte(c, col_slot, line);
    compiler__emit_byte(c, OP_GET_LOCAL, line);
    compiler__emit_byte(c, idx_slot, line);
    compiler__emit_byte(c, OP_VEC_GET, line);
    compiler__emit_byte(c, OP_SET_LOCAL, line);
    compiler__emit_byte(c, elem_slot, line);
    compiler__emit_byte(c, OP_POP, line);  /* discard SET_LOCAL's TOS */

    /* Compile body statements inline */
    uint32_t body_count = body_block->data.block.count;
    for (uint32_t i = 0; i < body_count; i++) {
      compiler__compile_node(c, body_block->data.block.commands[i]);
      compiler__emit_check_error(c, line);
    }

    /* --- Continue target: patch forward jumps from continue --- */
    for (uint32_t i = 0; i < lctx->continue_patch_count; i++) {
      compiler__patch_jump(c, lctx->continue_patches[i]);
    }

    /* Increment: __idx = __idx + 1 */
    compiler__emit_byte(c, OP_GET_LOCAL, line);
    compiler__emit_byte(c, idx_slot, line);
    compiler__emit_constant(c, jacl_i32(1), line);
    compiler__emit_byte(c, OP_ADD, line);
    compiler__emit_byte(c, OP_SET_LOCAL, line);
    compiler__emit_byte(c, idx_slot, line);
    compiler__emit_byte(c, OP_POP, line);  /* discard SET_LOCAL's TOS */

    /* Loop back to condition */
    compiler__emit_byte(c, OP_LOOP, line);
    uint32_t back_offset = c->chunk->code_count - loop_start + 2;
    compiler__emit_byte(c, (uint8_t)((back_offset >> 8) & 0xFF), line);
    compiler__emit_byte(c, (uint8_t)(back_offset & 0xFF), line);

    /* --- Exit --- */
    compiler__patch_jump(c, exit_jump);

    /* End scope: pop hidden locals (__col, __len, __idx, $it) */
    compiler__end_scope(c, line);

    /* Normal exit: push nil */
    compiler__emit_byte(c, OP_NIL, line);

    /* Jump over break landing zone */
    uint32_t skip_break = compiler__emit_jump(c, OP_JUMP, line);

    /* Break landing zone (break value already on stack, locals cleaned up) */
    for (uint32_t i = 0; i < lctx->break_patch_count; i++) {
      compiler__patch_jump(c, lctx->break_patches[i]);
    }

    /* Convergence */
    compiler__patch_jump(c, skip_break);

    /* Pop loop context */
    c->loop_depth--;
    return;
  }

  /* break [value] — bracket form */
  if (compiler__head_matches(head, "break", 5)) {
    if (argc > 1) {
      compiler__builtin_arity_error(c, line, col, "break", "0 or 1 arguments", argc);
      return;
    }
    if (c->loop_depth == 0) {
      compiler__error(c, line, col, "break outside of loop");
      return;
    }
    LoopContext* lctx = &c->loop_stack[c->loop_depth - 1];
    if (argc == 1) {
      compiler__compile_node(c, args[0]);
    } else {
      compiler__emit_byte(c, OP_NIL, line);
    }
    /* For inlined for-loops, clean up hidden locals under the break value */
    if (lctx->is_for_loop) {
      uint32_t cleanup = c->local_count - lctx->local_count_at_loop;
      if (cleanup > 0) {
        compiler__emit_byte(c, OP_CLOSE_LOOP, line);
        compiler__emit_byte(c, (uint8_t)cleanup, line);
      }
    }
    if (lctx->break_patch_count < COMPILER_BREAK_PATCHES_MAX) {
      lctx->break_patches[lctx->break_patch_count++] =
          compiler__emit_jump(c, OP_JUMP, line);
    } else {
      compiler__error(c, line, col, "too many break statements in loop");
    }
    return;
  }

  /* continue — bracket form */
  if (compiler__head_matches(head, "continue", 8)) {
    if (argc != 0) {
      compiler__builtin_arity_error(c, line, col, "continue", "0 arguments", argc);
      return;
    }
    if (c->loop_depth == 0) {
      compiler__error(c, line, col, "continue outside of loop");
      return;
    }
    LoopContext* lctx = &c->loop_stack[c->loop_depth - 1];
    if (lctx->is_for_loop) {
      /* For-loop: forward-jump to increment code (patched later) */
      if (lctx->continue_patch_count < COMPILER_CONTINUE_PATCHES_MAX) {
        lctx->continue_patches[lctx->continue_patch_count++] =
            compiler__emit_jump(c, OP_JUMP, line);
      } else {
        compiler__error(c, line, col, "too many continue statements in loop");
      }
    } else {
      /* While-loop: pop body-scope locals, then backward-jump to condition */
      uint32_t cleanup = c->local_count - lctx->local_count_at_loop;
      if (cleanup > 0) {
        compiler__emit_byte(c, OP_POP_N, line);
        compiler__emit_byte(c, (uint8_t)cleanup, line);
      }
      compiler__emit_byte(c, OP_LOOP, line);
      uint32_t offset = c->chunk->code_count - lctx->loop_start + 2;
      compiler__emit_byte(c, (uint8_t)((offset >> 8) & 0xFF), line);
      compiler__emit_byte(c, (uint8_t)(offset & 0xFF), line);
    }
    /* continue must leave a value for the statement (popped by CHECK_ERROR) */
    compiler__emit_byte(c, OP_NIL, line);
    return;
  }

  /* return [value] — bracket form */
  if (compiler__head_matches(head, "return", 6)) {
    if (argc > 1) {
      compiler__builtin_arity_error(c, line, col, "return", "0 or 1 arguments", argc);
      return;
    }
    if (argc == 1) {
      compiler__compile_node(c, args[0]);
    } else {
      compiler__emit_byte(c, OP_NIL, line);
    }
    compiler__emit_byte(c, OP_RETURN, line);
    return;
  }

  /* try special form: [try { body } name { handler }] */
  if (compiler__head_matches(head, "try", 3)) {
    if (argc != 3) {
      compiler__builtin_arity_error(c, line, col, "try", "3 arguments", argc);
      return;
    }
    if (args[0]->type != AST_BLOCK) {
      compiler__error(c, line, col, "try body must be a block");
      return;
    }
    if (args[1]->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "try binding must be a name");
      return;
    }
    if (args[2]->type != AST_BLOCK) {
      compiler__error(c, line, col, "try handler must be a block");
      return;
    }

    /* Get binding name */
    uint32_t bind_len = args[1]->data.lit_string.length;
    if (bind_len > 7) {
      compiler__error(c, line, col, "try binding name exceeds 7-byte inline limit");
      return;
    }
    JaclVal bind_name = jacl_inline_string(args[1]->data.lit_string.value, bind_len);

    /* Save try context */
    bool saved_in_try = c->in_try_body;
    uint32_t saved_patch_start = c->try_patch_count;
    c->in_try_body = true;

    /* Compile try body as block expression */
    compiler__compile_block_expr(c, args[0]);

    /* Restore in_try_body (patches still need to be applied) */
    c->in_try_body = saved_in_try;

    /* After body: check if final result is an error */
    uint32_t handler_jump = compiler__emit_jump(c, OP_JUMP_IF_ERROR, line);

    /* Normal path: skip handler */
    uint32_t skip_jump = compiler__emit_jump(c, OP_JUMP, line);

    /* Handler entry point */
    uint32_t handler_pos = c->chunk->code_count;

    /* Patch OP_JUMP_IF_ERROR to handler */
    compiler__patch_jump(c, handler_jump);

    /* Patch all OP_CHECK_ERROR offsets from try body to handler */
    for (uint32_t i = saved_patch_start; i < c->try_patch_count; i++) {
      uint32_t patch_pos = c->try_patches[i];
      uint32_t jump_dist = handler_pos - (patch_pos + 2);
      c->chunk->code[patch_pos]     = (uint8_t)((jump_dist >> 8) & 0xFF);
      c->chunk->code[patch_pos + 1] = (uint8_t)(jump_dist & 0xFF);
    }
    c->try_patch_count = saved_patch_start;

    /* Handler: error value is on stack, bind as local */
    uint32_t handler_scope_start = c->local_count;
    compiler__begin_scope(c);
    compiler__add_local(c, bind_name, line, col);

    /* Compile handler block expression */
    compiler__compile_block_expr(c, args[2]);

    /* Clean up handler scope (pop binding while keeping result) */
    uint32_t handler_pop = c->local_count - handler_scope_start;
    c->scope_depth--;
    c->local_count = handler_scope_start;
    if (handler_pop > 0) {
      compiler__emit_byte(c, OP_SET_LOCAL, line);
      compiler__emit_byte(c, (uint8_t)handler_scope_start, line);
      compiler__emit_byte(c, OP_POP_N, line);
      compiler__emit_byte(c, (uint8_t)handler_pop, line);
    }

    /* Patch skip jump to here (end of try expression) */
    compiler__patch_jump(c, skip_jump);
    return;
  }

  /* vec constructor (variadic: 0+ args) */
  if (compiler__head_matches(head, "vec", 3)) {
    for (uint32_t i = 0; i < argc; i++) {
      compiler__compile_node(c, args[i]);
    }
    compiler__emit_byte(c, OP_VEC, line);
    compiler__emit_byte(c, (uint8_t)argc, line);
    return;
  }

  /* vec-get builtin (exactly 2 args) */
  if (compiler__head_matches(head, "vec-get", 7)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "vec-get", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_VEC_GET, line);
    return;
  }

  /* vec-len builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "vec-len", 7)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "vec-len", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_VEC_LEN, line);
    return;
  }

  /* vec-push builtin (exactly 2 args) */
  if (compiler__head_matches(head, "vec-push", 8)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "vec-push", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_VEC_PUSH, line);
    return;
  }

  /* vec-set builtin (exactly 3 args) */
  if (compiler__head_matches(head, "vec-set", 7)) {
    if (argc != 3) {
      compiler__builtin_arity_error(c, line, col, "vec-set", "3 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__compile_node(c, args[2]);
    compiler__emit_byte(c, OP_VEC_SET, line);
    return;
  }

  /* vec-concat builtin (exactly 2 args) */
  if (compiler__head_matches(head, "vec-concat", 10)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "vec-concat", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_VEC_CONCAT, line);
    return;
  }

  /* vec-slice builtin (exactly 3 args) */
  if (compiler__head_matches(head, "vec-slice", 9)) {
    if (argc != 3) {
      compiler__builtin_arity_error(c, line, col, "vec-slice", "3 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__compile_node(c, args[2]);
    compiler__emit_byte(c, OP_VEC_SLICE, line);
    return;
  }

  /* map constructor (0 or any even number of args) */
  if (compiler__head_matches(head, "map", 3)) {
    if (argc % 2 != 0) {
      compiler__builtin_arity_error(c, line, col, "map",
                                     "an even number of arguments", argc);
      return;
    }
    for (uint32_t i = 0; i < argc; i++) {
      compiler__compile_node(c, args[i]);
    }
    compiler__emit_byte(c, OP_MAP, line);
    compiler__emit_byte(c, (uint8_t)(argc / 2), line);
    return;
  }

  /* map-get builtin (exactly 2 args) */
  if (compiler__head_matches(head, "map-get", 7)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "map-get", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_MAP_GET, line);
    return;
  }

  /* map-has builtin (exactly 2 args) */
  if (compiler__head_matches(head, "map-has", 7)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "map-has", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_MAP_HAS, line);
    return;
  }

  /* map-len builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "map-len", 7)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "map-len", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_MAP_LEN, line);
    return;
  }

  /* map-set builtin (exactly 3 args) */
  if (compiler__head_matches(head, "map-set", 7)) {
    if (argc != 3) {
      compiler__builtin_arity_error(c, line, col, "map-set", "3 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__compile_node(c, args[2]);
    compiler__emit_byte(c, OP_MAP_SET, line);
    return;
  }

  /* map-remove builtin (exactly 2 args) */
  if (compiler__head_matches(head, "map-remove", 10)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "map-remove", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_MAP_REMOVE, line);
    return;
  }

  /* map-keys builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "map-keys", 8)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "map-keys", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_MAP_KEYS, line);
    return;
  }

  /* map-vals builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "map-vals", 8)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "map-vals", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_MAP_VALS, line);
    return;
  }

  /* transform builtin (exactly 2 args — non-suspending callback) */
  if (compiler__head_matches(head, "transform", 9)) {
    compiler__compile_hof_builtin(c, "transform", args, argc, OP_TRANSFORM, line, col);
    return;
  }


  /* filter builtin (exactly 2 args — non-suspending callback) */
  if (compiler__head_matches(head, "filter", 6)) {
    compiler__compile_hof_builtin(c, "filter", args, argc, OP_FILTER, line, col);
    return;
  }

  /* error builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "error", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "error", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_ERROR, line);
    return;
  }

  /* error? builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "error?", 6)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "error?", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_IS_ERROR, line);
    return;
  }

  /* error-val builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "error-val", 9)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "error-val", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_ERROR_VAL, line);
    return;
  }

  /* stack-trace builtin (exactly 0 args) */
  if (compiler__head_matches(head, "stack-trace", 11)) {
    if (argc != 0) {
      compiler__builtin_arity_error(c, line, col, "stack-trace", "0 arguments", argc);
      return;
    }
    compiler__emit_byte(c, OP_STACK_TRACE, line);
    return;
  }

  /* to-string builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "to-string", 9)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "to-string", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__ensure_boxed(c, line);
    compiler__emit_byte(c, OP_TO_STRING, line);
    c->last_expr_type = TYPE_STR;
    return;
  }

  /* box builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "box", 3)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "box", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_BOX, line);
    return;
  }

  /* atom builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "atom", 4)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "atom", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_ATOM, line);
    return;
  }

  /* box? builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "box?", 4)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "box?", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_IS_BOX, line);
    return;
  }

  /* atom? builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "atom?", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "atom?", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_IS_ATOM, line);
    return;
  }

  /* future? builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "future?", 7)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "future?", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_IS_FUTURE, line);
    return;
  }

  /* deref builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "deref", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "deref", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_DEREF, line);
    return;
  }

  /* reset builtin (exactly 2 args) */
  if (compiler__head_matches(head, "reset", 5)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "reset", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_RESET, line);
    return;
  }

  /* swap builtin (exactly 2 args) */
  if (compiler__head_matches(head, "swap", 4)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "swap", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_SWAP, line);
    return;
  }

  /* to builtin — explicit type conversion: [to TYPE expr] */
  if (compiler__head_matches(head, "to", 2)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "to", "2 arguments", argc);
      return;
    }
    /* First arg must be a bare word matching a type keyword */
    AstNode* type_node = args[0];
    if (type_node->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "to: first argument must be a type keyword (i32, i64, u32, u64, f32, f64, dyn)");
      return;
    }
    const char* type_word = type_node->data.lit_string.value;
    uint32_t type_len = type_node->data.lit_string.length;
    if (!is_type_keyword(type_word, type_len)) {
      char err[128];
      snprintf(err, sizeof(err), "to: unknown type '%.*s'", (int)type_len, type_word);
      compiler__error(c, line, col, err);
      return;
    }
    JaclType target_type = type_from_keyword(type_word, type_len);

    /* Compile the expression */
    compiler__compile_node(c, args[1]);
    JaclType src_type = c->last_expr_type;

    /* Validate conversion at compile time */
    if (src_type != TYPE_DYN && target_type != TYPE_DYN) {
      /* Both concrete types — check if conversion is valid */
      bool src_ok = is_numeric_type(src_type);
      bool tgt_ok = is_numeric_type(target_type);
      if (!src_ok || !tgt_ok) {
        /* Allow same-type no-op (e.g. to str $s where s is str) */
        if (src_type != target_type) {
          char err[128];
          snprintf(err, sizeof(err), "type error: cannot convert %s to %s",
                   type_name(src_type), type_name(target_type));
          compiler__error(c, line, col, err);
          return;
        }
      }
    }
    /* Check dyn→non-numeric target (dyn can only convert to numeric or dyn at compile time) */
    if (src_type != TYPE_DYN && target_type != TYPE_DYN &&
        !is_numeric_type(src_type) && src_type != target_type) {
      char err[128];
      snprintf(err, sizeof(err), "type error: cannot convert %s to %s",
               type_name(src_type), type_name(target_type));
      compiler__error(c, line, col, err);
      return;
    }

    /* Same type → no-op */
    if (src_type == target_type) {
      c->last_expr_type = target_type;
      return;
    }

    /* dyn→dyn → no-op */
    if (src_type == TYPE_DYN && target_type == TYPE_DYN) {
      return;
    }

    /* Emit appropriate conversion opcode */
    uint8_t opcode;
    switch (target_type) {
      case TYPE_I32: opcode = OP_TO_I32; break;
      case TYPE_I64: opcode = OP_TO_I64; break;
      case TYPE_U32: opcode = OP_TO_U32; break;
      case TYPE_U64: opcode = OP_TO_U64; break;
      case TYPE_F32: opcode = OP_TO_F32; break;
      case TYPE_F64: opcode = OP_TO_F64; break;
      case TYPE_DYN: opcode = OP_TO_DYN; break;
      default: {
        compiler__error(c, line, col, "to: unsupported target type");
        return;
      }
    }
    compiler__emit_byte(c, opcode, line);
    compiler__emit_byte(c, (uint8_t)src_type, line);
    c->last_expr_type = target_type;
    return;
  }

  /* await — suspension point (CPS transform in US-003+, context checks now) */
  if (compiler__head_matches(head, "await", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "await", "1 argument", argc);
      return;
    }
    if (c->in_try_body) {
      compiler__error(c, line, col,
          "cannot suspend inside try/catch; use error capture on futures instead");
      return;
    }
    if (c->in_non_suspending_callback) {
      compiler__error(c, line, col,
          "cannot suspend inside non-suspending callback");
      return;
    }
    /* Placeholder: compile arg then replace with nil (OP_AWAIT in US-006) */
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_POP, line);
    compiler__emit_byte(c, OP_NIL, line);
    return;
  }

  /* yield — generator suspension point.
     In CPS mode, yield is handled by compiler__compile_cps_stmts.
     This path is a fallback for non-CPS contexts (shouldn't normally be reached
     for generators, but compile defensively). Stack: [value, continuation]. */
  if (compiler__head_matches(head, "yield", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "yield", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_NIL, line);  /* nil continuation (no CPS context) */
    compiler__emit_byte(c, OP_YIELD, line);
    c->has_yield = true;
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* stream_next — pull next element from a stream */
  if (compiler__head_matches(head, "stream_next", 11)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "stream_next", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_STREAM_NEXT, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* collect — materialize stream into vector (identity on vectors) */
  if (compiler__head_matches(head, "collect", 7)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "collect", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_COLLECT, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }

  /* count — count elements in stream or vector */
  if (compiler__head_matches(head, "count", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "count", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_COUNT, line);
    c->last_expr_type = TYPE_I32;
    return;
  }

  /* take — take first N elements from stream or vector */
  if (compiler__head_matches(head, "take", 4)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "take", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    JaclType col_type = c->last_expr_type;
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_TAKE, line);
    c->last_expr_type = col_type;
    return;
  }

  /* first — get first element from stream or vector */
  if (compiler__head_matches(head, "first", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "first", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_FIRST, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* parallel — suspension point (CPS transform in US-003+, context checks now) */
  if (compiler__head_matches(head, "parallel", 8)) {
    if (argc < 2) {
      compiler__builtin_arity_error(c, line, col, "parallel",
                                     "at least 2 arguments", argc);
      return;
    }
    if (c->in_try_body) {
      compiler__error(c, line, col,
          "cannot suspend inside try/catch; use error capture on futures instead");
      return;
    }
    if (c->in_non_suspending_callback) {
      compiler__error(c, line, col,
          "cannot suspend inside non-suspending callback");
      return;
    }
    /* Compile each body as a closure, emit OP_PARALLEL.
       In CPS context: handled by compile_cps_stmts Case 1b.
       This path is reached only for non-CPS compilation (shouldn't happen
       with well-formed programs, but compile defensively). */
    for (uint32_t i = 0; i < argc; i++) {
      compiler__compile_parallel_body(c, args[i], line, col);
    }
    /* Non-CPS: no continuation available, push nil as placeholder */
    compiler__emit_byte(c, OP_NIL, line);
    compiler__emit_byte(c, OP_PARALLEL, line);
    compiler__emit_byte(c, (uint8_t)argc, line);
    return;
  }

  /* race — suspension point (CPS transform in US-003+, context checks now) */
  if (compiler__head_matches(head, "race", 4)) {
    if (argc < 2) {
      compiler__builtin_arity_error(c, line, col, "race",
                                     "at least 2 arguments", argc);
      return;
    }
    if (c->in_try_body) {
      compiler__error(c, line, col,
          "cannot suspend inside try/catch; use error capture on futures instead");
      return;
    }
    if (c->in_non_suspending_callback) {
      compiler__error(c, line, col,
          "cannot suspend inside non-suspending callback");
      return;
    }
    /* Race bodies compiled as closures (same as parallel) */
    for (uint32_t i = 0; i < argc; i++) {
      compiler__compile_parallel_body(c, args[i], line, col);
    }
    compiler__emit_byte(c, OP_RACE, line);
    compiler__emit_byte(c, (uint8_t)argc, line);
    return;
  }

  /* spawn — NOT a suspension point (runtime task submission) */
  if (compiler__head_matches(head, "spawn", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "spawn", "1 argument", argc);
      return;
    }
    if (args[0]->type != AST_BLOCK) {
      compiler__error(c, line, col, "spawn body must be a block");
      return;
    }

    AstNode* body_block = args[0];
    uint32_t stmt_count = body_block->data.block.count;
    AstNode** stmts = body_block->data.block.commands;

    /* Check if the spawn body contains await → needs CPS transform */
    bool spawn_suspends = ast__contains_suspension(body_block, c->suspension_map);

    /* Allocate anonymous closure for the spawn body */
    JaclClosure* closure = (JaclClosure*)arena_alloc(c->arena, sizeof(JaclClosure));
    chunk_init(&closure->chunk, c->arena);
    closure->name         = "<spawn>";
    closure->upvalue_count = 0;
    closure->upvalues     = NULL;
    closure->param_names  = NULL;
    closure->min_args     = 0;
    closure->variadic     = false;
    closure->pin_worker_id = -1;

    /* Pin spawn body to thread 0 if it mutates non-local variables
       OR captures a mutable (mut/box) binding from an enclosing scope.
       Bodies with only local mutations can run on any worker. */
    bool needs_pinning = ast__contains_nonlocal_set(body_block)
                      || compiler__body_captures_mutable(c, body_block);
    closure->pinned = needs_pinning;

    if (spawn_suspends) {
      /* CPS spawn: hidden __k parameter */
      closure->param_count = 1;
      JaclVal* pnames = (JaclVal*)arena_alloc(c->arena, sizeof(JaclVal));
      pnames[0] = jacl_inline_string("__k", 3);
      closure->param_names = pnames;
    } else {
      closure->param_count = 0;
    }

    /* Create body compiler */
    Compiler body_compiler;
    compiler__init(&body_compiler, &closure->chunk, c->arena, c->intern_table, c->heap);
    body_compiler.scope_depth    = 1;
    body_compiler.enclosing      = c;
    body_compiler.suspension_map = c->suspension_map;
    body_compiler.pin_all_closures = needs_pinning;

    /* Copy global arities for suspension lookups */
    {
      Compiler* root = c;
      while (root->enclosing) root = root->enclosing;
      memcpy(body_compiler.global_arities, root->global_arities,
             sizeof(GlobalArity) * root->global_arity_count);
      body_compiler.global_arity_count = root->global_arity_count;
    }

    if (spawn_suspends) {
      /* Add __k as local (slot 0) */
      compiler__add_local(&body_compiler, jacl_inline_string("__k", 3), line, col);
      body_compiler.locals[body_compiler.local_count - 1].is_param = true;
      body_compiler.is_cps = true;
      body_compiler.in_concurrent_body = true;

      if (stmt_count == 0) {
        compiler__emit_get_k(&body_compiler, line);
        compiler__emit_byte(&body_compiler, OP_NIL, line);
        compiler__emit_byte(&body_compiler, OP_TAIL_CALL, line);
        compiler__emit_byte(&body_compiler, 1, line);
      } else {
        compiler__compile_cps_stmts(&body_compiler, stmts, stmt_count, line);
      }
      compiler__emit_byte(&body_compiler, OP_RETURN, line);
    } else {
      /* Non-suspending spawn body: compile as block expression */
      body_compiler.in_concurrent_body = true;
      compiler__compile_block_expr(&body_compiler, body_block);
      compiler__emit_byte(&body_compiler, OP_RETURN, line);
    }

    /* Propagate errors */
    c->error_count += body_compiler.error_count;
    if (!c->first_error && body_compiler.first_error) {
      c->first_error = body_compiler.first_error;
    }

    closure->upvalue_count = (uint8_t)body_compiler.upvalue_count;

    /* Emit OP_CLOSURE + upvalue descriptors */
    uint16_t closure_idx = chunk_add_constant(c->chunk, jacl_closure(closure));
    compiler__emit_byte(c, OP_CLOSURE, line);
    compiler__emit_u16(c, closure_idx, line);
    for (uint32_t i = 0; i < body_compiler.upvalue_count; i++) {
      compiler__emit_byte(c, body_compiler.upvalues[i].is_local, line);
      compiler__emit_byte(c, body_compiler.upvalues[i].index, line);
    }

    compiler__emit_byte(c, OP_SPAWN, line);
    return;
  }

  /* Struct field access/mutation: [. $s field] or [. $s field value] */
  if (compiler__head_matches(head, ".", 1)) {
    bool is_set = (argc == 3);
    if (argc != 2 && argc != 3) {
      compiler__builtin_arity_error(c, line, col, ".", "2 or 3 arguments", argc);
      return;
    }

    /* Compile struct expression */
    compiler__compile_node(c, args[0]);
    JaclType struct_type = c->last_expr_type;
    uint32_t struct_idx = c->last_struct_idx;

    if (struct_type != TYPE_STRUCT && struct_type != TYPE_DYN) {
      compiler__error(c, line, col, "type error: '.' requires a struct value");
      return;
    }

    /* Field name must be a literal string */
    if (args[1]->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "field name must be a literal identifier");
      return;
    }

    const char* field_name = args[1]->data.lit_string.value;
    uint32_t field_name_len = args[1]->data.lit_string.length;

    if (struct_type == TYPE_STRUCT && struct_idx != UINT32_MAX) {
      StructTypeRegistry* reg = compiler__get_struct_registry(c);
      if (reg && struct_idx < reg->count) {
        StructTypeDef* sdef = &reg->defs[struct_idx];
        /* Find field by name */
        uint32_t fi;
        for (fi = 0; fi < sdef->field_count; fi++) {
          if (sdef->fields[fi].name_len == field_name_len &&
              memcmp(sdef->fields[fi].name, field_name, field_name_len) == 0) {
            break;
          }
        }
        if (fi == sdef->field_count) {
          char err_msg[128];
          snprintf(err_msg, sizeof(err_msg),
                   "struct '%.*s' has no field '%.*s'",
                   (int)sdef->name_len, sdef->name,
                   (int)field_name_len, field_name);
          compiler__error(c, line, col, err_msg);
          return;
        }

        if (is_set) {
          /* Compile new value with type checking */
          JaclType field_type = sdef->fields[fi].type;
          c->expected_type = field_type;
          compiler__compile_node(c, args[2]);
          JaclType val_type = c->last_expr_type;
          c->expected_type = TYPE_DYN;

          if (field_type != TYPE_DYN && val_type != TYPE_DYN && val_type != field_type) {
            char err_msg[192];
            snprintf(err_msg, sizeof(err_msg),
                     "type error: field '%.*s' of struct '%.*s' expected %s, got %s",
                     (int)sdef->fields[fi].name_len, sdef->fields[fi].name,
                     (int)sdef->name_len, sdef->name,
                     type_name(field_type), type_name(val_type));
            compiler__error(c, line, col, err_msg);
            return;
          }

          /* Emit OP_STRUCT_SET + field_offset (u16) + field_type (u8) */
          compiler__emit_byte(c, OP_STRUCT_SET, line);
          compiler__emit_u16(c, (uint16_t)sdef->fields[fi].offset, line);
          compiler__emit_byte(c, (uint8_t)field_type, line);

          /* Returns struct value */
          c->last_expr_type = TYPE_STRUCT;
          c->last_struct_idx = struct_idx;
        } else {
          /* Emit OP_STRUCT_GET + field_offset (u16) + field_type (u8) */
          compiler__emit_byte(c, OP_STRUCT_GET, line);
          compiler__emit_u16(c, (uint16_t)sdef->fields[fi].offset, line);
          compiler__emit_byte(c, (uint8_t)sdef->fields[fi].type, line);

          c->last_expr_type = sdef->fields[fi].type;
          if (sdef->fields[fi].type == TYPE_STRUCT)
            c->last_struct_idx = sdef->fields[fi].struct_type_idx;
        }
        return;
      }
    }

    /* Struct type unknown at compile time — emit runtime field resolution */
    {
      /* Store field name as a constant */
      JaclVal name_val = jacl_inline_string(field_name, field_name_len);
      uint16_t name_idx = chunk_add_constant(c->chunk, name_val);

      if (is_set) {
        /* Compile the new value */
        compiler__compile_node(c, args[2]);
        /* Emit OP_STRUCT_SET_DYN + const_idx (field name) */
        compiler__emit_byte(c, OP_STRUCT_SET_DYN, line);
        compiler__emit_u16(c, name_idx, line);
        /* Result type is dyn (we don't know the struct type) */
        c->last_expr_type = TYPE_DYN;
        c->last_struct_idx = UINT32_MAX;
      } else {
        /* Emit OP_STRUCT_GET_DYN + const_idx (field name) */
        compiler__emit_byte(c, OP_STRUCT_GET_DYN, line);
        compiler__emit_u16(c, name_idx, line);
        /* Result type is dyn (field type unknown at compile time) */
        c->last_expr_type = TYPE_DYN;
        c->last_struct_idx = UINT32_MAX;
      }
    }
    return;
  }

  /* Struct constructor: [StructName field1 field2 ...] */
  if (head->type == AST_LIT_STRING) {
    StructTypeRegistry* reg = compiler__get_struct_registry(c);
    uint32_t name_len = head->data.lit_string.length;
    uint32_t struct_idx = struct_registry__find(reg,
        head->data.lit_string.value, name_len);
    if (struct_idx != UINT32_MAX) {
      StructTypeDef* sdef = &reg->defs[struct_idx];

      /* Arity check */
      if (argc != sdef->field_count) {
        char err_msg[128];
        snprintf(err_msg, sizeof(err_msg),
                 "struct '%.*s' has %u fields but got %u arguments",
                 (int)name_len, head->data.lit_string.value,
                 sdef->field_count, argc);
        compiler__error(c, line, col, err_msg);
        return;
      }

      /* Compile and type-check each field argument */
      for (uint32_t i = 0; i < argc; i++) {
        JaclType field_type = sdef->fields[i].type;
        c->expected_type = field_type;
        compiler__compile_node(c, args[i]);
        JaclType arg_type = c->last_expr_type;
        c->expected_type = TYPE_DYN;

        if (field_type != TYPE_DYN && arg_type != TYPE_DYN &&
            arg_type != field_type) {
          char err_msg[192];
          snprintf(err_msg, sizeof(err_msg),
                   "type error: field '%.*s' of struct '%.*s' expected %s, got %s",
                   (int)sdef->fields[i].name_len, sdef->fields[i].name,
                   (int)name_len, head->data.lit_string.value,
                   type_name(field_type), type_name(arg_type));
          compiler__error(c, line, col, err_msg);
          return;
        }
        if (field_type != TYPE_DYN && arg_type == TYPE_DYN) {
          char err_msg[192];
          snprintf(err_msg, sizeof(err_msg),
                   "type error: field '%.*s' of struct '%.*s' expected %s, got dyn",
                   (int)sdef->fields[i].name_len, sdef->fields[i].name,
                   (int)name_len, head->data.lit_string.value,
                   type_name(field_type));
          compiler__error(c, line, col, err_msg);
          return;
        }
      }

      /* Emit OP_STRUCT_NEW + uint16_t struct_type_index */
      compiler__emit_byte(c, OP_STRUCT_NEW, line);
      compiler__emit_u16(c, (uint16_t)struct_idx, line);

      c->last_expr_type = TYPE_STRUCT;
      c->last_struct_idx = struct_idx;
      return;
    }
  }

  /* Dynamic call: unrecognized command head — look up and call */
  {
    /* Resolve callee param types for call-site type checking */
    JaclType* call_param_types = NULL;
    JaclType call_return_type = TYPE_DYN;
    int16_t call_param_count = -1;
    const char* callee_name_str = NULL;
    uint32_t callee_name_len = 0;

    if (head->type == AST_LIT_STRING) {
      /* Look up bare word as a variable */
      uint32_t name_len = head->data.lit_string.length;
      if (name_len > 7) {
        compiler__error(c, line, col, "command name exceeds 7-byte inline limit");
        return;
      }
      JaclVal name_val = jacl_inline_string(head->data.lit_string.value, name_len);
      int local_slot = compiler__resolve_local(c, name_val);
      callee_name_str = head->data.lit_string.value;
      callee_name_len = name_len;

      /* Compile-time arity check and param type resolution */
      {
        int16_t head_arity = -1;
        if (local_slot != -1) {
          head_arity = c->locals[local_slot].known_arity;
          call_param_types = c->locals[local_slot].param_types;
          call_return_type = c->locals[local_slot].return_type;
          call_param_count = head_arity;
        } else {
          GlobalArity* ga = compiler__find_global(c, name_val);
          if (ga) {
            head_arity = ga->known_arity;
            call_param_types = ga->param_types;
            call_return_type = ga->return_type;
            call_param_count = head_arity;
          }
        }
        if (head_arity != -1 && (int16_t)argc != head_arity) {
          char err_msg[128];
          snprintf(err_msg, sizeof(err_msg),
                   "proc '%.*s' expects %d arguments but got %d",
                   (int)name_len, head->data.lit_string.value,
                   (int)head_arity, (int)argc);
          compiler__error(c, line, col, err_msg);
          return;
        }
      }

      if (local_slot != -1) {
        if (c->locals[local_slot].is_mutable) {
          compiler__emit_byte(c, OP_GET_CELL_LOCAL, line);
        } else {
          compiler__emit_byte(c, OP_GET_LOCAL, line);
        }
        compiler__emit_byte(c, (uint8_t)local_slot, line);
      } else {
        JaclVal gkey = compiler__global_name_val(c,
            head->data.lit_string.value, name_len);
        uint16_t name_idx = chunk_add_constant(c->chunk, gkey);
        compiler__emit_byte(c, OP_GET_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
      }
    } else {
      /* Non-string head (e.g. $var, nested command): compile as expression */
      /* Arity check for $var heads */
      if (head->type == AST_VAR_REF) {
        int16_t head_arity = compiler__node_known_arity(c, head);
        if (head_arity != -1 && (int16_t)argc != head_arity) {
          uint32_t name_len = head->data.var_ref.length;
          char err_msg[128];
          snprintf(err_msg, sizeof(err_msg),
                   "proc '%.*s' expects %d arguments but got %d",
                   (int)name_len, head->data.var_ref.name,
                   (int)head_arity, (int)argc);
          compiler__error(c, line, col, err_msg);
          return;
        }
        /* Resolve param types for $var call-site checking */
        callee_name_str = head->data.var_ref.name;
        callee_name_len = head->data.var_ref.length;
        if (head->data.var_ref.length <= 7) {
          JaclVal vname = jacl_inline_string(head->data.var_ref.name,
                                              head->data.var_ref.length);
          int slot = compiler__resolve_local(c, vname);
          if (slot != -1) {
            call_param_types = c->locals[slot].param_types;
            call_return_type = c->locals[slot].return_type;
            call_param_count = c->locals[slot].known_arity;
          } else {
            GlobalArity* ga = compiler__find_global(c, vname);
            if (ga) {
              call_param_types = ga->param_types;
              call_return_type = ga->return_type;
              call_param_count = ga->known_arity;
            }
          }
        }
      }
      compiler__compile_node(c, head);
    }

    /* Compile arguments with call-site type checking */
    for (uint32_t i = 0; i < argc; i++) {
      JaclType expected_param_type = TYPE_DYN;
      if (call_param_types && call_param_count > 0 && (int32_t)i < call_param_count) {
        expected_param_type = call_param_types[i];
      }

      /* Set contextual type for argument */
      if (expected_param_type != TYPE_DYN) {
        c->expected_type = expected_param_type;
      }
      compiler__compile_node(c, args[i]);
      JaclType arg_type = c->last_expr_type;
      c->expected_type = TYPE_DYN;

      /* Type check: argument vs declared param type */
      if (expected_param_type != TYPE_DYN) {
        if (arg_type != TYPE_DYN && arg_type != expected_param_type) {
          char err_msg[192];
          snprintf(err_msg, sizeof(err_msg),
                   "type error: argument %d of %.*s expected %s, got %s",
                   (int)(i + 1), (int)callee_name_len, callee_name_str,
                   type_name(expected_param_type), type_name(arg_type));
          compiler__error(c, line, col, err_msg);
          return;
        }
        if (arg_type == TYPE_DYN) {
          char err_msg[192];
          snprintf(err_msg, sizeof(err_msg),
                   "type error: argument %d of %.*s expected %s, got dyn (use [to %s $val])",
                   (int)(i + 1), (int)callee_name_len, callee_name_str,
                   type_name(expected_param_type), type_name(expected_param_type));
          compiler__error(c, line, col, err_msg);
          return;
        }
      }
    }

    /* Emit call */
    compiler__emit_byte(c, OP_CALL, line);
    compiler__emit_byte(c, (uint8_t)argc, line);

    /* Set result type from callee's return type */
    c->last_expr_type = call_return_type;
  }
}

/* --- Internal: Compile a single AST node --- */

static void compiler__compile_node(Compiler* c, AstNode* node) {
  uint32_t line = node->start.line;
  c->last_expr_type = TYPE_DYN;  /* default; specific cases override */

  switch (node->type) {

    case AST_LIT_INT: {
      JaclType et = c->expected_type;
      if (et == TYPE_I64) {
        int64_t v = (int64_t)node->data.lit_int.value;
        uint16_t idx = chunk_add_constant(c->chunk, (JaclVal)(uint64_t)v);
        compiler__emit_byte(c, OP_CONST_I64, line);
        compiler__emit_u16(c, idx, line);
        c->last_expr_type = TYPE_I64;
      } else if (et == TYPE_U64) {
        uint64_t v = (uint64_t)(uint32_t)node->data.lit_int.value;
        uint16_t idx = chunk_add_constant(c->chunk, (JaclVal)v);
        compiler__emit_byte(c, OP_CONST_U64, line);
        compiler__emit_u16(c, idx, line);
        c->last_expr_type = TYPE_U64;
      } else if (et == TYPE_F64) {
        double d = (double)node->data.lit_int.value;
        uint64_t raw;
        memcpy(&raw, &d, sizeof(raw));
        uint16_t idx = chunk_add_constant(c->chunk, (JaclVal)raw);
        compiler__emit_byte(c, OP_CONST_F64, line);
        compiler__emit_u16(c, idx, line);
        c->last_expr_type = TYPE_F64;
      } else if (et == TYPE_U32) {
        compiler__emit_constant(c, jacl_u32((uint32_t)node->data.lit_int.value), line);
        c->last_expr_type = TYPE_U32;
      } else if (et == TYPE_F32) {
        compiler__emit_constant(c, jacl_f32((float)node->data.lit_int.value), line);
        c->last_expr_type = TYPE_F32;
      } else {
        /* Default: tagged i32 (works for expected TYPE_DYN or TYPE_I32) */
        compiler__emit_constant(c, jacl_i32(node->data.lit_int.value), line);
        c->last_expr_type = (et == TYPE_I32) ? TYPE_I32 : TYPE_I32;
      }
      break;
    }

    case AST_LIT_FLOAT: {
      JaclType et = c->expected_type;
      if (et == TYPE_F64) {
        double d = (double)node->data.lit_float.value;
        uint64_t raw;
        memcpy(&raw, &d, sizeof(raw));
        uint16_t idx = chunk_add_constant(c->chunk, (JaclVal)raw);
        compiler__emit_byte(c, OP_CONST_F64, line);
        compiler__emit_u16(c, idx, line);
        c->last_expr_type = TYPE_F64;
      } else {
        /* Default: tagged f32 (works for expected TYPE_DYN or TYPE_F32) */
        compiler__emit_constant(c, jacl_f32(node->data.lit_float.value), line);
        c->last_expr_type = TYPE_F32;
      }
      break;
    }

    case AST_LIT_STRING: {
      uint32_t len = node->data.lit_string.length;
      JaclVal val = jacl_string_new(c->heap, c->intern_table,
                                     node->data.lit_string.value, (size_t)len);
      compiler__emit_constant(c, val, line);
      c->last_expr_type = TYPE_STR;
      break;
    }

    case AST_VAR_REF: {
      uint32_t name_len = node->data.var_ref.length;
      if (name_len > 7) {
        compiler__error(c, line, node->start.column,
                        "variable name exceeds 7-byte inline limit");
        break;
      }
      JaclVal name_val = jacl_inline_string(node->data.var_ref.name, name_len);

      int local_slot = compiler__resolve_local(c, name_val);
      if (local_slot != -1) {
        if (c->locals[local_slot].is_mutable) {
          compiler__emit_byte(c, OP_GET_CELL_LOCAL, line);
          compiler__emit_byte(c, (uint8_t)local_slot, line);
          /* Cells store boxed values; unbox if typed */
          JaclType local_type = c->locals[local_slot].type;
          if (is_unboxed_type(local_type)) {
            uint8_t to_op;
            switch (local_type) {
              case TYPE_I64: to_op = OP_TO_I64; break;
              case TYPE_U64: to_op = OP_TO_U64; break;
              case TYPE_F64: to_op = OP_TO_F64; break;
              default: to_op = 0; break;
            }
            if (to_op) {
              compiler__emit_byte(c, to_op, line);
              compiler__emit_byte(c, (uint8_t)TYPE_DYN, line);
            }
          }
        } else {
          compiler__emit_byte(c, OP_GET_LOCAL, line);
          compiler__emit_byte(c, (uint8_t)local_slot, line);
        }
        c->last_expr_type = c->locals[local_slot].type;
        if (c->locals[local_slot].type == TYPE_STRUCT)
          c->last_struct_idx = c->locals[local_slot].struct_type_idx;
      } else {
        int upvalue_idx = compiler__resolve_upvalue(c, name_val);
        if (upvalue_idx != -1) {
          if (c->upvalues[upvalue_idx].is_mutable) {
            compiler__emit_byte(c, OP_GET_CELL_UPVALUE, line);
            compiler__emit_byte(c, (uint8_t)upvalue_idx, line);
            /* Cells store boxed values; unbox if typed */
            JaclType uv_type = c->upvalues[upvalue_idx].type;
            if (is_unboxed_type(uv_type)) {
              uint8_t to_op;
              switch (uv_type) {
                case TYPE_I64: to_op = OP_TO_I64; break;
                case TYPE_U64: to_op = OP_TO_U64; break;
                case TYPE_F64: to_op = OP_TO_F64; break;
                default: to_op = 0; break;
              }
              if (to_op) {
                compiler__emit_byte(c, to_op, line);
                compiler__emit_byte(c, (uint8_t)TYPE_DYN, line);
              }
            }
          } else {
            compiler__emit_byte(c, OP_GET_UPVALUE, line);
            compiler__emit_byte(c, (uint8_t)upvalue_idx, line);
          }
          c->last_expr_type = c->upvalues[upvalue_idx].type;
          if (c->upvalues[upvalue_idx].type == TYPE_STRUCT)
            c->last_struct_idx = c->upvalues[upvalue_idx].struct_type_idx;
        } else {
          GlobalArity* ga = compiler__find_global(c, name_val);
          JaclType global_type = ga ? ga->type : TYPE_DYN;
          JaclVal gkey = compiler__global_name_val(c,
              node->data.var_ref.name, name_len);
          uint16_t name_idx = chunk_add_constant(c->chunk, gkey);
          compiler__emit_byte(c, OP_GET_GLOBAL, line);
          compiler__emit_u16(c, name_idx, line);
          /* Unbox typed globals: globals store tagged JaclVal, convert to raw */
          if (is_unboxed_type(global_type)) {
            uint8_t to_op;
            switch (global_type) {
              case TYPE_I64: to_op = OP_TO_I64; break;
              case TYPE_U64: to_op = OP_TO_U64; break;
              case TYPE_F64: to_op = OP_TO_F64; break;
              default: to_op = 0; break;
            }
            if (to_op) {
              compiler__emit_byte(c, to_op, line);
              compiler__emit_byte(c, (uint8_t)TYPE_DYN, line);
            }
          }
          c->last_expr_type = global_type;
          if (global_type == TYPE_STRUCT) {
            if (ga) c->last_struct_idx = ga->struct_type_idx;
          }
        }
      }
      break;
    }

    case AST_COMMAND: {
      compiler__compile_command(c, node);
      break;
    }

    case AST_BLOCK: {
      compiler__begin_scope(c);
      uint32_t count = node->data.block.count;
      for (uint32_t i = 0; i < count; i++) {
        compiler__compile_node(c, node->data.block.commands[i]);
        compiler__emit_check_error(c, line);
      }
      compiler__end_scope(c, line);
      /* Block evaluates to nil */
      compiler__emit_byte(c, OP_NIL, line);
      break;
    }

    case AST_INTERP_STRING: {
      uint32_t seg_count = node->data.interp_string.count;
      AstNode** segments = node->data.interp_string.segments;

      if (seg_count == 0) {
        /* Empty interpolated string → empty string constant */
        compiler__emit_constant(c, jacl_string_new(c->heap, c->intern_table, "", 0), line);
        c->last_expr_type = TYPE_STR;
        break;
      }

      /* Compile first segment */
      {
        AstNode* seg = segments[0];
        if (seg->type == AST_LIT_STRING) {
          compiler__compile_node(c, seg);
        } else {
          compiler__compile_node(c, seg);
          compiler__ensure_boxed(c, line);
          compiler__emit_byte(c, OP_TO_STRING, line);
        }
      }

      /* Compile remaining segments, each followed by OP_CONCAT */
      for (uint32_t i = 1; i < seg_count; i++) {
        AstNode* seg = segments[i];
        if (seg->type == AST_LIT_STRING) {
          compiler__compile_node(c, seg);
        } else {
          compiler__compile_node(c, seg);
          compiler__ensure_boxed(c, line);
          compiler__emit_byte(c, OP_TO_STRING, line);
        }
        compiler__emit_byte(c, OP_CONCAT, line);
      }
      c->last_expr_type = TYPE_STR;
      break;
    }

    case AST_USE: {
      /* Module import — compile the dependency module if not cached */
      const char* use_path = node->data.use_decl.path;

      /* Resolve path relative to the current module */
      const char* importer_path = c->current_module ? c->current_module->path : NULL;
      if (!importer_path) {
        /* No current module context — cannot resolve relative import */
        compiler__error(c, line, node->start.column,
                        "use declaration requires module context");
        break;
      }

      const char* canonical = module__resolve_path(importer_path, use_path, c->arena);
      if (!canonical) {
        char buf[256];
        snprintf(buf, sizeof(buf), "module not found: \"%s\"", use_path);
        char* msg = (char*)arena_alloc(c->arena, (uint32_t)(strlen(buf) + 1));
        memcpy(msg, buf, strlen(buf) + 1);
        compiler__error(c, line, node->start.column, msg);
        break;
      }

      /* Check circular import */
      if (c->import_stack && import_stack__contains(c->import_stack, canonical)) {
        const char* chain = import_stack__chain_str(c->import_stack, canonical,
                                                     c->arena);
        char buf[512];
        snprintf(buf, sizeof(buf), "circular import detected: %s", chain);
        char* msg = (char*)arena_alloc(c->arena, (uint32_t)(strlen(buf) + 1));
        memcpy(msg, buf, strlen(buf) + 1);
        compiler__error(c, line, node->start.column, msg);
        break;
      }

      /* Check cache — compile if not already compiled */
      Module* dep_mod = c->module_cache
                          ? module_cache__find(c->module_cache, canonical)
                          : NULL;
      if (!dep_mod) {
        /* Compile the dependency module */
        if (!compiler__compile_module(canonical, c, line, node->start.column)) {
          /* Error already reported by compile_module */
          break;
        }
        dep_mod = module_cache__find(c->module_cache, canonical);
        if (!dep_mod) {
          compiler__error(c, line, node->start.column,
                          "internal error: module not in cache after compile");
          break;
        }
      }

      /* Register each imported name as a GlobalArity in this compiler */
      for (uint32_t ni = 0; ni < node->data.use_decl.name_count; ni++) {
        const char* imp_name  = node->data.use_decl.names[ni];
        uint32_t    imp_len   = node->data.use_decl.name_lens[ni];

        /* Find the name in the module's exports */
        ExportEntry* found_export = NULL;
        for (uint32_t ei = 0; ei < dep_mod->export_count; ei++) {
          if (dep_mod->exports[ei].name_len == imp_len &&
              memcmp(dep_mod->exports[ei].name, imp_name, imp_len) == 0) {
            found_export = &dep_mod->exports[ei];
            break;
          }
        }

        if (!found_export) {
          /* Check if it's a private name (parser should catch this,
             but double-check for robustness) */
          char buf[256];
          snprintf(buf, sizeof(buf), "'%.*s' is not exported by '%s'",
                   (int)imp_len, imp_name, use_path);
          char* msg = (char*)arena_alloc(c->arena, (uint32_t)(strlen(buf) + 1));
          memcpy(msg, buf, strlen(buf) + 1);
          compiler__error(c, line, node->start.column, msg);
          continue;
        }

        /* Check for conflict with existing local or global definition */
        if (imp_len <= 7) {
          JaclVal name_val = jacl_inline_string(imp_name, imp_len);

          /* Check conflict with existing global */
          GlobalArity* existing = compiler__find_global(c, name_val);
          if (existing) {
            char buf[256];
            snprintf(buf, sizeof(buf), "'%.*s' is already defined",
                     (int)imp_len, imp_name);
            char* msg = (char*)arena_alloc(c->arena, (uint32_t)(strlen(buf) + 1));
            memcpy(msg, buf, strlen(buf) + 1);
            compiler__error(c, line, node->start.column, msg);
            continue;
          }

          /* Register the import as a GlobalArity with full type info */
          if (c->global_arity_count < COMPILER_GLOBAL_ARITIES_MAX) {
            GlobalArity* ga = &c->global_arities[c->global_arity_count++];
            ga->name              = name_val;
            ga->known_arity       = found_export->arity;
            ga->is_mutable        = found_export->is_mutable;
            ga->suspends          = found_export->suspends;
            ga->captures_mutable  = false;
            ga->type              = found_export->type;
            ga->return_type       = found_export->return_type;
            memcpy(ga->param_types, found_export->param_types,
                   sizeof(ga->param_types));
          }

          /* Struct types are compile-time only (no runtime value).
             Skip runtime bytecode for struct constructors — the shared
             struct_registry handles constructor detection at compile time. */
          if (found_export->type == TYPE_STRUCT &&
              found_export->return_type == TYPE_STRUCT) {
            /* No runtime bytecode needed — struct constructor is
               resolved at compile time via struct_registry */
          } else {
            /* Build dependency module's prefixed name */
            uint32_t dep_prefix_len;
            const char* dep_prefix = module__build_prefix(
                dep_mod->path, c->arena, &dep_prefix_len);
            char dep_buf[256];
            uint32_t dep_total = dep_prefix_len + imp_len;
            if (dep_total >= sizeof(dep_buf)) dep_total = sizeof(dep_buf) - 1;
            memcpy(dep_buf, dep_prefix, dep_prefix_len);
            memcpy(dep_buf + dep_prefix_len, imp_name,
                   dep_total - dep_prefix_len);
            dep_buf[dep_total] = '\0';
            JaclVal dep_key = jacl_intern(c->heap, c->intern_table,
                                           dep_buf, dep_total);
            uint16_t get_idx = chunk_add_constant(c->chunk, dep_key);
            compiler__emit_byte(c, OP_GET_GLOBAL, line);
            compiler__emit_u16(c, get_idx, line);

            /* Define under importing module's prefixed name */
            JaclVal self_key = compiler__global_name_val(c, imp_name, imp_len);
            uint16_t def_idx = chunk_add_constant(c->chunk, self_key);
            compiler__emit_byte(c, OP_DEF_GLOBAL, line);
            compiler__emit_u16(c, def_idx, line);
            /* Pop the nil pushed by OP_DEF_GLOBAL */
            compiler__emit_byte(c, OP_POP, line);
          } /* end else (non-struct runtime bytecode) */
        }
      }
      /* use statement produces nil as its result value */
      compiler__emit_byte(c, OP_NIL, line);
      break;
    }

    case AST_DEFSTRUCT: {
      const char* struct_name     = node->data.defstruct.name;
      uint32_t    struct_name_len = node->data.defstruct.name_len;
      uint32_t    field_count     = node->data.defstruct.field_count;

      /* Must be at top level (scope_depth == 0) — parser already rejects
         defstruct inside blocks, but double-check */
      if (c->scope_depth > 0) {
        compiler__error(c, line, node->start.column,
                        "defstruct must appear at top level");
        break;
      }

      /* Get or create struct registry on the root compiler */
      Compiler* root = c;
      while (root->enclosing) root = root->enclosing;
      if (!root->struct_registry) {
        root->struct_registry = (StructTypeRegistry*)arena_alloc(
            root->arena, sizeof(StructTypeRegistry));
        root->struct_registry->count = 0;
      }
      StructTypeRegistry* reg = root->struct_registry;

      /* Error on duplicate struct name */
      if (struct_registry__find(reg, struct_name, struct_name_len) != UINT32_MAX) {
        char err[128];
        snprintf(err, sizeof(err), "duplicate struct definition '%.*s'",
                 (int)struct_name_len, struct_name);
        compiler__error(c, line, node->start.column, err);
        break;
      }

      /* Check registry capacity */
      if (reg->count >= STRUCT_REGISTRY_MAX) {
        compiler__error(c, line, node->start.column,
                        "too many struct type definitions");
        break;
      }

      /* Reserve slot first so inline struct registration doesn't overwrite it */
      uint32_t this_idx = reg->count;
      reg->count++;
      StructTypeDef* sdef = &reg->defs[this_idx];
      sdef->name     = struct_name;
      sdef->name_len = struct_name_len;
      if (struct_name_len <= 7) {
        sdef->name_val = jacl_inline_string(struct_name, struct_name_len);
      } else {
        sdef->name_val = JACL_NIL;
      }
      sdef->field_count = field_count;

      /* Check for duplicate field names and resolve field types */
      uint32_t offset = 0;
      uint32_t max_align = 1;
      bool has_error = false;

      for (uint32_t fi = 0; fi < field_count; fi++) {
        const char* fname     = node->data.defstruct.field_names[fi];
        uint32_t    fname_len = node->data.defstruct.field_name_lens[fi];
        const char* ftype_str = node->data.defstruct.field_types[fi];
        uint32_t    ftype_len = node->data.defstruct.field_type_lens[fi];

        /* Check for duplicate field names */
        for (uint32_t j = 0; j < fi; j++) {
          if (sdef->fields[j].name_len == fname_len &&
              memcmp(sdef->fields[j].name, fname, fname_len) == 0) {
            char err[128];
            snprintf(err, sizeof(err), "duplicate field name '%.*s' in struct '%.*s'",
                     (int)fname_len, fname, (int)struct_name_len, struct_name);
            compiler__error(c, line, node->start.column, err);
            has_error = true;
            break;
          }
        }
        if (has_error) break;

        /* Resolve field type */
        JaclType ftype = TYPE_DYN;
        uint32_t f_struct_idx = 0;

        if (is_type_keyword(ftype_str, ftype_len)) {
          ftype = type_from_keyword(ftype_str, ftype_len);
        } else if (ftype_len > 7 && memcmp(ftype_str, "struct{", 7) == 0) {
          /* Inline anonymous struct type */
          uint32_t idx = compiler__register_inline_struct(reg, ftype_str, ftype_len);
          if (idx == UINT32_MAX) {
            char err[128];
            snprintf(err, sizeof(err),
                     "invalid inline struct type for field '%.*s' in struct '%.*s'",
                     (int)fname_len, fname,
                     (int)struct_name_len, struct_name);
            compiler__error(c, line, node->start.column, err);
            has_error = true;
            break;
          }
          ftype = TYPE_STRUCT;
          f_struct_idx = idx;
        } else {
          /* Check if it's a named struct type */
          uint32_t idx = struct_registry__find(reg, ftype_str, ftype_len);
          if (idx == UINT32_MAX) {
            char err[128];
            snprintf(err, sizeof(err),
                     "undefined type '%.*s' for field '%.*s' in struct '%.*s'",
                     (int)ftype_len, ftype_str,
                     (int)fname_len, fname,
                     (int)struct_name_len, struct_name);
            compiler__error(c, line, node->start.column, err);
            has_error = true;
            break;
          }
          ftype = TYPE_STRUCT;
          f_struct_idx = idx;
        }

        /* Compute C-ABI layout */
        uint32_t fsize  = struct__type_size(ftype, reg, f_struct_idx);
        uint32_t falign = struct__type_align(ftype, reg, f_struct_idx);
        offset = struct__align_up(offset, falign);

        sdef->fields[fi].name           = fname;
        sdef->fields[fi].name_len       = fname_len;
        sdef->fields[fi].type           = ftype;
        sdef->fields[fi].struct_type_idx = f_struct_idx;
        sdef->fields[fi].offset         = offset;
        sdef->fields[fi].size           = fsize;

        offset += fsize;
        if (falign > max_align) max_align = falign;
      }

      if (has_error) {
        reg->count = this_idx; /* rollback slot reservation */
        break;
      }

      /* Trailing padding to struct alignment */
      sdef->total_size = struct__align_up(offset, max_align);
      sdef->alignment  = max_align;

      /* Slot was already reserved above (reg->count++) */

      /* Register struct name as a global with arity = field_count (constructor)
         and type = TYPE_STRUCT */
      if (struct_name_len <= 7) {
        JaclVal name_val = sdef->name_val;
        compiler__set_global_arity(root, name_val, (int16_t)field_count);
        GlobalArity* ga = compiler__find_global(root, name_val);
        if (ga) {
          ga->type = TYPE_STRUCT;
          ga->return_type = TYPE_STRUCT;
        }
      }

      /* defstruct is a declaration, produces nil */
      compiler__emit_byte(c, OP_NIL, line);
      break;
    }

    case AST_BREAK: {
      if (c->loop_depth == 0) {
        compiler__error(c, line, node->start.column,
                        "break outside of loop");
        break;
      }
      LoopContext* lctx = &c->loop_stack[c->loop_depth - 1];
      /* Compile break value (or nil) */
      if (node->data.break_stmt.value) {
        compiler__compile_node(c, node->data.break_stmt.value);
      } else {
        compiler__emit_byte(c, OP_NIL, line);
      }
      /* For inlined for-loops, clean up hidden locals under the break value */
      if (lctx->is_for_loop) {
        uint32_t cleanup = c->local_count - lctx->local_count_at_loop;
        if (cleanup > 0) {
          compiler__emit_byte(c, OP_CLOSE_LOOP, line);
          compiler__emit_byte(c, (uint8_t)cleanup, line);
        }
      }
      /* Emit forward jump to be patched at loop exit */
      if (lctx->break_patch_count < COMPILER_BREAK_PATCHES_MAX) {
        lctx->break_patches[lctx->break_patch_count++] =
            compiler__emit_jump(c, OP_JUMP, line);
      } else {
        compiler__error(c, line, node->start.column,
                        "too many break statements in loop");
      }
      break;
    }

    case AST_CONTINUE: {
      if (c->loop_depth == 0) {
        compiler__error(c, line, node->start.column,
                        "continue outside of loop");
        break;
      }
      LoopContext* lctx = &c->loop_stack[c->loop_depth - 1];
      if (lctx->is_for_loop) {
        /* For-loop: forward-jump to increment code (patched later) */
        if (lctx->continue_patch_count < COMPILER_CONTINUE_PATCHES_MAX) {
          lctx->continue_patches[lctx->continue_patch_count++] =
              compiler__emit_jump(c, OP_JUMP, line);
        } else {
          compiler__error(c, line, node->start.column,
                          "too many continue statements in loop");
        }
      } else {
        /* While-loop: pop body-scope locals, then backward-jump to condition */
        uint32_t cleanup = c->local_count - lctx->local_count_at_loop;
        if (cleanup > 0) {
          compiler__emit_byte(c, OP_POP_N, line);
          compiler__emit_byte(c, (uint8_t)cleanup, line);
        }
        compiler__emit_byte(c, OP_LOOP, line);
        uint32_t offset = c->chunk->code_count - lctx->loop_start + 2;
        compiler__emit_byte(c, (uint8_t)((offset >> 8) & 0xFF), line);
        compiler__emit_byte(c, (uint8_t)(offset & 0xFF), line);
      }
      break;
    }

    case AST_RETURN: {
      /* Compile return value (or nil) */
      if (node->data.return_stmt.value) {
        compiler__compile_node(c, node->data.return_stmt.value);
      } else {
        compiler__emit_byte(c, OP_NIL, line);
      }
      compiler__emit_byte(c, OP_RETURN, line);
      break;
    }

    case AST_DESTRUCTURE_VEC: {
      compiler__error(c, line, node->start.column,
                      "destructuring pattern can only appear in def or mut");
      break;
    }

    case AST_DESTRUCTURE_NAMED: {
      compiler__error(c, line, node->start.column,
                      "destructuring pattern can only appear in def or mut");
      break;
    }

    case AST_SPREAD: {
      compiler__error(c, line, node->start.column,
                      "spread expression can only appear inside command arguments");
      break;
    }

    case AST_ERROR: {
      compiler__error(c, line, node->start.column, "parse error in AST");
      break;
    }
  }
}

/* --- Public API --- */

/**
 * Check if any top-level statement is suspending (uses await/parallel/race
 * or calls a suspending proc). Used to decide if top-level CPS is needed.
 */
static bool compiler__top_level_suspends(AstNode** stmts, uint32_t count,
                                          SuspensionMap* map) {
  for (uint32_t i = 0; i < count; i++) {
    if (ast__contains_suspension(stmts[i], map)) return true;
  }
  return false;
}

static CompileResult compiler_compile(ParseResult parse, arena_t* arena,
                                      JaclInternTable* intern_table,
                                      ThreadHeap* heap,
                                      StructTypeRegistry* seed_registry) {
  CompileResult result;
  chunk_init(&result.chunk, arena);
  result.error_count = parse.error_count;
  result.suspending  = false;

  /* Pre-compilation suspension analysis */
  SuspensionMap suspension_map = compiler__analyze_suspension(
      parse.nodes, parse.count);

  Compiler c;
  compiler__init(&c, &result.chunk, arena, intern_table, heap);
  c.suspension_map = &suspension_map;
  {
    StructTypeRegistry* reg = (StructTypeRegistry*)arena_alloc(arena, sizeof(StructTypeRegistry));
    if (seed_registry) {
      *reg = *seed_registry;  /* seed with accumulated struct defs */
    } else {
      reg->count = 0;
    }
    c.struct_registry = reg;
  }

  /* Check if top-level code is suspending */
  bool top_suspends = compiler__top_level_suspends(
      parse.nodes, parse.count, &suspension_map);

  if (top_suspends) {
    /* CPS-transform top-level code into a __main closure with __k parameter.
     * Proc definitions use force_global_procs so they remain globals. */

    /* Phase 1: Register all proc global arities FIRST so CPS body can
       resolve suspension status of proc calls. */
    for (uint32_t i = 0; i < parse.count; i++) {
      AstNode* node = parse.nodes[i];
      if (node->type == AST_COMMAND &&
          compiler__head_matches(node->data.command.head, "proc", 4)) {
        AstNode** pargs = node->data.command.args;
        uint32_t pargc = node->data.command.arg_count;
        if (pargc >= 3) {
          AstNode* name_node = pargs[0];
          if (name_node->type == AST_LIT_STRING && name_node->data.lit_string.length <= 7) {
            JaclVal pname = jacl_inline_string(
                name_node->data.lit_string.value, name_node->data.lit_string.length);
            /* Count user params from the param list (head + args) */
            AstNode* param_list = pargs[1];
            int16_t pcount = 0;
            if (param_list->type == AST_COMMAND) {
              AstNode* phead = param_list->data.command.head;
              if (phead && phead->type == AST_LIT_STRING &&
                  phead->data.lit_string.length > 0) {
                pcount = 1 + (int16_t)param_list->data.command.arg_count;
              }
            }
            compiler__set_global_arity(&c, pname, pcount);
            GlobalArity* ga = compiler__find_global(&c, pname);
            if (ga) {
              ga->suspends = suspension_map_lookup(&suspension_map, pname);
            }
          }
        }
      }
    }

    /* Phase 1b: Pre-register top-level mut declarations so proc bodies
       compiled in Phase 2 can resolve mutable globals (needed for set!
       inside spawn/parallel bodies that pin to the parent worker). */
    for (uint32_t i = 0; i < parse.count; i++) {
      AstNode* node = parse.nodes[i];
      if (node->type == AST_COMMAND &&
          compiler__head_matches(node->data.command.head, "mut", 3)) {
        uint32_t margc = node->data.command.arg_count;
        if (margc >= 2) {
          /* mut [type] name value — name is last-but-one arg */
          AstNode* name_node = node->data.command.args[margc >= 3 ? 1 : 0];
          if (name_node->type == AST_LIT_STRING &&
              name_node->data.lit_string.length <= 7) {
            JaclVal mname = jacl_inline_string(
                name_node->data.lit_string.value,
                name_node->data.lit_string.length);
            compiler__set_global_arity(&c, mname, -1);
            GlobalArity* ga = compiler__find_global(&c, mname);
            if (ga) {
              ga->is_mutable = true;
            }
          }
        }
      }
    }

    /* Phase 2: Hoist top-level proc definitions into the outer chunk so
       they are defined via OP_SET_GLOBAL before the CPS closure executes.
       This ensures all workers have proc definitions in their env when
       running in concurrent mode. */
    uint32_t non_proc_count = 0;
    AstNode** non_proc_stmts = (AstNode**)arena_alloc(arena,
        parse.count * sizeof(AstNode*));
    for (uint32_t i = 0; i < parse.count; i++) {
      AstNode* node = parse.nodes[i];
      if (node->type == AST_COMMAND &&
          compiler__head_matches(node->data.command.head, "proc", 4)) {
        /* Compile proc definition into outer (top-level) chunk */
        compiler__compile_node(&c, node);
        compiler__emit_check_error(&c, node->start.line);
      } else {
        non_proc_stmts[non_proc_count++] = node;
      }
    }

    /* Phase 3: Create __main CPS closure (only non-proc statements) */
    JaclClosure* main_cl = (JaclClosure*)arena_alloc(arena, sizeof(JaclClosure));
    chunk_init(&main_cl->chunk, arena);
    main_cl->param_count   = 1; /* __k */
    main_cl->upvalue_count = 0;
    main_cl->upvalues      = NULL;
    main_cl->name          = "__main";
    main_cl->min_args      = 1;
    main_cl->variadic      = false;
    main_cl->pinned        = false;
    main_cl->pin_worker_id = -1;
    JaclVal* main_pnames   = (JaclVal*)arena_alloc(arena, sizeof(JaclVal));
    main_pnames[0]         = jacl_inline_string("__k", 3);
    main_cl->param_names   = main_pnames;

    Compiler body;
    compiler__init(&body, &main_cl->chunk, arena, intern_table, heap);
    body.scope_depth       = 1;
    body.enclosing         = &c;
    body.suspension_map    = &suspension_map;
    body.is_cps            = true;
    body.force_global_procs = true;

    /* Copy global arities */
    memcpy(body.global_arities, c.global_arities,
           sizeof(GlobalArity) * c.global_arity_count);
    body.global_arity_count = c.global_arity_count;

    /* Add __k as local slot 0 */
    compiler__add_local(&body, jacl_inline_string("__k", 3), 1, 0);
    body.locals[body.local_count - 1].is_param = true;

    /* Compile non-proc stmts through CPS */
    compiler__compile_cps_stmts(&body, non_proc_stmts, non_proc_count, 1);
    compiler__emit_byte(&body, OP_RETURN, 1);

    /* Propagate errors */
    c.error_count += body.error_count;
    if (!c.first_error && body.first_error) {
      c.first_error = body.first_error;
    }

    main_cl->upvalue_count = (uint8_t)body.upvalue_count;

    /* Emit OP_CLOSURE in outer chunk */
    uint16_t cl_idx = chunk_add_constant(&result.chunk, jacl_closure(main_cl));
    compiler__emit_byte(&c, OP_CLOSURE, 1);
    compiler__emit_u16(&c, cl_idx, 1);
    for (uint32_t i = 0; i < body.upvalue_count; i++) {
      compiler__emit_byte(&c, body.upvalues[i].is_local, 1);
      compiler__emit_byte(&c, body.upvalues[i].index, 1);
    }
    compiler__emit_byte(&c, OP_HALT, 1);

    result.suspending = true;
  } else {
    /* Normal non-suspending top-level compilation */
    for (uint32_t i = 0; i < parse.count; i++) {
      compiler__compile_node(&c, parse.nodes[i]);

      /* Emit OP_CHECK_ERROR between statements: auto-return on error */
      if (i < parse.count - 1) {
        compiler__emit_check_error(&c, parse.nodes[i]->start.line);
      }
    }

    compiler__emit_byte(&c, OP_HALT,
                        parse.count > 0 ? parse.nodes[parse.count - 1]->start.line : 1);
  }

  result.error_count  += c.error_count;
  result.error_message = c.first_error;
  result.struct_registry = c.struct_registry;
  return result;
}

/* --- Module compilation --- */

/* Populate a Module's export list from the compiler's global_arities,
   excluding underscore-prefixed (private) names. */
static void module__populate_exports(Module* mod, Compiler* c) {
  /* Count non-private globals */
  uint32_t count = 0;
  for (uint32_t i = 0; i < c->global_arity_count; i++) {
    char name_buf[8];
    jacl_inline_string_get(c->global_arities[i].name, name_buf, sizeof(name_buf));
    size_t name_len = jacl_inline_string_len(c->global_arities[i].name);
    if (!module__is_private(name_buf, (uint32_t)name_len)) {
      count++;
    }
  }

  if (count == 0) {
    mod->exports = NULL;
    mod->export_count = 0;
    return;
  }

  mod->exports = (ExportEntry*)arena_alloc(c->arena, count * sizeof(ExportEntry));
  mod->export_count = 0;

  for (uint32_t i = 0; i < c->global_arity_count; i++) {
    char name_buf[8];
    jacl_inline_string_get(c->global_arities[i].name, name_buf, sizeof(name_buf));
    size_t name_len = jacl_inline_string_len(c->global_arities[i].name);
    if (module__is_private(name_buf, (uint32_t)name_len)) continue;

    ExportEntry* e = &mod->exports[mod->export_count++];
    /* Arena-copy the name so it outlives the stack buffer */
    char* stored_name = (char*)arena_alloc(c->arena, (uint32_t)(name_len + 1));
    memcpy(stored_name, name_buf, name_len);
    stored_name[name_len] = '\0';
    e->name       = stored_name;
    e->name_len   = (uint32_t)name_len;
    e->arity      = c->global_arities[i].known_arity;
    e->is_mutable = c->global_arities[i].is_mutable;
    e->suspends   = c->global_arities[i].suspends;
    e->type       = c->global_arities[i].type;
    e->return_type = c->global_arities[i].return_type;
    memcpy(e->param_types, c->global_arities[i].param_types, sizeof(e->param_types));
    e->param_count = (c->global_arities[i].known_arity >= 0) ?
                     (uint32_t)c->global_arities[i].known_arity : 0;
  }
}

/* Compile a module from a canonical file path.
   Reads the file, lexes, parses, and compiles into a new Module in the cache.
   Returns true on success, false on error (error reported via importer). */
static bool compiler__compile_module(const char* canonical_path,
                                     Compiler* importer,
                                     uint32_t line, uint32_t col) {
  arena_t* arena = importer->arena;

  /* Read source file */
  char* source = module__read_file(canonical_path, arena);
  if (!source) {
    char buf[256];
    snprintf(buf, sizeof(buf), "could not read module: \"%s\"", canonical_path);
    char* msg = (char*)arena_alloc(arena, (uint32_t)(strlen(buf) + 1));
    memcpy(msg, buf, strlen(buf) + 1);
    compiler__error(importer, line, col, msg);
    return false;
  }

  /* Create module in cache */
  Module* mod = module_cache__add(importer->module_cache, canonical_path);
  if (!mod) {
    compiler__error(importer, line, col, "too many modules (cache full)");
    return false;
  }
  mod->source = source;

  /* Push onto import stack for circular detection */
  import_stack__push(importer->import_stack, canonical_path);

  /* Lex and parse */
  LexResult tokens = lexer_lex(source, arena);
  ParseResult parse = parser_parse(tokens, arena);
  if (parse.error_count > 0) {
    char buf[256];
    snprintf(buf, sizeof(buf), "parse error in module \"%s\"", canonical_path);
    char* msg = (char*)arena_alloc(arena, (uint32_t)(strlen(buf) + 1));
    memcpy(msg, buf, strlen(buf) + 1);
    compiler__error(importer, line, col, msg);
    import_stack__pop(importer->import_stack);
    return false;
  }

  /* Suspension analysis */
  SuspensionMap suspension_map = compiler__analyze_suspension(
      parse.nodes, parse.count);

  /* Compile into a new chunk */
  BytecodeChunk* chunk = (BytecodeChunk*)arena_alloc(arena, sizeof(BytecodeChunk));
  chunk_init(chunk, arena);

  Compiler mc;
  compiler__init(&mc, chunk, arena, importer->intern_table, importer->heap);
  mc.suspension_map  = &suspension_map;
  mc.module_cache    = importer->module_cache;
  mc.current_module  = mod;
  mc.import_stack    = importer->import_stack;
  {
    /* Share struct registry from importer root with module compiler */
    Compiler* imp_root = importer;
    while (imp_root->enclosing) imp_root = imp_root->enclosing;
    mc.struct_registry = imp_root->struct_registry;
  }
  mc.module_prefix   = module__build_prefix(canonical_path, arena,
                                              &mc.module_prefix_len);

  /* Compile all top-level statements */
  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&mc, parse.nodes[i]);
    if (i < parse.count - 1) {
      compiler__emit_check_error(&mc, parse.nodes[i]->start.line);
    }
  }

  /* Every module chunk ends with OP_HALT */
  compiler__emit_byte(&mc, OP_HALT,
                      parse.count > 0 ? parse.nodes[parse.count - 1]->start.line : 1);

  /* Populate exports from global arities (exclude private names) */
  module__populate_exports(mod, &mc);

  mod->chunk      = chunk;
  mod->compiled   = true;
  mod->topo_order = importer->module_cache->topo_counter++;

  /* Pop import stack */
  import_stack__pop(importer->import_stack);

  /* Propagate errors to importer */
  if (mc.error_count > 0) {
    importer->error_count += mc.error_count;
    if (!importer->first_error && mc.first_error) {
      importer->first_error = mc.first_error;
    }
    return false;
  }

  return true;
}

/* --- Multi-file program compilation API --- */

/* Compile a program starting from a root file.
   Recursively compiles all dependency modules and returns them in
   topological order (dependencies first, root module last). */
static ProgramResult jacl_compile_program(const char* root_path,
                                          arena_t* arena,
                                          JaclInternTable* intern_table,
                                          ThreadHeap* heap) {
  ProgramResult result;
  memset(&result, 0, sizeof(result));

  /* Canonicalize root path */
  char resolved[1024];
  if (!realpath(root_path, resolved)) {
    result.error_count = 1;
    char buf[256];
    snprintf(buf, sizeof(buf), "module not found: \"%s\"", root_path);
    char* msg = (char*)arena_alloc(arena, (uint32_t)(strlen(buf) + 1));
    memcpy(msg, buf, strlen(buf) + 1);
    result.error_message = msg;
    return result;
  }
  char* canonical = (char*)arena_alloc(arena, (uint32_t)(strlen(resolved) + 1));
  memcpy(canonical, resolved, strlen(resolved) + 1);

  /* Read source file */
  char* source = module__read_file(canonical, arena);
  if (!source) {
    result.error_count = 1;
    char buf[256];
    snprintf(buf, sizeof(buf), "could not read module: \"%s\"", root_path);
    char* msg = (char*)arena_alloc(arena, (uint32_t)(strlen(buf) + 1));
    memcpy(msg, buf, strlen(buf) + 1);
    result.error_message = msg;
    return result;
  }

  /* Set up module infrastructure */
  ModuleCache cache;
  module_cache__init(&cache, arena);
  ImportStack istack;
  import_stack__init(&istack);

  /* Create root module in cache */
  Module* root_mod = module_cache__add(&cache, canonical);
  root_mod->source = source;

  import_stack__push(&istack, canonical);

  /* Lex and parse */
  LexResult tokens = lexer_lex(source, arena);
  ParseResult parse = parser_parse(tokens, arena);
  if (parse.error_count > 0) {
    result.error_count = parse.error_count;
    /* Extract first parse error message from AST_ERROR nodes */
    const char* parse_err = NULL;
    for (uint32_t i = 0; i < parse.count && !parse_err; i++) {
      if (parse.nodes[i]->type == AST_ERROR) {
        parse_err = parse.nodes[i]->data.error.message;
      }
    }
    result.error_message = parse_err ? parse_err : "parse error in root module";
    return result;
  }

  /* Suspension analysis */
  SuspensionMap suspension_map = compiler__analyze_suspension(
      parse.nodes, parse.count);
  bool top_suspends = compiler__top_level_suspends(
      parse.nodes, parse.count, &suspension_map);

  /* Create root module chunk */
  BytecodeChunk* root_chunk = (BytecodeChunk*)arena_alloc(
      arena, sizeof(BytecodeChunk));
  chunk_init(root_chunk, arena);

  /* Initialize compiler with module context */
  Compiler c;
  compiler__init(&c, root_chunk, arena, intern_table, heap);
  c.suspension_map = &suspension_map;
  c.module_cache   = &cache;
  c.current_module = root_mod;
  c.import_stack   = &istack;
  c.module_prefix  = module__build_prefix(canonical, arena, &c.module_prefix_len);
  {
    StructTypeRegistry* reg = (StructTypeRegistry*)arena_alloc(arena, sizeof(StructTypeRegistry));
    reg->count = 0;
    c.struct_registry = reg;
  }

  if (top_suspends) {
    /* CPS-transform top-level code — same logic as compiler_compile */

    /* Phase 1: Register proc global arities */
    for (uint32_t i = 0; i < parse.count; i++) {
      AstNode* node = parse.nodes[i];
      if (node->type == AST_COMMAND &&
          compiler__head_matches(node->data.command.head, "proc", 4)) {
        AstNode** pargs = node->data.command.args;
        uint32_t pargc = node->data.command.arg_count;
        if (pargc >= 3) {
          AstNode* name_node = pargs[0];
          if (name_node->type == AST_LIT_STRING &&
              name_node->data.lit_string.length <= 7) {
            JaclVal pname = jacl_inline_string(
                name_node->data.lit_string.value,
                name_node->data.lit_string.length);
            AstNode* param_list = pargs[1];
            int16_t pcount = 0;
            if (param_list->type == AST_COMMAND) {
              AstNode* phead = param_list->data.command.head;
              if (phead && phead->type == AST_LIT_STRING &&
                  phead->data.lit_string.length > 0) {
                pcount = 1 + (int16_t)param_list->data.command.arg_count;
              }
            }
            compiler__set_global_arity(&c, pname, pcount);
            GlobalArity* ga = compiler__find_global(&c, pname);
            if (ga) {
              ga->suspends = suspension_map_lookup(&suspension_map, pname);
            }
          }
        }
      }
    }

    /* Phase 1b: Pre-register mut declarations */
    for (uint32_t i = 0; i < parse.count; i++) {
      AstNode* node = parse.nodes[i];
      if (node->type == AST_COMMAND &&
          compiler__head_matches(node->data.command.head, "mut", 3)) {
        uint32_t margc = node->data.command.arg_count;
        if (margc >= 2) {
          AstNode* name_node = node->data.command.args[margc >= 3 ? 1 : 0];
          if (name_node->type == AST_LIT_STRING &&
              name_node->data.lit_string.length <= 7) {
            JaclVal mname = jacl_inline_string(
                name_node->data.lit_string.value,
                name_node->data.lit_string.length);
            compiler__set_global_arity(&c, mname, -1);
            GlobalArity* ga = compiler__find_global(&c, mname);
            if (ga) {
              ga->is_mutable = true;
            }
          }
        }
      }
    }

    /* Phase 2: Hoist proc definitions, collect non-proc stmts */
    uint32_t non_proc_count = 0;
    AstNode** non_proc_stmts = (AstNode**)arena_alloc(arena,
        parse.count * sizeof(AstNode*));
    for (uint32_t i = 0; i < parse.count; i++) {
      AstNode* node = parse.nodes[i];
      if ((node->type == AST_COMMAND &&
           compiler__head_matches(node->data.command.head, "proc", 4)) ||
          node->type == AST_USE) {
        compiler__compile_node(&c, node);
        compiler__emit_check_error(&c, node->start.line);
      } else {
        non_proc_stmts[non_proc_count++] = node;
      }
    }

    /* Phase 3: Create __main CPS closure */
    JaclClosure* main_cl = (JaclClosure*)arena_alloc(arena, sizeof(JaclClosure));
    chunk_init(&main_cl->chunk, arena);
    main_cl->param_count   = 1;
    main_cl->upvalue_count = 0;
    main_cl->upvalues      = NULL;
    main_cl->name          = "__main";
    main_cl->min_args      = 1;
    main_cl->variadic      = false;
    main_cl->pinned        = false;
    main_cl->pin_worker_id = -1;
    JaclVal* main_pnames   = (JaclVal*)arena_alloc(arena, sizeof(JaclVal));
    main_pnames[0]         = jacl_inline_string("__k", 3);
    main_cl->param_names   = main_pnames;

    Compiler body;
    compiler__init(&body, &main_cl->chunk, arena, intern_table, heap);
    body.scope_depth       = 1;
    body.enclosing         = &c;
    body.suspension_map    = &suspension_map;
    body.is_cps            = true;
    body.force_global_procs = true;
    body.module_cache      = &cache;
    body.current_module    = root_mod;
    body.import_stack      = &istack;

    memcpy(body.global_arities, c.global_arities,
           sizeof(GlobalArity) * c.global_arity_count);
    body.global_arity_count = c.global_arity_count;

    compiler__add_local(&body, jacl_inline_string("__k", 3), 1, 0);
    body.locals[body.local_count - 1].is_param = true;

    compiler__compile_cps_stmts(&body, non_proc_stmts, non_proc_count, 1);
    compiler__emit_byte(&body, OP_RETURN, 1);

    c.error_count += body.error_count;
    if (!c.first_error && body.first_error) {
      c.first_error = body.first_error;
    }

    main_cl->upvalue_count = (uint8_t)body.upvalue_count;

    uint16_t cl_idx = chunk_add_constant(root_chunk, jacl_closure(main_cl));
    compiler__emit_byte(&c, OP_CLOSURE, 1);
    compiler__emit_u16(&c, cl_idx, 1);
    for (uint32_t i = 0; i < body.upvalue_count; i++) {
      compiler__emit_byte(&c, body.upvalues[i].is_local, 1);
      compiler__emit_byte(&c, body.upvalues[i].index, 1);
    }
    compiler__emit_byte(&c, OP_HALT, 1);

    result.suspending = true;
  } else {
    /* Normal non-suspending compilation */
    for (uint32_t i = 0; i < parse.count; i++) {
      compiler__compile_node(&c, parse.nodes[i]);
      if (i < parse.count - 1) {
        compiler__emit_check_error(&c, parse.nodes[i]->start.line);
      }
    }
    compiler__emit_byte(&c, OP_HALT,
        parse.count > 0 ? parse.nodes[parse.count - 1]->start.line : 1);
  }

  /* Populate root module exports and finalize */
  module__populate_exports(root_mod, &c);
  root_mod->chunk      = root_chunk;
  root_mod->compiled   = true;
  root_mod->topo_order = cache.topo_counter++;

  import_stack__pop(&istack);

  /* Build topological module list: dependencies first, root last.
     Modules have topo_order assigned in DFS post-order during compilation,
     so sorting by topo_order gives correct dependency order. */
  result.module_count = cache.count;
  result.modules = (Module**)arena_alloc(arena,
      cache.count * sizeof(Module*));

  for (uint32_t i = 0; i < cache.count; i++) {
    result.modules[i] = cache.modules[i];
  }
  /* Insertion sort by topo_order (small N, arena-allocated) */
  for (uint32_t i = 1; i < cache.count; i++) {
    Module* key = result.modules[i];
    uint32_t j = i;
    while (j > 0 && result.modules[j - 1]->topo_order > key->topo_order) {
      result.modules[j] = result.modules[j - 1];
      j--;
    }
    result.modules[j] = key;
  }

  result.error_count   = c.error_count;
  result.error_message = c.first_error;
  result.struct_registry = c.struct_registry;
  return result;
}

#endif /* COMPILER_C */
