//! Bells (theSherwood/unir#225, #259, #349): what a host rings when something arrives for a vat,
//! so the vat waits for it rather than polling. A vat waits on its notification, which a thread of
//! its own signals each time its input's bell rings: a machine's root for its link
//! ([`crate::node`]), and unir's terminal vat for stdin ([`capability`], [`crate::pane`]).
//!
//! The wait is an offloadable host procedure's pool job on a condition variable, so a ring from
//! any thread, or from inside a host call, only notifies it.

use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex};

use temen_interp::{CapState, ForkedProc, HostProcGrant, OffloadOutcome, ProcHandler};
use temen_run::HostCap;

/// How many times the bell has rung, the condition a ring wakes, and whether anything has waited
/// on it.
#[derive(Default)]
pub struct Bell {
    rung: Mutex<u64>,
    cv: Condvar,
    waited: AtomicBool,
}

pub fn ring(bell: &Bell) {
    *bell.rung.lock().expect("the bell") += 1;
    bell.cv.notify_all();
}

/// Whether anything has waited on `bell`: for an input's, whether the vat reading the input waits
/// for it to ring rather than polling the input.
pub fn waited(bell: &Bell) -> bool {
    bell.waited.load(Ordering::SeqCst)
}

/// Waits until `bell` has rung more than `seen` times, and returns how many times it has rung.
fn wait(bell: &Bell, seen: u64) -> u64 {
    let rung = bell.rung.lock().expect("the bell");
    *bell.cv.wait_while(rung, |r| *r == seen).expect("the bell")
}

/// A wait on `bell` for the waiter `seen` keeps count for, off its caller's thread, until the bell
/// rings. Answers 1 if it rang since that waiter's last wait returned, else 0, so a ring between
/// two waits ends the next at once and none is lost.
fn wait_for(bell: &Arc<Bell>, seen: &Arc<AtomicU64>) -> OffloadOutcome {
    bell.waited.store(true, Ordering::SeqCst);
    let (bell, seen) = (Arc::clone(bell), Arc::clone(seen));
    OffloadOutcome::Offload(Box::new(move || {
        let was = seen.load(Ordering::SeqCst);
        let now = wait(&bell, was);
        seen.store(now, Ordering::SeqCst);
        i64::from(now != was)
    }))
}

/// `bell` as a capability a vat passes on to its children (theSherwood/unir#259): op 0 waits until
/// it rings, op 1 rings it. Each domain holding it waits as a waiter of its own.
pub fn capability(bell: &Arc<Bell>) -> HostCap {
    let bell = Arc::clone(bell);
    let make = Arc::new(move || -> temen_interp::OffloadHostProc {
        let (bell, seen) = (Arc::clone(&bell), Arc::default());
        Box::new(move |op, _| match op {
            0 => wait_for(&bell, &seen),
            1 => {
                ring(&bell);
                OffloadOutcome::Done(Ok(vec![0]))
            }
            _ => OffloadOutcome::Done(Ok(vec![-1])),
        })
    });
    HostCap::custom(temen_interp::cap_id::HOST_PROC, 0, move |h, _| {
        let fork = Arc::clone(&make);
        h.grant_host_proc(
            HostProcGrant::new(ProcHandler::Offloadable(make()), CapState::Uncaptured).forkable(
                Arc::new(move |_| {
                    Some(ForkedProc::shared(
                        ProcHandler::Offloadable(fork()),
                        CapState::Uncaptured,
                    ))
                }),
            ),
        )
    })
}
