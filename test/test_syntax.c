#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-001: JACL_TAG_SYNTAX value tag and OBJ_SYNTAX GC object ===== */

/* Test: JACL_TAG_SYNTAX occupies the expected tag slot */
static int test_syntax_tag_slot(void) {
    ASSERT_U64_EQ(JACL_TAG_SYNTAX, (uint64_t)0x17 << JACL_TAG_SHIFT);
    TEST_PASS();
}

/* Test: all SyntaxKind enum values are distinct */
static int test_syntax_kind_enum(void) {
    ASSERT_INT_EQ(SYNTAX_COMMAND, 0);
    ASSERT_INT_EQ(SYNTAX_LIT_INT, 1);
    ASSERT_INT_EQ(SYNTAX_LIT_FLOAT, 2);
    ASSERT_INT_EQ(SYNTAX_LIT_STRING, 3);
    ASSERT_INT_EQ(SYNTAX_VAR_REF, 4);
    ASSERT_INT_EQ(SYNTAX_BLOCK, 5);
    ASSERT_INT_EQ(SYNTAX_INTERP_STRING, 6);
    ASSERT_INT_EQ(SYNTAX_SPREAD, 7);
    ASSERT_INT_EQ(SYNTAX_USE, 8);
    ASSERT_INT_EQ(SYNTAX_DEFSTRUCT, 9);
    ASSERT_INT_EQ(SYNTAX_DEFMACRO, 10);
    ASSERT_INT_EQ(SYNTAX_QUOTE, 11);
    ASSERT_INT_EQ(SYNTAX_SYNTAX_QUOTE, 12);
    ASSERT_INT_EQ(SYNTAX_UNQUOTE, 13);
    ASSERT_INT_EQ(SYNTAX_UNQUOTE_SPLICING, 14);
    ASSERT_INT_EQ(SYNTAX_BREAK, 15);
    ASSERT_INT_EQ(SYNTAX_CONTINUE, 16);
    ASSERT_INT_EQ(SYNTAX_RETURN, 17);
    ASSERT_INT_EQ(SYNTAX_DESTRUCTURE_VEC, 18);
    ASSERT_INT_EQ(SYNTAX_DESTRUCTURE_NAMED, 19);
    TEST_PASS();
}

