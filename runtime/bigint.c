/* =====================================================================
 * bigint — the third representation of a dynamic integer (jacl #106 slice 6).
 *
 * `docs/TEMEN_NUMERICS.md` § "The integer model": a `dyn` integer is an integer of
 * conceptually arbitrary precision, held in the narrowest of three representations that
 * fits — inline i32, boxed i64, boxed bigint — chosen by magnitude alone. Until this file
 * existed the third tier did not, so `dyn` arithmetic *errored* past `INT64_MAX`, which the
 * docs labelled a violation of the model rather than a rule of it. This is that violation
 * being closed: `dyn` arithmetic now cannot fail on magnitude, only on a domain error.
 *
 * Representation — sign-magnitude, base 2^32, little-endian limbs:
 *
 *   sign   -1 or +1, never 0
 *   n      limb count, >= 1, with no leading zero limb
 *   limb[] limb[0] is least significant
 *
 * Base 2^32 rather than 2^64 so that a limb product fits a `uint64_t`. A 2^64 base would
 * need a 128-bit intermediate, which is not portable C and not something to assume of the
 * VM target.
 *
 * The cell is a JOBJ_BLOB: no outgoing pointers, so the collector needs no new tracing for
 * it and a bigint can never hold a reference alive.
 *
 * **Canonical form is an invariant, not a convention.** A bigint whose value fits an i64
 * must not exist — every construction goes through `jbig_canon`, which demotes. Two
 * consequences are load-bearing: `==` and a map key agree across the whole tower (#107,
 * where a wide-computed 37 hashing differently from an inline 37 was a real lookup miss),
 * and a bigint compared against an i32/i64 can be settled by magnitude alone.
 * ===================================================================== */

/* A bound, deliberately: ~1233 decimal digits. "Arbitrary precision" is the *model*; the
 * implementation states a limit and exceeds it **loudly** (a domain error) rather than
 * wrapping, truncating, or smashing a stack buffer. Raising it is this constant plus moving
 * the multiply scratch off the stack — the scratch is what the bound really protects. */
#define JBIG_MAX_LIMBS   128u
#define JBIG_SCRATCH     (2u * JBIG_MAX_LIMBS + 2u)

typedef struct {
  int32_t  sign;    /* -1 or +1 */
  uint32_t n;       /* limbs in use; limb[n-1] != 0 */
  uint32_t limb[];  /* base 2^32, little-endian */
} JaclBig;

int jacl_is_bigint(JaclVal v) { return jaclrt_type_index(v) == 0x09; }

static JaclBig *jbig_of(JaclVal v) {
  return (JaclBig *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(v));
}

/* Read any dynamic integer as sign + limbs. A bigint's limbs are used in place; an i32 or a
 * boxed i64 is expanded into the caller's two-limb scratch, so callers handle one shape. */
static uint32_t jbig_read(JaclVal v, int32_t *sign, uint32_t *scratch, const uint32_t **limbs) {
  if (jacl_is_bigint(v)) {
    JaclBig *b = jbig_of(v);
    *sign = b->sign; *limbs = b->limb; return b->n;
  }
  int64_t x = jaclrt_is_i32(v) ? (int64_t)jaclrt_as_i32(v) : jacl_wide_bits(v);
  uint64_t u = x < 0 ? 0u - (uint64_t)x : (uint64_t)x;   /* no UB at INT64_MIN */
  *sign = x < 0 ? -1 : 1;
  scratch[0] = (uint32_t)u;
  scratch[1] = (uint32_t)(u >> 32);
  *limbs = scratch;
  return scratch[1] ? 2u : 1u;
}

/* The one constructor. Trims, demotes anything an i64 can hold, and refuses to build past
 * the limb bound. Every bigint in the system comes from here. */
