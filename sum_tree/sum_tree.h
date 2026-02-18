/* sum_tree.h - Persistent B-tree with monoidal summaries (include-as-template)
 *
 * Generic persistent B-tree where each node caches a monoidal summary
 * of its subtree. Suitable for ropes, segment trees, and similar structures.
 *
 * Define STREE_T (element type), STREE_SUMMARY_T (summary/monoid type),
 * and STREE_NAME (prefix) before including. Can be included multiple times
 * with different values.
 *
 * Example:
 *   #define STREE_T          char
 *   #define STREE_SUMMARY_T  size_t
 *   #define STREE_NAME       rope
 *   #include "sum_tree.h"
 *   // Now have: rope_root, rope_empty, rope_from_array, rope_get, etc.
 */

#ifndef STREE_T
#error "STREE_T must be defined before including sum_tree.h"
#define STREE_T int /* suppress further errors */
#endif

#ifndef STREE_SUMMARY_T
#error "STREE_SUMMARY_T must be defined before including sum_tree.h"
#define STREE_SUMMARY_T int
#endif

#ifndef STREE_NAME
#error "STREE_NAME must be defined before including sum_tree.h"
#define STREE_NAME _stree_err
#endif

/* Concatenation helpers */
#define ST_XCAT(a, b) a##b
#define ST_CAT(a, b)  ST_XCAT(a, b)
#define ST_NS(name)   ST_CAT(STREE_NAME, name)

/* --- Generated type names --- */

#define ST_NODE_TYPE  ST_NS(_node_type)
#define ST_NODE       ST_NS(_node)
#define ST_INTERNAL   ST_NS(_internal)
#define ST_LEAF       ST_NS(_leaf)
#define ST_ROOT       ST_NS(_root)
#define ST_HANDLERS   ST_NS(_handlers)
#define ST_GET_RESULT ST_NS(_get_result)
#define ST_SPLIT_RESULT ST_NS(_split_result)
#define ST_SEARCH_RESULT ST_NS(_search_result)
#define ST_DIMENSION_FN  ST_NS(_dimension_fn_t)

/* Node type enum values */
#define ST_NODE_INTERNAL_VAL ST_NS(_NODE_INTERNAL)
#define ST_NODE_LEAF_VAL     ST_NS(_NODE_LEAF)

/* --- Generated function names --- */

#define ST_SET_HANDLERS  ST_NS(_set_handlers)
#define ST_EMPTY         ST_NS(_empty)
#define ST_REF           ST_NS(_ref)
#define ST_UNREF         ST_NS(_unref)
#define ST_COUNT         ST_NS(_count)
#define ST_SUMMARY       ST_NS(_summary)
#define ST_FROM_ARRAY    ST_NS(_from_array)
#define ST_GET           ST_NS(_get)
#define ST_SET           ST_NS(_set)
#define ST_SPLIT         ST_NS(_split)
#define ST_CONCAT        ST_NS(_concat)
#define ST_PUSH_BACK     ST_NS(_push_back)
#define ST_PUSH_FRONT    ST_NS(_push_front)
#define ST_SEARCH        ST_NS(_search)
#define ST_COPY_RANGE    ST_NS(_copy_range)

/* Internal helpers */
#define ST_NODE_DESTROY     ST_NS(_node_destroy)
#define ST_ROOT_DESTROY     ST_NS(_root_destroy)
#define ST_MK_LEAF          ST_NS(_mk_leaf)
#define ST_MK_INTERNAL      ST_NS(_mk_internal)
#define ST_MK_ROOT          ST_NS(_mk_root)
#define ST_RECOMPUTE_SUMMARY ST_NS(_recompute_summary)
#define ST_LEAF_SUMMARY     ST_NS(_leaf_summary)
#define ST_INTERNAL_SUMMARY ST_NS(_internal_summary)
#define ST_NODE_COUNT       ST_NS(_node_count)
#define ST_NODE_SUMMARY     ST_NS(_node_summary)
#define ST_MK_INTERNAL_NREF ST_NS(_mk_internal_nref)

/* Handler function pointer types */
#define ST_IDENTITY_FN    ST_NS(_identity_fn_t)
#define ST_COMBINE_FN     ST_NS(_combine_fn_t)
#define ST_SUMMARIZE_FN   ST_NS(_summarize_fn_t)
#define ST_LEAF_SEARCH_FN ST_NS(_leaf_search_fn_t)

/* Static state */
#define ST_HANDLER_STATE ST_NS(_handler_state)
#define ST_ALLOCATOR_VAR ST_NS(_allocator)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../platform/platform.h"

/* --- Leaf max --- */

#ifndef STREE_LEAF_MAX
#define STREE_LEAF_MAX           32
#define STREE_LEAF_MAX_DEFAULTED 1
#endif

#define ST_LEAF_MIN (STREE_LEAF_MAX / 2)

/* --- Allocation hooks --- */

#ifndef STREE_ALLOCATOR
#define STREE_ALLOCATOR          libc_allocator
#define STREE_ALLOC_DEFAULTED    1
#endif

static Allocator ST_ALLOCATOR_VAR = STREE_ALLOCATOR;

/* Wire into rc.h template */
#define RC_ALLOCATOR ST_ALLOCATOR_VAR
#define RC_NAME      STREE_NAME
#include "../rc/rc.h"

/* RC function aliases */
#define ST_RC_ALLOC ST_NS(_rc_alloc)
#define ST_RC_REF   ST_NS(_rc_ref)
#define ST_RC_UNREF ST_NS(_rc_unref)
#define ST_RC_COUNT ST_NS(_rc_count)

/* --- Handler typedefs --- */

