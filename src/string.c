/*
 * JACL Heap String and Intern Table
 *
 * Heap-allocated interned strings for values longer than 7 bytes.
 * Uses FNV-1a hashing and open-addressing hash table with linear probing.
 * All allocations go through the arena.
 *
 * Strings are NOT null-terminated; the length field is the sole source
 * of truth for string size.
 */

#ifndef STRING_C
#define STRING_C

/* --- Heap string structure --- */

typedef struct {
  uint32_t byte_len;     /* byte count (not null-terminated) */
  uint32_t grapheme_len; /* grapheme cluster count */
  uint32_t hash;         /* precomputed FNV-1a hash */
  char     data[];       /* flexible array member */
} JaclHeapString;

/* --- FNV-1a hash (operates on data+length, no null-terminator dependency) --- */

uint32_t string__fnv1a(const char* data, uint32_t length) {
  uint32_t hash = 2166136261u;
  for (uint32_t i = 0; i < length; i++) {
    hash ^= (uint8_t)data[i];
    hash *= 16777619u;
  }
  return hash;
}

/* --- Intern table (open addressing, linear probing) --- */

#define INTERN_INIT_CAP 16

/* Tombstone sentinel: (JaclHeapString*)1 is never a valid pointer due to
 * 8-byte alignment of GC objects. Used to mark evicted intern table slots. */
#define INTERN_TOMBSTONE ((JaclHeapString*)1)

typedef struct {
  JaclHeapString** entries;  /* array of pointers (NULL = empty, TOMBSTONE = evicted) */
  uint32_t         count;           /* live entries */
  uint32_t         tombstone_count; /* tombstone entries */
  uint32_t         cap;
  arena_t*         arena;
  platform_mutex_t lock;    /* mutex for concurrent access */
} JaclInternTable;

/* Initialize an intern table */
void intern_table_init(JaclInternTable* table, arena_t* arena) {
  table->arena           = arena;
  table->count           = 0;
  table->tombstone_count = 0;
  table->cap             = INTERN_INIT_CAP;
  table->entries = (JaclHeapString**)arena_alloc(arena,
      INTERN_INIT_CAP * sizeof(JaclHeapString*));
  memset(table->entries, 0, INTERN_INIT_CAP * sizeof(JaclHeapString*));
  MUTEX_INIT(table->lock);
}

/* Destroy an intern table (releases mutex resources) */
void intern_table_destroy(JaclInternTable* table) {
  MUTEX_DESTROY(table->lock);
}

/* Internal: find or insert slot.
 * Probes past tombstones (continue probing), stops at NULL (empty).
 * Returns the matching entry if found, otherwise the first available
 * slot for insertion (first tombstone seen, or the NULL slot). */
JaclHeapString** intern__find_slot(JaclHeapString** entries,
                                           uint32_t cap,
                                           const char* data,
                                           uint32_t length,
                                           uint32_t hash) {
  uint32_t idx = hash & (cap - 1);
  JaclHeapString** first_tombstone = NULL;
  for (;;) {
    JaclHeapString* entry = entries[idx];
    if (entry == NULL) {
      return first_tombstone ? first_tombstone : &entries[idx];
    }
    if (entry == INTERN_TOMBSTONE) {
      if (!first_tombstone) first_tombstone = &entries[idx];
    } else if (entry->hash == hash &&
               entry->byte_len == length &&
               memcmp(entry->data, data, length) == 0) {
      return &entries[idx];
    }
    idx = (idx + 1) & (cap - 1);
  }
}

/* Internal: rehash into a new table of given capacity, skipping tombstones */
void intern__rehash(JaclInternTable* table, uint32_t new_cap) {
  JaclHeapString** new_entries = (JaclHeapString**)arena_alloc(
      table->arena, new_cap * sizeof(JaclHeapString*));
  memset(new_entries, 0, new_cap * sizeof(JaclHeapString*));

  /* Rehash all live entries (skip NULL and tombstones) */
  for (uint32_t i = 0; i < table->cap; i++) {
    JaclHeapString* entry = table->entries[i];
    if (entry != NULL && entry != INTERN_TOMBSTONE) {
      JaclHeapString** slot = intern__find_slot(
          new_entries, new_cap, entry->data, entry->byte_len, entry->hash);
      *slot = entry;
    }
  }

  table->entries         = new_entries;
  table->cap             = new_cap;
  table->tombstone_count = 0;
}

