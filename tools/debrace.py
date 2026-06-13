#!/usr/bin/env python3
"""Corpus debrace: {…} -> […] in .jacl sources (the []-flip migration).

Both spellings parse to the same [do …] AST now, so this is a pure
spelling rewrite. Skips:
  - comments (# to EOL)
  - strings ("…" with \\ escapes) and triple strings (\"\"\"…\"\"\")
  - inline struct types (`struct{…}` / `struct {…}` with no name — the
    inline-type parser requires the brace spelling)
Braces are converted pairwise via a stack so a skipped opener keeps its
matching closer.
"""
import sys, re

def debrace(text):
    out = []
    i = 0
    n = len(text)
    stack = []  # True = converted opener
    while i < n:
        c = text[i]
        # comment
        if c == '#':
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(text[i:j]); i = j; continue
        # triple string
        if text.startswith('"""', i):
            j = text.find('"""', i + 3)
            j = n if j < 0 else j + 3
            out.append(text[i:j]); i = j; continue
        # string
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            j = min(j + 1, n)
            out.append(text[i:j]); i = j; continue
        if c == '{':
            # inline struct type: `struct` directly before (optional spaces)
            k = len(out) - 1
            tail = ''.join(out[max(0, k - 1):])  # cheap lookback
            m = re.search(r'struct[ \t]*$', ''.join(out)[-16:])
            if m:
                stack.append(False)
                out.append('{')
            else:
                stack.append(True)
                out.append('[')
            i += 1; continue
        if c == '}':
            conv = stack.pop() if stack else True
            out.append(']' if conv else '}')
            i += 1; continue
        out.append(c); i += 1
    return ''.join(out)

if __name__ == '__main__':
    changed = 0
    for path in sys.argv[1:]:
        src = open(path).read()
        dst = debrace(src)
        if dst != src:
            open(path, 'w').write(dst)
            changed += 1
    print(f"debraced {changed}/{len(sys.argv)-1} files")
