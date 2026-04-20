# PRD: Shell Interop (`!cmd`)

## Introduction

Add the ability to run external commands from JACL using the `!cmd` syntax. This is the core feature that makes JACL usable as a shell scripting language. External commands integrate with JACL's existing error handling, streams, pipes, and sandboxing model.

The `!cmd` syntax is sugar for external command execution. The `exec` builtin is a prelude-controlled capability that can be removed for sandboxed code, providing a clean security boundary without special-casing.

## Goals

- Enable running external commands with `!cmd args...` syntax
- Return stdout as a **stream** (lazy, memory-safe for large outputs)
- On failure (non-zero exit), return error value with stderr as message
- Provide full-control `[exec ...]` form returning struct with stdout/stderr/exit
- Support background processes (Jobs) with `&` suffix
- Integrate with existing streams (stdin from JACL strings/streams)
- Optimize adjacent `!cmd | !cmd` as real OS pipes
- Enable sandboxing by making `exec` a prelude entry
- Support `$PATH` fallback for bare commands in REPL mode

## Design Decisions

**Stdout is a stream:** `!cmd` returns a lazy stream of stdout, not a buffered string. This is memory-safe for large outputs. Use `collect` to materialize when needed:
```jacl
# Stream processing — O(1) memory
!cat huge.log | for line { print $line }

# Materialize when you need a string
version = {!git --version | collect}

# OS pipes stay lazy
!cat huge.log | !grep "error"   # never materializes in JACL
```

**`!cmd` vs `[exec ...]`:** Two forms with different return types:
- `!cmd` — returns stream on success, error value on failure (95% use case)
- `[exec "cmd" args...]` — always returns struct `{stdout, stderr, exit}` for full control

**Environment variables:** Child processes inherit the parent process environment directly. This is simple and works without `$ctx`. When `$ctx` is implemented later, commands will use `$ctx.env` instead, enabling scoped environment changes via `with-env`. The `exec` builtin should be designed with this migration in mind.

**Stderr encoding:** Lossy UTF-8 conversion — invalid bytes replaced with U+FFFD. This ensures stderr is always a valid JACL string. A future bytes type could enable strict handling.

**Signal on completed Job:** `[signal $job SIG]` returns boolean — `$true` if signal was sent, `$false` if process already exited. This avoids exceptions for common race conditions.

**Job cleanup on JACL exit:** Jobs receive SIGTERM when JACL exits; JACL does not block waiting for them. Per-Job exit behavior (orphan vs kill) deferred until option syntax is designed (e.g., `:on-exit "orphan"`).

## User Stories

### US-001: Lexer recognizes `!` prefix
**Description:** As the lexer, I need to recognize `!` at the start of a command as the external command prefix.

**Acceptance Criteria:**
- [ ] `!` at command position produces `TOKEN_BANG` or similar
- [ ] `!ls` lexes as bang + identifier `ls`
- [ ] `!git status` lexes as bang + identifier `git` + identifier `status`
- [ ] `!!` (bang-bang for last command) is NOT in scope — just single `!`
- [ ] Existing `!` usage (if any) doesn't break
- [ ] `build.sh` passes

### US-002: Parser handles `!cmd` syntax
**Description:** As the parser, I need to parse `!cmd args...` and produce an AST node that the compiler can process.

**Acceptance Criteria:**
- [ ] `!ls -la` parses to AST node with command="ls", args=["-la"]
- [ ] `!git status --short` parses correctly with multiple args
- [ ] `!"my script.sh"` works (quoted command name)
- [ ] `!$cmd` works (variable as command name)
- [ ] `!cmd ..$args` works (splat for args)
- [ ] Parse errors for malformed `!` usage produce clear messages
- [ ] Parser tests added
- [ ] `build.sh` passes

### US-003: Compiler desugars `!cmd` to `[exec ...]`
**Description:** As the compiler, I need to transform `!cmd args...` into a call to the `exec` builtin.

