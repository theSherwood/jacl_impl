/* hamt.h - Hash Array Mapped Trie (include-as-template)
 *
 * Persistent, immutable HAMT with optional reference counting or GC.
 *
 * Define HAMT_KEY_T (key type), HAMT_VAL_T (value type), and HAMT_NAME (prefix)
 * before including. Can be included multiple times with different values.
 *
 * RC mode (default): include with HAMT_ALLOCATOR for reference-counted nodes.
 * GC mode: define HAMT_GC_MODE and provide:
 *   HAMT_GC_ALLOC(obj_type, payload_size) — allocation function
 *   HAMT_GC_OBJ_INTERNAL, HAMT_GC_OBJ_LEAF, HAMT_GC_OBJ_COLLISION — obj types
 *
 * Example:
 *   #define HAMT_KEY_T    const char*
 *   #define HAMT_VAL_T    void*
 *   #define HAMT_NAME     str_hamt
 *   #include "hamt.h"
 *   // Now have: str_hamt_node, str_hamt_leaf, str_hamt_get, str_hamt_set, etc.
 */

#ifndef HAMT_KEY_T
#error "HAMT_KEY_T must be defined before including hamt.h"
#define HAMT_KEY_T int /* suppress further errors */
#endif

#ifndef HAMT_VAL_T
#error "HAMT_VAL_T must be defined before including hamt.h"
#define HAMT_VAL_T int
#endif

#ifndef HAMT_NAME
#error "HAMT_NAME must be defined before including hamt.h"
#define HAMT_NAME _hamt_err
#endif

/* Concatenation helpers */
#define H_XCAT(a, b) a##b
#define H_CAT(a, b)  H_XCAT(a, b)
#define H_NS(name)   H_CAT(HAMT_NAME, name)

/* Generated type names */
#define H_NODE_TYPE  H_NS(_node_type)
#define H_NODE       H_NS(_node)
#define H_INTERNAL   H_NS(_internal)
#define H_LEAF       H_NS(_leaf)
#define H_COLLISION  H_NS(_collision)
#define H_ITER       H_NS(_iter)
#define H_ITER_RES   H_NS(_iter_result)

/* Node type enum values */
#define H_NODE_INTERNAL_VAL  H_NS(_NODE_INTERNAL)
#define H_NODE_LEAF_VAL      H_NS(_NODE_LEAF)
#define H_NODE_COLLISION_VAL H_NS(_NODE_COLLISION)

/* Generated function names */
#define H_SET_KEY_HANDLERS    H_NS(_set_key_handlers)
#define H_SET_LIFECYCLE_HOOKS H_NS(_set_lifecycle_hooks)
#define H_SET_HASH_HANDLERS   H_NS(_set_hash_handlers)
#define H_GET                 H_NS(_get)
#define H_GET_OR_DEFAULT      H_NS(_get_or_default)
#define H_HAS                 H_NS(_has)
#define H_SET                 H_NS(_set)
#define H_UNSET               H_NS(_unset)
#define H_COUNT               H_NS(_count)
#define H_ITER_INIT           H_NS(_iter_init)
#define H_NEXT_LEAF           H_NS(_next_leaf)
#define H_NEXT_KEY            H_NS(_next_key)
#define H_NEXT_VALUE          H_NS(_next_value)
#define H_KEY_FROM_LEAF       H_NS(_key_from_leaf)
#define H_VALUE_FROM_LEAF     H_NS(_value_from_leaf)
#define H_REF                 H_NS(_ref)
#define H_UNREF               H_NS(_unref)
#define H_REF_COUNT           H_NS(_ref_count)
#define H_NODE_HASH           H_NS(_node_hash)

/* Stride API */
#define H_SET_WIDE            H_NS(_set_wide)
#define H_GET_PTR             H_NS(_get_ptr)
#define H_VALUE_PTR_FROM_LEAF H_NS(_value_ptr_from_leaf)
#define H_KEY_PTR_FROM_LEAF   H_NS(_key_ptr_from_leaf)
#define H_UNSET_WIDE          H_NS(_unset_wide)
#define H_HAS_WIDE            H_NS(_has_wide)
#define H_WIDE_KEY_HASH       H_NS(_wide_key_hash)
#define H_WIDE_KEY_EQ         H_NS(_wide_key_eq)
#define H_LEAF_KEY_PTR(leaf)  ((leaf)->slots)
#define H_LEAF_VAL_PTR(leaf)  ((HAMT_VAL_T*)((char*)(leaf)->slots + sizeof(HAMT_KEY_T) * (leaf)->key_stride))

/* Internal helpers */
#define H_NODE_DESTROY       H_NS(_node_destroy)
#define H_MK_LEAF            H_NS(_mk_leaf)
#define H_MK_INTERNAL_COPY   H_NS(_mk_internal_copy)
#define H_SET_RECURSIVE      H_NS(_set_recursive)
#define H_SET_RECURSIVE_T    H_NS(_set_recursive_t)
#define H_UNSET_RECURSIVE    H_NS(_unset_recursive)
#define H_UNSET_RECURSIVE_T  H_NS(_unset_recursive_t)
#define H_GET_LEAF           H_NS(_get_leaf)
#define H_GET_NODE_COUNT     H_NS(_get_node_count)
#define H_GET_INDEX          H_NS(_get_index)
#define H_LEAF_REHASH        H_NS(_leaf_rehash)
#define H_INTERNAL_REHASH    H_NS(_internal_rehash)
#define H_COLLISION_REHASH   H_NS(_collision_rehash)

/* Handler types */
#define H_KEY_HASH_FN  H_NS(_key_hash_fn)
#define H_KEY_EQ_FN    H_NS(_key_eq_fn)
#define H_LEAF_HOOK_FN H_NS(_leaf_hook_fn)
#define H_VAL_HASH_FN  H_NS(_val_hash_fn)

/* Static state for this instantiation */
#define H_HASH_FN    H_NS(_hash_fn)
#define H_EQ_FN      H_NS(_eq_fn)
#define H_ON_CREATE  H_NS(_on_create)
#define H_ON_DESTROY H_NS(_on_destroy)
#define H_ALLOCATOR  H_NS(_allocator)
#define H_STRUCT_KEY_HASH H_NS(_struct_key_hash)
#define H_STRUCT_VAL_HASH H_NS(_struct_val_hash)

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../platform/platform.h"

#ifdef HAMT_GC_MODE
/* --- GC mode: nodes allocated via gc_alloc, no reference counting ---
 * Caller must define HAMT_GC_ALLOC(obj_type, payload_size),
 * HAMT_GC_OBJ_INTERNAL, HAMT_GC_OBJ_LEAF, HAMT_GC_OBJ_COLLISION. */

#define H_ALLOC_LEAF(sz)       HAMT_GC_ALLOC(HAMT_GC_OBJ_LEAF, (sz))
#define H_ALLOC_INTERNAL(sz)   HAMT_GC_ALLOC(HAMT_GC_OBJ_INTERNAL, (sz))
#define H_ALLOC_COLLISION(sz)  HAMT_GC_ALLOC(HAMT_GC_OBJ_COLLISION, (sz))

/* No-op reference counting */
#define H_RC_REF(p)   (p)
#define H_RC_UNREF(p) ((void)0)
#define H_RC_COUNT(p) ((intptr_t)2)

/* Unused in GC mode but kept for cleanup section */
#define H_RC_ALLOC H_NS(_rc_alloc_unused)

#else /* !HAMT_GC_MODE */

/* --- RC mode: traditional reference-counted nodes --- */
#ifndef HAMT_ALLOCATOR
#define HAMT_ALLOCATOR    libc_allocator
#define HAMT_ALLOC_DEFAULTED 1
#endif

static Allocator H_ALLOCATOR = HAMT_ALLOCATOR;

/* Wire into rc.h template */
#define RC_ALLOCATOR H_ALLOCATOR
#define RC_NAME      HAMT_NAME
#include "../rc/rc.h"

/* RC function aliases */
#define H_RC_ALLOC H_NS(_rc_alloc)
#define H_RC_REF   H_NS(_rc_ref)
#define H_RC_UNREF H_NS(_rc_unref)
#define H_RC_COUNT H_NS(_rc_count)

