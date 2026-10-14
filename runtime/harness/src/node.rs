//! One of two machines editing one document over TCP: unir's store vat (`runtime/unir/
//! unir_store_vat.ll`) run as `peer ROLE LOG2`, its editor a JACL program (theSherwood/unir#62,
//! docs/UNIR_SERVICES.md). Machine 0 listens and machine 1 connects, once: the vats replicate ref
//! `doc` over that one connection in both directions (unir#110), machine 1 merging, until both
//! editors have made ref `~done`. Each authenticates the other with keys (unir#71): a keystore
//! holding what the other machine delegated to it, and the other machine's key, which the store
//! unit's `pair` mode makes for two machines ([`pair`]), since a host here has none of unir's code.
//!
//! The host side follows unir's (`e2e/temen/src/link.rs`): the connection is one host procedure,
//! writing to its socket and reading what a reader thread queued from it.

use std::io::Read;
use std::net::{Shutdown, TcpListener, TcpStream, ToSocketAddrs};
use std::path::Path;
use std::sync::mpsc::{self, Receiver, TryRecvError};
use std::sync::{Arc, Mutex};
use std::time::Instant;

use temen_run::{Backend, HostCap, RunConfig};

/// The vendored store unit.
pub const STORE_VAT_LL: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../unir/unir_store_vat.ll");

/// The store unit twice: as the root (the vat), and as the child it spawns its pullers and
/// senders from, granted to it as `store-self`.
pub fn store_images() -> (temen_ir::Module, temen_ir::Module) {
    let translate = |child_entry| {
        let opts = temen_llvm::TranslateOptions { child_entry, ..Default::default() };
        temen_llvm::translate_ll_path_with_options(Path::new(STORE_VAT_LL), opts)
            .expect("translate unir_store_vat.ll")
            .module
    };
    (translate(false), translate(true))
}

/// One machine's connection to the other.
pub struct Link {
    pub role: u8,
    pub sock: Arc<TcpStream>,
}

fn tcp(s: TcpStream) -> std::io::Result<Arc<TcpStream>> {
    s.set_nodelay(true)?;
    Ok(Arc::new(s))
}

/// Whether `addr` may be listened on or connected to: only loopback, unless `allow_remote`
/// (theSherwood/unir#82, decision 83), so that reaching another host is an explicit choice.
pub fn permitted(addr: &str, allow_remote: bool) -> Result<(), String> {
    let addrs: Vec<_> = addr
        .to_socket_addrs()
        .map_err(|e| format!("{addr}: {e}"))?
        .collect();
    if addrs.is_empty() {
        return Err(format!("{addr}: no address"));
    }
    match addrs.iter().find(|a| !a.ip().is_loopback()) {
        Some(a) if !allow_remote => Err(format!(
            "{a} is not loopback; pass --allow-remote to reach another host"
        )),
        _ => Ok(()),
    }
}

/// Machine 0: waits on `listener` for machine 1's connection.
pub fn accept(listener: &TcpListener) -> std::io::Result<Link> {
    let (s, _) = listener.accept()?;
    Ok(Link { role: 0, sock: tcp(s)? })
}

/// Machine 1: connects to machine 0 at `addr`.
pub fn connect(addr: impl ToSocketAddrs) -> std::io::Result<Link> {
    Ok(Link { role: 1, sock: tcp(TcpStream::connect(addr)?)? })
}

/// Starts a thread reading `sock` to its close, queueing each read, and the close as an empty
/// one.
fn reader(sock: &Arc<TcpStream>) -> Receiver<Vec<u8>> {
    let (tx, rx) = mpsc::channel();
    let sock = Arc::clone(sock);
    std::thread::spawn(move || {
        let mut buf = vec![0; 1 << 16];
        loop {
            let n = (&*sock).read(&mut buf).unwrap_or(0);
            if tx.send(buf[..n].to_vec()).is_err() || n == 0 {
                return;
            }
        }
    });
    rx
}

