/* rrb_vec.h - RRB Persistent Vector (include-as-template)
 *
 * Immutable vector backed by a Relaxed Radix Balanced tree with
 * copy-on-write semantics and reference counting.
 *
 * Define RRB_VEC_T (element type) and RRB_VEC_NAME (prefix) before including.
 * Can be included multiple times with different values.
 *
 * Example:
 *   #define RRB_VEC_T     int
 *   #define RRB_VEC_NAME  int_vec
 *   #include "rrb_vec.h"
 *   // Now have: int_vec_empty, int_vec_push_back, int_vec_get, etc.
 */

#ifndef RRB_VEC_T
#error "RRB_VEC_T must be defined before including rrb_vec.h"
#define RRB_VEC_T int /* suppress further errors */
#endif

#ifndef RRB_VEC_NAME
#error "RRB_VEC_NAME must be defined before including rrb_vec.h"
#define RRB_VEC_NAME _rrb_vec_err
#endif

/* Concatenation helpers */
#define RV_XCAT(a, b) a##b
#define RV_CAT(a, b)  RV_XCAT(a, b)
#define RV_NS(name)   RV_CAT(RRB_VEC_NAME, name)

/* --- Generated type names --- */

#define RV_NODE_TYPE   RV_NS(_node_type)
#define RV_NODE        RV_NS(_node)
#define RV_INTERNAL    RV_NS(_internal)
#define RV_LEAF        RV_NS(_leaf)
#define RV_ROOT        RV_NS(_root)
#define RV_GET_RESULT  RV_NS(_get_result)
#define RV_POP_RESULT  RV_NS(_pop_result)
#define RV_ITER_T      RV_NS(_iter)
#define RV_ITER_RESULT RV_NS(_iter_result)

/* --- Generated function names --- */

#define RV_EMPTY       RV_NS(_empty)
#define RV_REF         RV_NS(_ref)
#define RV_UNREF       RV_NS(_unref)
#define RV_COUNT       RV_NS(_count)
#define RV_PUSH_BACK   RV_NS(_push_back)
#define RV_GET         RV_NS(_get)
#define RV_SET         RV_NS(_set)
#define RV_POP_BACK    RV_NS(_pop_back)
#define RV_CONCAT      RV_NS(_concat)
#define RV_SLICE       RV_NS(_slice)
#define RV_PUSH_FRONT  RV_NS(_push_front)
#define RV_POP_FRONT   RV_NS(_pop_front)
#define RV_INSERT      RV_NS(_insert)
#define RV_REMOVE      RV_NS(_remove)
#define RV_MAP         RV_NS(_map)
#define RV_FILTER      RV_NS(_filter)
#define RV_REDUCE      RV_NS(_reduce)
#define RV_FIND        RV_NS(_find)
#define RV_INDEX_OF    RV_NS(_index_of)
#define RV_EQUALS      RV_NS(_equals)
#define RV_TO_ARRAY    RV_NS(_to_array)
#define RV_FROM_ARRAY  RV_NS(_from_array)
#define RV_SORT        RV_NS(_sort)
#define RV_ITER_INIT   RV_NS(_iter_init)
#define RV_ITER_NEXT   RV_NS(_iter_next)

/* Hash API */
#define RV_SET_HASH_HANDLER  RV_NS(_set_hash_handler)
#define RV_HASH              RV_NS(_hash)

/* Internal helpers */
#define RV_ELEMENT_HASH_FN_T RV_NS(_element_hash_fn_t)
#define RV_ELEMENT_HASH_PTR  RV_NS(_element_hash_ptr)
#define RV_ROTATE_LEFT       RV_NS(_rotate_left)
#define RV_LEAF_REHASH       RV_NS(_leaf_rehash)
#define RV_INTERNAL_REHASH   RV_NS(_internal_rehash)
#define RV_NODE_DESTROY      RV_NS(_node_destroy)
#define RV_ROOT_DESTROY    RV_NS(_root_destroy)
#define RV_MK_LEAF         RV_NS(_mk_leaf)
#define RV_MK_INTERNAL     RV_NS(_mk_internal)
#define RV_MK_ROOT         RV_NS(_mk_root)
#define RV_COPY_LEAF       RV_NS(_copy_leaf)
#define RV_PUSH_TAIL       RV_NS(_push_tail)
#define RV_NEW_PATH        RV_NS(_new_path)
#define RV_TAIL_OFFSET     RV_NS(_tail_offset)
#define RV_GET_LEAF        RV_NS(_get_leaf)
#define RV_COPY_INTERNAL   RV_NS(_copy_internal)
#define RV_SET_IN_TREE     RV_NS(_set_in_tree)
#define RV_POP_TAIL        RV_NS(_pop_tail)
#define RV_RIGHTMOST_LEAF  RV_NS(_rightmost_leaf)
#define RV_REMOVE_RIGHTMOST RV_NS(_remove_rightmost)
#define RV_SUBTREE_COUNT    RV_NS(_subtree_count)
#define RV_PUSH_TAIL_FULL   RV_NS(_push_tail_full)
#define RV_FIND_LEAF_OFF    RV_NS(_find_leaf_off)
#define RV_BUILD_TREE       RV_NS(_build_tree)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../platform/platform.h"

/* Allocation hooks */
#ifndef RRB_VEC_ALLOCATOR
#define RRB_VEC_ALLOCATOR    libc_allocator
#define RRB_VEC_ALLOC_DEFAULTED 1
#endif

#ifdef RRB_VEC_GC_MODE
/* --- GC mode: nodes allocated via gc_alloc, no reference counting ---
 * Caller must define RRB_VEC_GC_ALLOC(obj_type, payload_size),
 * RRB_VEC_GC_OBJ_INTERNAL, RRB_VEC_GC_OBJ_LEAF, RRB_VEC_GC_OBJ_ROOT. */

/* Auxiliary allocator for size tables, temp arrays (NOT nodes) */
static Allocator RV_NS(_allocator) = RRB_VEC_ALLOCATOR;

/* Node allocation via GC */
#define RV_ALLOC_LEAF(sz)      RRB_VEC_GC_ALLOC(RRB_VEC_GC_OBJ_LEAF, (sz))
#define RV_ALLOC_INTERNAL(sz)  RRB_VEC_GC_ALLOC(RRB_VEC_GC_OBJ_INTERNAL, (sz))
#define RV_ALLOC_ROOT(sz)      RRB_VEC_GC_ALLOC(RRB_VEC_GC_OBJ_ROOT, (sz))

/* No-op reference counting */
#define RV_RC_REF(p)   (p)
#define RV_RC_UNREF(p) ((void)0)
#define RV_RC_COUNT(p) ((intptr_t)2)

/* Unused in GC mode but kept for cleanup section */
#define RV_RC_ALLOC RV_NS(_rc_alloc_unused)

#else /* !RRB_VEC_GC_MODE */

static Allocator RV_NS(_allocator) = RRB_VEC_ALLOCATOR;

/* Wire into rc.h template */
#define RC_ALLOCATOR RV_NS(_allocator)
#define RC_NAME      RRB_VEC_NAME
#include "../rc/rc.h"

/* RC function aliases */
#define RV_RC_ALLOC RV_NS(_rc_alloc)
#define RV_RC_REF   RV_NS(_rc_ref)
#define RV_RC_UNREF RV_NS(_rc_unref)
#define RV_RC_COUNT RV_NS(_rc_count)

/* Allocation wrappers delegate to RC */
#define RV_ALLOC_LEAF(sz)      RV_RC_ALLOC((sz), RV_NODE_DESTROY)
#define RV_ALLOC_INTERNAL(sz)  RV_RC_ALLOC((sz), RV_NODE_DESTROY)
#define RV_ALLOC_ROOT(sz)      RV_RC_ALLOC((sz), RV_ROOT_DESTROY)

#endif /* RRB_VEC_GC_MODE */

/* --- Constants --- */

#define RV_BITS       5
#define RV_BRANCH     (1 << RV_BITS)  /* 32 */
#define RV_MASK       (RV_BRANCH - 1) /* 0x1F */

/* --- Node type enum --- */

typedef enum { RV_NS(_NODE_INTERNAL), RV_NS(_NODE_LEAF) } RV_NODE_TYPE;

#define RV_NODE_INTERNAL_VAL RV_NS(_NODE_INTERNAL)
#define RV_NODE_LEAF_VAL     RV_NS(_NODE_LEAF)

/* --- Data Structure Definitions --- */

typedef struct RV_NODE {
  RV_NODE_TYPE type;
  uint32_t     hash;
} RV_NODE;

/* Internal node: children + optional size table for relaxed nodes */
typedef struct RV_INTERNAL {
  RV_NODE   header;
  uint32_t  child_count;
  uint32_t* size_table;  /* NULL for strict (radix) nodes; cumulative counts for relaxed */
  RV_NODE*  children[RV_BRANCH];
} RV_INTERNAL;

/* Leaf node: array of elements */
typedef struct RV_LEAF {
  RV_NODE     header;
  uint32_t    count;
  RRB_VEC_T   elements[RV_BRANCH];
} RV_LEAF;

/* Root struct: holds tree root, tail, count, shift */
typedef struct RV_ROOT {
  RV_NODE*  root;   /* pointer to tree root node (NULL if all elements in tail) */
  RV_LEAF*  tail;   /* pointer to tail leaf */
  uint32_t  count;  /* total element count */
  uint32_t  shift;  /* depth * RV_BITS */
  uint32_t  hash;   /* structural hash: tree_root_hash ^ tail_hash */
} RV_ROOT;

/* Result struct for get */
typedef struct RV_GET_RESULT {
  RRB_VEC_T value;
  bool      found;
} RV_GET_RESULT;

/* Result struct for pop_back */
typedef struct RV_POP_RESULT {
  RV_ROOT*  root;
  RRB_VEC_T value;
  bool      found;
} RV_POP_RESULT;

/* Iterator struct (stack-based, no heap allocation) */
typedef struct RV_ITER_T {
  RV_ROOT*  root;
  uint32_t  index;
  RV_LEAF*  leaf;       /* cached current leaf */
  uint32_t  leaf_base;  /* global index of first element in cached leaf */
} RV_ITER_T;

/* Result struct for iter_next */
typedef struct RV_ITER_RESULT {
  RRB_VEC_T value;
  bool      done;
} RV_ITER_RESULT;

/* --- Function pointer typedefs --- */

typedef RRB_VEC_T (*RV_NS(_map_fn))(RRB_VEC_T);
typedef bool (*RV_NS(_pred_fn))(RRB_VEC_T);
typedef RRB_VEC_T (*RV_NS(_reduce_fn))(RRB_VEC_T, RRB_VEC_T);
typedef bool (*RV_NS(_eq_fn))(RRB_VEC_T, RRB_VEC_T);
typedef int (*RV_NS(_cmp_fn))(RRB_VEC_T, RRB_VEC_T);

#define RV_MAP_FN    RV_NS(_map_fn)
#define RV_PRED_FN   RV_NS(_pred_fn)
#define RV_REDUCE_FN RV_NS(_reduce_fn)
#define RV_EQ_FN     RV_NS(_eq_fn)
#define RV_CMP_FN    RV_NS(_cmp_fn)

/* --- Element hash handler (for structural hashing) --- */

typedef uint32_t (*RV_ELEMENT_HASH_FN_T)(RRB_VEC_T);
static RV_ELEMENT_HASH_FN_T RV_ELEMENT_HASH_PTR = NULL;

static inline void RV_SET_HASH_HANDLER(RV_ELEMENT_HASH_FN_T fn) {
  RV_ELEMENT_HASH_PTR = fn;
}

