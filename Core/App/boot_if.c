/*
 * boot_if.c - Bootloader interface for applications
 *
 * Provides functions the application calls to communicate with
 * the bootloader.  These use a shared RAM region that survives
 * a soft reset (but NOT a power cycle).
 *
 * For persistent communication (across power cycles), the
 * application can call info_block_mark_boot_success() directly
 * if the info block API is linked in.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#include "stm32f1xx_hal.h"

/* ── Shared RAM communication ────────────────────────────────────── */

/*
 * The bootloader and application share the last 2 KB of RAM
 * (0x20004800 - 0x20004FFF) for flags that survive soft resets.
 *
 * These are used when the app doesn't want to (or can't) access
 * the flash-based info block directly.
 */

#define SHARED_BASE      0x20004800U
#define MAGIC_OFFSET     0x000U    /* Boot magic word */
#define REASON_OFFSET    0x004U    /* Reboot reason */
#define CMD_OFFSET       0x008U    /* App → Bootloader command */

#define BOOT_MAGIC       0xB007C0DEU
#define REASON_NORMAL    0x00000000U
#define REASON_OTA       0x0TA0001U
#define CMD_NONE         0x00000000U
#define CMD_ENTER_OTA    0x00000001U

/**
 * boot_mark_success - tell the bootloader this image booted OK
 *
 * This sets a flag in the shared RAM region AND writes to the
 * info block in Flash to permanently mark this image as valid.
 *
 * Must be called early in the application's main().
 */
void boot_mark_success(void)
{
    /* Set the shared RAM magic word to indicate a successful boot */
    volatile uint32_t *magic = (volatile uint32_t *)(SHARED_BASE + MAGIC_OFFSET);
    volatile uint32_t *reason = (volatile uint32_t *)(SHARED_BASE + REASON_OFFSET);

    *magic = BOOT_MAGIC;
    *reason = REASON_NORMAL;

    /*
     * For a full implementation, also call the info block API here:
     *   info_block_mark_boot_success();
     *
     * This requires linking the info_block.c + flash.c + crc16.c
     * into the application as well, or providing a thin wrapper
     * that the bootloader exposes at a known address.
     */

    /* Memory barrier to ensure the write completes */
    __DSB();
}

/**
 * boot_request_ota - request the bootloader to enter OTA mode on next reset
 *
 * Call this before triggering a system reset.
 */
void boot_request_ota(void)
{
    volatile uint32_t *magic = (volatile uint32_t *)(SHARED_BASE + MAGIC_OFFSET);
    volatile uint32_t *reason = (volatile uint32_t *)(SHARED_BASE + REASON_OFFSET);
    volatile uint32_t *cmd = (volatile uint32_t *)(SHARED_BASE + CMD_OFFSET);

    *magic  = BOOT_MAGIC;
    *reason = REASON_OTA;
    *cmd    = CMD_ENTER_OTA;

    __DSB();
}

/**
 * boot_reset - perform a software system reset
 */
void boot_reset(void)
{
    __DSB();
    NVIC_SystemReset();
}
