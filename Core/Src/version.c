/*
 * version.c - Version string formatting and parsing
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#include "version.h"
#include "boot_errno.h"

/**
 * version_to_str - format version as "MAJOR.MINOR.PATCH"
 */
const char *version_to_str(const struct version *v, char *buf)
{
    /* Simple integer-to-string conversion — no sprintf needed in bootloader */
    static const char digits[] = "0123456789";
    char *p = buf;

    /* Major */
    if (v->major >= 100) { *p++ = digits[v->major / 100]; }
    if (v->major >= 10)  { *p++ = digits[(v->major / 10) % 10]; }
    *p++ = digits[v->major % 10];
    *p++ = '.';

    /* Minor */
    if (v->minor >= 100) { *p++ = digits[v->minor / 100]; }
    if (v->minor >= 10)  { *p++ = digits[(v->minor / 10) % 10]; }
    *p++ = digits[v->minor % 10];
    *p++ = '.';

    /* Patch */
    if (v->patch >= 100) { *p++ = digits[v->patch / 100]; }
    if (v->patch >= 10)  { *p++ = digits[(v->patch / 10) % 10]; }
    *p++ = digits[v->patch % 10];
    *p = '\0';

    return buf;
}

/**
 * version_parse - parse "MAJOR.MINOR.PATCH" string
 */
int version_parse(const char *str, struct version *v)
{
    u16 parts[3] = {0, 0, 0};
    int part = 0;
    const char *s = str;

    if (!str || !v)
        return E_INVAL;

    while (*s) {
        if (*s >= '0' && *s <= '9') {
            parts[part] = (u16)(parts[part] * 10 + (u32)(*s - '0'));
        } else if (*s == '.') {
            part++;
            if (part > 2)
                return E_INVAL;
        } else {
            return E_INVAL;
        }
        s++;
    }

    if (part != 2)
        return E_INVAL;

    v->major = parts[0];
    v->minor = parts[1];
    v->patch = parts[2];
    return E_OK;
}