typedef STREE_SUMMARY_T (*ST_IDENTITY_FN)(void);
typedef STREE_SUMMARY_T (*ST_COMBINE_FN)(STREE_SUMMARY_T a, STREE_SUMMARY_T b);
typedef STREE_SUMMARY_T (*ST_SUMMARIZE_FN)(const STREE_T* elems, size_t count);
typedef size_t (*ST_LEAF_SEARCH_FN)(const STREE_T* elements, size_t count,
                                    STREE_SUMMARY_T* accumulated,
                                    size_t (*dimension_fn)(STREE_SUMMARY_T),
                                    size_t target);

/* --- Handler struct --- */

typedef struct ST_HANDLERS {
  ST_IDENTITY_FN   identity;
  ST_COMBINE_FN    combine;
  ST_SUMMARIZE_FN  summarize;
  ST_LEAF_SEARCH_FN leaf_search;
} ST_HANDLERS;

/* --- Per-instantiation static state --- */

static ST_HANDLERS ST_HANDLER_STATE;

static inline void ST_SET_HANDLERS(ST_HANDLERS handlers) {
  ST_HANDLER_STATE = handlers;
}

/* --- Node type enum --- */

typedef enum { ST_NODE_INTERNAL_VAL, ST_NODE_LEAF_VAL } ST_NODE_TYPE;

/* --- Node header --- */

typedef struct ST_NODE {
  ST_NODE_TYPE type;
} ST_NODE;

/* --- Leaf node: holds elements + cached summary --- */

typedef struct ST_LEAF {
  ST_NODE        header;
  size_t         count;
  STREE_SUMMARY_T summary;
  STREE_T        elements[STREE_LEAF_MAX];
} ST_LEAF;

/* --- Internal node: holds children + cached summary --- */

typedef struct ST_INTERNAL {
  ST_NODE         header;
  size_t          n_children;
  size_t          count;        /* total elements in subtree */
  STREE_SUMMARY_T summary;
  ST_NODE*        children[STREE_LEAF_MAX];
  size_t          child_counts[STREE_LEAF_MAX]; /* element count per child subtree */
} ST_INTERNAL;

/* --- Root struct --- */

typedef struct ST_ROOT {
  ST_NODE* node;
  size_t   count;
  size_t   height; /* 0 = empty, 1 = single leaf, 2+ = internal levels */
} ST_ROOT;

/* --- Get result --- */

typedef struct ST_GET_RESULT {
  STREE_T value;
  bool    found;
} ST_GET_RESULT;

/* --- Split result --- */

typedef struct ST_SPLIT_RESULT {
  ST_ROOT left;
  ST_ROOT right;
  bool    ok;
} ST_SPLIT_RESULT;

/* --- Search result --- */

typedef size_t (*ST_DIMENSION_FN)(STREE_SUMMARY_T);

typedef struct ST_SEARCH_RESULT {
  size_t          index;
  STREE_SUMMARY_T prefix_summary;
  bool            found;
} ST_SEARCH_RESULT;

/* --- Forward declaration for destructor --- */

static void ST_NODE_DESTROY(void* arg);

/* --- Node construction helpers --- */

static inline ST_LEAF* ST_MK_LEAF(const STREE_T* elems, size_t count) {
  ST_LEAF* leaf = (ST_LEAF*)ST_RC_ALLOC(sizeof(ST_LEAF), ST_NODE_DESTROY);
  leaf->header  = (ST_NODE){.type = ST_NODE_LEAF_VAL};
  leaf->count   = count;
  if (count > 0 && elems) {
    memcpy(leaf->elements, elems, sizeof(STREE_T) * count);
  }
  leaf->summary = ST_HANDLER_STATE.summarize(leaf->elements, count);
  return leaf;
}

static inline ST_INTERNAL* ST_MK_INTERNAL(ST_NODE** children, size_t n_children) {
  ST_INTERNAL* node = (ST_INTERNAL*)ST_RC_ALLOC(sizeof(ST_INTERNAL), ST_NODE_DESTROY);
  node->header      = (ST_NODE){.type = ST_NODE_INTERNAL_VAL};
  node->n_children  = n_children;
  node->count       = 0;
  node->summary     = ST_HANDLER_STATE.identity();

  for (size_t i = 0; i < n_children; i++) {
    node->children[i] = children[i];
    ST_RC_REF(children[i]);

    size_t cc;
    STREE_SUMMARY_T cs;
    if (children[i]->type == ST_NODE_LEAF_VAL) {
      ST_LEAF* cl = (ST_LEAF*)children[i];
      cc = cl->count;
      cs = cl->summary;
    } else {
      ST_INTERNAL* ci = (ST_INTERNAL*)children[i];
      cc = ci->count;
      cs = ci->summary;
    }
    node->child_counts[i] = cc;
    node->count += cc;
    node->summary = ST_HANDLER_STATE.combine(node->summary, cs);
  }

  return node;
}

/* Like MK_INTERNAL but does NOT ref children — caller transfers ownership */
static inline ST_INTERNAL* ST_MK_INTERNAL_NREF(ST_NODE** children, size_t n_children) {
  ST_INTERNAL* node = (ST_INTERNAL*)ST_RC_ALLOC(sizeof(ST_INTERNAL), ST_NODE_DESTROY);
  node->header      = (ST_NODE){.type = ST_NODE_INTERNAL_VAL};
  node->n_children  = n_children;
  node->count       = 0;
  node->summary     = ST_HANDLER_STATE.identity();

  for (size_t i = 0; i < n_children; i++) {
    node->children[i] = children[i];
    /* No RC_REF — caller transfers ownership */

    size_t cc;
    STREE_SUMMARY_T cs;
    if (children[i]->type == ST_NODE_LEAF_VAL) {
      ST_LEAF* cl = (ST_LEAF*)children[i];
      cc = cl->count;
      cs = cl->summary;
    } else {
      ST_INTERNAL* ci_node = (ST_INTERNAL*)children[i];
      cc = ci_node->count;
      cs = ci_node->summary;
    }
    node->child_counts[i] = cc;
    node->count += cc;
    node->summary = ST_HANDLER_STATE.combine(node->summary, cs);
  }

  return node;
}

