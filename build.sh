#!/bin/bash

CC="${CC:-gcc}"
CFLAGS="-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE"
PASS=0
FAIL=0
FAILED_TESTS=""

# Detect libffi for trampoline support.
# LIBFFI_COMPILE_FLAGS: -D and -I flags needed when compiling libjacl.a.
# LIBFFI_LINK_FLAGS: -l flag(s) needed when linking any binary against libjacl.a.
# LIBFFI_FLAGS: compile + link flags for final test executables (back-compat).
LIBFFI_COMPILE_FLAGS=""
LIBFFI_LINK_FLAGS=""
LIBFFI_FLAGS=""
if pkg-config --exists libffi 2>/dev/null; then
  LIBFFI_CFLAGS="$(pkg-config --cflags libffi 2>/dev/null)"
  LIBFFI_LIBS="$(pkg-config --libs libffi 2>/dev/null)"
  LIBFFI_COMPILE_FLAGS="-DJACL_HAS_LIBFFI=1 ${LIBFFI_CFLAGS}"
  LIBFFI_LINK_FLAGS="${LIBFFI_LIBS}"
  LIBFFI_FLAGS="-DJACL_HAS_LIBFFI=1 ${LIBFFI_CFLAGS} ${LIBFFI_LIBS}"
  echo "libffi: found via pkg-config"
elif [ -f "/usr/include/ffi.h" ] || [ -f "/usr/local/include/ffi.h" ] || \
     [ -f "/usr/include/x86_64-linux-gnu/ffi.h" ]; then
  LIBFFI_COMPILE_FLAGS="-DJACL_HAS_LIBFFI=1"
  LIBFFI_LINK_FLAGS="-lffi"
  LIBFFI_FLAGS="-DJACL_HAS_LIBFFI=1 -lffi"
  echo "libffi: found in standard paths"
else
  echo "libffi: not found (trampolines disabled)"
fi

LIB_ONLY=0
FILTER=""
CLEAN=0
PARALLEL=0
TSAN=0
RELEASE=0
for arg in "$@"; do
  case "$arg" in
    --lib) LIB_ONLY=1 ;;
    --clean) CLEAN=1 ;;
    --test=*) FILTER="${arg#--test=}" ;;
    --parallel|-j) PARALLEL=$(nproc) ;;
    --parallel=*|-j=*) PARALLEL="${arg#*=}" ;;
    --tsan) TSAN=1 ;;
    --release) RELEASE=1 ;;
  esac
done

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

# TSAN and --release each use a separate build cache so .build doesn't get
# poisoned with instrumented or differently-optimized binaries. Each variant
# is keyed on the mode via the platform stamp.
if [ "$TSAN" -eq 1 ] && [ "$RELEASE" -eq 1 ]; then
  echo "error: --tsan and --release are mutually exclusive."
  exit 2
fi
if [ "$TSAN" -eq 1 ]; then
  BUILD_DIR="${DIR}/.build-tsan"
  TSAN_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -O1"
  CFLAGS="$CFLAGS $TSAN_FLAGS"
  # halt_on_error=1: abort on first race so downstream UAFs don't cascade
  # and tests can't hang after data corruption. exitcode=66 keeps the
  # build runner's pass/fail logic working.
  export TSAN_OPTIONS="halt_on_error=1 exitcode=66 second_deadlock_stack=1${TSAN_OPTIONS:+ $TSAN_OPTIONS}"
  echo "TSAN mode: thread sanitizer enabled, build dir: $BUILD_DIR"
  echo "         TSAN_OPTIONS=$TSAN_OPTIONS"
elif [ "$RELEASE" -eq 1 ]; then
  # Release: -O2 for perf benchmarks and any runtime measurement. -g is kept
  # so profilers (samply, instruments) can still resolve symbols and lines.
  # Asserts stay enabled — strip them by hand if needed.
  BUILD_DIR="${DIR}/.build-release"
  TSAN_FLAGS=""
  CFLAGS="$CFLAGS -O2"
  echo "Release mode: -O2, build dir: $BUILD_DIR"
else
  TSAN_FLAGS=""
  BUILD_DIR="${DIR}/.build"
fi
mkdir -p "$BUILD_DIR"

if [ "$CLEAN" -eq 1 ]; then
  rm -rf "$BUILD_DIR"
  echo "Build cache cleaned."
  exit 0
fi

