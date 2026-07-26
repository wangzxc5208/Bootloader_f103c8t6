/*
 * version.h - Firmware version management
 *
 * Implements semantic versioning (MAJOR.MINOR.PATCH) with comparison
 * and serialization helpers.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _VERSION_H
#define _VERSION_H

#include "types.h"

/**
 * struct version - Semantic version triple
 *
 * Each component is a u16 to allow reasonable ranges.
 * The version array representation uses 3 × u32 for
 * alignment-friendly storage in Flash info blocks.
 */
struct version {
    u16 major;
    u16 minor;
    u16 patch;
};

/* ── Well-known versions ─────────────────────────────────────────── */

#define VERSION_ZERO  ((struct version){0, 0, 0})
#define VERSION_INIT  ((struct version){1, 0, 0})

/* ── Construction ────────────────────────────────────────────────── */

/**
 * version_make - create a version from components
 */
static inline struct version version_make(u16 major, u16 minor, u16 patch)
{
    struct version v = {major, minor, patch};
    return v;
}

/* ── Comparison ──────────────────────────────────────────────────── */

/**
 * version_cmp - compare two versions
 * @a: first version
 * @b: second version
 * @return: 0 if equal, <0 if a < b, >0 if a > b
 */
static inline int version_cmp(const struct version *a, const struct version *b)
{
    if (a->major != b->major)
        return (int)a->major - (int)b->major;
    if (a->minor != b->minor)
        return (int)a->minor - (int)b->minor;
    return (int)a->patch - (int)b->patch;
}

/**
 * version_lt - is a < b?
 */
static inline bool version_lt(const struct version *a, const struct version *b)
{
    return version_cmp(a, b) < 0;
}

/**
 * version_gt - is a > b?
 */
static inline bool version_gt(const struct version *a, const struct version *b)
{
    return version_cmp(a, b) > 0;
}

/**
 * version_eq - are versions equal?
 */
static inline bool version_eq(const struct version *a, const struct version *b)
{
    return version_cmp(a, b) == 0;
}

/**
 * version_is_zero - is this the zero version (unset)?
 */
static inline bool version_is_zero(const struct version *v)
{
    return v->major == 0 && v->minor == 0 && v->patch == 0;
}

/* ── Conversion ──────────────────────────────────────────────────── */

/**
 * version_from_array - build a version from u32[3] (info block format)
 */
static inline struct version version_from_array(const u32 arr[3])
{
    struct version v = {
        .major = (u16)arr[0],
        .minor = (u16)arr[1],
        .patch = (u16)arr[2],
    };
    return v;
}

/**
 * version_to_array - write a version to u32[3] (info block format)
 */
static inline void version_to_array(const struct version *v, u32 arr[3])
{
    arr[0] = v->major;
    arr[1] = v->minor;
    arr[2] = v->patch;
}

/* ── String representation ───────────────────────────────────────── */

/**
 * version_to_str - format version as "MAJOR.MINOR.PATCH"
 * @v: version to format
 * @buf: output buffer (must be at least 16 bytes)
 * @return: buf
 */
const char *version_to_str(const struct version *v, char *buf);

/**
 * version_parse - parse "MAJOR.MINOR.PATCH" string to version
 * @str: input string
 * @v: output version
 * @return: 0 on success, -EINVAL on parse error
 */
int version_parse(const char *str, struct version *v);

#endif /* _VERSION_H */