/* --- Node destruction --- */

static void ST_NODE_DESTROY(void* arg) {
  ST_NODE* node = (ST_NODE*)arg;
  if (!node) return;

  if (node->type == ST_NODE_INTERNAL_VAL) {
    ST_INTERNAL* n = (ST_INTERNAL*)node;
    for (size_t i = 0; i < n->n_children; i++) {
      ST_RC_UNREF(n->children[i]);
    }
  }
  /* Leaf nodes have no children to free */
}

/* --- Helper: count elements in a node --- */

static inline size_t ST_NODE_COUNT(ST_NODE* node) {
  if (!node) return 0;
  if (node->type == ST_NODE_LEAF_VAL) return ((ST_LEAF*)node)->count;
  return ((ST_INTERNAL*)node)->count;
}

/* --- Helper: extract summary from any node --- */

static inline STREE_SUMMARY_T ST_NODE_SUMMARY(ST_NODE* node) {
  if (node->type == ST_NODE_LEAF_VAL) return ((ST_LEAF*)node)->summary;
  return ((ST_INTERNAL*)node)->summary;
}

/* --- Public API: empty --- */

static inline ST_ROOT ST_EMPTY(void) {
  return (ST_ROOT){.node = NULL, .count = 0, .height = 0};
}

/* --- Public API: ref/unref --- */

static inline ST_ROOT ST_REF(ST_ROOT root) {
  if (root.node) ST_RC_REF(root.node);
  return root;
}

static inline void ST_UNREF(ST_ROOT root) {
  if (root.node) ST_RC_UNREF(root.node);
}

/* --- Public API: count --- */

static inline size_t ST_COUNT(ST_ROOT root) {
  return root.count;
}

/* --- Public API: summary --- */

static inline STREE_SUMMARY_T ST_SUMMARY(ST_ROOT root) {
  if (!root.node) return ST_HANDLER_STATE.identity();
  if (root.node->type == ST_NODE_LEAF_VAL) return ((ST_LEAF*)root.node)->summary;
  return ((ST_INTERNAL*)root.node)->summary;
}

/* --- Public API: from_array --- */

static inline ST_ROOT ST_FROM_ARRAY(const STREE_T* elems, size_t count) {
  if (count == 0) return ST_EMPTY();

  /* Build leaves */
  size_t n_leaves = (count + STREE_LEAF_MAX - 1) / STREE_LEAF_MAX;

  /* Allocate temporary array for current level nodes */
  ST_NODE* level_buf_a[512];
  ST_NODE* level_buf_b[512];
  ST_NODE** current = level_buf_a;
  ST_NODE** next    = level_buf_b;

  /* For very large arrays, fall back to heap allocation */
  ST_NODE** heap_a = NULL;
  ST_NODE** heap_b = NULL;
  if (n_leaves > 512) {
    heap_a  = (ST_NODE**)ST_ALLOCATOR_VAR.alloc(ST_ALLOCATOR_VAR.ctx, sizeof(ST_NODE*) * n_leaves);
    heap_b  = (ST_NODE**)ST_ALLOCATOR_VAR.alloc(ST_ALLOCATOR_VAR.ctx, sizeof(ST_NODE*) * n_leaves);
    current = heap_a;
    next    = heap_b;
  }

  /* Create leaf nodes */
  size_t n_current = 0;
  for (size_t i = 0; i < count; i += STREE_LEAF_MAX) {
    size_t chunk = count - i;
    if (chunk > STREE_LEAF_MAX) chunk = STREE_LEAF_MAX;
    current[n_current++] = (ST_NODE*)ST_MK_LEAF(elems + i, chunk);
  }

  size_t height = 1;

  /* Build internal levels until single root */
  while (n_current > 1) {
    size_t n_next = 0;
    for (size_t i = 0; i < n_current; i += STREE_LEAF_MAX) {
      size_t chunk = n_current - i;
      if (chunk > STREE_LEAF_MAX) chunk = STREE_LEAF_MAX;
      ST_INTERNAL* internal = ST_MK_INTERNAL(current + i, chunk);
      /* MK_INTERNAL refs children, but we own them from creation — release our ref */
      for (size_t j = 0; j < chunk; j++) {
        ST_RC_UNREF(current[i + j]);
      }
      next[n_next++] = (ST_NODE*)internal;
    }

    /* Swap buffers */
    ST_NODE** tmp = current;
    current = next;
    next = tmp;
    n_current = n_next;
    height++;
  }

  ST_ROOT root;
  root.node   = current[0];
  root.count  = count;
  root.height = height;

  if (heap_a) {
    ST_ALLOCATOR_VAR.free(ST_ALLOCATOR_VAR.ctx, heap_a);
    ST_ALLOCATOR_VAR.free(ST_ALLOCATOR_VAR.ctx, heap_b);
  }

  return root;
}

/* --- Public API: get --- */

static inline ST_GET_RESULT ST_GET(ST_ROOT root, size_t index) {
  if (index >= root.count || !root.node) {
    ST_GET_RESULT r;
    memset(&r, 0, sizeof(r));
    r.found = false;
    return r;
  }

  ST_NODE* node = root.node;
  size_t   idx  = index;

  while (node->type == ST_NODE_INTERNAL_VAL) {
    ST_INTERNAL* n = (ST_INTERNAL*)node;
    size_t i;
    for (i = 0; i < n->n_children; i++) {
      if (idx < n->child_counts[i]) {
        node = n->children[i];
        break;
      }
      idx -= n->child_counts[i];
    }
    if (i == n->n_children) {
      ST_GET_RESULT r;
      memset(&r, 0, sizeof(r));
      r.found = false;
      return r;
    }
  }

  ST_LEAF* leaf = (ST_LEAF*)node;
  ST_GET_RESULT r;
  r.value = leaf->elements[idx];
  r.found = true;
  return r;
}

/* --- Public API: set --- */