# Define tests: src|name|flags
TESTS=(
    "lib/arena/test_arena.c|arena|"
    "lib/chase_lev/test_chase_lev.c|chase_lev|"
    "lib/chase_lev/test_chase_lev_stress.c|chase_lev_stress|-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE -O2 -lpthread"
    "lib/platform/test_rwlock.c|rwlock|-Wall -Wextra -std=c99 -g -D_DEFAULT_SOURCE -lpthread"
    "lib/sum_tree/test_sum_tree.c|sum_tree|"
    "lib/sum_tree/test_utf8.c|utf8|"
    "lib/sum_tree/test_rope.c|rope|"
    "lib/sum_tree/test_grapheme_split.c|grapheme_split|"
    "lib/sum_tree/test_rope_concat.c|rope_concat|"
    "lib/regex/test_nfa.c|nfa|"
    "lib/regex/test_nfa_rope.c|nfa_rope|"
    "lib/bignum/test_bigint.c|bigint|"
    "lib/bignum/test_bigfloat.c|bigfloat|"
    "lib/bignum/test_bigint_pbt.c|bigint_pbt|"
    "lib/bignum/test_bigfloat_pbt.c|bigfloat_pbt|"
    "lib/segment_array/test_segment_array.c|segment_array|"
    "lib/segment_array/test_segment_array_var.c|segment_array_var|"
    # C-internal unit tests that link against the VM-free libjacl.a. The VM-only
    # tests (bytecode/compiler/vm/embed/e2e, milestones, and the run_ok-driven
    # feature tests that executed on the legacy VM) were removed with the bytecode
    # backend in item 6; SVM feature coverage lives in runtime/harness/tests/*.rs
    # and the SVM GC's concurrent/MT coverage in runtime/tests/*_mt.c.
    "test/test_value.c|value|"
    "test/test_lexer.c|lexer|"
    "test/test_parser.c|parser|"
    "test/test_head_id_stamp.c|head_id_stamp|"
    "test/test_typer.c|typer|"
    "test/test_rope_string.c|rope_string|"
    "test/test_utf8_nfd.c|utf8_nfd|"
    "test/test_string_new.c|string_new|"
)

# Filter tests if --lib flag is set
if [ "$LIB_ONLY" -eq 1 ]; then
  FILTERED=()
  for entry in "${TESTS[@]}"; do
    IFS='|' read -r src name extra_cflags <<< "$entry"
    case "$src" in lib/*) FILTERED+=("$entry") ;; esac
  done
  TESTS=("${FILTERED[@]}")
fi

# Filter to single test if --test=NAME is set
if [ -n "$FILTER" ]; then
  FILTERED=()
  for entry in "${TESTS[@]}"; do
    IFS='|' read -r src name extra_cflags <<< "$entry"
    if [ "$name" = "$FILTER" ]; then
      FILTERED+=("$entry")
    fi
  done
  if [ ${#FILTERED[@]} -eq 0 ]; then
    echo "No test matching '$FILTER'"
    exit 1
  fi
  TESTS=("${FILTERED[@]}")
fi

echo "=== ds test runner ==="
echo ""

# Find newest source file for cache invalidation
NEWEST_SRC=$(find src/ lib/ -name '*.c' -o -name '*.h' 2>/dev/null | xargs ls -t 2>/dev/null | head -1)

# Platform stamp: detect when switching between macOS and Linux so stale
# cross-platform binaries are automatically purged from the build cache.
PLATFORM_STAMP="$BUILD_DIR/.platform"
CURRENT_PLATFORM="$(uname -s)-$(uname -m)"
if [ -f "$PLATFORM_STAMP" ]; then
    CACHED_PLATFORM=$(cat "$PLATFORM_STAMP")
    if [ "$CACHED_PLATFORM" != "$CURRENT_PLATFORM" ]; then
        echo "Platform changed ($CACHED_PLATFORM -> $CURRENT_PLATFORM), clearing build cache..."
        rm -rf "$BUILD_DIR"
        mkdir -p "$BUILD_DIR"
    fi
else
    # No platform stamp — if there are cached binaries they may be from another
    # platform (e.g. macOS binaries in a Linux container).  Clear to be safe.
    if ls "$BUILD_DIR"/* >/dev/null 2>&1; then
        echo "No platform stamp found, clearing build cache for clean $CURRENT_PLATFORM build..."
        rm -rf "$BUILD_DIR"
        mkdir -p "$BUILD_DIR"
    fi
fi
echo "$CURRENT_PLATFORM" > "$PLATFORM_STAMP"

# Generate prelude_source.h from prelude.jacl if needed
PRELUDE_SRC="$DIR/src/prelude.jacl"
PRELUDE_HDR="$DIR/src/prelude_source.h"
if [ ! -f "$PRELUDE_HDR" ] || [ "$PRELUDE_HDR" -ot "$PRELUDE_SRC" ]; then
    echo -n "Generating prelude_source.h... "
    {
        echo '/* Auto-generated from prelude.jacl - do not edit */'
        echo 'static const char *jacl_prelude_source ='
        # Convert file contents to C string literal: escape backslashes, quotes, and wrap each line
        sed 's/\\/\\\\/g; s/"/\\"/g; s/^/    "/; s/$/\\n"/' "$PRELUDE_SRC"
        echo '    ;'
    } > "$PRELUDE_HDR"
    echo "ok"
