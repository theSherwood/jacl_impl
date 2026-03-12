# JACL Simplification Report

Assessment of the JACL language design and implementation, with a focus on
opportunities to reduce complexity without losing capability.

**Codebase at time of writing:** ~19,400 lines of C across 14 source files.

---

## 1. compiler.c is doing too many jobs (6,129 lines)

The compiler is the largest module by far — nearly a third of the entire
codebase. It handles type checking, CPS transformation, escape analysis, arity
checking, struct layout, module resolution, suspension analysis, and bytecode
emission, all interleaved in a single file.

This isn't just an organizational concern. Interleaving makes each subsystem
harder to reason about independently, and it creates copy-paste duplication
where a cleaner separation would enable shared code.

### 1a. CPS argument extraction is duplicated

`compiler__compile_cps_extract_args` (lines 2245–2339) and
`compiler__compile_cps_extract_def_value` (lines 2346–2442) do nearly
identical work: scan arguments for suspension points, generate synthetic
`[def __aN ...]` AST nodes for each suspending argument, replace originals
with `$__aN` var refs, and recurse through CPS compilation.

The two functions differ only in where the modified node gets inserted into
the new statement list — one modifies a command's arguments, the other
modifies a def's value expression. The core loop (lines 2269–2322 vs
2369–2416) is structurally identical: same temp name generation, same
`ast_alloc` sequence, same var ref / name node / def head / def command
construction.

**Simplification:** Extract a shared helper that takes an `AstNode**` array
of arguments and returns the synthetic def statements + modified argument
array. Both callers would become thin wrappers.

### 1b. Global arity lookup is four separate functions

Lines 1225–1270 define four functions that each walk to the root compiler
and linear-scan the same `global_arities` array:

```c
compiler__resolve_global_arity(c, name)  → int16_t   (lines 1225–1235)
compiler__resolve_global_info(c, name)   → bool      (lines 1237–1248)
compiler__resolve_global_type(c, name)   → JaclType  (lines 1250–1259)
compiler__find_global_arity(c, name)     → GlobalArity*  (lines 1261–1270)
```

Each repeats the root-walk and the linear scan. The `GlobalArity` struct
already contains all the fields these functions extract individually.

**Simplification:** Replace all four with a single
`compiler__find_global(c, name) → GlobalArity*` and have callers access
fields directly. This is what `compiler__find_global_arity` already does —
the other three are redundant wrappers.

### 1c. Non-suspending callback validation is copy-pasted three times

The `transform`, `each`, and `filter` builtins (lines 4103–4220) each
contain identical 30-line blocks that:

1. Check if the callback is a known suspending proc (local slot or global)
2. Check if the callback block contains suspension points
3. Save/restore `in_non_suspending_callback` around compilation

The only difference between the three copies is the builtin name in the
error message string.

**Simplification:** Extract a
`compiler__compile_nonsuspending_callback(c, name, args, line, col, opcode)`
helper. Each builtin becomes a 5-line arity check + one call.

---

## 2. Struct field marshaling is duplicated across files

Reading and writing struct fields requires a type switch that dispatches on
the field's `JaclType` to determine the correct `memcpy` size and
boxing/unboxing logic. This switch appears **six times** across two files:

| Location | Function/Opcode | Lines | Direction |
|----------|----------------|-------|-----------|
| vm.c | `OP_STRUCT_GET` | 3941–3983 | read, mixed boxing |
| vm.c | `OP_STRUCT_SET` | 4008–4049 | write, mixed unboxing |
| vm.c | `OP_STRUCT_GET_DYN` | 4092–4101 | read, always boxed |
| vm.c | `OP_STRUCT_SET_DYN` | 4144–4153 | write, from boxed |
| embed.c | `embed__struct_read_field` | 838–872 | read, always boxed |
| embed.c | `embed__struct_write_field` | 876–918 | write, from boxed |

The embed.c versions already do the right thing as standalone functions.
The vm.c opcode handlers inline the same logic rather than calling them.

**Simplification:** Define `struct_field_read(data, offset, type, heap)`
and `struct_field_write(data, offset, type, val)` once (in value.c or a
shared header), and call them from all six sites. The static vs dynamic
opcode variants differ only in how they resolve the field offset and type —
the actual marshaling is identical.

---

## 3. Type conversion opcodes are a wall of repetition