/* Internal: resize when load factor exceeds 0.75 */
void intern__resize(JaclInternTable* table) {
  intern__rehash(table, table->cap * 2);
}

/* Internal: compact — rehash at same capacity to remove tombstones */
void intern__compact(JaclInternTable* table) {
  intern__rehash(table, table->cap);
}

/* Thread-local flag: when true, gc_sweep_intern_table is skipped.
 * Set during jacl_intern to prevent GC (triggered by gc_alloc)
 * from evicting intern table entries while we're about to insert. */
JACL_THREAD_LOCAL bool gc__interning = false;

/* Intern a string: returns JaclVal with JACL_TAG_STRING tag.
 * String data is allocated on the GC heap (Phase 1: immortal).
 * Intern table bookkeeping (entries array) remains arena-backed.
 * Thread-safe: uses read lock for lookups, write lock for inserts. */
JaclVal jacl_intern(ThreadHeap* heap, JaclInternTable* table,
                            const char* data, uint32_t length) {
  uint32_t hash = string__fnv1a(data, length);

  /* Allocate new heap string BEFORE acquiring lock.
     gc_alloc may trigger GC, which calls gc_sweep_intern_table
     and takes the same lock — so we must not hold it here.
     Set gc__interning to prevent the GC from evicting intern
     entries during this allocation. */
  gc__interning = true;
  JaclHeapString* str = (JaclHeapString*)gc_alloc(
      heap, OBJ_STRING, sizeof(JaclHeapString) + length);
  gc__interning = false;
  str->byte_len     = length;
  str->grapheme_len = (uint32_t)unicode_grapheme_count(
      (const uint8_t*)data, (size_t)length);
  str->hash         = hash;
  memcpy(str->data, data, length);

  MUTEX_LOCK(table->lock);

  /* Check if already inserted (by this or another thread) */
  JaclHeapString** slot = intern__find_slot(
      table->entries, table->cap, data, length, hash);
  if (*slot != NULL && *slot != INTERN_TOMBSTONE) {
    JaclVal result = jacl_string_ptr(*slot);
    MUTEX_UNLOCK(table->lock);
    /* str is now orphaned but will be collected by GC */
    return result;
  }

  /* Compact if tombstone ratio > 25% of capacity */
  if (table->tombstone_count * 4 > table->cap) {
    intern__compact(table);
    slot = intern__find_slot(table->entries, table->cap, data, length, hash);
  }

  /* Resize if load factor (count + tombstones) > 0.75 */
  if ((table->count + table->tombstone_count + 1) * 4 > table->cap * 3) {
    intern__resize(table);
    /* Re-find slot after resize */
    slot = intern__find_slot(table->entries, table->cap, data, length, hash);
  }

  /* Reuse tombstone slot if available */
  if (*slot == INTERN_TOMBSTONE) {
    table->tombstone_count--;
  }
  *slot = str;
  table->count++;

  MUTEX_UNLOCK(table->lock);
  return jacl_string_ptr(str);
}

/* --- Heap string predicate --- */

bool jacl_is_heap_string(JaclVal v) {
  return (v & JACL_TYPE_MASK) == JACL_TAG_STRING;
}

/* --- Heap string extractor --- */

JaclHeapString* jacl_as_heap_string(JaclVal v) {
  return (JaclHeapString*)jacl_as_ptr(v);
}

/* --- Rope string wrapper (GC-managed, RC rope internals) --- */

typedef struct {
  uint32_t hash;  /* precomputed FNV-1a hash over all rope bytes */
  rope     r;     /* rope tree (RC-managed internals) */
} JaclRopeString;

bool jacl_is_rope_string(JaclVal v) {
  return (v & JACL_TYPE_MASK) == JACL_TAG_ROPE_STRING;
}

JaclRopeString* jacl_as_rope_string(JaclVal v) {
  return (JaclRopeString*)jacl_as_ptr(v);
}

JaclVal jacl_rope_string_ptr(JaclRopeString* p) {
  return JACL_TAG_ROPE_STRING | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
}

/* --- Unified string API (works for inline, heap, and rope strings) --- */

/* Returns grapheme cluster count. For inline strings (≤7 bytes),
 * computed on-the-fly. For heap strings, returns cached grapheme_len. */