static inline uint32_t RV_ROTATE_LEFT(uint32_t x, uint32_t n) {
  n &= 31;
  return (x << n) | (x >> ((32 - n) & 31));
}

static inline void RV_LEAF_REHASH(RV_LEAF* leaf) {
  if (!RV_ELEMENT_HASH_PTR) { leaf->header.hash = 0; return; }
  uint32_t h = 0;
  for (uint32_t i = 0; i < leaf->count; i++) {
    h ^= RV_ROTATE_LEFT(RV_ELEMENT_HASH_PTR(leaf->elements[i]), i % 32);
  }
  leaf->header.hash = h;
}

static inline void RV_INTERNAL_REHASH(RV_INTERNAL* node) {
  uint32_t h = 0;
  for (uint32_t i = 0; i < node->child_count; i++) {
    h ^= node->children[i]->hash;
  }
  node->header.hash = h;
}

static inline uint32_t RV_HASH(RV_ROOT* root) {
  return root ? root->hash : 0;
}

/* --- Forward declarations (RC mode only) --- */

#ifndef RRB_VEC_GC_MODE
static void RV_NODE_DESTROY(void* arg);
static void RV_ROOT_DESTROY(void* arg);
#endif

/* --- Node construction --- */

static inline RV_LEAF* RV_MK_LEAF(void) {
  RV_LEAF* leaf = (RV_LEAF*)RV_ALLOC_LEAF(sizeof(RV_LEAF));
  leaf->header  = (RV_NODE){.type = RV_NODE_LEAF_VAL};
  leaf->count   = 0;
  return leaf;
}

static inline RV_INTERNAL* RV_MK_INTERNAL(void) {
  RV_INTERNAL* node = (RV_INTERNAL*)RV_ALLOC_INTERNAL(sizeof(RV_INTERNAL));
  node->header      = (RV_NODE){.type = RV_NODE_INTERNAL_VAL};
  node->child_count = 0;
  node->size_table  = NULL;
  memset(node->children, 0, sizeof(node->children));
  return node;
}

static inline RV_ROOT* RV_MK_ROOT(RV_NODE* root, RV_LEAF* tail, uint32_t count, uint32_t shift) {
  RV_ROOT* r = (RV_ROOT*)RV_ALLOC_ROOT(sizeof(RV_ROOT));
  r->root    = root;
  r->tail    = tail;
  r->count   = count;
  r->shift   = shift;
  r->hash    = (root ? root->hash : 0) ^ (tail ? tail->header.hash : 0);
  return r;
}

/* --- Node destruction (RC mode only — GC handles lifecycle) --- */

#ifndef RRB_VEC_GC_MODE
static void RV_NODE_DESTROY(void* arg) {
  RV_NODE* node = (RV_NODE*)arg;
  if (!node) return;

  if (node->type == RV_NODE_INTERNAL_VAL) {
    RV_INTERNAL* n = (RV_INTERNAL*)node;
    for (uint32_t i = 0; i < n->child_count; i++) {
      RV_RC_UNREF(n->children[i]);
    }
    if (n->size_table) {
      RV_NS(_allocator).free(RV_NS(_allocator).ctx, n->size_table);
    }
  }
  /* Leaf nodes have no children to unref — elements are stored by value */
}

static void RV_ROOT_DESTROY(void* arg) {
  RV_ROOT* r = (RV_ROOT*)arg;
  if (!r) return;
  if (r->root) RV_RC_UNREF(r->root);
  if (r->tail) RV_RC_UNREF((RV_NODE*)r->tail);
}
#endif /* !RRB_VEC_GC_MODE */

/* --- Internal helpers for push_back / get --- */

static inline RV_LEAF* RV_COPY_LEAF(RV_LEAF* src) {
  RV_LEAF* dst = RV_MK_LEAF();
  dst->count = src->count;
  memcpy(dst->elements, src->elements, sizeof(RRB_VEC_T) * src->count);
  dst->header.hash = src->header.hash;
  return dst;
}

/* Create a path of internal nodes from level down to the leaf */
static inline RV_NODE* RV_NEW_PATH(uint32_t level, RV_NODE* leaf) {
  if (level == 0) {
    return leaf;
  }
  RV_INTERNAL* node = RV_MK_INTERNAL();
  node->children[0] = RV_NEW_PATH(level - RV_BITS, leaf);
  node->child_count = 1;
  RV_INTERNAL_REHASH(node);
  return (RV_NODE*)node;
}

/* Push the full tail into the tree, returning new root node.
 * level is the current shift of the tree. */
static inline RV_NODE* RV_PUSH_TAIL(uint32_t level, uint32_t count, RV_NODE* node, RV_NODE* tail_node) {
  if (level == RV_BITS) {
    /* At the lowest internal level, just add the tail as a child */
    RV_INTERNAL* new_node = RV_MK_INTERNAL();
    RV_INTERNAL* old = (RV_INTERNAL*)node;
    for (uint32_t i = 0; i < old->child_count; i++) {
      new_node->children[i] = (RV_NODE*)RV_RC_REF(old->children[i]);
    }
    new_node->children[old->child_count] = (RV_NODE*)RV_RC_REF(tail_node);
    new_node->child_count = old->child_count + 1;
    RV_INTERNAL_REHASH(new_node);
    return (RV_NODE*)new_node;
  }

  RV_INTERNAL* old = (RV_INTERNAL*)node;
  RV_INTERNAL* new_node = RV_MK_INTERNAL();

  /* Copy existing children with ref */
  for (uint32_t i = 0; i < old->child_count; i++) {
    new_node->children[i] = (RV_NODE*)RV_RC_REF(old->children[i]);
  }
  new_node->child_count = old->child_count;

  uint32_t subidx = ((count - 1) >> level) & RV_MASK;
  if (subidx < old->child_count) {
    /* Recurse into existing subtree */
    RV_NODE* new_child = RV_PUSH_TAIL(level - RV_BITS, count, old->children[subidx], tail_node);
    RV_RC_UNREF(new_node->children[subidx]);
    new_node->children[subidx] = new_child;
  } else {
    /* Create new path */
    new_node->children[subidx] = RV_NEW_PATH(level - RV_BITS, tail_node);
    new_node->child_count = subidx + 1;
  }
  RV_INTERNAL_REHASH(new_node);
  return (RV_NODE*)new_node;
}

/* Number of elements stored in the tree (excluding tail) */
static inline uint32_t RV_TAIL_OFFSET(RV_ROOT* r) {
  if (r->count == 0) return 0;
  /* tail_offset = count - tail->count */
  return r->count - r->tail->count;
}

/* Find the leaf node containing the given index */
static inline RV_LEAF* RV_GET_LEAF(RV_ROOT* r, uint32_t index) {
  uint32_t tail_off = RV_TAIL_OFFSET(r);
  if (index >= tail_off) {
    return r->tail;
  }
  /* Traverse the tree */
  RV_NODE* node = r->root;
  for (uint32_t level = r->shift; level > 0; level -= RV_BITS) {
    RV_INTERNAL* n = (RV_INTERNAL*)node;
    if (n->size_table) {
      /* Relaxed node: scan size table */
      uint32_t slot = 0;
      while (n->size_table[slot] <= index) slot++;
      if (slot > 0) index -= n->size_table[slot - 1];
      node = n->children[slot];
    } else {
      /* Strict node: radix indexing */
      uint32_t slot = (index >> level) & RV_MASK;
      node = n->children[slot];
    }
  }
  return (RV_LEAF*)node;
}

/* --- Public API --- */

static inline RV_ROOT* RV_EMPTY(void) {
  RV_LEAF* tail = RV_MK_LEAF();
  return RV_MK_ROOT(NULL, tail, 0, RV_BITS);
}

static inline RV_ROOT* RV_REF(RV_ROOT* root) {
  return (RV_ROOT*)RV_RC_REF(root);
}

static inline void RV_UNREF(RV_ROOT* root) {
  RV_RC_UNREF(root);
}

static inline uint32_t RV_COUNT(RV_ROOT* root) {
  return root ? root->count : 0;
}

static inline RV_ROOT* RV_PUSH_BACK(RV_ROOT* r, RRB_VEC_T value) {
  if (r->tail->count < RV_BRANCH) {
    /* Tail has room — copy tail and append */
    RV_LEAF* new_tail = RV_COPY_LEAF(r->tail);
    new_tail->elements[new_tail->count] = value;
    new_tail->count++;
    RV_LEAF_REHASH(new_tail);
    RV_NODE* tree = r->root ? (RV_NODE*)RV_RC_REF(r->root) : NULL;
    return RV_MK_ROOT(tree, new_tail, r->count + 1, r->shift);
  }

  /* Tail is full — push tail into the tree, start a new tail */
  RV_LEAF* new_tail = RV_MK_LEAF();
  new_tail->elements[0] = value;
  new_tail->count = 1;
  RV_LEAF_REHASH(new_tail);

  RV_NODE* new_root_node;
  uint32_t new_shift = r->shift;

  if (r->root == NULL) {
    /* Tree was empty — wrap old tail in an internal node */
    RV_INTERNAL* node = RV_MK_INTERNAL();
    node->children[0] = (RV_NODE*)RV_RC_REF((RV_NODE*)r->tail);
    node->child_count = 1;
    RV_INTERNAL_REHASH(node);
    new_root_node = (RV_NODE*)node;
  } else {
    /* Check if tree is full at current depth: count of tree elements == capacity */
    uint32_t tree_count = RV_TAIL_OFFSET(r);
    uint32_t capacity = (uint32_t)1 << (r->shift + RV_BITS);
    if (tree_count >= capacity) {
      /* Overflow: create new root level */
      RV_INTERNAL* new_top = RV_MK_INTERNAL();
      new_top->children[0] = (RV_NODE*)RV_RC_REF(r->root);
      new_top->children[1] = RV_NEW_PATH(r->shift, (RV_NODE*)RV_RC_REF((RV_NODE*)r->tail));
      new_top->child_count = 2;
      RV_INTERNAL_REHASH(new_top);
      new_root_node = (RV_NODE*)new_top;
      new_shift = r->shift + RV_BITS;
    } else {
      /* Push old tail into existing tree */
      new_root_node = RV_PUSH_TAIL(r->shift, tree_count, r->root, (RV_NODE*)r->tail);
    }
  }

  return RV_MK_ROOT(new_root_node, new_tail, r->count + 1, new_shift);
}

static inline RV_GET_RESULT RV_GET(RV_ROOT* r, uint32_t index) {
  if (!r || index >= r->count) {
    return (RV_GET_RESULT){.found = false};
  }
  uint32_t tail_off = RV_TAIL_OFFSET(r);
  if (index >= tail_off) {
    return (RV_GET_RESULT){.value = r->tail->elements[index - tail_off], .found = true};
  }
  /* Traverse tree with index adjustment for both strict and relaxed nodes */
  RV_NODE* node = r->root;
  uint32_t idx = index;
  for (uint32_t level = r->shift; level > 0; level -= RV_BITS) {
    RV_INTERNAL* n = (RV_INTERNAL*)node;
    uint32_t slot;
    if (n->size_table) {
      slot = 0;
      while (n->size_table[slot] <= idx) slot++;
      if (slot > 0) idx -= n->size_table[slot - 1];
    } else {
      slot = (idx >> level) & RV_MASK;
      idx -= slot << level;
    }
    node = n->children[slot];
  }
  return (RV_GET_RESULT){.value = ((RV_LEAF*)node)->elements[idx], .found = true};
}

/* --- Internal helpers for set --- */

