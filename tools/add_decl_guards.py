#!/usr/bin/env python3
"""
Add JACL_DECL_ONLY guards around function bodies in .c files.

For each file:
1. Find each non-inline static function body (brace-matched)
2. Group consecutive functions into blocks (separated by type definitions)
3. Wrap each block in #ifndef JACL_DECL_ONLY / #endif
4. Add forward declarations for all guarded functions at the top
5. Wrap file-scope static variables with #ifdef JACL_DECL_ONLY extern / #else def / #endif

This allows:
- Normal unity build: everything compiles as before
- Separate compilation: -DJACL_DECL_ONLY skips function bodies, types remain visible
"""

import re
import sys
import os


def count_braces_in_line(line):
    """Count net braces in a line, skipping string/char literals and comments."""
    count = 0
    i = 0
    n = len(line)
    while i < n:
        ch = line[i]
        # Skip single-line comment
        if ch == '/' and i + 1 < n and line[i + 1] == '/':
            break
        # Skip block comment start (/* ... */)
        if ch == '/' and i + 1 < n and line[i + 1] == '*':
            i += 2
            while i < n - 1:
                if line[i] == '*' and line[i + 1] == '/':
                    i += 2
                    break
                i += 1
            continue
        # Skip string literal
        if ch == '"':
            i += 1
            while i < n:
                if line[i] == '\\':
                    i += 2
                    continue
                if line[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        # Skip character literal
        if ch == "'":
            i += 1
            while i < n:
                if line[i] == '\\':
                    i += 2
                    continue
                if line[i] == "'":
                    i += 1
                    break
                i += 1
            continue
        if ch == '{':
            count += 1
        elif ch == '}':
            count -= 1
        i += 1
    return count


def find_matching_brace(lines, start_line):
    """Find the line containing the closing brace matching the first { from start_line."""
    brace_count = 0
    started = False
    for i in range(start_line, len(lines)):
        brace_count += count_braces_in_line(lines[i])
        if brace_count > 0:
            started = True
        if started and brace_count <= 0:
            return i
    return len(lines) - 1


def is_func_def_start(line, lines, i):
    """Check if line i starts a non-inline static function definition."""
    if line.startswith('static inline'):
        return False
    if line.startswith('JACL_EMBED_FN'):
        # Check it's a definition (has {) not a declaration
        for j in range(i, min(i + 5, len(lines))):
            if '{' in lines[j]:
                return True
            if ');' in lines[j]:
                return False
        return False
    if not line.startswith('static '):
        return False
    if 'JACL_THREAD_LOCAL' in line:
        return False
    if '(' not in line:
        return False
    # Check it's a function definition (has {), not a forward declaration (has ;)
    for j in range(i, min(i + 5, len(lines))):
        if '{' in lines[j]:
            return True
        if ');' in lines[j] or lines[j].rstrip().endswith(');'):
            return False
    return False


def extract_signature(lines, start):
    """Extract function signature as a forward declaration string."""
    sig_parts = []
    for i in range(start, min(start + 10, len(lines))):
        line = lines[i]
        if '{' in line:
            sig_parts.append(line[:line.index('{')].rstrip())
            break
        sig_parts.append(line.rstrip())
    sig = ' '.join(sig_parts)
    sig = re.sub(r'\s+', ' ', sig).strip()
    if not sig.endswith(')'):
        sig = sig.rstrip()
    return sig + ';'


def is_type_or_important_line(line):
    """Check if a line contains a type definition or other non-function code
    that should remain visible in DECL_ONLY mode."""
    stripped = line.strip()
    if not stripped:
        return False
    if stripped.startswith('typedef'):
        return True
    if re.match(r'^(struct|enum|union)\s+', stripped):
        return True
    if stripped.startswith('#define') or stripped.startswith('#undef'):
        return True
    if stripped.startswith('#include'):
        return True
    if stripped.startswith('#if') or stripped.startswith('#else') or stripped.startswith('#endif'):
        return True
    if stripped.startswith('static inline'):
        return True
    # File-scope static variables need extern treatment in DECL_ONLY mode,
    # so they must be outside guarded blocks
    if re.match(r'^static\s+JACL_THREAD_LOCAL\s+', line):
        return True
    if re.match(r'^static\s+.*\(\*\w+\)', line) and '{' not in line and ';' in line:
        return True
    if re.match(r'^static\s+', line) and 'inline' not in line and '(' not in line and ';' in line:
        return True
    if re.match(r'^static\s+\w+\s+\w+\s*\[', line) and ';' in line and 'inline' not in line:
        return True
    return False


def find_closing_endif(lines):
    """Find the final #endif that closes the include guard."""
    for i in range(len(lines) - 1, -1, -1):
        stripped = lines[i].strip()
        if stripped.startswith('#endif'):
            return i
    return -1


def find_func_blocks(lines):
    """Find all non-inline static function bodies and group them into blocks.
    Returns list of (block_start, block_end) tuples."""
    func_defs = []  # (start_line, end_line)

    i = 0
    while i < len(lines):
        if is_func_def_start(lines[i], lines, i):
            end = find_matching_brace(lines, i)
            func_defs.append((i, end))
            i = end + 1
        else:
            i += 1

    if not func_defs:
        return []

    # Merge consecutive functions into blocks.
    # Break into separate blocks when there's a type definition or
    # important non-function code between two functions.
    blocks = []
    block_start = func_defs[0][0]
    block_end = func_defs[0][1]

    for k in range(1, len(func_defs)):
        fs, fe = func_defs[k]
        # Check if there's important code between this function and the previous one
        has_important = False
        for j in range(block_end + 1, fs):
            if is_type_or_important_line(lines[j]):
                has_important = True
                break
        if has_important:
            blocks.append((block_start, block_end))
            block_start = fs
        block_end = fe

    blocks.append((block_start, block_end))
    return blocks


def find_static_vars(lines):
    """Find file-scope static variables (not inside functions)."""
    vars_found = []
    in_func = False
    brace_depth = 0

    for i, line in enumerate(lines):
        # Track brace depth to skip function bodies
        if not in_func:
            if is_func_def_start(line, lines, i):
                in_func = True
                brace_depth = 0
        if in_func:
            brace_depth += line.count('{') - line.count('}')
            if brace_depth <= 0 and '{' in ''.join(lines[max(0,i-10):i+1]):
                in_func = False
            continue

        # Thread-local variable
        if re.match(r'^static\s+JACL_THREAD_LOCAL\s+', line) and ';' in line:
            vars_found.append(i)
            continue
        # Static function pointer variable
        if re.match(r'^static\s+.*\(\*\w+\)', line) and '{' not in line and ';' in line:
            vars_found.append(i)
            continue
        # Plain static variable (not function, not inline)
        if re.match(r'^static\s+', line) and 'inline' not in line and '(' not in line and ';' in line:
            vars_found.append(i)
            continue
        # Static array variable
        if re.match(r'^static\s+\w+\s+\w+\s*\[', line) and ';' in line and 'inline' not in line:
            vars_found.append(i)
            continue

    return vars_found


def make_extern_decl(line):
    """Convert a static variable definition to an extern declaration."""
    extern_line = re.sub(r'^static\s+', 'extern ', line)
    if '=' in extern_line:
        extern_line = re.sub(r'\s*=\s*[^;]*;', ';', extern_line)
    return extern_line


def find_guard_def_line(lines):
    """Find the #define FOO_C line (include guard definition)."""
    for i, line in enumerate(lines):
        if re.match(r'^#define\s+\w+_C\s*$', line.strip()):
            return i
    return -1


def transform_file(filepath, dry_run=False):
    """Transform a .c file to support JACL_DECL_ONLY mode."""
    with open(filepath) as f:
        lines = f.readlines()

    basename = os.path.basename(filepath)

    # Find function blocks
    func_blocks = find_func_blocks(lines)
    if not func_blocks:
        print(f"  {basename}: no function blocks found, skipping")
        return False

    # Extract forward declarations for all functions in blocks
    fwd_decls = []
    all_func_defs = []
    i = 0
    while i < len(lines):
        if is_func_def_start(lines[i], lines, i):
            sig = extract_signature(lines, i)
            fwd_decls.append(sig)
            end = find_matching_brace(lines, i)
            all_func_defs.append((i, end))
            i = end + 1
        else:
            i += 1

    # Find static variables
    static_var_lines = find_static_vars(lines)

    print(f"  {basename}: {len(func_blocks)} block(s), {len(all_func_defs)} function(s), {len(static_var_lines)} var(s)")
    for bs, be in func_blocks:
        print(f"    block: lines {bs+1}-{be+1}")

    if dry_run:
        if fwd_decls:
            print(f"    forward declarations:")
            for d in fwd_decls[:5]:
                print(f"      {d[:80]}")
            if len(fwd_decls) > 5:
                print(f"      ... and {len(fwd_decls)-5} more")
        return True

    # Apply transforms bottom-to-top to preserve line numbers

    # 1. Wrap function blocks in #ifndef JACL_DECL_ONLY / #endif
    for block_start, block_end in reversed(func_blocks):
        lines.insert(block_end + 1, '#endif /* !JACL_DECL_ONLY */\n')
        lines.insert(block_start, '#ifndef JACL_DECL_ONLY\n')

    # 2. Handle static variables: wrap with #ifdef JACL_DECL_ONLY extern / #else / #endif
    # After inserting guards, line numbers have shifted. Process bottom-to-top.
    # Calculate offset for each variable line based on how many guard pairs were inserted before it
    for var_line in reversed(static_var_lines):
        # Count how many block guards were inserted before this line
        offset = 0
        for block_start, block_end in func_blocks:
            if block_start <= var_line:
                offset += 1  # #ifndef line inserted before block_start
            if block_end < var_line:
                offset += 1  # #endif line inserted after block_end
        actual_line = var_line + offset
        original_line = lines[actual_line]
        extern_decl = make_extern_decl(original_line)
        lines[actual_line] = (
            '#ifdef JACL_DECL_ONLY\n'
            + extern_decl
            + '#else\n'
            + original_line
            + '#endif\n'
        )

    # 3. Add forward declarations just BEFORE each guarded block.
    #    This ensures declarations appear after all types defined before the block
    #    but before any code that might reference the functions.
    #    Build a mapping: block index -> list of forward declaration strings
    block_fwd_decls = {}
    func_idx = 0
    for block_idx, (bs, be) in enumerate(func_blocks):
        block_fwd_decls[block_idx] = []
        while func_idx < len(all_func_defs):
            fs, fe = all_func_defs[func_idx]
            if fs >= bs and fe <= be:
                sig = fwd_decls[func_idx]
                clean_sig = re.sub(r'^static\s+', '', sig)
                clean_sig = re.sub(r'^JACL_EMBED_FN\s+', '', clean_sig)
                block_fwd_decls[block_idx].append(clean_sig)
                func_idx += 1
            else:
                break

    # Insert forward declarations before each block (bottom-to-top)
    # At this point, lines already have #ifndef/#endif guards inserted.
    # Each block added 2 lines (one before, one after).
    # Calculate offsets.
    for block_idx in range(len(func_blocks) - 1, -1, -1):
        decls = block_fwd_decls.get(block_idx, [])
        if not decls:
            continue
        # Find the #ifndef JACL_DECL_ONLY line for this block
        # It was inserted at the original block_start position,
        # shifted by prior insertions (2 lines per block before this one,
        # plus variable expansions)
        # The simplest approach: search for the Nth #ifndef JACL_DECL_ONLY
        count = 0
        target_line = -1
        for li, l in enumerate(lines):
            if l.strip() == '#ifndef JACL_DECL_ONLY':
                if count == block_idx:
                    target_line = li
                    break
                count += 1
        if target_line < 0:
            continue
        fwd_block = ['#ifdef JACL_DECL_ONLY\n']
        for d in decls:
            fwd_block.append(d + '\n')
        fwd_block.append('#endif\n')
        lines = lines[:target_line] + fwd_block + lines[target_line:]

    with open(filepath, 'w') as f:
        f.writelines(lines)

    new_count = len(lines)
    print(f"    wrote {new_count} lines")
    return True


def main():
    dry_run = '--dry-run' in sys.argv

    src_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'src')

    # value.c is all static inline — no implementation to guard
    files = [
        'gc.c', 'string.c', 'lexer.c', 'ast.c', 'parser.c',
        'bytecode.c', 'collections.c', 'compiler.c', 'vm.c',
        'gc_collect.c', 'runtime.c', 'embed.c'
    ]

    print(f"Adding JACL_DECL_ONLY guards ({'dry run' if dry_run else 'live'}):")
    for fname in files:
        fpath = os.path.join(src_dir, fname)
        if os.path.isfile(fpath):
            transform_file(fpath, dry_run)
        else:
            print(f"  {fname}: not found")


if __name__ == '__main__':
    main()
