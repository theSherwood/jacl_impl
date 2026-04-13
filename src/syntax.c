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
    syn->scope_mark = node->scope_mark;  /* hygiene: preserve macro mark */
    syn->is_caret   = node->is_caret;    /* US-013: preserve ^ flag */
    syn->is_gensym  = node->is_gensym;   /* US-014: preserve gensym flag */

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

    case AST_SYNTAX_QUOTE:
        syn->kind = SYNTAX_SYNTAX_QUOTE;
        syn->data.syntax_quote.child = syntax_from_ast(
            node->data.syntax_quote.child, heap, intern);
        break;

    case AST_UNQUOTE:
        syn->kind = SYNTAX_UNQUOTE;
        syn->data.unquote.child = syntax_from_ast(
            node->data.unquote.child, heap, intern);
        break;

    case AST_UNQUOTE_SPLICING:
        syn->kind = SYNTAX_UNQUOTE_SPLICING;
        syn->data.unquote_splicing.child = syntax_from_ast(
            node->data.unquote_splicing.child, heap, intern);
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
    node->scope_mark   = syn->scope_mark;  /* hygiene: preserve macro mark */
    node->is_caret     = syn->is_caret;    /* US-013: preserve ^ flag */
    node->is_gensym    = syn->is_gensym;   /* US-014: preserve gensym flag */
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

    case SYNTAX_SYNTAX_QUOTE:
        node->type = AST_SYNTAX_QUOTE;
        node->data.syntax_quote.child = syntax_to_ast(
            syn->data.syntax_quote.child, arena);
        break;

    case SYNTAX_UNQUOTE:
        node->type = AST_UNQUOTE;
        node->data.unquote.child = syntax_to_ast(
            syn->data.unquote.child, arena);
        break;

    case SYNTAX_UNQUOTE_SPLICING:
        node->type = AST_UNQUOTE_SPLICING;
        node->data.unquote_splicing.child = syntax_to_ast(
            syn->data.unquote_splicing.child, arena);
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

/* -------------------------------------------------------------------------
 * syntax__splice_template: walk a syntax template, replacing SYNTAX_UNQUOTE
 * with values from the splice array. SYNTAX_UNQUOTE_SPLICING flattens into
 * parent arg lists / block command lists.
 *
 * values[0..n-1] are consumed in the order unquotes appear (left-to-right,
 * depth-first traversal). *idx tracks the next value to consume.
 * Returns a new syntax object tree (the template is not mutated).
 *
 * Type validation: ~ (unquote) result must be a syntax object (or nil).
 * ~@ (unquote-splicing) result must be a vec of syntax objects. On type
 * error, sets *err to a static error message and returns JACL_NIL.
 * ------------------------------------------------------------------------- */

