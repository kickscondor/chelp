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
#ifndef SLOTTABLE_H
#define SLOTTABLE_H

#include "slotbase.h"

typedef struct {
  uint32_t allocated;
  uint32_t used;
  uint32_t active;
  SLOT_ID next_free;
  SLOT_ID index[0];
} Ch_SlotTable;

typedef struct {
  uint32_t hash;
  SLOT_ID next;
  uint8_t data[0];
} Ch_SlotTableItem;

static inline uint32_t slottable_str_hash(const char *s)
{
  uint32_t h = (uint32_t)*s;
  if (h) for (++s ; *s; ++s) h = (h << 5) - h + (uint32_t)*s;
  return h;
}

#ifndef SLOT_DOUBLE_SIZE
#define SLOT_DOUBLE_SIZE(n) (!(n) ? 8 : ((n) * 2))
#endif

#define slottable__size(a,itemsize) \
  (((a) * (itemsize + sizeof(SLOT_ID) + sizeof(Ch_SlotTableItem))) + \
    sizeof(Ch_SlotTable))
#define slottable_mem_usage(a)  (slottable__size(slottable_allocated(a), sizeof(*(a))))
// A count of how many entries in the slot table 'a' have been used in the allocation block.
// Some of these may be deleted already, however.
// Returns: A uint32_t.
#define slottable_used(a)       ((a) ? ((Ch_SlotTable *)a)->used : 0)

// A count of how many active entries there are with valid data in the slot table 'a'.
// Returns: A uint32_t.
#define slottable_count(a)      ((a) ? ((Ch_SlotTable *)a)->active : 0)

// A count of how many entries the slot table 'a' already has allocated space for.
// Returns: A uint32_t.
#define slottable_allocated(a)  ((a) ? ((Ch_SlotTable *)a)->allocated : 0)

#define SLOTTABLE_ORDERED  1
#define SLOTTABLE_FIXED_ID 2

// Add a new entry in the slot table 'a' and set SLOT_ID variable 'id' to the ID of the new entry.
// The ID is also stored in the index array spot corresponding to the 32-bit hash 'hsh'.
// If the SLOTTABLE_ORDERED attribute is set, items will be kept in insertion order.
// If the SLOTTABLE_FIXED_ID attribute is set, IDs will be kept permanent and items will not be shifted
// around when the hashtable is resized.
// Returns: A pointer to the new item or NULL if the slot table has reached its maximum.
#define slottable_add(a, hsh, id, flags)          slottable__add(a, hsh, id, sizeof(*(a)), flags)
#define slottable__add(a, hsh, id, sz, flags)     ({ \
  Ch_SlotTableItem *item = slottable__insert((uint8_t **)&a, sz, &id, flags); \
  item->hash = slottable__fix_hash(hsh); \
  slottable__add_hash((Ch_SlotTable *)a, id, item); \
  (__typeof__(a))item->data; \
})

// Find an entry in slot table 'a' using uint32_t hash 'hsh' and any type of 'key'.
// The key in the slot table item is compared with 'key' using the 'cmp' function.
// If a matching item is found, the 'id' is set to the item's ID in the slot table.
// Returns: A pointer to the slot table item's data or NULL if no item is found.
#define slottable_find(a, hsh, id, cmp, key) (!(a) ? (id = SLOT_NONE_ID, NULL) : ({ \
  Ch_SlotTable *__tblf__ = (Ch_SlotTable *)a; \
  uint32_t __hsh__ = slottable__fix_hash(hsh); \
  uint32_t __idx__ = __hsh__ & (__tblf__->allocated - 1); \
  uint8_t *items = (uint8_t *)slottable__data(a); \
  id = __tblf__->index[__idx__]; \
  Ch_SlotTableItem *item = NULL; \
  while (SLOT_NONE_ID != id && ({ \
    item = slottable__data_item(items, id, sizeof(*(a))); \
    __hsh__ != item->hash || cmp(key, (__typeof__(a))item->data) != 0;})) { \
      id = item->next; \
      item = NULL; \
  } \
  item == NULL ? NULL : (__typeof__(a))item->data; \
}))

