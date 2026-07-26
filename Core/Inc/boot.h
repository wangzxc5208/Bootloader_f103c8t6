/*
 * boot.h - Bootloader constants and configuration (simplified: single slot)
 *
 * Defines the Flash partition layout, timing constants, and frame
 * constants used throughout the bootloader.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _BOOT_H
#define _BOOT_H

#include "types.h"

/* ── Flash geometry ──────────────────────────────────────────────── */

#ifndef FLASH_SIZE
#define FLASH_SIZE           (64U * 1024U)       /* 64 KB total */
#endif
#define BOOT_FLASH_PAGE_COUNT 64U

/* ── Bootloader partition ────────────────────────────────────────── */

#define BOOTLOADER_BASE     0x08000000U
#define BOOTLOADER_SIZE     (16U * 1024U)       /* 16 KB */
#define BOOTLOADER_END      (BOOTLOADER_BASE + BOOTLOADER_SIZE)

/* ── Application partition (single slot, rest of Flash) ──────────── */

#define APP_BASE            0x08004000U
#define APP_SIZE            (48U * 1024U)       /* 48 KB */
#define APP_END             (APP_BASE + APP_SIZE)
#define APP_PAGE_START      16U
#define APP_PAGE_END        63U

/* ── Application image header ────────────────────────────────────── */

#define APP_HEADER_OFFSET   0x200U              /* Offset past vector table */
#define APP_HEADER_MAGIC    0xCAFEBABEU         /* Image valid marker */
#define APP_MAX_SIZE        APP_SIZE

/* ── Timing ──────────────────────────────────────────────────────── */

#define OTA_ENTER_TIMEOUT_MS  3000U   /* Wait 3s for OTA command on boot */
#define OTA_DATA_TIMEOUT_MS   5000U   /* Timeout between data packets   */
#define OTA_FRAME_TIMEOUT_MS  1000U   /* Per-frame receive timeout      */

/* ── UART configuration ──────────────────────────────────────────── */

#define BOOT_UART_BAUDRATE    9600U
#define BOOT_UART_DATABITS    UART_WORDLENGTH_8B
#define BOOT_UART_STOPBITS    UART_STOPBITS_1
#define BOOT_UART_PARITY      UART_PARITY_NONE
#define BOOT_UART_FLOWCTRL    UART_HWCONTROL_NONE

/* ── Frame constants ─────────────────────────────────────────────── */

#define FRAME_SYNC0          0xAAU
#define FRAME_SYNC1          0x55U
#define FRAME_HEADER_SIZE    5U    /* sync0 + sync1 + cmd + len(2)   */
#define FRAME_CRC_SIZE       2U
#define FRAME_MAX_PAYLOAD    1024U /* Max payload per frame          */
#define FRAME_MAX_SIZE       (FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD + FRAME_CRC_SIZE)

#endif /* _BOOT_H */
