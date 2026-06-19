/* P3.5 — the JACL runtime `print` builtin through the powerbox. A `main(void)` program
 * (so svm-llvm synthesizes the `_start` powerbox entry) prints a string and an int via
 * jacl_print, which writes to stdout through the granted Stream capability. The harness
 * (run_powerbox) grants the powerbox and captures stdout on interp + jit. Expected:
 * "hello\n42\n". */
#include "jaclrt.h"

int main(void) {
  jacl_heap_init();
  jacl_intern_init();
  jacl_print(jacl_str_new("hello", 5));   /* inline string (<=7), no heap alloc */
  jacl_print(jaclrt_i32(42));
  return 0;
}

#include "jaclrt.c"   /* runtime impl last */
