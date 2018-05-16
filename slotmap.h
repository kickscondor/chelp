//
// slotmap.h
//
// A slot map type, based on Sean Barrett's stretchy_buffer, except with a fixed-size freelist, to
// reuse slots. And the idea from that came from Sean Middleditch's blog on slotmaps.
// http://seanmiddleditch.com/data-structures-for-game-developers-the-slot-map/
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
//   uint32_t allocated_entries
//   uint32_t filled_entries
//   uint32_t next_free_entry
//   uint32_t total_free_entries
//   user_struct[allocated_entries] items
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
// * Keep memory low? I don't know - there's a 1 byte overhead on each entry.
//
// TODO: Size down the slotmap. What I want to do with this is to make the freelist
// only as big as the difference between the last allocation size and the new
// allocation size. And once the freelist fills completely, then I'll size the
// entire list down.
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

typedef struct {
  uint32_t version   : 8;
  uint32_t next_free : 24;
} SLOTMAP_FREE;

// The maximum element ID for a slot map. This is a hard limit of 16 million.
#define SLOTMAP_MAX_ID        0xFFFFFF

// Gets the 0-based array index of an element in the slot map by its 'id'. (You cannot loop through
// a slot map in this order, though. There will be holes with invalid data.)
// Returns: A SLOT_ID.
#define slotmap_index(id)     ((id) & SLOTMAP_MAX_ID)

// Free an entire slot map 'a' from memory. This doesn't just free the slot map metadata -
// everything is freed. Elements are part of the contiguous block of the slot map.
// Returns: NULL.
#define slotmap_free(a)       ((a) ? free(a),0 : 0)

// A count of how many entries in the slot map 'a' have been used in the allocation block.
// Some of these may be freed already, however.
// Returns: A uint32_t.
#define slotmap_used(a)       ((a) ? slotmap__use(a) : 0)

// A count of how many active entries there are with valid data in the slot map 'a'.
// Returns: A uint32_t.
#define slotmap_count(a)      ((a) ? slotmap__use(a) - slotmap__frc(a) : 0)

// A count of how many entries the slot map 'a' already has allocated space for.
// Returns: A uint32_t.
#define slotmap_allocated(a)  ((a) ? slotmap__siz(a) : 0)

// Add a new entry in the slot map 'a' and set SLOT_ID variable 'id' to the ID of the new entry.
// Returns: A pointer to the new item or NULL if the slot map has reached its maximum.
#define slotmap_add(a,id)     slotmap__new(a,id,{})

// Copy an entry 'o' into a new slot in slot map 'a', setting 'id' to the ID of the new entry.
// Returns: A pointer to the new item or NULL if the slot map has reached its maximum.
#define slotmap_copy(a,o,id)  slotmap__new(a,id, { *__item__ = *((__typeof__(a))o); })

// Determine an element's ID by supplying the slot map 'a' that contains it and a pointer 'o'
// to the element itself.
// Returns: A SLOT_ID.
#define slotmap_id(a,o)       slotmap__id((o)-slotmap_array(a), (o)->version)

// Get a pointer to an element by supplying the slot map 'a' that contains it and its SLOT_ID 'id'.
// Returns: A pointer to the element or NULL if the element is not found.
#define slotmap_at(a,id)     (!a ? 0 : ({ \
  SLOT_ID __id__ = slotmap_index(id); \
  __typeof__(a) __item__ = NULL; \
  if (__id__ < slotmap__use(a)) { \
    __item__ = slotmap_array(a) + __id__; \
    __item__ = (__item__->version != (id >> 24) ? NULL : __item__); \
  } \
  __item__; \
}))

// Removes an element with SLOT_ID 'id' from the slot map 'a'.
#define slotmap_remove(a,id)  slotmap_remove_and(a,id,__item__,{})

