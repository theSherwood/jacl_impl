//! The JACL shell's REPL (jacl_impl#204): a line compiled in the shell, by the compiler linked into
//! its image, and run in the shell's own domain on the §22 Jit capability.

use std::path::Path;

const SPIKE_JACL: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/tests/services/repl_spike.jacl"
);

/// What `repl_spike.jacl` prints: each line's value; a macro's definition refused, and the next
/// line not seeing it; and the last line's compile error, which the refusal did not change.
const SPIKE_OUT: &str = "41\nnil\n42\n0\n2\n2\n<closure>\n7\n\
    nil\n[Point x 3 y 4]\n7\nnil\n30\n[Point x 3 y 4]\n\
    <error: repl-eval: a macro cannot be defined at the prompt yet>\n\
    <error: codegen: unsupported command head 'unless'>\n\
    <error: codegen: unsupported command head 'nope'>\n";

#[test]
fn a_repl_line_sees_what_earlier_lines_defined() {
    let (image, _) =
        jacl_runtime_harness::repl::shell_image(Path::new(SPIKE_JACL), false).expect("the image");
    let mut failed = Vec::new();
    for backend in [
        temen_run::Backend::TreeWalk,
        temen_run::Backend::Bytecode,
        temen_run::Backend::Jit,
    ] {
        let inst = temen_run::instantiate(image.clone()).expect("instantiate the shell");
        match inst.run(backend, &temen_run::RunConfig::default()) {
            Ok(run) if run.stdout == SPIKE_OUT.as_bytes() => {}
            Ok(run) => {
                let out = String::from_utf8_lossy(&run.stdout);
                println!("{backend:?}: {:?}\n{out}", run.outcome);
                failed.push(backend);
            }
            Err(e) => {
                println!("{backend:?}: {e}");
                failed.push(backend);
            }
        }
    }
    assert!(failed.is_empty(), "failed on {failed:?}");
}

#[test]
fn a_line_yields_at_its_loops() {
    // #218: a line's loops are job-control points, once a slice of their headers has run. The
    // line's task yields there, so a task it started runs while it loops; and a generator's loop,
    // on a fiber of its own, yields only its elements.
    let path = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/tests/services/repl_loops.jacl"
    );
    let (image, _) =
        jacl_runtime_harness::repl::shell_image(Path::new(path), false).expect("the image");
    for backend in [
        temen_run::Backend::TreeWalk,
        temen_run::Backend::Bytecode,
        temen_run::Backend::Jit,
    ] {
        let inst = temen_run::instantiate(image.clone()).expect("instantiate the shell");
        let run = inst
            .run(backend, &temen_run::RunConfig::default())
            .expect("run");
        assert_eq!(
            String::from_utf8_lossy(&run.stdout),
            "1\n40000\n",
            "{backend:?}: {:?}",
            run.outcome
        );
    }
}

#[test]
fn a_session_outlasts_the_jit_tables() {
    // 1,000 lines, each compiled into a unit of its own. A line's compiled code is a capability,
    // and the capability table holds 256 (temen §22), so the REPL gives each up once its unit is
    // installed; and the dispatch table has room for about 700 units, so a line that leaves
    // nothing referring to its code gives its slot back once it returns. A line that defines a
    // proc keeps its own, so the next line calls it.
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/services/repl_long.jacl");
    let (image, _) =
        jacl_runtime_harness::repl::shell_image(Path::new(path), false).expect("the image");
    let inst = temen_run::instantiate(image).expect("instantiate the shell");
    let run = inst
        .run(temen_run::Backend::Jit, &temen_run::RunConfig::default())
        .expect("run");
    assert_eq!(
        String::from_utf8_lossy(&run.stdout),
        "1000\n7\n",
        "{:?}",
        run.outcome
    );
}

const SHELL_JACL: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/services/shell.jacl");
const PROGRAMS: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/pipelines");

