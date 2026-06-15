#!/usr/bin/env python3
"""Migrate binding operators to prefix forms in .jacl sources:
   LHS = V   -> def LHS V       (also typed:  i64 x = 5 -> def i64 x 5)
   NAME = proc {p} {b} -> proc NAME {p} {b}   (named proc; multi-line-safe)
   LHS : V   -> mut LHS V
   LHS :: V  -> set LHS V
(ctx is handled separately — left alone here.) Finds the binding operator at
bracket-depth 0, skipping #-comments and "…" strings, excluding ==/<=/>=/!=.
Behavior-preserving (the ops are def/mut/set sugar); suite verifies.
"""
import sys

def split_code_comment(line):
    i, n = 0, len(line); instr = False
    while i < n:
        c = line[i]
        if c == '"' and not (i and line[i-1] == '\\'): instr = not instr
        elif c == '#' and not instr: return line[:i], line[i:]
        i += 1
    return line, ''

def find_binding_op(code):
    i, n = 0, len(code); depth = 0; instr = False
    while i < n:
        c = code[i]
        if c == '"' and not (i and code[i-1] == '\\'): instr = not instr; i += 1; continue
        if instr: i += 1; continue
        if c in '[{': depth += 1; i += 1; continue
        if c in ']}': depth -= 1; i += 1; continue
        if depth == 0 and c == ' ':
            if code[i+1:i+4] == ':: ': return (i, 4, 'set')
            if code[i+1:i+3] == ': ' and code[i+1:i+4] != ':: ': return (i, 3, 'mut')
            if code[i+1:i+3] == '= ': return (i, 3, 'def')
        i += 1
    return None

def has_depth0_space(s):
    depth = 0; instr = False
    for i, c in enumerate(s):
        if c == '"' and not (i and s[i-1] == '\\'): instr = not instr; continue
        if instr: continue
        if c in '[{': depth += 1
        elif c in ']}': depth -= 1
        elif c == ' ' and depth == 0: return True
    return False

def migrate_line(line):
    code, comment = split_code_comment(line)
    code_r = code.rstrip()
    trail = code[len(code_r):]
    stripped = code_r.lstrip()
    if stripped.startswith('ctx ') or stripped.startswith('ctx\t'):
        return line                       # ctx handled separately
    indent = code_r[:len(code_r) - len(stripped)]
    op = find_binding_op(code_r)
    if not op: return line
    idx, oplen, kw = op
    lhs = code_r[:idx].strip()
    val = code_r[idx + oplen:].strip()
    is_name = lhs and ' ' not in lhs and lhs[0] not in '[{'
    if kw == 'def' and is_name and (val.startswith('proc ') or val.startswith('proc{')):
        rest = val[4:].lstrip()
        new = f"{indent}proc {lhs} {rest}"
    else:
        if has_depth0_space(val):
            val = f"[{val}]"
        new = f"{indent}{kw} {lhs} {val}"
    return new + trail + comment

if __name__ == '__main__':
    changed = 0
    for path in sys.argv[1:]:
        src = open(path).read()
        out = '\n'.join(migrate_line(l) for l in src.split('\n'))
        if out != src:
            open(path, 'w').write(out); changed += 1
    print(f"migrated {changed} files")