static inline ST_ROOT ST_SET(ST_ROOT root, size_t index, STREE_T value) {
  if (index >= root.count || !root.node) {
    return (ST_ROOT){.node = NULL, .count = 0, .height = 0};
  }

  /* Path-copy from root to target leaf */
  /* We need to walk down, then rebuild up */

  /* Stack for path */
  ST_NODE* path[64];
  size_t   path_idx[64]; /* which child index at each level */
  int      depth = 0;

  ST_NODE* node = root.node;
  size_t   idx  = index;

  while (node->type == ST_NODE_INTERNAL_VAL) {
    ST_INTERNAL* n = (ST_INTERNAL*)node;
    path[depth] = node;
    for (size_t i = 0; i < n->n_children; i++) {
      if (idx < n->child_counts[i]) {
        path_idx[depth] = i;
        node = n->children[i];
        break;
      }
      idx -= n->child_counts[i];
    }
    depth++;
  }

  /* Clone the leaf and update element */
  ST_LEAF* old_leaf = (ST_LEAF*)node;
  ST_LEAF* new_leaf = (ST_LEAF*)ST_RC_ALLOC(sizeof(ST_LEAF), ST_NODE_DESTROY);
  *new_leaf = *old_leaf;
  /* Reset header (it's the rc payload, so we got a fresh rc header) */
  new_leaf->header = (ST_NODE){.type = ST_NODE_LEAF_VAL};
  new_leaf->elements[idx] = value;
  new_leaf->summary = ST_HANDLER_STATE.summarize(new_leaf->elements, new_leaf->count);

  ST_NODE* child = (ST_NODE*)new_leaf;

  /* Rebuild path upward */
  for (int d = depth - 1; d >= 0; d--) {
    ST_INTERNAL* old_int = (ST_INTERNAL*)path[d];
    ST_INTERNAL* new_int = (ST_INTERNAL*)ST_RC_ALLOC(sizeof(ST_INTERNAL), ST_NODE_DESTROY);
    new_int->header     = (ST_NODE){.type = ST_NODE_INTERNAL_VAL};
    new_int->n_children = old_int->n_children;
    new_int->count      = old_int->count;
    new_int->summary    = ST_HANDLER_STATE.identity();

    for (size_t i = 0; i < old_int->n_children; i++) {
      if (i == path_idx[d]) {
        new_int->children[i]     = child;
        new_int->child_counts[i] = old_int->child_counts[i];
      } else {
        new_int->children[i]     = old_int->children[i];
        new_int->child_counts[i] = old_int->child_counts[i];
        ST_RC_REF(old_int->children[i]);
      }

      STREE_SUMMARY_T cs;
      if (new_int->children[i]->type == ST_NODE_LEAF_VAL) {
        cs = ((ST_LEAF*)new_int->children[i])->summary;
      } else {
        cs = ((ST_INTERNAL*)new_int->children[i])->summary;
      }
      new_int->summary = ST_HANDLER_STATE.combine(new_int->summary, cs);
    }

    child = (ST_NODE*)new_int;
  }

  return (ST_ROOT){.node = child, .count = root.count, .height = root.height};
}

/* --- Internal: build root from a sequence of nodes at a given height --- */

static inline ST_ROOT ST_MK_ROOT(ST_NODE** nodes, size_t n_nodes, size_t leaf_height) {
  if (n_nodes == 0) return ST_EMPTY();

  if (n_nodes == 1) {
    ST_RC_REF(nodes[0]);
    return (ST_ROOT){
      .node   = nodes[0],
      .count  = ST_NODE_COUNT(nodes[0]),
      .height = leaf_height
    };
  }

  /* Build internal levels */
  ST_NODE* level_buf_a[512];
  ST_NODE* level_buf_b[512];
  ST_NODE** current = level_buf_a;
  ST_NODE** next_buf = level_buf_b;

  ST_NODE** heap_a = NULL;
  ST_NODE** heap_b = NULL;
  if (n_nodes > 512) {
    heap_a  = (ST_NODE**)ST_ALLOCATOR_VAR.alloc(ST_ALLOCATOR_VAR.ctx, sizeof(ST_NODE*) * n_nodes);
    heap_b  = (ST_NODE**)ST_ALLOCATOR_VAR.alloc(ST_ALLOCATOR_VAR.ctx, sizeof(ST_NODE*) * n_nodes);
    current = heap_a;
    next_buf = heap_b;
  }

  for (size_t i = 0; i < n_nodes; i++) {
    current[i] = nodes[i];
    ST_RC_REF(nodes[i]);
  }
  size_t n_current = n_nodes;
  size_t height = leaf_height;

  while (n_current > 1) {
    size_t n_next = 0;
    for (size_t i = 0; i < n_current; i += STREE_LEAF_MAX) {
      size_t chunk = n_current - i;
      if (chunk > STREE_LEAF_MAX) chunk = STREE_LEAF_MAX;
      ST_INTERNAL* internal = ST_MK_INTERNAL(current + i, chunk);
      for (size_t j = 0; j < chunk; j++) {
        ST_RC_UNREF(current[i + j]);
      }
      next_buf[n_next++] = (ST_NODE*)internal;
    }
    ST_NODE** tmp = current;
    current = next_buf;
    next_buf = tmp;
    n_current = n_next;
    height++;
  }

  size_t total = ST_NODE_COUNT(current[0]);
  ST_ROOT result = {.node = current[0], .count = total, .height = height};

  if (heap_a) {
    ST_ALLOCATOR_VAR.free(ST_ALLOCATOR_VAR.ctx, heap_a);
    ST_ALLOCATOR_VAR.free(ST_ALLOCATOR_VAR.ctx, heap_b);
  }

  return result;
}

/* --- Public API: split (O(log n) path-copy) --- */

