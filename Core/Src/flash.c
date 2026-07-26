/*
 * flash.c - STM32F1 internal flash driver
 *
 * Wraps HAL_FLASH_* functions as a struct flash_driver implementation.
 *
 * STM32F103C8T6 flash constraints:
 *   - Page size: 1 KB
 *   - Write width: 16-bit half-word only
 *   - Must unlock before erase/write, lock after
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#include "flash.h"
#include "boot.h"
#include "errno.h"
#include "compiler.h"
#include "stm32f1xx_hal.h"

/* ── Private helpers ─────────────────────────────────────────────── */

static u32 flash_page_size(struct flash_driver *drv)
{
    (void)drv;
    return FLASH_PAGE_SIZE;
}

/**
 * flash_get_page - return the page number for a given address
 */
static inline u32 flash_get_page(u32 addr)
{
    return (addr - FLASH_BASE) / FLASH_PAGE_SIZE;
}

/* ── Flash ops ───────────────────────────────────────────────────── */

static int flash_init(struct flash_driver *drv)
{
    (void)drv;
    /* Internal flash needs no separate init — HAL_Init handles it */
    return E_OK;
}

static int flash_erase(struct flash_driver *drv, u32 addr, u32 len)
{
    (void)drv;

    u32 start_page = flash_get_page(addr);
    u32 end_addr   = addr + len;
    u32 end_page   = flash_get_page(end_addr - 1);
    u32 page_error = 0;
    FLASH_EraseInitTypeDef erase_init;

    if (addr < FLASH_BASE || end_addr > (FLASH_BASE + FLASH_SIZE))
        return E_RANGE;

    /* Unlock flash */
    if (HAL_FLASH_Unlock() != HAL_OK)
        return E_FLASH_LOCK;

    /* Erase page by page */
    erase_init.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase_init.Banks       = 0;
    erase_init.NbPages     = 1;

    for (u32 page = start_page; page <= end_page; page++) {
        erase_init.PageAddress = FLASH_BASE + page * FLASH_PAGE_SIZE;

        if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK) {
            HAL_FLASH_Lock();
            return E_FLASH_ERASE;
        }

        if (page_error != 0xFFFFFFFFU) {
            HAL_FLASH_Lock();
            return E_FLASH_ERASE;
        }
    }

    HAL_FLASH_Lock();
    return E_OK;
}

static int flash_write(struct flash_driver *drv, u32 addr, const void *data, u32 len)
{
    (void)drv;

    const u16 *src = (const u16 *)data;
    u32 remaining = len;
    u32 current_addr = addr;

    if (addr < FLASH_BASE || (addr + len) > (FLASH_BASE + FLASH_SIZE))
        return E_RANGE;

    /* Must be half-word aligned on STM32F1 */
    if (!IS_ALIGNED(addr, 2) || !IS_ALIGNED((u32)data, 2))
        return E_FLASH_ALIGN;

    if (HAL_FLASH_Unlock() != HAL_OK)
        return E_FLASH_LOCK;

    /* Write half-words */
    u32 hw_count = len / 2;
    for (u32 i = 0; i < hw_count; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, current_addr, src[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return E_FLASH_WRITE;
        }
        current_addr += 2;
        remaining -= 2;
    }

    /* Handle trailing byte (if odd length) — pad with 0xFF */
    if (remaining > 0) {
        u16 last = (u16)(((const u8 *)data)[len - 1]) | 0xFF00U;
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, current_addr, last) != HAL_OK) {
            HAL_FLASH_Lock();
            return E_FLASH_WRITE;
        }
        remaining--;
    }

    HAL_FLASH_Lock();
    return (int)len;
}

static int flash_read(struct flash_driver *drv, u32 addr, void *data, u32 len)
{
    (void)drv;

    if (addr < FLASH_BASE || (addr + len) > (FLASH_BASE + FLASH_SIZE))
        return E_RANGE;

    /* Flash is memory-mapped — direct memcpy works */
    const u8 *src = (const u8 *)addr;
    u8 *dst = (u8 *)data;
    for (u32 i = 0; i < len; i++)
        dst[i] = src[i];

    return (int)len;
}

static int flash_lock(struct flash_driver *drv)
{
    (void)drv;
    if (HAL_FLASH_Lock() != HAL_OK)
        return E_FLASH_LOCK;
    return E_OK;
}

static int flash_unlock_op(struct flash_driver *drv)
{
    (void)drv;
    if (HAL_FLASH_Unlock() != HAL_OK)
        return E_FLASH_LOCK;
    return E_OK;
}

/* ── Driver instance ─────────────────────────────────────────────── */

struct flash_driver flash_driver_stm32f1 = {
    .name          = "stm32f1_internal",
    .init          = flash_init,
    .erase         = flash_erase,
    .write         = flash_write,
    .read          = flash_read,
    .lock          = flash_lock,
    .unlock        = flash_unlock_op,
    .get_page_size = flash_page_size,
    .priv          = NULL,
    .list          = LIST_HEAD_INIT(flash_driver_stm32f1.list),
};

/* ── Global driver list ─────────────────────────────────────────── */

LIST_HEAD(flash_drivers);

int flash_driver_register(struct flash_driver *drv)
{
    if (!drv || !drv->name)
        return E_INVAL;
    list_add_tail(&drv->list, &flash_drivers);
    return E_OK;
}

void flash_driver_unregister(struct flash_driver *drv)
{
    if (drv)
        list_del_init(&drv->list);
}

struct flash_driver *flash_driver_find(const char *name)
{
    struct flash_driver *drv;
    list_for_each_entry(drv, &flash_drivers, list) {
        const char *a = drv->name;
        const char *b = name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == '\0' && *b == '\0')
            return drv;
    }
    return NULL;
}

struct flash_driver *flash_get_default(void)
{
    return &flash_driver_stm32f1;
}
