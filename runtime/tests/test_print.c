/* Minimal powerbox proof: a `main(void)` program that writes to stdout via the libc
 * `write` (svm-llvm lowers it to a Stream.write cap.call on the stashed stdout handle,
 * and synthesizes the `_start` powerbox entry as function 0). The harness grants the
 * powerbox and captures stdout. No JACL runtime — this validates the host-I/O path
 * before wiring a real `print` builtin. */
int write(int fd, const char *buf, long n);

int main(void) {
  const char *s = "hi\n";
  write(1, s, 3);
  return 0;
}