static inline ST_SPLIT_RESULT ST_SPLIT(ST_ROOT root, size_t index) {
  ST_SPLIT_RESULT fail = {.left = ST_EMPTY(), .right = ST_EMPTY(), .ok = false};

  if (index > root.count) return fail;
  if (index == 0) {
    return (ST_SPLIT_RESULT){.left = ST_EMPTY(), .right = ST_REF(root), .ok = true};
  }
  if (index == root.count) {
    return (ST_SPLIT_RESULT){.left = ST_REF(root), .right = ST_EMPTY(), .ok = true};
  }

  /* Walk root to leaf, recording path */
  ST_NODE* path_nodes[64];
  size_t   path_ci[64];
  int      depth = 0;

  ST_NODE* node = root.node;
  size_t   idx  = index;

  while (node->type == ST_NODE_INTERNAL_VAL) {
    ST_INTERNAL* n = (ST_INTERNAL*)node;
    path_nodes[depth] = node;
    for (size_t i = 0; i < n->n_children; i++) {
      if (idx < n->child_counts[i]) {
        path_ci[depth] = i;
        node = n->children[i];
        break;
      }
      idx -= n->child_counts[i];
    }
    depth++;
  }

  /* At the leaf: idx is the intra-leaf offset */
  ST_NODE* left_sub  = NULL;
  ST_NODE* right_sub = NULL;

  if (idx == 0) {
    /* Split at leaf boundary — entire leaf goes right */
    left_sub  = NULL;
    right_sub = node;
    ST_RC_REF(right_sub);
  } else {
    ST_LEAF* leaf = (ST_LEAF*)node;
    left_sub  = (ST_NODE*)ST_MK_LEAF(leaf->elements, idx);
    right_sub = (ST_NODE*)ST_MK_LEAF(leaf->elements + idx, leaf->count - idx);
  }

  /* Walk back up, building left and right trees */
  for (int d = depth - 1; d >= 0; d--) {
    ST_INTERNAL* old = (ST_INTERNAL*)path_nodes[d];
    size_t ci = path_ci[d];

    /* Left side: shared children [0..ci-1] + left_sub (if not NULL) */
    size_t n_left = ci + (left_sub ? 1 : 0);
    ST_NODE* new_left_sub = NULL;
    if (n_left > 0) {
      ST_NODE* lchildren[STREE_LEAF_MAX];
      for (size_t i = 0; i < ci; i++) {
        lchildren[i] = old->children[i];
        ST_RC_REF(lchildren[i]);
      }
      if (left_sub) lchildren[ci] = left_sub; /* transfer */
      new_left_sub = (ST_NODE*)ST_MK_INTERNAL_NREF(lchildren, n_left);
    } else if (left_sub) {
      /* Shouldn't happen (n_left > 0 when left_sub != NULL), but be safe */
      new_left_sub = left_sub;
    }

    /* Right side: right_sub (if not NULL) + shared children [ci+1..n-1] */
    size_t n_after = old->n_children - ci - 1;
    size_t n_right = (right_sub ? 1 : 0) + n_after;
    ST_NODE* new_right_sub = NULL;
    if (n_right > 0) {
      ST_NODE* rchildren[STREE_LEAF_MAX];
      size_t rc = 0;
      if (right_sub) rchildren[rc++] = right_sub; /* transfer */
      for (size_t i = ci + 1; i < old->n_children; i++) {
        rchildren[rc] = old->children[i];
        ST_RC_REF(rchildren[rc]);
        rc++;
      }
      new_right_sub = (ST_NODE*)ST_MK_INTERNAL_NREF(rchildren, n_right);
    } else if (right_sub) {
      new_right_sub = right_sub;
    }

    left_sub  = new_left_sub;
    right_sub = new_right_sub;
  }

  /* Trim degenerate single-child wrappers */
  size_t left_height = root.height;
  while (left_sub && left_sub->type == ST_NODE_INTERNAL_VAL) {
    ST_INTERNAL* n = (ST_INTERNAL*)left_sub;
    if (n->n_children != 1) break;
    ST_NODE* child = n->children[0];
    ST_RC_REF(child);
    ST_RC_UNREF(left_sub);
    left_sub = child;
    left_height--;
  }

  size_t right_height = root.height;
  while (right_sub && right_sub->type == ST_NODE_INTERNAL_VAL) {
    ST_INTERNAL* n = (ST_INTERNAL*)right_sub;
    if (n->n_children != 1) break;
    ST_NODE* child = n->children[0];
    ST_RC_REF(child);
    ST_RC_UNREF(right_sub);
    right_sub = child;
    right_height--;
  }

  ST_ROOT left_root, right_root;

  if (left_sub) {
    left_root = (ST_ROOT){.node = left_sub, .count = index, .height = left_height};
  } else {
    left_root = ST_EMPTY();
  }

  if (right_sub) {
    right_root = (ST_ROOT){.node = right_sub, .count = root.count - index, .height = right_height};
  } else {
    right_root = ST_EMPTY();
  }

  return (ST_SPLIT_RESULT){.left = left_root, .right = right_root, .ok = true};
}

/* --- Public API: concat (O(log n) height-aware graft) --- */

