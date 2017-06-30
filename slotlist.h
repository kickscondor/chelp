//
// slotlist.h
//
// A derivative of Sean Barrett's stretchy_buffer which allows one to specific a larger
// minimum size, a growth pattern and adds two extra fields for data. (Two fields to make
// room for a 64-bit pointer, if need be.)
//
// LICENSE
//
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
#ifndef SLOTLIST_H
#define SLOTLIST_H

#include "slotbase.h"

#define SLOTLIST_MAX             UINT32_MAX

#define slotlist_id(a,v)         (v - slotlist_array(a))
#define slotlist_free(a)         ((a) ? free(a),0 : 0)
#define slotlist_push(a,v)       (slotlist__sbmaybegrow(a,1), slotlist_at(a, slotlist__sbn(a)++) = (v))
#define slotlist_allocated(a)    ((a) ? slotlist__sbm(a) : 0)
#define slotlist_count(a)        ((a) ? slotlist__sbn(a) : 0)
#define slotlist_expand(a,n)     (slotlist__sbmaybegrow(a,n), slotlist__sbn(a)+=(n))
#define slotlist_add(a,n)        (slotlist_expand(a,n), &slotlist_at(a, slotlist__sbn(a)-(n)))
#define slotlist_truncate(a,n)   ((a) ? (slotlist__sbn(a)-=(n),1) : 0)
#define slotlist_clear(a)        slotlist__sbn(a) = 0
#define slotlist_last(a)         slotlist_at(a, slotlist__sbn(a)-1)

#define slotlist_array(a)        ((__typeof__(a))((SLOT_ID *) (a) + (2 + SLOT_EXT_SIZE)))
#define slotlist_at(a,n)         slotlist_array(a)[n]
#define slotlist_ptr(a)          ((SLOT_ID *) (a) - (2 + SLOT_EXT_SIZE))

#define slotlist__sbraw(a) ((SLOT_ID *) (a))
#define slotlist__sbm(a)   slotlist__sbraw(a)[0 + SLOT_EXT_SIZE]
#define slotlist__sbn(a)   slotlist__sbraw(a)[1 + SLOT_EXT_SIZE]

#define slotlist__sbneedgrow(a,n)  ((a)==0 || slotlist__sbn(a)+(n) > slotlist__sbm(a))
#define slotlist__sbmaybegrow(a,n) (slotlist__sbneedgrow(a,(n)) ? slotlist__sbgrow(a,n) : 0)
#define slotlist__sbgrow(a,n)      ((a) = slotlist__sbgrowf((a), (n), sizeof(*(a))))

#ifndef SLOTLIST_MACROS_ONLY
#include <stdlib.h>

static void *
slotlist__sbgrowf(void *arr, SLOT_ID increment, size_t itemsize)
{
  size_t newsize = 0,
         newitems = slotlist_allocated(arr),
         extsize = sizeof(SLOT_ID) * (2 + SLOT_EXT_SIZE),
         needed = slotlist_count(arr) + increment + SLOT_DIV_ALIGN(extsize, itemsize);
  while (newitems < needed)
    newitems = SLOT_FLEX_SIZE(newitems);

  newsize = SLOT_ALIGN(newitems * itemsize, SLOT_ALIGN_SIZE);
  newitems = (newsize - extsize) / itemsize;
  if (newitems < SLOTLIST_MAX) {
    SLOT_ID *p = (SLOT_ID *)SLOT_REALLOC(arr, newsize);
    if (p) {
      if (!arr)
        p[1 + SLOT_EXT_SIZE] = 0;
      p[0 + SLOT_EXT_SIZE] = newitems;
      return p;
    }
  }

  return NULL;
}
#endif

#endif