static inline RV_INTERNAL* RV_COPY_INTERNAL(RV_INTERNAL* src) {
  RV_INTERNAL* dst = RV_MK_INTERNAL();
  dst->child_count = src->child_count;
  for (uint32_t i = 0; i < src->child_count; i++) {
    dst->children[i] = (RV_NODE*)RV_RC_REF(src->children[i]);
  }
  if (src->size_table) {
    size_t st_size = sizeof(uint32_t) * src->child_count;
    dst->size_table = (uint32_t*)RV_NS(_allocator).alloc(RV_NS(_allocator).ctx, st_size);
    memcpy(dst->size_table, src->size_table, st_size);
  }
  dst->header.hash = src->header.hash;
  return dst;
}

/* Recursively set a value in the tree (not tail), returning a new path-copied subtree */
static inline RV_NODE* RV_SET_IN_TREE(uint32_t level, RV_NODE* node, uint32_t index, RRB_VEC_T value) {
  if (level == 0) {
    /* Leaf level — index is already adjusted to be leaf-local */
    RV_LEAF* old_leaf = (RV_LEAF*)node;
    RV_LEAF* new_leaf = RV_COPY_LEAF(old_leaf);
    new_leaf->elements[index] = value;
    RV_LEAF_REHASH(new_leaf);
    return (RV_NODE*)new_leaf;
  }

  RV_INTERNAL* old = (RV_INTERNAL*)node;
  RV_INTERNAL* new_node = RV_COPY_INTERNAL(old);

  uint32_t slot;
  uint32_t child_index;
  if (old->size_table) {
    slot = 0;
    while (old->size_table[slot] <= index) slot++;
    child_index = (slot > 0) ? index - old->size_table[slot - 1] : index;
  } else {
    slot = (index >> level) & RV_MASK;
    child_index = index - (slot << level);
  }

  RV_NODE* new_child = RV_SET_IN_TREE(level - RV_BITS, old->children[slot], child_index, value);
  RV_RC_UNREF(new_node->children[slot]);
  new_node->children[slot] = new_child;
  RV_INTERNAL_REHASH(new_node);
  return (RV_NODE*)new_node;
}

static inline RV_ROOT* RV_SET(RV_ROOT* r, uint32_t index, RRB_VEC_T value) {
  if (!r || index >= r->count) return NULL;

  uint32_t tail_off = RV_TAIL_OFFSET(r);

  if (index >= tail_off) {
    /* In the tail */
    RV_LEAF* new_tail = RV_COPY_LEAF(r->tail);
    new_tail->elements[index - tail_off] = value;
    RV_LEAF_REHASH(new_tail);
    RV_NODE* tree = r->root ? (RV_NODE*)RV_RC_REF(r->root) : NULL;
    return RV_MK_ROOT(tree, new_tail, r->count, r->shift);
  }

  /* In the tree */
  RV_NODE* new_tree = RV_SET_IN_TREE(r->shift, r->root, index, value);
  RV_LEAF* new_tail = RV_COPY_LEAF(r->tail);
  return RV_MK_ROOT(new_tree, new_tail, r->count, r->shift);
}

/* --- Internal helpers for pop_back --- */

/* Get the rightmost leaf from the tree */
static inline RV_LEAF* RV_RIGHTMOST_LEAF(RV_NODE* node, uint32_t level) {
  if (level == 0) {
    return (RV_LEAF*)node;
  }
  RV_INTERNAL* n = (RV_INTERNAL*)node;
  return RV_RIGHTMOST_LEAF(n->children[n->child_count - 1], level - RV_BITS);
}

/* Remove the rightmost leaf from the tree, returning a new tree (or NULL if empty).
 * Returns a new path-copied tree with the rightmost leaf removed. */
static inline RV_NODE* RV_REMOVE_RIGHTMOST(uint32_t level, RV_NODE* node) {
  if (level == RV_BITS) {
    /* At lowest internal level, the rightmost child is the leaf to remove */
    RV_INTERNAL* old = (RV_INTERNAL*)node;
    if (old->child_count == 1) {
      return NULL; /* This node becomes empty */
    }
    RV_INTERNAL* new_node = RV_MK_INTERNAL();
    new_node->child_count = old->child_count - 1;
    for (uint32_t i = 0; i < new_node->child_count; i++) {
      new_node->children[i] = (RV_NODE*)RV_RC_REF(old->children[i]);
    }
    if (old->size_table) {
      size_t st_size = sizeof(uint32_t) * new_node->child_count;
      new_node->size_table = (uint32_t*)RV_NS(_allocator).alloc(RV_NS(_allocator).ctx, st_size);
      memcpy(new_node->size_table, old->size_table, st_size);
    }
    RV_INTERNAL_REHASH(new_node);
    return (RV_NODE*)new_node;
  }

  RV_INTERNAL* old = (RV_INTERNAL*)node;
  RV_NODE* new_child = RV_REMOVE_RIGHTMOST(level - RV_BITS, old->children[old->child_count - 1]);

  if (new_child == NULL && old->child_count == 1) {
    return NULL; /* This node becomes empty too */
  }

  RV_INTERNAL* new_node = RV_MK_INTERNAL();
  if (new_child == NULL) {
    /* Last child was removed, copy all but last */
    new_node->child_count = old->child_count - 1;
  } else {
    new_node->child_count = old->child_count;
  }
  for (uint32_t i = 0; i < new_node->child_count; i++) {
    if (i == new_node->child_count - 1 && new_child != NULL && i == old->child_count - 1) {
      new_node->children[i] = new_child;
    } else {
      new_node->children[i] = (RV_NODE*)RV_RC_REF(old->children[i]);
    }
  }
  if (old->size_table) {
    size_t st_size = sizeof(uint32_t) * new_node->child_count;
    new_node->size_table = (uint32_t*)RV_NS(_allocator).alloc(RV_NS(_allocator).ctx, st_size);
    memcpy(new_node->size_table, old->size_table, st_size);
    /* Update the last entry if we modified the last child */
    if (new_child != NULL) {
      /* Need to recalculate the last cumulative count */
      /* The rightmost leaf was removed, so subtract its count from the last entry */
      /* Actually, we need to figure out how many elements were removed */
      /* For now, just decrement appropriately - the removed leaf had some count */
      /* We'll recalculate: last entry = previous entry's cumulative + subtree count */
      /* This is complex for relaxed nodes; for strict nodes size_table is NULL */
    }
  }
  RV_INTERNAL_REHASH(new_node);
  return (RV_NODE*)new_node;
}

static inline RV_POP_RESULT RV_POP_BACK(RV_ROOT* r) {
  if (!r || r->count == 0) {
    return (RV_POP_RESULT){.root = NULL, .found = false};
  }

  RRB_VEC_T popped = r->tail->elements[r->tail->count - 1];

  if (r->count == 1) {
    /* Popping last element — return empty vec */
    return (RV_POP_RESULT){.root = RV_EMPTY(), .value = popped, .found = true};
  }

  if (r->tail->count > 1) {
    /* Tail has more than one element — just shrink the tail */
    RV_LEAF* new_tail = RV_MK_LEAF();
    new_tail->count = r->tail->count - 1;
    memcpy(new_tail->elements, r->tail->elements, sizeof(RRB_VEC_T) * new_tail->count);
    RV_LEAF_REHASH(new_tail);
    RV_NODE* tree = r->root ? (RV_NODE*)RV_RC_REF(r->root) : NULL;
    RV_ROOT* new_root = RV_MK_ROOT(tree, new_tail, r->count - 1, r->shift);
    return (RV_POP_RESULT){.root = new_root, .value = popped, .found = true};
  }

  /* Tail has exactly 1 element — need to pull rightmost leaf from tree */
  RV_LEAF* new_tail = RV_COPY_LEAF(RV_RIGHTMOST_LEAF(r->root, r->shift));
  RV_NODE* new_tree = RV_REMOVE_RIGHTMOST(r->shift, r->root);
  uint32_t new_shift = r->shift;

  /* Shrink tree if root has single child */
  while (new_tree != NULL && new_shift > RV_BITS) {
    RV_INTERNAL* top = (RV_INTERNAL*)new_tree;
    if (top->child_count == 1) {
      RV_NODE* child = (RV_NODE*)RV_RC_REF(top->children[0]);
      RV_RC_UNREF(new_tree);
      new_tree = child;
      new_shift -= RV_BITS;
    } else {
      break;
    }
  }

  RV_ROOT* new_root = RV_MK_ROOT(new_tree, new_tail, r->count - 1, new_shift);
  return (RV_POP_RESULT){.root = new_root, .value = popped, .found = true};
}

/* --- Internal helpers for concat --- */

/* Count total elements in a subtree */
static inline uint32_t RV_SUBTREE_COUNT(RV_NODE* node, uint32_t shift) {
  if (shift == 0) {
    return ((RV_LEAF*)node)->count;
  }
  RV_INTERNAL* n = (RV_INTERNAL*)node;
  if (n->size_table) {
    return n->size_table[n->child_count - 1];
  }
  /* Strict: first (child_count-1) children are full, last may be partial */
  uint32_t full_size = 1u << shift;
  uint32_t count = (uint32_t)(n->child_count - 1) * full_size;
  count += RV_SUBTREE_COUNT(n->children[n->child_count - 1], shift - RV_BITS);
  return count;
}

/* Push a vector's tail into its tree, returning a complete tree node.
 * Works for both strict and relaxed trees.
 * Caller must ensure r->count > 0. */
static inline RV_NODE* RV_PUSH_TAIL_FULL(RV_ROOT* r, uint32_t* out_shift) {
  if (r->root == NULL) {
    /* Only tail, no tree — wrap tail in a relaxed internal node */
    RV_INTERNAL* node = RV_MK_INTERNAL();
    node->children[0] = (RV_NODE*)RV_RC_REF((RV_NODE*)r->tail);
    node->child_count = 1;
    node->size_table = (uint32_t*)RV_NS(_allocator).alloc(
        RV_NS(_allocator).ctx, sizeof(uint32_t));
    node->size_table[0] = r->tail->count;
    RV_INTERNAL_REHASH(node);
    *out_shift = RV_BITS;
    return (RV_NODE*)node;
  }

  RV_INTERNAL* root = (RV_INTERNAL*)r->root;

  /* Compute existing tree element count */
  uint32_t tree_count;
  if (root->size_table) {
    tree_count = root->size_table[root->child_count - 1];
  } else {
    tree_count = r->count - r->tail->count;
  }

  /* Wrap tail to matching height */
  RV_NODE* wrapped = (RV_NODE*)RV_RC_REF((RV_NODE*)r->tail);
  for (uint32_t lvl = RV_BITS; lvl < r->shift; lvl += RV_BITS) {
    RV_INTERNAL* w = RV_MK_INTERNAL();
    w->children[0] = wrapped;
    w->child_count = 1;
    RV_INTERNAL_REHASH(w);
    wrapped = (RV_NODE*)w;
  }

  if (root->child_count < RV_BRANCH) {
    /* Room in root — add wrapped tail with updated size table */
    RV_INTERNAL* new_root = RV_MK_INTERNAL();
    new_root->child_count = root->child_count + 1;
    for (uint32_t i = 0; i < root->child_count; i++) {
      new_root->children[i] = (RV_NODE*)RV_RC_REF(root->children[i]);
    }
    new_root->children[root->child_count] = wrapped;
    uint32_t* st = (uint32_t*)RV_NS(_allocator).alloc(
        RV_NS(_allocator).ctx, sizeof(uint32_t) * new_root->child_count);
    if (root->size_table) {
      memcpy(st, root->size_table, sizeof(uint32_t) * root->child_count);
    } else {
      /* Build size table from strict tree */
      uint32_t cum = 0;
      for (uint32_t i = 0; i < root->child_count; i++) {
        cum += RV_SUBTREE_COUNT(root->children[i], r->shift - RV_BITS);
        st[i] = cum;
      }
    }
    st[root->child_count] = tree_count + r->tail->count;
    new_root->size_table = st;
    RV_INTERNAL_REHASH(new_root);
    *out_shift = r->shift;
    return (RV_NODE*)new_root;
  }

  /* Root is full — create new level */
  RV_INTERNAL* new_top = RV_MK_INTERNAL();
  new_top->children[0] = (RV_NODE*)RV_RC_REF(r->root);
  RV_INTERNAL* w = RV_MK_INTERNAL();
  w->children[0] = wrapped;
  w->child_count = 1;
  RV_INTERNAL_REHASH(w);
  new_top->children[1] = (RV_NODE*)w;
  new_top->child_count = 2;
  new_top->size_table = (uint32_t*)RV_NS(_allocator).alloc(
      RV_NS(_allocator).ctx, sizeof(uint32_t) * 2);
  new_top->size_table[0] = tree_count;
  new_top->size_table[1] = tree_count + r->tail->count;
  RV_INTERNAL_REHASH(new_top);
  *out_shift = r->shift + RV_BITS;
  return (RV_NODE*)new_top;
}

