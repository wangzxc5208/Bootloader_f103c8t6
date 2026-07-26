/*
 * flash.h - Flash driver interface (Linux kernel OOP style)
 *
 * Defines struct flash_driver as an ops table for Flash operations.
 * Each flash implementation (internal, external SPI flash, etc.)
 * registers itself via this interface.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _FLASH_H
#define _FLASH_H

#include "types.h"
#include "list.h"

/* Forward declaration */
struct flash_driver;

/**
 * struct flash_driver - Flash operations interface
 *
 * Like struct file_operations in the Linux kernel, this provides
 * a vtable of function pointers.  Each flash controller driver
 * fills in the ops it supports and registers via flash_driver_register().
 *
 * All functions return 0 on success or a negative error code (see errno.h).
 */
struct flash_driver {
    const char *name;

    /**
     * @init: one-time hardware initialization
     * @drv: pointer to this driver instance
     */
    int (*init)(struct flash_driver *drv);

    /**
     * @erase: erase a range of flash pages
     * @drv:  pointer to this driver instance
     * @addr: start address (must be page-aligned)
     * @len:  number of bytes to erase (rounded up to page boundary)
     *
     * Returns 0 on success, -EFLASHERASE on failure.
     */
    int (*erase)(struct flash_driver *drv, u32 addr, u32 len);

    /**
     * @write: program data into flash
     * @drv:  pointer to this driver instance
     * @addr: destination address (must be half-word aligned for STM32F1)
     * @data: source data pointer
     * @len:  number of bytes to write
     *
     * The target area must have been erased first.
     * Returns number of bytes written on success, negative error on failure.
     */
    int (*write)(struct flash_driver *drv, u32 addr, const void *data, u32 len);

    /**
     * @read: read data from flash
     * @drv:  pointer to this driver instance
     * @addr: source address
     * @data: destination buffer
     * @len:  number of bytes to read
     *
     * Returns number of bytes read on success, negative error on failure.
     */
    int (*read)(struct flash_driver *drv, u32 addr, void *data, u32 len);

    /**
     * @lock: lock the flash controller (write-protect)
     */
    int (*lock)(struct flash_driver *drv);

    /**
     * @unlock: unlock the flash controller for erase/write
     */
    int (*unlock)(struct flash_driver *drv);

    /**
     * @get_page_size: return the erase page size in bytes
     */
    u32 (*get_page_size)(struct flash_driver *drv);

    /* Private data — driver-specific state */
    void *priv;

    /* Linked into the global flash_drivers list */
    struct list_head list;
};

/* ── Global driver list ──────────────────────────────────────────── */

extern struct list_head flash_drivers;

/**
 * flash_driver_register - register a flash driver
 * @drv: driver to register
 */
int flash_driver_register(struct flash_driver *drv);

/**
 * flash_driver_unregister - unregister a flash driver
 * @drv: driver to unregister
 */
void flash_driver_unregister(struct flash_driver *drv);

/**
 * flash_driver_find - find a registered driver by name
 * @name: driver name
 * @return: pointer to flash_driver or NULL
 */
struct flash_driver *flash_driver_find(const char *name);

/**
 * flash_get_default - return the default (internal) flash driver
 */
struct flash_driver *flash_get_default(void);

/* ── STM32F1 internal flash driver ───────────────────────────────── */

extern struct flash_driver flash_driver_stm32f1;

#endif /* _FLASH_H */