uint32_t jacl_string_len(JaclVal v) {
  if (jacl_is_inline_string(v)) {
    uint8_t buf[8];
    uint64_t payload = v & JACL_PAYLOAD_MASK;
    size_t len = 0;
    for (size_t i = 0; i < 7; i++) {
      uint8_t c = (uint8_t)((payload >> (i * 8)) & 0xFF);
      if (c == 0) break;
      buf[i] = c;
      len++;
    }
    return (uint32_t)unicode_grapheme_count(buf, len);
  }
  if ((v & JACL_TYPE_MASK) == JACL_TAG_ROPE_STRING) {
    JaclRopeString* rs = (JaclRopeString*)jacl_as_ptr(v);
    return (uint32_t)rope_grapheme_count(rs->r);
  }
  return jacl_as_heap_string(v)->grapheme_len;
}

/* Returns byte length for inline, heap, and rope strings. */
uint32_t jacl_string_byte_len(JaclVal v) {
  if (jacl_is_inline_string(v)) {
    return (uint32_t)jacl_inline_string_len(v);
  }
  if ((v & JACL_TYPE_MASK) == JACL_TAG_ROPE_STRING) {
    JaclRopeString* rs = (JaclRopeString*)jacl_as_ptr(v);
    return (uint32_t)rope_byte_count(rs->r);
  }
  return jacl_as_heap_string(v)->byte_len;
}

uint32_t jacl_string_data(JaclVal v, char* buf, size_t buflen) {
  if (jacl_is_inline_string(v)) {
    uint32_t len = (uint32_t)jacl_inline_string_len(v);
    uint64_t payload = v & JACL_PAYLOAD_MASK;
    uint32_t copy = len < (uint32_t)buflen ? len : (uint32_t)buflen;
    for (uint32_t i = 0; i < copy; i++) {
      buf[i] = (char)((payload >> (i * 8)) & 0xFF);
    }
    return len;
  }
  if ((v & JACL_TYPE_MASK) == JACL_TAG_ROPE_STRING) {
    JaclRopeString* rs = (JaclRopeString*)jacl_as_ptr(v);
    size_t total = rope_byte_count(rs->r);
    size_t to_copy = buflen < total ? buflen : total;
    rope_to_str(rs->r, (uint8_t*)buf, to_copy);
    return (uint32_t)total;
  }
  JaclHeapString* hs = jacl_as_heap_string(v);
  uint32_t copy = hs->byte_len < (uint32_t)buflen ? hs->byte_len : (uint32_t)buflen;
  memcpy(buf, hs->data, copy);
  return hs->byte_len;
}

/* Internal: compute FNV-1a hash of a JaclVal string (any tier) */
uint32_t jacl_string_hash(JaclVal v) {
  if (jacl_is_inline_string(v)) {
    uint64_t payload = v & JACL_PAYLOAD_MASK;
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 7; i++) {
      unsigned char c = (unsigned char)((payload >> (i * 8)) & 0xFF);
      if (c == 0) break;
      hash ^= c;
      hash *= 16777619u;
    }
    return hash;
  }
  if (jacl_is_rope_string(v)) {
    return jacl_as_rope_string(v)->hash;
  }
  return jacl_as_heap_string(v)->hash;
}