static JaclVal jbig_canon(int32_t sign, const uint32_t *limb, uint32_t n) {
  while (n && !limb[n - 1]) n--;
  if (n == 0) return jaclrt_i32(0);
  if (n <= 2) {
    uint64_t u = (uint64_t)limb[0] | (n == 2 ? ((uint64_t)limb[1] << 32) : 0u);
    if (sign > 0 && u <= (uint64_t)INT64_MAX) return jacl_int_result(0x0E, (int64_t)u);
    if (sign < 0) {
      if (u == (uint64_t)INT64_MAX + 1u) return jacl_int_result(0x0E, INT64_MIN);
      if (u <= (uint64_t)INT64_MAX)      return jacl_int_result(0x0E, -(int64_t)u);
    }
  }
  if (n > JBIG_MAX_LIMBS) return jaclrt_set_error(jaclrt_i32(0));
  JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)(sizeof(JaclBig) + (size_t)n * 4u));
  JaclBig *b = (JaclBig *)jacl_obj_payload(o);
  b->sign = sign; b->n = n;
  for (uint32_t i = 0; i < n; i++) b->limb[i] = limb[i];
  return jaclrt_from_ptr(JACL_TAG_BIGNUM, o);
}

/* ---- magnitude primitives (schoolbook; the operands are small) ---- */

static int jbig_mag_cmp(const uint32_t *a, uint32_t na, const uint32_t *b, uint32_t nb) {
  while (na && !a[na - 1]) na--;
  while (nb && !b[nb - 1]) nb--;
  if (na != nb) return na < nb ? -1 : 1;
  for (uint32_t i = na; i--;) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
  return 0;
}

/* out needs max(na,nb)+1 limbs */
static uint32_t jbig_mag_add(const uint32_t *a, uint32_t na,
                             const uint32_t *b, uint32_t nb, uint32_t *out) {
  if (na < nb) { const uint32_t *t = a; a = b; b = t; uint32_t tn = na; na = nb; nb = tn; }
  uint64_t carry = 0;
  for (uint32_t i = 0; i < na; i++) {
    uint64_t s = (uint64_t)a[i] + carry + (i < nb ? b[i] : 0u);
    out[i] = (uint32_t)s;
    carry = s >> 32;
  }
  out[na] = (uint32_t)carry;
  return na + (carry ? 1u : 0u);
}

/* |a| >= |b| required; out needs na limbs */
static uint32_t jbig_mag_sub(const uint32_t *a, uint32_t na,
                             const uint32_t *b, uint32_t nb, uint32_t *out) {
  int64_t borrow = 0;
  for (uint32_t i = 0; i < na; i++) {
    int64_t d = (int64_t)a[i] - borrow - (int64_t)(i < nb ? b[i] : 0u);
    if (d < 0) { d += (int64_t)1 << 32; borrow = 1; } else borrow = 0;
    out[i] = (uint32_t)d;
  }
  while (na && !out[na - 1]) na--;
  return na;
}

/* out needs na+nb limbs. `out[i+nb]` is untouched by earlier i, so the carry store is an
 * assignment rather than an accumulate. */
static uint32_t jbig_mag_mul(const uint32_t *a, uint32_t na,
                             const uint32_t *b, uint32_t nb, uint32_t *out) {
  for (uint32_t i = 0; i < na + nb; i++) out[i] = 0;
  for (uint32_t i = 0; i < na; i++) {
    uint64_t carry = 0;
    for (uint32_t j = 0; j < nb; j++) {
      uint64_t t = (uint64_t)a[i] * b[j] + out[i + j] + carry;
      out[i + j] = (uint32_t)t;
      carry = t >> 32;
    }
    out[i + nb] = (uint32_t)carry;
  }
  uint32_t n = na + nb;
  while (n && !out[n - 1]) n--;
  return n;
}

/* a /= d in place, returning the remainder. Single-limb divisor only — see jacl_big_divmod. */
static uint32_t jbig_divmod_small(uint32_t *a, uint32_t n, uint32_t d) {
  uint64_t rem = 0;
  for (uint32_t i = n; i--;) {
    uint64_t cur = (rem << 32) | a[i];
    a[i] = (uint32_t)(cur / d);
    rem = cur % d;
  }
  return (uint32_t)rem;
}

/* ---- the operations builtins.c routes to ---- */