`OP_TO_I32` through `OP_TO_F64` (vm.c lines 3693–3869) comprise six opcode
handlers, each containing a switch over source types with a nested
`TYPE_DYN` case that itself contains an if-else chain over all numeric
types. That's 177 lines of conversion logic where the structure is identical
and only the cast expression differs.

Each handler follows the pattern:

```c
case OP_TO_Xxx: {
  uint8_t src_type = vm__read_byte(vm);
  JaclVal val;
  result = vm__pop(vm, &val);
  Xxx target;
  switch (src_type) {
    case TYPE_I32: { target = (Xxx)jacl_as_i32(val); ... }
    case TYPE_U32: { target = (Xxx)jacl_as_u32(val); ... }
    case TYPE_I64: { target = (Xxx)(int64_t)val;     ... }
    // ... same for all 6 source types
    case TYPE_DYN: {
      if (jacl_is_i32(val)) { target = (Xxx)jacl_as_i32(val); }
      else if (jacl_is_u32(val)) { ... }
      // ... same chain again
    }
  }
}
```

**Simplification:** Extract the value to a `double` or `int64_t`
intermediate (depending on source type) in one shared function, then cast
to the target type. Alternatively, a table-driven approach with function
pointers for the `extract` and `box` operations would collapse 177 lines
to ~40.

---

## 4. Typed arithmetic opcodes: correct design, mechanical implementation

The 40 typed opcodes (OP_ADD_I64, OP_SUB_I64, ..., OP_GE_F64, etc.) are
justified — they avoid tag checks on hot paths when the compiler knows both
operands' types. But the handlers are structurally identical within each
type family.

Every i64 binary op (lines 3338–3462) follows:

```c
case OP_XXX_I64: {
  JaclVal raw_b, raw_a;
  result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
  result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
  int64_t a = (int64_t)raw_a;
  int64_t b = (int64_t)raw_b;
  result = vm__push(vm, (uint64_t)(a OP b));  // only this line differs
  if (result != VM_OK) return result;
  break;
}
```

The f64 family (lines 3467–3545) is the same with `memcpy` for
double↔uint64_t conversion.

**Simplification:** These are good candidates for macros:

```c
#define VM__I64_BINOP(op) { \
  JaclVal raw_b, raw_a; \
  result = vm__pop(vm, &raw_b); if (result != VM_OK) return result; \
  result = vm__pop(vm, &raw_a); if (result != VM_OK) return result; \
  result = vm__push(vm, (uint64_t)((int64_t)raw_a op (int64_t)raw_b)); \
  if (result != VM_OK) return result; \
  break; \
}
```

This would replace ~125 lines of i64 handlers with ~15 lines of macro
invocations, and similarly for f64. The div/mod variants (which need a
zero check) can use a slightly extended macro.

---

## 5. Unused libraries in lib/

Five libraries in `lib/` are not included in the unity build (`src/jacl.c`)
and have no `#include` references from any source file:

| Library | Purpose | Status |
|---------|---------|--------|
| `bignum/` | Arbitrary precision integers | Not included, not referenced |
| `regex/` | Thompson NFA regex engine | Not included, not referenced |
| `segment_array/` | Segmented array data structure | Not included, not referenced |
| `sum_tree/` | B-tree and rope implementation | Not included, not referenced |
| `rc/` | Reference counting | Superseded by GC |

The value system defines `jacl_bignum_ptr` and `jacl_is_bignum` (value.c
lines 159, 188), but these are tag-level placeholders — the bignum library
itself is never used.

**Simplification:** Remove these from the repository. They can live in a
separate repo or branch if needed later. Their presence suggests future
features that haven't been built and adds to the mental model of "what is
this project" without contributing to it.

---

## 6. Phase 2 syntax (M16) should be reconsidered

The roadmap plans to add infix operators with precedence (`$x + $y * $z`)
alongside the existing bracket syntax. This would give JACL three distinct
syntactic modes:

1. **Bracket commands:** `[+ $x [* $y $z]]`
2. **Line-based sugar:** `print "hello"`
3. **Infix operators:** `$x + $y * $z`

Two is already a lot. Three risks making the language feel inconsistent.
The bracket syntax *is* the language's identity — it's what makes JACL
recognizable as a command-oriented lisp rather than yet another scripting
language. Adding infix doesn't make JACL better at being JACL; it makes
JACL a weaker version of every language that already has infix.

The implementation cost is also significant: new parser rules, operator
precedence tables, associativity handling, and desugaring in the compiler.
This complexity would land squarely in compiler.c, which is already the
largest and most entangled module.