bool jacl_string_eq(JaclVal a, JaclVal b) {
  bool a_inline = jacl_is_inline_string(a);
  bool b_inline = jacl_is_inline_string(b);
  bool a_heap = jacl_is_heap_string(a);
  bool b_heap = jacl_is_heap_string(b);
  bool a_rope = jacl_is_rope_string(a);
  bool b_rope = jacl_is_rope_string(b);

  /* Both inline: bitwise comparison (tag+payload, ignoring flags) */
  if (a_inline && b_inline) {
    uint64_t mask = JACL_TYPE_MASK | JACL_PAYLOAD_MASK;
    return (a & mask) == (b & mask);
  }

  /* Both heap: pointer comparison (interning guarantees correctness) */
  if (a_heap && b_heap) {
    return (a & JACL_PAYLOAD_MASK) == (b & JACL_PAYLOAD_MASK);
  }

  /* Any comparison involving rope or cross-tier inline/heap:
   * fast-reject via hash, then compare content */
  if ((a_inline || a_heap || a_rope) && (b_inline || b_heap || b_rope)) {
    uint32_t len_a = jacl_string_byte_len(a);
    uint32_t len_b = jacl_string_byte_len(b);
    if (len_a != len_b) return false;

    /* Hash fast-path: different hashes → definitely not equal */
    uint32_t hash_a = jacl_string_hash(a);
    uint32_t hash_b = jacl_string_hash(b);
    if (hash_a != hash_b) return false;

    /* Same hash, same length — compare content */
    /* For rope-rope: compare leaf-by-leaf via cursors */
    if (a_rope && b_rope) {
      rope ra = jacl_as_rope_string(a)->r;
      rope rb = jacl_as_rope_string(b)->r;
      rope_cursor ca = rope_cursor_new(ra, 0);
      rope_cursor cb = rope_cursor_new(rb, 0);
      size_t remaining = len_a;
      while (remaining > 0) {
        uint8_t chunk_a[256], chunk_b[256];
        size_t n = remaining < 256 ? remaining : 256;
        size_t ra_read = rope_cursor_read(&ca, chunk_a, n);
        size_t rb_read = rope_cursor_read(&cb, chunk_b, n);
        size_t cmp_len = ra_read < rb_read ? ra_read : rb_read;
        if (cmp_len == 0) return false;
        if (memcmp(chunk_a, chunk_b, cmp_len) != 0) return false;
        rope_cursor_advance_bytes(&ca, cmp_len);
        rope_cursor_advance_bytes(&cb, cmp_len);
        remaining -= cmp_len;
      }
      return true;
    }

    /* Cross-tier with at least one rope: materialize non-rope side,
     * compare against rope via cursor */
    if (a_rope || b_rope) {
      /* Arrange so 'rv' is the rope, 'fv' is the flat/inline */
      JaclVal rv = a_rope ? a : b;
      JaclVal fv = a_rope ? b : a;
      uint32_t len = len_a; /* both are same length */

      /* Get flat bytes */
      char flat_buf[128];
      const char* flat_data;
      if (jacl_is_inline_string(fv)) {
        jacl_string_data(fv, flat_buf, sizeof(flat_buf));
        flat_data = flat_buf;
      } else {
        flat_data = jacl_as_heap_string(fv)->data;
      }

      /* Compare rope against flat data chunk by chunk */
      rope rr = jacl_as_rope_string(rv)->r;
      rope_cursor cr = rope_cursor_new(rr, 0);
      size_t remaining = len;
      size_t flat_off = 0;
      while (remaining > 0) {
        uint8_t chunk[256];
        size_t n = remaining < 256 ? remaining : 256;
        size_t rd = rope_cursor_read(&cr, chunk, n);
        if (rd == 0) return false;
        if (memcmp(chunk, flat_data + flat_off, rd) != 0) return false;
        rope_cursor_advance_bytes(&cr, rd);
        flat_off += rd;
        remaining -= rd;
      }
      return true;
    }

    /* Cross inline/heap (one is inline ≤7 bytes) */
    char buf_a[8], buf_b[8];
    jacl_string_data(a, buf_a, sizeof(buf_a));
    jacl_string_data(b, buf_b, sizeof(buf_b));
    return memcmp(buf_a, buf_b, len_a) == 0;
  }

  /* Not both strings */
  return false;
}

/* Internal: get pointer to flat string bytes (inline → stack buf, heap → direct) */
const char* jacl_string_flat_ptr(JaclVal v, char* buf, size_t buflen) {
  if (jacl_is_inline_string(v)) {
    jacl_string_data(v, buf, buflen);
    return buf;
  }
  return jacl_as_heap_string(v)->data;
}