static JaclVal syntax__splice_template(JaclVal tmpl, JaclVal *values,
                                       uint32_t n_values, uint32_t *idx,
                                       ThreadHeap *heap,
                                       const char **err) {
    if (*err) return JACL_NIL;
    if (jacl_is_nil(tmpl)) return JACL_NIL;
    if (!jacl_is_syntax(tmpl)) return tmpl;

    JaclSyntax *syn = jacl_as_syntax(tmpl);

    /* Unquote: replace with the next value. The value must be a syntax
     * object (or nil). */
    if (syn->kind == SYNTAX_UNQUOTE) {
        if (*idx < n_values) {
            JaclVal v = values[(*idx)++];
            if (!jacl_is_nil(v) && !jacl_is_syntax(v)) {
                *err = "~ (unquote) requires a syntax object";
                return JACL_NIL;
            }
            return v;
        }
        return JACL_NIL;  /* shouldn't happen if counts match */
    }

    /* Unquote-splicing at a non-spliceable position is an error — ~@ only
     * makes sense inside command args or block command lists. */
    if (syn->kind == SYNTAX_UNQUOTE_SPLICING) {
        *err = "~@ (unquote-splicing) is only valid in command args or block bodies";
        return JACL_NIL;
    }

    /* Command: splice head and args, flatten ~@ in args */
    if (syn->kind == SYNTAX_COMMAND) {
        JaclVal new_head = syntax__splice_template(syn->data.command.head,
                                                    values, n_values, idx,
                                                    heap, err);
        if (*err) return JACL_NIL;
        jacl_vec_root *old_args =
            (jacl_vec_root *)jacl_as_ptr(syn->data.command.args);
        uint32_t argc = jacl_vec_count(old_args);
        jacl_vec_root *new_args = jacl_vec_empty();
        for (uint32_t i = 0; i < argc; i++) {
            JaclVal arg = jacl_vec_get(old_args, i).value;
            if (jacl_is_syntax(arg)) {
                JaclSyntax *arg_syn = jacl_as_syntax(arg);
                if (arg_syn->kind == SYNTAX_UNQUOTE_SPLICING) {
                    /* Flatten: the value must be a vec of syntax objects */
                    if (*idx >= n_values) continue;
                    JaclVal splice_val = values[(*idx)++];
                    if (!jacl_is_vector(splice_val)) {
                        *err = "~@ (unquote-splicing) requires a vec of syntax objects";
                        return JACL_NIL;
                    }
                    jacl_vec_root *splice_vec =
                        (jacl_vec_root *)jacl_as_ptr(splice_val);
                    uint32_t sc = jacl_vec_count(splice_vec);
                    for (uint32_t j = 0; j < sc; j++) {
                        JaclVal elem = jacl_vec_get(splice_vec, j).value;
                        if (!jacl_is_syntax(elem)) {
                            *err = "~@ (unquote-splicing) requires a vec of syntax objects";
                            return JACL_NIL;
                        }
                        new_args = jacl_vec_push_back(new_args, elem);
                    }
                    continue;
                }
            }
            JaclVal spliced = syntax__splice_template(arg, values, n_values,
                                                       idx, heap, err);
            if (*err) return JACL_NIL;
            new_args = jacl_vec_push_back(new_args, spliced);
        }
        JaclVal result = gc_alloc_syntax(heap);
        JaclSyntax *rsyn = jacl_as_syntax(result);
        rsyn->kind = SYNTAX_COMMAND;
        rsyn->pos_line = syn->pos_line;
        rsyn->pos_col = syn->pos_col;
        rsyn->pos_offset = syn->pos_offset;
        rsyn->scope_mark = syn->scope_mark;
        rsyn->data.command.head = new_head;
        rsyn->data.command.args = jacl_vector_ptr(new_args);
        return result;
    }

    /* Block: splice commands, flatten ~@ in command list */
    if (syn->kind == SYNTAX_BLOCK) {
        jacl_vec_root *old_cmds =
            (jacl_vec_root *)jacl_as_ptr(syn->data.block.commands);
        uint32_t cnt = jacl_vec_count(old_cmds);
        jacl_vec_root *new_cmds = jacl_vec_empty();
        for (uint32_t i = 0; i < cnt; i++) {
            JaclVal cmd = jacl_vec_get(old_cmds, i).value;
            if (jacl_is_syntax(cmd)) {
                JaclSyntax *cmd_syn = jacl_as_syntax(cmd);
                if (cmd_syn->kind == SYNTAX_UNQUOTE_SPLICING) {
                    if (*idx >= n_values) continue;
                    JaclVal splice_val = values[(*idx)++];
                    if (!jacl_is_vector(splice_val)) {
                        *err = "~@ (unquote-splicing) requires a vec of syntax objects";
                        return JACL_NIL;
                    }
                    jacl_vec_root *splice_vec =
                        (jacl_vec_root *)jacl_as_ptr(splice_val);
                    uint32_t sc = jacl_vec_count(splice_vec);
                    for (uint32_t j = 0; j < sc; j++) {
                        JaclVal elem = jacl_vec_get(splice_vec, j).value;
                        if (!jacl_is_syntax(elem)) {
                            *err = "~@ (unquote-splicing) requires a vec of syntax objects";
                            return JACL_NIL;
                        }
                        new_cmds = jacl_vec_push_back(new_cmds, elem);
                    }
                    continue;
                }
            }
            JaclVal spliced = syntax__splice_template(cmd, values, n_values,
                                                       idx, heap, err);
            if (*err) return JACL_NIL;
            new_cmds = jacl_vec_push_back(new_cmds, spliced);
        }
        JaclVal result = gc_alloc_syntax(heap);
        JaclSyntax *rsyn = jacl_as_syntax(result);
        rsyn->kind = SYNTAX_BLOCK;
        rsyn->pos_line = syn->pos_line;
        rsyn->pos_col = syn->pos_col;
        rsyn->pos_offset = syn->pos_offset;
        rsyn->scope_mark = syn->scope_mark;
        rsyn->data.block.commands = jacl_vector_ptr(new_cmds);
        return result;
    }

    /* Interp-string: splice segments, flatten ~@ */
    if (syn->kind == SYNTAX_INTERP_STRING) {
        jacl_vec_root *old_segs =
            (jacl_vec_root *)jacl_as_ptr(syn->data.interp_string.segments);
        uint32_t cnt = jacl_vec_count(old_segs);
        jacl_vec_root *new_segs = jacl_vec_empty();
        for (uint32_t i = 0; i < cnt; i++) {
            JaclVal seg = jacl_vec_get(old_segs, i).value;
            JaclVal spliced = syntax__splice_template(seg, values, n_values,
                                                       idx, heap, err);
            if (*err) return JACL_NIL;
            new_segs = jacl_vec_push_back(new_segs, spliced);
        }
        JaclVal result = gc_alloc_syntax(heap);
        JaclSyntax *rsyn = jacl_as_syntax(result);
        rsyn->kind = SYNTAX_INTERP_STRING;
        rsyn->pos_line = syn->pos_line;
        rsyn->pos_col = syn->pos_col;
        rsyn->pos_offset = syn->pos_offset;
        rsyn->scope_mark = syn->scope_mark;
        rsyn->data.interp_string.segments = jacl_vector_ptr(new_segs);
        return result;
    }

    /* Spread: recurse into child */
    if (syn->kind == SYNTAX_SPREAD) {
        JaclVal new_child = syntax__splice_template(syn->data.spread.child,
                                                     values, n_values, idx,
                                                     heap, err);
        if (*err) return JACL_NIL;
        JaclVal result = gc_alloc_syntax(heap);
        JaclSyntax *rsyn = jacl_as_syntax(result);
        rsyn->kind = SYNTAX_SPREAD;
        rsyn->pos_line = syn->pos_line;
        rsyn->pos_col = syn->pos_col;
        rsyn->pos_offset = syn->pos_offset;
        rsyn->scope_mark = syn->scope_mark;
        rsyn->data.spread.child = new_child;
        return result;
    }

    /* Quote/syntax-quote: recurse into child */
    if (syn->kind == SYNTAX_QUOTE) {
        JaclVal new_child = syntax__splice_template(syn->data.quote.child,
                                                     values, n_values, idx,
                                                     heap, err);
        if (*err) return JACL_NIL;
        JaclVal result = gc_alloc_syntax(heap);
        JaclSyntax *rsyn = jacl_as_syntax(result);
        rsyn->kind = SYNTAX_QUOTE;
        rsyn->pos_line = syn->pos_line;
        rsyn->pos_col = syn->pos_col;
        rsyn->pos_offset = syn->pos_offset;
        rsyn->scope_mark = syn->scope_mark;
        rsyn->data.quote.child = new_child;
        return result;
    }

    /* Nested syntax-quote: treat as literal (don't recurse into unquotes) */
    if (syn->kind == SYNTAX_SYNTAX_QUOTE) {
        return tmpl;  /* nested syntax-quote is literal */
    }

    /* All other kinds (lit-int, lit-float, lit-string, var-ref, break, etc.)
     * are leaves — return as-is */
    return tmpl;
}

/* -------------------------------------------------------------------------
 * ast_expand_macros: macro expansion pass
 *
 * Walks the AST top-to-bottom. When a defmacro node is found, compiles it
 * and registers in the macro table. When a macro call is found, expands it
 * by executing the macro closure with syntax object arguments.
 *
 * Returns NULL on success, or an error message string on failure.
 * ------------------------------------------------------------------------- */

/* Macro expansion state is now per-compilation via ExpandState (defined in
 * jacl.h), eliminating the file-static reentrancy hazards that previously
 * prevented nested or concurrent compile+run cycles. */

static bool expand__push_frame(ExpandState *es, const char *name,
                                uint32_t name_len,
                                uint32_t line, uint32_t col) {
    if (es->frame_top >= EXPAND_FRAME_MAX) return false;
    es->frames[es->frame_top].name     = name;
    es->frames[es->frame_top].name_len = name_len;
    es->frames[es->frame_top].line     = line;
    es->frames[es->frame_top].col      = col;
    es->frame_top++;
    return true;
}

