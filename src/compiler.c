/*
 * JACL Compiler
 *
 * Translates AST (from parser) into bytecode chunks for the VM.
 */

#ifndef COMPILER_C
#define COMPILER_C

#include <string.h>

/* --- Compile Result --- */

typedef struct {
  BytecodeChunk chunk;
  uint32_t      error_count;
  const char*   error_message;  /* first error message, or NULL */
  bool          suspending;     /* true if top-level code is CPS-transformed */
} CompileResult;

/* --- API --- */

static CompileResult compiler_compile(ParseResult parse, arena_t* arena,
                                      JaclInternTable* intern_table,
                                      ThreadHeap* heap);

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
  TYPE_CLOSURE
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
} Upvalue;

/* --- Internal: Suspension analysis --- */

#define SUSPENSION_MAP_MAX 256
#define SUSPENSION_CALLEES_MAX 64

typedef struct {
  JaclVal name;
  bool    suspends;
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

static void suspension_map_set(SuspensionMap* map, JaclVal name, bool suspends) {
  for (uint32_t i = 0; i < map->count; i++) {
    if (map->entries[i].name == name) {
      map->entries[i].suspends = suspends;
      return;
    }
  }
  if (map->count < SUSPENSION_MAP_MAX) {
    map->entries[map->count].name = name;
    map->entries[map->count].suspends = suspends;
    map->count++;
  }
}

/* Info collected per proc during suspension analysis */
typedef struct {
  JaclVal  name;
  bool     direct_suspends;   /* directly contains await/parallel/race */
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
                       proc_list.procs[i].direct_suspends);
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

      /* Rule 1: direct call to suspending proc */
      for (uint32_t j = 0; j < proc_list.procs[i].callee_count; j++) {
        if (suspension_map_lookup(&map, proc_list.procs[i].callees[j])) {
          suspension_map_set(&map, proc_list.procs[i].name, true);
          changed = true;
          break;
        }
      }

      /* Rule 2: indirect call when suspending procs exist */
      if (any_suspending &&
          !suspension_map_lookup(&map, proc_list.procs[i].name) &&
          proc_list.procs[i].has_indirect_call) {
        suspension_map_set(&map, proc_list.procs[i].name, true);
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
            (len == 4 && memcmp(name, "race", 4) == 0)) {
          return true;
        }
        /* Don't recurse into nested proc or spawn definitions
           (their block args are separate closure scopes) */
        if ((len == 4 && memcmp(name, "proc", 4) == 0) ||
            (len == 5 && memcmp(name, "spawn", 5) == 0)) {
          return false;
        }
        /* Check if this is a call to a known suspending proc */
        if (map && len <= 7) {
          JaclVal name_val = jacl_inline_string(name, len);
          if (suspension_map_lookup(map, name_val)) return true;
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
        if (len == 4 && memcmp(name, "set!", 4) == 0) {
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
  JaclType         return_type;     /* declared return type for current function */
  ModuleCache*     module_cache;    /* shared cache of compiled modules */
  Module*          current_module;  /* module currently being compiled */
  ImportStack*     import_stack;    /* shared import stack for circular detection */
  const char*      module_prefix;   /* "basename::" for namespace-prefixed globals */
  uint32_t         module_prefix_len;
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
  c->return_type     = TYPE_DYN;
  c->module_cache    = NULL;
  c->current_module  = NULL;
  c->import_stack    = NULL;
  c->module_prefix     = NULL;
  c->module_prefix_len = 0;
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

static int16_t compiler__resolve_global_arity(Compiler* c, JaclVal name) {
  /* Walk to root compiler which holds global arity info */
  Compiler* root = c;
  while (root->enclosing) root = root->enclosing;
  for (uint32_t i = 0; i < root->global_arity_count; i++) {
    if (root->global_arities[i].name == name) {
      return root->global_arities[i].known_arity;
    }
  }
  return -1;
}

static bool compiler__resolve_global_info(Compiler* c, JaclVal name,
                                           bool* is_mutable) {
  Compiler* root = c;
  while (root->enclosing) root = root->enclosing;
  for (uint32_t i = 0; i < root->global_arity_count; i++) {
    if (root->global_arities[i].name == name) {
      *is_mutable = root->global_arities[i].is_mutable;
      return true;
    }
  }
  return false;
}

static JaclType compiler__resolve_global_type(Compiler* c, JaclVal name) {
  Compiler* root = c;
  while (root->enclosing) root = root->enclosing;
  for (uint32_t i = 0; i < root->global_arity_count; i++) {
    if (root->global_arities[i].name == name) {
      return root->global_arities[i].type;
    }
  }
  return TYPE_DYN;
}

static GlobalArity* compiler__find_global_arity(Compiler* c, JaclVal name) {
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
  GlobalArity* ga = compiler__find_global_arity(enclosing, name);
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
          (len == 4 && memcmp(name, "race", 4) == 0)) {
        return true;
      }

      /* Skip nested proc definitions — they have their own CPS */
      if (len == 4 && memcmp(name, "proc", 4) == 0) {
        return false;
      }

      /* Call to known-suspending proc */
      if (len <= 7 && c->suspension_map) {
        JaclVal name_val = jacl_inline_string(name, len);
        /* Check locals first */
        int slot = compiler__resolve_local(c, name_val);
        if (slot != -1 && c->locals[slot].suspends) return true;
        /* Check globals */
        GlobalArity* ga = compiler__find_global_arity(c, name_val);
        if (ga && ga->suspends) return true;
        /* Check suspension map */
        if (suspension_map_lookup(c->suspension_map, name_val)) return true;
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
    /* Skip built-in suspension points handled separately */
    if ((len == 5 && memcmp(name, "await", 5) == 0) ||
        (len == 8 && memcmp(name, "parallel", 8) == 0) ||
        (len == 4 && memcmp(name, "race", 4) == 0) ||
        (len == 3 && memcmp(name, "def", 3) == 0) ||
        (len == 4 && memcmp(name, "proc", 4) == 0)) {
      return false;
    }
    if (len <= 7 && c->suspension_map) {
      JaclVal name_val = jacl_inline_string(name, len);
      int slot = compiler__resolve_local(c, name_val);
      if (slot != -1 && c->locals[slot].suspends) return true;
      GlobalArity* ga = compiler__find_global_arity(c, name_val);
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
  AstNode** args = cmd_node->data.command.args;

  /* Count extractable suspending args */
  uint32_t susp_count = 0;
  for (uint32_t i = 0; i < argc; i++) {
    if (args[i]->type != AST_BLOCK &&
        compiler__node_is_suspension(c, args[i])) {
      susp_count++;
    }
  }

  /* Build synthetic statement list: defs + modified_cmd + remaining */
  uint32_t total = susp_count + 1 + remaining_count;
  AstNode** new_stmts = ast_alloc_array(c->arena, total);

  /* Create modified args array */
  AstNode** new_args = ast_alloc_array(c->arena, argc);
  memcpy(new_args, args, sizeof(AstNode*) * argc);

  uint32_t def_idx = 0;
  for (uint32_t i = 0; i < argc; i++) {
    if (args[i]->type != AST_BLOCK &&
        compiler__node_is_suspension(c, args[i])) {
      /* Generate temp name __a0, __a1, etc. */
      char tmp_name[8];
      snprintf(tmp_name, sizeof(tmp_name), "__a%u", def_idx);
      uint32_t name_len = (uint32_t)strlen(tmp_name);

      char* name_copy = (char*)arena_alloc(c->arena, name_len + 1);
      memcpy(name_copy, tmp_name, name_len + 1);

      /* Create var ref node to replace the suspending arg */
      AstNode* var_ref = ast_alloc(c->arena);
      var_ref->type = AST_VAR_REF;
      var_ref->start = args[i]->start;
      var_ref->end = args[i]->end;
      var_ref->data.var_ref.name = name_copy;
      var_ref->data.var_ref.length = name_len;

      /* Create name literal node for def */
      AstNode* name_node = ast_alloc(c->arena);
      name_node->type = AST_LIT_STRING;
      name_node->start = args[i]->start;
      name_node->end = args[i]->end;
      name_node->data.lit_string.value = name_copy;
      name_node->data.lit_string.length = name_len;

      /* Create def head */
      AstNode* def_head = ast_alloc(c->arena);
      def_head->type = AST_LIT_STRING;
      def_head->start = args[i]->start;
      def_head->end = args[i]->end;
      def_head->data.lit_string.value = "def";
      def_head->data.lit_string.length = 3;

      /* Create def args: [name, value] */
      AstNode** def_args = ast_alloc_array(c->arena, 2);
      def_args[0] = name_node;
      def_args[1] = args[i]; /* original suspending expression */

      /* Create def command node */
      AstNode* def_cmd = ast_alloc(c->arena);
      def_cmd->type = AST_COMMAND;
      def_cmd->start = args[i]->start;
      def_cmd->end = args[i]->end;
      def_cmd->data.command.head = def_head;
      def_cmd->data.command.args = def_args;
      def_cmd->data.command.arg_count = 2;

      new_stmts[def_idx] = def_cmd;
      new_args[i] = var_ref;
      def_idx++;
    }
  }

  /* Create modified command with extracted args replaced by var refs */
  AstNode* mod_cmd = ast_alloc(c->arena);
  *mod_cmd = *cmd_node;
  mod_cmd->data.command.args = new_args;

  new_stmts[susp_count] = mod_cmd;

  /* Append remaining statements */
  for (uint32_t i = 0; i < remaining_count; i++) {
    new_stmts[susp_count + 1 + i] = remaining_stmts[i];
  }

  /* Compile the expanded sequence through CPS */
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
  uint32_t v_argc = value_node->data.command.arg_count;
  AstNode** v_args = value_node->data.command.args;

  /* Count extractable suspending args in value */
  uint32_t susp_count = 0;
  for (uint32_t i = 0; i < v_argc; i++) {
    if (v_args[i]->type != AST_BLOCK &&
        compiler__node_is_suspension(c, v_args[i])) {
      susp_count++;
    }
  }

  /* Build: defs + modified_def_stmt + remaining */
  uint32_t total = susp_count + 1 + remaining_count;
  AstNode** new_stmts = ast_alloc_array(c->arena, total);

  /* Create modified value args */
  AstNode** new_v_args = ast_alloc_array(c->arena, v_argc);
  memcpy(new_v_args, v_args, sizeof(AstNode*) * v_argc);

  uint32_t def_idx = 0;
  for (uint32_t i = 0; i < v_argc; i++) {
    if (v_args[i]->type != AST_BLOCK &&
        compiler__node_is_suspension(c, v_args[i])) {
      char tmp_name[8];
      snprintf(tmp_name, sizeof(tmp_name), "__a%u", def_idx);
      uint32_t name_len = (uint32_t)strlen(tmp_name);

      char* name_copy = (char*)arena_alloc(c->arena, name_len + 1);
      memcpy(name_copy, tmp_name, name_len + 1);

      AstNode* var_ref = ast_alloc(c->arena);
      var_ref->type = AST_VAR_REF;
      var_ref->start = v_args[i]->start;
      var_ref->end = v_args[i]->end;
      var_ref->data.var_ref.name = name_copy;
      var_ref->data.var_ref.length = name_len;

      AstNode* name_node = ast_alloc(c->arena);
      name_node->type = AST_LIT_STRING;
      name_node->start = v_args[i]->start;
      name_node->end = v_args[i]->end;
      name_node->data.lit_string.value = name_copy;
      name_node->data.lit_string.length = name_len;

      AstNode* def_head = ast_alloc(c->arena);
      def_head->type = AST_LIT_STRING;
      def_head->start = v_args[i]->start;
      def_head->end = v_args[i]->end;
      def_head->data.lit_string.value = "def";
      def_head->data.lit_string.length = 3;

      AstNode** def_args_arr = ast_alloc_array(c->arena, 2);
      def_args_arr[0] = name_node;
      def_args_arr[1] = v_args[i];

      AstNode* def_cmd = ast_alloc(c->arena);
      def_cmd->type = AST_COMMAND;
      def_cmd->start = v_args[i]->start;
      def_cmd->end = v_args[i]->end;
      def_cmd->data.command.head = def_head;
      def_cmd->data.command.args = def_args_arr;
      def_cmd->data.command.arg_count = 2;

      new_stmts[def_idx] = def_cmd;
      new_v_args[i] = var_ref;
      def_idx++;
    }
  }

  /* Create modified value node */
  AstNode* mod_value = ast_alloc(c->arena);
  *mod_value = *value_node;
  mod_value->data.command.args = new_v_args;

  /* Create modified def stmt */
  AstNode* mod_def = ast_alloc(c->arena);
  *mod_def = *def_stmt;
  AstNode** mod_def_args = ast_alloc_array(c->arena, def_stmt->data.command.arg_count);
  memcpy(mod_def_args, def_stmt->data.command.args,
         sizeof(AstNode*) * def_stmt->data.command.arg_count);
  /* Replace the value node in def args */
  uint32_t d_argc = def_stmt->data.command.arg_count;
  mod_def_args[d_argc - 1] = mod_value;
  mod_def->data.command.args = mod_def_args;

  new_stmts[susp_count] = mod_def;

  for (uint32_t i = 0; i < remaining_count; i++) {
    new_stmts[susp_count + 1 + i] = remaining_stmts[i];
  }

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
      return compiler__resolve_global_arity(c, name_val);
    }
  }
  if (node->type == AST_LIT_STRING) {
    uint32_t name_len = node->data.lit_string.length;
    if (name_len <= 7) {
      JaclVal name_val = jacl_inline_string(node->data.lit_string.value, name_len);
      return compiler__resolve_global_arity(c, name_val);
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
      if (!is_type_keyword(first_str, first_len)) {
        compiler__error(c, line, col, "mut with 3 arguments requires type keyword as first argument");
        return;
      }
      declared_type  = type_from_keyword(first_str, first_len);
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

    /* Determine effective type: declared type wins, else infer unboxed from RHS */
    JaclType effective_type;
    if (declared_type != TYPE_DYN) {
      effective_type = declared_type;
    } else if (is_unboxed_type(rhs_type)) {
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
            break;
          }
        }
      }
    }
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* set! — reassign mutable binding */
  if (compiler__head_matches(head, "set!", 4)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "set!", "2 arguments", argc); return; }
    if (args[0]->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "set! first argument must be a name");
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
    bool global_mutable = false;
    if (compiler__resolve_global_info(c, name_val, &global_mutable)) {
      if (global_mutable) {
        JaclType target_type = compiler__resolve_global_type(c, name_val);
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

  /* def builtin — supports [def name value] and [def TYPE name value] */
  if (compiler__head_matches(head, "def", 3)) {
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
      if (!is_type_keyword(first_str, first_len)) {
        compiler__error(c, line, col, "def with 3 arguments requires type keyword as first argument");
        return;
      }
      declared_type  = type_from_keyword(first_str, first_len);
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

    /* Determine effective type: declared type wins, else infer unboxed from RHS */
    JaclType effective_type;
    if (declared_type != TYPE_DYN) {
      effective_type = declared_type;
    } else if (is_unboxed_type(rhs_type)) {
      /* Infer unboxed types from RHS — must track because stack holds raw values */
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
          !is_type_keyword(args[0]->data.lit_string.value,
                           args[0]->data.lit_string.length)) {
        compiler__error(c, line, col,
            "proc with 4 arguments requires type keyword as first argument");
        return;
      }
      proc_return_type = type_from_keyword(args[0]->data.lit_string.value,
                                           args[0]->data.lit_string.length);
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

      /* Check for variadic marker & */
      if (wlen == 1 && word[0] == '&') {
        is_variadic = true;
        /* Next element is the rest param name (always dyn) */
        fi++;
        if (fi >= flat_count) {
          compiler__error(c, line, col, "expected parameter name after &");
          return;
        }
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
        param_types_arr[param_count] = TYPE_DYN;
        param_count++;
        continue;
      }

      /* Check if current element is a type keyword */
      if (is_type_keyword(word, wlen) && fi + 1 < flat_count) {
        /* Type keyword followed by param name → typed param */
        JaclType ptype = type_from_keyword(word, wlen);
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

    /* Set upvalue count on the closure */
    closure->upvalue_count = (uint8_t)body_compiler.upvalue_count;

    /* Store closure in parent's constant pool */
    uint16_t closure_idx = chunk_add_constant(c->chunk, jacl_closure(closure));

    /* Emit OP_CLOSURE to push the closure value, followed by upvalue descriptors */
    compiler__emit_byte(c, OP_CLOSURE, line);
    compiler__emit_u16(c, closure_idx, line);
    for (uint32_t i = 0; i < body_compiler.upvalue_count; i++) {
      compiler__emit_byte(c, body_compiler.upvalues[i].is_local, line);
      compiler__emit_byte(c, body_compiler.upvalues[i].index, line);
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
      c->locals[c->local_count - 1].known_arity = (int16_t)user_param_count;
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
      compiler__set_global_arity(c, name_val, (int16_t)user_param_count);
      /* Store param types, return type, and suspension in GlobalArity */
      {
        GlobalArity* ga = compiler__find_global_arity(c, name_val);
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

    /* Loop-start label */
    uint32_t loop_start = c->chunk->code_count;

    /* Compile condition */
    compiler__compile_node(c, args[0]);

    /* OP_JUMP_IF_FALSE to exit */
    uint32_t exit_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);

    /* Compile body statements directly (no extra scope, so def rebinds
       at the same scope level as the surrounding code) */
    uint32_t body_count = args[1]->data.block.count;
    for (uint32_t i = 0; i < body_count; i++) {
      compiler__compile_node(c, args[1]->data.block.commands[i]);
      compiler__emit_check_error(c, line);
    }

    /* OP_LOOP back to loop_start */
    compiler__emit_byte(c, OP_LOOP, line);
    uint32_t offset = c->chunk->code_count - loop_start + 2;
    compiler__emit_byte(c, (uint8_t)((offset >> 8) & 0xFF), line);
    compiler__emit_byte(c, (uint8_t)(offset & 0xFF), line);

    /* Patch exit jump to here */
    compiler__patch_jump(c, exit_jump);

    /* while returns nil */
    compiler__emit_byte(c, OP_NIL, line);
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
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "transform", "2 arguments", argc);
      return;
    }
    /* Check if callback is a known suspending proc ($var reference) */
    if (args[1]->type == AST_VAR_REF && args[1]->data.var_ref.length <= 7) {
      JaclVal cb_name = jacl_inline_string(args[1]->data.var_ref.name,
                                            args[1]->data.var_ref.length);
      int slot = compiler__resolve_local(c, cb_name);
      if (slot != -1 && c->locals[slot].suspends) {
        compiler__error(c, line, col,
            "cannot pass suspending closure to non-suspending builtin 'transform'");
        return;
      }
      GlobalArity* ga = compiler__find_global_arity(c, cb_name);
      if (ga && ga->suspends) {
        compiler__error(c, line, col,
            "cannot pass suspending closure to non-suspending builtin 'transform'");
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
    {
      bool saved = c->in_non_suspending_callback;
      c->in_non_suspending_callback = true;
      compiler__compile_node(c, args[1]);
      c->in_non_suspending_callback = saved;
    }
    compiler__emit_byte(c, OP_TRANSFORM, line);
    return;
  }

  /* each builtin (exactly 2 args — non-suspending callback) */
  if (compiler__head_matches(head, "each", 4)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "each", "2 arguments", argc);
      return;
    }
    /* Check if callback is a known suspending proc ($var reference) */
    if (args[1]->type == AST_VAR_REF && args[1]->data.var_ref.length <= 7) {
      JaclVal cb_name = jacl_inline_string(args[1]->data.var_ref.name,
                                            args[1]->data.var_ref.length);
      int slot = compiler__resolve_local(c, cb_name);
      if (slot != -1 && c->locals[slot].suspends) {
        compiler__error(c, line, col,
            "cannot pass suspending closure to non-suspending builtin 'each'");
        return;
      }
      GlobalArity* ga = compiler__find_global_arity(c, cb_name);
      if (ga && ga->suspends) {
        compiler__error(c, line, col,
            "cannot pass suspending closure to non-suspending builtin 'each'");
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
    {
      bool saved = c->in_non_suspending_callback;
      c->in_non_suspending_callback = true;
      compiler__compile_node(c, args[1]);
      c->in_non_suspending_callback = saved;
    }
    compiler__emit_byte(c, OP_EACH, line);
    return;
  }

  /* filter builtin (exactly 2 args — non-suspending callback) */
  if (compiler__head_matches(head, "filter", 6)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "filter", "2 arguments", argc);
      return;
    }
    /* Check if callback is a known suspending proc ($var reference) */
    if (args[1]->type == AST_VAR_REF && args[1]->data.var_ref.length <= 7) {
      JaclVal cb_name = jacl_inline_string(args[1]->data.var_ref.name,
                                            args[1]->data.var_ref.length);
      int slot = compiler__resolve_local(c, cb_name);
      if (slot != -1 && c->locals[slot].suspends) {
        compiler__error(c, line, col,
            "cannot pass suspending closure to non-suspending builtin 'filter'");
        return;
      }
      GlobalArity* ga = compiler__find_global_arity(c, cb_name);
      if (ga && ga->suspends) {
        compiler__error(c, line, col,
            "cannot pass suspending closure to non-suspending builtin 'filter'");
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
    {
      bool saved = c->in_non_suspending_callback;
      c->in_non_suspending_callback = true;
      compiler__compile_node(c, args[1]);
      c->in_non_suspending_callback = saved;
    }
    compiler__emit_byte(c, OP_FILTER, line);
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

  /* reset! builtin (exactly 2 args) */
  if (compiler__head_matches(head, "reset!", 6)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "reset!", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_RESET, line);
    return;
  }

  /* swap! builtin (exactly 2 args) */
  if (compiler__head_matches(head, "swap!", 5)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "swap!", "2 arguments", argc);
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
          head_arity = compiler__resolve_global_arity(c, name_val);
          GlobalArity* ga = compiler__find_global_arity(c, name_val);
          if (ga) {
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
            GlobalArity* ga = compiler__find_global_arity(c, vname);
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
      JaclVal val;
      if (len > 7) {
        val = jacl_intern(c->heap, c->intern_table,
                          node->data.lit_string.value, len);
      } else {
        val = jacl_inline_string(node->data.lit_string.value, len);
      }
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
        } else {
          JaclType global_type = compiler__resolve_global_type(c, name_val);
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
        compiler__emit_constant(c, jacl_inline_string("", 0), line);
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
          GlobalArity* existing = compiler__find_global_arity(c, name_val);
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

          /* Emit runtime bytecode: copy value from dependency namespace
             to importing module's namespace.
             OP_GET_GLOBAL "dep.jacl::name" → OP_DEF_GLOBAL "self::name" */
          {
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
          }
        }
      }
      /* use statement produces nil as its result value */
      compiler__emit_byte(c, OP_NIL, line);
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
                                      ThreadHeap* heap) {
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
            GlobalArity* ga = compiler__find_global_arity(&c, pname);
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
            GlobalArity* ga = compiler__find_global_arity(&c, mname);
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
    result.error_message = "parse error in root module";
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
            GlobalArity* ga = compiler__find_global_arity(&c, pname);
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
            GlobalArity* ga = compiler__find_global_arity(&c, mname);
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
  return result;
}

#endif /* COMPILER_C */
