/*
 * errno.h - Error codes (Linux kernel style, negative errno)
 *
 * All functions in the bootloader return 0 on success or a negative
 * error code on failure. Positive values indicate a count (e.g.,
 * number of bytes written) and are never errors.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _BOOT_ERRNO_H
#define _BOOT_ERRNO_H

/* ── Success ─────────────────────────────────────────────────────── */

#define E_OK            0       /* No error */

/* ── Generic errors ──────────────────────────────────────────────── */

#define E_PERM         -1       /* Operation not permitted */
#define E_NOENT        -2       /* No such file or directory */
#define E_IO           -5       /* I/O error */
#define E_NOMEM       -12       /* Out of memory */
#define E_INVAL       -22       /* Invalid argument */
#define E_RANGE       -34       /* Math result not representable */
#define E_TIMEDOUT   -110       /* Connection timed out */
#define E_AGAIN      -111       /* Try again */

/* ── Bootloader-specific errors ──────────────────────────────────── */

#define E_BAD_MAGIC   -200      /* Invalid magic number */
#define E_BAD_CRC     -201      /* CRC checksum mismatch */
#define E_BAD_VERSION -202      /* Invalid/unsupported version */
#define E_BAD_LENGTH  -203      /* Invalid data length */

#define E_FLASH_ERASE -210      /* Flash erase failed */
#define E_FLASH_WRITE -211      /* Flash write failed */
#define E_FLASH_LOCK  -212      /* Flash lock/unlock failed */
#define E_FLASH_ALIGN -213      /* Flash address not aligned */

#define E_SLOT_EMPTY  -220      /* Application slot is empty */
#define E_SLOT_INVALID -221     /* Application slot image invalid */
#define E_SLOT_CRC    -222      /* Application slot CRC mismatch */

#define E_OTA_NOT_READY    -230 /* Not in OTA mode */
#define E_OTA_IN_PROGRESS  -231 /* OTA already in progress */
#define E_OTA_BAD_CMD      -232 /* Unknown OTA command */
#define E_OTA_BAD_FRAME    -233 /* Malformed OTA frame */
#define E_OTA_VERIFY_FAIL  -234 /* Image verification failed */
#define E_OTA_SIZE_EXCEED  -235 /* Image too large for slot */
#define E_OTA_WRITE_FAIL   -236 /* Write to flash failed during OTA */

#define E_PROTO_TIMEOUT  -240   /* Protocol timeout */
#define E_PROTO_SYNC     -241   /* Sync byte mismatch */
#define E_PROTO_OVERFLOW -242   /* Buffer overflow */

/* ── Error string lookup ─────────────────────────────────────────── */

/**
 * errno_name - return a human-readable name for an error code
 * @err: the error code (negative)
 * @return: string literal describing the error
 */
static inline const char *errno_name(int err)
{
    switch (err) {
    case E_OK:              return "OK";
    case E_PERM:            return "EPERM";
    case E_NOENT:           return "ENOENT";
    case E_IO:              return "EIO";
    case E_NOMEM:           return "ENOMEM";
    case E_INVAL:           return "EINVAL";
    case E_RANGE:           return "ERANGE";
    case E_TIMEDOUT:        return "ETIMEDOUT";
    case E_AGAIN:           return "EAGAIN";
    case E_BAD_MAGIC:       return "EBADMAGIC";
    case E_BAD_CRC:         return "EBADCRC";
    case E_BAD_VERSION:     return "EBADVERSION";
    case E_BAD_LENGTH:      return "EBADLENGTH";
    case E_FLASH_ERASE:     return "EFLASHERASE";
    case E_FLASH_WRITE:     return "EFLASHWRITE";
    case E_FLASH_LOCK:      return "EFLASHLOCK";
    case E_FLASH_ALIGN:     return "EFLASHALIGN";
    case E_SLOT_EMPTY:      return "ESLOTEMPTY";
    case E_SLOT_INVALID:    return "ESLOTINVALID";
    case E_SLOT_CRC:        return "ESLOTCRC";
    case E_OTA_NOT_READY:   return "EOTANOTREADY";
    case E_OTA_IN_PROGRESS: return "EOTAINPROGRESS";
    case E_OTA_BAD_CMD:     return "EOTABADCMD";
    case E_OTA_BAD_FRAME:   return "EOTABADFRAME";
    case E_OTA_VERIFY_FAIL: return "EOTAVERIFYFAIL";
    case E_OTA_SIZE_EXCEED: return "EOTASIZEEXCEED";
    case E_OTA_WRITE_FAIL:  return "EOTAWRITEFAIL";
    case E_PROTO_TIMEOUT:   return "EPROTOTIMEOUT";
    case E_PROTO_SYNC:      return "EPROTOSYNC";
    case E_PROTO_OVERFLOW:  return "EPROTOOVERFLOW";
    default:                return "EUNKNOWN";
    }
}

#endif /* _BOOT_ERRNO_H */