static inline ST_ROOT ST_CONCAT(ST_ROOT left, ST_ROOT right) {
  if (!left.node) return ST_REF(right);
  if (!right.node) return ST_REF(left);

  size_t total_count = left.count + right.count;

  if (left.height == right.height) {
    /* Equal height */
    if (left.node->type == ST_NODE_LEAF_VAL && right.node->type == ST_NODE_LEAF_VAL) {
      ST_LEAF* ll = (ST_LEAF*)left.node;
      ST_LEAF* rl = (ST_LEAF*)right.node;
      if (ll->count + rl->count <= STREE_LEAF_MAX) {
        /* Merge into single leaf */
        STREE_T merged_elems[STREE_LEAF_MAX];
        memcpy(merged_elems, ll->elements, sizeof(STREE_T) * ll->count);
        memcpy(merged_elems + ll->count, rl->elements, sizeof(STREE_T) * rl->count);
        ST_LEAF* ml = ST_MK_LEAF(merged_elems, ll->count + rl->count);
        return (ST_ROOT){.node = (ST_NODE*)ml, .count = total_count, .height = left.height};
      }
    } else if (left.node->type == ST_NODE_INTERNAL_VAL && right.node->type == ST_NODE_INTERNAL_VAL) {
      ST_INTERNAL* li = (ST_INTERNAL*)left.node;
      ST_INTERNAL* ri = (ST_INTERNAL*)right.node;
      if (li->n_children + ri->n_children <= STREE_LEAF_MAX) {
        /* Merge children into one internal node */
        ST_NODE* children[STREE_LEAF_MAX];
        for (size_t i = 0; i < li->n_children; i++) children[i] = li->children[i];
        for (size_t i = 0; i < ri->n_children; i++) children[li->n_children + i] = ri->children[i];
        ST_INTERNAL* mi = ST_MK_INTERNAL(children, li->n_children + ri->n_children);
        return (ST_ROOT){.node = (ST_NODE*)mi, .count = total_count, .height = left.height};
      }
    }
    /* Can't merge — create 2-child root */
    ST_NODE* pair[2] = {left.node, right.node};
    ST_INTERNAL* new_root = ST_MK_INTERNAL(pair, 2);
    return (ST_ROOT){.node = (ST_NODE*)new_root, .count = total_count, .height = left.height + 1};
  }

  if (left.height > right.height) {
    /* Graft right: walk L's right spine down (left.height - right.height) levels */
    size_t delta = left.height - right.height;
    ST_NODE* spine[64];

    spine[0] = left.node;
    for (size_t i = 0; i < delta; i++) {
      ST_INTERNAL* n = (ST_INTERNAL*)spine[i];
      spine[i + 1] = n->children[n->n_children - 1];
    }

    ST_NODE* bottom = spine[delta];
    ST_NODE* merged_a = NULL;
    ST_NODE* overflow = NULL;

    /* Try to merge bottom with right.node (same height) */
    if (bottom->type == ST_NODE_LEAF_VAL && right.node->type == ST_NODE_LEAF_VAL) {
      ST_LEAF* bl = (ST_LEAF*)bottom;
      ST_LEAF* rl = (ST_LEAF*)right.node;
      if (bl->count + rl->count <= STREE_LEAF_MAX) {
        STREE_T merged_elems[STREE_LEAF_MAX];
        memcpy(merged_elems, bl->elements, sizeof(STREE_T) * bl->count);
        memcpy(merged_elems + bl->count, rl->elements, sizeof(STREE_T) * rl->count);
        merged_a = (ST_NODE*)ST_MK_LEAF(merged_elems, bl->count + rl->count);
      }
    } else if (bottom->type == ST_NODE_INTERNAL_VAL && right.node->type == ST_NODE_INTERNAL_VAL) {
      ST_INTERNAL* bi = (ST_INTERNAL*)bottom;
      ST_INTERNAL* ri = (ST_INTERNAL*)right.node;
      if (bi->n_children + ri->n_children <= STREE_LEAF_MAX) {
        ST_NODE* children[STREE_LEAF_MAX];
        for (size_t i = 0; i < bi->n_children; i++) children[i] = bi->children[i];
        for (size_t i = 0; i < ri->n_children; i++) children[bi->n_children + i] = ri->children[i];
        merged_a = (ST_NODE*)ST_MK_INTERNAL(children, bi->n_children + ri->n_children);
      }
    }

    if (!merged_a) {
      /* Can't merge — keep both as siblings */
      merged_a = bottom;
      ST_RC_REF(merged_a);
      overflow = right.node;
      ST_RC_REF(overflow);
    }

    /* Walk back up the right spine */
    for (int k = (int)delta - 1; k >= 0; k--) {
      ST_INTERNAL* old = (ST_INTERNAL*)spine[k];
      size_t ri = old->n_children - 1;

      ST_NODE* new_children[STREE_LEAF_MAX + 1];
      size_t nc = 0;

      /* Shared children [0..ri-1] */
      for (size_t i = 0; i < ri; i++) {
        new_children[nc] = old->children[i];
        ST_RC_REF(new_children[nc]);
        nc++;
      }

      /* Merged child replaces rightmost */
      new_children[nc++] = merged_a; /* transfer */

      /* Overflow (if any) */
      if (overflow) {
        new_children[nc++] = overflow; /* transfer */
        overflow = NULL;
      }

      if (nc <= STREE_LEAF_MAX) {
        merged_a = (ST_NODE*)ST_MK_INTERNAL_NREF(new_children, nc);
      } else {
        size_t mid = nc / 2;
        merged_a = (ST_NODE*)ST_MK_INTERNAL_NREF(new_children, mid);
        overflow = (ST_NODE*)ST_MK_INTERNAL_NREF(new_children + mid, nc - mid);
      }
    }

    if (overflow) {
      ST_NODE* root_children[2] = {merged_a, overflow};
      ST_INTERNAL* nr = ST_MK_INTERNAL_NREF(root_children, 2);
      return (ST_ROOT){.node = (ST_NODE*)nr, .count = total_count, .height = left.height + 1};
    }
    return (ST_ROOT){.node = merged_a, .count = total_count, .height = left.height};
  }

  /* right.height > left.height: graft left — walk R's left spine */
  {
    size_t delta = right.height - left.height;
    ST_NODE* spine[64];

    spine[0] = right.node;
    for (size_t i = 0; i < delta; i++) {
      ST_INTERNAL* n = (ST_INTERNAL*)spine[i];
      spine[i + 1] = n->children[0];
    }

    ST_NODE* bottom = spine[delta];
    ST_NODE* merged_a = NULL;
    ST_NODE* overflow = NULL;

    /* Try to merge left.node with bottom (same height) */
    if (left.node->type == ST_NODE_LEAF_VAL && bottom->type == ST_NODE_LEAF_VAL) {
      ST_LEAF* ll = (ST_LEAF*)left.node;
      ST_LEAF* bl = (ST_LEAF*)bottom;
      if (ll->count + bl->count <= STREE_LEAF_MAX) {
        STREE_T merged_elems[STREE_LEAF_MAX];
        memcpy(merged_elems, ll->elements, sizeof(STREE_T) * ll->count);
        memcpy(merged_elems + ll->count, bl->elements, sizeof(STREE_T) * bl->count);
        merged_a = (ST_NODE*)ST_MK_LEAF(merged_elems, ll->count + bl->count);
      }
    } else if (left.node->type == ST_NODE_INTERNAL_VAL && bottom->type == ST_NODE_INTERNAL_VAL) {
      ST_INTERNAL* li = (ST_INTERNAL*)left.node;
      ST_INTERNAL* bi = (ST_INTERNAL*)bottom;
      if (li->n_children + bi->n_children <= STREE_LEAF_MAX) {
        ST_NODE* children[STREE_LEAF_MAX];
        for (size_t i = 0; i < li->n_children; i++) children[i] = li->children[i];
        for (size_t i = 0; i < bi->n_children; i++) children[li->n_children + i] = bi->children[i];
        merged_a = (ST_NODE*)ST_MK_INTERNAL(children, li->n_children + bi->n_children);
      }
    }

    if (!merged_a) {
      /* Can't merge — keep both as siblings */
      overflow = left.node;
      ST_RC_REF(overflow);
      merged_a = bottom;
      ST_RC_REF(merged_a);
    }

    /* Walk back up the left spine */
    for (int k = (int)delta - 1; k >= 0; k--) {
      ST_INTERNAL* old = (ST_INTERNAL*)spine[k];

      ST_NODE* new_children[STREE_LEAF_MAX + 1];
      size_t nc = 0;

      /* Overflow goes first (leftmost) */
      if (overflow) {
        new_children[nc++] = overflow; /* transfer */
        overflow = NULL;
      }

      /* Merged child replaces leftmost */
      new_children[nc++] = merged_a; /* transfer */

      /* Shared children [1..n-1] */
      for (size_t i = 1; i < old->n_children; i++) {
        new_children[nc] = old->children[i];
        ST_RC_REF(new_children[nc]);
        nc++;
      }

      if (nc <= STREE_LEAF_MAX) {
        merged_a = (ST_NODE*)ST_MK_INTERNAL_NREF(new_children, nc);
        overflow = NULL;
      } else {
        size_t mid = nc / 2;
        overflow = (ST_NODE*)ST_MK_INTERNAL_NREF(new_children, mid);
        merged_a = (ST_NODE*)ST_MK_INTERNAL_NREF(new_children + mid, nc - mid);
      }
    }

    if (overflow) {
      ST_NODE* root_children[2] = {overflow, merged_a};
      ST_INTERNAL* nr = ST_MK_INTERNAL_NREF(root_children, 2);
      return (ST_ROOT){.node = (ST_NODE*)nr, .count = total_count, .height = right.height + 1};
    }
    return (ST_ROOT){.node = merged_a, .count = total_count, .height = right.height};
  }
}

