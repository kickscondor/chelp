//
// slotbase.h
//
// Common stuff used by slotmap and slotlist. The base settings here are
// designed to be used with lists and maps that are small.
//
// LICENSE
//
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
#ifndef SLOTBASE_H
#define SLOTBASE_H

#define SLOT_ALIGN(n, d)  (((n) + (d)) & ~((d) - 1))

// The realloc function to use.
#ifndef SLOT_REALLOC
#define SLOT_REALLOC(p, n) realloc(p, n)
#endif

// The standard growth pattern for slot lists and slot maps.
#ifndef SLOT_FLEX_SIZE
#define SLOT_FLEX_SIZE(n) ((n) < 10 ? 10 : \
                           (n) < 100 ? 100 : \
                           (n) < 1000 ? 1000 : \
                           (n) < 10000 ? 10000 : ((n) * 2))
#endif

// The standard byte alignment for slot maps and slot lists.
// The structures will fill up to the alignment rather than
// just the above size.
#ifndef SLOT_ALIGN_SIZE
#define SLOT_ALIGN_SIZE 16
#endif

// The basic slot lists and maps have no surplus space for user data.
// This size should be the number of uint32_t spaces to allocate.
#ifndef SLOT_EXT_SIZE
#define SLOT_EXT_SIZE 0
#endif

// IDs in slot lists and slot maps are 32-bit unsigned integers.
typedef uint32_t SLOT_ID;

#endif
