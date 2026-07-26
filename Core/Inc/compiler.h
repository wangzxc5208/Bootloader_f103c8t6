/*
 * compiler.h - Compiler utilities (Linux kernel style)
 *
 * Provides container_of(), likely()/unlikely(), alignment macros,
 * and other compiler-level helpers used throughout the bootloader.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _COMPILER_H
#define _COMPILER_H

#include <stddef.h>

/* ── Compiler attribute wrappers ─────────────────────────────────── */

#define __weak          __attribute__((weak))
#define __packed        __attribute__((packed))
#define __aligned(x)    __attribute__((aligned(x)))
#define __section(s)    __attribute__((section(s)))
#define __used          __attribute__((used))
#define __unused        __attribute__((unused))
#define __must_check    __attribute__((warn_unused_result))
#define __noreturn      __attribute__((noreturn))
#define __noinline      __attribute__((noinline))
#define __always_inline __attribute__((always_inline)) static inline
#define __constructor   __attribute__((constructor))
#define __format(p,f,a) __attribute__((format(printf, p, f, a)))

/* ── Branch prediction hints ─────────────────────────────────────── */

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* ── container_of() — the cornerstone of Linux kernel OOP ────────── */

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:    pointer to the member
 * @type:   type of the container struct this is embedded in
 * @member: name of the member within the struct
 */
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* ── Array size ──────────────────────────────────────────────────── */

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* ── Alignment ───────────────────────────────────────────────────── */

#define ALIGN(x, a)       (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a)  ((x) & ~((a) - 1))
#define IS_ALIGNED(x, a)  (((x) & ((a) - 1)) == 0)

/* ── Min / Max / Clamp ───────────────────────────────────────────── */

#define min(a, b) ({                \
    typeof(a) _a = (a);             \
    typeof(b) _b = (b);             \
    _a < _b ? _a : _b;              \
})

#define max(a, b) ({                \
    typeof(a) _a = (a);             \
    typeof(b) _b = (b);             \
    _a > _b ? _a : _b;              \
})

#define clamp(val, lo, hi) ({       \
    typeof(val) _v = (val);         \
    typeof(lo)  _l = (lo);          \
    typeof(hi)  _h = (hi);          \
    _v < _l ? _l : (_v > _h ? _h : _v); \
})

/* ── Build-time assertions ───────────────────────────────────────── */

#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2 * !!(condition)]))

/* ── Symbol visibility ───────────────────────────────────────────── */

#define __visible __attribute__((externally_visible))

/* ── Read-write barrier ──────────────────────────────────────────── */

#define barrier() __asm__ __volatile__("" : : : "memory")

#endif /* _COMPILER_H */