**Acceptance Criteria:**
- [ ] `!ls -la` compiles to equivalent of `[exec "ls" "-la"]`
- [ ] `!$cmd $arg1 $arg2` compiles with variable references preserved
- [ ] `!cmd ..$args` compiles with splat preserved
- [ ] Compiler checks that `exec` is available in the prelude/env
- [ ] If `exec` is not in prelude (sandboxed), compile error: "exec not available"
- [ ] Compiler tests added
- [ ] `build.sh` passes

### US-004: `!cmd` returns stdout stream
**Description:** As the VM, I need `!cmd` to spawn a child process and return stdout as a lazy stream.

**Acceptance Criteria:**
- [ ] `OP_EXEC` opcode (or similar) added to bytecode
- [ ] `exec` capability added to default prelude (via `[interpret-prelude]`)
- [ ] `!ls` runs `ls` and returns stdout as a stream
- [ ] `!ls -la /tmp` passes multiple args correctly
- [ ] Child process inherits parent's environment variables
- [ ] Child process inherits parent's working directory
- [ ] Stream is lazy — large outputs don't buffer fully in memory
- [ ] `{!echo "hello" | collect}` returns `"hello\n"` (string after collect)
- [ ] `build.sh` passes

### US-005: `!cmd` error handling — exit codes
**Description:** As a JACL programmer, I want failed commands to return error values so pipes short-circuit correctly.

**Acceptance Criteria:**
- [ ] Exit code 0: return stdout stream
- [ ] Exit code non-zero: return JACL error value
- [ ] Error value's message is stderr content (lossy UTF-8)
- [ ] `!false` returns error value (exit 1)
- [ ] `{!sh -c "echo err >&2; exit 1"}` error message contains "err"
- [ ] Error composes with pipes: `{!false | for x { print $x }}` — for never runs
- [ ] Error works with `catch`: `{!false | catch {"fallback"}}`
- [ ] Integration tests for error cases
- [ ] `build.sh` passes

### US-006: `[exec ...]` full form — struct result
**Description:** As a JACL programmer, I want access to stdout, stderr, and exit code separately for cases where I need full control.