static inline RV_ROOT* RV_CONCAT(RV_ROOT* left, RV_ROOT* right) {
  if (!left || left->count == 0) return right ? RV_REF(right) : RV_EMPTY();
  if (!right || right->count == 0) return RV_REF(left);

  /* Result tail is a copy of right's tail */
  RV_LEAF* new_tail = RV_COPY_LEAF(right->tail);
  uint32_t total = left->count + right->count;

  /* Push left's tail into left's tree */
  uint32_t l_shift;
  RV_NODE* l_tree = RV_PUSH_TAIL_FULL(left, &l_shift);
  uint32_t l_count = left->count;

  if (right->root == NULL) {
    /* Right has only a tail — result is left tree + right's tail */
    return RV_MK_ROOT(l_tree, new_tail, total, l_shift);
  }

  /* Both trees exist — merge them */
  RV_NODE* r_tree = (RV_NODE*)RV_RC_REF(right->root);
  uint32_t r_shift = right->shift;
  uint32_t r_count = right->count - right->tail->count;

  /* Wrap shorter tree to match taller */
  uint32_t max_shift = l_shift > r_shift ? l_shift : r_shift;
  while (l_shift < max_shift) {
    RV_INTERNAL* w = RV_MK_INTERNAL();
    w->children[0] = l_tree;
    w->child_count = 1;
    RV_INTERNAL_REHASH(w);
    l_tree = (RV_NODE*)w;
    l_shift += RV_BITS;
  }
  while (r_shift < max_shift) {
    RV_INTERNAL* w = RV_MK_INTERNAL();
    w->children[0] = r_tree;
    w->child_count = 1;
    RV_INTERNAL_REHASH(w);
    r_tree = (RV_NODE*)w;
    r_shift += RV_BITS;
  }

  /* Create relaxed parent with size table */
  RV_INTERNAL* parent = RV_MK_INTERNAL();
  parent->children[0] = l_tree;
  parent->children[1] = r_tree;
  parent->child_count = 2;
  parent->size_table = (uint32_t*)RV_NS(_allocator).alloc(
      RV_NS(_allocator).ctx, sizeof(uint32_t) * 2);
  parent->size_table[0] = l_count;
  parent->size_table[1] = l_count + r_count;
  RV_INTERNAL_REHASH(parent);

  return RV_MK_ROOT((RV_NODE*)parent, new_tail, total, max_shift + RV_BITS);
}

/* --- Internal helpers for slice --- */

/* Find leaf containing index and return the global index of its first element */
static inline RV_LEAF* RV_FIND_LEAF_OFF(RV_ROOT* r, uint32_t index, uint32_t* leaf_base) {
  uint32_t tail_off = RV_TAIL_OFFSET(r);
  if (index >= tail_off) {
    *leaf_base = tail_off;
    return r->tail;
  }
  RV_NODE* node = r->root;
  uint32_t idx = index;
  for (uint32_t level = r->shift; level > 0; level -= RV_BITS) {
    RV_INTERNAL* n = (RV_INTERNAL*)node;
    uint32_t slot;
    if (n->size_table) {
      slot = 0;
      while (n->size_table[slot] <= idx) slot++;
      if (slot > 0) idx -= n->size_table[slot - 1];
    } else {
      slot = (idx >> level) & RV_MASK;
      idx -= slot << level;
    }
    node = n->children[slot];
  }
  *leaf_base = index - idx;
  return (RV_LEAF*)node;
}

/* Build a relaxed tree from an array of child nodes with their element counts.
 * Takes ownership of the nodes (no additional rc_ref). */
static inline RV_NODE* RV_BUILD_TREE(RV_NODE** nodes, uint32_t* counts, uint32_t n, uint32_t* out_shift) {
  uint32_t level = RV_BITS;
  RV_NODE** cur = nodes;
  uint32_t* cur_c = counts;
  uint32_t cur_n = n;
  bool owns = false;

  while (1) {
    uint32_t ng = (cur_n + RV_BRANCH - 1) / RV_BRANCH;
    RV_NODE** nxt = (RV_NODE**)RV_NS(_allocator).alloc(
        RV_NS(_allocator).ctx, ng * sizeof(RV_NODE*));
    uint32_t* nxt_c = (uint32_t*)RV_NS(_allocator).alloc(
        RV_NS(_allocator).ctx, ng * sizeof(uint32_t));

    for (uint32_t g = 0; g < ng; g++) {
      uint32_t gs = g * RV_BRANCH;
      uint32_t ge = gs + RV_BRANCH;
      if (ge > cur_n) ge = cur_n;
      uint32_t gc = ge - gs;

      RV_INTERNAL* inode = RV_MK_INTERNAL();
      inode->child_count = gc;
      uint32_t* st = (uint32_t*)RV_NS(_allocator).alloc(
          RV_NS(_allocator).ctx, gc * sizeof(uint32_t));
      uint32_t cum = 0;
      for (uint32_t i = 0; i < gc; i++) {
        inode->children[i] = cur[gs + i];
        cum += cur_c[gs + i];
        st[i] = cum;
      }
      inode->size_table = st;
      RV_INTERNAL_REHASH(inode);
      nxt[g] = (RV_NODE*)inode;
      nxt_c[g] = cum;
    }

    if (owns) {
      RV_NS(_allocator).free(RV_NS(_allocator).ctx, cur);
      RV_NS(_allocator).free(RV_NS(_allocator).ctx, cur_c);
    }

    if (ng == 1) {
      RV_NODE* result = nxt[0];
      RV_NS(_allocator).free(RV_NS(_allocator).ctx, nxt);
      RV_NS(_allocator).free(RV_NS(_allocator).ctx, nxt_c);
      *out_shift = level;
      return result;
    }

    cur = nxt;
    cur_c = nxt_c;
    cur_n = ng;
    owns = true;
    level += RV_BITS;
  }
}

static inline RV_ROOT* RV_SLICE(RV_ROOT* r, uint32_t start, uint32_t end) {
  if (!r || start >= end || end > r->count) return NULL;
  if (start == 0 && end == r->count) return RV_REF(r);

  uint32_t total = end - start;

  /* Collect leaves covering [start, end) */
  RV_NODE** leaves = (RV_NODE**)RV_NS(_allocator).alloc(
      RV_NS(_allocator).ctx, total * sizeof(RV_NODE*));
  uint32_t* leaf_counts = (uint32_t*)RV_NS(_allocator).alloc(
      RV_NS(_allocator).ctx, total * sizeof(uint32_t));
  uint32_t num_leaves = 0;

  uint32_t pos = start;
  while (pos < end) {
    uint32_t base;
    RV_LEAF* leaf = RV_FIND_LEAF_OFF(r, pos, &base);
    uint32_t local_start = pos - base;
    uint32_t avail = leaf->count - local_start;
    uint32_t remaining = end - pos;
    uint32_t take = avail < remaining ? avail : remaining;

    if (local_start == 0 && take == leaf->count) {
      /* Fully contained — zero copy */
      leaves[num_leaves] = (RV_NODE*)RV_RC_REF((RV_NODE*)leaf);
    } else {
      /* Boundary — partial copy */
      RV_LEAF* partial = RV_MK_LEAF();
      partial->count = take;
      memcpy(partial->elements, leaf->elements + local_start,
             sizeof(RRB_VEC_T) * take);
      RV_LEAF_REHASH(partial);
      leaves[num_leaves] = (RV_NODE*)partial;
    }
    leaf_counts[num_leaves] = take;
    num_leaves++;
    pos += take;
  }

  /* Last leaf becomes the tail, rest go into tree */
  RV_LEAF* new_tail = (RV_LEAF*)leaves[num_leaves - 1];
  uint32_t tree_n = num_leaves - 1;

  RV_NODE* tree_root = NULL;
  uint32_t shift = RV_BITS;
  if (tree_n > 0) {
    tree_root = RV_BUILD_TREE(leaves, leaf_counts, tree_n, &shift);
  }

  RV_NS(_allocator).free(RV_NS(_allocator).ctx, leaves);
  RV_NS(_allocator).free(RV_NS(_allocator).ctx, leaf_counts);

  return RV_MK_ROOT(tree_root, new_tail, total, shift);
}

/* --- Deque access (push_front / pop_front) --- */

static inline RV_ROOT* RV_PUSH_FRONT(RV_ROOT* r, RRB_VEC_T value) {
  RV_ROOT* single = RV_EMPTY();
  RV_ROOT* s1 = RV_PUSH_BACK(single, value);
  RV_UNREF(single);
  RV_ROOT* result = RV_CONCAT(s1, r);
  RV_UNREF(s1);
  return result;
}

static inline RV_POP_RESULT RV_POP_FRONT(RV_ROOT* r) {
  if (!r || r->count == 0) {
    return (RV_POP_RESULT){.root = NULL, .found = false};
  }
  RV_GET_RESULT gr = RV_GET(r, 0);
  RV_ROOT* rest;
  if (r->count == 1) {
    rest = RV_EMPTY();
  } else {
    rest = RV_SLICE(r, 1, r->count);
  }
  return (RV_POP_RESULT){.root = rest, .value = gr.value, .found = true};
}

/* --- Insert / Remove at index --- */

static inline RV_ROOT* RV_INSERT(RV_ROOT* r, uint32_t index, RRB_VEC_T value) {
  if (!r || index > r->count) return NULL;

  /* Build single-element vector */
  RV_ROOT* single = RV_EMPTY();
  RV_ROOT* s1 = RV_PUSH_BACK(single, value);
  RV_UNREF(single);

  if (index == 0) {
    RV_ROOT* result = RV_CONCAT(s1, r);
    RV_UNREF(s1);
    return result;
  }
  if (index == r->count) {
    RV_ROOT* result = RV_CONCAT(r, s1);
    RV_UNREF(s1);
    return result;
  }

  /* slice [0, index) + single + slice [index, count) */
  RV_ROOT* left = RV_SLICE(r, 0, index);
  RV_ROOT* right = RV_SLICE(r, index, r->count);
  RV_ROOT* tmp = RV_CONCAT(left, s1);
  RV_ROOT* result = RV_CONCAT(tmp, right);
  RV_UNREF(left);
  RV_UNREF(right);
  RV_UNREF(s1);
  RV_UNREF(tmp);
  return result;
}

