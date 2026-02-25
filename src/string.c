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
  uint32_t length;  /* byte count (not null-terminated) */
  uint32_t hash;    /* precomputed FNV-1a hash */
  char     data[];  /* flexible array member */
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
        entry->length == length &&
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
          new_entries, new_cap, entry->data, entry->length, entry->hash);
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
  str->length = length;
  str->hash   = hash;
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

/* --- Unified string API (works for both inline and heap strings) --- */

static inline uint32_t jacl_string_len(JaclVal v) {
  if (jacl_is_inline_string(v)) {
    return (uint32_t)jacl_inline_string_len(v);
  }
  return jacl_as_heap_string(v)->length;
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
  JaclHeapString* hs = jacl_as_heap_string(v);
  uint32_t copy = hs->length < (uint32_t)buflen ? hs->length : (uint32_t)buflen;
  memcpy(buf, hs->data, copy);
  return hs->length;
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

  /* Cross-representation: length check + memcmp */
  if ((a_inline || a_heap) && (b_inline || b_heap)) {
    uint32_t len_a = jacl_string_len(a);
    uint32_t len_b = jacl_string_len(b);
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
  uint32_t len_a = jacl_string_len(a);
  uint32_t len_b = jacl_string_len(b);

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

#endif /* STRING_C */