The line-based sugar from M6 already covers the main ergonomic gap (not
needing brackets for top-level commands). Infix adds a second layer of
sugar on top of that with diminishing returns.

**Recommendation:** Cut M16 from the roadmap, or at minimum defer it
indefinitely. The language's identity is sharper without it.

---

## 7. Generational GC: premature infrastructure

The GC design documents describe a generational collection strategy with
sticky mark bits, remembered sets, promotion thresholds, and minor/major
GC scheduling. Some of this infrastructure is already in the code:

- `GCHeader` has a `gen` bit and `survive_count` field (gc.c lines 38–42)
- Remembered set data structures are defined (gc.c lines 656–732)
- `gc_remembered_set_barrier()` is called during mutations (gc.c line 828)
- `gc_collect_minor()` exists and is invoked from vm.c (gc_collect.c line 686)

The current epoch-based tracing GC works correctly. For an embeddable
scripting language — where programs are typically short-lived and
allocations are modest — generational collection is almost certainly
premature. It adds write barrier complexity, two GC code paths (minor vs
major), and promotion logic, all for a theoretical throughput improvement
that hasn't been measured.

**Recommendation:** Don't expand the generational GC infrastructure further.
If profiling of real workloads ever shows that full-collection pauses are a
problem, revisit then. The current single-generation tracing GC is a solid
foundation. Removing the generational scaffolding isn't urgent (it's
implemented and working), but don't invest more complexity budget here.

---

## 8. embed.c `_val` wrapper layer

Lines 429–502 of embed.c define `_val`-suffixed public API wrappers that
are one-line delegations to internal functions:

```c
JaclVal jacl_nil_val(void)          { return JACL_NIL; }
JaclVal jacl_bool_val(bool b)       { return jacl_bool(b); }
int32_t jacl_as_i32_val(JaclVal v)  { return jacl_as_i32(v); }
// ... 14 more of these
```

Some of these add real value — `jacl_i64_val` takes a `JaclVM*` to access
the heap, which the internal `jacl_i64` doesn't. But most are pure
indirection.

This isn't a high-priority simplification (the wrappers exist for ABI
stability and WASM compatibility), but if the `_val` convention is needed,
consider generating these with a macro to eliminate the boilerplate:

```c
#define JACL_WRAP_EXTRACTOR(target_type, suffix, internal_fn) \
  JACL_EMBED_FN target_type jacl_as_##suffix##_val(JaclVal v) { \
    return internal_fn(v); \
  }
```

---

## 9. Higher-order builtin compilation pattern

The `each`, `transform`, and `filter` builtins (compiler.c lines 4103–4220)
share an almost-identical compilation pattern:

1. Arity check (must be exactly 2 args)
2. If callback is a var ref to a known suspending proc, error
3. If callback block contains suspension points, error
4. Compile collection argument
5. Save/set/restore `in_non_suspending_callback`
6. Compile callback
7. Emit opcode

The only differences are the builtin name (for error messages) and the
final opcode. This 120 lines of code could be ~40 with a shared helper:

```c
static void compiler__compile_hof_builtin(
    Compiler* c, const char* name, uint32_t name_len,
    AstNode** args, uint32_t argc,
    uint8_t opcode, uint32_t line, uint32_t col);
```

---

## 10. Summary: prioritized simplification targets

Ranked by impact (reduction in complexity or duplication) relative to effort:

| Priority | Target | Est. lines saved | Risk |
|----------|--------|-------------------|------|
| 1 | Extract shared struct field marshaling | ~200 | Low — pure refactor |
| 2 | Macro-ify typed arithmetic opcodes | ~100 | Low — mechanical |
| 3 | Merge CPS extraction functions | ~80 | Low — structural |
| 4 | Consolidate global arity lookups | ~40 | Low — pure refactor |
| 5 | Extract HOF builtin compilation helper | ~80 | Low — pure refactor |
| 6 | Table-drive type conversion opcodes | ~120 | Medium — behavioral |
| 7 | Remove unused libraries | ~0 (code), high (clarity) | None |
| 8 | Cut M16 from roadmap | Future savings | Design decision |
| 9 | Freeze generational GC scope | Future savings | Design decision |

Items 1–6 are implementation refactors that reduce duplication without
changing behavior. Items 7–9 are design simplifications that sharpen the
project's scope. None of them remove capability.