/// The scripted terminal for `shell.jacl`: each step's keys, typed once the terminal has been sent
/// the step's marker at least that many times, so the script follows the shell rather than a clock.
const SHELL_SCRIPT: &[(&str, &str, usize)] = &[
    ("def x 41\r", "$ ", 1),
    ("proc sq {n} { * $n $n }\r", "\x1b[1mdef x 41", 1),
    ("sq [+ $x 1]\r", "\x1b[1mproc sq", 1),
    // Up, then Enter: the line before again.
    ("\x1b[A\r", "1764", 1),
    ("!gen hi | !upcase\r", "1764", 2),
    ("!nums 3 maps\r", "HI", 1),
    // A value of a schema: a reference, which the driver underlines.
    (
        "emit [map \"name\" \"notes.txt\"] [schema \"link\"]\r",
        "\x1b[1m!nums 3 maps",
        1,
    ),
    // Keys typed while the shell waits for a job are its input; those it leaves come back to the
    // prompt once it ends, and are erased there.
    (
        "sleep 1; concat \"in \" [to-string [length [deref $ctx->in]]]\rab",
        "\x1b[4mnotes.txt",
        1,
    ),
    // A job in the background, listed, then ended.
    ("\x7f\x7f!forever | !sink &\rjobs\r", "$ ab", 1),
    ("kill\r", "running", 1),
    // A job waited for, stopped at once (Ctrl-Z) and listed; another stopped, then resumed in the
    // background (`bg`), where it finishes, its output in its block; then the first waited for
    // again (`fg 10`) and ended (Ctrl-C). A Ctrl key is typed only while no job is waited for, so
    // it acts on the job its own line started.
    ("!forever | !upcase\r\x1ajobs\r", "cancelled", 1),
    (
        "sleep 2; print [concat \"wo\" \"ke\"]\r\x1ajobs\r",
        "stopped  !forever | !upcase",
        1,
    ),
    ("bg\rjobs\r", "stopped  sleep 2", 1),
    ("fg 10\r\x03", "woke", 1),
    ("nope\r", "cancelled", 2),
    // #218: lines that loop without reaching a job-control point of their own (an await, a sleep, a
    // pipeline's read) act on Ctrl-C and Ctrl-Z at their loops' headers. The keys typed after a
    // Ctrl key are the job's input until it ends, then come back to the prompt: the first line,
    // ended by Ctrl-C, starts the second, which Ctrl-Z stops; it is waited for again (`fg`) and
    // Ctrl-C ends it.
    (
        "print [concat \"spin\" \"ning\"]; while $true {}\r",
        "unsupported command head",
        1,
    ),
    (
        "\x03print [concat \"loop\" \"ing\"]; while $true {}\r",
        "spinning",
        1,
    ),
    ("\x1ajobs\r", "looping", 1),
    (
        "fg\r\x03quit\r",
        "stopped  print [concat \"loop\" \"ing\"]",
        1,
    ),
];

fn is_jacl_error(v: i64) -> bool {
    (v as u64) & (1u64 << 61) != 0
}

#[test]
fn the_shell_runs_each_line_in_its_own_domain() {
    // #204: a REPL in a terminal pane. Each line is a job: compiled in the shell and run in its
    // domain, on the Jit capability unir's store vat passes it, as a task whose output goes to its
    // blocks of the transcript. A line sees what the lines before it defined; programs run as jobs,
    // their typed output shown as values; and jobs run in the background, stop, resume and end.
    let (client, log2) =
        jacl_runtime_harness::repl::shell_image(Path::new(SHELL_JACL), true).expect("the image");
    let programs: Vec<(&str, temen_ir::Module)> = ["gen", "upcase", "nums", "forever", "sink"]
        .into_iter()
        .map(|name| {
            let path = format!("{PROGRAMS}/{name}.jacl");
            let (image, _) = jacl_runtime_harness::child_image(Path::new(&path))
                .unwrap_or_else(|e| panic!("{path}: {e}"));
            (name, image)
        })
        .collect();
    let programs = std::sync::Arc::new(programs);
    let mut failed = Vec::new();
    for backend in [
        temen_run::Backend::TreeWalk,
        temen_run::Backend::Bytecode,
        temen_run::Backend::Jit,
    ] {
        let (client, programs) = (client.clone(), std::sync::Arc::clone(&programs));
        // Each engine on its own thread, so one's failure is reported and the rest still run.
        if std::thread::spawn(move || shell_session(&client, log2, &programs, backend))
            .join()
            .is_err()
        {
            failed.push(backend);
        }
    }
    assert!(failed.is_empty(), "failed on {failed:?}");
}