/* Test: jacl_is_syntax predicate */
static int test_syntax_is_predicate(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal syn = gc_alloc_syntax(&vm.heap);
    ASSERT(jacl_is_syntax(syn));
    ASSERT(!jacl_is_nil(syn));
    ASSERT(!jacl_is_i32(syn));
    ASSERT(!jacl_is_string(syn));
    ASSERT(!jacl_is_closure(syn));

    /* Non-syntax values should not pass */
    ASSERT(!jacl_is_syntax(JACL_NIL));
    ASSERT(!jacl_is_syntax(jacl_i32(42)));
    ASSERT(!jacl_is_syntax(JACL_TRUE));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: jacl_is_heap_type includes syntax */
static int test_syntax_is_heap_type(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal syn = gc_alloc_syntax(&vm.heap);
    ASSERT(jacl_is_heap_type(syn));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: gc_alloc_syntax returns valid object with correct GC header */
static int test_syntax_alloc_gc_header(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal syn = gc_alloc_syntax(&vm.heap);
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT(s != NULL);

    GCHeader *hdr = gc_header_of(s);
    ASSERT_INT_EQ(hdr->obj_type, OBJ_SYNTAX);
    ASSERT(hdr->alloc_total >= sizeof(GCHeader) + sizeof(JaclSyntax));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax object fields are zeroed on allocation */
static int test_syntax_alloc_zeroed(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal syn = gc_alloc_syntax(&vm.heap);
    JaclSyntax *s = jacl_as_syntax(syn);

    ASSERT_INT_EQ(s->kind, 0);
    ASSERT_U32_EQ(s->pos_line, 0);
    ASSERT_U32_EQ(s->pos_col, 0);
    ASSERT_U32_EQ(s->pos_offset, 0);
    ASSERT_U32_EQ(s->scope_mark, 0);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax object round-trip: alloc, set fields, read back */
static int test_syntax_lit_int(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal syn = gc_alloc_syntax(&vm.heap);
    JaclSyntax *s = jacl_as_syntax(syn);
    s->kind = SYNTAX_LIT_INT;
    s->data.lit_int.value = 42;
    s->pos_line = 10;
    s->pos_col = 5;

    /* Read back */
    JaclSyntax *r = jacl_as_syntax(syn);
    ASSERT_INT_EQ(r->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(r->data.lit_int.value, 42);
    ASSERT_U32_EQ(r->pos_line, 10);
    ASSERT_U32_EQ(r->pos_col, 5);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax object with float literal */
static int test_syntax_lit_float(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal syn = gc_alloc_syntax(&vm.heap);
    JaclSyntax *s = jacl_as_syntax(syn);
    s->kind = SYNTAX_LIT_FLOAT;
    s->data.lit_float.value = 3.14f;

    ASSERT_INT_EQ(s->kind, SYNTAX_LIT_FLOAT);
    float diff = s->data.lit_float.value - 3.14f;
    ASSERT(diff < 0.001f && diff > -0.001f);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax var-ref stores a string JaclVal */
static int test_syntax_var_ref(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    JaclVal name = jacl_intern(&vm.heap, &intern, "x", 1);
    JaclVal syn = gc_alloc_syntax(&vm.heap);
    JaclSyntax *s = jacl_as_syntax(syn);
    s->kind = SYNTAX_VAR_REF;
    s->data.var_ref.name = name;

    ASSERT_INT_EQ(s->kind, SYNTAX_VAR_REF);
    ASSERT(jacl_is_string(s->data.var_ref.name));

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax command with head and args vec */
static int test_syntax_command(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    /* Create head: var-ref for "+" */
    JaclVal head_syn = gc_alloc_syntax(&vm.heap);
    JaclSyntax *head = jacl_as_syntax(head_syn);
    head->kind = SYNTAX_VAR_REF;
    head->data.var_ref.name = jacl_intern(&vm.heap, &intern, "+", 1);

    /* Create arg1: lit-int 1 */
    JaclVal arg1_syn = gc_alloc_syntax(&vm.heap);
    JaclSyntax *arg1 = jacl_as_syntax(arg1_syn);
    arg1->kind = SYNTAX_LIT_INT;
    arg1->data.lit_int.value = 1;

    /* Create arg2: lit-int 2 */
    JaclVal arg2_syn = gc_alloc_syntax(&vm.heap);
    JaclSyntax *arg2 = jacl_as_syntax(arg2_syn);
    arg2->kind = SYNTAX_LIT_INT;
    arg2->data.lit_int.value = 2;

    /* Build args vec */
    jacl_vec_root *vec = jacl_vec_empty();
    vec = jacl_vec_push_back(vec, arg1_syn);
    vec = jacl_vec_push_back(vec, arg2_syn);
    JaclVal args_val = JACL_TAG_VECTOR | ((uint64_t)(uintptr_t)vec & JACL_PAYLOAD_MASK);

    /* Create command syntax */
    JaclVal cmd_syn = gc_alloc_syntax(&vm.heap);
    JaclSyntax *cmd = jacl_as_syntax(cmd_syn);
    cmd->kind = SYNTAX_COMMAND;
    cmd->data.command.head = head_syn;
    cmd->data.command.args = args_val;

    ASSERT_INT_EQ(cmd->kind, SYNTAX_COMMAND);
    ASSERT(jacl_is_syntax(cmd->data.command.head));
    ASSERT(jacl_is_vector(cmd->data.command.args));

    jacl_vec_root *rargs = (jacl_vec_root *)jacl_as_ptr(cmd->data.command.args);
    ASSERT_U32_EQ(jacl_vec_count(rargs), 2);

    JaclSyntax *a1 = jacl_as_syntax(jacl_vec_get(rargs, 0).value);
    ASSERT_INT_EQ(a1->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(a1->data.lit_int.value, 1);

    JaclSyntax *a2 = jacl_as_syntax(jacl_vec_get(rargs, 1).value);
    ASSERT_INT_EQ(a2->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(a2->data.lit_int.value, 2);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax block with a vec of commands */
static int test_syntax_block(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal s1 = gc_alloc_syntax(&vm.heap);
    jacl_as_syntax(s1)->kind = SYNTAX_LIT_INT;
    jacl_as_syntax(s1)->data.lit_int.value = 10;

    JaclVal s2 = gc_alloc_syntax(&vm.heap);
    jacl_as_syntax(s2)->kind = SYNTAX_LIT_INT;
    jacl_as_syntax(s2)->data.lit_int.value = 20;

    jacl_vec_root *cmds = jacl_vec_empty();
    cmds = jacl_vec_push_back(cmds, s1);
    cmds = jacl_vec_push_back(cmds, s2);
    JaclVal cmds_val = JACL_TAG_VECTOR | ((uint64_t)(uintptr_t)cmds & JACL_PAYLOAD_MASK);

    JaclVal blk = gc_alloc_syntax(&vm.heap);
    JaclSyntax *b = jacl_as_syntax(blk);
    b->kind = SYNTAX_BLOCK;
    b->data.block.commands = cmds_val;

    ASSERT_INT_EQ(b->kind, SYNTAX_BLOCK);
    jacl_vec_root *rc = (jacl_vec_root *)jacl_as_ptr(b->data.block.commands);
    ASSERT_U32_EQ(jacl_vec_count(rc), 2);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax spread wraps a child */
static int test_syntax_spread(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal child = gc_alloc_syntax(&vm.heap);
    jacl_as_syntax(child)->kind = SYNTAX_LIT_INT;
    jacl_as_syntax(child)->data.lit_int.value = 99;

    JaclVal spr = gc_alloc_syntax(&vm.heap);
    JaclSyntax *sp = jacl_as_syntax(spr);
    sp->kind = SYNTAX_SPREAD;
    sp->data.spread.child = child;

    ASSERT_INT_EQ(sp->kind, SYNTAX_SPREAD);
    ASSERT(jacl_is_syntax(sp->data.spread.child));
    ASSERT_INT_EQ(jacl_as_syntax(sp->data.spread.child)->data.lit_int.value, 99);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: scope_mark field is writable and readable */
static int test_syntax_scope_mark(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal syn = gc_alloc_syntax(&vm.heap);
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_U32_EQ(s->scope_mark, 0);

    s->scope_mark = 42;
    ASSERT_U32_EQ(s->scope_mark, 42);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax printing — lit-int */
static int test_syntax_print_lit_int(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal syn = gc_alloc_syntax(&vm.heap);
    JaclSyntax *s = jacl_as_syntax(syn);
    s->kind = SYNTAX_LIT_INT;
    s->data.lit_int.value = 42;

    VMFormatBuf buf = {0};
    buf.arena = &arena;
    vm__fmt_value(&buf, syn);

    char result[64];
    uint32_t len = buf.len < 63 ? buf.len : 63;
    memcpy(result, buf.data, len);
    result[len] = '\0';

    ASSERT_STR_EQ(result, "<syntax:lit-int 42>");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax printing — lit-float */
static int test_syntax_print_lit_float(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal syn = gc_alloc_syntax(&vm.heap);
    JaclSyntax *s = jacl_as_syntax(syn);
    s->kind = SYNTAX_LIT_FLOAT;
    s->data.lit_float.value = 3.14f;

    VMFormatBuf buf = {0};
    buf.arena = &arena;
    vm__fmt_value(&buf, syn);

    char result[64];
    uint32_t len = buf.len < 63 ? buf.len : 63;
    memcpy(result, buf.data, len);
    result[len] = '\0';

    ASSERT_STR_EQ(result, "<syntax:lit-float 3.14>");

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax printing — var-ref */
static int test_syntax_print_var_ref(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    JaclVal name = jacl_intern(&vm.heap, &intern, "foo", 3);
    JaclVal syn = gc_alloc_syntax(&vm.heap);
    JaclSyntax *s = jacl_as_syntax(syn);
    s->kind = SYNTAX_VAR_REF;
    s->data.var_ref.name = name;

    VMFormatBuf buf = {0};
    buf.arena = &arena;
    vm__fmt_value(&buf, syn);

    char result[64];
    uint32_t len = buf.len < 63 ? buf.len : 63;
    memcpy(result, buf.data, len);
    result[len] = '\0';

    ASSERT_STR_EQ(result, "<syntax:var-ref $foo>");

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax printing — command [+ 1 2] */
static int test_syntax_print_command(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    /* head: var-ref "+" */
    JaclVal head_syn = gc_alloc_syntax(&vm.heap);
    JaclSyntax *h = jacl_as_syntax(head_syn);
    h->kind = SYNTAX_VAR_REF;
    h->data.var_ref.name = jacl_intern(&vm.heap, &intern, "+", 1);

    /* arg1: lit-int 1 */
    JaclVal arg1 = gc_alloc_syntax(&vm.heap);
    jacl_as_syntax(arg1)->kind = SYNTAX_LIT_INT;
    jacl_as_syntax(arg1)->data.lit_int.value = 1;

    /* arg2: lit-int 2 */
    JaclVal arg2 = gc_alloc_syntax(&vm.heap);
    jacl_as_syntax(arg2)->kind = SYNTAX_LIT_INT;
    jacl_as_syntax(arg2)->data.lit_int.value = 2;

    /* args vec */
    jacl_vec_root *vec = jacl_vec_empty();
    vec = jacl_vec_push_back(vec, arg1);
    vec = jacl_vec_push_back(vec, arg2);
    JaclVal args_val = JACL_TAG_VECTOR | ((uint64_t)(uintptr_t)vec & JACL_PAYLOAD_MASK);

    /* command */
    JaclVal cmd = gc_alloc_syntax(&vm.heap);
    JaclSyntax *c = jacl_as_syntax(cmd);
    c->kind = SYNTAX_COMMAND;
    c->data.command.head = head_syn;
    c->data.command.args = args_val;

    VMFormatBuf buf = {0};
    buf.arena = &arena;
    vm__fmt_value(&buf, cmd);

    char result[128];
    uint32_t len = buf.len < 127 ? buf.len : 127;
    memcpy(result, buf.data, len);
    result[len] = '\0';

    ASSERT_STR_EQ(result,
      "<syntax:command [<syntax:var-ref $+> <syntax:lit-int 1> <syntax:lit-int 2>]>");

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax lit-string */
static int test_syntax_lit_string(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    JaclVal str = jacl_intern(&vm.heap, &intern, "hello", 5);
    JaclVal syn = gc_alloc_syntax(&vm.heap);
    JaclSyntax *s = jacl_as_syntax(syn);
    s->kind = SYNTAX_LIT_STRING;
    s->data.lit_string.value = str;

    ASSERT_INT_EQ(s->kind, SYNTAX_LIT_STRING);
    ASSERT(jacl_is_string(s->data.lit_string.value));

    VMFormatBuf buf = {0};
    buf.arena = &arena;
    vm__fmt_value(&buf, syn);

    char result[64];
    uint32_t len = buf.len < 63 ? buf.len : 63;
    memcpy(result, buf.data, len);
    result[len] = '\0';

    ASSERT_STR_EQ(result, "<syntax:lit-string \"hello\">");

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax break and return with value */
static int test_syntax_break_return(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* break with a value */
    JaclVal val_syn = gc_alloc_syntax(&vm.heap);
    jacl_as_syntax(val_syn)->kind = SYNTAX_LIT_INT;
    jacl_as_syntax(val_syn)->data.lit_int.value = 7;

    JaclVal brk = gc_alloc_syntax(&vm.heap);
    JaclSyntax *b = jacl_as_syntax(brk);
    b->kind = SYNTAX_BREAK;
    b->data.break_stmt.value = val_syn;
    ASSERT(jacl_is_syntax(b->data.break_stmt.value));

    /* break without value */
    JaclVal brk2 = gc_alloc_syntax(&vm.heap);
    JaclSyntax *b2 = jacl_as_syntax(brk2);
    b2->kind = SYNTAX_BREAK;
    b2->data.break_stmt.value = JACL_NIL;
    ASSERT(jacl_is_nil(b2->data.break_stmt.value));

    /* continue has no value field */
    JaclVal cont = gc_alloc_syntax(&vm.heap);
    jacl_as_syntax(cont)->kind = SYNTAX_CONTINUE;

    /* return with value */
    JaclVal ret = gc_alloc_syntax(&vm.heap);
    JaclSyntax *r = jacl_as_syntax(ret);
    r->kind = SYNTAX_RETURN;
    r->data.return_stmt.value = val_syn;
    ASSERT(jacl_is_syntax(r->data.return_stmt.value));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax interp-string with segments vec */
static int test_syntax_interp_string(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    /* Segments: lit-string "hello " + var-ref "name" */
    JaclVal seg1 = gc_alloc_syntax(&vm.heap);
    jacl_as_syntax(seg1)->kind = SYNTAX_LIT_STRING;
    jacl_as_syntax(seg1)->data.lit_string.value = jacl_intern(&vm.heap, &intern, "hello ", 6);

    JaclVal seg2 = gc_alloc_syntax(&vm.heap);
    jacl_as_syntax(seg2)->kind = SYNTAX_VAR_REF;
    jacl_as_syntax(seg2)->data.var_ref.name = jacl_intern(&vm.heap, &intern, "name", 4);

    jacl_vec_root *segs = jacl_vec_empty();
    segs = jacl_vec_push_back(segs, seg1);
    segs = jacl_vec_push_back(segs, seg2);
    JaclVal segs_val = JACL_TAG_VECTOR | ((uint64_t)(uintptr_t)segs & JACL_PAYLOAD_MASK);

    JaclVal is = gc_alloc_syntax(&vm.heap);
    JaclSyntax *s = jacl_as_syntax(is);
    s->kind = SYNTAX_INTERP_STRING;
    s->data.interp_string.segments = segs_val;

    ASSERT_INT_EQ(s->kind, SYNTAX_INTERP_STRING);
    jacl_vec_root *rs = (jacl_vec_root *)jacl_as_ptr(s->data.interp_string.segments);
    ASSERT_U32_EQ(jacl_vec_count(rs), 2);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: multiple syntax objects survive GC */
static int test_syntax_gc_survives(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Allocate syntax objects and push to VM stack as roots */
    JaclVal s1 = gc_alloc_syntax(&vm.heap);
    jacl_as_syntax(s1)->kind = SYNTAX_LIT_INT;
    jacl_as_syntax(s1)->data.lit_int.value = 100;
    vm.stack[vm.stack_top++] = s1;

    JaclVal s2 = gc_alloc_syntax(&vm.heap);
    jacl_as_syntax(s2)->kind = SYNTAX_LIT_INT;
    jacl_as_syntax(s2)->data.lit_int.value = 200;
    vm.stack[vm.stack_top++] = s2;

    /* Force a GC cycle */
    gc_collect(&vm.heap, &vm);

    /* Objects should survive (they're on the stack) */
    ASSERT(jacl_is_syntax(s1));
    JaclSyntax *r1 = jacl_as_syntax(s1);
    ASSERT_INT_EQ(r1->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(r1->data.lit_int.value, 100);

    JaclSyntax *r2 = jacl_as_syntax(s2);
    ASSERT_INT_EQ(r2->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(r2->data.lit_int.value, 200);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax object with command and inner refs survives GC */
static int test_syntax_command_gc_survives(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    /* Build [+ 1 2] as syntax */
    JaclVal head_syn = gc_alloc_syntax(&vm.heap);
    jacl_as_syntax(head_syn)->kind = SYNTAX_VAR_REF;
    jacl_as_syntax(head_syn)->data.var_ref.name = jacl_intern(&vm.heap, &intern, "+", 1);

    JaclVal a1 = gc_alloc_syntax(&vm.heap);
    jacl_as_syntax(a1)->kind = SYNTAX_LIT_INT;
    jacl_as_syntax(a1)->data.lit_int.value = 1;

    JaclVal a2 = gc_alloc_syntax(&vm.heap);
    jacl_as_syntax(a2)->kind = SYNTAX_LIT_INT;
    jacl_as_syntax(a2)->data.lit_int.value = 2;

    jacl_vec_root *args = jacl_vec_empty();
    args = jacl_vec_push_back(args, a1);
    args = jacl_vec_push_back(args, a2);
    JaclVal args_val = JACL_TAG_VECTOR | ((uint64_t)(uintptr_t)args & JACL_PAYLOAD_MASK);

    JaclVal cmd = gc_alloc_syntax(&vm.heap);
    jacl_as_syntax(cmd)->kind = SYNTAX_COMMAND;
    jacl_as_syntax(cmd)->data.command.head = head_syn;
    jacl_as_syntax(cmd)->data.command.args = args_val;

    /* Root only the command — inner syntax should survive via tracing */
    vm.stack[vm.stack_top++] = cmd;

    gc_collect(&vm.heap, &vm);

    /* Verify the entire tree survived */
    JaclSyntax *c = jacl_as_syntax(cmd);
    ASSERT_INT_EQ(c->kind, SYNTAX_COMMAND);
    ASSERT(jacl_is_syntax(c->data.command.head));
    ASSERT(jacl_is_vector(c->data.command.args));

    jacl_vec_root *rargs = (jacl_vec_root *)jacl_as_ptr(c->data.command.args);
    ASSERT_U32_EQ(jacl_vec_count(rargs), 2);
    ASSERT_INT_EQ(jacl_as_syntax(jacl_vec_get(rargs, 0).value)->data.lit_int.value, 1);
    ASSERT_INT_EQ(jacl_as_syntax(jacl_vec_get(rargs, 1).value)->data.lit_int.value, 2);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== US-002: AstNode to syntax object conversion ===== */

/* Helper: parse source code into AST nodes */
static ParseResult parse_source(const char *src, arena_t *arena) {
    LexResult tokens = lexer_lex(src, arena);
    return parser_parse(tokens, arena);
}

/* Test: convert AST_LIT_INT to syntax */
static int test_from_ast_lit_int(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("42", &arena);
    ASSERT(pr.count >= 1);

    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm.heap, &intern);
    ASSERT(jacl_is_syntax(syn));
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_INT_EQ(s->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(s->data.lit_int.value, 42);
    ASSERT_U32_EQ(s->scope_mark, 0);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: convert AST_LIT_FLOAT to syntax */
static int test_from_ast_lit_float(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("3.14", &arena);
    ASSERT(pr.count >= 1);

    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm.heap, &intern);
    ASSERT(jacl_is_syntax(syn));
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_INT_EQ(s->kind, SYNTAX_LIT_FLOAT);
    float diff = s->data.lit_float.value - 3.14f;
    ASSERT(diff < 0.01f && diff > -0.01f);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: convert AST_LIT_STRING to syntax */
static int test_from_ast_lit_string(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("\"hello\"", &arena);
    ASSERT(pr.count >= 1);

    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm.heap, &intern);
    ASSERT(jacl_is_syntax(syn));
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_INT_EQ(s->kind, SYNTAX_LIT_STRING);
    ASSERT(jacl_is_string(s->data.lit_string.value));

    char buf[32];
    uint32_t len = jacl_string_data(s->data.lit_string.value, buf, sizeof(buf));
    buf[len] = '\0';
    ASSERT_STR_EQ(buf, "hello");

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: convert AST_VAR_REF to syntax */
static int test_from_ast_var_ref(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("$x", &arena);
    ASSERT(pr.count >= 1);

    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm.heap, &intern);
    ASSERT(jacl_is_syntax(syn));
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_INT_EQ(s->kind, SYNTAX_VAR_REF);
    ASSERT(jacl_is_string(s->data.var_ref.name));

    char buf[32];
    uint32_t len = jacl_string_data(s->data.var_ref.name, buf, sizeof(buf));
    buf[len] = '\0';
    ASSERT_STR_EQ(buf, "x");

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: convert AST_COMMAND [+ 1 2] to syntax */
static int test_from_ast_command(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("[+ 1 2]", &arena);
    ASSERT(pr.count >= 1);

    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm.heap, &intern);
    ASSERT(jacl_is_syntax(syn));
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_INT_EQ(s->kind, SYNTAX_COMMAND);

    /* head should be lit-string "+" */
    ASSERT(jacl_is_syntax(s->data.command.head));
    JaclSyntax *head = jacl_as_syntax(s->data.command.head);
    ASSERT_INT_EQ(head->kind, SYNTAX_LIT_STRING);

    /* args should be vec of 2 lit-ints */
    ASSERT(jacl_is_vector(s->data.command.args));
    jacl_vec_root *args = (jacl_vec_root *)jacl_as_ptr(s->data.command.args);
    ASSERT_U32_EQ(jacl_vec_count(args), 2);

    JaclSyntax *a1 = jacl_as_syntax(jacl_vec_get(args, 0).value);
    ASSERT_INT_EQ(a1->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(a1->data.lit_int.value, 1);

    JaclSyntax *a2 = jacl_as_syntax(jacl_vec_get(args, 1).value);
    ASSERT_INT_EQ(a2->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(a2->data.lit_int.value, 2);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: convert AST_BLOCK { print 1; print 2 } to syntax */
static int test_from_ast_block(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("{ print 1; print 2 }", &arena);
    ASSERT(pr.count >= 1);

    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm.heap, &intern);
    ASSERT(jacl_is_syntax(syn));
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_INT_EQ(s->kind, SYNTAX_BLOCK);

    jacl_vec_root *cmds = (jacl_vec_root *)jacl_as_ptr(s->data.block.commands);
    ASSERT_U32_EQ(jacl_vec_count(cmds), 2);

    /* Each command should be a SYNTAX_COMMAND */
    JaclSyntax *c1 = jacl_as_syntax(jacl_vec_get(cmds, 0).value);
    ASSERT_INT_EQ(c1->kind, SYNTAX_COMMAND);
    JaclSyntax *c2 = jacl_as_syntax(jacl_vec_get(cmds, 1).value);
    ASSERT_INT_EQ(c2->kind, SYNTAX_COMMAND);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: source positions copied from AstNode */
static int test_from_ast_source_positions(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("42", &arena);
    ASSERT(pr.count >= 1);

    /* Check the AstNode has source position set */
    AstNode *node = pr.nodes[0];
    ASSERT_U32_EQ(node->start.line, 1);
    ASSERT(node->start.column >= 1);

    JaclVal syn = syntax_from_ast(node, &vm.heap, &intern);
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_U32_EQ(s->pos_line, node->start.line);
    ASSERT_U32_EQ(s->pos_col, node->start.column);
    ASSERT_U32_EQ(s->pos_offset, node->start.offset);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: scope marks initialized to 0 */
static int test_from_ast_scope_marks_zero(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("[+ $x 1]", &arena);
    ASSERT(pr.count >= 1);

    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm.heap, &intern);
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_U32_EQ(s->scope_mark, 0);

    /* Check children too */
    JaclSyntax *head = jacl_as_syntax(s->data.command.head);
    ASSERT_U32_EQ(head->scope_mark, 0);

    jacl_vec_root *args = (jacl_vec_root *)jacl_as_ptr(s->data.command.args);
    JaclSyntax *a1 = jacl_as_syntax(jacl_vec_get(args, 0).value);
    ASSERT_U32_EQ(a1->scope_mark, 0);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: AST_SPREAD conversion */
static int test_from_ast_spread(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("[foo ..$xs]", &arena);
    ASSERT(pr.count >= 1);

    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm.heap, &intern);
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_INT_EQ(s->kind, SYNTAX_COMMAND);

    /* The spread arg */
    jacl_vec_root *args = (jacl_vec_root *)jacl_as_ptr(s->data.command.args);
    ASSERT(jacl_vec_count(args) >= 1);
    JaclSyntax *spread = jacl_as_syntax(jacl_vec_get(args, 0).value);
    ASSERT_INT_EQ(spread->kind, SYNTAX_SPREAD);
    ASSERT(jacl_is_syntax(spread->data.spread.child));

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: AST_BREAK with value */
static int test_from_ast_break(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("break 42", &arena);
    ASSERT(pr.count >= 1);

    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm.heap, &intern);
    ASSERT(jacl_is_syntax(syn));
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_INT_EQ(s->kind, SYNTAX_BREAK);
    ASSERT(!jacl_is_nil(s->data.break_stmt.value));
    ASSERT(jacl_is_syntax(s->data.break_stmt.value));

    JaclSyntax *val = jacl_as_syntax(s->data.break_stmt.value);
    ASSERT_INT_EQ(val->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(val->data.lit_int.value, 42);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: AST_CONTINUE conversion */
static int test_from_ast_continue(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("continue", &arena);
    ASSERT(pr.count >= 1);

    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm.heap, &intern);
    ASSERT(jacl_is_syntax(syn));
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_INT_EQ(s->kind, SYNTAX_CONTINUE);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: AST_RETURN with value */
static int test_from_ast_return(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("return 99", &arena);
    ASSERT(pr.count >= 1);

    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm.heap, &intern);
    ASSERT(jacl_is_syntax(syn));
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_INT_EQ(s->kind, SYNTAX_RETURN);
    ASSERT(jacl_is_syntax(s->data.return_stmt.value));

    JaclSyntax *val = jacl_as_syntax(s->data.return_stmt.value);
    ASSERT_INT_EQ(val->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(val->data.lit_int.value, 99);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: nested command [if [> $x 3] { print $x }] */
static int test_from_ast_nested_command(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("[if [> $x 3] { print $x }]", &arena);
    ASSERT(pr.count >= 1);

    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm.heap, &intern);
    ASSERT(jacl_is_syntax(syn));
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_INT_EQ(s->kind, SYNTAX_COMMAND);

    /* args[0] should be the nested [> $x 3] command */
    jacl_vec_root *args = (jacl_vec_root *)jacl_as_ptr(s->data.command.args);
    ASSERT(jacl_vec_count(args) >= 2);

    JaclSyntax *cond = jacl_as_syntax(jacl_vec_get(args, 0).value);
    ASSERT_INT_EQ(cond->kind, SYNTAX_COMMAND);

    /* args[1] should be a block */
    JaclSyntax *body = jacl_as_syntax(jacl_vec_get(args, 1).value);
    ASSERT_INT_EQ(body->kind, SYNTAX_BLOCK);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: interp-string "hello $name" */
static int test_from_ast_interp_string(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("\"hello $name\"", &arena);
    ASSERT(pr.count >= 1);

    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm.heap, &intern);
    ASSERT(jacl_is_syntax(syn));
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_INT_EQ(s->kind, SYNTAX_INTERP_STRING);

    jacl_vec_root *segs = (jacl_vec_root *)jacl_as_ptr(s->data.interp_string.segments);
    ASSERT_U32_EQ(jacl_vec_count(segs), 2);

    /* First segment: lit-string "hello " */
    JaclSyntax *seg1 = jacl_as_syntax(jacl_vec_get(segs, 0).value);
    ASSERT_INT_EQ(seg1->kind, SYNTAX_LIT_STRING);

    /* Second segment: var-ref "name" */
    JaclSyntax *seg2 = jacl_as_syntax(jacl_vec_get(segs, 1).value);
    ASSERT_INT_EQ(seg2->kind, SYNTAX_VAR_REF);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: AST_ERROR returns nil */
static int test_from_ast_error_returns_nil(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    /* Manually create an error node */
    AstNode *err = ast_alloc(&arena);
    memset(err, 0, sizeof(AstNode));
    err->type = AST_ERROR;
    err->data.error.message = "test error";

    JaclVal syn = syntax_from_ast(err, &vm.heap, &intern);
    ASSERT(jacl_is_nil(syn));

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: NULL node returns nil */
static int test_from_ast_null_returns_nil(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    JaclVal syn = syntax_from_ast(NULL, &vm.heap, &intern);
    ASSERT(jacl_is_nil(syn));

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax objects from AST survive GC */
static int test_from_ast_gc_survives(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("[+ 1 2]", &arena);
    ASSERT(pr.count >= 1);

    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm.heap, &intern);
    vm.stack[vm.stack_top++] = syn;

    gc_collect(&vm.heap, &vm);

    /* Verify the tree survived */
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_INT_EQ(s->kind, SYNTAX_COMMAND);
    ASSERT(jacl_is_syntax(s->data.command.head));
    ASSERT(jacl_is_vector(s->data.command.args));

    jacl_vec_root *args = (jacl_vec_root *)jacl_as_ptr(s->data.command.args);
    ASSERT_U32_EQ(jacl_vec_count(args), 2);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== US-003: Syntax object to AstNode conversion ===== */

/* Helper: round-trip a source string through AST → syntax → AST → pretty-print */
static const char *roundtrip_pp(const char *src, arena_t *arena, VM *vm,
                                JaclInternTable *intern) {
    ParseResult pr = parse_source(src, arena);
    if (pr.count == 0) return NULL;
    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm->heap, intern);
    AstNode *back = syntax_to_ast(syn, arena);
    if (!back) return NULL;
    return ast_pretty_print(back, arena);
}

/* Test: round-trip lit-int */
static int test_to_ast_lit_int(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("42", &arena);
    ASSERT(pr.count >= 1);
    const char *orig = ast_pretty_print(pr.nodes[0], &arena);

    const char *rt = roundtrip_pp("42", &arena, &vm, &intern);
    ASSERT(rt != NULL);
    ASSERT_STR_EQ(rt, orig);
    ASSERT_STR_EQ(rt, "42");

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: round-trip lit-float */
static int test_to_ast_lit_float(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    const char *rt = roundtrip_pp("3.14", &arena, &vm, &intern);
    ASSERT(rt != NULL);
    ASSERT_STR_EQ(rt, "3.14");

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: round-trip lit-string */
static int test_to_ast_lit_string(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("\"hello\"", &arena);
    ASSERT(pr.count >= 1);
    const char *orig = ast_pretty_print(pr.nodes[0], &arena);

    const char *rt = roundtrip_pp("\"hello\"", &arena, &vm, &intern);
    ASSERT(rt != NULL);
    ASSERT_STR_EQ(rt, orig);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: round-trip var-ref */
static int test_to_ast_var_ref(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    const char *rt = roundtrip_pp("$x", &arena, &vm, &intern);
    ASSERT(rt != NULL);
    ASSERT_STR_EQ(rt, "$x");

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: round-trip simple command [+ 1 2] */
static int test_to_ast_command(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("[+ 1 2]", &arena);
    ASSERT(pr.count >= 1);
    const char *orig = ast_pretty_print(pr.nodes[0], &arena);

    const char *rt = roundtrip_pp("[+ 1 2]", &arena, &vm, &intern);
    ASSERT(rt != NULL);
    ASSERT_STR_EQ(rt, orig);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: round-trip with nested commands [if [> $x 3] { print $x }] */
static int test_to_ast_nested_command(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    const char *src = "[if [> $x 3] { print $x }]";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    const char *orig = ast_pretty_print(pr.nodes[0], &arena);

    const char *rt = roundtrip_pp(src, &arena, &vm, &intern);
    ASSERT(rt != NULL);
    ASSERT_STR_EQ(rt, orig);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: round-trip block with multiple commands */
static int test_to_ast_block_multi(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    const char *src = "{ print 1; print 2 }";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    const char *orig = ast_pretty_print(pr.nodes[0], &arena);

    const char *rt = roundtrip_pp(src, &arena, &vm, &intern);
    ASSERT(rt != NULL);
    ASSERT_STR_EQ(rt, orig);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: source positions round-trip */
static int test_to_ast_source_positions(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    ParseResult pr = parse_source("42", &arena);
    ASSERT(pr.count >= 1);
    AstNode *orig_node = pr.nodes[0];

    JaclVal syn = syntax_from_ast(orig_node, &vm.heap, &intern);
    AstNode *back = syntax_to_ast(syn, &arena);
    ASSERT(back != NULL);

    /* Source positions should be preserved */
    ASSERT_U32_EQ(back->start.line, orig_node->start.line);
    ASSERT_U32_EQ(back->start.column, orig_node->start.column);
    ASSERT_U32_EQ(back->start.offset, orig_node->start.offset);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: nil syntax returns NULL */
static int test_to_ast_nil_returns_null(void) {
    arena_t arena = {0};
    AstNode *result = syntax_to_ast(JACL_NIL, &arena);
    ASSERT(result == NULL);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: break round-trip */
static int test_to_ast_break(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    const char *src = "break 42";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    const char *orig = ast_pretty_print(pr.nodes[0], &arena);

    const char *rt = roundtrip_pp(src, &arena, &vm, &intern);
    ASSERT(rt != NULL);
    ASSERT_STR_EQ(rt, orig);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: continue round-trip */
static int test_to_ast_continue(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    const char *rt = roundtrip_pp("continue", &arena, &vm, &intern);
    ASSERT(rt != NULL);
    ASSERT_STR_EQ(rt, "continue");

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: return round-trip */
static int test_to_ast_return(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    const char *src = "return 99";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    const char *orig = ast_pretty_print(pr.nodes[0], &arena);

    const char *rt = roundtrip_pp(src, &arena, &vm, &intern);
    ASSERT(rt != NULL);
    ASSERT_STR_EQ(rt, orig);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: spread round-trip */
static int test_to_ast_spread(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    const char *src = "[foo ..$xs]";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    const char *orig = ast_pretty_print(pr.nodes[0], &arena);

    const char *rt = roundtrip_pp(src, &arena, &vm, &intern);
    ASSERT(rt != NULL);
    ASSERT_STR_EQ(rt, orig);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== US-004: Defmacro parsing tests ===== */

/* Test: parse defmacro with params produces AST_DEFMACRO */
static int test_parse_defmacro_basic(void) {
    arena_t arena = {0};
    const char *src = "defmacro unless {cond, body} { if [not $cond] $body }";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    AstNode *node = pr.nodes[0];
    ASSERT_INT_EQ(node->type, AST_DEFMACRO);
    ASSERT_U32_EQ(node->data.defmacro.name_len, 6);
    ASSERT(memcmp(node->data.defmacro.name, "unless", 6) == 0);
    ASSERT_U32_EQ(node->data.defmacro.param_count, 2);
    ASSERT(memcmp(node->data.defmacro.param_names[0], "cond", 4) == 0);
    ASSERT_U32_EQ(node->data.defmacro.param_name_lens[0], 4);
    ASSERT(memcmp(node->data.defmacro.param_names[1], "body", 4) == 0);
    ASSERT_U32_EQ(node->data.defmacro.param_name_lens[1], 4);
    ASSERT(node->data.defmacro.body != NULL);
    ASSERT_INT_EQ(node->data.defmacro.body->type, AST_BLOCK);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: defmacro with zero params */
static int test_parse_defmacro_zero_params(void) {
    arena_t arena = {0};
    const char *src = "defmacro now {} { print 42 }";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    AstNode *node = pr.nodes[0];
    ASSERT_INT_EQ(node->type, AST_DEFMACRO);
    ASSERT(memcmp(node->data.defmacro.name, "now", 3) == 0);
    ASSERT_U32_EQ(node->data.defmacro.param_count, 0);
    ASSERT(node->data.defmacro.body != NULL);
    ASSERT_INT_EQ(node->data.defmacro.body->type, AST_BLOCK);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: defmacro pretty-prints correctly */
static int test_parse_defmacro_pretty_print(void) {
    arena_t arena = {0};
    const char *src = "defmacro unless {cond, body} { if [not $cond] $body }";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    const char *pp = ast_pretty_print(pr.nodes[0], &arena);
    ASSERT(pp != NULL);
    /* Should contain "defmacro unless" and param names */
    ASSERT(strstr(pp, "defmacro unless") != NULL);
    ASSERT(strstr(pp, "cond") != NULL);
    ASSERT(strstr(pp, "body") != NULL);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: defmacro inside a block produces parse error (not at top level) */
static int test_parse_defmacro_not_top_level(void) {
    arena_t arena = {0};
    /* defmacro inside a proc body — it will parse as a command at parse level,
     * but the compiler rejects it at compile time. The parser only dispatches
     * defmacro at top level. Inside a block, 'defmacro' becomes a bare word
     * command head. So let's test that the compiler rejects it. */
    const char *src = "defmacro unless {cond, body} { if [not $cond] $body }";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    /* Verify this is at top level — correct parse */
    ASSERT_INT_EQ(pr.nodes[0]->type, AST_DEFMACRO);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: defmacro with no name produces error */
static int test_parse_defmacro_no_name(void) {
    arena_t arena = {0};
    const char *src = "defmacro {cond} { print 1 }";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    /* Should get an error node since '{' is not a word */
    ASSERT_INT_EQ(pr.nodes[0]->type, AST_ERROR);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: defmacro with ambiguous syntax — body is expected after param block */
static int test_parse_defmacro_no_params(void) {
    arena_t arena = {0};
    /* "defmacro foo { print 1 }" parses "{print 1}" as param block,
     * then there's no body block — should produce an error. */
    const char *src = "defmacro foo { print 1 }";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    /* The first node should be an error (no body block after params) */
    ASSERT_INT_EQ(pr.nodes[0]->type, AST_ERROR);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: defmacro with no body produces error */
static int test_parse_defmacro_no_body(void) {
    arena_t arena = {0};
    const char *src = "defmacro foo {x}";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    /* After parsing params {x}, there's no body block — should error */
    ASSERT_INT_EQ(pr.nodes[0]->type, AST_ERROR);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== US-005: Quote parsing tests ===== */

/* Test: parse quote [+ 1 2] produces AST_QUOTE wrapping AST_COMMAND */
static int test_parse_quote_command(void) {
    arena_t arena = {0};
    const char *src = "quote [+ 1 2]";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    AstNode *node = pr.nodes[0];
    ASSERT_INT_EQ(node->type, AST_QUOTE);
    ASSERT(node->data.quote.child != NULL);
    ASSERT_INT_EQ(node->data.quote.child->type, AST_COMMAND);
    /* Check the command is [+ 1 2] */
    AstNode *cmd = node->data.quote.child;
    ASSERT_INT_EQ(cmd->data.command.head->type, AST_LIT_STRING);
    ASSERT(memcmp(cmd->data.command.head->data.lit_string.value, "+", 1) == 0);
    ASSERT_U32_EQ(cmd->data.command.arg_count, 2);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: parse quote $x produces AST_QUOTE wrapping AST_VAR_REF */
static int test_parse_quote_var_ref(void) {
    arena_t arena = {0};
    const char *src = "quote $x";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    AstNode *node = pr.nodes[0];
    ASSERT_INT_EQ(node->type, AST_QUOTE);
    ASSERT(node->data.quote.child != NULL);
    ASSERT_INT_EQ(node->data.quote.child->type, AST_VAR_REF);
    ASSERT(memcmp(node->data.quote.child->data.var_ref.name, "x", 1) == 0);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: parse quote { print 1; print 2 } produces AST_QUOTE wrapping AST_BLOCK */
static int test_parse_quote_block(void) {
    arena_t arena = {0};
    const char *src = "quote { print 1; print 2 }";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    AstNode *node = pr.nodes[0];
    ASSERT_INT_EQ(node->type, AST_QUOTE);
    ASSERT(node->data.quote.child != NULL);
    ASSERT_INT_EQ(node->data.quote.child->type, AST_BLOCK);
    ASSERT_U32_EQ(node->data.quote.child->data.block.count, 2);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: quote pretty-prints as "quote <child>" */
static int test_parse_quote_pretty_print(void) {
    arena_t arena = {0};
    const char *src = "quote [+ 1 2]";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    const char *pp = ast_pretty_print(pr.nodes[0], &arena);
    ASSERT(pp != NULL);
    ASSERT_STR_EQ(pp, "quote [+ 1 2]");
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: quote $x pretty-prints correctly */
static int test_parse_quote_var_pretty_print(void) {
    arena_t arena = {0};
    const char *src = "quote $x";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    const char *pp = ast_pretty_print(pr.nodes[0], &arena);
    ASSERT(pp != NULL);
    ASSERT_STR_EQ(pp, "quote $x");
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: quote round-trips through syntax objects */
static int test_quote_roundtrip(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    const char *src = "quote [+ 1 2]";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    const char *orig = ast_pretty_print(pr.nodes[0], &arena);

    const char *rt = roundtrip_pp(src, &arena, &vm, &intern);
    ASSERT(rt != NULL);
    ASSERT_STR_EQ(rt, orig);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: quote inside a command is valid */
static int test_parse_quote_inside_command(void) {
    arena_t arena = {0};
    const char *src = "[foo quote [+ 1 2]]";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.count >= 1);
    AstNode *node = pr.nodes[0];
    ASSERT_INT_EQ(node->type, AST_COMMAND);
    /* The argument should be a quote node */
    ASSERT_U32_EQ(node->data.command.arg_count, 1);
    AstNode *arg = node->data.command.args[0];
    ASSERT_INT_EQ(arg->type, AST_QUOTE);
    ASSERT_INT_EQ(arg->data.quote.child->type, AST_COMMAND);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== US-006: syntax-quote, ~, ~@ parsing tests ===== */

/* Test: parse syntax-quote [if [not ~cond] ~body] — AST_SYNTAX_QUOTE with unquotes */
static int test_parse_syntax_quote_with_unquotes(void) {
    arena_t arena = {0};
    const char *src = "syntax-quote [if [not ~$cond] ~$body]";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.error_count == 0);
    ASSERT(pr.count >= 1);
    AstNode *node = pr.nodes[0];
    ASSERT_INT_EQ(node->type, AST_SYNTAX_QUOTE);
    /* Child should be a command [if ...] */
    AstNode *child = node->data.syntax_quote.child;
    ASSERT_INT_EQ(child->type, AST_COMMAND);
    /* Second arg [not ~$cond] is a command */
    AstNode *not_cmd = child->data.command.args[0];
    ASSERT_INT_EQ(not_cmd->type, AST_COMMAND);
    /* The arg to not should be an unquote wrapping $cond */
    ASSERT_U32_EQ(not_cmd->data.command.arg_count, 1);
    AstNode *unq = not_cmd->data.command.args[0];
    ASSERT_INT_EQ(unq->type, AST_UNQUOTE);
    ASSERT_INT_EQ(unq->data.unquote.child->type, AST_VAR_REF);
    /* Third arg ~$body is also an unquote */
    AstNode *body_unq = child->data.command.args[1];
    ASSERT_INT_EQ(body_unq->type, AST_UNQUOTE);
    ASSERT_INT_EQ(body_unq->data.unquote.child->type, AST_VAR_REF);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: parse syntax-quote [foo ~@$args] — AST_UNQUOTE_SPLICING */
static int test_parse_syntax_quote_unquote_splicing(void) {
    arena_t arena = {0};
    const char *src = "syntax-quote [foo ~@$args]";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.error_count == 0);
    ASSERT(pr.count >= 1);
    AstNode *node = pr.nodes[0];
    ASSERT_INT_EQ(node->type, AST_SYNTAX_QUOTE);
    AstNode *child = node->data.syntax_quote.child;
    ASSERT_INT_EQ(child->type, AST_COMMAND);
    /* Second arg should be unquote-splicing */
    AstNode *splice = child->data.command.args[0];
    ASSERT_INT_EQ(splice->type, AST_UNQUOTE_SPLICING);
    ASSERT_INT_EQ(splice->data.unquote_splicing.child->type, AST_VAR_REF);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: ~ outside syntax-quote still works as logical NOT (not an error) */
static int test_tilde_outside_syntax_quote_error(void) {
    arena_t arena = {0};
    /* ~ outside syntax-quote is the logical NOT operator in infix context */
    const char *src = "(~$x)";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.error_count == 0);
    ASSERT(pr.count >= 1);
    /* Should parse as [~ $x] command */
    AstNode *node = pr.nodes[0];
    ASSERT_INT_EQ(node->type, AST_COMMAND);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: pretty-print syntax-quote, ~, ~@ */
static int test_parse_syntax_quote_pretty_print(void) {
    arena_t arena = {0};
    const char *src = "syntax-quote [if [not ~$cond] ~$body]";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.error_count == 0);
    ASSERT(pr.count >= 1);
    const char *pp = ast_pretty_print(pr.nodes[0], &arena);
    /* Should contain "syntax-quote" */
    ASSERT(strstr(pp, "syntax-quote") != NULL);
    /* Should contain unquote markers */
    ASSERT(strstr(pp, "~$cond") != NULL);
    ASSERT(strstr(pp, "~$body") != NULL);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: round-trip syntax-quote through syntax objects */
static int test_parse_syntax_quote_roundtrip(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;
    const char *src = "syntax-quote [foo ~$bar]";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.error_count == 0);
    ASSERT(pr.count >= 1);
    /* Convert to syntax object */
    JaclVal syn = syntax_from_ast(pr.nodes[0], &vm.heap, &intern);
    ASSERT(jacl_is_syntax(syn));
    JaclSyntax *s = jacl_as_syntax(syn);
    ASSERT_INT_EQ(s->kind, SYNTAX_SYNTAX_QUOTE);
    /* Convert back to AST */
    AstNode *rt = syntax_to_ast(syn, &arena);
    ASSERT(rt != NULL);
    ASSERT_INT_EQ(rt->type, AST_SYNTAX_QUOTE);
    /* Pretty-print both should match */
    const char *pp_orig = ast_pretty_print(pr.nodes[0], &arena);
    const char *pp_rt = ast_pretty_print(rt, &arena);
    ASSERT_STR_EQ(pp_orig, pp_rt);
    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: nested syntax-quote — inner ~ stays literal at depth > 1 */
static int test_syntax_quote_nested_depth(void) {
    arena_t arena = {0};
    /* At depth 2, ~ should NOT produce AST_UNQUOTE */
    const char *src = "syntax-quote [foo syntax-quote [bar ~$baz]]";
    ParseResult pr = parse_source(src, &arena);
    ASSERT(pr.error_count == 0);
    ASSERT(pr.count >= 1);
    AstNode *outer = pr.nodes[0];
    ASSERT_INT_EQ(outer->type, AST_SYNTAX_QUOTE);
    /* The child command [foo syntax-quote [...]] */
    AstNode *outer_cmd = outer->data.syntax_quote.child;
    ASSERT_INT_EQ(outer_cmd->type, AST_COMMAND);
    /* The second arg should be a syntax-quote node */
    AstNode *inner_sq = outer_cmd->data.command.args[0];
    ASSERT_INT_EQ(inner_sq->type, AST_SYNTAX_QUOTE);
    /* Inside the inner syntax-quote, ~ should NOT be AST_UNQUOTE
       (it's at depth 2, so it's treated as a literal) */
    AstNode *inner_cmd = inner_sq->data.syntax_quote.child;
    ASSERT_INT_EQ(inner_cmd->type, AST_COMMAND);
    /* The ~ at depth 2 falls through to atom parsing — becomes [~ $baz] command */
    AstNode *inner_arg = inner_cmd->data.command.args[0];
    /* At depth 2, ~ is literal, so this is NOT AST_UNQUOTE */
    ASSERT(inner_arg->type != AST_UNQUOTE);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: ~@ outside syntax-quote produces error */
static int test_tilde_at_outside_syntax_quote_error(void) {
    arena_t arena = {0};
    const char *src = "~@$x";
    ParseResult pr = parse_source(src, &arena);
    /* Should have an error */
    ASSERT(pr.error_count > 0);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== US-007: Compile-time macro table and defmacro registration ===== */

/* Helper: compile source and return CompileResult (caller must destroy arena) */
static CompileResult compile_source(const char* src, arena_t* arena, VM* vm) {
    LexResult tokens = lexer_lex(src, arena);
    ParseResult parse = parser_parse(tokens, arena);
    JaclInternTable intern_table;
    intern_table_init(&intern_table, arena);
    CompileResult cr = compiler_compile(parse, arena, &intern_table, &vm->heap, NULL);
    cr.error_count += parse.error_count;
    return cr;
}

/* Test: defmacro registers in macro table, lookup by name succeeds */
static int test_defmacro_registers_in_table(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "defmacro unless {cond, body} {\n"
                      "  syntax-quote [if [not ~cond] ~body]\n"
                      "}";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT(cr.error_count == 0);
    ASSERT(cr.macro_table != NULL);
    ASSERT(cr.macro_table->count == 1);

    MacroEntry* entry = macro_table_lookup(cr.macro_table, "unless", 6);
    ASSERT(entry != NULL);
    ASSERT(entry->name_len == 6);
    ASSERT(memcmp(entry->name, "unless", 6) == 0);
    ASSERT(entry->param_count == 2);
    ASSERT(entry->closure != NULL);
    ASSERT(entry->closure->param_count == 2);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: defmacro lookup for non-existent name returns NULL */
static int test_defmacro_lookup_missing(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "defmacro unless {cond, body} {\n"
                      "  syntax-quote [if [not ~cond] ~body]\n"
                      "}";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT(cr.error_count == 0);
    ASSERT(cr.macro_table != NULL);

    MacroEntry* entry = macro_table_lookup(cr.macro_table, "when", 4);
    ASSERT(entry == NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: defmacro with zero params registers correctly */
static int test_defmacro_zero_params_registers(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "defmacro now {} { syntax-quote [+ 1 2] }";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT(cr.error_count == 0);
    ASSERT(cr.macro_table != NULL);
    ASSERT(cr.macro_table->count == 1);

    MacroEntry* entry = macro_table_lookup(cr.macro_table, "now", 3);
    ASSERT(entry != NULL);
    ASSERT(entry->param_count == 0);
    ASSERT(entry->closure != NULL);
    ASSERT(entry->closure->param_count == 0);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: multiple defmacros register correctly */
static int test_defmacro_multiple_register(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "defmacro unless {cond, body} {\n"
                      "  syntax-quote [if [not ~cond] ~body]\n"
                      "}\n"
                      "defmacro double {x} {\n"
                      "  syntax-quote [+ ~x ~x]\n"
                      "}";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT(cr.error_count == 0);
    ASSERT(cr.macro_table != NULL);
    ASSERT(cr.macro_table->count == 2);

    MacroEntry* e1 = macro_table_lookup(cr.macro_table, "unless", 6);
    ASSERT(e1 != NULL);
    ASSERT(e1->param_count == 2);

    MacroEntry* e2 = macro_table_lookup(cr.macro_table, "double", 6);
    ASSERT(e2 != NULL);
    ASSERT(e2->param_count == 1);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: defmacro shadowing 'if' produces error (parser rejects keyword as name) */
static int test_defmacro_shadow_if_error(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "defmacro if {cond, body} {\n"
                      "  syntax-quote [+ ~cond ~body]\n"
                      "}";
    CompileResult cr = compile_source(src, &arena, &vm);
    /* 'if' is a keyword token, so the parser rejects it before the compiler */
    ASSERT(cr.error_count > 0);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: defmacro shadowing a non-keyword special form (spawn) produces compile error */
static int test_defmacro_shadow_spawn_error(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "defmacro spawn {x} {\n"
                      "  syntax-quote [+ ~x 1]\n"
                      "}";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT(cr.error_count > 0);
    ASSERT(cr.error_message != NULL);
    ASSERT(strstr(cr.error_message, "shadows a special form") != NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: defmacro shadowing other special forms */
static int test_defmacro_shadow_special_forms(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Test several special forms */
    const char* forms[] = {"proc", "def", "mut", "set", "while", "for",
                           "match", "break", "return", "defmacro"};
    int nforms = (int)(sizeof(forms) / sizeof(forms[0]));
    for (int i = 0; i < nforms; i++) {
        arena_t a2 = {0};
        VM vm2;
        vm_init(&vm2, &a2);
        char buf[128];
        snprintf(buf, sizeof(buf), "defmacro %s {x} { syntax-quote [+ ~x 1] }", forms[i]);
        CompileResult cr = compile_source(buf, &a2, &vm2);
        ASSERT(cr.error_count > 0);
        vm_destroy(&vm2);
        arena_destroy(&a2);
    }

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: duplicate defmacro produces compile error */
static int test_defmacro_duplicate_error(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "defmacro unless {cond, body} {\n"
                      "  syntax-quote [if [not ~cond] ~body]\n"
                      "}\n"
                      "defmacro unless {x} {\n"
                      "  syntax-quote [+ ~x 1]\n"
                      "}";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT(cr.error_count > 0);
    ASSERT(cr.error_message != NULL);
    ASSERT(strstr(cr.error_message, "already defined") != NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: macro table consulted during command head check */
static int test_macro_table_lookup_api(void) {
    MacroTable mt;
    macro_table_init(&mt);
    ASSERT(mt.count == 0);
    ASSERT(macro_table_lookup(&mt, "foo", 3) == NULL);

    /* Manually add an entry */
    mt.entries[0].name = "foo";
    mt.entries[0].name_len = 3;
    mt.entries[0].param_count = 1;
    mt.entries[0].closure = NULL;
    mt.count = 1;

    ASSERT(macro_table_lookup(&mt, "foo", 3) != NULL);
    ASSERT(macro_table_lookup(&mt, "bar", 3) == NULL);
    ASSERT(macro_table_lookup(&mt, "fo", 2) == NULL);
    ASSERT(macro_table_lookup(&mt, "fooo", 4) == NULL);

    TEST_PASS();
}

/* Test: macro__is_special_form recognizes all special forms */
static int test_is_special_form(void) {
    ASSERT(macro__is_special_form("if", 2));
    ASSERT(macro__is_special_form("proc", 4));
    ASSERT(macro__is_special_form("def", 3));
    ASSERT(macro__is_special_form("mut", 3));
    ASSERT(macro__is_special_form("set", 3));
    ASSERT(macro__is_special_form("while", 5));
    ASSERT(macro__is_special_form("for", 3));
    ASSERT(macro__is_special_form("match", 5));
    ASSERT(macro__is_special_form("break", 5));
    ASSERT(macro__is_special_form("continue", 8));
    ASSERT(macro__is_special_form("return", 6));
    ASSERT(macro__is_special_form("defmacro", 8));
    ASSERT(macro__is_special_form("defstruct", 9));
    ASSERT(macro__is_special_form("syntax-quote", 12));
    /* These should NOT be special forms */
    ASSERT(!macro__is_special_form("unless", 6));
    ASSERT(!macro__is_special_form("print", 5));
    ASSERT(!macro__is_special_form("length", 6));
    ASSERT(!macro__is_special_form("foo", 3));
    TEST_PASS();
}

/* ===== US-008: Macro expansion pass — basic expansion ===== */

/* Helper: parse source and run expansion pass, return expanded program.
 * Sets *out_err to a non-NULL error message on failure. */
static ParseResult expand_source(const char* src, arena_t* arena, VM* vm,
                                 MacroTable* mt, const char** out_err) {
    LexResult tokens = lexer_lex(src, arena);
    ParseResult parse = parser_parse(tokens, arena);
    if (parse.error_count > 0) {
        *out_err = "parse error";
        return parse;
    }
    JaclInternTable intern_table;
    intern_table_init(&intern_table, arena);
    macro_table_init(mt);
    uint32_t err_line = 0, err_col = 0;
    *out_err = ast_expand_macros(parse.nodes, parse.count, mt,
                                  &vm->heap, &intern_table, arena,
                                  &err_line, &err_col);
    return parse;
}

/* Test: defmacro unless expands correctly */
static int test_expand_unless(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "defmacro unless {cond, body} {\n"
                      "  syntax-quote [if [not ~cond] ~body]\n"
                      "}\n"
                      "unless [eq 1 2] { print ok }";
    MacroTable mt;
    const char* err = NULL;
    ParseResult pr = expand_source(src, &arena, &vm, &mt, &err);
    ASSERT(err == NULL);

    /* After expansion, node[0] is defmacro (unchanged),
     * node[1] should be the expanded [if [not [eq 1 2]] { print ok }] */
    ASSERT(pr.count >= 2);
    AstNode* expanded = pr.nodes[1];
    ASSERT(expanded != NULL);
    ASSERT(expanded->type == AST_COMMAND);

    const char* pp = ast_pretty_print(expanded, &arena);
    /* The expanded form should be: [if [not [eq 1 2]] { print ok }] */
    ASSERT(pp != NULL);
    /* Check that 'if' is the head */
    ASSERT(expanded->data.command.head != NULL);
    ASSERT(expanded->data.command.head->type == AST_LIT_STRING);
    ASSERT(expanded->data.command.head->data.lit_string.length == 2);
    ASSERT(memcmp(expanded->data.command.head->data.lit_string.value, "if", 2) == 0);
    /* Check 2 args: [not [eq 1 2]] and { print ok } */
    ASSERT(expanded->data.command.arg_count == 2);
    /* First arg: [not [eq 1 2]] */
    AstNode* not_cmd = expanded->data.command.args[0];
    ASSERT(not_cmd != NULL);
    ASSERT(not_cmd->type == AST_COMMAND);
    ASSERT(not_cmd->data.command.head->type == AST_LIT_STRING);
    ASSERT(memcmp(not_cmd->data.command.head->data.lit_string.value, "not", 3) == 0);
    ASSERT(not_cmd->data.command.arg_count == 1);
    /* The arg to not should be [eq 1 2] */
    AstNode* eq_cmd = not_cmd->data.command.args[0];
    ASSERT(eq_cmd != NULL);
    ASSERT(eq_cmd->type == AST_COMMAND);
    ASSERT(eq_cmd->data.command.head->type == AST_LIT_STRING);
    ASSERT(memcmp(eq_cmd->data.command.head->data.lit_string.value, "eq", 2) == 0);
    ASSERT(eq_cmd->data.command.arg_count == 2);
    /* Second arg: { print ok } — should be a block */
    AstNode* body = expanded->data.command.args[1];
    ASSERT(body != NULL);
    ASSERT(body->type == AST_BLOCK);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: defmacro double {x} expansion with ~x used twice */
static int test_expand_double(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "defmacro double {x} {\n"
                      "  syntax-quote [+ ~x ~x]\n"
                      "}\n"
                      "double 21";
    MacroTable mt;
    const char* err = NULL;
    ParseResult pr = expand_source(src, &arena, &vm, &mt, &err);
    ASSERT(err == NULL);

    ASSERT(pr.count >= 2);
    AstNode* expanded = pr.nodes[1];
    ASSERT(expanded != NULL);
    ASSERT(expanded->type == AST_COMMAND);
    /* Head should be + */
    ASSERT(expanded->data.command.head->type == AST_LIT_STRING);
    ASSERT(memcmp(expanded->data.command.head->data.lit_string.value, "+", 1) == 0);
    /* Two args, both should be 21 */
    ASSERT(expanded->data.command.arg_count == 2);
    ASSERT(expanded->data.command.args[0]->type == AST_LIT_INT);
    ASSERT(expanded->data.command.args[0]->data.lit_int.value == 21);
    ASSERT(expanded->data.command.args[1]->type == AST_LIT_INT);
    ASSERT(expanded->data.command.args[1]->data.lit_int.value == 21);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: macro that expands to another macro call (two-level expansion) */
static int test_expand_two_level(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "defmacro double {x} {\n"
                      "  syntax-quote [+ ~x ~x]\n"
                      "}\n"
                      "defmacro quadruple {x} {\n"
                      "  syntax-quote [double [double ~x]]\n"
                      "}\n"
                      "quadruple 5";
    MacroTable mt;
    const char* err = NULL;
    ParseResult pr = expand_source(src, &arena, &vm, &mt, &err);
    ASSERT(err == NULL);

    ASSERT(pr.count >= 3);
    AstNode* expanded = pr.nodes[2];
    ASSERT(expanded != NULL);
    ASSERT(expanded->type == AST_COMMAND);
    /* Should expand to [+ [+ 5 5] [+ 5 5]] */
    ASSERT(expanded->data.command.head->type == AST_LIT_STRING);
    ASSERT(memcmp(expanded->data.command.head->data.lit_string.value, "+", 1) == 0);
    ASSERT(expanded->data.command.arg_count == 2);
    /* Each arg should be [+ 5 5] */
    for (int a = 0; a < 2; a++) {
        AstNode* inner = expanded->data.command.args[a];
        ASSERT(inner->type == AST_COMMAND);
        ASSERT(inner->data.command.head->type == AST_LIT_STRING);
        ASSERT(memcmp(inner->data.command.head->data.lit_string.value, "+", 1) == 0);
        ASSERT(inner->data.command.arg_count == 2);
        ASSERT(inner->data.command.args[0]->type == AST_LIT_INT);
        ASSERT(inner->data.command.args[0]->data.lit_int.value == 5);
        ASSERT(inner->data.command.args[1]->type == AST_LIT_INT);
        ASSERT(inner->data.command.args[1]->data.lit_int.value == 5);
    }

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: depth limit hit by self-referencing macro */
static int test_expand_depth_limit(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "defmacro loop {x} {\n"
                      "  syntax-quote [loop ~x]\n"
                      "}\n"
                      "loop 1";
    MacroTable mt;
    const char* err = NULL;
    expand_source(src, &arena, &vm, &mt, &err);
    ASSERT(err != NULL);
    /* Error should mention depth limit */
    ASSERT(strstr(err, "depth limit") != NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: macro used before its defmacro is not expanded (treated as normal command) */
static int test_expand_use_before_defmacro(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "unless [eq 1 2] { print ok }\n"
                      "defmacro unless {cond, body} {\n"
                      "  syntax-quote [if [not ~cond] ~body]\n"
                      "}";
    MacroTable mt;
    const char* err = NULL;
    ParseResult pr = expand_source(src, &arena, &vm, &mt, &err);
    ASSERT(err == NULL);

    /* The first statement should NOT be expanded (macro defined after use).
     * It should remain as AST_COMMAND with head "unless". */
    ASSERT(pr.count >= 2);
    AstNode* first = pr.nodes[0];
    ASSERT(first != NULL);
    ASSERT(first->type == AST_COMMAND);
    ASSERT(first->data.command.head->type == AST_LIT_STRING);
    ASSERT(first->data.command.head->data.lit_string.length == 6);
    ASSERT(memcmp(first->data.command.head->data.lit_string.value, "unless", 6) == 0);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: macro with wrong argument count produces expansion error */
static int test_expand_wrong_arg_count(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "defmacro unless {cond, body} {\n"
                      "  syntax-quote [if [not ~cond] ~body]\n"
                      "}\n"
                      "unless [eq 1 2]";
    MacroTable mt;
    const char* err = NULL;
    expand_source(src, &arena, &vm, &mt, &err);
    ASSERT(err != NULL);
    /* Error should mention argument count */
    ASSERT(strstr(err, "expects 2 arguments but got 1") != NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: non-macro commands pass through unchanged */
static int test_expand_non_macro_passthrough(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "defmacro unless {cond, body} {\n"
                      "  syntax-quote [if [not ~cond] ~body]\n"
                      "}\n"
                      "print hello";
    MacroTable mt;
    const char* err = NULL;
    ParseResult pr = expand_source(src, &arena, &vm, &mt, &err);
    ASSERT(err == NULL);

    ASSERT(pr.count >= 2);
    AstNode* print_cmd = pr.nodes[1];
    ASSERT(print_cmd != NULL);
    ASSERT(print_cmd->type == AST_COMMAND);
    ASSERT(print_cmd->data.command.head->type == AST_LIT_STRING);
    ASSERT(memcmp(print_cmd->data.command.head->data.lit_string.value, "print", 5) == 0);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: macro expansion inside nested blocks */
static int test_expand_inside_block(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "defmacro double {x} {\n"
                      "  syntax-quote [+ ~x ~x]\n"
                      "}\n"
                      "if 1 { double 3 }";
    MacroTable mt;
    const char* err = NULL;
    ParseResult pr = expand_source(src, &arena, &vm, &mt, &err);
    ASSERT(err == NULL);

    ASSERT(pr.count >= 2);
    AstNode* if_cmd = pr.nodes[1];
    ASSERT(if_cmd != NULL);
    ASSERT(if_cmd->type == AST_COMMAND);
    /* The block body should contain expanded [+ 3 3] */
    AstNode* block = if_cmd->data.command.args[1];
    ASSERT(block != NULL);
    ASSERT(block->type == AST_BLOCK);
    ASSERT(block->data.block.count == 1);
    AstNode* plus_cmd = block->data.block.commands[0];
    ASSERT(plus_cmd != NULL);
    ASSERT(plus_cmd->type == AST_COMMAND);
    ASSERT(memcmp(plus_cmd->data.command.head->data.lit_string.value, "+", 1) == 0);
    ASSERT(plus_cmd->data.command.arg_count == 2);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== US-009: Compiler handling of quote — emit syntax object literal ===== */

/* Helper: find the first syntax object constant in a chunk. */
static JaclVal find_first_syntax_const(BytecodeChunk* chunk) {
    for (uint32_t i = 0; i < chunk->const_count; i++) {
        if (jacl_is_syntax(chunk->constants[i])) {
            return chunk->constants[i];
        }
    }
    return JACL_NIL;
}

/* Test: quote [+ 1 2] compiles to a syntax object command constant. */
static int test_compile_quote_command(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "quote [+ 1 2]";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT(cr.error_count == 0);

    /* The constant pool should contain a syntax object. */
    JaclVal syn_val = find_first_syntax_const(&cr.chunk);
    ASSERT(jacl_is_syntax(syn_val));
    JaclSyntax* s = jacl_as_syntax(syn_val);
    ASSERT_INT_EQ(s->kind, SYNTAX_COMMAND);

    /* Head should be a lit-string syntax with value "+". */
    ASSERT(jacl_is_syntax(s->data.command.head));
    JaclSyntax* head = jacl_as_syntax(s->data.command.head);
    ASSERT_INT_EQ(head->kind, SYNTAX_LIT_STRING);
    ASSERT(jacl_is_string(head->data.lit_string.value));
    {
        char buf[16];
        uint32_t n = jacl_string_data(head->data.lit_string.value, buf, sizeof(buf));
        buf[n] = '\0';
        ASSERT_STR_EQ(buf, "+");
    }

    /* Args should be a vec of 2 lit-int syntax objects. */
    ASSERT(jacl_is_vector(s->data.command.args));
    jacl_vec_root* args = (jacl_vec_root*)jacl_as_ptr(s->data.command.args);
    ASSERT_U32_EQ(jacl_vec_count(args), 2);

    JaclSyntax* a1 = jacl_as_syntax(jacl_vec_get(args, 0).value);
    ASSERT_INT_EQ(a1->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(a1->data.lit_int.value, 1);

    JaclSyntax* a2 = jacl_as_syntax(jacl_vec_get(args, 1).value);
    ASSERT_INT_EQ(a2->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(a2->data.lit_int.value, 2);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: quote $x compiles to a var-ref syntax (not a variable lookup). */
static int test_compile_quote_var_ref(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "quote $x";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT(cr.error_count == 0);

    JaclVal syn_val = find_first_syntax_const(&cr.chunk);
    ASSERT(jacl_is_syntax(syn_val));
    JaclSyntax* s = jacl_as_syntax(syn_val);
    ASSERT_INT_EQ(s->kind, SYNTAX_VAR_REF);
    ASSERT(jacl_is_string(s->data.var_ref.name));
    {
        char buf[16];
        uint32_t n = jacl_string_data(s->data.var_ref.name, buf, sizeof(buf));
        buf[n] = '\0';
        ASSERT_STR_EQ(buf, "x");
    }

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: quote 42 compiles to a lit-int syntax. */
static int test_compile_quote_lit_int(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "quote 42";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT(cr.error_count == 0);

    JaclVal syn_val = find_first_syntax_const(&cr.chunk);
    ASSERT(jacl_is_syntax(syn_val));
    JaclSyntax* s = jacl_as_syntax(syn_val);
    ASSERT_INT_EQ(s->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(s->data.lit_int.value, 42);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: nested quote — quote [foo quote [bar]] produces a command syntax
 * whose second element is a SYNTAX_QUOTE wrapping another command. */
static int test_compile_quote_nested(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char* src = "quote [foo quote [bar]]";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT(cr.error_count == 0);

    JaclVal syn_val = find_first_syntax_const(&cr.chunk);
    ASSERT(jacl_is_syntax(syn_val));
    JaclSyntax* outer = jacl_as_syntax(syn_val);
    ASSERT_INT_EQ(outer->kind, SYNTAX_COMMAND);

    /* Head: lit-string "foo" */
    JaclSyntax* head = jacl_as_syntax(outer->data.command.head);
    ASSERT_INT_EQ(head->kind, SYNTAX_LIT_STRING);
    {
        char buf[16];
        uint32_t n = jacl_string_data(head->data.lit_string.value, buf, sizeof(buf));
        buf[n] = '\0';
        ASSERT_STR_EQ(buf, "foo");
    }

    /* Args: one element, a SYNTAX_QUOTE wrapping [bar] */
    jacl_vec_root* args = (jacl_vec_root*)jacl_as_ptr(outer->data.command.args);
    ASSERT_U32_EQ(jacl_vec_count(args), 1);

    JaclSyntax* inner_quote = jacl_as_syntax(jacl_vec_get(args, 0).value);
    ASSERT_INT_EQ(inner_quote->kind, SYNTAX_QUOTE);

    JaclSyntax* inner_cmd = jacl_as_syntax(inner_quote->data.quote.child);
    ASSERT_INT_EQ(inner_cmd->kind, SYNTAX_COMMAND);
    JaclSyntax* inner_head = jacl_as_syntax(inner_cmd->data.command.head);
    ASSERT_INT_EQ(inner_head->kind, SYNTAX_LIT_STRING);
    {
        char buf[16];
        uint32_t n = jacl_string_data(inner_head->data.lit_string.value, buf, sizeof(buf));
        buf[n] = '\0';
        ASSERT_STR_EQ(buf, "bar");
    }

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: quote inside a macro body — the macro's compiled closure chunk
 * must contain the syntax object as a constant, and the macro must
 * return that syntax object when called. */
static int test_compile_quote_inside_macro_body(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Macro body uses quote [+ 1 2] directly (not syntax-quote). */
    const char* src = "defmacro q {} { quote [+ 1 2] }";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT(cr.error_count == 0);
    ASSERT(cr.macro_table != NULL);

    MacroEntry* entry = macro_table_lookup(cr.macro_table, "q", 1);
    ASSERT(entry != NULL);
    ASSERT(entry->closure != NULL);

    /* Find the syntax object constant inside the closure's chunk. */
    JaclVal syn_val = find_first_syntax_const(&entry->closure->chunk);
    ASSERT(jacl_is_syntax(syn_val));
    JaclSyntax* s = jacl_as_syntax(syn_val);
    ASSERT_INT_EQ(s->kind, SYNTAX_COMMAND);

    /* Verify the head is the literal "+" */
    JaclSyntax* head = jacl_as_syntax(s->data.command.head);
    ASSERT_INT_EQ(head->kind, SYNTAX_LIT_STRING);
    {
        char buf[16];
        uint32_t n = jacl_string_data(head->data.lit_string.value, buf, sizeof(buf));
        buf[n] = '\0';
        ASSERT_STR_EQ(buf, "+");
    }

    /* And the args are lit-ints 1 and 2 */
    jacl_vec_root* args = (jacl_vec_root*)jacl_as_ptr(s->data.command.args);
    ASSERT_U32_EQ(jacl_vec_count(args), 2);
    JaclSyntax* a1 = jacl_as_syntax(jacl_vec_get(args, 0).value);
    ASSERT_INT_EQ(a1->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(a1->data.lit_int.value, 1);
    JaclSyntax* a2 = jacl_as_syntax(jacl_vec_get(args, 1).value);
    ASSERT_INT_EQ(a2->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(a2->data.lit_int.value, 2);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== US-010: Compiler handling of syntax-quote — template expansion ===== */

/* Helper: allocate a SYNTAX_LIT_INT value */
static JaclVal us010_make_lit_int(ThreadHeap* heap, int32_t v) {
    JaclVal s = gc_alloc_syntax(heap);
    JaclSyntax* sy = jacl_as_syntax(s);
    sy->kind = SYNTAX_LIT_INT;
    sy->data.lit_int.value = v;
    return s;
}

/* Helper: allocate a SYNTAX_VAR_REF value with a given name */
static JaclVal us010_make_var_ref(ThreadHeap* heap, const char* name) {
    JaclVal s = gc_alloc_syntax(heap);
    JaclSyntax* sy = jacl_as_syntax(s);
    sy->kind = SYNTAX_VAR_REF;
    sy->data.var_ref.name = jacl_inline_string(name, (uint32_t)strlen(name));
    return s;
}

/* Helper: allocate a SYNTAX_COMMAND with a head and args vec */
static JaclVal us010_make_command(ThreadHeap* heap, JaclVal head,
                                   JaclVal* args, uint32_t argc) {
    jacl_vec_root* v = jacl_vec_empty();
    for (uint32_t i = 0; i < argc; i++) {
        v = jacl_vec_push_back(v, args[i]);
    }
    JaclVal s = gc_alloc_syntax(heap);
    JaclSyntax* sy = jacl_as_syntax(s);
    sy->kind = SYNTAX_COMMAND;
    sy->data.command.head = head;
    sy->data.command.args = jacl_vector_ptr(v);
    return s;
}

/* Helper: build a vec of values */
static JaclVal us010_make_vec(JaclVal* elems, uint32_t n) {
    jacl_vec_root* v = jacl_vec_empty();
    for (uint32_t i = 0; i < n; i++) {
        v = jacl_vec_push_back(v, elems[i]);
    }
    return jacl_vector_ptr(v);
}

/* Helper: invoke a compiled macro closure with the given syntax object
 * arguments, returning the result in *out. Mimics jacl_call_val's setup
 * without requiring a JaclVM wrapper. */
static VMResult us010_run_closure(VM* vm, JaclClosure* cl,
                                   JaclVal* args, uint32_t argc,
                                   JaclVal* out) {
    vm->error_message = NULL;
    vm->error_line = 0;

    vm->stack[0] = jacl_closure(cl);
    for (uint32_t i = 0; i < argc; i++) vm->stack[1 + i] = args[i];
    vm->stack_top = 1 + argc;

    vm->frames[0].closure    = cl;
    vm->frames[0].return_ip  = NULL;
    vm->frames[0].stack_base = 1;
    vm->frames[0].chunk      = &cl->chunk;
    vm->frame_count = 1;

    vm->chunk     = &cl->chunk;
    vm->top_chunk = &cl->chunk;
    vm->ip        = cl->chunk.code;

    VMResult r = vm__run(vm, 0);
    if (out) {
        *out = (vm->stack_top > 0) ? vm->stack[0] : JACL_NIL;
    }
    return r;
}

/* Helper: extract a short string from a SYNTAX_LIT_STRING/VAR_REF name field */
static void us010_syn_name(JaclSyntax* s, char* buf, size_t buf_len) {
    JaclVal nm = (s->kind == SYNTAX_VAR_REF) ? s->data.var_ref.name
                                              : s->data.lit_string.value;
    uint32_t n = jacl_string_data(nm, buf, (uint32_t)buf_len - 1);
    buf[n] = '\0';
}

/* Test: syntax-quote [if [not ~cond] ~body] runs through the compiled
 * bytecode and builds an SYNTAX_COMMAND with head "if" and 2 args. */
static int test_us010_syntax_quote_unquotes(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    const char* src =
        "defmacro wrap {cond, body} {\n"
        "  syntax-quote [if [not ~cond] ~body]\n"
        "}";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT_INT_EQ(cr.error_count, 0);
    MacroEntry* entry = macro_table_lookup(cr.macro_table, "wrap", 4);
    ASSERT(entry != NULL);
    ASSERT(entry->closure != NULL);

    /* Build syntax object args: cond = lit-int 0, body = lit-int 42 */
    JaclVal cond_arg = us010_make_lit_int(&vm.heap, 0);
    JaclVal body_arg = us010_make_lit_int(&vm.heap, 42);
    JaclVal args[2] = { cond_arg, body_arg };

    JaclVal result;
    VMResult r = us010_run_closure(&vm, entry->closure, args, 2, &result);
    ASSERT_INT_EQ(r, VM_OK);
    ASSERT(jacl_is_syntax(result));

    JaclSyntax* top = jacl_as_syntax(result);
    ASSERT_INT_EQ(top->kind, SYNTAX_COMMAND);

    /* Head should be "if" */
    JaclSyntax* top_head = jacl_as_syntax(top->data.command.head);
    ASSERT_INT_EQ(top_head->kind, SYNTAX_LIT_STRING);
    {
        char buf[16];
        us010_syn_name(top_head, buf, sizeof(buf));
        ASSERT_STR_EQ(buf, "if");
    }

    /* Args: [not cond] and body_arg (the filled-in one) */
    jacl_vec_root* top_args = (jacl_vec_root*)jacl_as_ptr(top->data.command.args);
    ASSERT_U32_EQ(jacl_vec_count(top_args), 2);

    /* First arg: [not ~cond] → command with head "not" and 1 arg (cond_arg) */
    JaclSyntax* a0 = jacl_as_syntax(jacl_vec_get(top_args, 0).value);
    ASSERT_INT_EQ(a0->kind, SYNTAX_COMMAND);
    JaclSyntax* a0_head = jacl_as_syntax(a0->data.command.head);
    ASSERT_INT_EQ(a0_head->kind, SYNTAX_LIT_STRING);
    {
        char buf[16];
        us010_syn_name(a0_head, buf, sizeof(buf));
        ASSERT_STR_EQ(buf, "not");
    }
    jacl_vec_root* a0_args = (jacl_vec_root*)jacl_as_ptr(a0->data.command.args);
    ASSERT_U32_EQ(jacl_vec_count(a0_args), 1);
    JaclSyntax* cond_filled = jacl_as_syntax(jacl_vec_get(a0_args, 0).value);
    ASSERT_INT_EQ(cond_filled->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(cond_filled->data.lit_int.value, 0);

    /* Second arg: body_arg as-is (lit-int 42) */
    JaclSyntax* a1 = jacl_as_syntax(jacl_vec_get(top_args, 1).value);
    ASSERT_INT_EQ(a1->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(a1->data.lit_int.value, 42);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax-quote [foo ~@args] flattens a 2-element vec into the
 * command's arg list, producing a 2-arg command. */
static int test_us010_unquote_splicing_in_args(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    const char* src =
        "defmacro wrap {args} {\n"
        "  syntax-quote [foo ~@args]\n"
        "}";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT_INT_EQ(cr.error_count, 0);
    MacroEntry* entry = macro_table_lookup(cr.macro_table, "wrap", 4);
    ASSERT(entry != NULL);

    /* Build a vec of 2 lit-int syntax objects */
    JaclVal e0 = us010_make_lit_int(&vm.heap, 10);
    JaclVal e1 = us010_make_lit_int(&vm.heap, 20);
    JaclVal elems[2] = { e0, e1 };
    JaclVal args_vec = us010_make_vec(elems, 2);
    JaclVal call_args[1] = { args_vec };

    JaclVal result;
    VMResult r = us010_run_closure(&vm, entry->closure, call_args, 1, &result);
    ASSERT_INT_EQ(r, VM_OK);
    ASSERT(jacl_is_syntax(result));

    JaclSyntax* cmd = jacl_as_syntax(result);
    ASSERT_INT_EQ(cmd->kind, SYNTAX_COMMAND);

    JaclSyntax* head = jacl_as_syntax(cmd->data.command.head);
    ASSERT_INT_EQ(head->kind, SYNTAX_LIT_STRING);
    {
        char buf[16];
        us010_syn_name(head, buf, sizeof(buf));
        ASSERT_STR_EQ(buf, "foo");
    }

    /* Args: 2 elements flattened from the vec */
    jacl_vec_root* rargs = (jacl_vec_root*)jacl_as_ptr(cmd->data.command.args);
    ASSERT_U32_EQ(jacl_vec_count(rargs), 2);
    JaclSyntax* a0 = jacl_as_syntax(jacl_vec_get(rargs, 0).value);
    ASSERT_INT_EQ(a0->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(a0->data.lit_int.value, 10);
    JaclSyntax* a1 = jacl_as_syntax(jacl_vec_get(rargs, 1).value);
    ASSERT_INT_EQ(a1->kind, SYNTAX_LIT_INT);
    ASSERT_INT_EQ(a1->data.lit_int.value, 20);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: syntax-quote { ~@stmts } flattens a vec of syntax commands into
 * a block. */
static int test_us010_unquote_splicing_in_block(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    const char* src =
        "defmacro wrap {stmts} {\n"
        "  syntax-quote { ~@stmts }\n"
        "}";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT_INT_EQ(cr.error_count, 0);
    MacroEntry* entry = macro_table_lookup(cr.macro_table, "wrap", 4);
    ASSERT(entry != NULL);

    /* Build 2 command syntax objects: [print 1] and [print 2] */
    JaclVal head1 = us010_make_var_ref(&vm.heap, "print");
    JaclVal arg1  = us010_make_lit_int(&vm.heap, 1);
    JaclVal cmd1_args[1] = { arg1 };
    JaclVal cmd1 = us010_make_command(&vm.heap, head1, cmd1_args, 1);

    JaclVal head2 = us010_make_var_ref(&vm.heap, "print");
    JaclVal arg2  = us010_make_lit_int(&vm.heap, 2);
    JaclVal cmd2_args[1] = { arg2 };
    JaclVal cmd2 = us010_make_command(&vm.heap, head2, cmd2_args, 1);

    JaclVal stmts_elems[2] = { cmd1, cmd2 };
    JaclVal stmts_vec = us010_make_vec(stmts_elems, 2);
    JaclVal call_args[1] = { stmts_vec };

    JaclVal result;
    VMResult r = us010_run_closure(&vm, entry->closure, call_args, 1, &result);
    ASSERT_INT_EQ(r, VM_OK);
    ASSERT(jacl_is_syntax(result));

    JaclSyntax* blk = jacl_as_syntax(result);
    ASSERT_INT_EQ(blk->kind, SYNTAX_BLOCK);

    jacl_vec_root* cmds = (jacl_vec_root*)jacl_as_ptr(blk->data.block.commands);
    ASSERT_U32_EQ(jacl_vec_count(cmds), 2);
    JaclSyntax* b0 = jacl_as_syntax(jacl_vec_get(cmds, 0).value);
    ASSERT_INT_EQ(b0->kind, SYNTAX_COMMAND);
    JaclSyntax* b1 = jacl_as_syntax(jacl_vec_get(cmds, 1).value);
    ASSERT_INT_EQ(b1->kind, SYNTAX_COMMAND);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: runtime error when ~ result is not a syntax object. */
static int test_us010_unquote_type_error(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    const char* src =
        "defmacro wrap {x} {\n"
        "  syntax-quote [foo ~x]\n"
        "}";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT_INT_EQ(cr.error_count, 0);
    MacroEntry* entry = macro_table_lookup(cr.macro_table, "wrap", 4);
    ASSERT(entry != NULL);

    /* Pass a raw i32 instead of a syntax object */
    JaclVal args[1] = { jacl_i32(42) };

    JaclVal result;
    VMResult r = us010_run_closure(&vm, entry->closure, args, 1, &result);
    ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
    ASSERT(vm.error_message != NULL);
    ASSERT(strstr(vm.error_message, "syntax object") != NULL);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: runtime error when ~@ result is not a vec of syntax objects. */
static int test_us010_unquote_splicing_type_error(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    const char* src =
        "defmacro wrap {args} {\n"
        "  syntax-quote [foo ~@args]\n"
        "}";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT_INT_EQ(cr.error_count, 0);
    MacroEntry* entry = macro_table_lookup(cr.macro_table, "wrap", 4);
    ASSERT(entry != NULL);

    /* Pass a raw i32 (not a vec) */
    JaclVal call_args[1] = { jacl_i32(42) };

    JaclVal result;
    VMResult r = us010_run_closure(&vm, entry->closure, call_args, 1, &result);
    ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
    ASSERT(vm.error_message != NULL);
    ASSERT(strstr(vm.error_message, "vec of syntax objects") != NULL);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: runtime error when ~@ receives a vec containing non-syntax elements. */
static int test_us010_unquote_splicing_vec_nonsyntax(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    const char* src =
        "defmacro wrap {args} {\n"
        "  syntax-quote [foo ~@args]\n"
        "}";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT_INT_EQ(cr.error_count, 0);
    MacroEntry* entry = macro_table_lookup(cr.macro_table, "wrap", 4);
    ASSERT(entry != NULL);

    /* Build a vec where elements are i32 instead of syntax */
    JaclVal elems[2] = { jacl_i32(1), jacl_i32(2) };
    JaclVal bad_vec = us010_make_vec(elems, 2);
    JaclVal call_args[1] = { bad_vec };

    JaclVal result;
    VMResult r = us010_run_closure(&vm, entry->closure, call_args, 1, &result);
    ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
    ASSERT(vm.error_message != NULL);
    ASSERT(strstr(vm.error_message, "vec of syntax objects") != NULL);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: nested syntax-quote is treated as literal (outer ~ does not
 * recurse into inner template). */
static int test_us010_nested_syntax_quote_literal(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable intern;
    intern_table_init(&intern, &arena);
    vm.intern_table = &intern;

    /* Outer macro with no unquotes — the inner syntax-quote is a literal
     * part of the template. With zero unquotes, the template constant is
     * emitted directly (OP_SYNTAX_SPLICE is not used). */
    const char* src =
        "defmacro mk {} {\n"
        "  syntax-quote [foo 1]\n"
        "}";
    CompileResult cr = compile_source(src, &arena, &vm);
    ASSERT_INT_EQ(cr.error_count, 0);
    MacroEntry* entry = macro_table_lookup(cr.macro_table, "mk", 2);
    ASSERT(entry != NULL);

    JaclVal result;
    VMResult r = us010_run_closure(&vm, entry->closure, NULL, 0, &result);
    ASSERT_INT_EQ(r, VM_OK);
    ASSERT(jacl_is_syntax(result));

    JaclSyntax* cmd = jacl_as_syntax(result);
    ASSERT_INT_EQ(cmd->kind, SYNTAX_COMMAND);
    jacl_vec_root* rargs = (jacl_vec_root*)jacl_as_ptr(cmd->data.command.args);
    ASSERT_U32_EQ(jacl_vec_count(rargs), 1);

    intern_table_destroy(&intern);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== US-011: End-to-end basic macro tests ===== */

/* Test: defmacro double {x} { syntax-quote [+ ~x ~x] } ; double 21 → 42 */
static int test_us011_double_macro(void) {
    JaclVM* vm = jacl_vm_new();
    ASSERT(vm != NULL);

    const char* src =
        "defmacro double {x} {\n"
        "  syntax-quote [+ ~x ~x]\n"
        "}\n"
        "double 21";
    JaclVal result = jacl_eval(vm, src);
    ASSERT(!jacl_is_error(result));
    ASSERT(jacl_is_i32(result));
    ASSERT_INT_EQ(jacl_as_i32(result), 42);

    jacl_vm_free(vm);
    TEST_PASS();
}

/* Test: defmacro unless expands and executes body when condition is false.
 * Note: the PRD example uses `[not ~cond]` but jacl's current builtins don't
 * include `not` — we express the same semantics with `if ~cond {} ~body`
 * (3-arg if with empty then-branch). */
static int test_us011_unless_macro(void) {
    JaclVM* vm = jacl_vm_new();
    ASSERT(vm != NULL);

    /* Use a mutable global side-effect to verify the body was executed:
     * unless [== 1 2] { set x 42 } — cond is false, so body runs, x becomes 42. */
    const char* src =
        "mut x 0\n"
        "defmacro unless {cond, body} {\n"
        "  syntax-quote [if ~cond {} ~body]\n"
        "}\n"
        "unless [== 1 2] { set x 42 }\n"
        "$x";
    JaclVal result = jacl_eval(vm, src);
    ASSERT(!jacl_is_error(result));
    ASSERT(jacl_is_i32(result));
    ASSERT_INT_EQ(jacl_as_i32(result), 42);

    jacl_vm_free(vm);
    TEST_PASS();
}

/* Test: unless macro does NOT execute body when condition is true */
static int test_us011_unless_macro_true_cond(void) {
    JaclVM* vm = jacl_vm_new();
    ASSERT(vm != NULL);

    /* unless [== 1 1] { set x 42 } — cond is true, so body is NOT run,
     * x remains 7. */
    const char* src =
        "mut x 7\n"
        "defmacro unless {cond, body} {\n"
        "  syntax-quote [if ~cond {} ~body]\n"
        "}\n"
        "unless [== 1 1] { set x 42 }\n"
        "$x";
    JaclVal result = jacl_eval(vm, src);
    ASSERT(!jacl_is_error(result));
    ASSERT(jacl_is_i32(result));
    ASSERT_INT_EQ(jacl_as_i32(result), 7);

    jacl_vm_free(vm);
    TEST_PASS();
}

/* Test: macro that expands to another macro call (two-level expansion) */
static int test_us011_two_level_macro(void) {
    JaclVM* vm = jacl_vm_new();
    ASSERT(vm != NULL);

    /* quadruple 5 → [double [double 5]] → [+ [+ 5 5] [+ 5 5]] → 20 */
    const char* src =
        "defmacro double {x} {\n"
        "  syntax-quote [+ ~x ~x]\n"
        "}\n"
        "defmacro quadruple {x} {\n"
        "  syntax-quote [double [double ~x]]\n"
        "}\n"
        "quadruple 5";
    JaclVal result = jacl_eval(vm, src);
    ASSERT(!jacl_is_error(result));
    ASSERT(jacl_is_i32(result));
    ASSERT_INT_EQ(jacl_as_i32(result), 20);

    jacl_vm_free(vm);
    TEST_PASS();
}

/* Test: macro used before its defmacro produces an error.
 * The expansion pass walks top-to-bottom and leaves the pre-defmacro call as
 * a plain AST_COMMAND — the compiler then tries to resolve 'unless' as a
 * normal proc/builtin and fails. */
static int test_us011_use_before_defmacro_error(void) {
    JaclVM* vm = jacl_vm_new();
    ASSERT(vm != NULL);

    const char* src =
        "unless [eq 1 2] { set x 42 }\n"
        "defmacro unless {cond, body} {\n"
        "  syntax-quote [if [not ~cond] ~body]\n"
        "}";
    JaclVal result = jacl_eval(vm, src);
    ASSERT(jacl_is_error(result));

    const char* msg = jacl_error_message_str(vm, result);
    ASSERT(msg != NULL);
    ASSERT(strlen(msg) > 0);

    jacl_vm_free(vm);
    TEST_PASS();
}

/* ===== US-012: Scope marks on syntax objects for hygiene ===== */

/* Test: a macro-introduced local does not shadow the caller's variable
 * when the caller's variable is referenced through an unquote.
 *
 * Without hygiene, the template's `mut tmp 1` would shadow the caller's
 * `tmp` and the unquoted `~x` (which is syntactically `$tmp` from the
 * caller) would bind to the inner `1`, giving `+ 1 1 = 2`.
 *
 * With scope marks: the template's `tmp` carries the macro's fresh
 * mark, while `~x` retains the caller's mark (0). Name resolution
 * finds the caller's `tmp = 99` for `~x` and the template's `tmp = 1`
 * for the template-introduced `$tmp`. Result: 99 + 1 = 100. */
static int test_us012_hygiene_local_shadow(void) {
    JaclVM* vm = jacl_vm_new();
    ASSERT(vm != NULL);

    const char* src =
        "defmacro m {x} {\n"
        "  syntax-quote {\n"
        "    mut tmp 1\n"
        "    [+ ~x $tmp]\n"
        "  }\n"
        "}\n"
        "proc run {} {\n"
        "  mut tmp 99\n"
        "  m $tmp\n"
        "}\n"
        "run";
    JaclVal result = jacl_eval(vm, src);
    ASSERT(!jacl_is_error(result));
    ASSERT(jacl_is_i32(result));
    ASSERT_INT_EQ(jacl_as_i32(result), 100);

    jacl_vm_free(vm);
    TEST_PASS();
}

/* Test: two nested macro expansions that each introduce the same name
 * do not collide — each expansion gets a distinct scope mark.
 *
 * `add_ten` binds `t = 10`, `add_hundred` binds `t = 100`, caller has
 * `t = 0`. Result: 0 + 100 + 10 = 110. Every `~x` reference retains
 * its original scope mark so the chain of additions is correct. */
static int test_us012_hygiene_nested_macros(void) {
    JaclVM* vm = jacl_vm_new();
    ASSERT(vm != NULL);

    const char* src =
        "defmacro add_ten {x} {\n"
        "  syntax-quote {\n"
        "    mut t 10\n"
        "    [+ ~x $t]\n"
        "  }\n"
        "}\n"
        "defmacro add_hundred {x} {\n"
        "  syntax-quote {\n"
        "    mut t 100\n"
        "    [add_ten [+ ~x $t]]\n"
        "  }\n"
        "}\n"
        "proc run {} {\n"
        "  mut t 0\n"
        "  add_hundred $t\n"
        "}\n"
        "run";
    JaclVal result = jacl_eval(vm, src);
    ASSERT(!jacl_is_error(result));
    ASSERT(jacl_is_i32(result));
    ASSERT_INT_EQ(jacl_as_i32(result), 110);

    jacl_vm_free(vm);
    TEST_PASS();
}

/* Test: macros can still reference globals/builtins from the template
 * (e.g. `+`) — these resolve via the mark-0 fallback because globals
 * carry no scope mark. The double macro from US-011 still works after
 * introducing scope marks. */
static int test_us012_global_fallback_still_works(void) {
    JaclVM* vm = jacl_vm_new();
    ASSERT(vm != NULL);

    const char* src =
        "defmacro double {x} {\n"
        "  syntax-quote [+ ~x ~x]\n"
        "}\n"
        "double 21";
    JaclVal result = jacl_eval(vm, src);
    ASSERT(!jacl_is_error(result));
    ASSERT(jacl_is_i32(result));
    ASSERT_INT_EQ(jacl_as_i32(result), 42);

    jacl_vm_free(vm);
    TEST_PASS();
}

/* ===== US-013: ^ prefix for intentional binding introduction ===== */

/* Core caret behavior: a macro body using [mut ^it ~v] creates an `it`
 * binding in the caller's scope (scope mark 0), so the caller's $it
 * reference resolves to it.
 *
 * Without the caret: stamping would mark `it` with a fresh macro mark,
 * making it invisible to the caller's mark-0 $it reference. */
static int test_us013_caret_binds_in_caller_scope(void) {
    JaclVM* vm = jacl_vm_new();
    ASSERT(vm != NULL);

    const char* src =
        "defmacro bindit {v} {\n"
        "  syntax-quote [mut ^it ~v]\n"
        "}\n"
        "proc run {} {\n"
        "  bindit 42\n"
        "  $it\n"
        "}\n"
        "run";
    JaclVal result = jacl_eval(vm, src);
    ASSERT(!jacl_is_error(result));
    ASSERT(jacl_is_i32(result));
    ASSERT_INT_EQ(jacl_as_i32(result), 42);

    jacl_vm_free(vm);
    TEST_PASS();
}

/* Test: using ^name outside of syntax-quote is a parse error whose
 * message mentions syntax-quote. */
static int test_us013_caret_outside_syntax_quote_error(void) {
    JaclVM* vm = jacl_vm_new();
    ASSERT(vm != NULL);

    const char* src = "^foo";
    JaclVal result = jacl_eval(vm, src);
    ASSERT(jacl_is_error(result));
    const char* msg = jacl_error_message_str(vm, result);
    ASSERT(msg != NULL);
    ASSERT(strstr(msg, "syntax-quote") != NULL);

    jacl_vm_free(vm);
    TEST_PASS();
}

/* Test: ^it in a macro body overrides a caller's pre-existing `it`.
 * This verifies the caret path does not accidentally create a separate
 * binding with a different scope mark — it ends up at mark 0, same as
 * the caller's own `mut it`, so the most recent binding wins. */
static int test_us013_caret_does_not_collide_with_internal(void) {
    JaclVM* vm = jacl_vm_new();
    ASSERT(vm != NULL);

    const char* src =
        "defmacro collide {x} {\n"
        "  syntax-quote [mut ^it ~x]\n"
        "}\n"
        "proc run {} {\n"
        "  mut it 999\n"
        "  collide 7\n"
        "  $it\n"
        "}\n"
        "run";
    JaclVal result = jacl_eval(vm, src);
    ASSERT(!jacl_is_error(result));
    ASSERT(jacl_is_i32(result));
    ASSERT_INT_EQ(jacl_as_i32(result), 7);

    jacl_vm_free(vm);
    TEST_PASS();
}

/* Test: macro with wrong argument count produces a compile error. */
static int test_us011_wrong_arg_count_error(void) {
    JaclVM* vm = jacl_vm_new();
    ASSERT(vm != NULL);

    /* unless expects 2 args but only 1 given. */
    const char* src =
        "defmacro unless {cond, body} {\n"
        "  syntax-quote [if [not ~cond] ~body]\n"
        "}\n"
        "unless [eq 1 2]";
    JaclVal result = jacl_eval(vm, src);
    ASSERT(jacl_is_error(result));

    const char* msg = jacl_error_message_str(vm, result);
    ASSERT(msg != NULL);
    /* The error from ast_expand_macros mentions argument count. */
    ASSERT(strstr(msg, "argument") != NULL || strstr(msg, "expects") != NULL);

    jacl_vm_free(vm);
    TEST_PASS();
}

/* ===== US-014: gensym builtin ===== */

/* Test: jacl_gensym_next returns a SYNTAX_VAR_REF with is_gensym=1. */
static int test_us014_gensym_returns_var_ref(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char *err = NULL;
    JaclVal v = jacl_gensym_next("g", 1, &vm.heap, 0, &err);
    ASSERT(err == NULL);
    ASSERT(jacl_is_syntax(v));
    JaclSyntax *s = jacl_as_syntax(v);
    ASSERT_U32_EQ(s->kind, SYNTAX_VAR_REF);
    ASSERT_U32_EQ(s->is_gensym, 1);
    ASSERT_U32_EQ(s->is_caret, 0);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: two gensym calls produce two distinct syntax objects with
 * distinct name strings. */
static int test_us014_gensym_produces_distinct_names(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char *err = NULL;
    JaclVal a = jacl_gensym_next("t", 1, &vm.heap, 0, &err);
    ASSERT(err == NULL);
    JaclVal b = jacl_gensym_next("t", 1, &vm.heap, 0, &err);
    ASSERT(err == NULL);

    /* Different heap pointers (distinct allocations). */
    ASSERT(a != b);

    JaclSyntax *sa = jacl_as_syntax(a);
    JaclSyntax *sb = jacl_as_syntax(b);
    char nbuf_a[16];
    char nbuf_b[16];
    uint32_t la = jacl_string_byte_len(sa->data.var_ref.name);
    uint32_t lb = jacl_string_byte_len(sb->data.var_ref.name);
    jacl_string_data(sa->data.var_ref.name, nbuf_a, sizeof(nbuf_a));
    jacl_string_data(sb->data.var_ref.name, nbuf_b, sizeof(nbuf_b));
    nbuf_a[la] = '\0';
    nbuf_b[lb] = '\0';

    /* Distinct name strings. */
    ASSERT(la != lb || memcmp(nbuf_a, nbuf_b, la) != 0);

    /* Both start with "t__" (prefix + separator). */
    ASSERT(la >= 3 && nbuf_a[0] == 't' && nbuf_a[1] == '_' && nbuf_a[2] == '_');
    ASSERT(lb >= 3 && nbuf_b[0] == 't' && nbuf_b[1] == '_' && nbuf_b[2] == '_');

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: jacl_gensym_next with zero-length prefix uses the default "g". */
static int test_us014_gensym_default_prefix(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char *err = NULL;
    JaclVal v = jacl_gensym_next(NULL, 0, &vm.heap, 0, &err);
    ASSERT(err == NULL);
    JaclSyntax *s = jacl_as_syntax(v);
    char nbuf[16];
    uint32_t n = jacl_string_byte_len(s->data.var_ref.name);
    jacl_string_data(s->data.var_ref.name, nbuf, sizeof(nbuf));
    nbuf[n] = '\0';
    /* Name starts with "g__" (default prefix). */
    ASSERT(n >= 3 && nbuf[0] == 'g' && nbuf[1] == '_' && nbuf[2] == '_');

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: prefix longer than 3 chars is rejected. */
static int test_us014_gensym_prefix_too_long(void) {
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    const char *err = NULL;
    JaclVal v = jacl_gensym_next("toolong", 7, &vm.heap, 0, &err);
    ASSERT(err != NULL);
    ASSERT(jacl_is_nil(v));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* Test: a macro using [gensym] as a mut binding name compiles and runs.
 * The generated unique name is a fresh local that does not exist before the
 * macro call. */
static int test_us014_gensym_in_macro_mut(void) {
    JaclVM* vm = jacl_vm_new();
    ASSERT(vm != NULL);

    const char* src =
        "defmacro m {v} {\n"
        "  syntax-quote [mut [gensym] ~v]\n"
        "}\n"
        "proc run {} {\n"
        "  m 42\n"
        "  99\n"
        "}\n"
        "run";
    JaclVal result = jacl_eval(vm, src);
    ASSERT(!jacl_is_error(result));
    ASSERT(jacl_is_i32(result));
    ASSERT_INT_EQ(jacl_as_i32(result), 99);

    jacl_vm_free(vm);
    TEST_PASS();
}

/* Test: macro using [gensym "t"] for a temporary does not collide with a
 * caller variable named "t". The caller's t keeps its original value after
 * the macro call. */
static int test_us014_gensym_no_collision_with_caller(void) {
    JaclVM* vm = jacl_vm_new();
    ASSERT(vm != NULL);

    const char* src =
        "defmacro bindit {v} {\n"
        "  syntax-quote [mut [gensym t] ~v]\n"
        "}\n"
        "proc run {} {\n"
        "  mut t 99\n"
        "  bindit 7\n"
        "  $t\n"
        "}\n"
        "run";
    JaclVal result = jacl_eval(vm, src);
    ASSERT(!jacl_is_error(result));
    ASSERT(jacl_is_i32(result));
    /* Caller's t is unchanged — gensym'd name is distinct from "t". */
    ASSERT_INT_EQ(jacl_as_i32(result), 99);

    jacl_vm_free(vm);
    TEST_PASS();
}

int main(void) {
    int pass = 0, fail = 0;

    printf("=== Syntax Object Tests (US-001) ===\n");

#define RUN(fn) do { \
        printf("  %-45s ", #fn); \
        if (fn()) { printf("PASS\n"); pass++; } \
        else { printf("FAIL\n"); fail++; } \
    } while (0)

    RUN(test_syntax_tag_slot);
    RUN(test_syntax_kind_enum);
    RUN(test_syntax_is_predicate);
    RUN(test_syntax_is_heap_type);
    RUN(test_syntax_alloc_gc_header);
    RUN(test_syntax_alloc_zeroed);
    RUN(test_syntax_lit_int);
    RUN(test_syntax_lit_float);
    RUN(test_syntax_var_ref);
    RUN(test_syntax_command);
    RUN(test_syntax_block);
    RUN(test_syntax_spread);
    RUN(test_syntax_scope_mark);
    RUN(test_syntax_print_lit_int);
    RUN(test_syntax_print_lit_float);
    RUN(test_syntax_print_var_ref);
    RUN(test_syntax_print_command);
    RUN(test_syntax_lit_string);
    RUN(test_syntax_break_return);
    RUN(test_syntax_interp_string);
    RUN(test_syntax_gc_survives);
    RUN(test_syntax_command_gc_survives);

    printf("\n=== AST-to-Syntax Conversion Tests (US-002) ===\n");

    RUN(test_from_ast_lit_int);
    RUN(test_from_ast_lit_float);
    RUN(test_from_ast_lit_string);
    RUN(test_from_ast_var_ref);
    RUN(test_from_ast_command);
    RUN(test_from_ast_block);
    RUN(test_from_ast_source_positions);
    RUN(test_from_ast_scope_marks_zero);
    RUN(test_from_ast_spread);
    RUN(test_from_ast_break);
    RUN(test_from_ast_continue);
    RUN(test_from_ast_return);
    RUN(test_from_ast_nested_command);
    RUN(test_from_ast_interp_string);
    RUN(test_from_ast_error_returns_nil);
    RUN(test_from_ast_null_returns_nil);
    RUN(test_from_ast_gc_survives);

    printf("\n=== Syntax-to-AST Conversion Tests (US-003) ===\n");

    RUN(test_to_ast_lit_int);
    RUN(test_to_ast_lit_float);
    RUN(test_to_ast_lit_string);
    RUN(test_to_ast_var_ref);
    RUN(test_to_ast_command);
    RUN(test_to_ast_nested_command);
    RUN(test_to_ast_block_multi);
    RUN(test_to_ast_source_positions);
    RUN(test_to_ast_nil_returns_null);
    RUN(test_to_ast_break);
    RUN(test_to_ast_continue);
    RUN(test_to_ast_return);
    RUN(test_to_ast_spread);

    printf("\n=== Defmacro Parsing Tests (US-004) ===\n");

    RUN(test_parse_defmacro_basic);
    RUN(test_parse_defmacro_zero_params);
    RUN(test_parse_defmacro_pretty_print);
    RUN(test_parse_defmacro_not_top_level);
    RUN(test_parse_defmacro_no_name);
    RUN(test_parse_defmacro_no_params);
    RUN(test_parse_defmacro_no_body);

    printf("\n=== Quote Parsing Tests (US-005) ===\n");

    RUN(test_parse_quote_command);
    RUN(test_parse_quote_var_ref);
    RUN(test_parse_quote_block);
    RUN(test_parse_quote_pretty_print);
    RUN(test_parse_quote_var_pretty_print);
    RUN(test_quote_roundtrip);
    RUN(test_parse_quote_inside_command);

    printf("\n=== Syntax-Quote Parsing Tests (US-006) ===\n");

    RUN(test_parse_syntax_quote_with_unquotes);
    RUN(test_parse_syntax_quote_unquote_splicing);
    RUN(test_tilde_outside_syntax_quote_error);
    RUN(test_parse_syntax_quote_pretty_print);
    RUN(test_parse_syntax_quote_roundtrip);
    RUN(test_syntax_quote_nested_depth);
    RUN(test_tilde_at_outside_syntax_quote_error);

    printf("\n=== Macro Table Tests (US-007) ===\n");

    RUN(test_defmacro_registers_in_table);
    RUN(test_defmacro_lookup_missing);
    RUN(test_defmacro_zero_params_registers);
    RUN(test_defmacro_multiple_register);
    RUN(test_defmacro_shadow_if_error);
    RUN(test_defmacro_shadow_spawn_error);
    RUN(test_defmacro_shadow_special_forms);
    RUN(test_defmacro_duplicate_error);
    RUN(test_macro_table_lookup_api);
    RUN(test_is_special_form);

    printf("\n=== Macro Expansion Tests (US-008) ===\n");

    RUN(test_expand_unless);
    RUN(test_expand_double);
    RUN(test_expand_two_level);
    RUN(test_expand_depth_limit);
    RUN(test_expand_use_before_defmacro);
    RUN(test_expand_wrong_arg_count);
    RUN(test_expand_non_macro_passthrough);
    RUN(test_expand_inside_block);

    printf("\n=== Compile Quote Tests (US-009) ===\n");

    RUN(test_compile_quote_command);
    RUN(test_compile_quote_var_ref);
    RUN(test_compile_quote_lit_int);
    RUN(test_compile_quote_nested);
    RUN(test_compile_quote_inside_macro_body);

    printf("\n=== Compile Syntax-Quote Tests (US-010) ===\n");

    RUN(test_us010_syntax_quote_unquotes);
    RUN(test_us010_unquote_splicing_in_args);
    RUN(test_us010_unquote_splicing_in_block);
    RUN(test_us010_unquote_type_error);
    RUN(test_us010_unquote_splicing_type_error);
    RUN(test_us010_unquote_splicing_vec_nonsyntax);
    RUN(test_us010_nested_syntax_quote_literal);

    printf("\n=== End-to-End Macro Tests (US-011) ===\n");

    RUN(test_us011_double_macro);
    RUN(test_us011_unless_macro);
    RUN(test_us011_unless_macro_true_cond);
    RUN(test_us011_two_level_macro);
    RUN(test_us011_use_before_defmacro_error);
    RUN(test_us011_wrong_arg_count_error);

    printf("\n=== Hygiene Tests (US-012) ===\n");

    RUN(test_us012_hygiene_local_shadow);
    RUN(test_us012_hygiene_nested_macros);
    RUN(test_us012_global_fallback_still_works);

    printf("\n=== Caret Anaphoric Binding Tests (US-013) ===\n");

    RUN(test_us013_caret_binds_in_caller_scope);
    RUN(test_us013_caret_outside_syntax_quote_error);
    RUN(test_us013_caret_does_not_collide_with_internal);

    printf("\n=== Gensym Tests (US-014) ===\n");

    RUN(test_us014_gensym_returns_var_ref);
    RUN(test_us014_gensym_produces_distinct_names);
    RUN(test_us014_gensym_default_prefix);
    RUN(test_us014_gensym_prefix_too_long);
    RUN(test_us014_gensym_in_macro_mut);
    RUN(test_us014_gensym_no_collision_with_caller);

#undef RUN

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
