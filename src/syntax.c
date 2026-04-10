/*
 * JACL Syntax Objects — AST ↔ Syntax Conversion
 *
 * Converts between AstNode trees (parser output) and JaclSyntax objects
 * (GC-allocated values for macro expansion). syntax_from_ast converts
 * parsed AST into manipulable syntax values; syntax_to_ast converts back
 * for bytecode compilation.
 */

#ifndef SYNTAX_C
#define SYNTAX_C

/* -------------------------------------------------------------------------
 * Helper: set source position on a syntax object from an AstNode
 * ------------------------------------------------------------------------- */

static void syntax__set_pos(JaclSyntax *syn, AstNode *node) {
    syn->pos_line   = node->start.line;
    syn->pos_col    = node->start.column;
    syn->pos_offset = node->start.offset;
}

/* -------------------------------------------------------------------------
 * syntax_from_ast: recursively convert AstNode → JaclVal syntax object
 * ------------------------------------------------------------------------- */

JaclVal syntax_from_ast(AstNode *node, ThreadHeap *heap,
                        JaclInternTable *intern) {
    if (!node) return JACL_NIL;

    JaclVal syn_val = gc_alloc_syntax(heap);
    JaclSyntax *syn = jacl_as_syntax(syn_val);
    syntax__set_pos(syn, node);
    syn->scope_mark = 0;

    switch (node->type) {

    case AST_COMMAND: {
        syn->kind = SYNTAX_COMMAND;
        syn->data.command.head = syntax_from_ast(node->data.command.head,
                                                 heap, intern);
        /* Convert args array to vec of syntax */
        uint32_t argc = node->data.command.arg_count;
        jacl_vec_root *args = jacl_vec_empty();
        for (uint32_t i = 0; i < argc; i++) {
            JaclVal arg = syntax_from_ast(node->data.command.args[i],
                                          heap, intern);
            args = jacl_vec_push_back(args, arg);
        }
        syn->data.command.args = jacl_vector_ptr(args);
        break;
    }

    case AST_LIT_INT:
        syn->kind = SYNTAX_LIT_INT;
        syn->data.lit_int.value = node->data.lit_int.value;
        break;

    case AST_LIT_FLOAT:
        syn->kind = SYNTAX_LIT_FLOAT;
        syn->data.lit_float.value = node->data.lit_float.value;
        break;

    case AST_LIT_STRING:
        syn->kind = SYNTAX_LIT_STRING;
        syn->data.lit_string.value = jacl_intern(heap, intern,
            node->data.lit_string.value, node->data.lit_string.length);
        break;

    case AST_VAR_REF:
        syn->kind = SYNTAX_VAR_REF;
        syn->data.var_ref.name = jacl_intern(heap, intern,
            node->data.var_ref.name, node->data.var_ref.length);
        break;

    case AST_BLOCK: {
        syn->kind = SYNTAX_BLOCK;
        uint32_t cnt = node->data.block.count;
        jacl_vec_root *cmds = jacl_vec_empty();
        for (uint32_t i = 0; i < cnt; i++) {
            JaclVal cmd = syntax_from_ast(node->data.block.commands[i],
                                          heap, intern);
            cmds = jacl_vec_push_back(cmds, cmd);
        }
        syn->data.block.commands = jacl_vector_ptr(cmds);
        break;
    }

    case AST_INTERP_STRING: {
        syn->kind = SYNTAX_INTERP_STRING;
        uint32_t cnt = node->data.interp_string.count;
        jacl_vec_root *segs = jacl_vec_empty();
        for (uint32_t i = 0; i < cnt; i++) {
            JaclVal seg = syntax_from_ast(node->data.interp_string.segments[i],
                                          heap, intern);
            segs = jacl_vec_push_back(segs, seg);
        }
        syn->data.interp_string.segments = jacl_vector_ptr(segs);
        break;
    }

    case AST_SPREAD:
        syn->kind = SYNTAX_SPREAD;
        syn->data.spread.child = syntax_from_ast(node->data.spread.expr,
                                                 heap, intern);
        break;

    case AST_USE: {
        syn->kind = SYNTAX_USE;
        /* Simplified: store path and names as a vec of strings
         * [path, name1, name2, ...] */
        jacl_vec_root *items = jacl_vec_empty();
        items = jacl_vec_push_back(items,
            jacl_intern(heap, intern, node->data.use_decl.path,
                        node->data.use_decl.path_len));
        for (uint32_t i = 0; i < node->data.use_decl.name_count; i++) {
            items = jacl_vec_push_back(items,
                jacl_intern(heap, intern, node->data.use_decl.names[i],
                            node->data.use_decl.name_lens[i]));
        }
        syn->data.use_decl.child = jacl_vector_ptr(items);
        break;
    }

    case AST_DEFSTRUCT: {
        syn->kind = SYNTAX_DEFSTRUCT;
        /* Simplified: store [name, type1, field1, type2, field2, ...] */
        jacl_vec_root *items = jacl_vec_empty();
        items = jacl_vec_push_back(items,
            jacl_intern(heap, intern, node->data.defstruct.name,
                        node->data.defstruct.name_len));
        for (uint32_t i = 0; i < node->data.defstruct.field_count; i++) {
            items = jacl_vec_push_back(items,
                jacl_intern(heap, intern, node->data.defstruct.field_types[i],
                            node->data.defstruct.field_type_lens[i]));
            items = jacl_vec_push_back(items,
                jacl_intern(heap, intern, node->data.defstruct.field_names[i],
                            node->data.defstruct.field_name_lens[i]));
        }
        syn->data.defstruct.child = jacl_vector_ptr(items);
        break;
    }

    case AST_BREAK:
        syn->kind = SYNTAX_BREAK;
        syn->data.break_stmt.value = node->data.break_stmt.value
            ? syntax_from_ast(node->data.break_stmt.value, heap, intern)
            : JACL_NIL;
        break;

    case AST_CONTINUE:
        syn->kind = SYNTAX_CONTINUE;
        break;

    case AST_RETURN:
        syn->kind = SYNTAX_RETURN;
        syn->data.return_stmt.value = node->data.return_stmt.value
            ? syntax_from_ast(node->data.return_stmt.value, heap, intern)
            : JACL_NIL;
        break;

    case AST_DESTRUCTURE_VEC: {
        syn->kind = SYNTAX_DESTRUCTURE_VEC;
        /* Store names as vec of strings */
        jacl_vec_root *names = jacl_vec_empty();
        for (uint32_t i = 0; i < node->data.destructure_vec.count; i++) {
            names = jacl_vec_push_back(names,
                jacl_intern(heap, intern,
                    node->data.destructure_vec.names[i],
                    node->data.destructure_vec.name_lens[i]));
        }
        if (node->data.destructure_vec.rest_name) {
            /* Mark rest name with a ".." prefix to distinguish it */
            char rest_buf[256];
            uint32_t rlen = node->data.destructure_vec.rest_name_len;
            if (rlen + 2 <= sizeof(rest_buf)) {
                rest_buf[0] = '.'; rest_buf[1] = '.';
                memcpy(rest_buf + 2, node->data.destructure_vec.rest_name, rlen);
                names = jacl_vec_push_back(names,
                    jacl_intern(heap, intern, rest_buf, rlen + 2));
            }
        }
        syn->data.destructure_vec.names = jacl_vector_ptr(names);
        break;
    }

    case AST_DESTRUCTURE_NAMED: {
        syn->kind = SYNTAX_DESTRUCTURE_NAMED;
        /* Store names as vec of strings */
        jacl_vec_root *names = jacl_vec_empty();
        for (uint32_t i = 0; i < node->data.destructure_named.count; i++) {
            names = jacl_vec_push_back(names,
                jacl_intern(heap, intern,
                    node->data.destructure_named.names[i],
                    node->data.destructure_named.name_lens[i]));
        }
        if (node->data.destructure_named.rest_name) {
            char rest_buf[256];
            uint32_t rlen = node->data.destructure_named.rest_name_len;
            if (rlen + 2 <= sizeof(rest_buf)) {
                rest_buf[0] = '.'; rest_buf[1] = '.';
                memcpy(rest_buf + 2, node->data.destructure_named.rest_name, rlen);
                names = jacl_vec_push_back(names,
                    jacl_intern(heap, intern, rest_buf, rlen + 2));
            }
        }
        syn->data.destructure_named.names = jacl_vector_ptr(names);
        break;
    }

    case AST_ERROR:
        /* AST_ERROR nodes cannot be converted to syntax objects.
         * Return nil to signal the error. Callers should check
         * for errors before calling syntax_from_ast. */
        return JACL_NIL;
    }

    return syn_val;
}

#endif /* SYNTAX_C */
