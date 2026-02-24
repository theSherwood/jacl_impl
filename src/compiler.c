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
                                      JaclInternTable* intern_table);

/* --- Internal: Local variable tracking --- */

#define COMPILER_LOCALS_MAX 256
#define COMPILER_UPVALUES_MAX 256
#define COMPILER_TRY_PATCHES_MAX 128

typedef struct {
  JaclVal  name;         /* inline string name */
  int      depth;        /* scope depth when declared */
  int16_t  known_arity;  /* arity if bound to a proc, -1 = unknown */
} Local;

/* --- Internal: Global arity tracking --- */

#define COMPILER_GLOBAL_ARITIES_MAX 64

typedef struct {
  JaclVal name;
  int16_t known_arity;
} GlobalArity;

typedef struct {
  uint8_t index;    /* local slot (if is_local) or parent upvalue index */
  uint8_t is_local; /* 1 = capture from enclosing locals, 0 = from parent upvalues */
  JaclVal name;     /* for debug/lookup */
} Upvalue;

/* --- Internal: Compiler state --- */

typedef struct Compiler Compiler;
struct Compiler {
  BytecodeChunk*   chunk;
  arena_t*         arena;
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
};

static void compiler__init(Compiler* c, BytecodeChunk* chunk, arena_t* arena,
                           JaclInternTable* intern_table) {
  c->chunk         = chunk;
  c->arena         = arena;
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

static void compiler__set_global_arity(Compiler* c, JaclVal name, int16_t arity) {
  for (uint32_t i = 0; i < c->global_arity_count; i++) {
    if (c->global_arities[i].name == name) {
      c->global_arities[i].known_arity = arity;
      return;
    }
  }
  if (c->global_arity_count < COMPILER_GLOBAL_ARITIES_MAX) {
    c->global_arities[c->global_arity_count].name = name;
    c->global_arities[c->global_arity_count].known_arity = arity;
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
  c->upvalues[c->upvalue_count].index    = index;
  c->upvalues[c->upvalue_count].is_local = is_local;
  c->upvalues[c->upvalue_count].name     = name;
  return (int)c->upvalue_count++;
}

static int compiler__resolve_upvalue(Compiler* c, JaclVal name) {
  if (!c->enclosing) return -1;

  /* Check if the variable is a local in the enclosing scope */
  int local = compiler__resolve_local(c->enclosing, name);
  if (local != -1) {
    return compiler__add_upvalue(c, (uint8_t)local, 1, name);
  }

  /* Check if it's an upvalue in the enclosing scope (transitive capture) */
  int upvalue = compiler__resolve_upvalue(c->enclosing, name);
  if (upvalue != -1) {
    return compiler__add_upvalue(c, (uint8_t)upvalue, 0, name);
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

/* --- Internal: Compile a binary operation --- */

static void compiler__compile_binary(Compiler* c, AstNode** args,
                                     uint8_t op, uint32_t line) {
  compiler__compile_node(c, args[0]);
  compiler__compile_node(c, args[1]);
  compiler__emit_byte(c, op, line);
}

/* --- Internal: Compile a command invocation --- */

static void compiler__compile_command(Compiler* c, AstNode* node) {
  AstNode* head = node->data.command.head;
  uint32_t argc = node->data.command.arg_count;
  AstNode** args = node->data.command.args;
  uint32_t line = node->start.line;
  uint32_t col  = node->start.column;

  /* Arithmetic builtins */
  if (compiler__head_matches(head, "+", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "+", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_ADD, line);
    return;
  }
  if (compiler__head_matches(head, "-", 1)) {
    if (argc == 1) {
      compiler__compile_node(c, args[0]);
      compiler__emit_byte(c, OP_NEG, line);
    } else if (argc == 2) {
      compiler__compile_binary(c, args, OP_SUB, line);
    } else {
      compiler__builtin_arity_error(c, line, col, "-", "1 or 2 arguments", argc);
    }
    return;
  }
  if (compiler__head_matches(head, "*", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "*", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_MUL, line);
    return;
  }
  if (compiler__head_matches(head, "/", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "/", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_DIV, line);
    return;
  }
  if (compiler__head_matches(head, "%", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "%", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_MOD, line);
    return;
  }

  /* Comparison builtins */
  if (compiler__head_matches(head, "==", 2)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "==", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_EQ, line);
    return;
  }
  if (compiler__head_matches(head, "<", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "<", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_LT, line);
    return;
  }
  if (compiler__head_matches(head, ">", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, ">", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_GT, line);
    return;
  }
  if (compiler__head_matches(head, "<=", 2)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "<=", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_LE, line);
    return;
  }
  if (compiler__head_matches(head, ">=", 2)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, ">=", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_GE, line);
    return;
  }

  /* Print builtin */
  if (compiler__head_matches(head, "print", 5)) {
    if (argc != 1) { compiler__builtin_arity_error(c, line, col, "print", "1 argument", argc); return; }
    compiler__compile_node(c, args[0]);
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

  /* def builtin */
  if (compiler__head_matches(head, "def", 3)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "def", "2 arguments", argc); return; }
    if (args[0]->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "def first argument must be a name");
      return;
    }
    uint32_t name_len = args[0]->data.lit_string.length;
    if (name_len > 7) {
      compiler__error(c, line, col, "variable name exceeds 7-byte inline limit");
      return;
    }
    /* Compile the value expression */
    compiler__compile_node(c, args[1]);

    JaclVal name_val = jacl_inline_string(args[0]->data.lit_string.value, name_len);

    if (c->scope_depth > 0) {
      /* Local variable: value is on stack as the local slot */
      int16_t rhs_arity = compiler__node_known_arity(c, args[1]);
      compiler__add_local(c, name_val, line, col);
      c->locals[c->local_count - 1].known_arity = rhs_arity;
      /* def returns nil */
      compiler__emit_byte(c, OP_NIL, line);
    } else {
      /* Global variable */
      int16_t rhs_arity = compiler__node_known_arity(c, args[1]);
      uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, name_idx, line);
      if (rhs_arity != -1) {
        compiler__set_global_arity(c, name_val, rhs_arity);
      }
    }
    return;
  }

  /* proc definition */
  if (compiler__head_matches(head, "proc", 4)) {
    if (argc != 3) {
      compiler__builtin_arity_error(c, line, col, "proc", "3 arguments", argc);
      return;
    }
    if (args[0]->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "proc name must be a string");
      return;
    }
    if (args[1]->type != AST_COMMAND) {
      compiler__error(c, line, col, "proc params must be a bracketed list");
      return;
    }
    if (args[2]->type != AST_BLOCK) {
      compiler__error(c, line, col, "proc body must be a block");
      return;
    }

    /* Get proc name */
    const char* proc_name = args[0]->data.lit_string.value;
    uint32_t proc_name_len = args[0]->data.lit_string.length;
    if (proc_name_len > 7) {
      compiler__error(c, line, col, "proc name exceeds 7-byte inline limit");
      return;
    }

    /* Parse parameters from command node [a b c] */
    AstNode* params_node = args[1];
    AstNode* params_head = params_node->data.command.head;
    uint8_t param_count;

    if (params_head->data.lit_string.length == 0) {
      /* Empty params: [] */
      param_count = 0;
    } else {
      param_count = 1 + (uint8_t)params_node->data.command.arg_count;
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
    closure->min_args     = param_count;
    closure->variadic     = false;

    /* Allocate and fill param_names */
    if (param_count > 0) {
      closure->param_names = (JaclVal*)arena_alloc(c->arena, sizeof(JaclVal) * param_count);

      /* First param is the head of the params command */
      if (params_head->type != AST_LIT_STRING ||
          params_head->data.lit_string.length > 7) {
        compiler__error(c, line, col, "proc parameter name invalid");
        return;
      }
      closure->param_names[0] = jacl_inline_string(
          params_head->data.lit_string.value,
          params_head->data.lit_string.length);

      /* Remaining params are the args of the params command */
      for (uint8_t i = 0; i < params_node->data.command.arg_count; i++) {
        AstNode* param = params_node->data.command.args[i];
        if (param->type != AST_LIT_STRING ||
            param->data.lit_string.length > 7) {
          compiler__error(c, line, col, "proc parameter name invalid");
          return;
        }
        closure->param_names[1 + i] = jacl_inline_string(
            param->data.lit_string.value,
            param->data.lit_string.length);
      }
    } else {
      closure->param_names = NULL;
    }

    /* Create body compiler with function-level scope */
    Compiler body_compiler;
    compiler__init(&body_compiler, &closure->chunk, c->arena, c->intern_table);
    body_compiler.scope_depth = 1;
    body_compiler.enclosing   = c;

    /* Add params as locals in body compiler (slots 0..N-1) */
    for (uint8_t i = 0; i < param_count; i++) {
      compiler__add_local(&body_compiler, closure->param_names[i], line, col);
    }

    /* Compile body as expression (last stmt value stays on stack) */
    compiler__compile_block_expr(&body_compiler, args[2]);

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

    /* Bind the name */
    JaclVal name_val = jacl_inline_string(proc_name, proc_name_len);
    if (c->scope_depth > 0) {
      /* Local scope: closure is on stack as local */
      compiler__add_local(c, name_val, line, col);
      c->locals[c->local_count - 1].known_arity = (int16_t)param_count;
      /* proc returns the closure value (enables make-adder pattern) */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, (uint8_t)(c->local_count - 1), line);
    } else {
      /* Global scope */
      uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, name_idx, line);
      compiler__set_global_arity(c, name_val, (int16_t)param_count);
    }
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

  /* transform builtin (exactly 2 args) */
  if (compiler__head_matches(head, "transform", 9)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "transform", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_TRANSFORM, line);
    return;
  }

  /* each builtin (exactly 2 args) */
  if (compiler__head_matches(head, "each", 4)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "each", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_EACH, line);
    return;
  }

  /* filter builtin (exactly 2 args) */
  if (compiler__head_matches(head, "filter", 6)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "filter", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
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
    compiler__emit_byte(c, OP_TO_STRING, line);
    return;
  }

  /* Dynamic call: unrecognized command head — look up and call */
  {
    if (head->type == AST_LIT_STRING) {
      /* Look up bare word as a variable */
      uint32_t name_len = head->data.lit_string.length;
      if (name_len > 7) {
        compiler__error(c, line, col, "command name exceeds 7-byte inline limit");
        return;
      }
      JaclVal name_val = jacl_inline_string(head->data.lit_string.value, name_len);
      int local_slot = compiler__resolve_local(c, name_val);

      /* Compile-time arity check for known procs */
      {
        int16_t head_arity = -1;
        if (local_slot != -1) {
          head_arity = c->locals[local_slot].known_arity;
        } else {
          head_arity = compiler__resolve_global_arity(c, name_val);
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
        compiler__emit_byte(c, OP_GET_LOCAL, line);
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
      }
      compiler__compile_node(c, head);
    }

    /* Compile arguments */
    for (uint32_t i = 0; i < argc; i++) {
      compiler__compile_node(c, args[i]);
    }

    /* Emit call */
    compiler__emit_byte(c, OP_CALL, line);
    compiler__emit_byte(c, (uint8_t)argc, line);
  }
}

