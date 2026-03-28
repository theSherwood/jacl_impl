#!/usr/bin/env python3
"""
Transform JACL .c files to support JACL_HEADER_ONLY mode.

For each file:
1. Remove 'static' from non-inline function definitions and forward declarations
2. Add #ifndef JACL_HEADER_ONLY / #endif guards around function implementation blocks
3. Handle file-scope variables with extern/definition conditionals
"""

import re
import sys
import os

def classify_line(line, prev_line=""):
    """Classify a line of C code."""
    stripped = line.strip()

    # Empty or comment
    if not stripped or stripped.startswith('/*') or stripped.startswith('*') or stripped.startswith('//'):
        return 'other'

    # Preprocessor
    if stripped.startswith('#'):
        return 'preprocessor'

    # Static inline function
    if re.match(r'^static\s+inline\s+', line):
        return 'static_inline'

    # Thread-local variable
    if 'JACL_THREAD_LOCAL' in line and line.startswith('static'):
        return 'thread_local_var'

    # Static function pointer variable (like gc__oom_handler)
    if re.match(r'^static\s+\w+.*\(\*\w+\)', line) and '{' not in line:
        return 'static_var'

    # Static variable (non-function, ends with ; on this line or looks like assignment)
    if re.match(r'^static\s+(void\s*\*|const\s|int\s|uint|bool\s|char\s)', line):
        if '(' not in line and ';' in line:
            return 'static_var'

    # Static function forward declaration (ends with ;)
    if re.match(r'^static\s+', line) and '(' in line:
        # Check if the line (possibly continued) ends with );
        if re.search(r'\)\s*;', line):
            return 'static_func_decl'
        # Multi-line forward declaration - check if no { follows
        if '{' not in line and ';' not in line:
            return 'static_func_decl_start'

    # Static function definition (has { on this line or is a definition start)
    if re.match(r'^static\s+', line) and '(' in line:
        if '{' in line:
            return 'static_func_def'
        # Could be start of multi-line function definition
        return 'static_func_def_start'

    # JACL_EMBED_FN functions
    if line.startswith('JACL_EMBED_FN'):
        return 'embed_func'

    # Typedef
    if stripped.startswith('typedef'):
        return 'typedef'

    # Struct/enum closing with name
    if re.match(r'^}\s*\w+', stripped):
        return 'type_close'

    return 'other'


def find_func_blocks(lines):
    """Find contiguous blocks of non-inline function implementations."""
    blocks = []  # list of (start_line, end_line) tuples
    i = 0
    n = len(lines)

    while i < n:
        line = lines[i]
        cls = classify_line(line)

        if cls in ('static_func_def', 'static_func_def_start', 'embed_func'):
            block_start = i
            # Find the end of this function (matching braces)
            brace_count = 0
            j = i
            while j < n:
                brace_count += lines[j].count('{') - lines[j].count('}')
                if brace_count > 0 or '{' in lines[j]:
                    break
                j += 1
            # Now find closing brace
            if j < n:
                while j < n:
                    brace_count += lines[j].count('{') - lines[j].count('}')
                    if brace_count <= 0 and '{' in ''.join(lines[block_start:j+1]):
                        break
                    j += 1

            # Try to find the actual end by counting braces from the function start
            brace_count = 0
            started = False
            for k in range(i, n):
                brace_count += lines[k].count('{') - lines[k].count('}')
                if '{' in lines[k]:
                    started = True
                if started and brace_count == 0:
                    blocks.append((i, k))
                    i = k + 1
                    break
            else:
                i += 1
        else:
            i += 1

    return blocks


def extract_signature(lines, start):
    """Extract function signature from a function definition starting at 'start'."""
    sig_lines = []
    for i in range(start, len(lines)):
        line = lines[i]
        if '{' in line:
            # Take everything before the {
            sig_lines.append(line[:line.index('{')])
            break
        sig_lines.append(line)

    sig = ' '.join(l.strip() for l in sig_lines).strip()
    # Remove 'static ' prefix
    sig = re.sub(r'^static\s+', '', sig)
    # Remove JACL_EMBED_FN prefix
    sig = re.sub(r'^JACL_EMBED_FN\s+', '', sig)
    # Clean up whitespace
    sig = re.sub(r'\s+', ' ', sig).strip()
    if not sig.endswith(')'):
        sig = sig.rstrip()
    return sig + ';'


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 header_only_transform.py <file.c> [--dry-run]")
        sys.exit(1)

    filepath = sys.argv[1]
    dry_run = '--dry-run' in sys.argv

    with open(filepath) as f:
        lines = f.readlines()

    # Find all non-inline static function blocks
    func_blocks = find_func_blocks(lines)

    if not func_blocks:
        print(f"No non-inline static functions found in {filepath}")
        return

    # Extract signatures for forward declarations
    signatures = []
    for start, end in func_blocks:
        sig = extract_signature(lines, start)
        signatures.append(sig)

    # Find thread-local and static variables
    vars_info = []
    for i, line in enumerate(lines):
        cls = classify_line(line)
        if cls == 'thread_local_var':
            vars_info.append((i, 'thread_local', line))
        elif cls == 'static_var':
            vars_info.append((i, 'static', line))

    print(f"\n=== {os.path.basename(filepath)} ===")
    print(f"  Functions: {len(func_blocks)}")
    print(f"  Variables: {len(vars_info)}")

    if func_blocks:
        print(f"  First func at line {func_blocks[0][0]+1}, last ends at line {func_blocks[-1][1]+1}")

    # Check for interleaving: are there non-function lines between function blocks?
    for i in range(len(func_blocks) - 1):
        gap_start = func_blocks[i][1] + 1
        gap_end = func_blocks[i+1][0]
        for j in range(gap_start, gap_end):
            cls = classify_line(lines[j])
            if cls in ('typedef', 'type_close', 'static_inline'):
                print(f"  INTERLEAVING at line {j+1}: {cls}: {lines[j].rstrip()[:60]}")
                break

    print("\n  Forward declarations needed:")
    for sig in signatures:
        print(f"    {sig[:80]}")

    if vars_info:
        print("\n  Variables needing extern/def treatment:")
        for line_no, vtype, line in vars_info:
            print(f"    Line {line_no+1} ({vtype}): {line.rstrip()[:70]}")

    if dry_run:
        return

    print("\n  Function blocks:")
    for start, end in func_blocks:
        print(f"    Lines {start+1}-{end+1}")


if __name__ == '__main__':
    main()