/// The connection (the guest's `Link`): op 0 writes `(ptr, len)` from the caller's window and
/// returns 0, or -1; op 1 reads up to `(ptr, len)` of what the reader thread queued into the
/// caller's window and returns the count, 0 at the close, -2 if nothing is queued yet, -1 on
/// failure; op 2 shuts the socket down, its writing side with 0, both with 1. Reading never
/// waits: a host procedure runs holding the root's host lock, which the vat's senders need to
/// write, so a root waiting here for the other machine's bytes could keep its own from ever
/// being sent. The guest polls.
fn link(sock: Arc<TcpStream>) -> HostCap {
    let queue = Arc::new(Mutex::new(reader(&sock)));
    HostCap::host_proc(0, move || {
        let (sock, queue) = (Arc::clone(&sock), Arc::clone(&queue));
        // A delivery longer than the caller's buffer, and how much of it was handed over.
        let mut rest: Option<(Vec<u8>, usize)> = None;
        let proc: temen_interp::HostProc = Box::new(move |op, args, mem, _| {
            use std::io::Write;
            let r = match (op, args, mem) {
                (0, [ptr, len, ..], Some(mem)) => match mem.read_bytes(*ptr as u64, *len as u64) {
                    Some(b) if (&*sock).write_all(&b).is_ok() => 0,
                    _ => -1,
                },
                (1, [ptr, len, ..], Some(mem)) => {
                    if rest.is_none() {
                        let got = match queue.lock().expect("the reader's queue").try_recv() {
                            Err(TryRecvError::Empty) => return Ok(vec![-2]),
                            r => r.ok(),
                        };
                        let Some(d) = got else { return Ok(vec![-1]) };
                        rest = Some((d, 0));
                    }
                    let (bytes, at) = rest.take().expect("taken above");
                    let n = (bytes.len() - at).min(*len as usize);
                    if mem.write_bytes(*ptr as u64, &bytes[at..at + n]).is_none() {
                        return Ok(vec![-1]);
                    }
                    if at + n < bytes.len() {
                        rest = Some((bytes, at + n));
                    }
                    n as i64
                }
                (2, [how, ..], _) => {
                    let _ = sock.shutdown(if *how == 0 { Shutdown::Write } else { Shutdown::Both });
                    0
                }
                _ => -1,
            };
            Ok(vec![r])
        });
        (proc, temen_interp::CapState::Uncaptured)
    })
}

/// A machine's keys: its encoded keystore, holding what the other machine delegated to it, and
/// the other machine's key.
#[derive(Clone)]
pub struct Keys {
    pub keystore: Vec<u8>,
    pub peer: [u8; 32],
}

impl Keys {
    /// A machine's line of `pair`'s output, as [`keep`] writes it: `machine R KEYSTORE PEER`, in
    /// hex.
    fn parse(line: &str) -> Option<(u8, Keys)> {
        let hex = |h: &str| -> Option<Vec<u8>> {
            (h.len() % 2 == 0).then_some(())?;
            (0..h.len())
                .step_by(2)
                .map(|i| u8::from_str_radix(h.get(i..i + 2)?, 16).ok())
                .collect()
        };
        let mut w = line.split_whitespace();
        let (Some("machine"), Some(r), Some(ks), Some(peer), None) =
            (w.next(), w.next(), w.next(), w.next(), w.next())
        else {
            return None;
        };
        let keys = Keys { keystore: hex(ks)?, peer: hex(peer)?.try_into().ok()? };
        Some((r.parse().ok()?, keys))
    }
}

/// Two machines' keys, made by the store unit `vat` in its `pair` mode on `backend`: fresh keys,
/// each machine delegating its store to the other until one of them rotates its key.
pub fn pair(vat: &temen_ir::Module, backend: Backend) -> Result<[Keys; 2], String> {
    let inst = temen_run::instantiate(vat.clone()).map_err(|e| format!("instantiate: {e}"))?;
    let config = RunConfig {
        memory_size_log2: vat.memory.map(|m| m.size_log2 + 8),
        stdin: b"pair".to_vec(),
        ..Default::default()
    };
    let caps = vec![("entropy", entropy())];
    let out = inst
        .run_with_caps_and_host(backend, &config, &caps, None)
        .map_err(|e| format!("pair: {e}"))?;
    let out = String::from_utf8_lossy(&out.stdout).into_owned();
    let mut keys: [Option<Keys>; 2] = [None, None];
    for (r, k) in out.lines().filter_map(Keys::parse) {
        if let Some(slot) = keys.get_mut(r as usize) {
            *slot = Some(k);
        }
    }
    match keys {
        [Some(a), Some(b)] => Ok([a, b]),
        _ => Err(format!("pair: {out}")),
    }
}

/// Keeps machine `role`'s keys in `dir`, as `keys`.
pub fn keep(dir: &Path, role: u8, k: &Keys) -> Result<(), String> {
    let hex = |b: &[u8]| b.iter().map(|b| format!("{b:02x}")).collect::<String>();
    let line = format!("machine {role} {} {}\n", hex(&k.keystore), hex(&k.peer));
    std::fs::write(dir.join("keys"), line).map_err(|e| format!("{}: {e}", dir.display()))
}

/// Machine `role`'s keys, kept in `dir` ([`keep`]).
pub fn kept(dir: &Path, role: u8) -> Result<Keys, String> {
    let path = dir.join("keys");
    let line = std::fs::read_to_string(&path).map_err(|e| format!("{}: {e}", path.display()))?;
    match Keys::parse(&line) {
        Some((r, k)) if r == role => Ok(k),
        Some((r, _)) => Err(format!("{} holds machine {r}'s keys, not {role}'s", path.display())),
        None => Err(format!("{}: not a machine's keys", path.display())),
    }
}

