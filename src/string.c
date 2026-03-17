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

static uint32_t string__fnv1a(const char* data, uint32_t length) {
  uint32_t hash = 2166136261u;
  for (uint32_t i = 0; i < length; i++) {
    hash ^= (uint8_t)data[i];
    hash *= 16777619u;
  }
  return hash;
}

/* --- Intern table (open addressing, linear probing) --- */

#define INTERN_INIT_CAP 16

typedef struct {
  JaclHeapString** entries;  /* array of pointers (NULL = empty slot) */
  uint32_t         count;
  uint32_t         cap;
  arena_t*         arena;
} JaclInternTable;

/* Initialize an intern table */
static void intern_table_init(JaclInternTable* table, arena_t* arena) {
  table->arena   = arena;
  table->count   = 0;
  table->cap     = INTERN_INIT_CAP;
  table->entries = (JaclHeapString**)arena_alloc(arena,
      INTERN_INIT_CAP * sizeof(JaclHeapString*));
  memset(table->entries, 0, INTERN_INIT_CAP * sizeof(JaclHeapString*));
}

/* Internal: find or insert slot */
static JaclHeapString** intern__find_slot(JaclHeapString** entries,
                                           uint32_t cap,
                                           const char* data,
                                           uint32_t length,
                                           uint32_t hash) {
  uint32_t idx = hash & (cap - 1);
  for (;;) {
    JaclHeapString* entry = entries[idx];
    if (entry == NULL) {
      return &entries[idx];
    }
    if (entry->hash == hash &&
        entry->byte_len == length &&
        memcmp(entry->data, data, length) == 0) {
      return &entries[idx];
    }
    idx = (idx + 1) & (cap - 1);
  }
}

/* Internal: resize when load factor exceeds 0.75 */
static void intern__resize(JaclInternTable* table) {
  uint32_t new_cap = table->cap * 2;
  JaclHeapString** new_entries = (JaclHeapString**)arena_alloc(
      table->arena, new_cap * sizeof(JaclHeapString*));
  memset(new_entries, 0, new_cap * sizeof(JaclHeapString*));

  /* Rehash all existing entries */
  for (uint32_t i = 0; i < table->cap; i++) {
    JaclHeapString* entry = table->entries[i];
    if (entry != NULL) {
      JaclHeapString** slot = intern__find_slot(
          new_entries, new_cap, entry->data, entry->byte_len, entry->hash);
      *slot = entry;
    }
  }

  table->entries = new_entries;
  table->cap     = new_cap;
}

/* Intern a string: returns JaclVal with JACL_TAG_STRING tag.
 * String data is allocated on the GC heap (Phase 1: immortal).
 * Intern table bookkeeping (entries array) remains arena-backed. */
static JaclVal jacl_intern(ThreadHeap* heap, JaclInternTable* table,
                            const char* data, uint32_t length) {
  uint32_t hash = string__fnv1a(data, length);

  /* Look for existing entry */
  JaclHeapString** slot = intern__find_slot(
      table->entries, table->cap, data, length, hash);

  if (*slot != NULL) {
    /* Already interned — return same pointer */
    return jacl_string_ptr(*slot);
  }

  /* Resize if load factor > 0.75 */
  if ((table->count + 1) * 4 > table->cap * 3) {
    intern__resize(table);
    /* Re-find slot after resize */
    slot = intern__find_slot(table->entries, table->cap, data, length, hash);
  }

  /* Allocate new heap string on GC heap */
  JaclHeapString* str = (JaclHeapString*)gc_alloc(
      heap, OBJ_STRING, sizeof(JaclHeapString) + length);
  str->byte_len     = length;
  str->grapheme_len = (uint32_t)unicode_grapheme_count(
      (const uint8_t*)data, (size_t)length);
  str->hash         = hash;
  memcpy(str->data, data, length);

  *slot = str;
  table->count++;

  return jacl_string_ptr(str);
}

/* --- Heap string predicate --- */

static inline bool jacl_is_heap_string(JaclVal v) {
  return (v & JACL_TYPE_MASK) == JACL_TAG_STRING;
}

/* --- Heap string extractor --- */

static inline JaclHeapString* jacl_as_heap_string(JaclVal v) {
  return (JaclHeapString*)jacl_as_ptr(v);
}

/* --- Rope string wrapper (GC-managed, RC rope internals) --- */

typedef struct {
  uint32_t hash;  /* precomputed FNV-1a hash over all rope bytes */
  rope     r;     /* rope tree (RC-managed internals) */
} JaclRopeString;

static inline bool jacl_is_rope_string(JaclVal v) {
  return (v & JACL_TYPE_MASK) == JACL_TAG_ROPE_STRING;
}

static inline JaclRopeString* jacl_as_rope_string(JaclVal v) {
  return (JaclRopeString*)jacl_as_ptr(v);
}

static inline JaclVal jacl_rope_string_ptr(JaclRopeString* p) {
  return JACL_TAG_ROPE_STRING | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
}

/* --- Unified string API (works for inline, heap, and rope strings) --- */

/* Returns grapheme cluster count. For inline strings (≤7 bytes),
 * computed on-the-fly. For heap strings, returns cached grapheme_len. */