static void expand__pop_frame(ExpandState *es) {
    if (es->frame_top > 0) es->frame_top--;
}

/* Build an arena-allocated error message combining base_msg with the
 * current expansion call stack. Frames are rendered outermost-first
 * (index 0 → frame_top - 1) so the reader sees the chain top-down. When
 * the stack is deeper than 8 frames we show the outermost 4, an "... N
 * more ..." marker, and the innermost 4, so that a self-recursing macro
 * hitting the depth limit doesn't produce a 200-line error. When the
 * stack is empty (no active expansion) we simply copy base_msg into the
 * arena so the returned pointer has the same lifetime as the arena. */
static const char *expand__build_error_with_chain(ExpandState *es,
                                                   const char *base_msg,
                                                   arena_t *arena) {
    size_t base_len = strlen(base_msg);
    if (es->frame_top == 0) {
        char *buf = (char *)arena_alloc(arena, (uint32_t)(base_len + 1));
        memcpy(buf, base_msg, base_len);
        buf[base_len] = '\0';
        return buf;
    }

    const uint32_t DISPLAY_MAX = 8;
    bool truncated = es->frame_top > DISPLAY_MAX;

    size_t cap = base_len + 64;
    for (uint32_t i = 0; i < es->frame_top; i++)
        cap += es->frames[i].name_len + 80;
    cap += 96;  /* truncation marker headroom */

    char *buf = (char *)arena_alloc(arena, (uint32_t)cap);
    size_t n = 0;
    memcpy(buf + n, base_msg, base_len);
    n += base_len;

    if (!truncated) {
        for (uint32_t i = 0; i < es->frame_top; i++) {
            int w = snprintf(buf + n, cap - n,
                "\n  in expansion of macro `%.*s` at line %u, col %u",
                (int)es->frames[i].name_len,
                es->frames[i].name,
                es->frames[i].line,
                es->frames[i].col);
            if (w > 0) n += (size_t)w;
        }
    } else {
        for (uint32_t i = 0; i < 4; i++) {
            int w = snprintf(buf + n, cap - n,
                "\n  in expansion of macro `%.*s` at line %u, col %u",
                (int)es->frames[i].name_len,
                es->frames[i].name,
                es->frames[i].line,
                es->frames[i].col);
            if (w > 0) n += (size_t)w;
        }
        {
            int w = snprintf(buf + n, cap - n,
                "\n  ... %u more expansion frames ...",
                es->frame_top - DISPLAY_MAX);
            if (w > 0) n += (size_t)w;
        }
        for (uint32_t i = es->frame_top - 4; i < es->frame_top; i++) {
            int w = snprintf(buf + n, cap - n,
                "\n  in expansion of macro `%.*s` at line %u, col %u",
                (int)es->frames[i].name_len,
                es->frames[i].name,
                es->frames[i].line,
                es->frames[i].col);
            if (w > 0) n += (size_t)w;
        }
    }
    buf[n] = '\0';
    return buf;
}

static void expand__set_error(ExpandState *es, const char *msg,
                               uint32_t line, uint32_t col,
                               arena_t *arena) {
    es->error_msg  = expand__build_error_with_chain(es, msg, arena);
    es->error_line = line;
    es->error_col  = col;
}

/* -------------------------------------------------------------------------
 * expand__stamp_syntax: recursively stamp a freshly-allocated template
 * syntax tree with the given scope mark. Used before substitution so that
 * every template-originated node carries the macro's scope mark. Nodes
 * that the substitution replaces (unquoted arguments from the caller)
 * retain their own marks because the substitution returns arg values
 * directly without copying.
 *
 * Safe to mutate: the template syntax tree was produced by syntax_from_ast
 * at expansion time and is not aliased by any user-visible value.
 * ------------------------------------------------------------------------- */

static void expand__stamp_syntax(JaclVal val, uint32_t mark);

static void expand__stamp_vec(JaclVal vec_val, uint32_t mark) {
    if (!jacl_is_vector(vec_val)) return;
    jacl_vec_root *vec = (jacl_vec_root *)jacl_as_ptr(vec_val);
    uint32_t n = jacl_vec_count(vec);
    for (uint32_t i = 0; i < n; i++) {
        expand__stamp_syntax(jacl_vec_get(vec, i).value, mark);
    }
}

static void expand__stamp_syntax(JaclVal val, uint32_t mark) {
    if (!jacl_is_syntax(val)) return;
    JaclSyntax *syn = jacl_as_syntax(val);
    /* US-013: caret-flagged var refs stay at scope mark 0 so the
       identifier resolves in the caller's scope instead of the
       freshly-minted macro mark.  We leave the mark alone but still
       recurse below — a SYNTAX_VAR_REF has no children anyway. */
    if (!syn->is_caret) {
        syn->scope_mark = mark;
    }
    switch ((SyntaxKind)syn->kind) {
    case SYNTAX_COMMAND:
        expand__stamp_syntax(syn->data.command.head, mark);
        expand__stamp_vec(syn->data.command.args, mark);
        break;
    case SYNTAX_BLOCK:
        expand__stamp_vec(syn->data.block.commands, mark);
        break;
    case SYNTAX_INTERP_STRING:
        expand__stamp_vec(syn->data.interp_string.segments, mark);
        break;
    case SYNTAX_SPREAD:
        expand__stamp_syntax(syn->data.spread.child, mark);
        break;
    case SYNTAX_QUOTE:
        expand__stamp_syntax(syn->data.quote.child, mark);
        break;
    case SYNTAX_SYNTAX_QUOTE:
        expand__stamp_syntax(syn->data.syntax_quote.child, mark);
        break;
    case SYNTAX_UNQUOTE:
        /* Stamp the unquote placeholder too — it will be replaced by an
         * arg value during substitution, but in case the placeholder
         * leaks through (e.g. ~@ at a non-spliceable position) we still
         * want its mark set. */
        expand__stamp_syntax(syn->data.unquote.child, mark);
        break;
    case SYNTAX_UNQUOTE_SPLICING:
        expand__stamp_syntax(syn->data.unquote_splicing.child, mark);
        break;
    case SYNTAX_BREAK:
        expand__stamp_syntax(syn->data.break_stmt.value, mark);
        break;
    case SYNTAX_RETURN:
        expand__stamp_syntax(syn->data.return_stmt.value, mark);
        break;
    case SYNTAX_LIT_INT:
    case SYNTAX_LIT_FLOAT:
    case SYNTAX_LIT_STRING:
    case SYNTAX_VAR_REF:
    case SYNTAX_CONTINUE:
    case SYNTAX_USE:
    case SYNTAX_DEFSTRUCT:
    case SYNTAX_DEFMACRO:
    case SYNTAX_DESTRUCTURE_VEC:
    case SYNTAX_DESTRUCTURE_NAMED:
        /* Leaves or simplified representations — nothing more to recurse
         * into for the hygiene walk. */
        break;
    }
}

