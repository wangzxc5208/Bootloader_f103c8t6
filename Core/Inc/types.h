/*
 * types.h - Fixed-width type definitions (Linux kernel style)
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _TYPES_H
#define _TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Fixed-width unsigned types */
typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;

/* Fixed-width signed types */
typedef int8_t    s8;
typedef int16_t   s16;
typedef int32_t   s32;
typedef int64_t   s64;

/* Size type */
typedef size_t    usize;

/* Physical address */
typedef u32       phys_addr_t;

#endif /* _TYPES_H */