else
    echo "prelude_source.h is up-to-date"
fi

# Generate prelude_macro_names.h — list of every macro NAME in prelude.jacl.
# Drives the compiler macro-expansion fast-path: any user invocation of one
# of these heads needs to be expanded. Hand-maintaining this list (as we
# used to) silently dropped new prelude macros from expansion.
PRELUDE_NAMES_HDR="$DIR/src/prelude_macro_names.h"
if [ ! -f "$PRELUDE_NAMES_HDR" ] || [ "$PRELUDE_NAMES_HDR" -ot "$PRELUDE_SRC" ]; then
    echo -n "Generating prelude_macro_names.h... "
    {
        echo '/* Auto-generated from prelude.jacl — do not edit. */'
        echo '/* List of (name, length) pairs for every macro defined in the prelude. */'
        echo 'static const struct { const char *name; uint32_t len; } jacl_prelude_macro_names[] = {'
        # Grab the second token of each `defmacro NAME ...` line, emit as
        # a C struct literal. Escapes backslash for the `\` macro name.
        grep -E '^[[:space:]]*defmacro[[:space:]]+' "$PRELUDE_SRC" |
          awk '{print $2}' |
          while IFS= read -r name; do
            esc=$(printf '%s' "$name" | sed 's/\\/\\\\/g')
            len=${#name}
            printf '    { "%s", %d },\n' "$esc" "$len"
          done
        echo '};'
        echo "#define JACL_PRELUDE_MACRO_NAMES_COUNT \
(sizeof(jacl_prelude_macro_names) / sizeof(jacl_prelude_macro_names[0]))"
    } > "$PRELUDE_NAMES_HDR"
    echo "ok"
else
    echo "prelude_macro_names.h is up-to-date"
fi

# Phase 0: Build shared libjacl.a (compile the unity build once)
#
# Freshness check uses both (a) source mtimes and (b) a flag-fingerprint
# file. The fingerprint catches the case where someone (a human running
# `gcc -c src/jacl.c` by hand, or build.sh re-invoked with a different
# flag mix) produces a libjacl.a whose mtime is newer than every src
# file but whose compile flags differ from the ones build.sh would use
# now. Without the fingerprint, the mtime check trusts that stale
# artifact -- and tests fail in ways that look like flakes (e.g.
# libffi disappears from libjacl.a while tests still expect it).
LIBJACL="$BUILD_DIR/libjacl.a"
JACL_OBJ="$BUILD_DIR/jacl.o"
LIBJACL_FLAGS_FILE="$BUILD_DIR/libjacl.flags"
LIBJACL_FLAGS_CURRENT="$CC|$CFLAGS|$LIBFFI_COMPILE_FLAGS"
NEED_LIB=0
if [ ! -f "$LIBJACL" ]; then
    NEED_LIB=1
elif [ -n "$NEWEST_SRC" ] && [ "$LIBJACL" -ot "$NEWEST_SRC" ]; then
    NEED_LIB=1
elif [ ! -f "$LIBJACL_FLAGS_FILE" ]; then
    NEED_LIB=1
elif [ "$(cat "$LIBJACL_FLAGS_FILE")" != "$LIBJACL_FLAGS_CURRENT" ]; then
    NEED_LIB=1
elif [ "$LIBJACL_FLAGS_FILE" -ot "$LIBJACL" ]; then
    # The .flags file is written *after* the .a, so under build.sh it is
    # always at-or-newer. If the .a is newer, the .a was overwritten out
    # of band (manual `gcc -c` / `ar rcs`) with unknown flags.
    NEED_LIB=1
fi
if [ "$NEED_LIB" -eq 1 ]; then
    echo -n "Building libjacl.a... "
    if $CC $CFLAGS $LIBFFI_COMPILE_FLAGS -c src/jacl.c -o "$JACL_OBJ" 2>"$BUILD_DIR/jacl.err"; then
        ar rcs "$LIBJACL" "$JACL_OBJ"
        printf '%s' "$LIBJACL_FLAGS_CURRENT" > "$LIBJACL_FLAGS_FILE"
        echo "ok"
    else
        echo "FAIL"
        cat "$BUILD_DIR/jacl.err" >&2
        exit 1
    fi
else
    echo "libjacl.a is up-to-date"
fi
echo ""

# Phase 1: compile all tests in parallel
PIDS=()
NEED_COMPILE=()
for entry in "${TESTS[@]}"; do
    IFS='|' read -r src name extra_cflags <<< "$entry"
    flags="$CFLAGS"
    if [ -n "$extra_cflags" ]; then
        flags="$extra_cflags"
        # Per-test flag lines override CFLAGS entirely; re-append TSAN flags
        # so sanitizer instrumentation is preserved.
        if [ "$TSAN" -eq 1 ]; then
            flags="$flags $TSAN_FLAGS"
        fi
    fi

    # Check if binary is up-to-date
    if [ -x "$BUILD_DIR/$name" ]; then
        skip=1
        # Check against the test source file itself
        if [ "$BUILD_DIR/$name" -ot "$src" ]; then
            skip=0
        fi
        # For test/ files that link against libjacl.a, check against the library
        case "$src" in
            test/*)
                if [ "$BUILD_DIR/$name" -ot "$LIBJACL" ]; then
                    skip=0
                fi
                ;;
        esac
        if [ "$skip" -eq 1 ]; then
            PIDS+=("")
            NEED_COMPILE+=("0")
            continue
        fi
    fi

    # test/ files link against libjacl.a; lib/ files compile standalone.
    # libjacl.a references ffi symbols when JACL_HAS_LIBFFI is defined, so
    # every test that links against it must also link -lffi.
    case "$src" in
        test/*)
            $CC $flags "$src" -o "$BUILD_DIR/$name" "$LIBJACL" $LIBFFI_LINK_FLAGS -lm 2>"$BUILD_DIR/${name}.err" &
            ;;
        *)
            $CC $flags "$src" -o "$BUILD_DIR/$name" -lm 2>"$BUILD_DIR/${name}.err" &
            ;;
    esac
    PIDS+=($!)
    NEED_COMPILE+=("1")
done

# Wait for all compilations
COMPILED=0
for i in "${!TESTS[@]}"; do
    IFS='|' read -r src name extra_cflags <<< "${TESTS[$i]}"
    if [ "${NEED_COMPILE[$i]}" = "0" ]; then
        continue
    fi
    COMPILED=$((COMPILED + 1))
    if ! wait "${PIDS[$i]}"; then
        printf "%-30s COMPILE FAIL\n" "$name"
        cat "$BUILD_DIR/${name}.err" >&2
        FAIL=$((FAIL + 1))
        FAILED_TESTS="$FAILED_TESTS $name"
    fi
done
TOTAL=${#TESTS[@]}
SKIPPED=$((TOTAL - COMPILED))
if [ "$SKIPPED" -gt 0 ]; then
    echo "Compiled $COMPILED/$TOTAL tests ($SKIPPED cached)"
else
    echo "Compiled $TOTAL tests"
fi
echo ""

# Phase 2: run tests
if [ "$PARALLEL" -gt 1 ]; then
    # Parallel execution: run up to $PARALLEL tests at once
    RESULTS_DIR=$(mktemp -d)
    TEST_NAMES=()
    for entry in "${TESTS[@]}"; do
        IFS='|' read -r src name extra_cflags <<< "$entry"
        if [ ! -x "$BUILD_DIR/$name" ]; then
            continue
        fi
        TEST_NAMES+=("$name")
    done
    printf '%s\n' "${TEST_NAMES[@]}" | xargs -P "$PARALLEL" -I{} sh -c \
        '"$1/$2" > "$3/$2.out" 2>&1; echo $? > "$3/$2.rc"' _ "$BUILD_DIR" {} "$RESULTS_DIR"

    # Collect results in order
    for entry in "${TESTS[@]}"; do
        IFS='|' read -r src name extra_cflags <<< "$entry"
        if [ ! -x "$BUILD_DIR/$name" ]; then
            continue
        fi
        rc=$(cat "$RESULTS_DIR/${name}.rc" 2>/dev/null || echo "1")
        if [ "$rc" = "0" ]; then
            printf "%-30s PASS\n" "$name"
            PASS=$((PASS + 1))
        else
            printf "%-30s FAIL\n" "$name"
            cat "$RESULTS_DIR/${name}.out" >&2
            FAIL=$((FAIL + 1))
            FAILED_TESTS="$FAILED_TESTS $name"
        fi
    done
    rm -rf "$RESULTS_DIR"
else
    # Sequential execution
    for entry in "${TESTS[@]}"; do
        IFS='|' read -r src name extra_cflags <<< "$entry"
        if [ ! -x "$BUILD_DIR/$name" ]; then
            continue  # compilation failed, already counted
        fi
        printf "%-30s" "$name"
        if "$BUILD_DIR/$name"; then
            echo "PASS"
            PASS=$((PASS + 1))
        else
            echo "FAIL"
            FAIL=$((FAIL + 1))
            FAILED_TESTS="$FAILED_TESTS $name"
        fi
    done
fi

# Summary
echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
if [ "$FAIL" -ne 0 ]; then
    echo "FAILED:$FAILED_TESTS"
    exit 1
fi
