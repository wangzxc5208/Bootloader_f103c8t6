/*
 * info_block.c - Persistent bootloader metadata in Flash (dual-copy)
 *
 * Dual-copy strategy:
 *   Two copies of struct info_block are stored at fixed Flash pages.
 *   On each write, the copy with the LOWER sequence number (older one)
 *   is overwritten.  On read, the copy with valid magic+CRC and the
 *   HIGHER sequence number wins.
 *
 * This provides:
 *   1. Wear leveling — writes alternate between the two pages
 *   2. Power-loss safety — if power fails during write, the other
 *      copy is still valid
 *
 * The info block is cached in RAM after the initial read and accessed
 * via info_block_get().  info_block_write() persists the cache.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#include "info_block.h"
#include "flash.h"
#include "boot.h"
#include "boot_errno.h"
#include "compiler.h"
#include "list.h"
#include "types.h"

/* ── CRC forward declarations ────────────────────────────────────── */

extern u16 crc16_calc(const u8 *data, u32 len, u16 crc);

/* ── Cached info block ───────────────────────────────────────────── */

static struct info_block g_ib;           /* Cached copy in RAM */
static bool              g_ib_valid;     /* Has been loaded? */
static u32               g_ib_copy;      /* Which copy is active (0 or 1) */

/* ── Internal helpers ────────────────────────────────────────────── */

/**
 * info_block_crc - compute CRC-16 over an info block
 * @ib: info block to compute CRC for
 * @return: CRC-16 value
 *
 * The ib->crc field is zeroed during computation.
 */
static u16 info_block_calc_crc(const struct info_block *ib)
{
    struct info_block tmp = *ib;
    tmp.crc = 0;
    return crc16_calc((const u8 *)&tmp, sizeof(tmp), 0x0000);
}

/**
 * info_block_validate - check if an info block is valid
 * @ib:  info block to check
 * @return: true if magic matches and CRC is correct
 */
static bool info_block_validate(const struct info_block *ib)
{
    if (ib->magic != INFO_MAGIC)
        return false;
    u16 computed = info_block_calc_crc(ib);
    return computed == (u16)ib->crc;
}

/**
 * info_block_read_raw - read an info block copy from Flash
 * @drv: flash driver
 * @copy: 0 or 1 (which copy to read)
 * @ib: output
 * @return: 0 on success, negative on error
 */
static int info_block_read_raw(struct flash_driver *drv, u32 copy,
                                struct info_block *ib)
{
    u32 addr = (copy == 0) ? INFO_BLOCK_A_BASE : INFO_BLOCK_B_BASE;
    return drv->read(drv, addr, ib, sizeof(*ib));
}

/**
 * info_block_write_raw - write an info block copy to Flash
 * @drv: flash driver
 * @copy: 0 or 1
 * @ib: info block to write
 * @return: 0 on success, negative on error
 */
static int info_block_write_raw(struct flash_driver *drv, u32 copy,
                                 const struct info_block *ib)
{
    u32 addr = (copy == 0) ? INFO_BLOCK_A_BASE : INFO_BLOCK_B_BASE;

    /* Erase first */
    int ret = drv->erase(drv, addr, drv->get_page_size(drv));
    if (ret != E_OK)
        return ret;

    /* Write */
    ret = drv->write(drv, addr, ib, sizeof(*ib));
    if (ret < 0)
        return ret;

    return E_OK;
}

/**
 * info_block_create_default - create a default (factory) info block
 * @ib: output
 */
static void info_block_create_default(struct info_block *ib)
{
    /* Zero the whole struct first */
    for (u32 i = 0; i < sizeof(*ib) / sizeof(u32); i++)
        ((u32 *)ib)[i] = 0;

    ib->magic         = INFO_MAGIC;
    ib->struct_version = 1;
    ib->sequence       = 0;
    ib->active_slot    = SLOT_A;
    ib->previous_slot  = SLOT_A;
    ib->update_status  = INFO_STATUS_IDLE;
    ib->boot_attempt   = 0;
    ib->max_attempts   = DEFAULT_MAX_ATTEMPTS;
    ib->last_error     = 0;
    ib->total_updates  = 0;
    ib->total_rollbacks = 0;

    /* Slots start empty (version zero, size zero, crc zero) */
    ib->crc = info_block_calc_crc(ib);
}

/* ── Public API ──────────────────────────────────────────────────── */

/**
 * info_block_init - read and validate the info block on boot
 *
 * Scans both copies.  If at least one is valid, picks the one with
 * the highest sequence number.  If neither is valid, creates a
 * factory-default info block and persists it.
 */