/// One scripted session of `shell.jacl` in a pane on `backend`, checked.
fn shell_session(
    client: &temen_ir::Module,
    log2: u8,
    programs: &[(&'static str, temen_ir::Module)],
    backend: temen_run::Backend,
) {
    use jacl_runtime_harness::pane;
    use std::sync::{Arc, Mutex};
    let dir = std::env::temp_dir().join(format!("jacl_shell_{}_{backend:?}", std::process::id()));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).expect("a scratch directory");
    let shown = Arc::new(Mutex::new(String::new()));
    let (tee, keys) = (Arc::clone(&shown), Arc::clone(&shown));
    let (client, dir2, programs) = (client.clone(), dir.clone(), programs.to_vec());
    let (done, finished) = std::sync::mpsc::channel();
    std::thread::spawn(move || {
        let mut step = 0;
        let mut terminal = |h: &mut temen_interp::Host| {
            let tee = Arc::clone(&tee);
            h.set_stdout_tee(Box::new(move |b| {
                tee.lock().unwrap().push_str(&String::from_utf8_lossy(b))
            }));
            let keys = Arc::clone(&keys);
            h.set_stdin_source(Box::new(move || {
                let (typed, marker, times) = *SHELL_SCRIPT.get(step)?;
                if keys.lock().unwrap().matches(marker).count() < times {
                    return None;
                }
                step += 1;
                Some(typed.as_bytes().to_vec())
            }));
        };
        let size = pane::Size { rows: 24, cols: 80 };
        let out = pane::run(
            &client,
            log2,
            &programs,
            backend,
            &dir2,
            size,
            &mut terminal,
        );
        let _ = done.send(out);
    });
    let out = finished
        .recv_timeout(std::time::Duration::from_secs(900))
        .unwrap_or_else(|_| {
            panic!(
                "{backend:?}: the shell never finished; it showed {:?}",
                shown.lock().unwrap()
            )
        });
    let _ = std::fs::remove_dir_all(&dir);
    println!("{backend:?}: {out:?}");
    let status = out
        .lines()
        .rev()
        .find_map(|l| l.trim().strip_prefix("shell [terminal, view, client] ["))
        .and_then(|l| l.strip_suffix(']'))
        .map(|l| {
            l.split(", ")
                .map(|n| n.parse::<i64>().unwrap())
                .collect::<Vec<_>>()
        })
        .unwrap_or_else(|| panic!("{backend:?}: no report in {out:?}"));
    assert_eq!(
        status[..2],
        [0, 0],
        "{backend:?}: the terminal or the view failed: {out:?}"
    );
    assert!(
        !is_jacl_error(status[2]) && status[2] != -1,
        "{backend:?}: shell.jacl failed: {out:?}"
    );
    let at = |s: &str| {
        out.find(s)
            .unwrap_or_else(|| panic!("{backend:?}: no {s:?} in {out:?}"))
    };
    // Each line a block headed by the line in bold, holding its value.
    assert!(
        at("\x1b[1mdef x 41\x1b[0m") < at("\x1b[1msq [+ $x 1]\x1b[0m"),
        "{backend:?}"
    );
    // A line sees the binding and the proc the lines before it defined.
    assert!(at("\x1b[1msq [+ $x 1]\x1b[0m") < at("1764"), "{backend:?}");
    // A line that fails to compile shows why.
    assert!(
        at("\x1b[1mnope\x1b[0m") < at("unsupported command head"),
        "{backend:?}"
    );
    // A value emitted under a schema is a block of it: a reference, underlined.
    assert!(
        at("\x1b[1memit [map") < at("\x1b[4mnotes.txt\x1b[0m"),
        "{backend:?}"
    );
    // A job's input is the keys typed while the shell waits for it; it left them, so they came
    // back to the prompt.
    assert!(
        at("\x1b[1msleep 1; concat") < at("in 2") && at("in 2") < at("$ ab"),
        "{backend:?}"
    );
    // A pipeline's text is its job's output block; a program's values are a block of values.
    assert!(at("\x1b[1m!gen hi | !upcase") < at("HI"), "{backend:?}");
    assert!(
        at("\x1b[1m!nums 3 maps") < at("value: I32(2)"),
        "{backend:?}"
    );
    // Whether a line's value block, its line in bold with the value on the next row, holds the
    // `cancelled` error.
    let cancelled = |line: &str| {
        let head = format!("\x1b[1m{line}\x1b[0m\x1b[");
        out.match_indices(&head).any(|(i, _)| {
            out[i + head.len()..]
                .split_once("H\x1b[K")
                .is_some_and(|(row, value)| {
                    row.bytes().all(|b| b.is_ascii_digit() || b == b';')
                        && value.starts_with("error: cancelled")
                })
        })
    };
    // A job in the background is listed running and ends when killed; one stopped is listed
    // stopped, and ends when it is waited for again and Ctrl-C ends it.
    let (running, stopped) = (
        at("running  !forever | !sink"),
        at("stopped  !forever | !upcase"),
    );
    assert!(
        running < at("error: cancelled") && at("error: cancelled") < stopped,
        "{backend:?}"
    );
    assert!(cancelled("!forever | !upcase"), "{backend:?}");
    // One stopped and resumed in the background is listed stopped, then running, and finishes.
    assert!(
        at("stopped  sleep 2") < at("running  sleep 2") && at("running  sleep 2") < at("woke"),
        "{backend:?}"
    );
    // #218: each looping line ran, and Ctrl-C ended it. The second was listed stopped by Ctrl-Z
    // first.
    let spin = "print [concat \"spin\" \"ning\"]; while $true {}";
    let looping = "print [concat \"loop\" \"ing\"]; while $true {}";
    assert!(
        at("spinning") < at("looping") && cancelled(spin),
        "{backend:?}"
    );
    assert!(
        at("looping") < at(&format!("stopped  {looping}")) && cancelled(looping),
        "{backend:?}"
    );
}
