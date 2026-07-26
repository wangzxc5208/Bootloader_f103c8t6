/*
 * boot.h - Bootloader global constants and configuration
 *
 * Defines the Flash partition layout, slot addresses, status codes,
 * and timing constants used throughout the bootloader.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _BOOT_H
#define _BOOT_H

#include "types.h"

/* ── Flash geometry ──────────────────────────────────────────────── */
/*
 * FLASH_BASE is defined by the STM32 CMSIS device header (stm32f103xb.h).
 * FLASH_PAGE_SIZE is defined by stm32f1xx_hal_flash_ex.h (0x400 = 1024).
 * We define only the total FLASH_SIZE which the HAL does not provide.
 */
#ifndef FLASH_SIZE
#define FLASH_SIZE           (64U * 1024U)       /* 64 KB total */
#endif
#define BOOT_FLASH_PAGE_COUNT 64U

/* ── Bootloader partition ────────────────────────────────────────── */

#define BOOTLOADER_BASE     0x08000000U
#define BOOTLOADER_SIZE     (16U * 1024U)       /* 16 KB */
#define BOOTLOADER_END      (BOOTLOADER_BASE + BOOTLOADER_SIZE)

/* ── Application slot A ──────────────────────────────────────────── */

#define SLOT_A_BASE         0x08004000U
#define SLOT_A_SIZE         (20U * 1024U)       /* 20 KB */
#define SLOT_A_END          (SLOT_A_BASE + SLOT_A_SIZE)
#define SLOT_A_PAGE_START   16U
#define SLOT_A_PAGE_END     35U

/* ── Application slot B ──────────────────────────────────────────── */

#define SLOT_B_BASE         0x08009000U
#define SLOT_B_SIZE         (20U * 1024U)       /* 20 KB */
#define SLOT_B_END          (SLOT_B_BASE + SLOT_B_SIZE)
#define SLOT_B_PAGE_START   36U
#define SLOT_B_PAGE_END     55U

/* ── Info Block (dual-copy, 2 KB each) ───────────────────────────── */

#define INFO_BLOCK_A_BASE   0x0800E000U
#define INFO_BLOCK_B_BASE   0x0800E800U
#define INFO_BLOCK_SIZE     2048U               /* 2 KB */
#define INFO_BLOCK_PAGE_A_START 56U
#define INFO_BLOCK_PAGE_B_START 58U

/* ── Reserved ────────────────────────────────────────────────────── */

#define RESERVED_BASE       0x0800F000U
#define RESERVED_SIZE       (4U * 1024U)

/* ── RAM shared region for bootloader ↔ app communication ───────── */

#define SHARED_RAM_BASE     0x20004800U         /* Last 2 KB of 20 KB RAM */
#define SHARED_RAM_SIZE     0x0800U
#define BOOT_MAGIC_ADDR     (SHARED_RAM_BASE)   /* Boot magic word */
#define BOOT_MAGIC_VALUE    0xB007C0DEU         /* "BOOT CODE" */
#define REBOOT_REASON_ADDR  (SHARED_RAM_BASE + 4)
#define REBOOT_REASON_OTA   0x0TA0001U
#define REBOOT_REASON_NORMAL 0

/* ── Application image header ────────────────────────────────────── */

#define APP_HEADER_OFFSET   0x200U              /* Offset past vector table (max ~400B) */
#define APP_HEADER_MAGIC    0xCAFEBABEU         /* Image valid marker */
#define APP_MAX_SIZE        SLOT_A_SIZE         /* Max app image size */

/* ── Update status constants ─────────────────────────────────────── */

#define STATUS_IDLE          0U   /* No update in progress */
#define STATUS_UPDATE_DONE   1U   /* New image written, ready to try */
#define STATUS_TRYING        2U   /* Attempting to boot new image */
#define STATUS_VALIDATED     3U   /* Image confirmed good by app */
#define STATUS_ROLLBACK      4U   /* Rollback in progress */
#define STATUS_FAILED        5U   /* Update failed permanently */

/* ── Slot identifiers ────────────────────────────────────────────── */

#define SLOT_A               0U
#define SLOT_B               1U
#define SLOT_INVALID         0xFFU

/* ── Boot attempt limits ─────────────────────────────────────────── */

#define DEFAULT_MAX_ATTEMPTS 3U

/* ── Timing ──────────────────────────────────────────────────────── */

#define OTA_ENTER_TIMEOUT_MS  3000U   /* Wait 3s for OTA command on boot */
#define OTA_DATA_TIMEOUT_MS   5000U   /* Timeout between data packets */
#define OTA_FRAME_TIMEOUT_MS  1000U   /* Per-frame receive timeout */

/* ── UART configuration ──────────────────────────────────────────── */

#define BOOT_UART_BAUDRATE    9600U
#define BOOT_UART_DATABITS    UART_WORDLENGTH_8B
#define BOOT_UART_STOPBITS    UART_STOPBITS_1
#define BOOT_UART_PARITY      UART_PARITY_NONE
#define BOOT_UART_FLOWCTRL    UART_HWCONTROL_NONE

/* ── Frame constants ─────────────────────────────────────────────── */

#define FRAME_SYNC0          0xAAU
#define FRAME_SYNC1          0x55U
#define FRAME_HEADER_SIZE    5U    /* sync0 + sync1 + cmd + len(2) */
#define FRAME_CRC_SIZE       2U
#define FRAME_MAX_PAYLOAD    1024U /* Max payload per frame */
#define FRAME_MAX_SIZE       (FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD + FRAME_CRC_SIZE)

/* ── Helper macros ───────────────────────────────────────────────── */

/**
 * slot_base - get the base address of a slot
 * @slot: SLOT_A or SLOT_B
 */
static inline u32 slot_base(u32 slot)
{
    return (slot == SLOT_A) ? SLOT_A_BASE : SLOT_B_BASE;
}

/**
 * slot_size - get the size of a slot
 * @slot: SLOT_A or SLOT_B
 */
static inline u32 slot_size(u32 slot)
{
    (void)slot;
    return SLOT_A_SIZE;  /* Both slots are same size */
}

/**
 * other_slot - get the opposite slot
 * @slot: SLOT_A or SLOT_B
 */
static inline u32 other_slot(u32 slot)
{
    return (slot == SLOT_A) ? SLOT_B : SLOT_A;
}

#endif /* _BOOT_H */
