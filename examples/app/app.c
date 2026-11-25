/* An application's own C beside a JACL program (theSherwood/jacl_impl#290). JACL calls it through an
 * `extern`: the sum of the `n` i64 at `xs`, plus `k`. */
#include <stdint.h>

int64_t app_sum(const int64_t *xs, int64_t n, int64_t k) {
  for (int64_t i = 0; i < n; i++) k += xs[i];
  return k;
}
