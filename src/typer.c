/*
 * JACL Type Inference Pass — Phase 3 foundation.
 *
 * Walks the AST after parsing/macro-expansion and before codegen, populating
 * `node->inferred_type` (and `inferred_struct_idx` for structs).
 *
 * Current scope: literals only. Future commits expand to var refs, calls,
 * arithmetic, struct construction, etc., eventually replacing the
 * `expected_type`/`last_expr_type` plumbing in compiler.c.
 *
 * Dual-track contract: compiler.c continues to compute types its own way;
 * the typer pass runs alongside but does not yet drive codegen decisions.
 * When typer and compiler disagree, that's a bug in the typer (compiler.c
 * is the ground truth until Phase 3b switches consumers).
 */

#ifndef TYPER_C
#define TYPER_C

/* AST_LIT_INT default type matches compiler.c:11116 — tagged i32 unless
 * the consumer's expected_type narrows it (i64/u64/f64/u32/f32). The
 * typer can't know expected_type without context propagation, so for
 * the foundation pass we mark literals with their "natural" type and
 * leave context-driven narrowing for a later subphase. */

static void typer__infer_node(AstNode* node);

static void typer__infer_command(AstNode* node) {
  /* Recurse into head and args; don't yet derive the command's own type. */
  if (node->data.command.head) typer__infer_node(node->data.command.head);
  for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
    typer__infer_node(node->data.command.args[i]);
  }
  node->inferred_type = TYPE_DYN;
}

static void typer__infer_block(AstNode* node) {
  for (uint32_t i = 0; i < node->data.block.count; i++) {
    typer__infer_node(node->data.block.commands[i]);
  }
  /* Block type is the type of its last expression, or NIL if empty/trailing-semi. */
  if (node->data.block.count > 0 && !node->data.block.trailing_semi) {
    AstNode* last = node->data.block.commands[node->data.block.count - 1];
    node->inferred_type = last->inferred_type;
    node->inferred_struct_idx = last->inferred_struct_idx;
  } else {
    node->inferred_type = TYPE_NIL;
  }
}

static void typer__infer_node(AstNode* node) {
  if (!node) return;
  switch (node->type) {
    case AST_LIT_INT:
      node->inferred_type = TYPE_I32;
      break;
    case AST_LIT_FLOAT:
      node->inferred_type = TYPE_F32;
      break;
    case AST_LIT_STRING:
      node->inferred_type = TYPE_STR;
      break;
    case AST_BLOCK:
      typer__infer_block(node);
      break;
    case AST_COMMAND:
      typer__infer_command(node);
      break;
    case AST_INTERP_STRING:
      for (uint32_t i = 0; i < node->data.interp_string.count; i++) {
        typer__infer_node(node->data.interp_string.segments[i]);
      }
      node->inferred_type = TYPE_STR;
      break;
    case AST_BREAK:
      if (node->data.break_stmt.value) typer__infer_node(node->data.break_stmt.value);
      node->inferred_type = TYPE_NIL;
      break;
    case AST_RETURN:
      if (node->data.return_stmt.value) typer__infer_node(node->data.return_stmt.value);
      node->inferred_type = TYPE_NIL;
      break;
    case AST_QUOTE:
      if (node->data.quote.child) typer__infer_node(node->data.quote.child);
      node->inferred_type = TYPE_DYN;
      break;
    case AST_SYNTAX_QUOTE:
      if (node->data.syntax_quote.child) typer__infer_node(node->data.syntax_quote.child);
      node->inferred_type = TYPE_DYN;
      break;
    case AST_UNQUOTE:
      if (node->data.unquote.child) typer__infer_node(node->data.unquote.child);
      node->inferred_type = TYPE_DYN;
      break;
    case AST_UNQUOTE_SPLICING:
      if (node->data.unquote_splicing.child) typer__infer_node(node->data.unquote_splicing.child);
      node->inferred_type = TYPE_DYN;
      break;
    case AST_SPREAD:
      if (node->data.spread.expr) typer__infer_node(node->data.spread.expr);
      node->inferred_type = TYPE_DYN;
      break;
    case AST_SHELL_CMD:
      if (node->data.shell_cmd.head) typer__infer_node(node->data.shell_cmd.head);
      for (uint32_t i = 0; i < node->data.shell_cmd.arg_count; i++) {
        typer__infer_node(node->data.shell_cmd.args[i]);
      }
      node->inferred_type = TYPE_DYN;
      break;
    /* Var refs, defs, struct construction, etc. need scope tracking — deferred
     * to the next subphase. They default to TYPE_DYN which is sound. */
    case AST_VAR_REF:
    case AST_USE:
    case AST_DEFSTRUCT:
    case AST_DEFMACRO:
    case AST_DESTRUCTURE_VEC:
    case AST_DESTRUCTURE_NAMED:
    case AST_CONTINUE:
    case AST_CTX_DECL:
    case AST_ERROR:
    default:
      node->inferred_type = TYPE_DYN;
      break;
  }
}

void typer_infer(AstNode** nodes, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    typer__infer_node(nodes[i]);
  }
}

#endif /* TYPER_C */
