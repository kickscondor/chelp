//
// slotmap64.h
//
// This is a larger slotmap, designed to use 64-bit IDs. This allows for more
// storage space and fewer version number conflicts.
//
// LICENSE
//
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
#ifndef SLOTMAP64_H
#define SLOTMAP64_H

#include "slotbase.h"

typedef struct {
  uint32_t index;
  uint32_t version;
} SLOTMAP64_ID;

typedef struct {
  uint32_t version;
  uint32_t next_free;
} SLOTMAP64_FREE;

// The maximum element ID for a slot map.
#define SLOTMAP64_MAX_ID        UINT32_MAX

#define SLOTMAP64_NONE_ID       slotmap64__id(UINT32_MAX, UINT32_MAX)

// Free an entire slot map 'a' from memory. This doesn't just free the slot map metadata -
// everything is freed. Elements are part of the contiguous block of the slot map.
// Returns: NULL.
#define slotmap64_free(a)       ((a) ? free(a),0 : 0)

// A count of how many entries in the slot map 'a' have been used in the allocation block.
// Some of these may be freed already, however.
// Returns: A uint32_t.
#define slotmap64_used(a)       ((a) ? slotmap64__use(a) : 0)

// A count of how many active entries there are with valid data in the slot map 'a'.
// Returns: A uint32_t.
#define slotmap64_count(a)      ((a) ? slotmap64__use(a) - slotmap64__frc(a) : 0)

// A count of how many entries the slot map 'a' already has allocated space for.
// Returns: A uint32_t.
#define slotmap64_allocated(a)  ((a) ? slotmap64__siz(a) : 0)

// Add a new entry in the slot map 'a' and set SLOTMAP64_ID variable 'id' to the ID of the new entry.
// Returns: A pointer to the new item or NULL if the slot map has reached its maximum.
#define slotmap64_add(a,id)     slotmap64__new(a,id,{})

// Copy an entry 'o' into a new slot in slot map 'a', setting 'id' to the ID of the new entry.
// Returns: A pointer to the new item or NULL if the slot map has reached its maximum.
#define slotmap64_copy(a,o,id)  slotmap64__new(a,id, { *item = *((__typeof__(a))o); })

// Determine an element's ID by supplying the slot map 'a' that contains it and a pointer 'o'
// to the element itself.
// Returns: A SLOTMAP64_ID.
#define slotmap64_id(a,o)       slotmap64__id((o)-slotmap64_array(a), (o)->version)

// Get a pointer to an element by supplying the slot map 'a' that contains it and its SLOTMAP64_ID 'id'.
// Returns: A pointer to the element or NULL if the element is not found.
#define slotmap64_at(a,id)     (!a ? 0 : ({ \
  __typeof__(a) item = NULL; \
  if ((id).index < slotmap64__use(a)) { \
    item = slotmap64_array(a) + (id).index; \
    item = (item->version != (id).version ? NULL : item); \
  } \
  item; \
}))

// Removes an element with SLOTMAP64_ID 'id' from the slot map 'a'.
// Returns: A pointer to the element or NULL if the element is not found. The pointer is only
// provided for final access to the element - please do not store the pointer, it is useless
// to any subsequent calls.
#define slotmap64_remove(a,id)  (!a ? 0 : ({ \
  __typeof__(a) item = slotmap64_at(a,id); \
  if (item) { \
    *((SLOTMAP64_FREE *)item) = (SLOTMAP64_FREE){item->version + 1, slotmap64__frl(a)}; \
    slotmap64__frl(a) = (id).index; \
    slotmap64__frc(c)++; \
  } \
  item; \
}))

// Fetch the beginning of the actual items.
#define slotmap64_array(a)      ((__typeof__(a))(((uint32_t *)(a)) + 4))

//
// internal macros
//
#define slotmap64__id(index,v)  ((SLOTMAP64_ID){index, v})
#define slotmap64__siz(a)       ((uint32_t *)(a))[0]
#define slotmap64__use(a)       ((uint32_t *)(a))[1]
#define slotmap64__frl(a)       ((uint32_t *)(a))[2]
#define slotmap64__frc(a)       ((uint32_t *)(a))[3]

#define slotmap64__new(a,id,blk)     ({ \
  __typeof__(a) item = (__typeof__(a))slotmap64__make((uint8_t **)&a, sizeof(*(a)), &id); \
  if (item) { \
    blk; \
    item->version = (id).version; \
  } \
  item; \
})

#include <string.h>

//
// Makes room for a new element.
// Returns: A pointer to the new object or NULL if no further objects could be created.
//
static inline uint8_t *
slotmap64__make(uint8_t **ary, size_t itemsize, SLOTMAP64_ID *idp)
{
  uint8_t *arr = *ary;
  uint32_t *p;
  uint32_t x;
  size_t used = 0, siz = 0, newsiz = 0;

  //
  // Reuse from the freelist.
  //
  if (arr) {
    x = slotmap64__frl(arr);
    if (x != UINT32_MAX) {
      SLOTMAP64_FREE *free_item = (SLOTMAP64_FREE *)(slotmap64_array(arr) + (x * itemsize));
      *idp = slotmap64__id(x, free_item->version);
      slotmap64__frl(arr) = free_item->next_free;
      return (uint8_t *)p;
    } else {
      siz = slotmap64__siz(arr);
      used = slotmap64__use(arr);
    }
  }

  //
  // Allocate additional space
  //
  newsiz = SLOT_ALIGN(
    (SLOT_FLEX_SIZE(siz) * itemsize) +
    (sizeof(uint32_t) * 4), SLOT_ALIGN_SIZE);
  if (used == siz) {
    p = (uint32_t *)SLOT_REALLOC(arr, newsiz);
    x = (newsiz - (sizeof(uint32_t) * 4)) / itemsize;
    *ary = (uint8_t *)p;
    p[0] = x;
  } else {
    p = (uint32_t *)arr;
  }

  //
  // Expand the array by one element and give back an ID.
  //
  if (p) {
    if (!arr) {
      p[1] = p[3] = 0;
      p[2] = UINT32_MAX;
    }
    *idp = slotmap64__id(x = p[1]++, 0);
    arr = *ary;
    return slotmap64_array(arr) + (x * itemsize);
  }

  *idp = SLOTMAP64_NONE_ID;
  return NULL;
}

#endif