int jacl_string_cmp(JaclVal a, JaclVal b) {
  uint32_t len_a = jacl_string_byte_len(a);
  uint32_t len_b = jacl_string_byte_len(b);
  bool a_rope = jacl_is_rope_string(a);
  bool b_rope = jacl_is_rope_string(b);

  /* Rope-rope: cursor-based leaf-by-leaf comparison (no materialization) */
  if (a_rope && b_rope) {
    rope ra = jacl_as_rope_string(a)->r;
    rope rb = jacl_as_rope_string(b)->r;
    rope_cursor ca = rope_cursor_new(ra, 0);
    rope_cursor cb = rope_cursor_new(rb, 0);
    uint32_t min_len = len_a < len_b ? len_a : len_b;
    size_t compared = 0;
    while (compared < min_len) {
      uint8_t chunk_a[256], chunk_b[256];
      size_t want = min_len - compared;
      if (want > 256) want = 256;
      size_t ra_read = rope_cursor_read(&ca, chunk_a, want);
      size_t rb_read = rope_cursor_read(&cb, chunk_b, want);
      size_t cmp_len = ra_read < rb_read ? ra_read : rb_read;
      if (cmp_len == 0) break;
      int r = memcmp(chunk_a, chunk_b, cmp_len);
      if (r != 0) return r;
      rope_cursor_advance_bytes(&ca, cmp_len);
      rope_cursor_advance_bytes(&cb, cmp_len);
      compared += cmp_len;
    }
    if (len_a < len_b) return -1;
    if (len_a > len_b) return 1;
    return 0;
  }

  /* Cross-tier with one rope: compare rope cursor against flat bytes */
  if (a_rope || b_rope) {
    JaclVal rv = a_rope ? a : b;
    JaclVal fv = a_rope ? b : a;
    uint32_t rv_len = a_rope ? len_a : len_b;
    uint32_t fv_len = a_rope ? len_b : len_a;

    char flat_stack[128];
    const char* flat_data = jacl_string_flat_ptr(fv, flat_stack, sizeof(flat_stack));

    rope rr = jacl_as_rope_string(rv)->r;
    rope_cursor cr = rope_cursor_new(rr, 0);
    uint32_t min_len = rv_len < fv_len ? rv_len : fv_len;
    size_t compared = 0;
    size_t flat_off = 0;
    while (compared < min_len) {
      uint8_t chunk[256];
      size_t want = min_len - compared;
      if (want > 256) want = 256;
      size_t rd = rope_cursor_read(&cr, chunk, want);
      if (rd == 0) break;
      int r = memcmp(chunk, flat_data + flat_off, rd);
      /* If a is the rope, rope chunk is 'a' side; if b is rope, flip sign */
      if (r != 0) return a_rope ? r : -r;
      rope_cursor_advance_bytes(&cr, rd);
      flat_off += rd;
      compared += rd;
    }
    if (len_a < len_b) return -1;
    if (len_a > len_b) return 1;
    return 0;
  }

  /* Non-rope: inline/heap only */
  char buf_a[8], buf_b[8];
  const char* data_a = jacl_string_flat_ptr(a, buf_a, sizeof(buf_a));
  const char* data_b = jacl_string_flat_ptr(b, buf_b, sizeof(buf_b));

  uint32_t min_len = len_a < len_b ? len_a : len_b;
  int result = min_len > 0 ? memcmp(data_a, data_b, min_len) : 0;
  if (result != 0) return result;
  if (len_a < len_b) return -1;
  if (len_a > len_b) return 1;
  return 0;
}

/* --- Compute FNV-1a hash over rope bytes in chunks (no large allocation) --- */

uint32_t rope_string__compute_hash(rope r) {
  uint32_t hash = 2166136261u;
  size_t total = rope_byte_count(r);
  uint8_t chunk[256];
  size_t pos = 0;
  while (pos < total) {
    size_t n = total - pos;
    if (n > sizeof(chunk)) n = sizeof(chunk);
    rope_st_copy_range(r.root, pos, n, chunk);
    for (size_t i = 0; i < n; i++) {
      hash ^= chunk[i];
      hash *= 16777619u;
    }
    pos += n;
  }
  return hash;
}

/* --- Create a JaclRopeString on the GC heap from raw bytes --- */

JaclVal jacl_rope_string_create(ThreadHeap* heap,
                                        const uint8_t* data, size_t len) {
  rope r = rope_from_str(data, len);
  uint32_t hash = rope_string__compute_hash(r);

  JaclRopeString* rs = (JaclRopeString*)gc_alloc(
      heap, OBJ_ROPE_STRING, sizeof(JaclRopeString));
  rs->hash = hash;
  rs->r    = r;

  return jacl_rope_string_ptr(rs);
}

/* --- UTF-8 validation --- */

/* Validate UTF-8: returns true if data contains only valid UTF-8 sequences.
 * Rejects overlong encodings, surrogate halves (U+D800–U+DFFF),
 * truncated sequences, and invalid continuation bytes. */
