# `$ctx` and `$env` across vat boundaries (theSherwood/unir#20)

**Status:** implemented (2026-09-29). Unir's one constraint (unir spec §12.3, §13.1): a child receives
only what its endowment grants. Nothing reaches it implicitly across a spawn or an `interpret`
boundary.

## What each one holds

| | `$ctx` | `$env` |
|---|---|---|
| what | dynamic scope inside one vat: request context, tracing, the environment for the programs this task starts | this vat's environment: the configuration it was spawned with |
| values | any JACL value, capabilities included | strings only (a map `str → str`) |
| mutable | yes: `set $ctx->f`, `with-ctx`, forked at `spawn` / `parallel` / `race` (SYNTAX.md) | no |
| crosses a vat boundary | never | only as a new vat's endowment, when its spawner passes one |

`$ctx` never leaves its vat: it is not serialized into a stage's spawn payload, and `interpret`
ships only the source and the allowed names. A stage and an interpreted program start with an empty
`$ctx`. Inside a vat, tasks inherit it as SYNTAX.md describes (a snapshot at `spawn`), which the
work-stealing runtime already keeps per task.

`$env` holds only strings because it is data handed to another vat. Capabilities reach a child as
named grants in its endowment (`bin.<name>`, the stdio edges), where the spawner names each one;
a capability in `$env` would be a second, unnamed way to pass authority.

## How `$env` reaches a child: explicitly, in its endowment

A program spawned from JACL gets an **empty** environment unless the spawner passes one:

```jacl
!prog                                  # $env is {} in prog
with-env {DEBUG 1} { !prog }           # $env is {DEBUG "1"} in prog, and nothing else
with-env $env { !prog }                # pass this vat's own environment through
with-env [map-set $env DEBUG 1] { … }  # pass it through with one change
```

`with-env M { body }` sets the environment for every program started in `body`, M exactly: it is
the map the children are spawned with, not a change to this vat's `$env`. It is sugar for
`with-ctx {env M} { body }`: the pending children's environment is the `$ctx` field `env`, so it
follows `$ctx`'s scoping (a `spawn` in `body` inherits it; it is restored when `body` ends). Keys and
values are converted to strings.

This is the issue's recommendation, "fold `$env` into the endowment map", made concrete: a child's
environment is part of what its spawn endows, next to its named grants and its arguments. There is
no inheritance by default, so there is no ambient channel to gate.

A vat's own `$env`:

- **A stage:** the environment its spawner passed (empty by default).
- **The root vat:** the host's environment block (temen DESIGN §3e: the `envc` strings of the
  powerbox args blob). That is the root's endowment from its host, which chose what to put there.

## The wire

A stage's arguments travel as `unir_spawn`'s argument bytes (op 15's payload): the value
`[argv env]`, encoded by the value codec (theSherwood/unir#43; `runtime/value.c`), which the stage
verifies before it decodes. `argv[0]` is the program name, and the other arguments arrive as the
values passed (docs/UNIR_PIPELINES.md, "One codec"); `env` is the string map above. The root's
environment is still the host's §3e blob (`{argc, envc}`, then NUL-terminated strings, the
environment's as `KEY=VALUE`).

A stage's payload is bounded by the args area (16,224 bytes); an overflow is an error rather than
a truncation.

## Not here

- The OS-synced `$env` atom of SYNTAX.md (`swap $env …` writing through to a process environment):
  a vat has no OS environment, and an environment that changes under its children would be ambient.
- `$ctx.pwd` and `with-dir`: a working directory is a filesystem capability's concern (§13.1: the
  environment is the namespace), which the versioned store (stage 3) settles.
- `$home` / `$pwd` / `$pid`.

## Tests

`runtime/harness/tests/pipelines.jacl` (`codegen.rs::pipelines_run_on_temen`, all three engines),
with the fixture `envdump`, which prints the sizes of its `$ctx` and `$env` and the named entries of
its `$env`:

- The root's `$env` holds the host's environment block (the harness sets `JACL_TEST_ENV`).
- A stage sees an empty `$ctx` and an empty `$env`, whatever the parent's `$ctx` and `$env` hold.
- `with-env {…}` gives a stage exactly that map; `with-env $env` passes the root's through; after the
  block, programs get an empty environment again.
- A `spawn` inside `with-env` passes the same environment to the programs it starts.
