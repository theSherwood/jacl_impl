# PRD: File-Based JACL Test Harness

## Introduction

Replace inline C string integration tests with `.jacl` source files that declare expected output in header comments. A small C test harness reads `.jacl` files from a directory, runs each through `jacl_run`, and compares actual output against expectations. This makes tests easier to write, read, and maintain as the language grows.

## Goals

- Enable writing end-to-end JACL tests as plain `.jacl` files with no C code
- Support expected output (`# expect:`) and expected errors (`# expect-error:`)
- Integrate as a single entry in build.sh's TESTS array
- Support targeting individual test files for debugging
- Establish a foundation for migrating existing C integration tests over time

## User Stories

### US-001: Test harness C program
**Description:** As a developer, I want a C test harness that discovers `.jacl` files in a directory and runs them so that I can write tests without touching C code.

**Acceptance Criteria:**
- [ ] `test/test_jacl_harness.c` exists, includes `../src/jacl.c` and `test_helpers.h` (standard unity build pattern)
- [ ] Harness scans a directory for `*.jacl` files (use `opendir`/`readdir` from `<dirent.h>`)
- [ ] Default directory is `test/jacl/` (relative to the binary's working directory — build.sh runs from project root)
- [ ] For each `.jacl` file: reads the file contents, parses `# expect:` / `# expect-error:` header comments, runs through `jacl_run` with PrintCapture, compares actual output to expected
- [ ] Uses tracked_allocator and checks for memory leaks after each test file (same pattern as existing tests)
- [ ] Prints per-file pass/fail with the filename, plus a summary line at the end
- [ ] Exits with code 0 if all pass, 1 if any fail
- [ ] Compiles cleanly with `-Wall -Wextra -std=c99 -g`

### US-002: Comment-based expected output parsing
**Description:** As a test author, I want to declare expected output in `# expect:` comments so that each test file is self-contained.

**Acceptance Criteria:**
- [ ] Lines starting with `# expect: ` (note trailing space) at the top of the file define expected stdout lines, in order
- [ ] Lines starting with `# expect-error: ` define an expected runtime error — the program should return VM_RUNTIME_ERROR and the VM error message should contain the given substring
- [ ] A file can have multiple `# expect:` lines (one per expected output line) OR a single `# expect-error:` line, but not both
- [ ] Regular `#` comments that don't match `# expect:` or `# expect-error:` are ignored (they're just JACL comments)
- [ ] Header comment parsing stops at the first non-comment, non-blank line (expectations must be at the top)
- [ ] If a file has no `# expect:` or `# expect-error:` directives, it is treated as "expect success with no output" (VM_OK, empty stdout)

### US-003: Individual test targeting
**Description:** As a developer, I want to run a single `.jacl` test file for debugging so that I can iterate quickly on failures.

**Acceptance Criteria:**
- [ ] The harness accepts an optional command-line argument: a path to a single `.jacl` file
- [ ] When given a path, it runs only that one file instead of scanning the directory
- [ ] When given a filename without a path (e.g., `basics.jacl`), it looks in the default `test/jacl/` directory
- [ ] Output for single-file mode is the same format as multi-file mode (pass/fail + summary)
- [ ] Example usage: `./test_jacl_harness test/jacl/bare_commands.jacl`

### US-004: Build system integration
**Description:** As a developer, I want the file-based tests to run as part of `build.sh` so that they're always verified alongside existing tests.

**Acceptance Criteria:**
- [ ] `test/jacl/` directory exists
- [ ] Entry added to build.sh TESTS array: `"test/test_jacl_harness.c|jacl_harness|"`
- [ ] `build.sh` passes (harness compiles and runs successfully, even if `test/jacl/` has zero test files initially)
- [ ] Compiles cleanly with `-Wall -Wextra -std=c99 -g`

### US-005: Initial test suite
**Description:** As a developer, I want a starter set of `.jacl` test files covering core functionality so that the harness is immediately useful.

**Acceptance Criteria:**
- [ ] `test/jacl/bare_commands.jacl` — bare command with args (`print hello`), bare command no args (proc call), nested brackets in line mode
- [ ] `test/jacl/separators.jacl` — semicolon, comma, newline, and mixed separators
- [ ] `test/jacl/line_continuation.jacl` — backslash continuation
- [ ] `test/jacl/procs.jacl` — proc definition, call, recursion (factorial), closures
- [ ] `test/jacl/variables.jacl` — def, $var usage, $var as command call
- [ ] `test/jacl/control_flow.jacl` — if/else, while loop
- [ ] `test/jacl/strings.jacl` — string literals, interpolation, concat, length, index, slice
- [ ] `test/jacl/arithmetic.jacl` — +, -, *, /, %, comparisons
- [ ] `test/jacl/error_basic.jacl` — at least one `# expect-error:` test (e.g., calling a non-callable value)
- [ ] All test files pass when run through the harness
- [ ] Total suite passes as part of `build.sh`

## Functional Requirements

- FR-1: The harness must read `.jacl` files and extract `# expect:` lines as expected stdout (one line per directive, in order)
- FR-2: The harness must support `# expect-error: <substring>` to assert runtime failure with a matching error message
- FR-3: The harness must run each `.jacl` file through `jacl_run` with PrintCapture and tracked_allocator
- FR-4: The harness must compare actual captured output line-by-line against expected output and report mismatches clearly (show expected vs actual)
- FR-5: The harness must check for memory leaks after each test file
- FR-6: The harness must support both directory scanning mode (default) and single-file mode (via CLI argument)
- FR-7: The harness must print clear pass/fail per file and a summary count at the end

## Non-Goals

- No test file generation or migration of existing C tests (that's future work)
- No support for test fixtures, setup/teardown, or shared state between test files
- No support for timeouts or test isolation beyond arena reset
- No wildcard or glob patterns for test selection — single file or whole directory only
- No subdirectory recursion — flat `test/jacl/` directory only

## Technical Considerations

- The harness is a single C file following the unity build pattern (`#include "../src/jacl.c"`)
- File reading can use standard `fopen`/`fread`/`fclose` — no need for mmap
- Directory scanning uses POSIX `<dirent.h>` (`opendir`/`readdir`) which is available on macOS and Linux
- The `print` builtin appends `\n` to output, so `# expect: hello` matches a `print hello` that produces `hello\n`
- Expected output is joined with `\n` between lines and a trailing `\n` — then compared as a single string against captured output
- For `# expect-error:`, check `vm.error_message` contains the substring (not exact match, for flexibility)

## Success Metrics

- All `.jacl` test files pass in `build.sh` alongside existing C tests
- Writing a new end-to-end test requires only creating a `.jacl` file (no C code, no build changes)
- Test failures produce clear output showing which file failed and expected vs actual

## Open Questions

- Should we support `# skip` or `# todo` directives for work-in-progress tests?
- Should the harness sort files alphabetically for deterministic ordering, or run in directory order?
