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

/* Intern a string: returns JaclVal with JACL_TAG_STRING tag */
static JaclVal jacl_intern(arena_t* arena, JaclInternTable* table,
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

  /* Allocate new heap string */
  JaclHeapString* str = (JaclHeapString*)arena_alloc(
      arena, sizeof(JaclHeapString) + length);
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

#endif /* STRING_C */
