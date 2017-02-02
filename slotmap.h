//
// slotmap.h
//
// A slot map type, based on Sean Barrett's stretchy_buffer, except with a fixed-size freelist, to
// reuse slots.
//
// A WARNING
//
// If you're going to redefine the base constants, don't use the macros to access different types
// of slot maps in the same file. Setup functions to wrap the macros for that slot map.
// 
// INTERNALS
//
// So basically, the layout looks like this:
//
// | freelist (alloc * u32) | alloc | len | freelist_len | ... actual items ... |
//
// You can't 'push' on to the slot map. Everything is kept unordered.
// (So you'll need to used linked-list strategies or an external list to order this.)
//
// Another major limitation of this setup is that, since I use 24-bits for the index, you
// can only store 16M objects in this thing.
//
// So, what's the point?
//
// * The ability to have psuedo-pointers (IDs) to all objects in the list - which won't change
//   with realloc of the entire pool.
// * It's basically another memory pool / arena strategy, but remains contiguous. Good for
//   vertex arrays. Good for the CPU cache.
// * Keep memory low? I don't know - there's a 4 byte overhead on each entry - so maybe this
//   is bogus.
//
// LICENSE
//
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
#ifndef SLOTMAP_H
#define SLOTMAP_H

#include "slotbase.h"

// The maximum element ID for a slot map. This is a hard limit of 16 million.
#define SLOTMAP_MAX_ID   0xFFFFFF

// The 'none' ID.
#define SLOTMAP_NONE_ID  UINT32_MAX

// Gets the 0-based array index of an element in the slot map by its 'id'. (You cannot loop through
// a slot map in this order, though. There will be holes with invalid data.)
// Returns: A SLOT_ID.
#define slotmap_at(id)        ((id) & SLOTMAP_MAX_ID)

// Free an entire slot map 'a' from memory. This doesn't just free the slot map metadata -
// everything is freed. Elements are part of the contiguous block of the slot map.
// Returns: NULL.
#define slotmap_free(a)       ((a) ? free(slotmap__head(a)),0 : 0)

// A count of how many entries in the slot map 'a' have been used in the allocation block.
// Some of these may be freed already, however.
// Returns: A uint32_t.
#define slotmap_used(a)       ((a) ? slotmap__use(a) : 0)

// A count of how many active entries there are with valid data in the slot map 'a'.
// Returns: A uint32_t.
#define slotmap_count(a)      ((a) ? slotmap__use(a) - slotmap__frl(a) : 0)

// Add a new entry in the slot map 'a' and set SLOT_ID variable 'id' to the ID of the new entry.
// Returns: A pointer to the new item or NULL if the slot map has reached its maximum.
#define slotmap_add(a,id)     ({ \
  __typeof__(a) item = (__typeof__(a))slotmap__make((uint8_t **)&a, sizeof(*(a)), &id); \
  if (item) { item->version = (id >> 24); } \
  item; \
})

// Determine an element's ID by supplying the slot map 'a' that contains it and a pointer 'o'
// to the element itself.
// Returns: A SLOT_ID.
#define slotmap_id(a,o)       slotmap__id((o)-(a), (o)->version)

// Get a pointer to an element by supplying the slot map 'a' that contains it and its SLOT_ID 'id'.
// Returns: A pointer to the element or NULL if the element is not found.
#define slotmap_get(a,id)     (!a ? 0 : ({ \
  SLOT_ID __id__ = slotmap_at(id); \
  __typeof__(a) item = NULL; \
  if (__id__ < slotmap__use(a)) { \
    item = a + slotmap_at(id); \
    item = (item->version != (id >> 24) ? NULL : item); \
  } \
  item; \
}))

// Removes an element with SLOT_ID 'id' from the slot map 'a'.
// Returns: A pointer to the element or NULL if the element is not found. The pointer is only
// provided for final access to the element - please do not store the pointer, it is useless
// to any subsequent calls.
#define slotmap_remove(a,id)  (!a ? 0 : ({ \
  __typeof__(a) item = slotmap_get(a,id); \
  if (item) { \
    item->version++; \
    *(slotmap__head(a) + (slotmap__frl(a)++)) = slotmap__id(id, item->version); \
  } \
  item; \
}))

// Access extra fields reserved for the user.
#define slotmap_ext(a)        ((SLOT_ID *) (a) - SLOT_EXT_SIZE)

//
// internal macros
//
#define slotmap__id(index,v)  (slotmap_at(index) | ((SLOT_ID)(v) << 24))
#define slotmap__head(a)      (slotmap__meta(a) - slotmap__siz(a))
#define slotmap__meta(a)      ((SLOT_ID *) (a) - (3 + SLOT_EXT_SIZE))
#define slotmap__siz(a)       slotmap__meta(a)[0]
#define slotmap__use(a)       slotmap__meta(a)[1]
#define slotmap__frl(a)       slotmap__meta(a)[2]

#include <string.h>

//
// Makes room for a new element.
// Returns: A pointer to the new object or NULL if no further objects could be created.
// 
static uint8_t *
slotmap__make(uint8_t **ary, size_t itemsize, SLOT_ID *idp)
{
  uint8_t *arr = *ary;
  SLOT_ID *p;
  SLOT_ID x;
  size_t used = 0, siz = 0, newsiz = 0;

  //
  // Reuse from the freelist.
  //
  if (arr) {
    x = slotmap__frl(arr);
    if (x) {
      *idp = x = *(slotmap__head(arr) + (--slotmap__frl(arr)));
      return arr + (slotmap_at(x) * itemsize);
    } else {
      siz = slotmap__siz(arr);
      used = slotmap__use(arr);
    }
  }

  //
  // Allocate additional space
  // 
  newsiz = SLOT_ALIGN(
    (SLOT_FLEX_SIZE(siz) * (itemsize + sizeof(SLOT_ID))) +
    (sizeof(SLOT_ID) * (3 + SLOT_EXT_SIZE)), SLOT_ALIGN_SIZE);
  if (used == siz) {
    p = (SLOT_ID *)realloc(arr ? slotmap__head(arr) : 0, newsiz);
    x = (newsiz - (sizeof(SLOT_ID) * (3 + SLOT_EXT_SIZE))) / (itemsize + sizeof(SLOT_ID));
    memmove(p + x, p + siz, (used * itemsize) + (sizeof(SLOT_ID) * 3));

    p += x;
    *ary = (uint8_t *)(p + 3 + SLOT_EXT_SIZE);
    p[0] = x;
  } else {
    p = (SLOT_ID *)slotmap__meta(arr);
  }

  //
  // Expand the array by one element and give back an ID.
  // 
  if (p) {
    if (!arr) {
      p[1] = p[2] = 0;
    }
    *idp = x = p[1]++;
    return *ary + (x * itemsize);
  }

  *idp = SLOTMAP_NONE_ID;
  return NULL;
}

#endif
