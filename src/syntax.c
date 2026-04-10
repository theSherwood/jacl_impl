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

    case AST_QUOTE:
        syn->kind = SYNTAX_QUOTE;
        syn->data.quote.child = syntax_from_ast(node->data.quote.child,
                                                 heap, intern);
        break;

    case AST_DEFMACRO: {
        syn->kind = SYNTAX_DEFMACRO;
        /* Simplified: store [name, param1, param2, ..., body_syntax] */
        jacl_vec_root *items = jacl_vec_empty();
        items = jacl_vec_push_back(items,
            jacl_intern(heap, intern, node->data.defmacro.name,
                        node->data.defmacro.name_len));
        for (uint32_t i = 0; i < node->data.defmacro.param_count; i++) {
            items = jacl_vec_push_back(items,
                jacl_intern(heap, intern, node->data.defmacro.param_names[i],
                            node->data.defmacro.param_name_lens[i]));
        }
        /* Convert body block to syntax and append */
        JaclVal body_syn = syntax_from_ast(node->data.defmacro.body, heap, intern);
        items = jacl_vec_push_back(items, body_syn);
        syn->data.defmacro.child = jacl_vector_ptr(items);
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

/* -------------------------------------------------------------------------
 * Helper: copy string data from a JaclVal string into arena memory
 * Returns the arena-allocated C string and sets *out_len to its byte length.
 * ------------------------------------------------------------------------- */

static const char *syntax__string_to_arena(JaclVal str_val, arena_t *arena,
                                           uint32_t *out_len) {
    uint32_t byte_len = jacl_string_byte_len(str_val);
    char *buf = (char *)arena_alloc(arena, byte_len + 1);
    jacl_string_data(str_val, buf, byte_len + 1);
    buf[byte_len] = '\0';
    *out_len = byte_len;
    return buf;
}

/* -------------------------------------------------------------------------
 * Helper: set source position on an AstNode from a syntax object
 * ------------------------------------------------------------------------- */

static void syntax__set_ast_pos(AstNode *node, JaclSyntax *syn) {
    node->start.line   = syn->pos_line;
    node->start.column = syn->pos_col;
    node->start.offset = syn->pos_offset;
    node->end = node->start;  /* approximate — original end not stored */
}

/* -------------------------------------------------------------------------
 * syntax_to_ast: recursively convert JaclVal syntax object → AstNode*
 * ------------------------------------------------------------------------- */

AstNode *syntax_to_ast(JaclVal syn_val, arena_t *arena) {
    if (jacl_is_nil(syn_val)) return NULL;
    if (!jacl_is_syntax(syn_val)) return NULL;

    JaclSyntax *syn = jacl_as_syntax(syn_val);
    AstNode *node = ast_alloc(arena);
    memset(node, 0, sizeof(AstNode));
    syntax__set_ast_pos(node, syn);

    switch ((SyntaxKind)syn->kind) {

    case SYNTAX_COMMAND: {
        node->type = AST_COMMAND;
        node->data.command.head = syntax_to_ast(syn->data.command.head, arena);
        /* Convert args vec */
        jacl_vec_root *args = (jacl_vec_root *)jacl_as_ptr(syn->data.command.args);
        uint32_t argc = jacl_vec_count(args);
        node->data.command.args = ast_alloc_array(arena, argc);
        node->data.command.arg_count = argc;
        for (uint32_t i = 0; i < argc; i++) {
            node->data.command.args[i] =
                syntax_to_ast(jacl_vec_get(args, i).value, arena);
        }
        break;
    }

    case SYNTAX_LIT_INT:
        node->type = AST_LIT_INT;
        node->data.lit_int.value = syn->data.lit_int.value;
        break;

    case SYNTAX_LIT_FLOAT:
        node->type = AST_LIT_FLOAT;
        node->data.lit_float.value = syn->data.lit_float.value;
        break;

    case SYNTAX_LIT_STRING: {
        node->type = AST_LIT_STRING;
        node->data.lit_string.value =
            syntax__string_to_arena(syn->data.lit_string.value, arena,
                                    &node->data.lit_string.length);
        break;
    }

    case SYNTAX_VAR_REF: {
        node->type = AST_VAR_REF;
        node->data.var_ref.name =
            syntax__string_to_arena(syn->data.var_ref.name, arena,
                                    &node->data.var_ref.length);
        break;
    }

    case SYNTAX_BLOCK: {
        node->type = AST_BLOCK;
        jacl_vec_root *cmds =
            (jacl_vec_root *)jacl_as_ptr(syn->data.block.commands);
        uint32_t cnt = jacl_vec_count(cmds);
        node->data.block.commands = ast_alloc_array(arena, cnt);
        node->data.block.count = cnt;
        node->data.block.trailing_semi = false;
        for (uint32_t i = 0; i < cnt; i++) {
            node->data.block.commands[i] =
                syntax_to_ast(jacl_vec_get(cmds, i).value, arena);
        }
        break;
    }

    case SYNTAX_INTERP_STRING: {
        node->type = AST_INTERP_STRING;
        jacl_vec_root *segs =
            (jacl_vec_root *)jacl_as_ptr(syn->data.interp_string.segments);
        uint32_t cnt = jacl_vec_count(segs);
        node->data.interp_string.segments = ast_alloc_array(arena, cnt);
        node->data.interp_string.count = cnt;
        for (uint32_t i = 0; i < cnt; i++) {
            node->data.interp_string.segments[i] =
                syntax_to_ast(jacl_vec_get(segs, i).value, arena);
        }
        break;
    }

    case SYNTAX_SPREAD:
        node->type = AST_SPREAD;
        node->data.spread.expr = syntax_to_ast(syn->data.spread.child, arena);
        break;

    case SYNTAX_USE: {
        node->type = AST_USE;
        jacl_vec_root *items =
            (jacl_vec_root *)jacl_as_ptr(syn->data.use_decl.child);
        uint32_t item_count = jacl_vec_count(items);
        /* First element is the path, rest are names */
        if (item_count > 0) {
            JaclVal path_val = jacl_vec_get(items, 0).value;
            node->data.use_decl.path =
                syntax__string_to_arena(path_val, arena,
                                        &node->data.use_decl.path_len);
        }
        uint32_t name_count = item_count > 0 ? item_count - 1 : 0;
        node->data.use_decl.name_count = name_count;
        if (name_count > 0) {
            node->data.use_decl.names =
                (const char **)arena_alloc(arena, sizeof(char *) * name_count);
            node->data.use_decl.name_lens =
                (uint32_t *)arena_alloc(arena, sizeof(uint32_t) * name_count);
            for (uint32_t i = 0; i < name_count; i++) {
                JaclVal name_val = jacl_vec_get(items, i + 1).value;
                node->data.use_decl.names[i] =
                    syntax__string_to_arena(name_val, arena,
                                            &node->data.use_decl.name_lens[i]);
            }
        }
        break;
    }

    case SYNTAX_DEFSTRUCT: {
        node->type = AST_DEFSTRUCT;
        jacl_vec_root *items =
            (jacl_vec_root *)jacl_as_ptr(syn->data.defstruct.child);
        uint32_t item_count = jacl_vec_count(items);
        /* First element is name, rest are type/field pairs */
        if (item_count > 0) {
            JaclVal name_val = jacl_vec_get(items, 0).value;
            node->data.defstruct.name =
                syntax__string_to_arena(name_val, arena,
                                        &node->data.defstruct.name_len);
        }
        uint32_t field_count = item_count > 1 ? (item_count - 1) / 2 : 0;
        node->data.defstruct.field_count = field_count;
        if (field_count > 0) {
            node->data.defstruct.field_types =
                (const char **)arena_alloc(arena, sizeof(char *) * field_count);
            node->data.defstruct.field_type_lens =
                (uint32_t *)arena_alloc(arena, sizeof(uint32_t) * field_count);
            node->data.defstruct.field_names =
                (const char **)arena_alloc(arena, sizeof(char *) * field_count);
            node->data.defstruct.field_name_lens =
                (uint32_t *)arena_alloc(arena, sizeof(uint32_t) * field_count);
            for (uint32_t i = 0; i < field_count; i++) {
                JaclVal type_val = jacl_vec_get(items, 1 + i * 2).value;
                JaclVal name_val = jacl_vec_get(items, 2 + i * 2).value;
                node->data.defstruct.field_types[i] =
                    syntax__string_to_arena(type_val, arena,
                        &node->data.defstruct.field_type_lens[i]);
                node->data.defstruct.field_names[i] =
                    syntax__string_to_arena(name_val, arena,
                        &node->data.defstruct.field_name_lens[i]);
            }
        }
        break;
    }

    case SYNTAX_QUOTE:
        node->type = AST_QUOTE;
        node->data.quote.child = syntax_to_ast(syn->data.quote.child, arena);
        break;

    case SYNTAX_DEFMACRO: {
        node->type = AST_DEFMACRO;
        jacl_vec_root *items =
            (jacl_vec_root *)jacl_as_ptr(syn->data.defmacro.child);
        uint32_t item_count = jacl_vec_count(items);
        /* First element is name, last is body, middle are param names */
        if (item_count > 0) {
            JaclVal name_val = jacl_vec_get(items, 0).value;
            node->data.defmacro.name =
                syntax__string_to_arena(name_val, arena,
                                        &node->data.defmacro.name_len);
        }
        uint32_t param_count = item_count > 2 ? item_count - 2 : 0;
        node->data.defmacro.param_count = param_count;
        if (param_count > 0) {
            node->data.defmacro.param_names =
                (const char **)arena_alloc(arena, sizeof(char *) * param_count);
            node->data.defmacro.param_name_lens =
                (uint32_t *)arena_alloc(arena, sizeof(uint32_t) * param_count);
            for (uint32_t i = 0; i < param_count; i++) {
                JaclVal pval = jacl_vec_get(items, 1 + i).value;
                node->data.defmacro.param_names[i] =
                    syntax__string_to_arena(pval, arena,
                        &node->data.defmacro.param_name_lens[i]);
            }
        } else {
            node->data.defmacro.param_names = NULL;
            node->data.defmacro.param_name_lens = NULL;
        }
        /* Last element is the body */
        if (item_count > 1) {
            JaclVal body_val = jacl_vec_get(items, item_count - 1).value;
            node->data.defmacro.body = syntax_to_ast(body_val, arena);
        } else {
            node->data.defmacro.body = NULL;
        }
        break;
    }

    case SYNTAX_BREAK:
        node->type = AST_BREAK;
        node->data.break_stmt.value =
            jacl_is_nil(syn->data.break_stmt.value)
                ? NULL
                : syntax_to_ast(syn->data.break_stmt.value, arena);
        break;

    case SYNTAX_CONTINUE:
        node->type = AST_CONTINUE;
        break;

    case SYNTAX_RETURN:
        node->type = AST_RETURN;
        node->data.return_stmt.value =
            jacl_is_nil(syn->data.return_stmt.value)
                ? NULL
                : syntax_to_ast(syn->data.return_stmt.value, arena);
        break;

    case SYNTAX_DESTRUCTURE_VEC: {
        node->type = AST_DESTRUCTURE_VEC;
        jacl_vec_root *names =
            (jacl_vec_root *)jacl_as_ptr(syn->data.destructure_vec.names);
        uint32_t total = jacl_vec_count(names);
        /* Count regular names (not ".." prefixed) and find rest name */
        uint32_t regular_count = 0;
        for (uint32_t i = 0; i < total; i++) {
            JaclVal nv = jacl_vec_get(names, i).value;
            uint32_t blen = jacl_string_byte_len(nv);
            char tmp[4];
            jacl_string_data(nv, tmp, 2);
            if (blen >= 2 && tmp[0] == '.' && tmp[1] == '.') {
                /* rest name */
            } else {
                regular_count++;
            }
        }
        node->data.destructure_vec.count = regular_count;
        node->data.destructure_vec.names =
            (const char **)arena_alloc(arena, sizeof(char *) * regular_count);
        node->data.destructure_vec.name_lens =
            (uint32_t *)arena_alloc(arena, sizeof(uint32_t) * regular_count);
        node->data.destructure_vec.types = NULL;
        node->data.destructure_vec.type_lens = NULL;
        node->data.destructure_vec.rest_name = NULL;
        node->data.destructure_vec.rest_name_len = 0;
        uint32_t ri = 0;
        for (uint32_t i = 0; i < total; i++) {
            JaclVal nv = jacl_vec_get(names, i).value;
            uint32_t blen = jacl_string_byte_len(nv);
            char prefix[4];
            uint32_t plen = blen < 2 ? blen : 2;
            jacl_string_data(nv, prefix, plen);
            if (blen >= 2 && prefix[0] == '.' && prefix[1] == '.') {
                /* rest name: skip ".." prefix */
                uint32_t rlen = blen - 2;
                char *rbuf = (char *)arena_alloc(arena, rlen + 1);
                char full[256];
                jacl_string_data(nv, full, blen);
                memcpy(rbuf, full + 2, rlen);
                rbuf[rlen] = '\0';
                node->data.destructure_vec.rest_name = rbuf;
                node->data.destructure_vec.rest_name_len = rlen;
            } else {
                node->data.destructure_vec.names[ri] =
                    syntax__string_to_arena(nv, arena,
                        &node->data.destructure_vec.name_lens[ri]);
                ri++;
            }
        }
        break;
    }

    case SYNTAX_DESTRUCTURE_NAMED: {
        node->type = AST_DESTRUCTURE_NAMED;
        jacl_vec_root *names =
            (jacl_vec_root *)jacl_as_ptr(syn->data.destructure_named.names);
        uint32_t total = jacl_vec_count(names);
        uint32_t regular_count = 0;
        for (uint32_t i = 0; i < total; i++) {
            JaclVal nv = jacl_vec_get(names, i).value;
            uint32_t blen = jacl_string_byte_len(nv);
            char tmp[4];
            jacl_string_data(nv, tmp, 2);
            if (blen >= 2 && tmp[0] == '.' && tmp[1] == '.') {
                /* rest name */
            } else {
                regular_count++;
            }
        }
        node->data.destructure_named.count = regular_count;
        node->data.destructure_named.names =
            (const char **)arena_alloc(arena, sizeof(char *) * regular_count);
        node->data.destructure_named.name_lens =
            (uint32_t *)arena_alloc(arena, sizeof(uint32_t) * regular_count);
        node->data.destructure_named.types = NULL;
        node->data.destructure_named.type_lens = NULL;
        node->data.destructure_named.rest_name = NULL;
        node->data.destructure_named.rest_name_len = 0;
        node->data.destructure_named.spread_all = 0;
        uint32_t ri = 0;
        for (uint32_t i = 0; i < total; i++) {
            JaclVal nv = jacl_vec_get(names, i).value;
            uint32_t blen = jacl_string_byte_len(nv);
            char prefix[4];
            uint32_t plen = blen < 2 ? blen : 2;
            jacl_string_data(nv, prefix, plen);
            if (blen >= 2 && prefix[0] == '.' && prefix[1] == '.') {
                uint32_t rlen = blen - 2;
                if (rlen > 0) {
                    char *rbuf = (char *)arena_alloc(arena, rlen + 1);
                    char full[256];
                    jacl_string_data(nv, full, blen);
                    memcpy(rbuf, full + 2, rlen);
                    rbuf[rlen] = '\0';
                    node->data.destructure_named.rest_name = rbuf;
                    node->data.destructure_named.rest_name_len = rlen;
                } else {
                    node->data.destructure_named.spread_all = 1;
                }
            } else {
                node->data.destructure_named.names[ri] =
                    syntax__string_to_arena(nv, arena,
                        &node->data.destructure_named.name_lens[ri]);
                ri++;
            }
        }
        break;
    }

    }

    return node;
}

#endif /* SYNTAX_C */
