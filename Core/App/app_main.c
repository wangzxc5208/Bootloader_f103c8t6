/*
 * app_main.c - Reference application for the bootloader
 *
 * Minimal application that:
 *   1. Configures a simple LED blink (PB12 or similar)
 *   2. Calls boot_mark_success() to tell the bootloader "I'm alive"
 *   3. Enters a main loop
 *
 * This demonstrates the contract between application and bootloader.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#include "stm32f1xx_hal.h"

/* Bootloader interface */
extern void boot_mark_success(void);
extern void boot_request_ota(void);

/* Forward declarations */
static void SystemClock_Config(void);
static void Error_Handler(void);

/**
 * @brief Application entry point
 */
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* Enable GPIOB clock (for LED on PB12) */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Configure PB12 as output (LED) */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /*
     * Critical: tell the bootloader this firmware booted successfully.
     * If this call is NOT made within a few seconds, the bootloader
     * will consider this image invalid and trigger a rollback on
     * the next reset.
     */
    boot_mark_success();

    /* Main loop — blink LED to show we're alive */
    uint32_t tick = 0;
    while (1) {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_12);
        HAL_Delay(500);  /* 500ms blink */
        tick++;

        /* Example: if button held for 5 seconds, request OTA mode */
        if (tick >= 10) {
            tick = 0;
            /* boot_request_ota(); — uncomment to test OTA entry */
        }
    }
}

/**
 * @brief System Clock Configuration (72MHz from HSE+PLL)
 */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

static void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}