static inline uint32_t jacl_string_len(JaclVal v) {
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
static inline uint32_t jacl_string_byte_len(JaclVal v) {
  if (jacl_is_inline_string(v)) {
    return (uint32_t)jacl_inline_string_len(v);
  }
  if ((v & JACL_TYPE_MASK) == JACL_TAG_ROPE_STRING) {
    JaclRopeString* rs = (JaclRopeString*)jacl_as_ptr(v);
    return (uint32_t)rope_byte_count(rs->r);
  }
  return jacl_as_heap_string(v)->byte_len;
}

static uint32_t jacl_string_data(JaclVal v, char* buf, size_t buflen) {
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

static bool jacl_string_eq(JaclVal a, JaclVal b) {
  bool a_inline = jacl_is_inline_string(a);
  bool b_inline = jacl_is_inline_string(b);
  bool a_heap = jacl_is_heap_string(a);
  bool b_heap = jacl_is_heap_string(b);

  /* Both inline: bitwise comparison (tag+payload, ignoring flags) */
  if (a_inline && b_inline) {
    uint64_t mask = JACL_TYPE_MASK | JACL_PAYLOAD_MASK;
    return (a & mask) == (b & mask);
  }

  /* Both heap: pointer comparison (interning guarantees correctness) */
  if (a_heap && b_heap) {
    return (a & JACL_PAYLOAD_MASK) == (b & JACL_PAYLOAD_MASK);
  }

  /* Cross-representation: byte length check + memcmp */
  if ((a_inline || a_heap) && (b_inline || b_heap)) {
    uint32_t len_a = jacl_string_byte_len(a);
    uint32_t len_b = jacl_string_byte_len(b);
    if (len_a != len_b) return false;
    /* Cross-rep means one is inline (<=7 bytes), so stack buffers suffice */
    char buf_a[8], buf_b[8];
    jacl_string_data(a, buf_a, sizeof(buf_a));
    jacl_string_data(b, buf_b, sizeof(buf_b));
    return memcmp(buf_a, buf_b, len_a) == 0;
  }

  /* Not both strings */
  return false;
}

static int jacl_string_cmp(JaclVal a, JaclVal b) {
  uint32_t len_a = jacl_string_byte_len(a);
  uint32_t len_b = jacl_string_byte_len(b);

  /* Rope strings: full implementation in US-011 */
  bool a_rope = (a & JACL_TYPE_MASK) == JACL_TAG_ROPE_STRING;
  bool b_rope = (b & JACL_TYPE_MASK) == JACL_TAG_ROPE_STRING;
  if (a_rope || b_rope) {
    /* Materialize to temporary buffers for comparison */
    char *ma = NULL, *mb = NULL;
    char sa[128], sb[128];
    const char *da, *db;

    if (a_rope) {
      ma = len_a <= sizeof(sa) ? sa : (char*)malloc(len_a);
      jacl_string_data(a, ma, len_a);
      da = ma;
    } else if (jacl_is_inline_string(a)) {
      jacl_string_data(a, sa, sizeof(sa));
      da = sa;
    } else {
      da = jacl_as_heap_string(a)->data;
    }

    if (b_rope) {
      mb = len_b <= sizeof(sb) ? sb : (char*)malloc(len_b);
      jacl_string_data(b, mb, len_b);
      db = mb;
    } else if (jacl_is_inline_string(b)) {
      jacl_string_data(b, sb, sizeof(sb));
      db = sb;
    } else {
      db = jacl_as_heap_string(b)->data;
    }

    uint32_t min_len = len_a < len_b ? len_a : len_b;
    int result = min_len > 0 ? memcmp(da, db, min_len) : 0;
    if (a_rope && ma != sa) free(ma);
    if (b_rope && mb != sb) free(mb);
    if (result != 0) return result;
    if (len_a < len_b) return -1;
    if (len_a > len_b) return 1;
    return 0;
  }

  char buf_a[8], buf_b[8];
  const char* data_a;
  const char* data_b;

  if (jacl_is_inline_string(a)) {
    jacl_string_data(a, buf_a, sizeof(buf_a));
    data_a = buf_a;
  } else {
    data_a = jacl_as_heap_string(a)->data;
  }

  if (jacl_is_inline_string(b)) {
    jacl_string_data(b, buf_b, sizeof(buf_b));
    data_b = buf_b;
  } else {
    data_b = jacl_as_heap_string(b)->data;
  }

  uint32_t min_len = len_a < len_b ? len_a : len_b;
  int result = min_len > 0 ? memcmp(data_a, data_b, min_len) : 0;
  if (result != 0) return result;
  if (len_a < len_b) return -1;
  if (len_a > len_b) return 1;
  return 0;
}

/* --- Compute FNV-1a hash over rope bytes in chunks (no large allocation) --- */

static uint32_t rope_string__compute_hash(rope r) {
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

static JaclVal jacl_rope_string_create(ThreadHeap* heap,
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
static bool jacl_utf8_validate(const char* data, size_t len) {
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
static bool jacl_nfd_normalize(const char* data, size_t len,
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
static bool jacl_grapheme_nth(const uint8_t* data, size_t len, size_t n,
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
static JaclVal jacl_string_new(ThreadHeap* heap, JaclInternTable* table,
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
