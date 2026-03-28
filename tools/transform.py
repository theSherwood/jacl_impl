#!/usr/bin/env python3
"""
Transform JACL .c files to support JACL_HEADER_ONLY mode.

Strategy:
- Parse each .c file to identify contiguous blocks of non-inline static function implementations
- Wrap each block in #ifndef JACL_HEADER_ONLY / #endif
- Remove 'static' from non-inline function definitions and forward declarations
- Handle file-scope variables with #ifdef JACL_HEADER_ONLY extern / #else def / #endif
- Add forward declarations for all non-inline functions after include guard
"""

import re
import sys
import os

def find_matching_brace(lines, start_line):
    """Find the line containing the closing brace that matches the first { found from start_line."""
    brace_count = 0
    for i in range(start_line, len(lines)):
        for ch in lines[i]:
            if ch == '{':
                brace_count += 1
            elif ch == '}':
                brace_count -= 1
                if brace_count == 0:
                    return i
    return len(lines) - 1


def is_func_line(line):
    """Check if a line starts a static non-inline function (definition or forward declaration)."""
    if not line.startswith('static '):
        return False
    if 'inline' in line.split('(')[0] if '(' in line else line:
        return False
    if 'JACL_THREAD_LOCAL' in line:
        return False
    if '(' not in line:
        return False
    return True


def is_func_def(lines, i):
    """Check if line i is the start of a function DEFINITION (not forward decl)."""
    # Look for { on this line or following lines before a ;
    for j in range(i, min(i + 5, len(lines))):
        if '{' in lines[j]:
            return True
        if ');' in lines[j]:
            return False
    return False


def is_embed_func(line):
    """Check if line starts a JACL_EMBED_FN function definition."""
    return line.startswith('JACL_EMBED_FN')


def is_forward_decl(lines, i):
    """Check if line i is a forward declaration (ends with ; before any {)."""
    for j in range(i, min(i + 5, len(lines))):
        stripped = lines[j].rstrip()
        if stripped.endswith(');') or stripped.endswith(';'):
            if '(' in ''.join(lines[i:j+1]):
                return True
        if '{' in lines[j]:
            return False
    return False


def extract_signature(lines, start):
    """Extract function signature from definition, return as 'type name(params);'"""
    sig_parts = []
    for i in range(start, min(start + 5, len(lines))):
        line = lines[i]
        if '{' in line:
            sig_parts.append(line[:line.index('{')].rstrip())
            break
        sig_parts.append(line.rstrip())

    sig = ' '.join(sig_parts)
    # Remove static prefix
    sig = re.sub(r'^static\s+', '', sig)
    # Remove JACL_EMBED_FN prefix
    sig = re.sub(r'^JACL_EMBED_FN\s+', '', sig)
    # Clean up
    sig = re.sub(r'\s+', ' ', sig).strip()
    if not sig.endswith(')'):
        sig = sig.rstrip()
    return sig + ';'


def remove_static_prefix(line):
    """Remove 'static ' prefix from a line."""
    return re.sub(r'^static\s+', '', line, count=1)