static JaclVal jbig_addsub(JaclVal av, JaclVal bv, int negate_b) {
  uint32_t sa[2], sb[2], out[JBIG_SCRATCH];
  const uint32_t *la, *lb;
  int32_t siga, sigb;
  uint32_t na = jbig_read(av, &siga, sa, &la);
  uint32_t nb = jbig_read(bv, &sigb, sb, &lb);
  if (negate_b) sigb = -sigb;
  if (na > JBIG_MAX_LIMBS || nb > JBIG_MAX_LIMBS) return jaclrt_set_error(jaclrt_i32(0));
  if (siga == sigb) return jbig_canon(siga, out, jbig_mag_add(la, na, lb, nb, out));
  int c = jbig_mag_cmp(la, na, lb, nb);
  if (c == 0) return jaclrt_i32(0);
  if (c > 0)  return jbig_canon(siga, out, jbig_mag_sub(la, na, lb, nb, out));
  return jbig_canon(sigb, out, jbig_mag_sub(lb, nb, la, na, out));
}

JaclVal jacl_big_add(JaclVal a, JaclVal b) { return jbig_addsub(a, b, 0); }
JaclVal jacl_big_sub(JaclVal a, JaclVal b) { return jbig_addsub(a, b, 1); }

JaclVal jacl_big_mul(JaclVal a, JaclVal b) {
  uint32_t sa[2], sb[2], out[JBIG_SCRATCH];
  const uint32_t *la, *lb;
  int32_t siga, sigb;
  uint32_t na = jbig_read(a, &siga, sa, &la);
  uint32_t nb = jbig_read(b, &sigb, sb, &lb);
  if (na > JBIG_MAX_LIMBS || nb > JBIG_MAX_LIMBS) return jaclrt_set_error(jaclrt_i32(0));
  if (na + nb > JBIG_SCRATCH) return jaclrt_set_error(jaclrt_i32(0));
  return jbig_canon(siga * sigb, out, jbig_mag_mul(la, na, lb, nb, out));
}

/* Ordering across the whole tower: sign first, then magnitude. Works for any pair of
 * dynamic integers, which is what lets `<` compare a bigint against an inline i32 without
 * either side being converted. */
int jacl_big_cmp(JaclVal a, JaclVal b) {
  uint32_t sa[2], sb[2];
  const uint32_t *la, *lb;
  int32_t siga, sigb;
  uint32_t na = jbig_read(a, &siga, sa, &la);
  uint32_t nb = jbig_read(b, &sigb, sb, &lb);
  int za = (na == 1 && la[0] == 0), zb = (nb == 1 && lb[0] == 0);
  if (za && zb) return 0;                      /* +0 and -0 are the same number */
  if (za) return sigb > 0 ? -1 : 1;
  if (zb) return siga > 0 ? 1 : -1;
  if (siga != sigb) return siga < sigb ? -1 : 1;
  int c = jbig_mag_cmp(la, na, lb, nb);
  return siga > 0 ? c : -c;
}

/* Hash by *value*, never by the cell address — the whole point of canonical form is that a
 * number's representation is not observable, and a pointer hash would leak it straight into
 * which bucket a map key lands in (#107). */
uint32_t jacl_big_hash(JaclVal v) {
  JaclBig *b = jbig_of(v);
  uint32_t h = 0x811c9dc5u ^ (uint32_t)(b->sign < 0);
  for (uint32_t i = 0; i < b->n; i++) h = (h ^ b->limb[i]) * 16777619u;
  return h;
}

/* Approximate value as a double, for mixing with floats. Lossy past 2^53 by definition —
 * that is what asking for a float means. */
double jacl_big_to_f64(JaclVal v) {
  JaclBig *b = jbig_of(v);
  double d = 0.0;
  for (uint32_t i = b->n; i--;) d = d * 4294967296.0 + (double)b->limb[i];
  return b->sign < 0 ? -d : d;
}

/* Decimal digits into `buf`, nine at a time (the largest power of ten a limb division can
 * peel off without overflowing). Returns the length, or 0 if `cap` is too small. */