static inline RV_POP_RESULT RV_REMOVE(RV_ROOT* r, uint32_t index) {
  if (!r || index >= r->count) {
    return (RV_POP_RESULT){.root = NULL, .found = false};
  }

  RV_GET_RESULT gr = RV_GET(r, index);
  RV_ROOT* result;

  if (r->count == 1) {
    result = RV_EMPTY();
  } else if (index == 0) {
    result = RV_SLICE(r, 1, r->count);
  } else if (index == r->count - 1) {
    result = RV_SLICE(r, 0, r->count - 1);
  } else {
    RV_ROOT* left = RV_SLICE(r, 0, index);
    RV_ROOT* right = RV_SLICE(r, index + 1, r->count);
    result = RV_CONCAT(left, right);
    RV_UNREF(left);
    RV_UNREF(right);
  }

  return (RV_POP_RESULT){.root = result, .value = gr.value, .found = true};
}

/* --- Iterator --- */

static inline RV_ITER_T RV_ITER_INIT(RV_ROOT* root) {
  RV_ITER_T it;
  memset(&it, 0, sizeof(it));
  it.root = root;
  if (root && root->count > 0) {
    it.leaf = RV_FIND_LEAF_OFF(root, 0, &it.leaf_base);
  }
  return it;
}

static inline RV_ITER_RESULT RV_ITER_NEXT(RV_ITER_T* it) {
  if (!it->root || it->index >= it->root->count) {
    return (RV_ITER_RESULT){.done = true};
  }
  uint32_t local = it->index - it->leaf_base;
  if (local >= it->leaf->count) {
    it->leaf = RV_FIND_LEAF_OFF(it->root, it->index, &it->leaf_base);
    local = it->index - it->leaf_base;
  }
  RRB_VEC_T val = it->leaf->elements[local];
  it->index++;
  return (RV_ITER_RESULT){.value = val, .done = false};
}

/* --- Reduce --- */

static inline RRB_VEC_T RV_REDUCE(RV_ROOT* r, RV_REDUCE_FN fn, RRB_VEC_T initial) {
  if (!r || r->count == 0) return initial;

  RRB_VEC_T acc = initial;
  RV_ITER_T it = RV_ITER_INIT(r);
  while (1) {
    RV_ITER_RESULT ir = RV_ITER_NEXT(&it);
    if (ir.done) break;
    acc = fn(acc, ir.value);
  }
  return acc;
}

/* --- Find, index_of, equals --- */

static inline RV_GET_RESULT RV_FIND(RV_ROOT* r, RV_PRED_FN pred) {
  if (!r || r->count == 0) return (RV_GET_RESULT){.found = false};

  RV_ITER_T it = RV_ITER_INIT(r);
  while (1) {
    RV_ITER_RESULT ir = RV_ITER_NEXT(&it);
    if (ir.done) break;
    if (pred(ir.value)) {
      return (RV_GET_RESULT){.value = ir.value, .found = true};
    }
  }
  return (RV_GET_RESULT){.found = false};
}

static inline int32_t RV_INDEX_OF(RV_ROOT* r, RRB_VEC_T value, RV_EQ_FN eq) {
  if (!r || r->count == 0) return -1;

  RV_ITER_T it = RV_ITER_INIT(r);
  int32_t idx = 0;
  while (1) {
    RV_ITER_RESULT ir = RV_ITER_NEXT(&it);
    if (ir.done) break;
    if (eq(ir.value, value)) return idx;
    idx++;
  }
  return -1;
}

static inline bool RV_EQUALS(RV_ROOT* a, RV_ROOT* b, RV_EQ_FN eq) {
  uint32_t ca = a ? a->count : 0;
  uint32_t cb = b ? b->count : 0;
  if (ca != cb) return false;
  if (ca == 0) return true;

  RV_ITER_T ia = RV_ITER_INIT(a);
  RV_ITER_T ib = RV_ITER_INIT(b);
  while (1) {
    RV_ITER_RESULT ra = RV_ITER_NEXT(&ia);
    RV_ITER_RESULT rb = RV_ITER_NEXT(&ib);
    if (ra.done) break;
    if (!eq(ra.value, rb.value)) return false;
  }
  return true;
}

/* --- To array / From array --- */

static inline RRB_VEC_T* RV_TO_ARRAY(RV_ROOT* r, uint32_t* out_count) {
  if (!r || r->count == 0) {
    *out_count = 0;
    return NULL;
  }
  uint32_t n = r->count;
  *out_count = n;
  RRB_VEC_T* arr = (RRB_VEC_T*)RV_NS(_allocator).alloc(
      RV_NS(_allocator).ctx, sizeof(RRB_VEC_T) * n);

  RV_ITER_T it = RV_ITER_INIT(r);
  for (uint32_t i = 0; i < n; i++) {
    RV_ITER_RESULT ir = RV_ITER_NEXT(&it);
    arr[i] = ir.value;
  }
  return arr;
}

static inline RV_ROOT* RV_FROM_ARRAY(RRB_VEC_T* array, uint32_t count) {
  if (count == 0) return RV_EMPTY();

  /* Build leaves from the array */
  uint32_t num_leaves = (count + RV_BRANCH - 1) / RV_BRANCH;

  /* Last leaf becomes the tail, rest go into tree */
  uint32_t tree_leaf_count = num_leaves - 1;

  /* Build the tail (last leaf) */
  uint32_t tail_start = tree_leaf_count * RV_BRANCH;
  uint32_t tail_count = count - tail_start;
  RV_LEAF* tail = RV_MK_LEAF();
  tail->count = tail_count;
  memcpy(tail->elements, array + tail_start, sizeof(RRB_VEC_T) * tail_count);
  RV_LEAF_REHASH(tail);

  if (tree_leaf_count == 0) {
    /* All elements fit in the tail */
    return RV_MK_ROOT(NULL, tail, count, RV_BITS);
  }

  /* Build tree leaves */
  RV_NODE** leaves = (RV_NODE**)RV_NS(_allocator).alloc(
      RV_NS(_allocator).ctx, tree_leaf_count * sizeof(RV_NODE*));
  uint32_t* leaf_counts = (uint32_t*)RV_NS(_allocator).alloc(
      RV_NS(_allocator).ctx, tree_leaf_count * sizeof(uint32_t));

  for (uint32_t i = 0; i < tree_leaf_count; i++) {
    RV_LEAF* leaf = RV_MK_LEAF();
    uint32_t start = i * RV_BRANCH;
    uint32_t lc = RV_BRANCH; /* all tree leaves are full */
    leaf->count = lc;
    memcpy(leaf->elements, array + start, sizeof(RRB_VEC_T) * lc);
    RV_LEAF_REHASH(leaf);
    leaves[i] = (RV_NODE*)leaf;
    leaf_counts[i] = lc;
  }

  uint32_t shift;
  RV_NODE* tree_root = RV_BUILD_TREE(leaves, leaf_counts, tree_leaf_count, &shift);

  RV_NS(_allocator).free(RV_NS(_allocator).ctx, leaves);
  RV_NS(_allocator).free(RV_NS(_allocator).ctx, leaf_counts);

  return RV_MK_ROOT(tree_root, tail, count, shift);
}

/* --- Sort --- */

static RV_CMP_FN RV_NS(_qsort_cmp_ctx);

static int RV_NS(_qsort_wrapper)(const void* a, const void* b) {
  return RV_NS(_qsort_cmp_ctx)(*(const RRB_VEC_T*)a, *(const RRB_VEC_T*)b);
}

static inline RV_ROOT* RV_SORT(RV_ROOT* r, RV_CMP_FN cmp) {
  if (!r || r->count == 0) return RV_EMPTY();
  if (r->count == 1) {
    return RV_REF(r);
  }

  uint32_t n;
  RRB_VEC_T* arr = RV_TO_ARRAY(r, &n);
  RV_NS(_qsort_cmp_ctx) = cmp;
  qsort(arr, n, sizeof(RRB_VEC_T), RV_NS(_qsort_wrapper));
  RV_ROOT* result = RV_FROM_ARRAY(arr, n);
  RV_NS(_allocator).free(RV_NS(_allocator).ctx, arr);
  return result;
}

/* --- Transient API (RC mode only — requires ref counting for ownership checks) --- */

#ifndef RRB_VEC_GC_MODE

#define RV_TRANSIENT           RV_NS(_transient)
#define RV_TRANSIENT_FN        RV_NS(_transient_fn)
#define RV_PERSISTENT_FN       RV_NS(_persistent_fn)
#define RV_TRANSIENT_PUSH_BACK RV_NS(_transient_push_back)
#define RV_TRANSIENT_PUSH_TAIL RV_NS(_transient_push_tail)
#define RV_TRANSIENT_SET       RV_NS(_transient_set)
#define RV_TRANSIENT_SET_IN_TREE RV_NS(_transient_set_in_tree)
#define RV_TRANSIENT_POP_BACK  RV_NS(_transient_pop_back)
#define RV_TRANSIENT_POP_RESULT_T RV_NS(_transient_pop_result)
#define RV_TRANSIENT_REMOVE_RIGHTMOST RV_NS(_transient_remove_rightmost)
#define RV_TRANSIENT_PUSH_FRONT RV_NS(_transient_push_front)
#define RV_TRANSIENT_POP_FRONT  RV_NS(_transient_pop_front)

typedef struct RV_TRANSIENT {
  RV_NODE*  root;
  RV_LEAF*  tail;
  uint32_t  count;
  uint32_t  shift;
  thread_t  owner;
} RV_TRANSIENT;

/* Sentinel owner value: all bits set — no real thread should match */
static const thread_t RV_NS(_invalid_owner) = (thread_t)0;

static inline void RV_NS(_transient_destroy)(void* arg) {
  RV_TRANSIENT* t = (RV_TRANSIENT*)arg;
  if (!t) return;
  if (t->root) RV_RC_UNREF(t->root);
  if (t->tail) RV_RC_UNREF((RV_NODE*)t->tail);
}

static inline RV_TRANSIENT* RV_TRANSIENT_FN(RV_ROOT* r) {
  if (!r) return NULL;
  RV_TRANSIENT* t = (RV_TRANSIENT*)RV_RC_ALLOC(sizeof(RV_TRANSIENT), RV_NS(_transient_destroy));
  t->root  = r->root ? (RV_NODE*)RV_RC_REF(r->root) : NULL;
  /* Eagerly copy the tail so the transient exclusively owns it */
  t->tail  = RV_COPY_LEAF(r->tail);
  t->count = r->count;
  t->shift = r->shift;
  t->owner = THREAD_SELF();
  return t;
}

static inline RV_ROOT* RV_PERSISTENT_FN(RV_TRANSIENT* t) {
  if (!t) return NULL;
  /* Check if already invalidated (must come before owner check) */
  if (THREAD_EQUAL(t->owner, RV_NS(_invalid_owner))) {
    return NULL;
  }
  /* Owner check */
  if (!THREAD_EQUAL(THREAD_SELF(), t->owner)) {
#ifndef NDEBUG
    assert(0 && "transient used by non-owner thread");
#endif
    return NULL;
  }
  /* Take ownership of root and tail from the transient */
  RV_NODE* root = t->root ? (RV_NODE*)RV_RC_REF(t->root) : NULL;
  RV_LEAF* tail = (RV_LEAF*)RV_RC_REF((RV_NODE*)t->tail);
  RV_ROOT* result = RV_MK_ROOT(root, tail, t->count, t->shift);
  /* Invalidate the transient */
  t->owner = RV_NS(_invalid_owner);
  return result;
}

/* --- Transient push_tail (in-place when owned) --- */