def transform_file(filepath, dry_run=False):
    """Transform a single .c file to support JACL_HEADER_ONLY."""
    with open(filepath) as f:
        lines = f.readlines()

    basename = os.path.basename(filepath)
    print(f"\n{'='*60}")
    print(f"Processing {basename}")
    print(f"{'='*60}")

    # Phase 1: Identify all non-inline static function definitions
    func_defs = []  # (start_line, end_line) of function bodies
    fwd_decls = []  # line numbers of forward declarations
    embed_funcs = []  # (start_line, end_line) of JACL_EMBED_FN functions
    var_lines = []  # (line_number, original_line) for variables needing treatment

    i = 0
    while i < len(lines):
        line = lines[i]

        # JACL_EMBED_FN functions (embed.c)
        if is_embed_func(line):
            if is_func_def(lines, i):
                end = find_matching_brace(lines, i)
                embed_funcs.append((i, end))
                func_defs.append((i, end))
                i = end + 1
                continue
            elif is_forward_decl(lines, i):
                fwd_decls.append(i)
                i += 1
                continue

        # Static non-inline functions
        if is_func_line(line):
            if is_func_def(lines, i):
                end = find_matching_brace(lines, i)
                func_defs.append((i, end))
                i = end + 1
                continue
            elif is_forward_decl(lines, i):
                fwd_decls.append(i)
                # Forward declarations can span multiple lines
                j = i
                while j < len(lines) and ';' not in lines[j]:
                    j += 1
                i = j + 1
                continue

        # Thread-local variables
        if 'JACL_THREAD_LOCAL' in line and line.startswith('static'):
            var_lines.append((i, line))
            i += 1
            continue

        # Regular static variables (non-function, non-inline)
        if line.startswith('static ') and 'inline' not in line and '(' not in line and ';' in line:
            # But not if it's inside a function (we'd need scope tracking for that)
            # Simple heuristic: only at column 0
            var_lines.append((i, line))
            i += 1
            continue

        # Static function pointer variables: static TYPE (*NAME)(...) = ...;
        if line.startswith('static ') and 'inline' not in line and '(*' in line and ';' in line:
            if 'JACL_THREAD_LOCAL' not in line:
                var_lines.append((i, line))
                i += 1
                continue

        i += 1

    print(f"  Found {len(func_defs)} function definitions")
    print(f"  Found {len(fwd_decls)} forward declarations")
    print(f"  Found {len(var_lines)} variables")

    if not func_defs and not var_lines:
        print("  Nothing to transform")
        return

    # Phase 2: Extract forward declarations for all function definitions
    new_fwd_decls = []
    for start, end in func_defs:
        sig = extract_signature(lines, start)
        new_fwd_decls.append(sig)

    # Phase 3: Merge consecutive function defs into blocks (allowing blank lines/comments between)
    func_blocks = []  # (block_start, block_end) - contiguous function regions
    if func_defs:
        block_start = func_defs[0][0]
        block_end = func_defs[0][1]

        for i in range(1, len(func_defs)):
            fs, fe = func_defs[i]
            # Check if there's a type definition, typedef, or inline function between blocks
            has_type_between = False
            for j in range(block_end + 1, fs):
                stripped = lines[j].strip()
                if stripped.startswith('typedef'):
                    has_type_between = True
                    break
                if stripped.startswith('static inline'):
                    has_type_between = True
                    break
                # Check for struct/enum definitions (not inside comments)
                if re.match(r'^(typedef\s+)?(struct|enum|union)\s+', stripped):
                    has_type_between = True
                    break
                # Macro definitions that define types or important constants
                if stripped.startswith('#define') and not stripped.startswith('#define GC_') and not stripped.startswith('#define INTERN_'):
                    # Only break for type-like macros
                    pass

            if has_type_between:
                func_blocks.append((block_start, block_end))
                block_start = fs
            block_end = fe

        func_blocks.append((block_start, block_end))

    print(f"  Merged into {len(func_blocks)} guard blocks")
    for bs, be in func_blocks:
        print(f"    Lines {bs+1}-{be+1}")

    if dry_run:
        print("\n  Forward declarations to add:")
        for sig in new_fwd_decls:
            print(f"    {sig}")
        return

    # Phase 4: Apply transformations (from bottom to top to preserve line numbers)

    # 4a: Add #ifndef JACL_HEADER_ONLY guards around function blocks (bottom to top)
    for block_start, block_end in reversed(func_blocks):
        # Insert #endif after block_end
        lines.insert(block_end + 1, '#endif /* !JACL_HEADER_ONLY */\n\n')
        # Insert #ifndef before block_start
        lines.insert(block_start, '\n#ifndef JACL_HEADER_ONLY\n')

    # 4b: Recalculate line positions after guard insertion
    # Each guard block adds 2 lines (one before, one after)
    # Since we inserted from bottom to top, the offsets for variables need adjustment
    # Let's just re-find the variable and function lines

    # Instead of complex offset tracking, let's do a second pass for static removal and variable handling
    result = []
    for line in lines:
        # Remove 'static' from non-inline function definitions/declarations
        if line.startswith('static ') and 'inline' not in line and 'JACL_THREAD_LOCAL' not in line and '(' in line:
            # But NOT from static variables (no parens before = or ;)
            # Check: is this a function (has paren before any = or ;)?
            paren_pos = line.index('(')
            has_eq = '=' in line and line.index('=') < paren_pos if '=' in line else False
            has_semi = ';' in line and line.index(';') < paren_pos if ';' in line else False
            if not has_eq and not has_semi:
                # Also check it's not a function pointer variable: (*name)
                if '(*' not in line[:paren_pos]:
                    line = remove_static_prefix(line)

        # Remove 'static' from JACL_EMBED_FN if needed
        # (JACL_EMBED_FN is already 'static' via macro - we need to change the macro)

        result.append(line)

    # Handle variables: need to add extern/def conditionals
    # This requires another pass since line numbers changed
    final = []
    i = 0
    while i < len(result):
        line = result[i]

        # Thread-local variable
        if 'JACL_THREAD_LOCAL' in line and line.startswith('static'):
            # Create extern declaration
            extern_line = line.replace('static ', 'extern ', 1)
            # Remove initializer for extern declaration
            if '=' in extern_line:
                extern_line = re.sub(r'\s*=\s*[^;]*;', ';', extern_line)
            elif not extern_line.rstrip().endswith(';'):
                extern_line = extern_line.rstrip() + ';\n'

            # Definition without static
            def_line = line.replace('static ', '', 1)

            final.append('#ifdef JACL_HEADER_ONLY\n')
            final.append(extern_line)
            final.append('#else\n')
            final.append(def_line)
            final.append('#endif\n')
            i += 1
            continue

        # Non-TL static variable (function pointers, etc.)
        if line.startswith('static ') and 'inline' not in line and '(' not in line.split('=')[0] and ';' in line:
            # Check it's really a variable, not a function
            before_eq = line.split('=')[0] if '=' in line else line
            if '(' not in before_eq or '(*' in before_eq:
                extern_line = line.replace('static ', 'extern ', 1)
                if '=' in extern_line:
                    extern_line = re.sub(r'\s*=\s*[^;]*;', ';', extern_line)
                def_line = line.replace('static ', '', 1)

                final.append('#ifdef JACL_HEADER_ONLY\n')
                final.append(extern_line)
                final.append('#else\n')
                final.append(def_line)
                final.append('#endif\n')
                i += 1
                continue

        # Static function pointer variable with parens
        if line.startswith('static ') and 'inline' not in line and '(*' in line and ';' in line and 'JACL_THREAD_LOCAL' not in line:
            extern_line = line.replace('static ', 'extern ', 1)
            if '=' in extern_line:
                extern_line = re.sub(r'\s*=\s*[^;]*;', ';', extern_line)
            def_line = line.replace('static ', '', 1)

            final.append('#ifdef JACL_HEADER_ONLY\n')
            final.append(extern_line)
            final.append('#else\n')
            final.append(def_line)
            final.append('#endif\n')
            i += 1
            continue

        final.append(line)
        i += 1

    # Now add forward declarations after the include guard
    # Find the line with #define XXX_C (the include guard)
    guard_line = -1
    for idx, line in enumerate(final):
        if re.match(r'^#define\s+\w+_C\s*$', line.strip()):
            guard_line = idx
            break

    if guard_line >= 0 and new_fwd_decls:
        # Find a good insertion point: after the include guard and any immediate includes/types
        # Insert after the guard line + 1 (after any blank line)
        insert_at = guard_line + 1
        # Skip blank lines and comments right after guard
        while insert_at < len(final) and (final[insert_at].strip() == '' or final[insert_at].strip().startswith('/*')):
            insert_at += 1

        # Build forward declarations block
        fwd_block = ['\n/* Forward declarations (for JACL_HEADER_ONLY separate compilation) */\n']
        for sig in new_fwd_decls:
            fwd_block.append(sig + '\n')
        fwd_block.append('\n')

        final = final[:insert_at] + fwd_block + final[insert_at:]

    # Write result
    with open(filepath, 'w') as f:
        f.writelines(final)

    print(f"  Wrote {len(final)} lines (was {len(lines)})")


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 transform.py <file.c|--all> [--dry-run]")
        sys.exit(1)

    dry_run = '--dry-run' in sys.argv
    files = [a for a in sys.argv[1:] if not a.startswith('--')]

    if '--all' in sys.argv:
        src_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'src')
        files = [
            'gc.c', 'string.c', 'lexer.c', 'ast.c', 'parser.c',
            'bytecode.c', 'collections.c', 'compiler.c', 'vm.c',
            'gc_collect.c', 'runtime.c', 'embed.c'
        ]
        files = [os.path.join(src_dir, f) for f in files]

    for f in files:
        if os.path.isfile(f):
            transform_file(f, dry_run)
        else:
            print(f"File not found: {f}")


if __name__ == '__main__':
    main()