uint32_t jacl_big_to_decimal(JaclVal v, char *buf, uint32_t cap) {
  JaclBig *b = jbig_of(v);
  uint32_t tmp[JBIG_MAX_LIMBS];
  uint32_t n = b->n;
  if (n > JBIG_MAX_LIMBS) return 0;
  for (uint32_t i = 0; i < n; i++) tmp[i] = b->limb[i];
  char rev[JBIG_MAX_LIMBS * 10 + 2];
  uint32_t r = 0;
  while (n) {
    uint32_t chunk = jbig_divmod_small(tmp, n, 1000000000u);
    while (n && !tmp[n - 1]) n--;
    for (int k = 0; k < 9; k++) { rev[r++] = (char)('0' + (chunk % 10u)); chunk /= 10u; }
  }
  while (r > 1 && rev[r - 1] == '0') r--;          /* strip the leading-chunk padding */
  uint32_t len = r + (b->sign < 0 ? 1u : 0u);
  if (len + 1 > cap) return 0;
  uint32_t o = 0;
  if (b->sign < 0) buf[o++] = '-';
  while (r) buf[o++] = rev[--r];
  buf[o] = '\0';
  return o;
}

/* Division and remainder. A divisor that fits one limb is peeled off directly; big-by-big
 * needs Knuth algorithm D and is jacl #121 — it produces a domain error rather than a wrong
 * answer until then. Truncation toward zero, and the remainder takes the dividend's sign,
 * matching what the i32 and i64 tiers already do. */
JaclVal jacl_big_divmod(JaclVal a, JaclVal b, int want_rem) {
  uint32_t sa[2], sb[2];
  const uint32_t *la, *lb;
  int32_t siga, sigb;
  uint32_t na = jbig_read(a, &siga, sa, &la);
  uint32_t nb = jbig_read(b, &sigb, sb, &lb);
  if (nb == 1 && lb[0] == 0) return jaclrt_set_error(jaclrt_i32(0));   /* divide by zero */
  if (nb > 1 || na > JBIG_MAX_LIMBS) return jaclrt_set_error(jaclrt_i32(0));
  uint32_t tmp[JBIG_MAX_LIMBS];
  for (uint32_t i = 0; i < na; i++) tmp[i] = la[i];
  uint32_t rem = jbig_divmod_small(tmp, na, lb[0]);
  if (want_rem) return jbig_canon(siga, &rem, 1);
  return jbig_canon(siga * sigb, tmp, na);
}

/* Build from a decimal digit string — how a bigint *literal* reaches the runtime, since one
 * has no inline form to constant-fold into. Validates rather than trusting the caller: the
 * lexer hands over a literal's own span, but this is reachable from the runtime surface. */
JaclVal jacl_big_from_decimal(JaclVal s) {
  if (!jaclrt_is_string(s)) return jaclrt_error();
  char buf[JACL_BIG_DECIMAL_MAX];
  uint32_t len = jacl_str_len(s);
  if (len == 0 || len + 1 > sizeof buf) return jaclrt_set_error(jaclrt_i32(0));
  jacl_str_bytes(s, buf, sizeof buf);
  uint32_t i = 0;
  int32_t sign = 1;
  if (buf[0] == '-') { sign = -1; i = 1; }
  if (i >= len) return jaclrt_error();
  uint32_t limb[JBIG_MAX_LIMBS], n = 0;
  for (; i < len; i++) {
    if (buf[i] < '0' || buf[i] > '9') return jaclrt_error();
    uint64_t carry = (uint64_t)(buf[i] - '0');            /* n = n*10 + digit */
    for (uint32_t k = 0; k < n; k++) {
      uint64_t t = (uint64_t)limb[k] * 10u + carry;
      limb[k] = (uint32_t)t;
      carry = t >> 32;
    }
    while (carry) {
      if (n >= JBIG_MAX_LIMBS) return jaclrt_set_error(jaclrt_i32(0));
      limb[n++] = (uint32_t)carry;
      carry >>= 32;
    }
  }
  return jbig_canon(sign, limb, n);
}