/// A machine's keys (the guest's `Keys`): op 1 returns the length of its encoded keystore, and
/// op 2 writes it to `(ptr, len)`; op 0 writes the other machine's key to `(ptr, 32)`, and op 4
/// the key the machine's names vouch for, the same here; op 3 returns 0, the root key never
/// rotating; op 5 returns -1, introducing no petnames.
fn keys(k: Keys) -> HostCap {
    HostCap::host_proc(0, move || {
        let k = k.clone();
        let give: temen_interp::HostProc = Box::new(move |op, args, mem, _| {
            let put = match (op, args) {
                (1, _) => return Ok(vec![k.keystore.len() as i64]),
                (3, _) => return Ok(vec![0]),
                (0 | 4, [ptr, 32, ..]) => Some((*ptr, k.peer.to_vec())),
                (2, [ptr, len, ..]) if *len as usize == k.keystore.len() => {
                    Some((*ptr, k.keystore.clone()))
                }
                _ => None,
            };
            let written = match (put, mem) {
                (Some((at, b)), Some(mem)) => mem.write_bytes(at as u64, &b).is_some(),
                _ => false,
            };
            Ok(vec![if written { 0 } else { -1 }])
        });
        (give, temen_interp::CapState::Uncaptured)
    })
}

/// The time (the guest's `Clock`): op 0 the nanoseconds since `epoch`, op 1 the seconds since the
/// Unix epoch, which a token's validity is in.
fn clock(epoch: Instant) -> HostCap {
    HostCap::host_proc(0, move || {
        let tick: temen_interp::HostProc = Box::new(move |op, _, _, _| {
            Ok(vec![match op {
                1 => std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .map_or(0, |d| d.as_secs() as i64),
                _ => epoch.elapsed().as_nanos() as i64,
            }])
        });
        (tick, temen_interp::CapState::Stateless)
    })
}

/// 64 random bits from the OS, a host procedure's op 0: each store's session source, so the two
/// machines' chunk ids never collide.
fn entropy() -> HostCap {
    HostCap::host_proc(0, || {
        let draw: temen_interp::HostProc = Box::new(|_, _, _, _| {
            let mut b = [0u8; 8];
            let ok = std::fs::File::open("/dev/urandom").and_then(|mut f| f.read_exact(&mut b));
            Ok(vec![if ok.is_ok() { i64::from_le_bytes(b) } else { 0 }])
        });
        (draw, temen_interp::CapState::Stateless)
    })
}

/// What a machine runs: the store unit as root and as child ([`store_images`]), and its editor,
/// a JACL image with the child entry, and its window's size, log2.
pub struct Images {
    pub vat: temen_ir::Module,
    pub helper: temen_ir::Module,
    pub editor: temen_ir::Module,
    pub editor_log2: u8,
}

/// Runs one machine over `link` with `keys` on `backend` until both editors are done, and returns
/// its output:
/// whatever its editor printed, then the vat's report (its head's id and text, its editor's and
/// puller's statuses, its senders'). With `dir`, the vat keeps its store in `dir/store.log`
/// (theSherwood/unir#86): a machine run again on it picks up where it left off. `setup` sees the
/// root's host before the run, to attach live I/O (`Host::set_stdin_source`,
/// `Host::set_stdout_tee`): the editor reads what the root's stdin holds after the vat's
/// command, and prints to the root's stdout.
pub fn run(
    images: &Images,
    link: Link,
    keys: Keys,
    backend: Backend,
    dir: Option<&Path>,
    setup: &mut dyn FnMut(&mut temen_interp::Host),
) -> String {
    let inst = temen_run::instantiate(images.vat.clone()).expect("instantiate the store vat");
    let vat_log2 = images.vat.memory.map(|m| m.size_log2).expect("a window") + 8;
    let log2 = images.editor_log2;
    let config = RunConfig {
        memory_size_log2: Some(vat_log2),
        stdin: format!("peer {} {log2}", link.role).into_bytes(),
        ..Default::default()
    };
    let sock = Arc::clone(&link.sock);
    let mut caps = vec![
        ("link", self::link(Arc::clone(&link.sock))),
        ("entropy", entropy()),
        ("clock", clock(Instant::now())),
        ("keys", self::keys(keys)),
    ];
    if let Some(dir) = dir {
        caps.push(("fs", temen_run::fs::host_fs(dir.to_path_buf())));
    }
    let mut grant = |h: &mut temen_interp::Host| {
        let m = h.grant_module(&images.editor);
        h.register_cap_name("store-client", m);
        let m = h.grant_module(&images.helper);
        h.register_cap_name("store-self", m);
        h.grant_instantiator(0, 1 << log2);
        h.grant_budget(-1, -1, -1);
        setup(h);
    };
    let out = match inst.run_with_caps_and_host(backend, &config, &caps, Some(&mut grant)) {
        Ok(run) => String::from_utf8_lossy(&run.stdout).into_owned(),
        Err(e) => format!("run failed: {e}\n"),
    };
    // Ends the reader thread: both machines are done with the connection.
    let _ = sock.shutdown(Shutdown::Both);
    out
}

#[cfg(test)]
mod tests {
    use super::permitted;

    #[test]
    fn only_loopback_unless_allowed() {
        assert!(permitted("127.0.0.1:7000", false).is_ok());
        assert!(permitted("[::1]:7000", false).is_ok());
        assert!(permitted("10.0.0.1:7000", false).is_err());
        assert!(permitted("0.0.0.0:7000", false).is_err());
        assert!(permitted("10.0.0.1:7000", true).is_ok());
        assert!(permitted("no-port", false).is_err());
    }
}
