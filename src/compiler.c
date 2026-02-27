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
} CompileResult;

/* --- API --- */

static CompileResult compiler_compile(ParseResult parse, arena_t* arena,
                                      JaclInternTable* intern_table,
                                      ThreadHeap* heap);

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

/* --- Internal: Local variable tracking --- */

#define COMPILER_LOCALS_MAX 256
#define COMPILER_UPVALUES_MAX 256
#define COMPILER_TRY_PATCHES_MAX 128
#define COMPILER_MAX_PROC_PARAMS 16

typedef struct {
  JaclVal   name;         /* inline string name */
  int       depth;        /* scope depth when declared */
  int16_t   known_arity;  /* arity if bound to a proc, -1 = unknown */
  bool      is_mutable;   /* true if declared with mut */
  bool      is_param;     /* true if this is a function parameter */
  bool      suspends;     /* true if bound to a suspending proc */
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

        /* spawn and run are NOT suspension points — just recurse into args */

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

/* Check if an AST subtree contains any suspension points (for callback checking) */
static bool ast__contains_suspension(AstNode* node) {
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
        /* Don't recurse into nested proc definitions */
        if (len == 4 && memcmp(name, "proc", 4) == 0) {
          return false;
        }
      }
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        if (ast__contains_suspension(node->data.command.args[i]))
          return true;
      }
      return false;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        if (ast__contains_suspension(node->data.block.commands[i]))
          return true;
      }
      return false;
    }
    default:
      return false;
  }
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
  JaclType         expected_type;   /* contextual type hint for RHS compilation */
  JaclType         last_expr_type;  /* type of the last compiled expression */
  JaclType         return_type;     /* declared return type for current function */
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
  c->expected_type   = TYPE_DYN;
  c->last_expr_type  = TYPE_DYN;
  c->return_type     = TYPE_DYN;
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
      c->upvalues[uv].suspends = c->enclosing->upvalues[upvalue].suspends;
      c->upvalues[uv].type = c->enclosing->upvalues[upvalue].type;
    }
    return uv;
  }

  return -1;
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
      uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, name_idx, line);
      /* Record as mutable in global info with type */
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
      uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, name_idx, line);
      /* Register global with arity and type */
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

    /* Add params as locals in body compiler (slots 0..N-1) with types */
    for (uint8_t i = 0; i < param_count; i++) {
      compiler__add_local(&body_compiler, closure->param_names[i], line, col);
      body_compiler.locals[body_compiler.local_count - 1].is_param = true;
      body_compiler.locals[body_compiler.local_count - 1].type = param_types_arr[i];
    }

    /* Compile body as expression (last stmt value stays on stack) */
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

    /* Bind the name */
    if (c->scope_depth > 0) {
      /* Local scope: closure is on stack as local */
      compiler__add_local(c, name_val, line, col);
      c->locals[c->local_count - 1].known_arity = (int16_t)param_count;
      c->locals[c->local_count - 1].return_type = proc_return_type;
      c->locals[c->local_count - 1].param_types = stored_param_types;
      c->locals[c->local_count - 1].suspends    = proc_suspends;
      /* proc returns the closure value (enables make-adder pattern) */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, (uint8_t)(c->local_count - 1), line);
    } else {
      /* Global scope */
      uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, name_idx, line);
      compiler__set_global_arity(c, name_val, (int16_t)param_count);
      /* Store param types, return type, and suspension in GlobalArity */
      {
        GlobalArity* ga = compiler__find_global_arity(c, name_val);
        if (ga) {
          ga->return_type = proc_return_type;
          ga->suspends    = proc_suspends;
          for (uint8_t i = 0; i < param_count && i < COMPILER_MAX_PROC_PARAMS; i++) {
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
    if (ast__contains_suspension(args[1])) {
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
    if (ast__contains_suspension(args[1])) {
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
    if (ast__contains_suspension(args[1])) {
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
    /* Placeholder: compile args then discard (OP_PARALLEL in US-007) */
    for (uint32_t i = 0; i < argc; i++) {
      compiler__compile_node(c, args[i]);
    }
    compiler__emit_byte(c, OP_POP_N, line);
    compiler__emit_byte(c, (uint8_t)argc, line);
    compiler__emit_byte(c, OP_NIL, line);
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
    /* Placeholder: compile args then discard (OP_RACE in US-008) */
    for (uint32_t i = 0; i < argc; i++) {
      compiler__compile_node(c, args[i]);
    }
    compiler__emit_byte(c, OP_POP_N, line);
    compiler__emit_byte(c, (uint8_t)argc, line);
    compiler__emit_byte(c, OP_NIL, line);
    return;
  }

  /* spawn — NOT a suspension point (runtime task submission) */
  if (compiler__head_matches(head, "spawn", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "spawn", "1 argument", argc);
      return;
    }
    /* Placeholder: compile arg, pop, push nil (OP_SPAWN in US-005) */
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_POP, line);
    compiler__emit_byte(c, OP_NIL, line);
    return;
  }

  /* run — NOT a suspension point (sync-to-async bridge) */
  if (compiler__head_matches(head, "run", 3)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "run", "1 argument", argc);
      return;
    }
    /* Placeholder: compile arg, pop, push nil (OP_RUN in US-005) */
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_POP, line);
    compiler__emit_byte(c, OP_NIL, line);
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
        uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
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
          uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
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

    case AST_ERROR: {
      compiler__error(c, line, node->start.column, "parse error in AST");
      break;
    }
  }
}

/* --- Public API --- */

static CompileResult compiler_compile(ParseResult parse, arena_t* arena,
                                      JaclInternTable* intern_table,
                                      ThreadHeap* heap) {
  CompileResult result;
  chunk_init(&result.chunk, arena);
  result.error_count = parse.error_count;

  /* Pre-compilation suspension analysis */
  SuspensionMap suspension_map = compiler__analyze_suspension(
      parse.nodes, parse.count);

  Compiler c;
  compiler__init(&c, &result.chunk, arena, intern_table, heap);
  c.suspension_map = &suspension_map;

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&c, parse.nodes[i]);

    /* Emit OP_CHECK_ERROR between statements: auto-return on error */
    if (i < parse.count - 1) {
      compiler__emit_check_error(&c, parse.nodes[i]->start.line);
    }
  }

  compiler__emit_byte(&c, OP_HALT,
                      parse.count > 0 ? parse.nodes[parse.count - 1]->start.line : 1);

  result.error_count  += c.error_count;
  result.error_message = c.first_error;
  return result;
}

#endif /* COMPILER_C */
