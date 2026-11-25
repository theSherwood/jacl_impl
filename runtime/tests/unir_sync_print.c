/* unir_sync_print.c — a call into unir's C ABI through the unit's static function pointers
 * (jacl_impl#267). `unir_sync_print` formats a sync with Rust's core::fmt, which calls through
 * vtables in the unit's data. The harness translates this file as a link unit and links it with
 * the runtime, whose chan_unir.c serves the unit's heap, and with the unit
 * (runtime/harness/tests/unir_unit.rs). It writes the sync's text to stdout and returns 0, or the
 * step that failed. */
#include "unir/unir_store.h"
#include <stdint.h>
#include <unistd.h>

int64_t unir_sync_print_test(void) {
  static char text[1024];
  unir_store *store = unir_store_open(1, 0, 0);
  if (!store) return -1;
  unir_limits limits = {4096, 4096, 4096};
  unir_sync *sync =
      unir_sync_pull(store, "doc", "me", "home", UNIR_SYNC_MERGE, 1000000000ull, &limits);
  if (!sync) return -2;
  size_t n = unir_sync_print(sync, text, sizeof text);
  unir_sync_free(store, sync);
  unir_store_free(store);
  if (n == 0 || n > sizeof text) return -3;
  if (write(1, text, n) != (ssize_t)n) return -4;
  return 0;
}