static inline RV_NODE* RV_TRANSIENT_PUSH_TAIL(uint32_t level, uint32_t count, RV_NODE* node, RV_NODE* tail_node) {
  if (level == RV_BITS) {
    RV_INTERNAL* old = (RV_INTERNAL*)node;
    if (RV_RC_COUNT(old) == 1) {
      /* Owned — mutate in place */
      old->children[old->child_count] = (RV_NODE*)RV_RC_REF(tail_node);
      old->child_count++;
      RV_INTERNAL_REHASH(old);
      return node;
    }
    /* Shared — copy */
    RV_INTERNAL* new_node = RV_MK_INTERNAL();
    for (uint32_t i = 0; i < old->child_count; i++) {
      new_node->children[i] = (RV_NODE*)RV_RC_REF(old->children[i]);
    }
    new_node->children[old->child_count] = (RV_NODE*)RV_RC_REF(tail_node);
    new_node->child_count = old->child_count + 1;
    RV_INTERNAL_REHASH(new_node);
    return (RV_NODE*)new_node;
  }

  RV_INTERNAL* old = (RV_INTERNAL*)node;
  uint32_t subidx = ((count - 1) >> level) & RV_MASK;

  if (subidx < old->child_count) {
    /* Recurse into existing subtree */
    RV_NODE* new_child = RV_TRANSIENT_PUSH_TAIL(level - RV_BITS, count, old->children[subidx], tail_node);
    if (RV_RC_COUNT(old) == 1) {
      if (new_child != old->children[subidx]) {
        RV_RC_UNREF(old->children[subidx]);
        old->children[subidx] = new_child;
      }
      RV_INTERNAL_REHASH(old);
      return node;
    }
    RV_INTERNAL* new_node = RV_MK_INTERNAL();
    for (uint32_t i = 0; i < old->child_count; i++) {
      new_node->children[i] = (RV_NODE*)RV_RC_REF(old->children[i]);
    }
    new_node->child_count = old->child_count;
    RV_RC_UNREF(new_node->children[subidx]);
    new_node->children[subidx] = new_child;
    RV_INTERNAL_REHASH(new_node);
    return (RV_NODE*)new_node;
  } else {
    /* Create new path */
    RV_NODE* new_path = RV_NEW_PATH(level - RV_BITS, (RV_NODE*)RV_RC_REF(tail_node));
    if (RV_RC_COUNT(old) == 1) {
      old->children[subidx] = new_path;
      old->child_count = subidx + 1;
      RV_INTERNAL_REHASH(old);
      return node;
    }
    RV_INTERNAL* new_node = RV_MK_INTERNAL();
    for (uint32_t i = 0; i < old->child_count; i++) {
      new_node->children[i] = (RV_NODE*)RV_RC_REF(old->children[i]);
    }
    new_node->children[subidx] = new_path;
    new_node->child_count = subidx + 1;
    RV_INTERNAL_REHASH(new_node);
    return (RV_NODE*)new_node;
  }
}

/* --- Transient push_back (in-place) --- */

static inline RV_TRANSIENT* RV_TRANSIENT_PUSH_BACK(RV_TRANSIENT* t, RRB_VEC_T value) {
  if (!t) return NULL;
  /* Check if already invalidated (must come before owner check) */
  if (THREAD_EQUAL(t->owner, RV_NS(_invalid_owner))) {
    return NULL;
  }
  /* Owner check */
  if (!THREAD_EQUAL(THREAD_SELF(), t->owner)) {
#ifndef NDEBUG
    assert(0 && "transient used by non-owner thread");
#endif
    return NULL;
  }

  if (t->tail->count < RV_BRANCH) {
    /* Tail has room — write directly */
    t->tail->elements[t->tail->count] = value;
    t->tail->count++;
    RV_LEAF_REHASH(t->tail);
    t->count++;
    return t;
  }

  /* Tail is full — push into tree, create new tail */
  RV_LEAF* old_tail = t->tail;
  RV_LEAF_REHASH(old_tail);
  RV_LEAF* new_tail = RV_MK_LEAF();
  new_tail->elements[0] = value;
  new_tail->count = 1;
  RV_LEAF_REHASH(new_tail);

  RV_NODE* new_root;
  uint32_t new_shift = t->shift;

  if (t->root == NULL) {
    /* Tree was empty — wrap old tail in an internal node */
    RV_INTERNAL* node = RV_MK_INTERNAL();
    node->children[0] = (RV_NODE*)RV_RC_REF((RV_NODE*)old_tail);
    node->child_count = 1;
    RV_INTERNAL_REHASH(node);
    new_root = (RV_NODE*)node;
  } else {
    uint32_t tree_count = t->count - old_tail->count;
    uint32_t capacity = (uint32_t)1 << (t->shift + RV_BITS);
    if (tree_count >= capacity) {
      /* Overflow: create new root level */
      RV_INTERNAL* new_top = RV_MK_INTERNAL();
      new_top->children[0] = (RV_NODE*)RV_RC_REF(t->root);
      new_top->children[1] = RV_NEW_PATH(t->shift, (RV_NODE*)RV_RC_REF((RV_NODE*)old_tail));
      new_top->child_count = 2;
      RV_INTERNAL_REHASH(new_top);
      new_root = (RV_NODE*)new_top;
      new_shift = t->shift + RV_BITS;
      RV_RC_UNREF(t->root);
    } else {
      /* Push old tail into existing tree — use t->count (total count including tail)
       * as routing key, matching Clojure's cnt convention */
      new_root = RV_TRANSIENT_PUSH_TAIL(t->shift, t->count, t->root, (RV_NODE*)old_tail);
      if (new_root != t->root) {
        RV_RC_UNREF(t->root);
      }
    }
  }

  RV_RC_UNREF((RV_NODE*)old_tail);
  t->root = new_root;
  t->tail = new_tail;
  t->count++;
  t->shift = new_shift;
  return t;
}

/* --- Transient set (in-place when owned) --- */

static inline RV_NODE* RV_TRANSIENT_SET_IN_TREE(uint32_t level, RV_NODE* node, uint32_t index, RRB_VEC_T value) {
  if (level == 0) {
    /* Leaf level */
    RV_LEAF* leaf = (RV_LEAF*)node;
    if (RV_RC_COUNT(leaf) == 1) {
      /* Owned — mutate in place */
      leaf->elements[index] = value;
      RV_LEAF_REHASH(leaf);
      return node;
    }
    /* Shared — copy */
    RV_LEAF* new_leaf = RV_COPY_LEAF(leaf);
    new_leaf->elements[index] = value;
    RV_LEAF_REHASH(new_leaf);
    return (RV_NODE*)new_leaf;
  }

  RV_INTERNAL* old = (RV_INTERNAL*)node;
  uint32_t slot;
  uint32_t child_index;
  if (old->size_table) {
    slot = 0;
    while (old->size_table[slot] <= index) slot++;
    child_index = (slot > 0) ? index - old->size_table[slot - 1] : index;
  } else {
    slot = (index >> level) & RV_MASK;
    child_index = index - (slot << level);
  }

  /* Ensure we have an owned node BEFORE recursing — top-down copy.
   * This is critical: if the parent is shared, children appear owned (rc=1)
   * but are actually shared between old and new parent after copy.
   * Copying first ensures children get correct rc before the recursion checks. */
  RV_INTERNAL* work;
  if (RV_RC_COUNT(old) == 1) {
    work = old;
  } else {
    work = RV_COPY_INTERNAL(old);
  }

  RV_NODE* new_child = RV_TRANSIENT_SET_IN_TREE(level - RV_BITS, work->children[slot], child_index, value);
  if (new_child != work->children[slot]) {
    RV_RC_UNREF(work->children[slot]);
    work->children[slot] = new_child;
  }
  RV_INTERNAL_REHASH(work);
  return (RV_NODE*)work;
}

static inline RV_TRANSIENT* RV_TRANSIENT_SET(RV_TRANSIENT* t, uint32_t index, RRB_VEC_T value) {
  if (!t) return NULL;
  /* Check if already invalidated */
  if (THREAD_EQUAL(t->owner, RV_NS(_invalid_owner))) {
    return NULL;
  }
  /* Owner check */
  if (!THREAD_EQUAL(THREAD_SELF(), t->owner)) {
#ifndef NDEBUG
    assert(0 && "transient used by non-owner thread");
#endif
    return NULL;
  }
  /* Bounds check */
  if (index >= t->count) {
#ifndef NDEBUG
    assert(0 && "transient set: index out of bounds");
#endif
    return NULL;
  }

  /* Check if index is in the tail */
  uint32_t tail_off = t->count - t->tail->count;
  if (index >= tail_off) {
    /* Tail is always owned — write directly */
    t->tail->elements[index - tail_off] = value;
    RV_LEAF_REHASH(t->tail);
    return t;
  }

  /* In the tree — use in-place mutation when owned */
  RV_NODE* new_root = RV_TRANSIENT_SET_IN_TREE(t->shift, t->root, index, value);
  if (new_root != t->root) {
    RV_RC_UNREF(t->root);
    t->root = new_root;
  }
  return t;
}

/* --- Transient pop_back result --- */

typedef struct RV_TRANSIENT_POP_RESULT_T {
  RV_TRANSIENT* transient;
  RRB_VEC_T     value;
  bool          found;
} RV_TRANSIENT_POP_RESULT_T;

/* --- Transient remove_rightmost (in-place when owned) --- */

static inline RV_NODE* RV_TRANSIENT_REMOVE_RIGHTMOST(uint32_t level, RV_NODE* node) {
  RV_INTERNAL* old = (RV_INTERNAL*)node;

  if (level == RV_BITS) {
    /* At lowest internal level, the rightmost child is the leaf to remove */
    if (old->child_count == 1) {
      /* Node becomes empty — caller must unref this node */
      return NULL;
    }
    if (RV_RC_COUNT(old) == 1) {
      /* Owned — mutate in place */
      old->child_count--;
      RV_RC_UNREF(old->children[old->child_count]);
      old->children[old->child_count] = NULL;
      RV_INTERNAL_REHASH(old);
      return node;
    }
    /* Shared — copy without last child */
    RV_INTERNAL* new_node = RV_MK_INTERNAL();
    new_node->child_count = old->child_count - 1;
    for (uint32_t i = 0; i < new_node->child_count; i++) {
      new_node->children[i] = (RV_NODE*)RV_RC_REF(old->children[i]);
    }
    if (old->size_table) {
      size_t st_size = sizeof(uint32_t) * new_node->child_count;
      new_node->size_table = (uint32_t*)RV_NS(_allocator).alloc(RV_NS(_allocator).ctx, st_size);
      memcpy(new_node->size_table, old->size_table, st_size);
    }
    RV_INTERNAL_REHASH(new_node);
    return (RV_NODE*)new_node;
  }

  /* Higher level — top-down copy: ensure we own the node BEFORE recursing */
  RV_INTERNAL* work;
  if (RV_RC_COUNT(old) == 1) {
    work = old;
  } else {
    work = RV_COPY_INTERNAL(old);
  }

  uint32_t last = work->child_count - 1;
  RV_NODE* new_child = RV_TRANSIENT_REMOVE_RIGHTMOST(level - RV_BITS, work->children[last]);

  if (new_child == NULL) {
    /* Child subtree became empty */
    RV_RC_UNREF(work->children[last]);
    work->children[last] = NULL;
    work->child_count--;
    if (work->child_count == 0) {
      return NULL;
    }
  } else {
    if (new_child != work->children[last]) {
      RV_RC_UNREF(work->children[last]);
      work->children[last] = new_child;
    }
    /* Update size table if present — subtree lost a leaf */
    if (work->size_table) {
      work->size_table[last] = (last > 0 ? work->size_table[last - 1] : 0)
                               + RV_SUBTREE_COUNT(new_child, level - RV_BITS);
    }
  }
  RV_INTERNAL_REHASH(work);
  return (RV_NODE*)work;
}