/* --- Public API: push_back --- */

static inline ST_ROOT ST_PUSH_BACK(ST_ROOT root, STREE_T value) {
  ST_ROOT single = ST_FROM_ARRAY(&value, 1);
  ST_ROOT result = ST_CONCAT(root, single);
  ST_UNREF(single);
  return result;
}

/* --- Public API: push_front --- */

static inline ST_ROOT ST_PUSH_FRONT(ST_ROOT root, STREE_T value) {
  ST_ROOT single = ST_FROM_ARRAY(&value, 1);
  ST_ROOT result = ST_CONCAT(single, root);
  ST_UNREF(single);
  return result;
}

/* --- Public API: search --- */

static inline ST_SEARCH_RESULT ST_SEARCH(ST_ROOT root, ST_DIMENSION_FN dimension_fn, size_t target) {
  ST_SEARCH_RESULT fail;
  memset(&fail, 0, sizeof(fail));
  fail.found = false;
  fail.prefix_summary = ST_HANDLER_STATE.identity();

  if (!root.node) return fail;

  /* Check if target exceeds total dimension */
  STREE_SUMMARY_T total_summary = ST_SUMMARY(root);
  if (target >= dimension_fn(total_summary)) return fail;

  ST_NODE* node = root.node;
  STREE_SUMMARY_T accumulated = ST_HANDLER_STATE.identity();
  size_t index_offset = 0;

  /* Navigate internal nodes */
  while (node->type == ST_NODE_INTERNAL_VAL) {
    ST_INTERNAL* n = (ST_INTERNAL*)node;
    size_t i;
    for (i = 0; i < n->n_children; i++) {
      STREE_SUMMARY_T child_summary;
      if (n->children[i]->type == ST_NODE_LEAF_VAL) {
        child_summary = ((ST_LEAF*)n->children[i])->summary;
      } else {
        child_summary = ((ST_INTERNAL*)n->children[i])->summary;
      }
      size_t child_dim = dimension_fn(child_summary);
      size_t acc_dim = dimension_fn(accumulated);
      if (acc_dim + child_dim > target) {
        node = n->children[i];
        break;
      }
      accumulated = ST_HANDLER_STATE.combine(accumulated, child_summary);
      index_offset += n->child_counts[i];
    }
    if (i == n->n_children) return fail; /* shouldn't happen */
  }

  /* Within the target leaf, find the element */
  ST_LEAF* leaf = (ST_LEAF*)node;

  if (ST_HANDLER_STATE.leaf_search) {
    /* Custom leaf search provided by instantiation */
    size_t leaf_idx = ST_HANDLER_STATE.leaf_search(
        leaf->elements, leaf->count, &accumulated, dimension_fn, target);
    if (leaf_idx < leaf->count) {
      ST_SEARCH_RESULT result;
      result.index = index_offset + leaf_idx;
      result.prefix_summary = accumulated;
      result.found = true;
      return result;
    }
  } else {
    /* Default element-by-element scan */
    for (size_t i = 0; i < leaf->count; i++) {
      STREE_SUMMARY_T elem_summary = ST_HANDLER_STATE.summarize(&leaf->elements[i], 1);
      size_t elem_dim = dimension_fn(elem_summary);
      size_t acc_dim = dimension_fn(accumulated);
      if (acc_dim + elem_dim > target) {
        ST_SEARCH_RESULT result;
        result.index = index_offset + i;
        result.prefix_summary = accumulated;
        result.found = true;
        return result;
      }
      accumulated = ST_HANDLER_STATE.combine(accumulated, elem_summary);
      (void)elem_dim; /* suppress unused warning after break */
    }
  }

  return fail; /* shouldn't happen if total check passed */
}

