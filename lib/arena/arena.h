/*
 * ---------------------------------------------------------------------------
 * Arena Allocator
 * ---------------------------------------------------------------------------
 * High-performance bulk memory management with chained regions,
 * pointer-aligned allocations, and zero-initialization support.
 *
 * An arena manages one or more memory regions chained together,
 * allowing fast bulk allocation and efficient cleanup.
 *
 * Usage: Declare an arena on the stack with lazy initialization:
 * ```
 * arena_t arena = {0};                    // Uses libc allocator by default
 * void* ptr = arena_alloc(&arena, size);  // Initializes on first alloc
 * arena_reset(&arena);                    // Free all regions, reset for reuse
 * arena_destroy(&arena);                  // Final cleanup (reset + zero)
 * ```
 *
 * With a custom allocator:
 * ```
 * arena_t arena = { .allocator = my_allocator };
 * void* ptr = arena_alloc(&arena, size);
 * arena_destroy(&arena);
 * ```
 */

#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

#include "../platform/platform.h"

/* Default region size and alignment (override by defining before include) */
#ifndef ARENA_DEFAULT_REGION_SIZE
#define ARENA_DEFAULT_REGION_SIZE 4096
#endif
#ifndef ARENA_DEFAULT_ALIGNMENT
#define ARENA_DEFAULT_ALIGNMENT (sizeof(void*))
#endif

/**
 * Region structure - represents a contiguous block of memory within the arena.
 * Uses a flexible array member for inline memory storage.
 */
typedef struct ArenaRegion {
  size_t              size;     /* Total usable size of this region */
  size_t              offset;   /* Current allocation offset within region */
  struct ArenaRegion* next;     /* Next region in chain (NULL if last) */
  char                memory[]; /* Flexible array member - data follows inline */
} ArenaRegion;

/**
 * Arena structure - manages a chain of regions.
 * Can be zero-initialized; lazy-initializes on first allocation.
 */
typedef struct Arena {
  ArenaRegion* head;        /* Most recent region for allocations */
  size_t       region_size; /* Default region size for new allocations */
  size_t       alignment;   /* Required alignment (must be power of 2) */
  Allocator    allocator;   /* Memory allocator for region management */
} arena_t;

#endif /* ARENA_H */

/* =========================================================================
 * Implementation Section
 * Define ARENA_IMPLEMENTATION before including to generate function bodies.
 * ========================================================================= */

#ifdef ARENA_IMPLEMENTATION
#ifndef ARENA_IMPL_GUARD_
#define ARENA_IMPL_GUARD_

#include <assert.h>
#include <stdint.h>
#include <string.h>

/**
 * Helper: Align an offset to the required alignment (must be power of 2).
 */
static size_t arena__align(size_t offset, size_t alignment) {
  return (offset + alignment - 1) & ~(alignment - 1);
}

/**
 * Internal: Initialize the first region in an arena.
 * Returns 1 on success, 0 on failure.
 */
static int arena__init_region(arena_t* arena, size_t region_size, size_t alignment) {
  assert(arena != NULL);
  assert(region_size > 0);
  assert(alignment > 0 && (alignment & (alignment - 1)) == 0);

  size_t total_size = sizeof(ArenaRegion) + region_size;
  if (total_size < region_size) return 0; /* overflow */

  ArenaRegion* first = (ArenaRegion*)arena->allocator.alloc(arena->allocator.ctx, total_size);
  if (!first) return 0;

  first->size   = region_size;
  first->offset = 0;
  first->next   = NULL;

  arena->head        = first;
  arena->region_size = region_size;
  arena->alignment   = alignment;

  return 1;
}

/**
 * Allocate memory from the arena.
 * Lazy-initializes on first call if arena was zero-initialized.
 * Uses libc allocator if no allocator was set.
 *
 * @param arena Arena pointer (must not be NULL). Can be zero-initialized.
 * @param size  Number of bytes to allocate.
 * @return Pointer-aligned, zero-initialized address, or NULL on failure/size==0.
 */
static void* arena_alloc(arena_t* arena, size_t size) {
  assert(arena != NULL);

  if (size == 0) return NULL;

  /* Lazy initialization */
  if (arena->head == NULL) {
    if (arena->allocator.alloc == NULL) {
      arena->allocator = libc_allocator;
    }
    if (!arena__init_region(arena, ARENA_DEFAULT_REGION_SIZE, ARENA_DEFAULT_ALIGNMENT)) {
      return NULL;
    }
  }

  ArenaRegion* region = arena->head;
  size_t aligned_offset = arena__align(region->offset, arena->alignment);

  /* Overflow check */
  if (aligned_offset > SIZE_MAX - size) return NULL;

  /* Fits in current region */
  if (aligned_offset + size <= region->size) {
    void* ptr      = (void*)(region->memory + aligned_offset);
    region->offset = aligned_offset + size;
    memset(ptr, 0, size);
    return ptr;
  }

  /* Need a new region */
  size_t new_region_size = arena->region_size;
  if (size > new_region_size) {
    new_region_size = size;
  }

  size_t total_size = sizeof(ArenaRegion) + new_region_size;
  if (total_size < new_region_size) return NULL; /* overflow */

  ArenaRegion* new_region = (ArenaRegion*)arena->allocator.alloc(arena->allocator.ctx, total_size);
  if (!new_region) return NULL;

  new_region->size   = new_region_size;
  new_region->offset = size;
  new_region->next   = arena->head;
  arena->head = new_region;

  /* Verify FAM alignment (guaranteed by struct layout, asserted for safety) */
  assert(((uintptr_t)new_region->memory) % arena->alignment == 0);

  void* ptr = (void*)new_region->memory;
  memset(ptr, 0, size);
  return ptr;
}

/**
 * Reset the arena, freeing all regions.
 * The arena retains its allocator and can be reused for new allocations.
 */
static void arena_reset(arena_t* arena) {
  assert(arena != NULL);

  ArenaRegion* region = arena->head;

  while (region != NULL) {
    ArenaRegion* next = region->next;
    arena->allocator.free(arena->allocator.ctx, region);
    region = next;
  }

  arena->head = NULL;
}

/**
 * Destroy the arena, freeing all regions and zeroing the struct.
 * Safe to call on a zero-initialized or already-destroyed arena.
 */
static void arena_destroy(arena_t* arena) {
  if (arena == NULL) return;
  if (arena->head != NULL) {
    arena_reset(arena);
  }
  *arena = (arena_t){0};
}

#endif /* ARENA_IMPL_GUARD_ */
#endif /* ARENA_IMPLEMENTATION */