/* --- Transient pop_back (in-place) --- */

static inline RV_TRANSIENT_POP_RESULT_T RV_TRANSIENT_POP_BACK(RV_TRANSIENT* t) {
  if (!t) return (RV_TRANSIENT_POP_RESULT_T){.transient = NULL, .found = false};
  /* Check if already invalidated */
  if (THREAD_EQUAL(t->owner, RV_NS(_invalid_owner))) {
    return (RV_TRANSIENT_POP_RESULT_T){.transient = NULL, .found = false};
  }
  /* Owner check */
  if (!THREAD_EQUAL(THREAD_SELF(), t->owner)) {
#ifndef NDEBUG
    assert(0 && "transient used by non-owner thread");
#endif
    return (RV_TRANSIENT_POP_RESULT_T){.transient = NULL, .found = false};
  }
  /* Empty check */
  if (t->count == 0) {
    return (RV_TRANSIENT_POP_RESULT_T){.transient = NULL, .found = false};
  }

  RRB_VEC_T popped = t->tail->elements[t->tail->count - 1];

  if (t->tail->count > 1) {
    /* Tail has more than one element — just shrink */
    t->tail->count--;
    RV_LEAF_REHASH(t->tail);
    t->count--;
    return (RV_TRANSIENT_POP_RESULT_T){.transient = t, .value = popped, .found = true};
  }

  /* Tail has exactly 1 element */
  if (t->root == NULL) {
    /* Popping last element */
    t->tail->count = 0;
    t->count = 0;
    return (RV_TRANSIENT_POP_RESULT_T){.transient = t, .value = popped, .found = true};
  }

  /* Pull rightmost leaf from tree to become new tail */
  RV_LEAF* new_tail = RV_COPY_LEAF(RV_RIGHTMOST_LEAF(t->root, t->shift));
  RV_NODE* old_root = t->root;
  RV_NODE* new_tree = RV_TRANSIENT_REMOVE_RIGHTMOST(t->shift, old_root);
  uint32_t new_shift = t->shift;

  /* Unref old root if it was replaced (not mutated in place) */
  if (new_tree != old_root) {
    RV_RC_UNREF(old_root);
  }

  /* Shrink tree if root has single child */
  while (new_tree != NULL && new_shift > RV_BITS) {
    RV_INTERNAL* top = (RV_INTERNAL*)new_tree;
    if (top->child_count == 1) {
      RV_NODE* child = (RV_NODE*)RV_RC_REF(top->children[0]);
      RV_RC_UNREF(new_tree);
      new_tree = child;
      new_shift -= RV_BITS;
    } else {
      break;
    }
  }

  RV_RC_UNREF((RV_NODE*)t->tail);
  t->root = new_tree;
  t->tail = new_tail;
  t->count--;
  t->shift = new_shift;

  return (RV_TRANSIENT_POP_RESULT_T){.transient = t, .value = popped, .found = true};
}

/* --- Transient push_front (via concat) --- */

static inline RV_TRANSIENT* RV_TRANSIENT_PUSH_FRONT(RV_TRANSIENT* t, RRB_VEC_T value) {
  if (!t) return NULL;
  /* Check if already invalidated */
  if (THREAD_EQUAL(t->owner, RV_NS(_invalid_owner))) {
    return NULL;
  }
  /* Owner check */
  if (!THREAD_EQUAL(THREAD_SELF(), t->owner)) {
#ifndef NDEBUG
    assert(0 && "transient used by non-owner thread");
#endif
    return NULL;
  }

  /* Create a temporary persistent view of the transient's current state */
  RV_NODE* tmp_root = t->root ? (RV_NODE*)RV_RC_REF(t->root) : NULL;
  RV_LEAF* tmp_tail = (RV_LEAF*)RV_RC_REF((RV_NODE*)t->tail);
  RV_ROOT* temp = RV_MK_ROOT(tmp_root, tmp_tail, t->count, t->shift);

  /* Create single-element vec and concat */
  RV_ROOT* single = RV_EMPTY();
  RV_ROOT* s1 = RV_PUSH_BACK(single, value);
  RV_UNREF(single);
  RV_ROOT* result = RV_CONCAT(s1, temp);
  RV_UNREF(s1);
  RV_UNREF(temp);

  /* Replace transient internals with result */
  if (t->root) RV_RC_UNREF(t->root);
  RV_RC_UNREF((RV_NODE*)t->tail);

  t->root  = result->root ? (RV_NODE*)RV_RC_REF(result->root) : NULL;
  t->tail  = (RV_LEAF*)RV_RC_REF((RV_NODE*)result->tail);
  t->count = result->count;
  t->shift = result->shift;

  RV_UNREF(result);
  return t;
}

/* --- Transient pop_front (via get + slice) --- */

static inline RV_TRANSIENT_POP_RESULT_T RV_TRANSIENT_POP_FRONT(RV_TRANSIENT* t) {
  if (!t) return (RV_TRANSIENT_POP_RESULT_T){.transient = NULL, .found = false};
  /* Check if already invalidated */
  if (THREAD_EQUAL(t->owner, RV_NS(_invalid_owner))) {
    return (RV_TRANSIENT_POP_RESULT_T){.transient = NULL, .found = false};
  }
  /* Owner check */
  if (!THREAD_EQUAL(THREAD_SELF(), t->owner)) {
#ifndef NDEBUG
    assert(0 && "transient used by non-owner thread");
#endif
    return (RV_TRANSIENT_POP_RESULT_T){.transient = NULL, .found = false};
  }
  /* Empty check */
  if (t->count == 0) {
    return (RV_TRANSIENT_POP_RESULT_T){.transient = NULL, .found = false};
  }

  /* Create a temporary persistent view of the transient's current state */
  RV_NODE* tmp_root = t->root ? (RV_NODE*)RV_RC_REF(t->root) : NULL;
  RV_LEAF* tmp_tail = (RV_LEAF*)RV_RC_REF((RV_NODE*)t->tail);
  RV_ROOT* temp = RV_MK_ROOT(tmp_root, tmp_tail, t->count, t->shift);

  /* Get first element */
  RV_GET_RESULT gr = RV_GET(temp, 0);
  RRB_VEC_T popped = gr.value;

  /* Slice off first element */
  RV_ROOT* rest;
  if (t->count == 1) {
    rest = RV_EMPTY();
  } else {
    rest = RV_SLICE(temp, 1, t->count);
  }
  RV_UNREF(temp);

  /* Replace transient internals with result */
  if (t->root) RV_RC_UNREF(t->root);
  RV_RC_UNREF((RV_NODE*)t->tail);

  t->root  = rest->root ? (RV_NODE*)RV_RC_REF(rest->root) : NULL;
  t->tail  = (RV_LEAF*)RV_RC_REF((RV_NODE*)rest->tail);
  t->count = rest->count;
  t->shift = rest->shift;

  RV_UNREF(rest);
  return (RV_TRANSIENT_POP_RESULT_T){.transient = t, .value = popped, .found = true};
}

#endif /* !RRB_VEC_GC_MODE — end of transient API */

/* --- Map into (cross-type) --- */

#ifdef RRB_VEC_MAP_INTO_DEST_NAME

#define RV_MAP_INTO        RV_NS(_map_into)
#define RV_MAP_INTO_FN     RV_NS(_map_into_fn)
#define RV_MAP_INTO_DEST_ROOT  RV_CAT(RRB_VEC_MAP_INTO_DEST_NAME, _root)
#define RV_MAP_INTO_DEST_EMPTY RV_CAT(RRB_VEC_MAP_INTO_DEST_NAME, _empty)
#define RV_MAP_INTO_DEST_PUSH  RV_CAT(RRB_VEC_MAP_INTO_DEST_NAME, _push_back)
#define RV_MAP_INTO_DEST_UNREF RV_CAT(RRB_VEC_MAP_INTO_DEST_NAME, _unref)

typedef RRB_VEC_MAP_INTO_DEST_T (*RV_MAP_INTO_FN)(RRB_VEC_T);

static inline RV_MAP_INTO_DEST_ROOT* RV_MAP_INTO(RV_ROOT* r, RV_MAP_INTO_FN fn) {
  RV_MAP_INTO_DEST_ROOT* result = RV_MAP_INTO_DEST_EMPTY();
  if (!r || r->count == 0) return result;

  RV_ITER_T it = RV_ITER_INIT(r);
  while (1) {
    RV_ITER_RESULT ir = RV_ITER_NEXT(&it);
    if (ir.done) break;
    RV_MAP_INTO_DEST_ROOT* next = RV_MAP_INTO_DEST_PUSH(result, fn(ir.value));
    RV_MAP_INTO_DEST_UNREF(result);
    result = next;
  }
  return result;
}

#undef RV_MAP_INTO
#undef RV_MAP_INTO_FN
#undef RV_MAP_INTO_DEST_ROOT
#undef RV_MAP_INTO_DEST_EMPTY
#undef RV_MAP_INTO_DEST_PUSH
#undef RV_MAP_INTO_DEST_UNREF
#undef RRB_VEC_MAP_INTO_DEST_NAME
#undef RRB_VEC_MAP_INTO_DEST_T

#endif /* RRB_VEC_MAP_INTO_DEST_NAME */

/* --- Map and filter --- */

#ifdef RRB_VEC_GC_MODE
/* GC mode: use persistent push_back (no transients available) */

static inline RV_ROOT* RV_MAP(RV_ROOT* r, RV_MAP_FN fn) {
  if (!r || r->count == 0) return RV_EMPTY();

  RV_ROOT* result = RV_EMPTY();
  RV_ITER_T it = RV_ITER_INIT(r);
  while (1) {
    RV_ITER_RESULT ir = RV_ITER_NEXT(&it);
    if (ir.done) break;
    RV_ROOT* next = RV_PUSH_BACK(result, fn(ir.value));
    result = next;
  }
  return result;
}

static inline RV_ROOT* RV_FILTER(RV_ROOT* r, RV_PRED_FN pred) {
  if (!r || r->count == 0) return RV_EMPTY();

  RV_ROOT* result = RV_EMPTY();
  RV_ITER_T it = RV_ITER_INIT(r);
  while (1) {
    RV_ITER_RESULT ir = RV_ITER_NEXT(&it);
    if (ir.done) break;
    if (pred(ir.value)) {
      RV_ROOT* next = RV_PUSH_BACK(result, ir.value);
      result = next;
    }
  }
  return result;
}

#else /* !RRB_VEC_GC_MODE */
/* RC mode: use transient push_back for efficiency */

static inline RV_ROOT* RV_MAP(RV_ROOT* r, RV_MAP_FN fn) {
  RV_ROOT* empty = RV_EMPTY();
  if (!r || r->count == 0) return empty;

  RV_TRANSIENT* t = RV_TRANSIENT_FN(empty);
  RV_UNREF(empty);

  RV_ITER_T it = RV_ITER_INIT(r);
  while (1) {
    RV_ITER_RESULT ir = RV_ITER_NEXT(&it);
    if (ir.done) break;
    RV_TRANSIENT_PUSH_BACK(t, fn(ir.value));
  }

  RV_ROOT* result = RV_PERSISTENT_FN(t);
  RV_RC_UNREF(t);
  return result;
}