bool jacl_utf8_validate(const char* data, size_t len) {
  const uint8_t* src = (const uint8_t*)data;
  size_t i = 0;
  while (i < len) {
    /* ASCII fast path */
    if (src[i] < 0x80) { i++; continue; }

    uint32_t cp;
    size_t consumed = utf8_decode(src + i, len - i, &cp);
    if (consumed == 0) return false;

    /* utf8_decode rejects overlong and >U+10FFFF but not surrogates */
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;

    i += consumed;
  }
  return true;
}

/* --- NFD normalization helper --- */

/* Normalize string data to NFD form.
 * - If already NFD (detected via unicode_is_nfd quick-check), sets *out = src
 *   and *out_len = len with no copy (fast path for ASCII and pre-decomposed).
 * - Otherwise, computes NFD length, writes to stack_buf if it fits,
 *   or allocates via malloc for larger output.
 * - Sets *out to NFD bytes and *out_len to NFD byte count.
 * - Returns true on success.
 * - Caller must free *out only if *out != (uint8_t*)data AND *out != stack_buf. */
bool jacl_nfd_normalize(const char* data, size_t len,
                                uint8_t* stack_buf, size_t stack_cap,
                                uint8_t** out, size_t* out_len) {
  const uint8_t* src = (const uint8_t*)data;

  /* Fast path: already NFD (covers pure ASCII and pre-decomposed text) */
  if (unicode_is_nfd(src, len)) {
    *out = (uint8_t*)data;
    *out_len = len;
    return true;
  }

  /* Compute NFD output length */
  size_t nfd_len = unicode_nfd_length(src, len);

  /* Select buffer: stack if fits, otherwise malloc */
  uint8_t* dst;
  if (nfd_len <= stack_cap) {
    dst = stack_buf;
  } else {
    dst = (uint8_t*)malloc(nfd_len);
    if (!dst) return false;
  }

  /* Perform NFD normalization */
  size_t actual_len = 0;
  unicode_nfd(src, len, dst, nfd_len, &actual_len);

  *out = dst;
  *out_len = actual_len;
  return true;
}

/* --- Grapheme indexing helper --- */

/* jacl_grapheme_nth: find the byte range [out_start, out_end) of the nth
 * grapheme cluster (0-indexed) in a UTF-8 buffer.
 * Returns true if the nth grapheme exists, false if n >= grapheme count. */
bool jacl_grapheme_nth(const uint8_t* data, size_t len, size_t n,
                               size_t* out_start, size_t* out_end) {
  size_t pos = 0;
  for (size_t i = 0; i < n; i++) {
    if (pos >= len) return false;
    pos = unicode_grapheme_next(data, len, pos);
  }
  if (pos >= len) return false;
  *out_start = pos;
  *out_end = unicode_grapheme_next(data, len, pos);
  return true;
}

/* --- Unified string constructor with validation, normalization, and tier routing --- */

/* jacl_string_new: validate UTF-8, normalize to NFD, route to correct tier.
 * Returns JACL_NIL on invalid UTF-8.
 * Tiers: 0-7 bytes → inline, 8-128 bytes → flat interned, 129+ bytes → rope. */
JaclVal jacl_string_new(ThreadHeap* heap, JaclInternTable* table,
                                const char* data, size_t length) {
  /* Step 1: UTF-8 validation */
  if (length > 0 && !jacl_utf8_validate(data, length)) {
    return JACL_NIL;
  }

  /* Step 2: NFD normalization */
  uint8_t stack_buf[512];
  uint8_t* nfd_data;
  size_t nfd_len;
  if (!jacl_nfd_normalize(data, length, stack_buf, sizeof(stack_buf),
                           &nfd_data, &nfd_len)) {
    return JACL_NIL;
  }

  /* Step 3: Tier routing based on post-NFD byte length */
  JaclVal result;
  if (nfd_len <= 7) {
    result = jacl_inline_string((const char*)nfd_data, nfd_len);
  } else if (nfd_len <= 128) {
    result = jacl_intern(heap, table, (const char*)nfd_data, (uint32_t)nfd_len);
  } else {
    result = jacl_rope_string_create(heap, nfd_data, nfd_len);
  }

  /* Free NFD buffer if it was malloc'd (not input pointer, not stack buffer) */
  if (nfd_data != (uint8_t*)data && nfd_data != stack_buf) {
    free(nfd_data);
  }

  return result;
}

#endif /* STRING_C */