/* Helper: check if an AstNode command head is a bare word matching a name */
static bool expand__head_is_name(AstNode *node, const char **out_name,
                                 uint32_t *out_len) {
    if (node->type != AST_COMMAND) return false;
    AstNode *head = node->data.command.head;
    if (!head || head->type != AST_LIT_STRING) return false;
    *out_name = head->data.lit_string.value;
    *out_len  = head->data.lit_string.length;
    return true;
}

/* -------------------------------------------------------------------------
 * expand__match_param: extract the name string from an unquote child
 * (either SYNTAX_LIT_STRING or SYNTAX_VAR_REF) and look up which
 * parameter index it matches. Returns -1 if no match.
 * ------------------------------------------------------------------------- */

static int expand__match_param(JaclVal unquote_child,
                               const char **param_names,
                               uint32_t *param_name_lens,
                               uint32_t param_count) {
    if (jacl_is_nil(unquote_child) || !jacl_is_syntax(unquote_child))
        return -1;
    JaclSyntax *csyn = jacl_as_syntax(unquote_child);
    JaclVal name_val;
    if (csyn->kind == SYNTAX_LIT_STRING)
        name_val = csyn->data.lit_string.value;
    else if (csyn->kind == SYNTAX_VAR_REF)
        name_val = csyn->data.var_ref.name;
    else
        return -1;

    uint32_t nlen = jacl_string_byte_len(name_val);
    char nbuf[256];
    if (nlen >= sizeof(nbuf)) return -1;
    jacl_string_data(name_val, nbuf, nlen);
    for (uint32_t i = 0; i < param_count; i++) {
        if (nlen == param_name_lens[i] &&
            memcmp(nbuf, param_names[i], nlen) == 0)
            return (int)i;
    }
    return -1;
}

/* -------------------------------------------------------------------------
 * US-014: gensym — generate unique var-ref syntax objects for macro
 * temporaries. A global counter is incremented for each call so names are
 * unique across all macro expansions in a program. Names are formatted as
 * <prefix>__<counter>. For results ≤7 bytes, inline strings are used;
 * longer results use interned strings (pointer-stable, == comparison).
 * Prefixes up to 64 bytes are allowed. The resulting var-ref is flagged
 * is_gensym=1 so that mut/set/def accept it as a binding name (analogous
 * to is_caret).
 * ------------------------------------------------------------------------- */

/* Build a fresh gensym var-ref syntax object. Names are formatted as
 * prefix__counter. For names ≤7 bytes, uses jacl_inline_string; for
 * longer names, uses jacl_intern. scope_mark is set to the supplied mark;
 * is_gensym is set to 1. If prefix_len > 64, returns JACL_NIL and sets
 * *err to a static message. The gensym_counter pointer is incremented
 * atomically per call (caller owns the counter — typically in ExpandState
 * or a local variable for tests). */
JaclVal jacl_gensym_next(const char *prefix, uint32_t prefix_len,
                         ThreadHeap *heap, JaclInternTable *intern,
                         uint32_t *gensym_counter,
                         uint32_t scope_mark, const char **err) {
    if (err) *err = NULL;
    if (prefix_len == 0) { prefix = "g"; prefix_len = 1; }
    if (prefix_len > 64) {
        if (err) *err = "gensym prefix too long (max 64 bytes)";
        return JACL_NIL;
    }

    uint32_t counter = (*gensym_counter)++;

    /* Format: prefix__counter — max 64 + 2 + 10 = 76 bytes */
    char name_buf[80];
    uint32_t name_len = 0;
    memcpy(name_buf, prefix, prefix_len);
    name_len = prefix_len;
    name_buf[name_len++] = '_';
    name_buf[name_len++] = '_';

    /* Emit decimal digits of counter. */
    char digit_buf[16];
    int digit_count = 0;
    uint32_t tmp = counter;
    if (tmp == 0) { digit_buf[digit_count++] = '0'; }
    while (tmp > 0) {
        digit_buf[digit_count++] = '0' + (tmp % 10);
        tmp /= 10;
    }
    for (int i = digit_count - 1; i >= 0; i--)
        name_buf[name_len++] = digit_buf[i];
    name_buf[name_len] = '\0';

    JaclVal name_val;
    if (name_len <= 7)
        name_val = jacl_inline_string(name_buf, name_len);
    else
        name_val = jacl_intern(heap, intern, name_buf, name_len);
    JaclVal result = gc_alloc_syntax(heap);
    JaclSyntax *rsyn = jacl_as_syntax(result);
    rsyn->kind = SYNTAX_VAR_REF;
    rsyn->is_caret = 0;
    rsyn->is_gensym = 1;
    rsyn->scope_mark = scope_mark;
    rsyn->data.var_ref.name = name_val;
    return result;
}

/* Returns true if tmpl is a [gensym] or [gensym "prefix"] call, and fills
 * prefix_out/prefix_len_out with the prefix string (default "g").
 * prefix_out must be at least 65 bytes. Prefixes up to 64 bytes accepted. */