/* --- Internal: Compile a single AST node --- */

static void compiler__compile_node(Compiler* c, AstNode* node) {
  uint32_t line = node->start.line;

  switch (node->type) {

    case AST_LIT_INT: {
      compiler__emit_constant(c, jacl_i32(node->data.lit_int.value), line);
      break;
    }

    case AST_LIT_FLOAT: {
      compiler__emit_constant(c, jacl_f32(node->data.lit_float.value), line);
      break;
    }

    case AST_LIT_STRING: {
      uint32_t len = node->data.lit_string.length;
      JaclVal val;
      if (len > 7) {
        val = jacl_intern(c->arena, c->intern_table,
                          node->data.lit_string.value, len);
      } else {
        val = jacl_inline_string(node->data.lit_string.value, len);
      }
      compiler__emit_constant(c, val, line);
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
        compiler__emit_byte(c, OP_GET_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)local_slot, line);
      } else {
        int upvalue_idx = compiler__resolve_upvalue(c, name_val);
        if (upvalue_idx != -1) {
          compiler__emit_byte(c, OP_GET_UPVALUE, line);
          compiler__emit_byte(c, (uint8_t)upvalue_idx, line);
        } else {
          uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
          compiler__emit_byte(c, OP_GET_GLOBAL, line);
          compiler__emit_u16(c, name_idx, line);
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
        break;
      }

      /* Compile first segment */
      {
        AstNode* seg = segments[0];
        if (seg->type == AST_LIT_STRING) {
          compiler__compile_node(c, seg);
        } else {
          compiler__compile_node(c, seg);
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
          compiler__emit_byte(c, OP_TO_STRING, line);
        }
        compiler__emit_byte(c, OP_CONCAT, line);
      }
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
                                      JaclInternTable* intern_table) {
  CompileResult result;
  chunk_init(&result.chunk, arena);
  result.error_count = parse.error_count;

  Compiler c;
  compiler__init(&c, &result.chunk, arena, intern_table);

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
