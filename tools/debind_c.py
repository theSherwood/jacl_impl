#!/usr/bin/env python3
"""Migrate binding operators inside embedded-JACL C string literals.

C-lexer-aware: only touches content INSIDE C double-quoted strings (never C
code, char literals, or comments). Within a C string the content is JACL with
\\n line separators and \\"…\\" JACL strings; for each JACL line:
   LHS = V   -> def LHS V        NAME = proc {p} {b} -> proc NAME {p} {b}
   LHS : V   -> mut LHS V        LHS :: V -> set LHS V
(ctx lines are left alone — handled separately.) Behavior-preserving while the
operators still work, so the suite verifies. Excludes assert-test files passed
to it by the caller (don't pass test_parser.c / test_typer.c).
"""
import sys

def _find_op(code):
    """Depth-0 binding op in a JACL segment; \\" delimits JACL strings."""
    i, n = 0, len(code); depth = 0; instr = False
    while i < n:
        if code[i] == '\\' and i + 1 < n:
            if code[i+1] == '"': instr = not instr
            i += 2; continue
        c = code[i]
        if instr: i += 1; continue
        if c in '[{': depth += 1; i += 1; continue
        if c in ']}': depth -= 1; i += 1; continue
        if depth == 0 and c == ' ':
            if code[i+1:i+4] == ':: ': return (i, 4, 'set')
            if code[i+1:i+3] == ': ' and code[i+1:i+4] != ':: ': return (i, 3, 'mut')
            if code[i+1:i+3] == '= ': return (i, 3, 'def')
        i += 1
    return None

def _has_d0_space(s):
    i, n = 0, len(s); depth = 0; instr = False
    while i < n:
        if s[i] == '\\' and i + 1 < n:
            if s[i+1] == '"': instr = not instr
            i += 2; continue
        c = s[i]
        if not instr:
            if c in '[{': depth += 1
            elif c in ']}': depth -= 1
            elif c == ' ' and depth == 0: return True
        i += 1
    return False

def migrate_segment(seg):
    s = seg.lstrip(' \t')
    indent = seg[:len(seg) - len(s)]
    if s.startswith('ctx ') or s.startswith('ctx\\t'):
        return seg
    op = _find_op(seg)
    if not op: return seg
    idx, oplen, kw = op
    lhs = seg[:idx].strip()
    val = seg[idx + oplen:].strip()
    if not lhs or not val: return seg
    is_name = ' ' not in lhs and lhs[0] not in '[{'
    if kw == 'def' and is_name and (val.startswith('proc ') or val.startswith('proc{')):
        return f"{indent}proc {lhs} {val[4:].lstrip()}"
    if _has_d0_space(val):
        val = f"[{val}]"
    return f"{indent}{kw} {lhs} {val}"

def _split_d0_semi(s):
    """Split on depth-0 ';' (respecting []/{} and \\" strings); keep the ';'."""
    parts = []; depth = 0; instr = False; last = 0; i = 0; n = len(s)
    while i < n:
        if s[i] == '\\' and i + 1 < n:
            if s[i+1] == '"': instr = not instr
            i += 2; continue
        c = s[i]
        if not instr:
            if c in '[{': depth += 1
            elif c in ']}': depth -= 1
            elif c == ';' and depth == 0:
                parts.append(s[last:i]); parts.append(';'); last = i + 1
        i += 1
    parts.append(s[last:])
    return parts

def migrate_content(content):
    out_lines = []
    for line in content.split('\\n'):
        pieces = _split_d0_semi(line)
        out_lines.append(''.join(p if p == ';' else migrate_segment(p) for p in pieces))
    return '\\n'.join(out_lines)

def debind_c(text):
    out = []; i, n = 0, len(text); state = 'CODE'; changed = [0]
    while i < n:
        c = text[i]
        if state == 'CODE':
            if c == '"':
                # collect string content until unescaped closing quote
                j = i + 1
                while j < n:
                    if text[j] == '\\': j += 2; continue
                    if text[j] == '"': break
                    j += 1
                content = text[i+1:j]
                new = migrate_content(content)
                if new != content: changed[0] += 1
                out.append('"' + new + '"')
                i = j + 1; continue
            if c == "'":
                out.append(c); i += 1
                while i < n:
                    if text[i] == '\\': out.append(text[i:i+2]); i += 2; continue
                    out.append(text[i])
                    if text[i] == "'": i += 1; break
                    i += 1
                continue
            if c == '/' and i+1 < n and text[i+1] == '/':
                while i < n and text[i] != '\n': out.append(text[i]); i += 1
                continue
            if c == '/' and i+1 < n and text[i+1] == '*':
                out.append(text[i:i+2]); i += 2
                while i < n and not (text[i] == '*' and i+1 < n and text[i+1] == '/'):
                    out.append(text[i]); i += 1
                continue
            out.append(c); i += 1; continue
    return ''.join(out), changed[0]

if __name__ == '__main__':
    files = [a for a in sys.argv[1:]]
    total = 0
    for path in files:
        src = open(path).read(); dst, ch = debind_c(src)
        if ch:
            total += ch; open(path, 'w').write(dst); print(f"{path}: {ch} strings")
    print(f"total: {total} in {len(files)} files")
