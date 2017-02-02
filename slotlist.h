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

#define slotlist_free(a)         ((a) ? free(slotlist_ptr(a)),0 : 0)
#define slotlist_push(a,v)       (slotlist__sbmaybegrow(a,1), (a)[slotlist__sbn(a)++] = (v))
#define slotlist_count(a)        ((a) ? slotlist__sbn(a) : 0)
#define slotlist_add(a,n)        (slotlist__sbmaybegrow(a,n), slotlist__sbn(a)+=(n), &(a)[slotlist__sbn(a)-(n)])
#define slotlist_last(a)         ((a)[slotlist__sbn(a)-1])
#define slotlist_ptr(a)          ((SLOT_ID *) (a) - (2 + SLOT_EXT_SIZE))
#define slotlist_array(a)        ((SLOT_ID *) (a) + (2 + SLOT_EXT_SIZE))

#define slotlist__sbraw(a) ((SLOT_ID *) (a) - 2))
#define slotlist__sbm(a)   slotlist__sbraw(a)[0]
#define slotlist__sbn(a)   slotlist__sbraw(a)[1]

#define slotlist__sbneedgrow(a,n)  ((a)==0 || slotlist__sbn(a)+(n) >= slotlist__sbm(a))
#define slotlist__sbmaybegrow(a,n) (slotlist__sbneedgrow(a,(n)) ? slotlist__sbgrow(a,n) : 0)
#define slotlist__sbgrow(a,n)      ((a) = slotlist__sbgrowf((a), (n), sizeof(*(a))))

#include <stdlib.h>

static void * slotlist__sbgrowf(void *arr, SLOT_ID increment, size_t itemsize)
{
   SLOT_ID needed = slotlist_count(arr), newsize = slotlist__sbm(arr);
   while (newsize < needed)
     newsize = SLOT_FLEX_SIZE(newsize);
   newsize = SLOT_ALIGN(newsize, SLOT_ALIGN_SIZE);

   SLOT_ID *p = (SLOT_ID *)SLOT_REALLOC(arr ? slotlist_ptr(arr) : 0,
     ((size_t)itemsize * newsize) + (sizeof(SLOT_ID) * (2 + SLOT_EXT_SIZE)));
   if (p) {
      if (!arr)
         p[1 + SLOT_EXT_SIZE] = 0;
      p[0 + SLOT_EXT_SIZE] = m;
      return p+2+SLOT_EXT_SIZE;
   } else {
      return (void *) ((2 + SLOT_EXT_SIZE)*sizeof(SLOT_ID)); // try to force a NULL pointer exception later
   }
}
#endif
