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

typedef struct {
  JaclVal name;     /* inline string name */
  int     depth;    /* scope depth when declared */
} Local;

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
  local->name  = name;
  local->depth = c->scope_depth;
}

static int compiler__resolve_local(Compiler* c, JaclVal name) {
  for (int i = (int)c->local_count - 1; i >= 0; i--) {
    if (c->locals[i].name == name) {
      return i;
    }
  }
  return -1;
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
    compiler__emit_byte(c, OP_POP, line);
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
    if (argc != 2) { compiler__error(c, line, col, "+ requires 2 arguments"); return; }
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
      compiler__error(c, line, col, "- requires 1 or 2 arguments");
    }
    return;
  }
  if (compiler__head_matches(head, "*", 1)) {
    if (argc != 2) { compiler__error(c, line, col, "* requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_MUL, line);
    return;
  }
  if (compiler__head_matches(head, "/", 1)) {
    if (argc != 2) { compiler__error(c, line, col, "/ requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_DIV, line);
    return;
  }
  if (compiler__head_matches(head, "%", 1)) {
    if (argc != 2) { compiler__error(c, line, col, "%% requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_MOD, line);
    return;
  }

  /* Comparison builtins */
  if (compiler__head_matches(head, "==", 2)) {
    if (argc != 2) { compiler__error(c, line, col, "== requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_EQ, line);
    return;
  }
  if (compiler__head_matches(head, "<", 1)) {
    if (argc != 2) { compiler__error(c, line, col, "< requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_LT, line);
    return;
  }
  if (compiler__head_matches(head, ">", 1)) {
    if (argc != 2) { compiler__error(c, line, col, "> requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_GT, line);
    return;
  }
  if (compiler__head_matches(head, "<=", 2)) {
    if (argc != 2) { compiler__error(c, line, col, "<= requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_LE, line);
    return;
  }
  if (compiler__head_matches(head, ">=", 2)) {
    if (argc != 2) { compiler__error(c, line, col, ">= requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_GE, line);
    return;
  }

  /* Print builtin */
  if (compiler__head_matches(head, "print", 5)) {
    if (argc != 1) { compiler__error(c, line, col, "print requires 1 argument"); return; }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_PRINT, line);
    return;
  }

  /* length builtin */
  if (compiler__head_matches(head, "length", 6)) {
    if (argc != 1) {
      compiler__error(c, line, col, "length requires 1 argument");
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_STR_LEN, line);
    return;
  }

  /* index builtin */
  if (compiler__head_matches(head, "index", 5)) {
    if (argc != 2) {
      compiler__error(c, line, col, "index requires 2 arguments");
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
      compiler__error(c, line, col, "slice requires 2 or 3 arguments");
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
      compiler__error(c, line, col, "concat requires at least 2 arguments");
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
    if (argc != 2) { compiler__error(c, line, col, "def requires 2 arguments"); return; }
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
      compiler__add_local(c, name_val, line, col);
      /* def returns nil */
      compiler__emit_byte(c, OP_NIL, line);
    } else {
      /* Global variable */
      uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, name_idx, line);
    }
    return;
  }

  /* proc definition */
  if (compiler__head_matches(head, "proc", 4)) {
    if (argc != 3) {
      compiler__error(c, line, col, "proc requires 3 arguments");
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
    closure->name         = proc_name;

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
      /* proc returns the closure value (enables make-adder pattern) */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, (uint8_t)(c->local_count - 1), line);
    } else {
      /* Global scope */
      uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, name_idx, line);
    }
    return;
  }

  /* if conditional */
  if (compiler__head_matches(head, "if", 2)) {
    if (argc != 2 && argc != 3) {
      compiler__error(c, line, col, "if requires 2 or 3 arguments");
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
      compiler__error(c, line, col, "while requires 2 arguments");
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
      compiler__emit_byte(c, OP_POP, line);
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
        compiler__emit_byte(c, OP_POP, line);
      }
      compiler__end_scope(c, line);
      /* Block evaluates to nil */
      compiler__emit_byte(c, OP_NIL, line);
      break;
    }

    case AST_INTERP_STRING: {
      /* Future story */
      compiler__error(c, line, node->start.column,
                      "interpolated strings not yet supported");
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

    /* Emit OP_POP between statements to keep the stack clean */
    if (i < parse.count - 1) {
      compiler__emit_byte(&c, OP_POP, parse.nodes[i]->start.line);
    }
  }

  compiler__emit_byte(&c, OP_HALT,
                      parse.count > 0 ? parse.nodes[parse.count - 1]->start.line : 1);

  result.error_count  += c.error_count;
  result.error_message = c.first_error;
  return result;
}

#endif /* COMPILER_C */