static bool expand__is_gensym_call(JaclVal tmpl,
                                   char prefix_out[65],
                                   uint32_t *prefix_len_out) {
    if (!jacl_is_syntax(tmpl)) return false;
    JaclSyntax *syn = jacl_as_syntax(tmpl);
    if (syn->kind != SYNTAX_COMMAND) return false;

    JaclVal head = syn->data.command.head;
    if (!jacl_is_syntax(head)) return false;
    JaclSyntax *hsyn = jacl_as_syntax(head);
    if (hsyn->kind != SYNTAX_LIT_STRING) return false;
    JaclVal head_name = hsyn->data.lit_string.value;
    uint32_t hlen = jacl_string_byte_len(head_name);
    if (hlen != 6) return false;
    char hbuf[16];
    jacl_string_data(head_name, hbuf, sizeof(hbuf));
    if (memcmp(hbuf, "gensym", 6) != 0) return false;

    jacl_vec_root *args =
        (jacl_vec_root *)jacl_as_ptr(syn->data.command.args);
    uint32_t argc = jacl_vec_count(args);
    if (argc == 0) {
        prefix_out[0] = 'g';
        prefix_out[1] = '\0';
        *prefix_len_out = 1;
        return true;
    }
    if (argc != 1) return false;

    JaclVal arg = jacl_vec_get(args, 0).value;
    if (!jacl_is_syntax(arg)) return false;
    JaclSyntax *asyn = jacl_as_syntax(arg);
    if (asyn->kind != SYNTAX_LIT_STRING) return false;
    JaclVal prefix_val = asyn->data.lit_string.value;
    uint32_t plen = jacl_string_byte_len(prefix_val);
    if (plen == 0 || plen > 64) return false;
    jacl_string_data(prefix_val, prefix_out, 65);
    prefix_out[plen] = '\0';
    *prefix_len_out = plen;
    return true;
}

/* -------------------------------------------------------------------------
 * expand__subst_template: walk a syntax object template from a syntax-quote,
 * replacing SYNTAX_UNQUOTE nodes by matching their child name against macro
 * parameter names. SYNTAX_UNQUOTE_SPLICING flattens into arg/cmd lists.
 *
 * This is the template-based expansion approach: no VM execution needed.
 * Works because macro bodies are syntax-quote templates with ~name holes.
 * ------------------------------------------------------------------------- */

static JaclVal expand__subst_template(JaclVal tmpl,
                                       const char **param_names,
                                       uint32_t *param_name_lens,
                                       JaclVal *arg_vals,
                                       uint32_t param_count,
                                       ThreadHeap *heap,
                                       JaclInternTable *intern,
                                       ExpandState *es) {
    if (jacl_is_nil(tmpl)) return JACL_NIL;
    if (!jacl_is_syntax(tmpl)) return tmpl;

    JaclSyntax *syn = jacl_as_syntax(tmpl);

    /* US-014: detect [gensym] / [gensym "prefix"] calls in the template
     * and replace them with a fresh gensym var-ref. The var-ref inherits
     * the current template mark (set by expand__stamp_syntax) so the
     * binding is hygienic within the macro's own scope. */
    {
        char prefix[65];
        uint32_t prefix_len;
        if (expand__is_gensym_call(tmpl, prefix, &prefix_len)) {
            const char *err = NULL;
            return jacl_gensym_next(prefix, prefix_len, heap, intern,
                                    &es->gensym_counter,
                                    syn->scope_mark, &err);
        }
    }

    /* Unquote: look up the child name and substitute with arg value */
    if (syn->kind == SYNTAX_UNQUOTE) {
        int idx = expand__match_param(syn->data.unquote.child,
                                       param_names, param_name_lens,
                                       param_count);
        if (idx >= 0) return arg_vals[idx];
        return JACL_NIL;
    }

    /* Unquote-splicing: handled by parent (command/block).
     * If we reach here, it means ~@ at a non-spliceable position. */
    if (syn->kind == SYNTAX_UNQUOTE_SPLICING) {
        int idx = expand__match_param(syn->data.unquote_splicing.child,
                                       param_names, param_name_lens,
                                       param_count);
        if (idx >= 0) return arg_vals[idx];
        return JACL_NIL;
    }

    /* Command: recurse into head and args, flatten ~@ in args */
    if (syn->kind == SYNTAX_COMMAND) {
        JaclVal new_head = expand__subst_template(
            syn->data.command.head, param_names, param_name_lens,
            arg_vals, param_count, heap, intern, es);
        jacl_vec_root *old_args =
            (jacl_vec_root *)jacl_as_ptr(syn->data.command.args);
        uint32_t argc = jacl_vec_count(old_args);
        jacl_vec_root *new_args = jacl_vec_empty();
        for (uint32_t i = 0; i < argc; i++) {
            JaclVal arg = jacl_vec_get(old_args, i).value;
            if (jacl_is_syntax(arg)) {
                JaclSyntax *asyn = jacl_as_syntax(arg);
                if (asyn->kind == SYNTAX_UNQUOTE_SPLICING) {
                    /* Flatten: look up the splice value (should be a vec) */
                    int idx = expand__match_param(
                        asyn->data.unquote_splicing.child,
                        param_names, param_name_lens, param_count);
                    if (idx >= 0) {
                        JaclVal splice_val = arg_vals[idx];
                        if (jacl_is_vector(splice_val)) {
                            jacl_vec_root *sv =
                                (jacl_vec_root *)jacl_as_ptr(splice_val);
                            uint32_t sc = jacl_vec_count(sv);
                            for (uint32_t j = 0; j < sc; j++)
                                new_args = jacl_vec_push_back(new_args,
                                    jacl_vec_get(sv, j).value);
                        } else {
                            new_args = jacl_vec_push_back(new_args, splice_val);
                        }
                    }
                    continue;
                }
            }
            new_args = jacl_vec_push_back(new_args,
                expand__subst_template(arg, param_names, param_name_lens,
                                        arg_vals, param_count, heap, intern, es));
        }
        JaclVal result = gc_alloc_syntax(heap);
        JaclSyntax *rsyn = jacl_as_syntax(result);
        rsyn->kind = SYNTAX_COMMAND;
        rsyn->pos_line = syn->pos_line;
        rsyn->pos_col = syn->pos_col;
        rsyn->pos_offset = syn->pos_offset;
        rsyn->scope_mark = syn->scope_mark;
        rsyn->data.command.head = new_head;
        rsyn->data.command.args = jacl_vector_ptr(new_args);
        return result;
    }

    /* Block: recurse, flatten ~@ in command list */
    if (syn->kind == SYNTAX_BLOCK) {
        jacl_vec_root *old_cmds =
            (jacl_vec_root *)jacl_as_ptr(syn->data.block.commands);
        uint32_t cnt = jacl_vec_count(old_cmds);
        jacl_vec_root *new_cmds = jacl_vec_empty();
        for (uint32_t i = 0; i < cnt; i++) {
            JaclVal cmd = jacl_vec_get(old_cmds, i).value;
            if (jacl_is_syntax(cmd)) {
                JaclSyntax *csyn = jacl_as_syntax(cmd);
                if (csyn->kind == SYNTAX_UNQUOTE_SPLICING) {
                    int idx = expand__match_param(
                        csyn->data.unquote_splicing.child,
                        param_names, param_name_lens, param_count);
                    if (idx >= 0) {
                        JaclVal splice_val = arg_vals[idx];
                        if (jacl_is_vector(splice_val)) {
                            jacl_vec_root *sv =
                                (jacl_vec_root *)jacl_as_ptr(splice_val);
                            uint32_t sc = jacl_vec_count(sv);
                            for (uint32_t j = 0; j < sc; j++)
                                new_cmds = jacl_vec_push_back(new_cmds,
                                    jacl_vec_get(sv, j).value);
                        } else {
                            new_cmds = jacl_vec_push_back(new_cmds, splice_val);
                        }
                    }
                    continue;
                }
            }
            new_cmds = jacl_vec_push_back(new_cmds,
                expand__subst_template(cmd, param_names, param_name_lens,
                                        arg_vals, param_count, heap, intern, es));
        }
        JaclVal result = gc_alloc_syntax(heap);
        JaclSyntax *rsyn = jacl_as_syntax(result);
        rsyn->kind = SYNTAX_BLOCK;
        rsyn->pos_line = syn->pos_line;
        rsyn->pos_col = syn->pos_col;
        rsyn->pos_offset = syn->pos_offset;
        rsyn->scope_mark = syn->scope_mark;
        rsyn->data.block.commands = jacl_vector_ptr(new_cmds);
        return result;
    }

    /* Nested syntax-quote: treat as literal */
    if (syn->kind == SYNTAX_SYNTAX_QUOTE) return tmpl;

    /* All other kinds (literals, var-ref, etc.) — return as-is */
    return tmpl;
}

