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

/* Include generated prelude source */
#include "prelude_source.h"

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

    case AST_SHELL_CMD: {
        /* Shell commands convert to SYNTAX_COMMAND with "exec" as head
         * and command name + args as the arguments.
         * This matches the compiler's transformation of !cmd to [exec ...] */
        syn->kind = SYNTAX_COMMAND;
        /* Create head = syntax object for "exec" string */
        JaclVal exec_syn_val = gc_alloc_syntax(heap);
        JaclSyntax *exec_syn = jacl_as_syntax(exec_syn_val);
        exec_syn->kind = SYNTAX_LIT_STRING;
        exec_syn->data.lit_string.value = jacl_intern(heap, intern, "exec", 4);
        syntax__set_pos(exec_syn, node);
        syn->data.command.head = exec_syn_val;

        /* Build args: [cmd_head, arg1, arg2, ...] */
        uint32_t argc = node->data.shell_cmd.arg_count;
        jacl_vec_root *args = jacl_vec_empty();
        /* First arg is the command name */
        JaclVal head_arg = syntax_from_ast(node->data.shell_cmd.head,
                                           heap, intern);
        args = jacl_vec_push_back(args, head_arg);
        /* Add remaining args */
        for (uint32_t i = 0; i < argc; i++) {
            JaclVal arg = syntax_from_ast(node->data.shell_cmd.args[i],
                                          heap, intern);
            args = jacl_vec_push_back(args, arg);
        }
        syn->data.command.args = jacl_vector_ptr(args);
        break;
    }

    case AST_CTX_DECL:
        /* ctx declarations are handled by the compiler, not syntax objects */
        return JACL_NIL;

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
                uint32_t min_args = entry->variadic
                    ? (entry->param_count > 0 ? entry->param_count - 1 : 0)
                    : entry->param_count;
                if (entry->variadic ? (argc < min_args) : (argc != entry->param_count)) {
                    char err[256];
                    if (entry->variadic) {
                        snprintf(err, sizeof(err),
                                 "macro '%.*s' expects at least %u arguments but got %u",
                                 (int)name_len, name, min_args, argc);
                    } else {
                        snprintf(err, sizeof(err),
                                 "macro '%.*s' expects %u arguments but got %u",
                                 (int)name_len, name,
                                 entry->param_count, argc);
                    }
                    expand__set_error(es, err, node->start.line,
                                      node->start.column, arena);
                    return false;
                }

                if (!entry->closure || !es->ctx) {
                    char err[256];
                    snprintf(err, sizeof(err),
                             "macro '%.*s' has no compiled closure",
                             (int)name_len, name);
                    expand__set_error(es, err, node->start.line,
                                      node->start.column, arena);
                    return false;
                }

                /* Convert args to syntax objects.
                 * For variadic macros, collect extra args into a vec. */
                JaclVal arg_vals[64];
                uint32_t call_argc;
                if (entry->variadic) {
                    /* Fixed params: 0..min_args-1, rest collected into vec */
                    for (uint32_t i = 0; i < min_args; i++) {
                        arg_vals[i] = syntax_from_ast(
                            node->data.command.args[i], heap, intern);
                    }
                    /* Build vec from remaining args */
                    jacl_vec_root *rest_root = jacl_vec_empty();
                    for (uint32_t i = min_args; i < argc; i++) {
                        JaclVal syn_arg = syntax_from_ast(
                            node->data.command.args[i], heap, intern);
                        rest_root = jacl_vec_push_back(rest_root, syn_arg);
                    }
                    arg_vals[min_args] = jacl_vector_ptr(rest_root);
                    call_argc = entry->param_count;
                } else {
                    for (uint32_t i = 0; i < argc; i++) {
                        arg_vals[i] = syntax_from_ast(
                            node->data.command.args[i], heap, intern);
                    }
                    call_argc = argc;
                }

                /* Allocate fresh scope mark for hygiene.
                 * Set it on the VM so make-syntax ops can apply it. */
                uint32_t macro_mark = ++es->scope_counter;
                es->ctx->vm.macro_scope_mark = macro_mark;

                /* Expose gensym counter to the VM so the
                 * gensym builtin (subop 16) can allocate fresh names. */
                es->ctx->vm.gensym_counter_ptr = &es->gensym_counter;

                /* Invoke the compiled macro closure */
                JaclError merr;
                JaclVal result_syn = jacl_ctx_run_closure(
                    es->ctx, entry->closure, arg_vals, call_argc, &merr);

                /* Reset scope mark and gensym counter pointer after invocation */
                es->ctx->vm.macro_scope_mark = 0;
                es->ctx->vm.gensym_counter_ptr = NULL;

                if (merr.kind != JACL_ERROR_NONE) {
                    char err[256];
                    snprintf(err, sizeof(err),
                             "macro '%.*s': %s",
                             (int)name_len, name,
                             merr.message ? merr.message : "runtime error");
                    expand__set_error(es, err, node->start.line,
                                      node->start.column, arena);
                    return false;
                }

                if (!jacl_is_syntax(result_syn)) {
                    char err[256];
                    snprintf(err, sizeof(err),
                             "macro '%.*s' must return a syntax object",
                             (int)name_len, name);
                    expand__set_error(es, err, node->start.line,
                                      node->start.column, arena);
                    return false;
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

/* Compile a macro body into a closure.
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
    closure->min_args    = entry->variadic
                             ? (uint8_t)(param_count > 0 ? param_count - 1 : 0)
                             : (uint8_t)param_count;
    closure->variadic    = entry->variadic;

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

    /* Three-phase staged approach:
     * Phase 1: Register ALL defmacros first (enables forward references).
     * Phase 2: Compile macro bodies into closures.
     * Phase 3: Expand macro calls in non-defmacro statements. */

    /* Create a temporary context for macro expansion if none provided */
    jacl_context_t *tmp_ctx = NULL;
    jacl_ctx_saved_t saved_ctx = {0};
    bool have_saved_ctx = false;
    if (!es->ctx) {
        jacl_ctx_save(&saved_ctx);
        have_saved_ctx = true;
        tmp_ctx = jacl_ctx_new(NULL);
        es->ctx = tmp_ctx;
    }

    /* Phase 0: Register built-in macros from prelude.jacl.
     * The prelude is parsed and compiled once into persistent static storage
     * (arena, heap, intern table) so we pay the full cost only on first use.
     * The cached closures are reused directly on every subsequent call. */
    #define PRELUDE_MAX_MACROS 32
    static arena_t          expand__prelude_arena   = {0};
    static BlockPool        expand__prelude_pool;
    static ThreadHeap       expand__prelude_heap;
    static JaclInternTable  expand__prelude_intern;
    static MacroEntry       expand__prelude_macros[PRELUDE_MAX_MACROS];
    static uint32_t         expand__prelude_count   = 0;
    static bool             expand__prelude_ready   = false;

    {
        if (!expand__prelude_ready) {
            /* Use prelude source from generated prelude_source.h */

            /* Parse the prelude */
            LexResult ltoks = lexer_lex(jacl_prelude_source, &expand__prelude_arena);
            ParseResult ppre = parser_parse(ltoks, &expand__prelude_arena);

            /* Initialize heap/intern once for all prelude macros */
            gc_block_pool_init(&expand__prelude_pool);
            gc_heap_init(&expand__prelude_heap, &expand__prelude_pool);
            intern_table_init(&expand__prelude_intern, &expand__prelude_arena);

            ThreadHeap *prev_heap = gc__current_heap;
            gc__current_heap = &expand__prelude_heap;

            /* Compile all defmacros from prelude */
            for (uint32_t pi = 0; pi < ppre.count && expand__prelude_count < PRELUDE_MAX_MACROS; pi++) {
                AstNode *node = ppre.nodes[pi];
                if (!node || node->type != AST_DEFMACRO) continue;

                MacroEntry *me = &expand__prelude_macros[expand__prelude_count];
                me->name           = node->data.defmacro.name;
                me->name_len       = node->data.defmacro.name_len;
                me->param_count    = node->data.defmacro.param_count;
                me->variadic       = node->data.defmacro.variadic;
                me->param_names    = node->data.defmacro.param_names;
                me->param_name_lens = node->data.defmacro.param_name_lens;
                me->body           = node->data.defmacro.body;
                me->closure        = NULL;
                me->is_builtin     = true;

                expand__compile_staged_body(me,
                    &expand__prelude_heap, &expand__prelude_intern,
                    &expand__prelude_arena);

                expand__prelude_count++;
            }

            gc__current_heap = prev_heap;
            expand__prelude_ready = true;
        }

        /* Register all prelude macros */
        for (uint32_t i = 0; i < expand__prelude_count && macros->count < MACRO_TABLE_MAX; i++) {
            MacroEntry *src = &expand__prelude_macros[i];
            MacroEntry *pe = &macros->entries[macros->count++];
            pe->name           = src->name;
            pe->name_len       = src->name_len;
            pe->param_count    = src->param_count;
            pe->variadic       = src->variadic;
            pe->param_names    = src->param_names;
            pe->param_name_lens = src->param_name_lens;
            pe->body           = src->body;
            pe->is_builtin     = true;
            pe->closure        = src->closure;
        }
    }

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
            if (tmp_ctx) { jacl_ctx_destroy(tmp_ctx); es->ctx = NULL; jacl_ctx_restore(saved_ctx); }
            return msg;
        }

        {
            MacroEntry *existing = macro_table_lookup(macros, mname, mname_len);
            if (existing) {
                if (existing->is_builtin) {
                    /* Allow user to override built-in macros (like \).
                     * Replace the entry in-place. */
                    existing->param_count    = node->data.defmacro.param_count;
                    existing->variadic       = node->data.defmacro.variadic;
                    existing->param_names    = node->data.defmacro.param_names;
                    existing->param_name_lens = node->data.defmacro.param_name_lens;
                    existing->closure        = NULL;
                    existing->body           = node->data.defmacro.body;
                    existing->is_builtin     = false;
                    continue;
                }
                char err[128];
                snprintf(err, sizeof(err),
                         "defmacro: '%.*s' already defined",
                         (int)mname_len, mname);
                char *msg = (char *)arena_alloc(arena, strlen(err) + 1);
                memcpy(msg, err, strlen(err) + 1);
                *out_error_line = node->start.line;
                *out_error_col  = node->start.column;
                if (tmp_ctx) { jacl_ctx_destroy(tmp_ctx); es->ctx = NULL; jacl_ctx_restore(saved_ctx); }
                return msg;
            }
        }

        if (macros->count >= MACRO_TABLE_MAX) {
            *out_error_line = node->start.line;
            *out_error_col  = node->start.column;
            if (tmp_ctx) { jacl_ctx_destroy(tmp_ctx); es->ctx = NULL; jacl_ctx_restore(saved_ctx); }
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
        entry->variadic       = node->data.defmacro.variadic;
        entry->param_names    = node->data.defmacro.param_names;
        entry->param_name_lens = node->data.defmacro.param_name_lens;
        entry->closure        = NULL;
        entry->body           = node->data.defmacro.body;
        entry->is_builtin     = false;
    }

    /* Phase 2: Compile macro bodies */
    for (uint32_t i = 0; i < macros->count; i++) {
        MacroEntry *entry = &macros->entries[i];
        if (entry->closure) continue;
        const char *err = expand__compile_staged_body(entry, heap,
                                                       intern, arena);
        if (err) {
            *out_error_line = 0;
            *out_error_col  = 0;
            if (tmp_ctx) { jacl_ctx_destroy(tmp_ctx); es->ctx = NULL; jacl_ctx_restore(saved_ctx); }
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
            if (tmp_ctx) { jacl_ctx_destroy(tmp_ctx); es->ctx = NULL; jacl_ctx_restore(saved_ctx); }
            return es->error_msg;
        }
    }

    if (tmp_ctx) {
        jacl_ctx_destroy(tmp_ctx);
        es->ctx = NULL;
        jacl_ctx_restore(saved_ctx);
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
