//! One of two machines editing one document over TCP: unir's store vat (`runtime/unir/
//! unir_store_vat.ll`) run as `peer ROLE LOG2`, its editor a JACL program (theSherwood/unir#62,
//! docs/UNIR_SERVICES.md). Machine 0 listens and machine 1 connects; the vats replicate ref `doc`
//! over two connections between them, machine 1 merging, until both editors have made ref
//! `~done`.
//!
//! The host side follows unir's (`e2e/temen/src/link.rs`): each connection's outgoing direction
//! is a host procedure writing to its socket, and both incoming directions one procedure a reader
//! thread per socket feeds, so the vat's root waits on both in one blocking call.

use std::io::{Read, Write};
use std::net::{Shutdown, TcpListener, TcpStream, ToSocketAddrs};
use std::path::Path;
use std::sync::mpsc::{self, TryRecvError};
use std::sync::{Arc, Mutex};

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

/// One machine's two connections to the other: `inn` carries the other machine's puller into this
/// machine's vat, `out` this machine's puller to the other's vat.
pub struct Link {
    pub role: u8,
    pub inn: Arc<TcpStream>,
    pub out: Arc<TcpStream>,
}

/// A connection's first byte, from the side that dialed it: which of the dialer's two it is.
const DIALER_OUT: u8 = 0;
const DIALER_IN: u8 = 1;

fn tcp(s: TcpStream) -> std::io::Result<Arc<TcpStream>> {
    s.set_nodelay(true)?;
    Ok(Arc::new(s))
}

/// Whether `addr` may be listened on or connected to: only loopback, unless `allow_remote`
/// (theSherwood/unir#82, decision 83). Links are unauthenticated and unencrypted until Unir's
/// cross-host security (theSherwood/unir#71), so a non-loopback address takes an explicit choice.
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
            "{a} is not loopback; links are not yet authenticated or encrypted, so pass --allow-remote to use it"
        )),
        _ => Ok(()),
    }
}

/// Machine 0: waits on `listener` for machine 1's two connections.
pub fn accept(listener: &TcpListener) -> std::io::Result<Link> {
    let (mut inn, mut out) = (None, None);
    while inn.is_none() || out.is_none() {
        let (mut s, _) = listener.accept()?;
        let mut tag = [0u8];
        s.read_exact(&mut tag)?;
        match tag[0] {
            // The dialer's outgoing connection carries its puller into this machine's vat.
            DIALER_OUT => inn = Some(tcp(s)?),
            DIALER_IN => out = Some(tcp(s)?),
            t => return Err(std::io::Error::other(format!("unknown connection tag {t}"))),
        }
    }
    Ok(Link { role: 0, inn: inn.expect("accepted"), out: out.expect("accepted") })
}

/// Machine 1: connects to machine 0 at `addr`, twice.
pub fn connect(addr: impl ToSocketAddrs + Copy) -> std::io::Result<Link> {
    let dial = |tag: u8| -> std::io::Result<Arc<TcpStream>> {
        let mut s = TcpStream::connect(addr)?;
        s.write_all(&[tag])?;
        tcp(s)
    };
    let out = dial(DIALER_OUT)?;
    let inn = dial(DIALER_IN)?;
    Ok(Link { role: 1, inn, out })
}

/// A connection's outgoing direction: a host procedure whose op 0 writes `(ptr, len)` from the
/// caller's window to its socket and returns 0, or -1.
fn sink(sock: Arc<TcpStream>) -> HostCap {
    HostCap::host_proc(0, move || {
        let sock = Arc::clone(&sock);
        let proc: temen_interp::HostProc = Box::new(move |_, args, mem, _| {
            let (Some(mem), [ptr, len, ..]) = (mem, args) else { return Ok(vec![-1]) };
            let ok = mem
                .read_bytes(*ptr as u64, *len as u64)
                .is_some_and(|b| (&*sock).write_all(&b).is_ok());
            Ok(vec![if ok { 0 } else { -1 }])
        });
        (proc, temen_interp::CapState::Uncaptured)
    })
}

/// Both connections' incoming directions, one host procedure a reader thread per socket feeds:
/// op 0 reads up to `(ptr, len)` into the caller's window and returns `(connection << 32) |
/// count`, a count of 0 at that connection's close; -2 if nothing is queued yet, and the guest
/// polls; -1 on failure. It never waits: a host procedure runs holding the root's host lock, which
/// the vat's senders need to write, so a root waiting here for the other machine's bytes could
/// keep its own from ever being sent. Both machines did, at the end of replication, given few
/// enough CPUs (two).
fn links(socks: [Arc<TcpStream>; 2]) -> HostCap {
    let (tx, rx) = mpsc::channel::<(usize, Vec<u8>)>();
    for (i, sock) in socks.into_iter().enumerate() {
        let tx = tx.clone();
        std::thread::spawn(move || {
            let mut buf = vec![0; 1 << 16];
            loop {
                let n = (&*sock).read(&mut buf).unwrap_or(0);
                if tx.send((i, buf[..n].to_vec())).is_err() || n == 0 {
                    return;
                }
            }
        });
    }
    let queue = Arc::new(Mutex::new(rx));
    HostCap::host_proc(0, move || {
        let queue = Arc::clone(&queue);
        // A delivery longer than the caller's buffer, and how much of it was handed over.
        let mut rest: Option<((usize, Vec<u8>), usize)> = None;
        let proc: temen_interp::HostProc = Box::new(move |_, args, mem, _| {
            let (Some(mem), [ptr, len, ..]) = (mem, args) else { return Ok(vec![-1]) };
            if rest.is_none() {
                let got = match queue.lock().expect("the readers' queue").try_recv() {
                    Err(TryRecvError::Empty) => return Ok(vec![-2]),
                    r => r.ok(),
                };
                let Some(d) = got else { return Ok(vec![-1]) };
                rest = Some((d, 0));
            }
            let ((i, bytes), at) = rest.take().expect("taken above");
            let n = (bytes.len() - at).min(*len as usize);
            if mem.write_bytes(*ptr as u64, &bytes[at..at + n]).is_none() {
                return Ok(vec![-1]);
            }
            if at + n < bytes.len() {
                rest = Some(((i, bytes), at + n));
            }
            Ok(vec![((i as i64) << 32) | n as i64])
        });
        (proc, temen_interp::CapState::Uncaptured)
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

/// Runs one machine over `link` on `backend` until both editors are done, and returns its output:
/// whatever its editor printed, then the vat's report (its head's id and text, its editor's and
/// puller's statuses, its senders'). With `dir`, the vat keeps its store in `dir/store.log`
/// (theSherwood/unir#86): a machine run again on it picks up where it left off. `setup` sees the
/// root's host before the run, to attach live I/O (`Host::set_stdin_source`,
/// `Host::set_stdout_tee`): the editor reads what the root's stdin holds after the vat's
/// command, and prints to the root's stdout.
pub fn run(
    images: &Images,
    link: Link,
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
    let socks = [Arc::clone(&link.inn), Arc::clone(&link.out)];
    let mut caps = vec![
        ("link.in", sink(Arc::clone(&link.inn))),
        ("link.out", sink(Arc::clone(&link.out))),
        ("links", links([Arc::clone(&link.inn), Arc::clone(&link.out)])),
        ("entropy", entropy()),
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
    // Ends the reader threads: both machines are done with the connections.
    for s in socks {
        let _ = s.shutdown(Shutdown::Both);
    }
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