/* -------------------------------------------------------------------------
 * expand__find_syntax_quote_child: extract the syntax-quote template from
 * a defmacro body AST. The body is a block containing a syntax-quote expr.
 * Returns the syntax-quote child, or NULL if not a simple template.
 * ------------------------------------------------------------------------- */

static AstNode *expand__find_syntax_quote_child(AstNode *body) {
    if (!body) return NULL;
    if (body->type == AST_SYNTAX_QUOTE)
        return body->data.syntax_quote.child;
    if (body->type == AST_BLOCK) {
        /* Block with one statement that is syntax-quote */
        if (body->data.block.count == 1) {
            AstNode *stmt = body->data.block.commands[0];
            if (stmt && stmt->type == AST_SYNTAX_QUOTE)
                return stmt->data.syntax_quote.child;
        }
    }
    return NULL;
}

/* Forward declare the recursive expansion helper */
static bool expand__node(AstNode **node_ptr, MacroTable *macros,
                         ThreadHeap *heap, JaclInternTable *intern,
                         arena_t *arena, ExpandState *es, uint32_t depth);

static bool expand__block(AstNode *block, MacroTable *macros,
                          ThreadHeap *heap, JaclInternTable *intern,
                          arena_t *arena, ExpandState *es, uint32_t depth) {
    if (!block || block->type != AST_BLOCK) return true;
    for (uint32_t i = 0; i < block->data.block.count; i++) {
        if (!expand__node(&block->data.block.commands[i], macros, heap,
                          intern, arena, es, depth))
            return false;
    }
    return true;
}

static bool expand__node(AstNode **node_ptr, MacroTable *macros,
                         ThreadHeap *heap, JaclInternTable *intern,
                         arena_t *arena, ExpandState *es, uint32_t depth) {
    AstNode *node = *node_ptr;
    if (!node) return true;

    if (depth > 256) {
        expand__set_error(es, "macro expansion depth limit exceeded",
                          node->start.line, node->start.column, arena);
        return false;
    }

    switch (node->type) {
    case AST_COMMAND: {
        const char *name;
        uint32_t name_len;
        if (expand__head_is_name(node, &name, &name_len)) {
            MacroEntry *entry = macro_table_lookup(macros, name, name_len);
            if (entry) {
                /* Macro call found — expand it */
                uint32_t argc = node->data.command.arg_count;
                if (argc != entry->param_count) {
                    char err[256];
                    snprintf(err, sizeof(err),
                             "macro '%.*s' expects %u arguments but got %u",
                             (int)name_len, name,
                             entry->param_count, argc);
                    expand__set_error(es, err, node->start.line,
                                      node->start.column, arena);
                    return false;
                }

                JaclVal result_syn;

                if (entry->use_staged_eval && entry->closure && es->ctx) {
                    /* ---- US-008 staged path ---- */
                    /* Convert args to syntax objects */
                    JaclVal arg_vals[64];
                    for (uint32_t i = 0; i < argc; i++) {
                        arg_vals[i] = syntax_from_ast(
                            node->data.command.args[i], heap, intern);
                    }

                    /* US-009: allocate fresh scope mark for hygiene.
                     * Set it on the VM so make-syntax ops can apply it. */
                    uint32_t macro_mark = ++es->scope_counter;
                    es->ctx->vm.macro_scope_mark = macro_mark;

                    /* Invoke the compiled macro closure */
                    JaclError merr;
                    result_syn = jacl_ctx_run_closure(
                        es->ctx, entry->closure, arg_vals, argc, &merr);

                    /* Reset scope mark after invocation */
                    es->ctx->vm.macro_scope_mark = 0;

                    if (merr.kind != JACL_ERROR_NONE) {
                        char err[256];
                        snprintf(err, sizeof(err),
                                 "staged macro '%.*s': %s",
                                 (int)name_len, name,
                                 merr.message ? merr.message : "runtime error");
                        expand__set_error(es, err, node->start.line,
                                          node->start.column, arena);
                        return false;
                    }

                    if (!jacl_is_syntax(result_syn)) {
                        char err[256];
                        snprintf(err, sizeof(err),
                                 "staged macro '%.*s' must return a syntax object",
                                 (int)name_len, name);
                        expand__set_error(es, err, node->start.line,
                                          node->start.column, arena);
                        return false;
                    }
                } else {
                    /* ---- Legacy template substitution path ---- */

                    /* Find the syntax-quote template in the body */
                    AstNode *tmpl_ast = expand__find_syntax_quote_child(
                        entry->body);
                    if (!tmpl_ast) {
                        char err[256];
                        snprintf(err, sizeof(err),
                                 "macro '%.*s' body must be a syntax-quote template",
                                 (int)name_len, name);
                        expand__set_error(es, err, node->start.line,
                                          node->start.column, arena);
                        return false;
                    }

                    /* Convert template and arguments to syntax objects */
                    JaclVal tmpl_syn = syntax_from_ast(tmpl_ast, heap, intern);

                    /* Fresh scope mark for this expansion */
                    uint32_t macro_mark = ++es->scope_counter;
                    expand__stamp_syntax(tmpl_syn, macro_mark);

                    JaclVal arg_vals[64];
                    for (uint32_t i = 0; i < argc; i++) {
                        arg_vals[i] = syntax_from_ast(
                            node->data.command.args[i], heap, intern);
                    }

                    result_syn = expand__subst_template(
                        tmpl_syn,
                        entry->param_names, entry->param_name_lens,
                        arg_vals, entry->param_count, heap, intern, es);
                }

                /* Convert result back to AstNode */
                AstNode *expanded = syntax_to_ast(result_syn, arena);
                if (!expanded) {
                    expand__set_error(es, "macro expansion produced invalid syntax",
                                      node->start.line,
                                      node->start.column, arena);
                    return false;
                }

                /* US-018: capture the ORIGINAL call site BEFORE replacement */
                uint32_t orig_line = node->start.line;
                uint32_t orig_col  = node->start.column;

                /* Replace the node and re-expand (iterative expansion) */
                *node_ptr = expanded;
                expand__push_frame(es, name, name_len, orig_line, orig_col);
                bool ok = expand__node(node_ptr, macros, heap, intern, arena,
                                       es, depth + 1);
                expand__pop_frame(es);
                return ok;
            }
        }

        /* Not a macro — recurse into head and args */
        if (!expand__node(&node->data.command.head, macros, heap, intern,
                          arena, es, depth))
            return false;
        for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
            if (!expand__node(&node->data.command.args[i], macros, heap,
                              intern, arena, es, depth))
                return false;
        }
        return true;
    }

    case AST_BLOCK:
        return expand__block(node, macros, heap, intern, arena, es, depth);

    case AST_QUOTE:
        /* Don't expand inside quote */
        return true;

    case AST_SYNTAX_QUOTE:
        /* Don't expand inside syntax-quote */
        return true;

    default:
        return true;
    }
}