// Remove an item from the slot table 'a' that matches the uint32_t 'hash' and
// the 'key'. The key in the slot table item is compared with 'key' using the
// 'cmp' function. Items are not freed, but only marked for removal. Items will
// be removed the next time the hash table is resized, if the SLOTTABLE_FIXED_ID
// flag is not used.
// Returns: A pointer to the slot table item's data or NULL if no item is found.
#define slottable_remove(a, hsh, cmp, key) (!(a) ? NULL : ({ \
  SLOT_ID __id__ = SLOT_NONE_ID; \
  Ch_SlotTable *__tbl__ = (Ch_SlotTable *)a; \
  __typeof__(a) data = slottable_find(a, hsh, __id__, cmp, key); \
  Ch_SlotTableItem *item = slottable__item(a, __id__, sizeof(*(a))); \
  item->hash = SLOT_NONE_ID; \
  item->next = __tbl__->next_free; \
  __tbl__->next_free = __id__; \
  __tbl__->active--; \
  data; \
}))

// Get a pointer to an element by supplying the slot table 'a' that contains it and its
// actual ID (not hash). Use slotmap_find to use the hash to lookup.
// Returns: A pointer to the element or NULL if the element is not found.
#define slottable_at_id(a, id) (!(a) ? NULL : ({ \
  Ch_SlotTableItem *item = slottable__item(a, id, sizeof(*(a))); \
  item->hash == SLOT_NONE_ID ? NULL : (__typeof__(a))item->data; \
}))

#define slottable__item(a, id, sz) slottable__data_item((uint8_t *)slottable__data(a), id, sz)
#define slottable__data_item(data, id, sz) ((Ch_SlotTableItem *)(((uint8_t *)(data)) + (id * (sz + sizeof(Ch_SlotTableItem)))))

#define slottable__fix_hash(hsh) ({ \
  __typeof__(hsh) __hsh__ = hsh; \
  __hsh__ == SLOT_NONE_ID ? SLOT_NONE_ID - 1 : __hsh__; \
})

#define slottable__add_hash(a, id, item) ({ \
  uint32_t __idx__ = item->hash & ((a)->allocated - 1); \
  item->next = (a)->index[__idx__]; \
  (a)->index[__idx__] = id; \
})

#define slottable__data(a) ({ \
  Ch_SlotTable *__tbl__ = (Ch_SlotTable *)(a); \
  __tbl__->index + __tbl__->allocated; \
})

#ifndef SLOTMAP_MACROS_ONLY
#include <string.h>

//
// Makes room for a new element.
// Returns: A pointer to the new object or NULL if no further objects could be created.
//
static Ch_SlotTableItem *
slottable__insert(uint8_t **ary, size_t itemsize, SLOT_ID *idp, uint8_t flags)
{
  Ch_SlotTable *tbl = (Ch_SlotTable *)*ary;
  SLOT_ID x;
  size_t used = 0, siz = 0, newsiz = 0;

  //
  // Reuse from the freelist if insertion order doesn't need to be kept.
  //
  if (tbl) {
    x = tbl->next_free;
    if (x != SLOT_NONE_ID && (flags & SLOTTABLE_ORDERED)) {
      Ch_SlotTableItem *item = slottable__item(tbl, x, itemsize);
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
    Ch_SlotTable *newtbl = (Ch_SlotTable *)malloc(slottable__size(newsiz, itemsize));
    newtbl->allocated = newsiz;

    //
    // Copy and rehash the table, removing holes along the way.
    //
    memset(newtbl->index, SLOT_NONE_ID, sizeof(SLOT_ID) * newsiz);
    newtbl->next_free = SLOT_NONE_ID;
    uint32_t newid = 0, newactive = 0;
    if (tbl) {
      for (uint32_t i = 0; i < used; i++) {
        Ch_SlotTableItem *item = slottable__item(tbl, i, itemsize);
        if (item->hash != SLOT_NONE_ID) {
          slottable__add_hash(newtbl, newid, item);
          newactive++;
        } else if (!(flags & SLOTTABLE_FIXED_ID)) {
          continue;
        }
        memcpy(slottable__item(newtbl, newid, itemsize), item, itemsize + sizeof(Ch_SlotTableItem));
        newid++;
      }
      if (flags & SLOTTABLE_FIXED_ID) {
        newtbl->next_free = tbl->next_free;
      }
      free(tbl);
    }
    newtbl->used = newid;
    newtbl->active = newactive;

    *ary = (uint8_t *)(tbl = newtbl);
  }

  //
  // Expand the array by one element and give back an ID.
  //
  if (tbl) {
    tbl->active++;
    *idp = x = tbl->used++;
    return slottable__item(tbl, x, itemsize);
  }

  *idp = SLOT_NONE_ID;
  return NULL;
}
#endif

#endif
