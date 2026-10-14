/* clockcap.c — the "clock" named-capability adapter for `[clock]`.
 *
 * temen's own clock is deterministic, so a time that means anything outside the guest is the
 * host's, granted by name as `exec` is (unir spec §11: no ambient authority). Op 0 is nanoseconds
 * since the host's epoch: in unir's session, the clock the host stamps its keys and frames with,
 * so a job can say when it wrote what a frame shows (unir's session benchmark, #141). Ungranted,
 * `[clock]` is a catchable error. Structurally a sibling of execcap.c. */
#include <stdint.h>

extern int  __vm_cap_resolve(const char *name, long len);
extern long __vm_host_call(int h, int op, long a, long b, long c, long d);

/* Probe once, cache the handle. -2 = unprobed; negative = not granted; >=0 = the handle. */
static int jacl_clock_handle = -2;

/* `[clock]` — nanoseconds since the host's epoch. */
JaclVal jacl_clock(void) {
  if (jacl_clock_handle == -2) jacl_clock_handle = __vm_cap_resolve("clock", 5);
  if (jacl_clock_handle < 0) return jacl_error_new(jacl_str_new("clock: no clock granted", 23));
  return jacl_i64_box(__vm_host_call(jacl_clock_handle, 0, 0, 0, 0, 0));
}
