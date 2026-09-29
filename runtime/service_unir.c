/* service_unir.c — services (theSherwood/unir#50, docs/UNIR_SERVICES.md). Included by chan.c
 * with the unir backend, after value.c.
 *
 * A service is a vat that serves its typed endpoint to its clients: unir's store vat is the
 * first. A program granted one as NAME (three capabilities: NAME.requests, NAME.replies,
 * NAME.post) opens it with [service NAME]. The service offers its schema at connect and this
 * runtime adopts it, so a program needs nothing generated to use a service: [service-call S REQ] sends a
 * value, converted to the service's request type by structure (unir's `Dynamic`: a map for a
 * struct by field name, `[map Variant payload]` or the variant's name for a sum, a string for
 * text or bytes), and returns its reply, converted back; [service-event S WAIT] returns the next
 * event the service sent, as `[map name value]`, or nil if there is none (and WAIT is false).
 * A value that does not fit the service's type is an error value, and nothing is sent.
 *
 * A session is one request at a time from one fiber. Services a program leaves open close when
 * it exits (jacl_services_close, from jacl_stage_finish and the root's exit). */

#define JACL_SERVICES 8u
#define JACL_SERVICE_FRAMES 8u      /* the service's edges' geometry: unir's store vat's */
#define JACL_SERVICE_SLOT 4096u

static unir_service *jacl_services[JACL_SERVICES];

static unir_service *service_of(JaclVal s) {
  if (!jaclrt_is_i32(s)) return 0;
  int32_t i = jaclrt_as_i32(s);
  return i >= 0 && (uint32_t)i < JACL_SERVICES ? jacl_services[i] : 0;
}

/* [service NAME]: a session with the service granted as NAME, an integer handle; or an error. */
JaclVal jacl_service(JaclVal name) {
  if (!jaclrt_is_string(name)) return chan_err("service: the name is not a string");
  char nb[128];
  uint32_t len = jacl_str_len(name);
  if (len >= sizeof nb) return chan_err("service: the name is too long");
  jacl_str_bytes(name, nb, sizeof nb);
  int64_t st = UNIR_EINVALID;
  int32_t slot = -1;
  /* Under the open lock, which the connect holds while it waits for the service's offer: the
   * service is running (it spawned this vat), so the wait is short. */
  unir_lock(&jacl_unir_open_lock);
  unir_vat *vat = unir_vat_get();
  for (uint32_t i = 0; i < JACL_SERVICES && slot < 0; i++)
    if (!jacl_services[i]) slot = (int32_t)i;
  unir_service *s = vat && slot >= 0
      ? unir_service_open(vat, (const uint8_t *)nb, len, JACL_SERVICE_FRAMES, JACL_SERVICE_SLOT,
                          jv_values_schema, sizeof jv_values_schema, &st)
      : 0;
  if (s) jacl_services[slot] = s;
  unir_unlock(&jacl_unir_open_lock);
  if (s) return jaclrt_i32(slot);
  if (slot < 0) return chan_err("service: too many open");
  return chan_err(st == UNIR_ENOCAP ? "service: none granted by that name"
                                    : "service: the service did not connect");
}

/* The result `r` of a service call or event: its value, nil for none, or an error. */
static JaclVal service_result(unir_service *s, int64_t r, const char *what) {
  if (r == 0) return JACL_NIL;
  if (r < 0) {
    static char msg[96];
    const char *why = r == UNIR_EMISMATCH ? ": the value does not fit the service's type"
                    : r == UNIR_EENDED ? ": the service is gone"
                    : ": the service failed";
    uint32_t n = 0;
    for (const char *p = what; *p && n < 40; p++) msg[n++] = *p;
    for (const char *p = why; *p && n < sizeof msg - 1; p++) msg[n++] = *p;
    msg[n] = 0;
    return chan_err(msg);
  }
  int32_t cap = (int32_t)r;
  JaclVal keep[1] = {fb_alloc_nd(1, 1, &cap)};  /* may collect; `keep` is on the data stack */
  uint8_t *b = (uint8_t *)fb_data(keep[0]);
  if (unir_service_result(s, b, (uint64_t)r) != r) return chan_err("service: the result was lost");
  return jv_decode(b, (uint64_t)r);
}

/* [service-call S REQ]: REQ as the service's request; its reply. */
JaclVal jacl_service_call(JaclVal svc, JaclVal req) {
  unir_service *s = service_of(svc);
  if (!s) return chan_err("service-call: not an open service");
  uint8_t buf[JACL_SERVICE_SLOT];
  JaclVal err = JACL_NIL;
  uint64_t n = jv_encode_into(req, buf, sizeof buf, &err);
  if (!n) return err;
  return service_result(s, unir_service_call(jacl_unir_vat, s, buf, n), "service-call");
}

/* [service-event S WAIT]: the service's next event as [map name value]; nil if there is none and
 * WAIT is false. */
JaclVal jacl_service_event(JaclVal svc, JaclVal wait) {
  unir_service *s = service_of(svc);
  if (!s) return chan_err("service-event: not an open service");
  int w = !jaclrt_is_nil(wait) && !(jaclrt_is_bool(wait) && !jaclrt_as_bool(wait));
  return service_result(s, unir_service_event(jacl_unir_vat, s, w ? 1u : 0u), "service-event");
}

/* At exit: close the sessions still open, so each service drops this client. */
static void jacl_services_close(void) {
  for (uint32_t i = 0; i < JACL_SERVICES; i++) {
    if (!jacl_services[i]) continue;
    unir_service_close(jacl_unir_vat, jacl_services[i]);
    jacl_services[i] = 0;
  }
}
