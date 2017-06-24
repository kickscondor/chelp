//
// slottable.h
//
// A hash table that uses the same semantics as slotlist and slotmap. The design is
// similar too - one single pointer - meaning the entire hashtable is kept in one
// contiguous block of memory. The design is inspired by PHP 7's hash tables where
// ordering of insertion is preserved.
//
// Rather than relying on lots of macro tricks to give us the feeling of generics here,
// I am instead just using a basic naming convention.
//
// The unit of data stored by the hashtable is a struct. Put your key and value in a
// struct that is given a typedef alias.
//
//   typedef struct {
//     char *key;
//     double value;
//   } TestTable;
//
// A four-byte 'SLOT_ID next' and a four-byte 'SLOT_ID hash' is also added to the end of every entry.
// This means that every entry in the slot table has a 12-byte overhead.
//
// INTERNALS
//
// The layout here is:
//
//   uint32_t allocated_entries
//   uint32_t used_entries
//   uint32_t active_entries
//   uint32_t next_free_entry
//   uint32_t[allocated_entries] indices
//   (user_struct + SLOT_ID)[allocated_entries] items
//
#ifndef SLOTTABLE_H
#define SLOTTABLE_H

#include "slotbase.h"

struct Ch_SlotTable {
  uint32_t allocated;
  uint32_t used;
  uint32_t active;
  SLOT_ID next_free;
  SLOT_ID index[0];
};

struct Ch_SlotTableItem {
  uint32_t hash;
  SLOT_ID next;
  uint8_t data[0];
};

#ifndef SLOT_DOUBLE_SIZE
#define SLOT_DOUBLE_SIZE(n) (!(n) ? 8 : ((n) * 2))
#endif

// A count of how many entries in the slot table 'a' have been used in the allocation block.
// Some of these may be deleted already, however.
// Returns: A uint32_t.
#define slottable_used(a)       ((a) ? ((struct Ch_SlotTable *)a)->used : 0)

// A count of how many active entries there are with valid data in the slot table 'a'.
// Returns: A uint32_t.
#define slottable_count(a)      ((a) ? ((struct Ch_SlotTable *)a)->active : 0)

// A count of how many entries the slot table 'a' already has allocated space for.
// Returns: A uint32_t.
#define slottable_allocated(a)  ((a) ? ((struct Ch_SlotTable *)a)->allocated : 0)

#define SLOTTABLE_ORDERED  1
#define SLOTTABLE_FIXED_ID 2

// Add a new entry in the slot table 'a' and set SLOT_ID variable 'id' to the ID of the new entry.
// The ID is also stored in the index array spot corresponding to the 32-bit hash 'hsh'.
// If the SLOTTABLE_ORDERED attribute is set, items will be kept in insertion order.
// If the SLOTTABLE_FIXED_ID attribute is set, IDs will be kept permanent and items will not be shifted
// around when the hashtable is resized.
// Returns: A pointer to the new item or NULL if the slot table has reached its maximum.
#define slottable_add(a, hsh, id, flags)     ({ \
  struct Ch_SlotTableItem *item = slottable__make((uint8_t **)&a, sizeof(*(a)), &id, flags); \
  item->hash = slottable__fix_hash(hsh); \
  slottable__add_hash(a, id, item); \
  (__typeof__(a))item->data; \
})

// Find an entry in slot table 'a' using uint32_t hash 'hsh' and any type of 'key'. 
// The key in the slot table item is compared with 'key' using the 'cmp' function.
// If a matching item is found, the 'id' is set to the item's ID in the slot table.
// Returns: A pointer to the slot table item's data or NULL if no item is found.
#define slottable_find(a, hsh, id, cmp, key) (!(a) ? (id = SLOT_NONE_ID, NULL) : ({ \
  struct Ch_SlotTable *tbl = (struct Ch_SlotTable *)a; \
  uint32_t __idx__ = slottable__fix_hash(hsh) & (tbl->allocated - 1); \
  __typeof__(a) data = NULL; \
  id = tbl->index[__idx__]; \
  while (SLOT_NONE_ID != id) { \
    struct Ch_SlotTableItem *item = slottable__item(a, id); \
    data = (__typeof__(a))item->data; \
    if (cmp(key, data->key) == 0) { break; } \
    data = NULL; \
    id = item->next; \
  } \
  data; \
}))