/* Allocation wrappers delegate to RC */
#define H_ALLOC_LEAF(sz)       H_RC_ALLOC((sz), H_NODE_DESTROY)
#define H_ALLOC_INTERNAL(sz)   H_RC_ALLOC((sz), H_NODE_DESTROY)
#define H_ALLOC_COLLISION(sz)  H_RC_ALLOC((sz), H_NODE_DESTROY)

#endif /* HAMT_GC_MODE */

/* Iterator max depth */
#ifndef HAMT_ITER_MAX_DEPTH
#define HAMT_ITER_MAX_DEPTH           8
#define HAMT_ITER_MAX_DEPTH_DEFAULTED 1
#endif

/* --- Node type enum --- */

typedef enum { H_NODE_INTERNAL_VAL, H_NODE_LEAF_VAL, H_NODE_COLLISION_VAL } H_NODE_TYPE;

/* --- Data Structure Definitions --- */

typedef struct H_NODE {
  H_NODE_TYPE type;
  uint32_t    hash;  /* structural content hash */
} H_NODE;

typedef struct H_INTERNAL {
  H_NODE    header;
  uint32_t  bitmap;
  size_t    count;
  H_NODE*   children[];
} H_INTERNAL;

typedef struct H_LEAF {
  H_NODE     header;
  uint32_t   hash;
  uint32_t   key_stride;  /* key slots (1 = normal, >1 = inline struct key) */
  uint32_t   val_stride;  /* value slots (1 = normal, >1 = inline struct value) */
  HAMT_KEY_T slots[];     /* key_stride keys then val_stride values (use H_LEAF_KEY_PTR/H_LEAF_VAL_PTR) */
} H_LEAF;

/* Ensure value pointer from H_LEAF_VAL_PTR is correctly aligned.
 * sizeof(KEY_T) * key_stride must be a multiple of VAL_T's alignment.
 * This holds whenever sizeof(KEY_T) >= _Alignof(VAL_T), which covers
 * same-type instantiations and all practical JACL uses. */
_Static_assert(sizeof(HAMT_KEY_T) % _Alignof(HAMT_VAL_T) == 0,
               "HAMT_KEY_T size must be a multiple of HAMT_VAL_T alignment for safe H_LEAF_VAL_PTR access");

typedef struct H_COLLISION {
  H_NODE   header;
  uint32_t hash;
  uint32_t count;
  H_LEAF*  items[];
} H_COLLISION;

typedef struct H_ITER {
  H_NODE*  stack[HAMT_ITER_MAX_DEPTH];
  uint32_t index[HAMT_ITER_MAX_DEPTH];
  int      depth;
} H_ITER;

typedef struct H_ITER_RES {
  bool     done;
  H_LEAF*  item;
} H_ITER_RES;

/* --- Handler typedefs --- */

typedef uint32_t (*H_KEY_HASH_FN)(HAMT_KEY_T key);
typedef bool (*H_KEY_EQ_FN)(HAMT_KEY_T a, HAMT_KEY_T b);
typedef void (*H_LEAF_HOOK_FN)(HAMT_KEY_T key, HAMT_VAL_T value);
typedef uint32_t (*H_VAL_HASH_FN)(HAMT_VAL_T value);

/* --- Per-instantiation static state --- */

static H_KEY_HASH_FN  H_HASH_FN    = NULL;
static H_KEY_EQ_FN    H_EQ_FN      = NULL;
static H_LEAF_HOOK_FN H_ON_CREATE  = NULL;
static H_LEAF_HOOK_FN H_ON_DESTROY = NULL;
static H_KEY_HASH_FN  H_STRUCT_KEY_HASH = NULL;
static H_VAL_HASH_FN  H_STRUCT_VAL_HASH = NULL;

/* --- Key hash/eq dispatch: stride==1 uses handlers, stride>1 uses bitwise --- */

