/*
 * info_block.h - Persistent bootloader state in Flash (dual-copy)
 *
 * The info block stores the bootloader's persistent state across resets:
 * which slot is active, update status, version/CRC for each slot, etc.
 *
 * Two copies are maintained for wear leveling and power-loss safety.
 * On each write, the older copy is overwritten.  On boot, the valid
 * copy with the highest sequence number wins.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _INFO_BLOCK_H
#define _INFO_BLOCK_H

#include "types.h"
#include "version.h"

/* ── Info block magic ────────────────────────────────────────────── */

#define INFO_MAGIC  0x424F4F54U   /* "BOOT" in little-endian ASCII */

/* ── Update status values ────────────────────────────────────────── */

enum {
    INFO_STATUS_IDLE         = 0,
    INFO_STATUS_UPDATE_DONE  = 1,
    INFO_STATUS_TRYING       = 2,
    INFO_STATUS_VALIDATED    = 3,
    INFO_STATUS_ROLLBACK     = 4,
    INFO_STATUS_FAILED       = 5,
};

/* ── Info block structure ────────────────────────────────────────── */

/**
 * struct info_block - Persistent bootloader metadata
 *
 * Stored in two copies in dedicated Flash pages (pages 56-59).
 * All fields are u32 for flash-friendly half-word programming.
 * Total size: 64 bytes (fits easily in one 2KB page).
 */
struct info_block {
    u32 magic;                  /* INFO_MAGIC */
    u32 struct_version;         /* Layout version (1) */
    u32 sequence;               /* Monotonic write counter */
    u32 crc;                    /* CRC-32 of this struct (field zeroed for calc) */

    u32 active_slot;            /* SLOT_A (0) or SLOT_B (1) */
    u32 previous_slot;          /* Previous active slot for rollback */
    u32 update_status;          /* INFO_STATUS_* */

    u32 boot_attempt;           /* Attempts made on current image */
    u32 max_attempts;           /* Max attempts before rollback (default 3) */

    /* Version info per slot */
    u32 slot_a_version[3];      /* {major, minor, patch} for slot A */
    u32 slot_b_version[3];      /* {major, minor, patch} for slot B */

    /* Image metadata per slot */
    u32 slot_a_size;            /* Image size in bytes */
    u32 slot_b_size;
    u32 slot_a_image_crc;       /* CRC-16 over the entire image */
    u32 slot_b_image_crc;

    /* Diagnostics */
    u32 last_error;             /* Last error code (see errno.h) */
    u32 total_updates;          /* Lifetime successful update counter */
    u32 total_rollbacks;        /* Lifetime rollback counter */

    /* Reserved for future use — pad to nice boundary */
    u32 reserved[4];
};

/* Static size check at compile time */
/* (ensures struct fits within a single page) */

/* ── API ─────────────────────────────────────────────────────────── */

/**
 * info_block_init - read and validate the info block from Flash on boot
 *
 * Scans both copies, picks the valid one with highest sequence number.
 * If neither is valid, creates a default (empty) info block.
 *
 * @ib:   output — the valid info block
 * @return: 0 on success, negative error code on failure
 */
int info_block_init(struct info_block *ib);

/**
 * info_block_write - write the info block to Flash
 *
 * Erases and overwrites the older copy.  Increments the sequence
 * number and recalculates CRC automatically.
 *
 * @ib: info block to write
 * @return: 0 on success, negative error code on failure
 */
int info_block_write(const struct info_block *ib);

/**
 * info_block_mark_boot_success - called by application on successful boot
 *
 * Sets update_status = INFO_STATUS_VALIDATED and resets boot_attempt.
 * The application calls this via a thin wrapper (boot_if.c).
 *
 * @return: 0 on success, negative error code on failure
 */
int info_block_mark_boot_success(void);

/**
 * info_block_prepare_update - prepare to receive a new OTA image
 *
 * Sets update_status = INFO_STATUS_UPDATE_DONE for the target slot.
 *
 * @slot:       target slot (SLOT_A or SLOT_B)
 * @version:    version of the new image
 * @size:       expected image size
 * @return: 0 on success, negative error code on failure
 */
int info_block_prepare_update(u32 slot, const struct version *version, u32 size);

/**
 * info_block_commit_update - finalize an OTA update
 *
 * Sets update_status = INFO_STATUS_UPDATE_DONE, switches active slot,
 * saves image CRC.
 *
 * @slot:       slot that received the new image
 * @image_crc:  CRC-16 of the complete image
 * @return: 0 on success, negative error code on failure
 */
int info_block_commit_update(u32 slot, u32 image_crc);

/**
 * info_block_trigger_rollback - roll back to the previous slot
 *
 * Called when boot_attempt exceeds max_attempts.
 *
 * @ib: current info block (modified in place and written back)
 * @return: 0 on success, negative error code on failure
 */
int info_block_trigger_rollback(struct info_block *ib);

/**
 * info_block_get - get a pointer to the current in-memory info block
 *
 * The info block is loaded once at boot and cached.  Most code
 * reads through this pointer.  Call info_block_write() to persist.
 *
 * @return: pointer to the cached info block (never NULL after init)
 */
const struct info_block *info_block_get(void);

#endif /* _INFO_BLOCK_H */