/* Main expansion pass: walk program top-to-bottom.
 * defmacro nodes are registered in the macro table; macro calls are expanded
 * via template-based substitution (walking the syntax-quote template and
 * replacing unquote holes by matching parameter names).
 * Returns NULL on success, or an error message string on failure.
 * Sets *out_line and *out_col to the error location on failure. */
/* US-008: Compile a staged macro body into a closure.
 * Called during expansion (before the main compilation pass) so the closure
 * is available for jacl_ctx_run_closure when a staged macro is invoked. */
static const char *expand__compile_staged_body(MacroEntry *entry,
                                               ThreadHeap *heap,
                                               JaclInternTable *intern,
                                               arena_t *arena) {
    uint32_t param_count = entry->param_count;

    JaclClosure *closure = (JaclClosure *)arena_alloc(arena, sizeof(JaclClosure));
    memset(closure, 0, sizeof(JaclClosure));
    chunk_init(&closure->chunk, arena);
    closure->param_count = (uint8_t)param_count;
    closure->min_args    = (uint8_t)param_count;
    closure->variadic    = false;

    char *name_copy = (char *)arena_alloc(arena, entry->name_len + 1);
    memcpy(name_copy, entry->name, entry->name_len);
    name_copy[entry->name_len] = '\0';
    closure->name = name_copy;

    if (param_count > 0) {
        closure->param_names = (JaclVal *)arena_alloc(arena,
                                   sizeof(JaclVal) * param_count);
        for (uint32_t pi = 0; pi < param_count; pi++) {
            closure->param_names[pi] = compiler__name_val(heap, intern,
                entry->param_names[pi], entry->param_name_lens[pi]);
        }
    }

    Compiler body_compiler;
    compiler__init(&body_compiler, &closure->chunk, arena, intern, heap);
    body_compiler.scope_depth = 1;
    body_compiler.use_staged_syntax_quote = true;

    for (uint32_t pi = 0; pi < param_count; pi++) {
        compiler__add_local(&body_compiler, closure->param_names[pi],
                            0 /*line*/, 0 /*col*/);
        body_compiler.locals[body_compiler.local_count - 1].is_param = true;
    }

    compiler__compile_block_expr(&body_compiler, entry->body);
    compiler__emit_byte(&body_compiler, OP_RETURN, 0);

    if (body_compiler.error_count > 0) {
        return body_compiler.first_error ? body_compiler.first_error
                                          : "staged macro body compilation failed";
    }

    closure->upvalue_count = (uint8_t)body_compiler.upvalue_count;
    entry->closure = closure;
    return NULL;
}