static inline uint32_t H_WIDE_KEY_HASH(const HAMT_KEY_T* keys, uint32_t key_stride) {
  if (key_stride == 1) return H_HASH_FN(keys[0]);
  /* FNV-1a over raw key bytes */
  uint32_t h = 2166136261u;
  const uint8_t* p = (const uint8_t*)keys;
  size_t len = sizeof(HAMT_KEY_T) * key_stride;
  for (size_t i = 0; i < len; i++) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

static inline bool H_WIDE_KEY_EQ(const HAMT_KEY_T* a, const HAMT_KEY_T* b, uint32_t key_stride) {
  if (key_stride == 1) return H_EQ_FN(a[0], b[0]);
  return memcmp(a, b, sizeof(HAMT_KEY_T) * key_stride) == 0;
}

/* --- Public configuration functions --- */

static inline void H_SET_KEY_HANDLERS(H_KEY_HASH_FN hash, H_KEY_EQ_FN eq) {
  H_HASH_FN = hash;
  H_EQ_FN   = eq;
}

static inline void H_SET_LIFECYCLE_HOOKS(H_LEAF_HOOK_FN on_create, H_LEAF_HOOK_FN on_destroy) {
  H_ON_CREATE  = on_create;
  H_ON_DESTROY = on_destroy;
}

static inline void H_SET_HASH_HANDLERS(H_KEY_HASH_FN key_fn, H_VAL_HASH_FN val_fn) {
  H_STRUCT_KEY_HASH = key_fn;
  H_STRUCT_VAL_HASH = val_fn;
}

/* --- Public ref counting wrappers --- */

static inline H_NODE* H_REF(H_NODE* n) {
  return (H_NODE*)H_RC_REF(n);
}

static inline void H_UNREF(H_NODE* n) {
  H_RC_UNREF(n);
}

static inline intptr_t H_REF_COUNT(void* p) {
  return H_RC_COUNT(p);
}

/* --- Helper: Bit manipulation --- */

static inline int H_GET_INDEX(uint32_t bitmap, uint32_t bit) {
  return get_popcount(bitmap & (bit - 1));
}

/* --- Node count helper --- */

static inline size_t H_GET_NODE_COUNT(H_NODE* node) {
  if (!node) return 0;
  if (node->type == H_NODE_LEAF_VAL) return 1;
  if (node->type == H_NODE_COLLISION_VAL) return ((H_COLLISION*)node)->count;
  return ((H_INTERNAL*)node)->count;
}

/* --- Structural hash: O(1) accessor --- */

static inline uint32_t H_NODE_HASH(H_NODE* node) {
  return node ? node->hash : 0;
}

/* --- Structural hash: rehash helpers --- */

static inline void H_LEAF_REHASH(H_LEAF* leaf) {
  if (!H_STRUCT_KEY_HASH || !H_STRUCT_VAL_HASH) { leaf->header.hash = 0; return; }
  const HAMT_KEY_T* keys = H_LEAF_KEY_PTR(leaf);
  const HAMT_VAL_T* vals = H_LEAF_VAL_PTR(leaf);
  uint32_t h = 0;
  for (uint32_t i = 0; i < leaf->key_stride; i++) {
    h ^= H_STRUCT_KEY_HASH(keys[i]);
  }
  for (uint32_t i = 0; i < leaf->val_stride; i++) {
    h ^= H_STRUCT_VAL_HASH(vals[i]);
  }
  leaf->header.hash = h;
}

static inline void H_INTERNAL_REHASH(H_INTERNAL* node) {
  uint32_t h = 0;
  int count = get_popcount(node->bitmap);
  for (int i = 0; i < count; i++) {
    h ^= node->children[i]->hash;
  }
  node->header.hash = h;
}

static inline void H_COLLISION_REHASH(H_COLLISION* col) {
  uint32_t h = 0;
  for (uint32_t i = 0; i < col->count; i++) {
    h ^= col->items[i]->header.hash;
  }
  col->header.hash = h;
}

/* --- Forward declaration for destructor (RC mode only) --- */

#ifndef HAMT_GC_MODE
static void H_NODE_DESTROY(void* arg);
#endif

/* --- Node construction --- */

static inline H_LEAF* H_MK_LEAF(const HAMT_KEY_T* keys, uint32_t key_stride,
                                 const HAMT_VAL_T* values, uint32_t val_stride, uint32_t hash) {
  size_t sz       = sizeof(H_LEAF) + sizeof(HAMT_KEY_T) * key_stride + sizeof(HAMT_VAL_T) * val_stride;
  H_LEAF* node    = (H_LEAF*)H_ALLOC_LEAF(sz);
  node->header    = (H_NODE){.type = H_NODE_LEAF_VAL};
  node->hash      = hash;
  node->key_stride = key_stride;
  node->val_stride = val_stride;
  memcpy(H_LEAF_KEY_PTR(node), keys, sizeof(HAMT_KEY_T) * key_stride);
  memcpy(H_LEAF_VAL_PTR(node), values, sizeof(HAMT_VAL_T) * val_stride);

  if (H_ON_CREATE) {
    H_ON_CREATE(keys[0], values[0]);
  }

  H_LEAF_REHASH(node);
  return node;
}

static inline H_INTERNAL* H_MK_INTERNAL_COPY(H_INTERNAL* old, uint32_t new_bitmap, H_NODE* new_child, int insert_idx) {
  int    old_count = get_popcount(old->bitmap);
  int    new_count = get_popcount(new_bitmap);
  size_t size      = sizeof(H_INTERNAL) + sizeof(H_NODE*) * (size_t)new_count;

  H_INTERNAL* node = (H_INTERNAL*)H_ALLOC_INTERNAL(size);
  node->header     = (H_NODE){.type = H_NODE_INTERNAL_VAL};
  node->bitmap     = new_bitmap;

  size_t total_count = 0;

  if (new_count > old_count) {
    /* Insertion */
    for (int i = 0, j = 0; i < new_count; i++) {
      if (i == insert_idx) {
        node->children[i] = new_child;
        total_count += H_GET_NODE_COUNT(new_child);
      } else {
        node->children[i] = old->children[j];
        total_count += H_GET_NODE_COUNT(old->children[j]);
        H_RC_REF(old->children[j]);
        j++;
      }
    }
  } else {
    /* Replacement: same bitmap, replace at insert_idx */
    for (int i = 0; i < old_count; i++) {
      if (i == insert_idx) {
        node->children[i] = new_child;
        total_count += H_GET_NODE_COUNT(new_child);
      } else {
        node->children[i] = old->children[i];
        total_count += H_GET_NODE_COUNT(old->children[i]);
        H_RC_REF(old->children[i]);
      }
    }
  }

  node->count = total_count;
  H_INTERNAL_REHASH(node);
  return node;
}

/* --- Node destruction (RC mode only — GC handles lifecycle) --- */

#ifndef HAMT_GC_MODE
static void H_NODE_DESTROY(void* arg) {
  H_NODE* node = (H_NODE*)arg;
  if (!node) return;

  if (node->type == H_NODE_INTERNAL_VAL) {
    H_INTERNAL* n     = (H_INTERNAL*)node;
    int         count = get_popcount(n->bitmap);
    for (int i = 0; i < count; i++) {
      H_UNREF(n->children[i]);
    }
  } else if (node->type == H_NODE_COLLISION_VAL) {
    H_COLLISION* n = (H_COLLISION*)node;
    for (uint32_t i = 0; i < n->count; i++) {
      H_UNREF((H_NODE*)n->items[i]);
    }
  } else if (node->type == H_NODE_LEAF_VAL) {
    H_LEAF* l = (H_LEAF*)node;
    if (H_ON_DESTROY) {
      H_ON_DESTROY(H_LEAF_KEY_PTR(l)[0], H_LEAF_VAL_PTR(l)[0]);
    }
  }
}
#endif /* !HAMT_GC_MODE */

/* --- Recursive set --- */

static H_NODE* H_SET_RECURSIVE(H_NODE* node, const HAMT_KEY_T* keys, uint32_t key_stride,
                               const HAMT_VAL_T* values, uint32_t val_stride, uint32_t hash, int shift) {
  if (!node) {
    return (H_NODE*)H_MK_LEAF(keys, key_stride, values, val_stride, hash);
  }

  if (node->type == H_NODE_LEAF_VAL) {
    H_LEAF* leaf = (H_LEAF*)node;
    if (H_WIDE_KEY_EQ(H_LEAF_KEY_PTR(leaf), keys, key_stride)) {
      /* Update same key */
      assert(leaf->key_stride == key_stride && leaf->val_stride == val_stride && "stride mismatch on key update");
      return (H_NODE*)H_MK_LEAF(keys, key_stride, values, val_stride, hash);
    }

    if (leaf->hash == hash) {
      /* Collision - create collision node */
      size_t       size    = sizeof(H_COLLISION) + sizeof(H_LEAF*) * 2;
      H_COLLISION* col     = (H_COLLISION*)H_ALLOC_COLLISION(size);
      col->header          = (H_NODE){.type = H_NODE_COLLISION_VAL};
      col->hash            = hash;
      col->count           = 2;
      col->items[0]        = (H_LEAF*)H_REF(node);
      col->items[1]        = H_MK_LEAF(keys, key_stride, values, val_stride, hash);
      H_COLLISION_REHASH(col);
      return (H_NODE*)col;
    }

    /* Different hashes at same shift: create internal node with both */
    uint32_t old_idx  = (leaf->hash >> shift) & 0x1F;
    uint32_t new_idx  = (hash >> shift) & 0x1F;
    uint32_t old_bit  = (1 << old_idx);
    uint32_t new_bit  = (1 << new_idx);

    if (old_idx == new_idx) {
      /* Same slot - recurse deeper */
      H_NODE* child = H_SET_RECURSIVE(node, keys, key_stride, values, val_stride, hash, shift + 5);
      size_t  size  = sizeof(H_INTERNAL) + sizeof(H_NODE*);

      H_INTERNAL* internal = (H_INTERNAL*)H_ALLOC_INTERNAL(size);
      internal->header     = (H_NODE){.type = H_NODE_INTERNAL_VAL};
      internal->bitmap     = old_bit;
      internal->children[0] = child;
      internal->count      = H_GET_NODE_COUNT(child);
      H_INTERNAL_REHASH(internal);
      return (H_NODE*)internal;
    } else {
      /* Different slots - create internal with both children */
      size_t  size  = sizeof(H_INTERNAL) + sizeof(H_NODE*) * 2;
      H_INTERNAL* internal = (H_INTERNAL*)H_ALLOC_INTERNAL(size);
      internal->header = (H_NODE){.type = H_NODE_INTERNAL_VAL};
      internal->bitmap = old_bit | new_bit;
      internal->count  = 2;

      H_LEAF* new_leaf = H_MK_LEAF(keys, key_stride, values, val_stride, hash);
      if (old_idx < new_idx) {
        internal->children[0] = H_REF(node);
        internal->children[1] = (H_NODE*)new_leaf;
      } else {
        internal->children[0] = (H_NODE*)new_leaf;
        internal->children[1] = H_REF(node);
      }
      H_INTERNAL_REHASH(internal);
      return (H_NODE*)internal;
    }
  }

  if (node->type == H_NODE_COLLISION_VAL) {
    H_COLLISION* col = (H_COLLISION*)node;
    if (col->hash != hash) {
      /* Different hash - wrap collision in internal node and add new leaf */
      uint32_t col_idx = (col->hash >> shift) & 0x1F;
      uint32_t new_idx = (hash >> shift) & 0x1F;
      uint32_t col_bit = (1 << col_idx);
      uint32_t new_bit = (1 << new_idx);

      if (col_idx == new_idx) {
        /* Recurse deeper */
        H_NODE* child = H_SET_RECURSIVE(node, keys, key_stride, values, val_stride, hash, shift + 5);
        size_t  size  = sizeof(H_INTERNAL) + sizeof(H_NODE*);

        H_INTERNAL* internal  = (H_INTERNAL*)H_ALLOC_INTERNAL(size);
        internal->header      = (H_NODE){.type = H_NODE_INTERNAL_VAL};
        internal->bitmap      = col_bit;
        internal->children[0] = child;
        internal->count       = H_GET_NODE_COUNT(child);
        H_INTERNAL_REHASH(internal);
        return (H_NODE*)internal;
      } else {
        size_t  size  = sizeof(H_INTERNAL) + sizeof(H_NODE*) * 2;
        H_INTERNAL* internal = (H_INTERNAL*)H_ALLOC_INTERNAL(size);
        internal->header = (H_NODE){.type = H_NODE_INTERNAL_VAL};
        internal->bitmap = col_bit | new_bit;
        internal->count  = col->count + 1;

        H_LEAF* new_leaf = H_MK_LEAF(keys, key_stride, values, val_stride, hash);
        if (col_idx < new_idx) {
          internal->children[0] = H_REF(node);
          internal->children[1] = (H_NODE*)new_leaf;
        } else {
          internal->children[0] = (H_NODE*)new_leaf;
          internal->children[1] = H_REF(node);
        }
        H_INTERNAL_REHASH(internal);
        return (H_NODE*)internal;
      }
    }

    /* Same hash - update or extend collision */
    for (uint32_t i = 0; i < col->count; i++) {
      if (H_WIDE_KEY_EQ(H_LEAF_KEY_PTR(col->items[i]), keys, key_stride)) {
        assert(col->items[i]->key_stride == key_stride && col->items[i]->val_stride == val_stride && "stride mismatch on key update");
        /* Update existing entry */
        size_t       size    = sizeof(H_COLLISION) + sizeof(H_LEAF*) * col->count;
        H_COLLISION* new_col = (H_COLLISION*)H_ALLOC_COLLISION(size);
        new_col->header      = (H_NODE){.type = H_NODE_COLLISION_VAL};
        new_col->hash        = hash;
        new_col->count       = col->count;
        for (uint32_t j = 0; j < col->count; j++) {
          if (j == i) {
            new_col->items[j] = H_MK_LEAF(keys, key_stride, values, val_stride, hash);
          } else {
            new_col->items[j] = col->items[j];
            H_RC_REF((H_NODE*)col->items[j]);
          }
        }
        H_COLLISION_REHASH(new_col);
        return (H_NODE*)new_col;
      }
    }

    /* Add new entry to collision */
    size_t       size    = sizeof(H_COLLISION) + sizeof(H_LEAF*) * (col->count + 1);
    H_COLLISION* new_col = (H_COLLISION*)H_ALLOC_COLLISION(size);
    new_col->header      = (H_NODE){.type = H_NODE_COLLISION_VAL};
    new_col->hash        = hash;
    new_col->count       = col->count + 1;
    for (uint32_t i = 0; i < col->count; i++) {
      new_col->items[i] = col->items[i];
      H_RC_REF((H_NODE*)col->items[i]);
    }
    new_col->items[col->count] = H_MK_LEAF(keys, key_stride, values, val_stride, hash);
    H_COLLISION_REHASH(new_col);
    return (H_NODE*)new_col;
  }

  /* Internal node */
  H_INTERNAL* internal = (H_INTERNAL*)node;
  uint32_t    bit_idx  = (hash >> shift) & 0x1F;
  uint32_t    bit      = (1 << bit_idx);
  int         arr_idx  = H_GET_INDEX(internal->bitmap, bit);

  if (internal->bitmap & bit) {
    /* Child exists, recurse */
    H_NODE* child     = internal->children[arr_idx];
    H_NODE* new_child = H_SET_RECURSIVE(child, keys, key_stride, values, val_stride, hash, shift + 5);

    if (new_child == child) {
      H_UNREF(new_child);
      return H_REF(node);
    }

    H_INTERNAL* new_internal = H_MK_INTERNAL_COPY(internal, internal->bitmap, new_child, arr_idx);
    return (H_NODE*)new_internal;
  } else {
    /* Child missing, insert new leaf */
    H_NODE*     new_leaf     = (H_NODE*)H_MK_LEAF(keys, key_stride, values, val_stride, hash);
    H_INTERNAL* new_internal = H_MK_INTERNAL_COPY(internal, internal->bitmap | bit, new_leaf, arr_idx);
    return (H_NODE*)new_internal;
  }
}

/* --- Recursive unset --- */

static H_NODE* H_UNSET_RECURSIVE(H_NODE* node, const HAMT_KEY_T* keys, uint32_t key_stride, uint32_t hash, int shift) {
  if (!node) return NULL;

  if (node->type == H_NODE_LEAF_VAL) {
    H_LEAF* leaf = (H_LEAF*)node;
    if (H_WIDE_KEY_EQ(H_LEAF_KEY_PTR(leaf), keys, key_stride)) return NULL;
    return H_REF(node);
  }

  if (node->type == H_NODE_COLLISION_VAL) {
    H_COLLISION* col = (H_COLLISION*)node;

    int found = -1;
    for (uint32_t i = 0; i < col->count; i++) {
      if (H_WIDE_KEY_EQ(H_LEAF_KEY_PTR(col->items[i]), keys, key_stride)) {
        found = (int)i;
        break;
      }
    }
    if (found == -1) return H_REF(node);

    if (col->count == 2) {
      int keep = (found == 0) ? 1 : 0;
      return H_REF((H_NODE*)col->items[keep]);
    }

    /* Shrink collision node */
    size_t       size    = sizeof(H_COLLISION) + sizeof(H_LEAF*) * (col->count - 1);
    H_COLLISION* new_col = (H_COLLISION*)H_ALLOC_COLLISION(size);
    new_col->header      = (H_NODE){.type = H_NODE_COLLISION_VAL};
    new_col->hash        = hash;
    new_col->count       = col->count - 1;
    int k                = 0;
    for (uint32_t i = 0; i < col->count; i++) {
      if ((int)i == found) continue;
      new_col->items[k++] = col->items[i];
      H_RC_REF((H_NODE*)col->items[i]);
    }
    H_COLLISION_REHASH(new_col);
    return (H_NODE*)new_col;
  }

  H_INTERNAL* internal = (H_INTERNAL*)node;
  uint32_t    bit_idx  = (hash >> shift) & 0x1F;
  uint32_t    bit      = (1 << bit_idx);

  if (!(internal->bitmap & bit)) return H_REF(node);

  int     arr_idx      = H_GET_INDEX(internal->bitmap, bit);
  H_NODE* result_child = H_UNSET_RECURSIVE(internal->children[arr_idx], keys, key_stride, hash, shift + 5);

  if (!result_child) {
    /* Child removed completely */
    uint32_t new_bitmap = internal->bitmap & ~bit;
    if (new_bitmap == 0) return NULL;

    int    new_count = get_popcount(new_bitmap);
    size_t size      = sizeof(H_INTERNAL) + sizeof(H_NODE*) * (size_t)new_count;

    H_INTERNAL* new_node = (H_INTERNAL*)H_ALLOC_INTERNAL(size);
    new_node->header     = (H_NODE){.type = H_NODE_INTERNAL_VAL};
    new_node->bitmap     = new_bitmap;

    size_t total_count = 0;
    int    k           = 0;
    for (int i = 0; i < get_popcount(internal->bitmap); i++) {
      if (i == arr_idx) continue;
      new_node->children[k++] = internal->children[i];
      total_count += H_GET_NODE_COUNT(internal->children[i]);
      H_RC_REF(internal->children[i]);
    }
    new_node->count = total_count;
    H_INTERNAL_REHASH(new_node);
    return (H_NODE*)new_node;
  } else {
    if (result_child == internal->children[arr_idx]) {
      H_UNREF(result_child);
      return H_REF(node);
    }

    H_INTERNAL* new_node = H_MK_INTERNAL_COPY(internal, internal->bitmap, result_child, arr_idx);
    return (H_NODE*)new_node;
  }
}

/* --- Public API --- */

static inline H_NODE* H_SET(H_NODE* root, HAMT_KEY_T key, HAMT_VAL_T value) {
  uint32_t h = H_HASH_FN(key);
  return H_SET_RECURSIVE(root, &key, 1, &value, 1, h, 0);
}

static inline H_NODE* H_SET_WIDE(H_NODE* root, const HAMT_KEY_T* keys, uint32_t key_stride,
                                  const HAMT_VAL_T* values, uint32_t val_stride) {
  uint32_t h = H_WIDE_KEY_HASH(keys, key_stride);
  return H_SET_RECURSIVE(root, keys, key_stride, values, val_stride, h, 0);
}

static inline H_NODE* H_UNSET(H_NODE* root, HAMT_KEY_T key) {
  uint32_t h = H_HASH_FN(key);
  return H_UNSET_RECURSIVE(root, &key, 1, h, 0);
}

static inline H_NODE* H_UNSET_WIDE(H_NODE* root, const HAMT_KEY_T* keys, uint32_t key_stride) {
  uint32_t h = H_WIDE_KEY_HASH(keys, key_stride);
  return H_UNSET_RECURSIVE(root, keys, key_stride, h, 0);
}

static inline H_LEAF* H_GET_LEAF(H_NODE* root, const HAMT_KEY_T* keys, uint32_t key_stride) {
  if (!root) return NULL;
  uint32_t h     = H_WIDE_KEY_HASH(keys, key_stride);
  int      shift = 0;
  H_NODE*  node  = root;

  while (node) {
    if (node->type == H_NODE_LEAF_VAL) {
      H_LEAF* leaf = (H_LEAF*)node;
      return H_WIDE_KEY_EQ(H_LEAF_KEY_PTR(leaf), keys, key_stride) ? leaf : NULL;
    }

    if (node->type == H_NODE_COLLISION_VAL) {
      H_COLLISION* col = (H_COLLISION*)node;
      if (col->hash != h) return NULL;
      for (uint32_t i = 0; i < col->count; i++) {
        if (H_WIDE_KEY_EQ(H_LEAF_KEY_PTR(col->items[i]), keys, key_stride)) return col->items[i];
      }
      return NULL;
    }

    H_INTERNAL* in  = (H_INTERNAL*)node;
    uint32_t    bit = (1 << ((h >> shift) & 0x1F));
    if (!(in->bitmap & bit)) return NULL;

    int idx = H_GET_INDEX(in->bitmap, bit);
    node    = in->children[idx];
    shift  += 5;
  }
  return NULL;
}

static inline bool H_HAS(H_NODE* root, HAMT_KEY_T key) {
  return H_GET_LEAF(root, &key, 1) != NULL;
}

static inline bool H_HAS_WIDE(H_NODE* root, const HAMT_KEY_T* keys, uint32_t key_stride) {
  return H_GET_LEAF(root, keys, key_stride) != NULL;
}

static inline HAMT_VAL_T H_GET(H_NODE* root, HAMT_KEY_T key) {
  H_LEAF* leaf = H_GET_LEAF(root, &key, 1);
  if (!leaf) return (HAMT_VAL_T){0};
  assert(leaf->val_stride == 1 && "use get_ptr for strided maps");
  return H_LEAF_VAL_PTR(leaf)[0];
}

static inline HAMT_VAL_T H_GET_OR_DEFAULT(H_NODE* root, HAMT_KEY_T key, HAMT_VAL_T default_value) {
  H_LEAF* leaf = H_GET_LEAF(root, &key, 1);
  if (!leaf) return default_value;
  assert(leaf->val_stride == 1 && "use get_ptr for strided maps");
  return H_LEAF_VAL_PTR(leaf)[0];
}

static inline const HAMT_VAL_T* H_GET_PTR(H_NODE* root, HAMT_KEY_T key) {
  H_LEAF* leaf = H_GET_LEAF(root, &key, 1);
  if (!leaf) return NULL;
  return H_LEAF_VAL_PTR(leaf);
}

static inline size_t H_COUNT(H_NODE* root) {
  return H_GET_NODE_COUNT(root);
}

static inline HAMT_KEY_T H_KEY_FROM_LEAF(H_LEAF* leaf) {
  if (!leaf) return (HAMT_KEY_T){0};
  assert(leaf->key_stride == 1 && "use key_ptr_from_leaf for strided keys");
  return H_LEAF_KEY_PTR(leaf)[0];
}

static inline const HAMT_KEY_T* H_KEY_PTR_FROM_LEAF(H_LEAF* leaf) {
  if (!leaf) return NULL;
  return H_LEAF_KEY_PTR(leaf);
}

static inline HAMT_VAL_T H_VALUE_FROM_LEAF(H_LEAF* leaf) {
  if (!leaf) return (HAMT_VAL_T){0};
  assert(leaf->val_stride == 1 && "use value_ptr_from_leaf for strided maps");
  return H_LEAF_VAL_PTR(leaf)[0];
}

static inline const HAMT_VAL_T* H_VALUE_PTR_FROM_LEAF(H_LEAF* leaf) {
  if (!leaf) return NULL;
  return H_LEAF_VAL_PTR(leaf);
}

/* --- Iterator API --- */

static inline H_ITER H_ITER_INIT(H_NODE* root) {
  H_ITER it = {.depth = 0};
  if (root) {
    it.stack[0] = root;
    it.index[0] = 0;
    it.depth    = 1;
  }
  return it;
}

static inline H_ITER_RES H_NEXT_LEAF(H_ITER* it) {
  while (it->depth > 0) {
    int      top  = it->depth - 1;
    H_NODE*  node = it->stack[top];
    uint32_t idx  = it->index[top];

    if (node->type == H_NODE_LEAF_VAL) {
      it->depth--;
      return (H_ITER_RES){.done = false, .item = (H_LEAF*)node};
    }

    if (node->type == H_NODE_COLLISION_VAL) {
      H_COLLISION* col = (H_COLLISION*)node;
      if (idx < col->count) {
        it->index[top]++;
        return (H_ITER_RES){.done = false, .item = col->items[idx]};
      }
      it->depth--;
      continue;
    }

    H_INTERNAL* in    = (H_INTERNAL*)node;
    int         count = get_popcount(in->bitmap);
    if (idx < (uint32_t)count) {
      it->index[top]++;
      it->stack[it->depth] = in->children[idx];
      it->index[it->depth] = 0;
      it->depth++;
    } else {
      it->depth--;
    }
  }
  return (H_ITER_RES){.done = true, .item = NULL};
}

static inline H_ITER_RES H_NEXT_KEY(H_ITER* it) {
  H_ITER_RES result = H_NEXT_LEAF(it);
  return (H_ITER_RES){.done = result.done, .item = result.item};
}

static inline H_ITER_RES H_NEXT_VALUE(H_ITER* it) {
  H_ITER_RES result = H_NEXT_LEAF(it);
  return (H_ITER_RES){.done = result.done, .item = result.item};
}

/* --- Transient API (RC mode only — requires ref counting for ownership checks) --- */

#ifndef HAMT_GC_MODE

#define H_TRANSIENT          H_NS(_transient)
#define H_TRANSIENT_SET      H_NS(_transient_set)
#define H_TRANSIENT_UNSET    H_NS(_transient_unset)
#define H_TRANSIENT_FN       H_NS(_transient_fn)
#define H_PERSISTENT_FN      H_NS(_persistent_fn)
#define H_TRANSIENT_ITER_INIT H_NS(_transient_iter_init)

/* Sentinel owner value: no real thread should match */
static const thread_t H_NS(_invalid_owner) = (thread_t)0;

typedef struct H_TRANSIENT {
  H_NODE*  root;
  size_t   count;
  thread_t owner;
} H_TRANSIENT;

static inline void H_NS(_transient_destroy)(void* arg) {
  H_TRANSIENT* t = (H_TRANSIENT*)arg;
  if (!t) return;
  if (t->root) H_RC_UNREF(t->root);
}

static inline H_TRANSIENT* H_TRANSIENT_FN(H_NODE* root) {
  H_TRANSIENT* t = (H_TRANSIENT*)H_RC_ALLOC(sizeof(H_TRANSIENT), H_NS(_transient_destroy));
  t->root  = root ? (H_NODE*)H_RC_REF(root) : NULL;
  t->count = H_GET_NODE_COUNT(root);
  t->owner = THREAD_SELF();
  return t;
}

/* --- Transient recursive set (top-down owned/shared check) --- */

static H_NODE* H_SET_RECURSIVE_T(H_NODE* node, const HAMT_KEY_T* keys, uint32_t key_stride,
                                 const HAMT_VAL_T* values, uint32_t val_stride, uint32_t hash, int shift) {
  if (!node) {
    return (H_NODE*)H_MK_LEAF(keys, key_stride, values, val_stride, hash);
  }

  if (node->type == H_NODE_LEAF_VAL) {
    H_LEAF* leaf = (H_LEAF*)node;
    if (H_WIDE_KEY_EQ(H_LEAF_KEY_PTR(leaf), keys, key_stride)) {
      /* Update same key — always create new leaf */
      assert(leaf->key_stride == key_stride && leaf->val_stride == val_stride && "stride mismatch on key update");
      return (H_NODE*)H_MK_LEAF(keys, key_stride, values, val_stride, hash);
    }

    if (leaf->hash == hash) {
      /* Collision — create collision node */
      size_t       size = sizeof(H_COLLISION) + sizeof(H_LEAF*) * 2;
      H_COLLISION* col  = (H_COLLISION*)H_ALLOC_COLLISION(size);
      col->header       = (H_NODE){.type = H_NODE_COLLISION_VAL};
      col->hash         = hash;
      col->count        = 2;
      col->items[0]     = (H_LEAF*)H_REF(node);
      col->items[1]     = H_MK_LEAF(keys, key_stride, values, val_stride, hash);
      H_COLLISION_REHASH(col);
      return (H_NODE*)col;
    }

    /* Different hashes — create internal node with both */
    uint32_t old_idx = (leaf->hash >> shift) & 0x1F;
    uint32_t new_idx = (hash >> shift) & 0x1F;
    uint32_t old_bit = (1 << old_idx);
    uint32_t new_bit = (1 << new_idx);

    if (old_idx == new_idx) {
      H_NODE* child = H_SET_RECURSIVE_T(node, keys, key_stride, values, val_stride, hash, shift + 5);
      size_t  size  = sizeof(H_INTERNAL) + sizeof(H_NODE*);

      H_INTERNAL* internal  = (H_INTERNAL*)H_ALLOC_INTERNAL(size);
      internal->header      = (H_NODE){.type = H_NODE_INTERNAL_VAL};
      internal->bitmap      = old_bit;
      internal->children[0] = child;
      internal->count       = H_GET_NODE_COUNT(child);
      H_INTERNAL_REHASH(internal);
      return (H_NODE*)internal;
    } else {
      size_t      size     = sizeof(H_INTERNAL) + sizeof(H_NODE*) * 2;
      H_INTERNAL* internal = (H_INTERNAL*)H_ALLOC_INTERNAL(size);
      internal->header     = (H_NODE){.type = H_NODE_INTERNAL_VAL};
      internal->bitmap     = old_bit | new_bit;
      internal->count      = 2;

      H_LEAF* new_leaf = H_MK_LEAF(keys, key_stride, values, val_stride, hash);
      if (old_idx < new_idx) {
        internal->children[0] = H_REF(node);
        internal->children[1] = (H_NODE*)new_leaf;
      } else {
        internal->children[0] = (H_NODE*)new_leaf;
        internal->children[1] = H_REF(node);
      }
      H_INTERNAL_REHASH(internal);
      return (H_NODE*)internal;
    }
  }

  if (node->type == H_NODE_COLLISION_VAL) {
    H_COLLISION* col = (H_COLLISION*)node;
    if (col->hash != hash) {
      /* Different hash — wrap collision in internal node */
      uint32_t col_idx = (col->hash >> shift) & 0x1F;
      uint32_t new_idx = (hash >> shift) & 0x1F;
      uint32_t col_bit = (1 << col_idx);
      uint32_t new_bit = (1 << new_idx);

      if (col_idx == new_idx) {
        H_NODE* child = H_SET_RECURSIVE_T(node, keys, key_stride, values, val_stride, hash, shift + 5);
        size_t  size  = sizeof(H_INTERNAL) + sizeof(H_NODE*);

        H_INTERNAL* internal  = (H_INTERNAL*)H_ALLOC_INTERNAL(size);
        internal->header      = (H_NODE){.type = H_NODE_INTERNAL_VAL};
        internal->bitmap      = col_bit;
        internal->children[0] = child;
        internal->count       = H_GET_NODE_COUNT(child);
        H_INTERNAL_REHASH(internal);
        return (H_NODE*)internal;
      } else {
        size_t      size     = sizeof(H_INTERNAL) + sizeof(H_NODE*) * 2;
        H_INTERNAL* internal = (H_INTERNAL*)H_ALLOC_INTERNAL(size);
        internal->header     = (H_NODE){.type = H_NODE_INTERNAL_VAL};
        internal->bitmap     = col_bit | new_bit;
        internal->count      = col->count + 1;

        H_LEAF* new_leaf = H_MK_LEAF(keys, key_stride, values, val_stride, hash);
        if (col_idx < new_idx) {
          internal->children[0] = H_REF(node);
          internal->children[1] = (H_NODE*)new_leaf;
        } else {
          internal->children[0] = (H_NODE*)new_leaf;
          internal->children[1] = H_REF(node);
        }
        H_INTERNAL_REHASH(internal);
        return (H_NODE*)internal;
      }
    }

    /* Same hash — check for update */
    for (uint32_t i = 0; i < col->count; i++) {
      if (H_WIDE_KEY_EQ(H_LEAF_KEY_PTR(col->items[i]), keys, key_stride)) {
        assert(col->items[i]->key_stride == key_stride && col->items[i]->val_stride == val_stride && "stride mismatch on key update");
        if (H_RC_COUNT(node) == 1) {
          /* Owned — replace item in place */
          H_LEAF* new_leaf = H_MK_LEAF(keys, key_stride, values, val_stride, hash);
          H_RC_UNREF((H_NODE*)col->items[i]);
          col->items[i] = new_leaf;
          H_COLLISION_REHASH(col);
          return node;
        } else {
          /* Shared — copy collision node */
          size_t       size    = sizeof(H_COLLISION) + sizeof(H_LEAF*) * col->count;
          H_COLLISION* new_col = (H_COLLISION*)H_ALLOC_COLLISION(size);
          new_col->header      = (H_NODE){.type = H_NODE_COLLISION_VAL};
          new_col->hash        = hash;
          new_col->count       = col->count;
          for (uint32_t j = 0; j < col->count; j++) {
            if (j == i) {
              new_col->items[j] = H_MK_LEAF(keys, key_stride, values, val_stride, hash);
            } else {
              new_col->items[j] = col->items[j];
              H_RC_REF((H_NODE*)col->items[j]);
            }
          }
          H_COLLISION_REHASH(new_col);
          return (H_NODE*)new_col;
        }
      }
    }

    /* Add new entry to collision — always allocate new (size changes) */
    {
      size_t       size    = sizeof(H_COLLISION) + sizeof(H_LEAF*) * (col->count + 1);
      H_COLLISION* new_col = (H_COLLISION*)H_ALLOC_COLLISION(size);
      new_col->header      = (H_NODE){.type = H_NODE_COLLISION_VAL};
      new_col->hash        = hash;
      new_col->count       = col->count + 1;
      for (uint32_t i = 0; i < col->count; i++) {
        new_col->items[i] = col->items[i];
        H_RC_REF((H_NODE*)col->items[i]);
      }
      new_col->items[col->count] = H_MK_LEAF(keys, key_stride, values, val_stride, hash);
      H_COLLISION_REHASH(new_col);
      return (H_NODE*)new_col;
    }
  }

  /* Internal node — top-down owned/shared check */
  H_INTERNAL* internal = (H_INTERNAL*)node;
  uint32_t    bit_idx  = (hash >> shift) & 0x1F;
  uint32_t    bit      = (1 << bit_idx);
  int         arr_idx  = H_GET_INDEX(internal->bitmap, bit);

  if (internal->bitmap & bit) {
    /* Child exists — recurse */
    if (H_RC_COUNT(node) == 1) {
      /* Owned — modify in place */
      H_NODE* child           = internal->children[arr_idx];
      size_t  old_child_count = H_GET_NODE_COUNT(child);
      H_NODE* new_child       = H_SET_RECURSIVE_T(child, keys, key_stride, values, val_stride, hash, shift + 5);

      if (new_child != child) {
        H_RC_UNREF(child);
        internal->children[arr_idx] = new_child;
      }
      internal->count = internal->count - old_child_count + H_GET_NODE_COUNT(internal->children[arr_idx]);
      H_INTERNAL_REHASH(internal);
      return node;
    } else {
      /* Shared — copy first (top-down), then recurse on copy */
      int    cnt  = get_popcount(internal->bitmap);
      size_t size = sizeof(H_INTERNAL) + sizeof(H_NODE*) * (size_t)cnt;

      H_INTERNAL* work = (H_INTERNAL*)H_ALLOC_INTERNAL(size);
      work->header     = (H_NODE){.type = H_NODE_INTERNAL_VAL};
      work->bitmap     = internal->bitmap;
      work->count      = internal->count;
      for (int i = 0; i < cnt; i++) {
        work->children[i] = internal->children[i];
        H_RC_REF(internal->children[i]);
      }

      H_NODE* child           = work->children[arr_idx];
      size_t  old_child_count = H_GET_NODE_COUNT(child);
      H_NODE* new_child       = H_SET_RECURSIVE_T(child, keys, key_stride, values, val_stride, hash, shift + 5);

      if (new_child != child) {
        H_RC_UNREF(child);
        work->children[arr_idx] = new_child;
      }
      work->count = work->count - old_child_count + H_GET_NODE_COUNT(work->children[arr_idx]);
      H_INTERNAL_REHASH(work);
      return (H_NODE*)work;
    }
  } else {
    /* Child missing — insert new leaf */
    H_NODE* new_leaf = (H_NODE*)H_MK_LEAF(keys, key_stride, values, val_stride, hash);

    /* Insertion always needs new allocation (size changes) — caller handles unref */
    {
      H_INTERNAL* new_internal = H_MK_INTERNAL_COPY(internal, internal->bitmap | bit, new_leaf, arr_idx);
      return (H_NODE*)new_internal;
    }
  }
}

/* --- Transient recursive unset (top-down owned/shared check) --- */

static H_NODE* H_UNSET_RECURSIVE_T(H_NODE* node, const HAMT_KEY_T* keys, uint32_t key_stride, uint32_t hash, int shift) {
  if (!node) return NULL;

  if (node->type == H_NODE_LEAF_VAL) {
    H_LEAF* leaf = (H_LEAF*)node;
    if (H_WIDE_KEY_EQ(H_LEAF_KEY_PTR(leaf), keys, key_stride)) return NULL;
    return node; /* not found — return same pointer (no ref change for transient) */
  }

  if (node->type == H_NODE_COLLISION_VAL) {
    H_COLLISION* col = (H_COLLISION*)node;

    int found = -1;
    for (uint32_t i = 0; i < col->count; i++) {
      if (H_WIDE_KEY_EQ(H_LEAF_KEY_PTR(col->items[i]), keys, key_stride)) {
        found = (int)i;
        break;
      }
    }
    if (found == -1) return node; /* not found */

    if (col->count == 2) {
      /* Collapse to leaf */
      int keep = (found == 0) ? 1 : 0;
      return H_REF((H_NODE*)col->items[keep]);
    }

    if (H_RC_COUNT(node) == 1) {
      /* Owned — allocate new smaller collision, free old */
      size_t       size    = sizeof(H_COLLISION) + sizeof(H_LEAF*) * (col->count - 1);
      H_COLLISION* new_col = (H_COLLISION*)H_ALLOC_COLLISION(size);
      new_col->header      = (H_NODE){.type = H_NODE_COLLISION_VAL};
      new_col->hash        = hash;
      new_col->count       = col->count - 1;
      int k = 0;
      for (uint32_t i = 0; i < col->count; i++) {
        if ((int)i == found) continue;
        new_col->items[k++] = col->items[i];
        H_RC_REF((H_NODE*)col->items[i]);
      }
      H_COLLISION_REHASH(new_col);
      return (H_NODE*)new_col;
    } else {
      /* Shared — copy without removed item */
      size_t       size    = sizeof(H_COLLISION) + sizeof(H_LEAF*) * (col->count - 1);
      H_COLLISION* new_col = (H_COLLISION*)H_ALLOC_COLLISION(size);
      new_col->header      = (H_NODE){.type = H_NODE_COLLISION_VAL};
      new_col->hash        = hash;
      new_col->count       = col->count - 1;
      int k = 0;
      for (uint32_t i = 0; i < col->count; i++) {
        if ((int)i == found) continue;
        new_col->items[k++] = col->items[i];
        H_RC_REF((H_NODE*)col->items[i]);
      }
      H_COLLISION_REHASH(new_col);
      return (H_NODE*)new_col;
    }
  }

  /* Internal node */
  H_INTERNAL* internal = (H_INTERNAL*)node;
  uint32_t    bit_idx  = (hash >> shift) & 0x1F;
  uint32_t    bit      = (1 << bit_idx);

  if (!(internal->bitmap & bit)) return node; /* not found */

  int arr_idx = H_GET_INDEX(internal->bitmap, bit);

  if (H_RC_COUNT(node) == 1) {
    /* Owned — modify in place where possible */
    H_NODE* child       = internal->children[arr_idx];
    size_t  old_child_count = H_GET_NODE_COUNT(child);
    H_NODE* result_child = H_UNSET_RECURSIVE_T(child, keys, key_stride, hash, shift + 5);

    if (result_child == child) {
      /* Child may have been modified in-place — update count */
      internal->count = internal->count - old_child_count + H_GET_NODE_COUNT(child);
      H_INTERNAL_REHASH(internal);
      return node;
    }

    if (!result_child) {
      /* Child removed completely — need new smaller internal (size changes) */
      uint32_t new_bitmap = internal->bitmap & ~bit;
      if (new_bitmap == 0) return NULL;

      int    new_count = get_popcount(new_bitmap);
      size_t size      = sizeof(H_INTERNAL) + sizeof(H_NODE*) * (size_t)new_count;

      H_INTERNAL* new_node = (H_INTERNAL*)H_ALLOC_INTERNAL(size);
      new_node->header     = (H_NODE){.type = H_NODE_INTERNAL_VAL};
      new_node->bitmap     = new_bitmap;

      size_t total_count = 0;
      int    k           = 0;
      int    old_count   = get_popcount(internal->bitmap);
      for (int i = 0; i < old_count; i++) {
        if (i == arr_idx) continue;
        new_node->children[k] = internal->children[i];
        total_count += H_GET_NODE_COUNT(internal->children[i]);
        H_RC_REF(internal->children[i]);
        k++;
      }
      new_node->count = total_count;
      H_INTERNAL_REHASH(new_node);
      return (H_NODE*)new_node;
    } else {
      /* Child replaced — update in place */
      H_RC_UNREF(child);
      internal->children[arr_idx] = result_child;
      internal->count = internal->count - old_child_count + H_GET_NODE_COUNT(result_child);
      H_INTERNAL_REHASH(internal);
      return node;
    }
  } else {
    /* Shared — copy first (top-down), then recurse on copy */
    int    cnt  = get_popcount(internal->bitmap);
    size_t size = sizeof(H_INTERNAL) + sizeof(H_NODE*) * (size_t)cnt;

    H_INTERNAL* work = (H_INTERNAL*)H_ALLOC_INTERNAL(size);
    work->header     = (H_NODE){.type = H_NODE_INTERNAL_VAL};
    work->bitmap     = internal->bitmap;
    work->count      = internal->count;
    for (int i = 0; i < cnt; i++) {
      work->children[i] = internal->children[i];
      H_RC_REF(internal->children[i]);
    }

    H_NODE* child       = work->children[arr_idx];
    size_t  old_child_count = H_GET_NODE_COUNT(child);
    H_NODE* result_child = H_UNSET_RECURSIVE_T(child, keys, key_stride, hash, shift + 5);

    if (result_child == child) {
      /* Not found — discard copy */
      H_RC_UNREF((H_NODE*)work);
      return node;
    }

    if (!result_child) {
      /* Child removed completely — need new smaller internal.
       * Don't unref child explicitly — work's destructor handles it. */
      uint32_t new_bitmap = work->bitmap & ~bit;

      if (new_bitmap == 0) {
        H_RC_UNREF((H_NODE*)work);
        return NULL;
      }

      int    new_count = get_popcount(new_bitmap);
      size_t new_size  = sizeof(H_INTERNAL) + sizeof(H_NODE*) * (size_t)new_count;

      H_INTERNAL* new_node = (H_INTERNAL*)H_ALLOC_INTERNAL(new_size);
      new_node->header     = (H_NODE){.type = H_NODE_INTERNAL_VAL};
      new_node->bitmap     = new_bitmap;

      size_t total_count = 0;
      int    k           = 0;
      for (int i = 0; i < cnt; i++) {
        if (i == arr_idx) continue;
        new_node->children[k] = work->children[i];
        total_count += H_GET_NODE_COUNT(work->children[i]);
        H_RC_REF(work->children[i]);
        k++;
      }
      new_node->count = total_count;
      H_INTERNAL_REHASH(new_node);
      H_RC_UNREF((H_NODE*)work);
      return (H_NODE*)new_node;
    } else {
      /* Child replaced */
      H_RC_UNREF(child);
      work->children[arr_idx] = result_child;
      work->count = work->count - old_child_count + H_GET_NODE_COUNT(result_child);
      H_INTERNAL_REHASH(work);
      return (H_NODE*)work;
    }
  }
}

/* --- Transient unset public API --- */

static inline H_TRANSIENT* H_TRANSIENT_UNSET(H_TRANSIENT* t, HAMT_KEY_T key) {
  if (!t) return NULL;
  if (THREAD_EQUAL(t->owner, H_NS(_invalid_owner))) return NULL;
  if (!THREAD_EQUAL(THREAD_SELF(), t->owner)) {
#ifndef NDEBUG
    assert(0 && "transient used by non-owner thread");
#endif
    return NULL;
  }

  uint32_t h        = H_HASH_FN(key);
  H_NODE*  new_root = H_UNSET_RECURSIVE_T(t->root, &key, 1, h, 0);

  if (new_root != t->root) {
    if (t->root) H_RC_UNREF(t->root);
    t->root = new_root;
  }
  t->count = H_GET_NODE_COUNT(t->root);
  return t;
}

/* --- Transient set public API --- */

static inline H_TRANSIENT* H_TRANSIENT_SET(H_TRANSIENT* t, HAMT_KEY_T key, HAMT_VAL_T value) {
  if (!t) return NULL;
  if (THREAD_EQUAL(t->owner, H_NS(_invalid_owner))) return NULL;
  if (!THREAD_EQUAL(THREAD_SELF(), t->owner)) {
#ifndef NDEBUG
    assert(0 && "transient used by non-owner thread");
#endif
    return NULL;
  }

  uint32_t h        = H_HASH_FN(key);
  H_NODE*  new_root = H_SET_RECURSIVE_T(t->root, &key, 1, &value, 1, h, 0);

  if (new_root != t->root) {
    if (t->root) H_RC_UNREF(t->root);
    t->root = new_root;
  }
  t->count = H_GET_NODE_COUNT(t->root);
  return t;
}

/* --- Transient iteration --- */

static inline H_ITER H_TRANSIENT_ITER_INIT(H_TRANSIENT* t) {
  if (!t) return (H_ITER){.depth = 0};
  if (THREAD_EQUAL(t->owner, H_NS(_invalid_owner))) return (H_ITER){.depth = 0};
  if (!THREAD_EQUAL(THREAD_SELF(), t->owner)) {
#ifndef NDEBUG
    assert(0 && "transient used by non-owner thread");
#endif
    return (H_ITER){.depth = 0};
  }
  return H_ITER_INIT(t->root);
}

static inline H_NODE* H_PERSISTENT_FN(H_TRANSIENT* t) {
  if (!t) return NULL;
  /* Check if already invalidated */
  if (THREAD_EQUAL(t->owner, H_NS(_invalid_owner))) {
    return NULL;
  }
  /* Owner check */
  if (!THREAD_EQUAL(THREAD_SELF(), t->owner)) {
#ifndef NDEBUG
    assert(0 && "transient used by non-owner thread");
#endif
    return NULL;
  }
  H_NODE* root = t->root ? (H_NODE*)H_RC_REF(t->root) : NULL;
  /* Invalidate the transient */
  t->owner = H_NS(_invalid_owner);
  return root;
}

#endif /* !HAMT_GC_MODE (transient API) */

/* --- Cleanup all internal macros --- */

#undef HAMT_KEY_T
#undef HAMT_VAL_T
#undef HAMT_NAME

#undef H_XCAT
#undef H_CAT
#undef H_NS

#undef H_NODE_TYPE
#undef H_NODE
#undef H_INTERNAL
#undef H_LEAF
#undef H_COLLISION
#undef H_ITER
#undef H_ITER_RES

#undef H_NODE_INTERNAL_VAL
#undef H_NODE_LEAF_VAL
#undef H_NODE_COLLISION_VAL

#undef H_SET_KEY_HANDLERS
#undef H_SET_LIFECYCLE_HOOKS
#undef H_GET
#undef H_GET_OR_DEFAULT
#undef H_HAS
#undef H_SET
#undef H_UNSET
#undef H_COUNT
#undef H_ITER_INIT
#undef H_NEXT_LEAF
#undef H_NEXT_KEY
#undef H_NEXT_VALUE
#undef H_KEY_FROM_LEAF
#undef H_KEY_PTR_FROM_LEAF
#undef H_VALUE_FROM_LEAF
#undef H_SET_WIDE
#undef H_GET_PTR
#undef H_VALUE_PTR_FROM_LEAF
#undef H_UNSET_WIDE
#undef H_HAS_WIDE
#undef H_WIDE_KEY_HASH
#undef H_WIDE_KEY_EQ
#undef H_LEAF_KEY_PTR
#undef H_LEAF_VAL_PTR
#undef H_REF
#undef H_UNREF
#undef H_REF_COUNT
#undef H_NODE_HASH
#undef H_SET_HASH_HANDLERS

#undef H_NODE_DESTROY
#undef H_MK_LEAF
#undef H_MK_INTERNAL_COPY
#undef H_SET_RECURSIVE
#ifndef HAMT_GC_MODE
#undef H_SET_RECURSIVE_T
#endif
#undef H_UNSET_RECURSIVE
#ifndef HAMT_GC_MODE
#undef H_UNSET_RECURSIVE_T
#endif
#undef H_GET_LEAF
#undef H_GET_NODE_COUNT
#undef H_GET_INDEX
#undef H_LEAF_REHASH
#undef H_INTERNAL_REHASH
#undef H_COLLISION_REHASH

#undef H_KEY_HASH_FN
#undef H_KEY_EQ_FN
#undef H_LEAF_HOOK_FN
#undef H_VAL_HASH_FN

#undef H_HASH_FN
#undef H_EQ_FN
#undef H_ON_CREATE
#undef H_ON_DESTROY
#ifndef HAMT_GC_MODE
#undef H_ALLOCATOR
#endif
#undef H_STRUCT_KEY_HASH
#undef H_STRUCT_VAL_HASH

#undef H_RC_ALLOC
#undef H_RC_REF
#undef H_RC_UNREF
#undef H_RC_COUNT

#undef H_ALLOC_LEAF
#undef H_ALLOC_INTERNAL
#undef H_ALLOC_COLLISION

#ifndef HAMT_GC_MODE
#undef H_TRANSIENT
#undef H_TRANSIENT_SET
#undef H_TRANSIENT_UNSET
#undef H_TRANSIENT_FN
#undef H_PERSISTENT_FN
#undef H_TRANSIENT_ITER_INIT
#endif

#ifdef HAMT_GC_MODE
#undef HAMT_GC_MODE
#undef HAMT_GC_ALLOC
#undef HAMT_GC_OBJ_INTERNAL
#undef HAMT_GC_OBJ_LEAF
#undef HAMT_GC_OBJ_COLLISION
#endif

#ifdef HAMT_ALLOC_DEFAULTED
#undef HAMT_ALLOCATOR
#undef HAMT_ALLOC_DEFAULTED
#endif

#ifdef HAMT_ITER_MAX_DEPTH_DEFAULTED
#undef HAMT_ITER_MAX_DEPTH
#undef HAMT_ITER_MAX_DEPTH_DEFAULTED
#endif