// Removes an element with SLOT_ID 'id' from the slot map 'a' and handles the
// item in the attached block using name of 'item' for the pointer. The pointer is only
// provided for final access to the element - please do not store the pointer, it is useless
// to any subsequent calls.
#define slotmap_remove_and(a,id,item,...)  (!a ? 0 : ({ \
  __typeof__(a) item = slotmap_at(a,id); \
  if (item) { \
    __VA_ARGS__; \
    *((SLOTMAP_FREE *)item) = (SLOTMAP_FREE){item->version + 1, slotmap__frl(a)}; \
    slotmap__frl(a) = slotmap_index(id); \
    slotmap__frc(a)++; \
  } \
  item; \
}))

// Burn the slotmap's freelist (useful to do before looping the structure as an
// array, to avoid garbled entries which are from the freelist.) I call this
// 'burn' because it's destructive: we can't regain these entries unless we
// rebuild the freelist somehow. So you really only want to do this before
// freeing.
#define slotmap_burn(a) (!a ? 0 : ({ \
  Q_ID x = slotmap__frl(a); \
  __typeof__(a) arr = slotmap_array(a); \
  slotmap__frl(a) = SLOT_NONE_ID; \
  while (slotmap_index(x) != SLOTMAP_MAX_ID) { \
    SLOTMAP_FREE *free_item = (SLOTMAP_FREE *)(arr + x); \
    x = free_item->next_free; \
    *free_item = (SLOTMAP_FREE){0}; \
  } \
}))

// Fetch the beginning of the actual items.
#define slotmap_array(a)      ((__typeof__(a))(((SLOT_ID *)(a)) + 4))

//
// internal macros
//
#define slotmap__id(index,v)  (slotmap_index(index) | ((SLOT_ID)(v) << 24))
#define slotmap__siz(a)       ((SLOT_ID *)(a))[0]
#define slotmap__use(a)       ((SLOT_ID *)(a))[1]
#define slotmap__frl(a)       ((SLOT_ID *)(a))[2]
#define slotmap__frc(a)       ((SLOT_ID *)(a))[3]

#define slotmap__new(a,id,...)     ({ \
  __typeof__(a) __item__ = (__typeof__(a))slotmap__make((uint8_t **)&a, sizeof(*(a)), &id); \
  if (__item__) { \
    __VA_ARGS__; \
    __item__->version = (id >> 24); \
  } \
  __item__; \
})

#include <string.h>

//
// Makes room for a new element.
// Returns: A pointer to the new object or NULL if no further objects could be created.
//
static inline uint8_t *
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
    if (slotmap_index(x) != SLOTMAP_MAX_ID) {
      SLOTMAP_FREE *free_item = (SLOTMAP_FREE *)(slotmap_array(arr) + (x * itemsize));
      *idp = slotmap__id(x, free_item->version);
      slotmap__frc(arr)--;
      slotmap__frl(arr) = free_item->next_free;
      return (uint8_t *)free_item;
    } else {
      siz = slotmap__siz(arr);
      used = slotmap__use(arr);
    }
  }

  //
  // Allocate additional space
  //
  newsiz = SLOT_ALIGN(
    (SLOT_FLEX_SIZE(siz) * itemsize) +
    (sizeof(SLOT_ID) * 4), SLOT_ALIGN_SIZE);
  if (used == siz) {
    p = (SLOT_ID *)SLOT_REALLOC(arr, newsiz);
    x = (newsiz - (sizeof(SLOT_ID) * 4)) / itemsize;
    *ary = (uint8_t *)p;
    p[0] = x;
  } else {
    p = (SLOT_ID *)arr;
  }

  //
  // Expand the array by one element and give back an ID.
  //
  if (p) {
    if (!arr) {
      p[1] = p[3] = 0;
      p[2] = SLOT_NONE_ID;
    }
    *idp = x = p[1]++;
    arr = *ary;
    return slotmap_array(arr) + (x * itemsize);
  }

  *idp = SLOT_NONE_ID;
  return NULL;
}

#endif
