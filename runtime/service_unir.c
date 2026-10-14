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
 * event the service sent, as `[map name value]`, or nil if none came within WAIT.
 * [service NAME N] opens a service whose events signal the notification the vat was endowed with
 * as N (unir decision #208.1): a wait for its events then waits on that notification, as an idle
 * worker does, rather than polling (#219).
 * A connection that is not request and reply, such as a pane (theSherwood/unir#102), is the same
 * session: [service-send S [map NAME V]] writes V on the entry named NAME and waits for nothing,
 * and every entry the other end writes comes as an event.
 * A value that does not fit the service's type is an error value, and nothing is sent.
 *
 * A session is one request at a time from one fiber. Services a program leaves open close when
 * it exits (jacl_services_close, from jacl_stage_finish and the root's exit). */

#define JACL_SERVICES 8u
#define JACL_SERVICE_FRAMES 8u      /* the service's edges' geometry: unir's store vat's */
#define JACL_SERVICE_SLOT 4096u

static unir_service *jacl_services[JACL_SERVICES];
/* Whether each was opened with the vat's notification, which its events signal while it is armed. */
static uint8_t jacl_service_woken[JACL_SERVICES];

static unir_service *service_of(JaclVal s) {
  if (!jaclrt_is_i32(s)) return 0;
  int32_t i = jaclrt_as_i32(s);
  return i >= 0 && (uint32_t)i < JACL_SERVICES ? jacl_services[i] : 0;
}

/* A session with the service granted as NAME, an integer handle; or an error. With `note` a
 * string, the service's events signal the vat's notification granted as `note`, which is opened
 * with it. */
static JaclVal service_open(JaclVal name, JaclVal note) {
  if (!jaclrt_is_string(name)) return chan_err("service: the name is not a string");
  char nb[128], nn[64];
  uint32_t len = jacl_str_len(name), nlen = 0;
  if (len >= sizeof nb) return chan_err("service: the name is too long");
  jacl_str_bytes(name, nb, sizeof nb);
  if (!jaclrt_is_nil(note)) {
    if (!jaclrt_is_string(note)) return chan_err("service: the notification's name is not a string");
    nlen = jacl_str_len(note);
    if (nlen >= sizeof nn) return chan_err("service: the notification's name is too long");
    jacl_str_bytes(note, nn, sizeof nn);
  }
  int64_t st = UNIR_EINVALID;
  int32_t slot = -1;
  /* The connect holds the vat while it waits for the service's offer: the service is running
   * (it spawned this vat), so the wait is short. */
  unir_vat *vat = vat_take();
  for (uint32_t i = 0; i < JACL_SERVICES && slot < 0; i++)
    if (!jacl_services[i]) slot = (int32_t)i;
  const char *why = !jaclrt_is_nil(note) ? vat_note_open(vat, nn, nlen) : 0;
  unir_service *s = !why && vat && slot >= 0
      ? unir_service_open(vat, (const uint8_t *)nb, len, JACL_SERVICE_FRAMES, JACL_SERVICE_SLOT,
                          jv_values_schema, sizeof jv_values_schema, &st)
      : 0;
  if (s) {
    jacl_services[slot] = s;
    jacl_service_woken[slot] = !jaclrt_is_nil(note);
  }
  vat_give();
  if (s) return jaclrt_i32(slot);
  if (why) {
    static char msg[64];
    uint32_t n = 0;
    for (const char *p = "service: "; *p; p++) msg[n++] = *p;
    for (const char *p = why; *p && n < sizeof msg - 1; p++) msg[n++] = *p;
    msg[n] = 0;
    return chan_err(msg);
  }
  if (slot < 0) return chan_err("service: too many open");
  return chan_err(st == UNIR_ENOCAP ? "service: none granted by that name"
                                    : "service: the service did not connect");
}

/* [service NAME]: a session with the service granted as NAME, an integer handle; or an error. */
JaclVal jacl_service(JaclVal name) { return service_open(name, JACL_NIL); }

/* [service NAME N]: as [service NAME], for a service whose events signal the notification the vat
 * was endowed with as N (a shell's pane signals `shell.n` in unir's session). A vat has one
 * notification: an N other than the first one opened is an error. */
JaclVal jacl_service_n(JaclVal name, JaclVal note) {
  if (jaclrt_is_nil(note)) return chan_err("service: the notification's name is not a string");
  return service_open(name, note);
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
  unir_vat *vat = vat_take();
  int64_t r = unir_service_call(vat, s, buf, n);
  vat_give();
  return service_result(s, r, "service-call");
}

/* [service-event S WAIT]: the service's next event as [map name value], or nil if none came: at
 * once if WAIT is false or nil; when one comes if WAIT is true; and if WAIT is a number of
 * seconds, within that long, or sooner once one of this program's tasks writes to a transcript or
 * ends (vat_stir), so a shell waiting for its pane notices its jobs (#219). Waiting for a service
 * opened with the vat's notification, it arms the service, whose next event signals the
 * notification, and parks until the notification's waiter or a task's write moves
 * jacl_unir_events; only a task's write or end moves jacl_unir_stirs. Any other service it polls,
 * giving the vat back between polls, from 50 microseconds apart to a millisecond. */
JaclVal jacl_service_event(JaclVal svc, JaclVal wait) {
  unir_service *s = service_of(svc);
  if (!s) return chan_err("service-event: not an open service");
  /* How long it may wait: not at all (0), as long as it takes (-1), or WAIT seconds. */
  long left = jaclrt_is_bool(wait) && jaclrt_as_bool(wait) ? -1 : 0;
  if (!jaclrt_is_nil(wait) && !jaclrt_is_bool(wait)) {
    double secs = jacl_secs(wait);
    if (!(secs >= 0)) return chan_err("service-event: WAIT is not a boolean or a number of seconds");
    left = secs < 1e9 ? (long)(secs * 1e9) : (long)1e18;
  }
  int woken = jacl_service_woken[jaclrt_as_i32(svc)];
  int32_t start = __vm_atomic_load32(&jacl_unir_stirs);
  for (long idle = 50000;; idle = idle < 1000000 ? idle * 2 : 1000000) {
    int32_t seen = __vm_atomic_load32(&jacl_unir_events);
    if (left > 0 && __vm_atomic_load32(&jacl_unir_stirs) != start) return JACL_NIL;   /* a task wrote or ended */
    unir_vat *vat = vat_take();
    int64_t r = unir_service_event(vat, s, 0);
    int64_t armed = r == 0 && left && woken ? unir_service_arm(vat, s) : 0;
    vat_give();
    if (r != 0 || !left) return service_result(s, r, "service-event");
    /* Armed, or polling: wait for the next poll, an event, a task's write, or the time WAIT
     * allows, at most a slice at a time. Not armed but woken, an event is ready: look again. */
    if (armed > 0 || !woken) {
      long ns = left < 0 || left > JACL_UNIR_SLICE_NS ? JACL_UNIR_SLICE_NS : left;
      if (!woken && idle < ns) ns = idle;
      int32_t mark = !woken ? 0 : left < 0 ? -1 : (int32_t)(ns / 1000 + 1);
      if (vat_await(seen, ns, mark) == 2 && left > 0) left = left > ns ? left - ns : 0;
    }
    if (armed > 0) {
      vat = vat_take();
      (void)unir_service_disarm(vat, s);
      vat_give();
    }
    jacl_ctl_point();   /* a waiting job can be cancelled or suspended */
    if (!left) return JACL_NIL;
  }
}

/* [service-send S [map NAME V]]: V as a message of the entry named NAME; nil, or an error. */
JaclVal jacl_service_send(JaclVal svc, JaclVal msg) {
  unir_service *s = service_of(svc);
  if (!s) return chan_err("service-send: not an open service");
  uint8_t buf[JACL_SERVICE_SLOT];
  JaclVal err = JACL_NIL;
  uint64_t n = jv_encode_into(msg, buf, sizeof buf, &err);
  if (!n) return err;
  unir_vat *vat = vat_take();
  int64_t r = unir_service_send(vat, s, buf, n);
  vat_give();
  return service_result(s, r, "service-send");
}

/* At exit: close the sessions still open, so each service drops this client. */
static void jacl_services_close(void) {
  for (uint32_t i = 0; i < JACL_SERVICES; i++) {
    if (!jacl_services[i]) continue;
    unir_vat *vat = vat_take();
    unir_service_close(vat, jacl_services[i]);
    vat_give();
    jacl_services[i] = 0;
    jacl_service_woken[i] = 0;
  }
}
