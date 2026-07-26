/*
 * app_main.c — Reference Application for Bootloader
 *
 * Minimal app that blinks PC13 LED using HAL_Delay.
 * Calls boot_mark_success() to confirm successful boot.
 *
 * Key rule for apps loaded by this bootloader:
 *   1. Link at 0x08004000 (Slot A) with .image_header at offset 0x200
 *   2. Call SystemCoreClockUpdate() BEFORE HAL_Init()
 *   3. Do NOT call SystemClock_Config() — bootloader already set 72MHz
 */

#include "stm32f1xx_hal.h"

extern void boot_mark_success(void);

int main(void)
{
    /* Update clock variable to match bootloader's 72MHz config */
    SystemCoreClockUpdate();
    HAL_Init();

    /* Init GPIO — PC13 as push-pull output, initially LOW (LED ON) */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin   = GPIO_PIN_13;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &g);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

    /* Blink 3 times fast */
    for (int i = 0; i < 3; i++) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(200);
    }

    /* Confirm to bootloader: this firmware booted successfully */
    boot_mark_success();

    /* Blink forever: 500ms on / 500ms off */
    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(500);
    }
}