int info_block_init(struct info_block *ib)
{
    struct flash_driver *drv = flash_get_default();
    struct info_block a, b;
    bool a_valid = false, b_valid = false;
    int ret;

    if (!drv)
        return E_IO;

    /* Initialize flash driver */
    ret = drv->init(drv);
    if (ret != E_OK)
        return ret;

    /* Read both copies */
    if (info_block_read_raw(drv, 0, &a) == E_OK)
        a_valid = info_block_validate(&a);

    if (info_block_read_raw(drv, 1, &b) == E_OK)
        b_valid = info_block_validate(&b);

    /* Pick the winning copy */
    if (a_valid && b_valid) {
        /* Both valid — pick the newer one (higher sequence) */
        if (a.sequence >= b.sequence) {
            *ib = a;
            g_ib_copy = 0;
        } else {
            *ib = b;
            g_ib_copy = 1;
        }
    } else if (a_valid) {
        *ib = a;
        g_ib_copy = 0;
    } else if (b_valid) {
        *ib = b;
        g_ib_copy = 1;
    } else {
        /* Neither valid — create factory default */
        info_block_create_default(ib);
        /* Write it to both copies */
        ib->sequence = 1;
        ib->crc = info_block_calc_crc(ib);
        info_block_write_raw(drv, 0, ib);
        info_block_write_raw(drv, 1, ib);
        g_ib_copy = 0;
    }

    /* Cache in RAM */
    g_ib = *ib;
    g_ib_valid = true;

    return E_OK;
}

/**
 * info_block_write - persist the info block to Flash
 *
 * Writes to the copy that has the LOWER sequence number (older one),
 * or to the opposite of the current active copy.  Increments the
 * sequence number and recalculates CRC.
 */
int info_block_write(const struct info_block *ib)
{
    struct flash_driver *drv = flash_get_default();
    u32 target_copy;
    int ret;

    if (!drv)
        return E_IO;

    /* Prepare block: increment sequence, update CRC */
    struct info_block to_write = *ib;
    to_write.sequence++;
    to_write.crc = info_block_calc_crc(&to_write);

    /* Write to the copy that is NOT the current active one */
    target_copy = (g_ib_copy == 0) ? 1 : 0;

    ret = info_block_write_raw(drv, target_copy, &to_write);
    if (ret != E_OK)
        return ret;

    /* Update the cache */
    g_ib = to_write;
    g_ib_copy = target_copy;

    return E_OK;
}

/**
 * info_block_mark_boot_success - called by application on successful boot
 */
int info_block_mark_boot_success(void)
{
    if (!g_ib_valid) {
        /* Info block not loaded — try to init it */
        int ret = info_block_init(&g_ib);
        if (ret != E_OK)
            return ret;
    }

    g_ib.update_status = INFO_STATUS_VALIDATED;
    g_ib.boot_attempt  = 0;
    g_ib.last_error    = 0;

    return info_block_write(&g_ib);
}

/**
 * info_block_prepare_update - prepare info block for an incoming OTA image
 */
int info_block_prepare_update(u32 slot, const struct version *version, u32 size)
{
    if (!g_ib_valid)
        return E_IO;

    if (slot != SLOT_A && slot != SLOT_B)
        return E_INVAL;

    if (size > SLOT_A_SIZE)
        return E_OTA_SIZE_EXCEED;

    /* Set the version and size for the target slot */
    u32 ver_arr[3];
    version_to_array(version, ver_arr);

    if (slot == SLOT_A) {
        g_ib.slot_a_version[0] = ver_arr[0];
        g_ib.slot_a_version[1] = ver_arr[1];
        g_ib.slot_a_version[2] = ver_arr[2];
        g_ib.slot_a_size       = size;
    } else {
        g_ib.slot_b_version[0] = ver_arr[0];
        g_ib.slot_b_version[1] = ver_arr[1];
        g_ib.slot_b_version[2] = ver_arr[2];
        g_ib.slot_b_size       = size;
    }

    /* Don't persist yet — commit_update does that */
    return E_OK;
}

/**
 * info_block_commit_update - finalize an OTA update
 */
int info_block_commit_update(u32 slot, u32 image_crc)
{
    if (!g_ib_valid)
        return E_IO;

    /* Store the previous active slot for rollback */
    g_ib.previous_slot = g_ib.active_slot;

    /* Switch to the new slot */
    g_ib.active_slot    = slot;
    g_ib.update_status  = INFO_STATUS_UPDATE_DONE;
    g_ib.boot_attempt   = 0;

    /* Store the image CRC */
    if (slot == SLOT_A)
        g_ib.slot_a_image_crc = image_crc;
    else
        g_ib.slot_b_image_crc = image_crc;

    g_ib.total_updates++;

    return info_block_write(&g_ib);
}

/**
 * info_block_trigger_rollback - roll back to the previous slot
 */
int info_block_trigger_rollback(struct info_block *ib)
{
    if (!ib)
        return E_INVAL;

    /* Swap active and previous slots */
    u32 prev = ib->previous_slot;
    ib->previous_slot = ib->active_slot;
    ib->active_slot   = prev;
    ib->update_status = INFO_STATUS_ROLLBACK;
    ib->boot_attempt  = 0;
    ib->total_rollbacks++;
    ib->last_error    = E_OTA_VERIFY_FAIL;

    /* Update the global cache */
    g_ib = *ib;

    return info_block_write(&g_ib);
}

/**
 * info_block_get - get pointer to the cached info block
 */
const struct info_block *info_block_get(void)
{
    return g_ib_valid ? &g_ib : NULL;
}