**Acceptance Criteria:**
- [ ] `[exec "cmd" args...]` returns struct `{stream stdout, str stderr, i32 exit}`
- [ ] Struct fields accessible via `->`: `$result->stdout`, `$result->exit`
- [ ] `$result->stdout` is a stream (same as `!cmd` on success)
- [ ] `$result->stderr` is a string (lossy UTF-8)
- [ ] Works even when exit is non-zero (doesn't return error value)
- [ ] Integration test: capture both stdout and stderr from a command
- [ ] `build.sh` passes

### US-007: Stdin from JACL strings
**Description:** As a JACL programmer, I want to pipe JACL strings into external commands as stdin.

**Acceptance Criteria:**
- [ ] `{"hello world" | !wc -w}` feeds string to wc's stdin
- [ ] Pipe detected at compile time or runtime — string goes to stdin, not arg
- [ ] Works with multi-line strings
- [ ] Works with string variables: `{$data | !grep "pattern"}`
- [ ] Integration test: `{"a\nb\nc" | !wc -l | collect}` returns "3\n"
- [ ] `build.sh` passes

### US-008: Stdin from JACL streams
**Description:** As a JACL programmer, I want to pipe JACL streams into external commands lazily.

**Acceptance Criteria:**
- [ ] Stream piped to `!cmd` feeds elements to stdin incrementally
- [ ] `{[lines "a\nb\nc"] | !cat}` works (stream to external)
- [ ] Large streams don't materialize fully in memory
- [ ] Works with `{!cmd1 | !cmd2}` (external stream to external)
- [ ] Integration test with stream input
- [ ] `build.sh` passes

### US-009: OS pipes between adjacent external commands
**Description:** As the runtime, I want adjacent `!cmd | !cmd` to use real OS pipes for efficiency.

**Acceptance Criteria:**
- [ ] `{!ls | !grep ".txt"}` creates OS pipe (stdout of ls → stdin of grep)
- [ ] No intermediate JACL stream materialization for adjacent externals
- [ ] `{!cmd1 | !cmd2 | !cmd3}` chains multiple OS pipes
- [ ] Mixed pipes work: `{!ls | filter [\\ match-re $it ".txt"] | !sort}` — ls output goes through JACL filter, then to sort
- [ ] Compiler/runtime detects adjacent external commands for optimization
- [ ] Integration test: large data through `{!cat file | !head -1}` doesn't OOM
- [ ] `build.sh` passes

### US-010: Jobs — background execution with `&`
**Description:** As a JACL programmer, I want to run external commands in the background and interact with the process.

**Acceptance Criteria:**
- [ ] `!cmd &` syntax parses and compiles
- [ ] Returns a `Job` value (a Future that wraps an OS process)
- [ ] `await $job` returns the ExecResult struct `{stdout, stderr, exit}`
- [ ] `$job->pid` returns the OS process ID
- [ ] Job doesn't block — parent continues immediately
- [ ] Integration test: start background process, do other work, await result
- [ ] `build.sh` passes

### US-011: Jobs — signals and cancellation
**Description:** As a JACL programmer, I want to send signals to background processes.

**Acceptance Criteria:**
- [ ] `[signal $job SIGTERM]` sends signal to the process, returns `$true`
- [ ] `[signal $job SIGKILL]` works
- [ ] `[cancel $job]` kills the process (sends SIGTERM)
- [ ] Signal on completed job returns `$false` (no error, no-op)
- [ ] Integration test: start long-running process, cancel it, verify it stopped
- [ ] Integration test: signal completed job, verify returns `$false`
- [ ] `build.sh` passes

### US-012: REPL `$PATH` fallback
**Description:** As a REPL user, I want bare unknown commands to fall back to `$PATH` lookup so I can use the REPL as a shell.

**Acceptance Criteria:**
- [ ] In REPL mode: `ls -la` (without `!`) tries `$PATH` lookup if `ls` is not a JACL binding
- [ ] In script mode: `ls -la` is a compile error (no implicit fallback)
- [ ] Fallback only triggers for command position, not expressions
- [ ] User can disable fallback in REPL via setting/pragma
- [ ] Integration test for REPL fallback behavior
- [ ] `build.sh` passes

### US-013: Sandboxing — `exec` as prelude entry
**Description:** As the sandboxing system, I need `exec` to be a prelude-controlled capability.

**Acceptance Criteria:**
- [ ] `exec` is in the default prelude returned by `[interpret-prelude]`
- [ ] `[interpret {} "!ls"]` fails with compile error (exec not available)
- [ ] `[interpret [interpret-prelude] "!ls"]` works
- [ ] `[interpret [map-remove [interpret-prelude] "exec"] "!ls"]` fails
- [ ] Custom exec can be injected: `[interpret {"exec": $my-exec} "!ls"]`
- [ ] Jobs (`&`) also blocked when exec is unavailable
- [ ] Integration tests for sandbox blocking
- [ ] `build.sh` passes

### US-014: Argument handling — literals and splat
**Description:** As a JACL programmer, I want predictable argument handling with explicit glob support.

**Acceptance Criteria:**
- [ ] `!ls *.txt` passes literal `"*.txt"` to ls (no expansion)
- [ ] `!ls ..[glob "*.txt"]` splats glob results as separate args
- [ ] `!cmd $arg` passes variable value as single arg
- [ ] `!cmd ..$args` splats vector elements as separate args
- [ ] Empty splat `..[]` adds no args
- [ ] Args with spaces work: `!cmd "hello world"` is single arg
- [ ] Integration tests for various arg patterns
- [ ] `build.sh` passes

## Functional Requirements

- FR-1: Lexer produces token for `!` prefix in command position
- FR-2: Parser transforms `!cmd args...` into AST node representing external command
- FR-3: Compiler emits code that checks `exec` capability and spawns process
- FR-4: `!cmd` returns stdout as a lazy stream on success, error value on failure
- FR-5: `[exec "cmd" args...]` returns struct `{stream stdout, str stderr, i32 exit}`
- FR-6: Stderr is lossy UTF-8 converted (invalid bytes → U+FFFD)
- FR-7: String piped to external command feeds stdin
- FR-8: Stream piped to external command feeds stdin lazily
- FR-9: Adjacent `!cmd | !cmd` uses OS pipes (no intermediate materialization)
- FR-10: `!cmd &` returns Job value for background execution
- FR-11: Job supports `await`, `pid`, `signal` (returns bool), `cancel`
- FR-12: REPL mode falls back to `$PATH` for unknown bare commands
- FR-13: Script mode requires explicit `!` prefix (no fallback)
- FR-14: `exec` is a prelude entry, removable for sandboxing
- FR-15: Arguments are literal by default; glob expansion via explicit `[glob ...]` + splat
- FR-16: On JACL exit, running Jobs receive SIGTERM (non-blocking)

## Non-Goals

- **`$ctx` integration:** Environment comes from parent process, not `$ctx.env` (deferred to `$ctx` implementation)
- **Per-Job exit behavior:** Configurable `:on-exit` option deferred until option syntax is designed
- **Glob expansion in args:** No implicit `*.txt` expansion; use explicit `[glob "*.txt"]`
- **Shell syntax in args:** No `$VAR` expansion, `~` expansion, backticks, etc. — args are JACL values
- **Builtin shell commands:** No `cd`, `export`, etc. as builtins (these will come with `$ctx`)
- **I/O redirection operators:** No `>`, `>>`, `<` syntax (use `write-file`, `read-file` commands)
- **`!!` history expansion:** Not in scope
- **Windows cmd.exe support:** Focus on Unix first; Windows can follow
- **Strict stderr encoding:** Lossy UTF-8 for now; strict handling deferred until bytes type exists

## Technical Considerations

- **Process spawning:** Use `fork`/`exec` on Unix, or `posix_spawn` for efficiency
- **Pipe plumbing:** For OS pipes between externals, create pipe fds before forking
- **Stream-to-stdin:** Need incremental write loop; may need non-blocking I/O to avoid deadlock with bidirectional pipes
- **Stdout streaming:** `!cmd` returns a stream backed by the process stdout fd; reads pull from the fd lazily
- **Job tracking:** Jobs need to be tracked for cleanup on JACL exit (SIGTERM on exit)
- **Signal handling:** JACL may need to handle SIGCHLD for Job completion notification
- **Error detection:** Non-zero exit must be detected; for streams this means checking exit code when stream is exhausted or on error
- **Prelude registration:** `exec` capability needs to be added to the prelude map

## Success Metrics

- External commands can be run and results captured
- Large outputs stream without OOM (`{!cat huge.log | for line { ... }}` works)
- Pipes between JACL and external commands work bidirectionally
- Adjacent external commands use efficient OS pipes
- Sandboxed interpret blocks shell access
- Error handling composes correctly with JACL pipes
- Jobs can be spawned, awaited, and cancelled
- REPL feels like a shell for basic command execution

## Open Questions

1. **Option/keyword syntax:** Need general syntax for options (e.g., `:on-exit "orphan"` for per-Job exit behavior, `:timeout 5000` for exec). Likely `:key value` syntax — need to verify no conflict with `:` mutable binding operator. Blocked until this is designed.

2. **Stream error timing:** When `!cmd` returns a stream but the process fails mid-output, when does the error surface? On next stream pull? At stream exhaustion? Need clear semantics.

3. **REPL fallback styling:** Deferred — revisit when REPL UX is prioritized.

4. **Bytes type:** Lossy UTF-8 works for now, but a future `bytes` type would enable strict stderr handling. Not blocking.