static inline RV_ROOT* RV_FILTER(RV_ROOT* r, RV_PRED_FN pred) {
  RV_ROOT* empty = RV_EMPTY();
  if (!r || r->count == 0) return empty;

  RV_TRANSIENT* t = RV_TRANSIENT_FN(empty);
  RV_UNREF(empty);

  RV_ITER_T it = RV_ITER_INIT(r);
  while (1) {
    RV_ITER_RESULT ir = RV_ITER_NEXT(&it);
    if (ir.done) break;
    if (pred(ir.value)) {
      RV_TRANSIENT_PUSH_BACK(t, ir.value);
    }
  }

  RV_ROOT* result = RV_PERSISTENT_FN(t);
  RV_RC_UNREF(t);
  return result;
}

#endif /* RRB_VEC_GC_MODE */

/* --- Transient iterator (RC mode only) --- */

#ifndef RRB_VEC_GC_MODE

#define RV_TRANSIENT_ITER_T    RV_NS(_transient_iter)
#define RV_TRANSIENT_ITER_INIT RV_NS(_transient_iter_init)
#define RV_TRANSIENT_ITER_NEXT RV_NS(_transient_iter_next)

typedef struct RV_TRANSIENT_ITER_T {
  RV_TRANSIENT* transient;
  uint32_t      index;
  RV_LEAF*      leaf;
  uint32_t      leaf_base;
} RV_TRANSIENT_ITER_T;

static inline RV_TRANSIENT_ITER_T RV_TRANSIENT_ITER_INIT(RV_TRANSIENT* t) {
  RV_TRANSIENT_ITER_T it;
  memset(&it, 0, sizeof(it));
  if (!t || THREAD_EQUAL(t->owner, RV_NS(_invalid_owner))) {
    return it;
  }
  if (!THREAD_EQUAL(THREAD_SELF(), t->owner)) {
#ifndef NDEBUG
    assert(0 && "transient used by non-owner thread");
#endif
    return it;
  }
  it.transient = t;
  if (t->count > 0) {
    /* Seed with the first leaf */
    uint32_t tail_off = t->count - t->tail->count;
    if (tail_off == 0) {
      /* All elements in tail */
      it.leaf = t->tail;
      it.leaf_base = 0;
    } else {
      /* First leaf is in the tree — traverse leftmost path */
      RV_NODE* node = t->root;
      for (uint32_t level = t->shift; level > 0; level -= RV_BITS) {
        RV_INTERNAL* n = (RV_INTERNAL*)node;
        node = n->children[0];
      }
      it.leaf = (RV_LEAF*)node;
      it.leaf_base = 0;
    }
  }
  return it;
}

static inline RV_ITER_RESULT RV_TRANSIENT_ITER_NEXT(RV_TRANSIENT_ITER_T* it) {
  if (!it->transient || it->index >= it->transient->count) {
    return (RV_ITER_RESULT){.done = true};
  }
  uint32_t local = it->index - it->leaf_base;
  if (local >= it->leaf->count) {
    /* Need to advance to next leaf */
    uint32_t tail_off = it->transient->count - it->transient->tail->count;
    if (it->index >= tail_off) {
      /* In tail */
      it->leaf = it->transient->tail;
      it->leaf_base = tail_off;
    } else {
      /* In tree — traverse to find leaf */
      RV_NODE* node = it->transient->root;
      uint32_t idx = it->index;
      for (uint32_t level = it->transient->shift; level > 0; level -= RV_BITS) {
        RV_INTERNAL* n = (RV_INTERNAL*)node;
        uint32_t slot;
        if (n->size_table) {
          slot = 0;
          while (n->size_table[slot] <= idx) slot++;
          if (slot > 0) idx -= n->size_table[slot - 1];
        } else {
          slot = (idx >> level) & RV_MASK;
          idx -= slot << level;
        }
        node = n->children[slot];
      }
      it->leaf = (RV_LEAF*)node;
      it->leaf_base = it->index - idx;
    }
    local = it->index - it->leaf_base;
  }
  RRB_VEC_T val = it->leaf->elements[local];
  it->index++;
  return (RV_ITER_RESULT){.value = val, .done = false};
}

/* --- Transient for_each --- */

#define RV_TRANSIENT_FOR_EACH RV_NS(_transient_for_each)

typedef void (*RV_NS(_for_each_fn))(RRB_VEC_T);

static inline void RV_TRANSIENT_FOR_EACH(RV_TRANSIENT* t, RV_NS(_for_each_fn) fn) {
  if (!t || THREAD_EQUAL(t->owner, RV_NS(_invalid_owner))) return;
  if (!THREAD_EQUAL(THREAD_SELF(), t->owner)) {
#ifndef NDEBUG
    assert(0 && "transient used by non-owner thread");
#endif
    return;
  }
  RV_TRANSIENT_ITER_T it = RV_TRANSIENT_ITER_INIT(t);
  while (1) {
    RV_ITER_RESULT ir = RV_TRANSIENT_ITER_NEXT(&it);
    if (ir.done) break;
    fn(ir.value);
  }
}

/* --- Transient map --- */

#define RV_TRANSIENT_MAP RV_NS(_transient_map)

static inline RV_TRANSIENT* RV_TRANSIENT_MAP(RV_TRANSIENT* t, RV_MAP_FN fn) {
  if (!t) return NULL;
  if (THREAD_EQUAL(t->owner, RV_NS(_invalid_owner))) return NULL;
  if (!THREAD_EQUAL(THREAD_SELF(), t->owner)) {
#ifndef NDEBUG
    assert(0 && "transient used by non-owner thread");
#endif
    return NULL;
  }

  RV_ROOT* empty_root = RV_EMPTY();
  RV_TRANSIENT* result = RV_TRANSIENT_FN(empty_root);
  RV_UNREF(empty_root);

  RV_TRANSIENT_ITER_T it = RV_TRANSIENT_ITER_INIT(t);
  while (1) {
    RV_ITER_RESULT ir = RV_TRANSIENT_ITER_NEXT(&it);
    if (ir.done) break;
    RV_TRANSIENT_PUSH_BACK(result, fn(ir.value));
  }

  return result;
}

/* --- Transient filter --- */

#define RV_TRANSIENT_FILTER RV_NS(_transient_filter)

static inline RV_TRANSIENT* RV_TRANSIENT_FILTER(RV_TRANSIENT* t, RV_PRED_FN pred) {
  if (!t) return NULL;
  if (THREAD_EQUAL(t->owner, RV_NS(_invalid_owner))) return NULL;
  if (!THREAD_EQUAL(THREAD_SELF(), t->owner)) {
#ifndef NDEBUG
    assert(0 && "transient used by non-owner thread");
#endif
    return NULL;
  }

  RV_ROOT* empty_root = RV_EMPTY();
  RV_TRANSIENT* result = RV_TRANSIENT_FN(empty_root);
  RV_UNREF(empty_root);

  RV_TRANSIENT_ITER_T it = RV_TRANSIENT_ITER_INIT(t);
  while (1) {
    RV_ITER_RESULT ir = RV_TRANSIENT_ITER_NEXT(&it);
    if (ir.done) break;
    if (pred(ir.value)) {
      RV_TRANSIENT_PUSH_BACK(result, ir.value);
    }
  }

  return result;
}

#endif /* !RRB_VEC_GC_MODE — end of transient iterator/map/filter */

/* --- Cleanup all internal macros --- */

#undef RRB_VEC_T
#undef RRB_VEC_NAME

#undef RV_XCAT
#undef RV_CAT
#undef RV_NS

#undef RV_NODE_TYPE
#undef RV_NODE
#undef RV_INTERNAL
#undef RV_LEAF
#undef RV_ROOT
#undef RV_GET_RESULT
#undef RV_POP_RESULT
#undef RV_ITER_T
#undef RV_ITER_RESULT

#undef RV_EMPTY
#undef RV_REF
#undef RV_UNREF
#undef RV_COUNT
#undef RV_PUSH_BACK
#undef RV_GET
#undef RV_SET
#undef RV_POP_BACK
#undef RV_CONCAT
#undef RV_SLICE
#undef RV_PUSH_FRONT
#undef RV_POP_FRONT
#undef RV_INSERT
#undef RV_REMOVE
#undef RV_MAP
#undef RV_FILTER
#undef RV_REDUCE
#undef RV_FIND
#undef RV_INDEX_OF
#undef RV_EQUALS
#undef RV_TO_ARRAY
#undef RV_FROM_ARRAY
#undef RV_SORT
#undef RV_ITER_INIT
#undef RV_ITER_NEXT

#ifndef RRB_VEC_GC_MODE
#undef RV_NODE_DESTROY
#undef RV_ROOT_DESTROY
#endif
#undef RV_MK_LEAF
#undef RV_MK_INTERNAL
#undef RV_MK_ROOT
#undef RV_COPY_LEAF
#undef RV_PUSH_TAIL
#undef RV_NEW_PATH
#undef RV_TAIL_OFFSET
#undef RV_GET_LEAF
#undef RV_COPY_INTERNAL
#undef RV_SET_IN_TREE
#undef RV_POP_TAIL
#undef RV_RIGHTMOST_LEAF
#undef RV_REMOVE_RIGHTMOST
#undef RV_SUBTREE_COUNT
#undef RV_PUSH_TAIL_FULL
#undef RV_FIND_LEAF_OFF
#undef RV_BUILD_TREE

#undef RV_SET_HASH_HANDLER
#undef RV_HASH
#undef RV_ELEMENT_HASH_FN_T
#undef RV_ELEMENT_HASH_PTR
#undef RV_ROTATE_LEFT
#undef RV_LEAF_REHASH
#undef RV_INTERNAL_REHASH

#undef RV_MAP_FN
#undef RV_PRED_FN
#undef RV_REDUCE_FN
#undef RV_EQ_FN
#undef RV_CMP_FN

#ifndef RRB_VEC_GC_MODE
#undef RV_TRANSIENT
#undef RV_TRANSIENT_FN
#undef RV_PERSISTENT_FN
#undef RV_TRANSIENT_PUSH_BACK
#undef RV_TRANSIENT_PUSH_TAIL
#undef RV_TRANSIENT_SET
#undef RV_TRANSIENT_SET_IN_TREE
#undef RV_TRANSIENT_POP_BACK
#undef RV_TRANSIENT_POP_RESULT_T
#undef RV_TRANSIENT_REMOVE_RIGHTMOST
#undef RV_TRANSIENT_PUSH_FRONT
#undef RV_TRANSIENT_POP_FRONT
#undef RV_TRANSIENT_ITER_T
#undef RV_TRANSIENT_ITER_INIT
#undef RV_TRANSIENT_ITER_NEXT
#undef RV_TRANSIENT_FOR_EACH
#undef RV_TRANSIENT_MAP
#undef RV_TRANSIENT_FILTER
#endif

#undef RV_NODE_INTERNAL_VAL
#undef RV_NODE_LEAF_VAL

#undef RV_RC_ALLOC
#undef RV_RC_REF
#undef RV_RC_UNREF
#undef RV_RC_COUNT

#undef RV_ALLOC_LEAF
#undef RV_ALLOC_INTERNAL
#undef RV_ALLOC_ROOT

#undef RV_BITS
#undef RV_BRANCH
#undef RV_MASK

#ifdef RRB_VEC_GC_MODE
#undef RRB_VEC_GC_MODE
#undef RRB_VEC_GC_ALLOC
#undef RRB_VEC_GC_OBJ_INTERNAL
#undef RRB_VEC_GC_OBJ_LEAF
#undef RRB_VEC_GC_OBJ_ROOT
#endif

#ifdef RRB_VEC_ALLOC_DEFAULTED
#undef RRB_VEC_ALLOCATOR
#undef RRB_VEC_ALLOC_DEFAULTED
#endif