const char *ast_expand_macros(AstNode **program, uint32_t count,
                              MacroTable *macros, ThreadHeap *heap,
                              JaclInternTable *intern, arena_t *arena,
                              ExpandState *es,
                              uint32_t *out_error_line, uint32_t *out_error_col) {
    /* Initialize expansion state for this compilation pass */
    ExpandState local_es;
    if (!es) {
        memset(&local_es, 0, sizeof(local_es));
        es = &local_es;
    } else {
        es->error_msg  = NULL;
        es->error_line = 0;
        es->error_col  = 0;
        es->frame_top  = 0;
    }

    bool staged_mode = es->staged_syntax_quote;

    if (staged_mode) {
        /* --- US-008 three-phase staged approach ---
         * Phase 1: Register ALL defmacros first (enables forward references).
         * Phase 2: Compile staged macro bodies.
         * Phase 3: Expand macro calls in non-defmacro statements. */

        /* Phase 1: Register all defmacros */
        for (uint32_t i = 0; i < count; i++) {
            AstNode *node = program[i];
            if (!node || node->type != AST_DEFMACRO) continue;

            const char *mname     = node->data.defmacro.name;
            uint32_t    mname_len = node->data.defmacro.name_len;

            if (macro__is_special_form(mname, mname_len)) {
                char err[128];
                snprintf(err, sizeof(err),
                         "defmacro: '%.*s' shadows a special form",
                         (int)mname_len, mname);
                char *msg = (char *)arena_alloc(arena, strlen(err) + 1);
                memcpy(msg, err, strlen(err) + 1);
                *out_error_line = node->start.line;
                *out_error_col  = node->start.column;
                return msg;
            }

            if (macro_table_lookup(macros, mname, mname_len)) {
                char err[128];
                snprintf(err, sizeof(err),
                         "defmacro: '%.*s' is already defined",
                         (int)mname_len, mname);
                char *msg = (char *)arena_alloc(arena, strlen(err) + 1);
                memcpy(msg, err, strlen(err) + 1);
                *out_error_line = node->start.line;
                *out_error_col  = node->start.column;
                return msg;
            }

            if (macros->count >= MACRO_TABLE_MAX) {
                *out_error_line = node->start.line;
                *out_error_col  = node->start.column;
                return "too many macro definitions";
            }

            uint32_t param_count = node->data.defmacro.param_count;
            char *name_copy = (char *)arena_alloc(arena, mname_len + 1);
            memcpy(name_copy, mname, mname_len);
            name_copy[mname_len] = '\0';

            MacroEntry *entry = &macros->entries[macros->count++];
            entry->name           = name_copy;
            entry->name_len       = mname_len;
            entry->param_count    = param_count;
            entry->param_names    = node->data.defmacro.param_names;
            entry->param_name_lens = node->data.defmacro.param_name_lens;
            entry->closure        = NULL;
            entry->body           = node->data.defmacro.body;
            entry->use_staged_eval = true;
        }

        /* Phase 2: Compile staged macro bodies */
        for (uint32_t i = 0; i < macros->count; i++) {
            MacroEntry *entry = &macros->entries[i];
            if (!entry->use_staged_eval || entry->closure) continue;
            const char *err = expand__compile_staged_body(entry, heap,
                                                           intern, arena);
            if (err) {
                *out_error_line = 0;
                *out_error_col  = 0;
                return err;
            }
        }

        /* Phase 3: Expand macro calls */
        for (uint32_t i = 0; i < count; i++) {
            AstNode *node = program[i];
            if (!node || node->type == AST_DEFMACRO) continue;
            if (!expand__node(&program[i], macros, heap, intern, arena, es, 0)) {
                *out_error_line = es->error_line;
                *out_error_col  = es->error_col;
                return es->error_msg;
            }
        }
    } else {
        /* --- Original single-pass approach (non-staged) ---
         * Registration and expansion are interleaved: a macro must be
         * defined before it is used. */
        for (uint32_t i = 0; i < count; i++) {
            AstNode *node = program[i];
            if (!node) continue;

            if (node->type == AST_DEFMACRO) {
                const char *mname     = node->data.defmacro.name;
                uint32_t    mname_len = node->data.defmacro.name_len;

                if (macro__is_special_form(mname, mname_len)) {
                    char err[128];
                    snprintf(err, sizeof(err),
                             "defmacro: '%.*s' shadows a special form",
                             (int)mname_len, mname);
                    char *msg = (char *)arena_alloc(arena, strlen(err) + 1);
                    memcpy(msg, err, strlen(err) + 1);
                    *out_error_line = node->start.line;
                    *out_error_col  = node->start.column;
                    return msg;
                }

                if (macro_table_lookup(macros, mname, mname_len)) {
                    char err[128];
                    snprintf(err, sizeof(err),
                             "defmacro: '%.*s' is already defined",
                             (int)mname_len, mname);
                    char *msg = (char *)arena_alloc(arena, strlen(err) + 1);
                    memcpy(msg, err, strlen(err) + 1);
                    *out_error_line = node->start.line;
                    *out_error_col  = node->start.column;
                    return msg;
                }

                if (macros->count >= MACRO_TABLE_MAX) {
                    *out_error_line = node->start.line;
                    *out_error_col  = node->start.column;
                    return "too many macro definitions";
                }

                uint32_t param_count = node->data.defmacro.param_count;
                char *name_copy = (char *)arena_alloc(arena, mname_len + 1);
                memcpy(name_copy, mname, mname_len);
                name_copy[mname_len] = '\0';

                MacroEntry *entry = &macros->entries[macros->count++];
                entry->name           = name_copy;
                entry->name_len       = mname_len;
                entry->param_count    = param_count;
                entry->param_names    = node->data.defmacro.param_names;
                entry->param_name_lens = node->data.defmacro.param_name_lens;
                entry->closure        = NULL;
                entry->body           = node->data.defmacro.body;
                entry->use_staged_eval = false;
                continue;
            }

            if (!expand__node(&program[i], macros, heap, intern, arena, es, 0)) {
                *out_error_line = es->error_line;
                *out_error_col  = es->error_col;
                return es->error_msg;
            }
        }
    }

    return NULL;  /* success */
}

/* -------------------------------------------------------------------------
 * US-015: Syntax introspection helpers
 * ------------------------------------------------------------------------- */

/* Return a short human-readable name for a SyntaxKind (used by syntax-kind). */
const char *syntax_kind_name(uint8_t kind) {
    switch ((SyntaxKind)kind) {
    case SYNTAX_COMMAND:           return "command";
    case SYNTAX_LIT_INT:           return "lit-int";
    case SYNTAX_LIT_FLOAT:         return "lit-float";
    case SYNTAX_LIT_STRING:        return "lit-string";
    case SYNTAX_VAR_REF:           return "var-ref";
    case SYNTAX_BLOCK:             return "block";
    case SYNTAX_INTERP_STRING:     return "interp-string";
    case SYNTAX_SPREAD:            return "spread";
    case SYNTAX_USE:               return "use";
    case SYNTAX_DEFSTRUCT:         return "defstruct";
    case SYNTAX_DEFMACRO:          return "defmacro";
    case SYNTAX_QUOTE:             return "quote";
    case SYNTAX_SYNTAX_QUOTE:      return "syntax-quote";
    case SYNTAX_UNQUOTE:           return "unquote";
    case SYNTAX_UNQUOTE_SPLICING:  return "unquote-splicing";
    case SYNTAX_BREAK:             return "break";
    case SYNTAX_CONTINUE:          return "continue";
    case SYNTAX_RETURN:            return "return";
    case SYNTAX_DESTRUCTURE_VEC:   return "destructure-vec";
    case SYNTAX_DESTRUCTURE_NAMED: return "destructure-named";
    }
    return "unknown";
}

#endif /* SYNTAX_C */
