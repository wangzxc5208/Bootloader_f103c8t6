/*
 * app_vectors.c - Application vector table (relocated)
 *
 * The bootloader expects the application's vector table at the
 * beginning of the slot (offset 0).  This file provides the
 * minimal vector table (just the stack pointer and reset handler).
 *
 * The application's main() is called with the MSP already set up
 * by the bootloader, but we still need the vector table in the
 * slot for the bootloader to validate and for interrupt handling.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#include <stdint.h>

/* External declarations */
extern void Reset_Handler(void);
extern int main(void);

/*
 * A minimal vector table entry that calls main().
 * The bootloader has already set up the system, so we just
 * need to re-init the application's own peripherals and run main.
 */
void App_Reset_Handler(void)
{
    /*
     * The application needs to call SystemInit() and reinitialize
     * any HAL peripherals it uses.  For simplicity, main() handles
     * this explicitly (HAL_Init, SystemClock_Config, etc.).
     */
    main();

    /* Should never return */
    while (1) {}
}

/*
 * Vector table placed in .isr_vector section.
 * At minimum, the first two entries (SP and PC) must be present.
 */
__attribute__((section(".isr_vector"), used))
const uint32_t app_vector_table[] = {
    /* [0] Initial Stack Pointer — bootloader sets MSP before jump,
     *     so this is informational but must be valid */
    0x20005000,
    /* [1] Reset Handler — the bootloader jumps here (must be thumb mode) */
    (uint32_t)App_Reset_Handler | 1,
};