/* --- Public API: copy_range --- */

static inline size_t ST_COPY_RANGE(ST_ROOT root, size_t start_index, size_t count, STREE_T* out_buf) {
  if (!root.node || start_index >= root.count || count == 0) return 0;

  /* Clamp count to available elements */
  if (start_index + count > root.count) count = root.count - start_index;

  size_t copied = 0;
  size_t remaining = count;

  /* Walk to starting leaf */
  ST_NODE* node = root.node;
  size_t idx = start_index;

  /* Find the starting leaf using a stack so we can iterate siblings */
  ST_NODE* path_nodes[64];
  size_t   path_child[64];
  int      depth = 0;

  while (node->type == ST_NODE_INTERNAL_VAL) {
    ST_INTERNAL* n = (ST_INTERNAL*)node;
    path_nodes[depth] = node;
    for (size_t i = 0; i < n->n_children; i++) {
      if (idx < n->child_counts[i]) {
        path_child[depth] = i;
        node = n->children[i];
        break;
      }
      idx -= n->child_counts[i];
    }
    depth++;
  }

  /* Copy from the starting leaf */
  ST_LEAF* leaf = (ST_LEAF*)node;
  size_t leaf_avail = leaf->count - idx;
  size_t to_copy = remaining < leaf_avail ? remaining : leaf_avail;
  memcpy(out_buf + copied, leaf->elements + idx, sizeof(STREE_T) * to_copy);
  copied += to_copy;
  remaining -= to_copy;

  /* Continue to subsequent leaves via tree traversal */
  while (remaining > 0) {
    /* Move to next leaf by walking up then down */
    bool found_next = false;
    while (depth > 0) {
      depth--;
      ST_INTERNAL* parent = (ST_INTERNAL*)path_nodes[depth];
      size_t next_child = path_child[depth] + 1;
      if (next_child < parent->n_children) {
        path_child[depth] = next_child;
        node = parent->children[next_child];
        depth++;
        /* Descend to leftmost leaf */
        while (node->type == ST_NODE_INTERNAL_VAL) {
          ST_INTERNAL* n = (ST_INTERNAL*)node;
          path_nodes[depth] = node;
          path_child[depth] = 0;
          node = n->children[0];
          depth++;
        }
        found_next = true;
        break;
      }
    }
    if (!found_next) break;

    leaf = (ST_LEAF*)node;
    to_copy = remaining < leaf->count ? remaining : leaf->count;
    memcpy(out_buf + copied, leaf->elements, sizeof(STREE_T) * to_copy);
    copied += to_copy;
    remaining -= to_copy;
  }

  return copied;
}

/* --- Cleanup all internal macros --- */

#undef STREE_T
#undef STREE_SUMMARY_T
#undef STREE_NAME

#undef ST_XCAT
#undef ST_CAT
#undef ST_NS

#undef ST_NODE_TYPE
#undef ST_NODE
#undef ST_INTERNAL
#undef ST_LEAF
#undef ST_ROOT
#undef ST_HANDLERS
#undef ST_GET_RESULT
#undef ST_SPLIT_RESULT
#undef ST_SEARCH_RESULT
#undef ST_DIMENSION_FN

#undef ST_NODE_INTERNAL_VAL
#undef ST_NODE_LEAF_VAL

#undef ST_SET_HANDLERS
#undef ST_EMPTY
#undef ST_REF
#undef ST_UNREF
#undef ST_COUNT
#undef ST_SUMMARY
#undef ST_FROM_ARRAY
#undef ST_GET
#undef ST_SET
#undef ST_SPLIT
#undef ST_CONCAT
#undef ST_PUSH_BACK
#undef ST_PUSH_FRONT
#undef ST_SEARCH
#undef ST_COPY_RANGE

#undef ST_NODE_DESTROY
#undef ST_ROOT_DESTROY
#undef ST_MK_LEAF
#undef ST_MK_INTERNAL
#undef ST_MK_ROOT
#undef ST_RECOMPUTE_SUMMARY
#undef ST_LEAF_SUMMARY
#undef ST_INTERNAL_SUMMARY
#undef ST_NODE_COUNT
#undef ST_NODE_SUMMARY
#undef ST_MK_INTERNAL_NREF

#undef ST_IDENTITY_FN
#undef ST_COMBINE_FN
#undef ST_SUMMARIZE_FN
#undef ST_LEAF_SEARCH_FN

#undef ST_HANDLER_STATE
#undef ST_ALLOCATOR_VAR

#undef ST_RC_ALLOC
#undef ST_RC_REF
#undef ST_RC_UNREF
#undef ST_RC_COUNT

#undef ST_LEAF_MIN

#ifdef STREE_LEAF_MAX_DEFAULTED
#undef STREE_LEAF_MAX
#undef STREE_LEAF_MAX_DEFAULTED
#endif

#ifdef STREE_ALLOC_DEFAULTED
#undef STREE_ALLOCATOR
#undef STREE_ALLOC_DEFAULTED
#endif