// Remove an item from the slot table 'a' that matches the uint32_t 'hash' and
// the 'key'. The key in the slot table item is compared with 'key' using the
// 'cmp' function. Items are not freed, but only marked for removal. Items will
// be removed the next time the hash table is resized, if the SLOTTABLE_FIXED_ID
// flag is not used.
// Returns: A pointer to the slot table item's data or NULL if no item is found.
#define slottable_remove(a, hsh, cmp, key) (!(a) ? NULL : ({ \
  SLOT_ID __id__ = SLOT_NONE_ID; \
  struct Ch_SlotTable *tbl = (struct Ch_SlotTable *)a; \
  __typeof__(a) data = slottable_find(a, hsh, __id__, cmp, key); \
  struct Ch_SlotTableItem *item = slottable__item(a, __id__); \
  item->hash = SLOT_NONE_ID; \
  item->next = tbl->next_free; \
  tbl->next_free = __id__; \
  tbl->active--; \
  data; \
}))

// Get a pointer to an element by supplying the slot table 'a' that contains it and its
// actual ID (not hash). Use slotmap_find to use the hash to lookup.
// Returns: A pointer to the element or NULL if the element is not found.
#define slottable__item(a, id) ((struct Ch_SlotTableItem *)(slottable__data(a)[id]))

#define slottable__fix_hash(hsh) (hsh == SLOT_NONE_ID ? SLOT_NONE_ID - 1 : hsh)

#define slottable__add_hash(a, id, item) ({ \
  uint32_t __idx__ = item->hash & (a->allocated - 1); \
  item->next = a->index[__idx__]; \
  a->index[__idx__] = id; \
})

#define slottable__data(a) ({ \
  struct Ch_SlotTable *tbl = (struct Ch_SlotTable *)a; \
  a->index + a->allocated; \
})

#ifndef SLOTMAP_MACROS_ONLY
#include <string.h>

//
// Makes room for a new element.
// Returns: A pointer to the new object or NULL if no further objects could be created.
//
static struct Ch_SlotTableItem *
slottable__make(uint8_t **ary, size_t itemsize, SLOT_ID *idp, uint8_t flags)
{
  struct Ch_SlotTable *tbl = *ary;
  SLOT_ID *p;
  SLOT_ID x;
  size_t used = 0, siz = 0, newsiz = 0;

  //
  // Reuse from the freelist if insertion order doesn't need to be kept.
  //
  if (tbl) {
    x = tbl->next_free;
    if (x != SLOT_NONE_ID && (flags & SLOTTABLE_ORDERED)) {
      struct Ch_SlotTableItem *item = slottable__item(tbl, x);
      tbl->next_free = item->next;
      *idp = x;
      return item;
    } else {
      siz = tbl->allocated;
      used = tbl->used;
    }
  }

  //
  // Allocate additional space
  //
  if (used == siz) {
    newsiz = SLOT_DOUBLE_SIZE(siz);
    struct Ch_SlotTable *newtbl = (struct Ch_SlotTable *)malloc(
      (SLOT_DOUBLE_SIZE(siz) * (itemsize + sizeof(SLOT_ID) + sizeof(struct Ch_SlotTableItem))) +
      sizeof(Ch_SlotTable));
    newtbl->allocated = newsiz;

    //
    // Copy and rehash the table, removing holes along the way.
    //
    memset(newtbl->index, SLOT_NONE_ID, sizeof(SLOT_ID) * newsiz);
    uint32_t newid = 0;
    if (tbl) {
      for (uint32_t i = 0; i < used; i++) {
        struct Ch_SlotTableItem *item = slottable__item(tbl, i);
        if (item->hash != SLOT_NONE_ID) {
          slottable__add_hash(tbl, newid, item);
        } else if (!(flags & SLOTTABLE_FIXED_ID)) {
          continue;
        }
        *slottable__item(newtbl, newid) = *item;
        newid++;
      }
      free(tbl);
    }
    newtbl->used = newtbl->active = newid;
    newtbl->next_free = SLOT_NONE_ID;

    *ary = (uint8_t *)(tbl = newtbl);
  }

  //
  // Expand the array by one element and give back an ID.
  //
  if (tbl) {
    *idp = x = tbl->used++;
    return slottable__item(tbl, x);
  }

  *idp = SLOT_NONE_ID;
  return NULL;
}
#endif

#endif
