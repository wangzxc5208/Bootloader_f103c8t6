/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Simplified USART Bootloader — download and jump
  *
  * Flash layout (STM32F103C8T6, 64KB):
  *   0x08000000  Bootloader      16 KB
  *   0x08004000  Application     48 KB  (single slot)
  *
  * Boot flow:
  *   1. Init USART2 + Flash driver
  *   2. Validate App (SP in RAM / PC in App range / thumb / magic)
  *   3. If valid → wait 3s for OTA command, else enter OTA mode
  *   4. Jump to App with clean environment
  *
  * Jump sequence (thorough cleanup):
  *   1. Disable all interrupts
  *   2. Disable SysTick
  *   3. Clear all NVIC enables and pending
  *   4. Reset all enabled peripherals via RCC, disable peripheral clocks
  *   5. Reset system clock to HSI (8 MHz)
  *   6. Set VTOR to App base
  *   7. Set MSP from App vector table
  *   8. Set CONTROL register (privileged thread mode, MSP)
  *   9. Memory barriers → jump to App Reset_Handler
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"
#include "gpio.h"
#include "boot.h"
#include "flash.h"
#include "uart.h"
#include "proto.h"
#include "ota.h"
#include "boot_errno.h"
#include "compiler.h"

static struct ota_ctx ota_ctx;

void SystemClock_Config(void);

/* ── Forward declarations ────────────────────────────────────────── */

static bool  boot_validate_app(void);
static void  boot_jump_to_app(void) __noreturn;
static void  boot_enter_ota_mode(struct transport *t);

/* ═══════════════════════════════════════════════════════════════════
 *  main
 * ═══════════════════════════════════════════════════════════════════ */

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();

    /* ── Init transport + flash ────────────────────────────────── */
    transport_register(&uart_transport_stm32);
    struct transport *tport = transport_find("usart2");
    if (tport)
        tport->init(tport);

    flash_driver_register(&flash_driver_stm32f1);

    /* ── Validate app ──────────────────────────────────────────── */
    bool app_valid = boot_validate_app();

    if (app_valid) {
        /*
         * App looks good.  Give the host 3 seconds to send an OTA
         * command (PING or START_OTA).  If none arrives, boot the app.
         */
        if (ota_enter_check(tport, OTA_ENTER_TIMEOUT_MS))
            boot_enter_ota_mode(tport);
    } else {
        /* No valid app — stay in OTA mode forever */
        if (tport)
            boot_enter_ota_mode(tport);
        while (1) {}
    }

    /* ── Jump to application ───────────────────────────────────── */
    boot_jump_to_app();
}

/* ═══════════════════════════════════════════════════════════════════
 *  App validation
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * boot_validate_app - check if a valid application image is present
 *
 * Checks three conditions (all must pass):
 *   1. Initial SP points into RAM (0x20000000–0x20005000)
 *   2. Reset vector (PC) is within the App flash range
 *   3. Cortex-M3 thumb mode (PC bit 0 = 1)
 *
 * No magic number check — any firmware with a valid vector table
 * at APP_BASE will boot.
 *
 * @return: true if the app image passes all checks
 */
static bool boot_validate_app(void)
{
    u32 *v  = (u32 *)APP_BASE;
    u32  sp = v[0];
    u32  pc = v[1];

    /* Stack pointer must target RAM */
    if (sp < 0x20000000U || sp > 0x20005000U)
        return false;

    /* Reset vector must be within the application area */
    if (pc < APP_BASE || pc >= APP_END)
        return false;

    /* Cortex-M3 requires thumb mode (bit 0 = 1) */
    if ((pc & 1) == 0)
        return false;

    return true;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Jump to application — clean environment
 * ═══════════════════════════════════════════════════════════════════ */

/**
 * boot_jump_to_app - jump to the application with a clean environment
 *
 * Cleanup sequence before the jump:
 *   1. Disable all interrupts
 *   2. Reset RCC to HSI 8 MHz (disables PLL, HSE, clears clock config)
 *   3. Disable all NVIC interrupt-enable bits
 *   4. Disable SysTick
 *   5. Clear all NVIC pending interrupt bits
 *   6. Set VTOR to the application's vector table
 *   7. Set MSP to the application's initial stack pointer
 *   8. Set CONTROL = 0 (privileged thread mode, use MSP)
 *   9. Memory barriers — then jump to App Reset_Handler
 *
 * After this call the bootloader never returns.
 */
static void __noreturn boot_jump_to_app(void)
{
    u32 *v     = (u32 *)APP_BASE;
    u32 app_sp = v[0];
    u32 app_pc = v[1];

    /* ── 1. Disable all interrupts ──────────────────────────────── */
    __disable_irq();

    /* ── 2. Reset RCC — switch to HSI 8 MHz, disable PLL/HSE ────── */
    RCC->CR |= RCC_CR_HSION;                                   /* Enable HSI         */
    while (!(RCC->CR & RCC_CR_HSIRDY));                         /* Wait HSI ready     */
    RCC->CFGR = 0;                                              /* HSI as SYSCLK,     */
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI);    /*   dividers /1      */
    RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_HSEON);                 /* Disable PLL & HSE  */
    RCC->CIR = 0;                                               /* Clear clock ints   */

    /* ── 3. Disable all NVIC interrupts ─────────────────────────── */
    for (u32 i = 0; i < 8U; i++)
        NVIC->ICER[i] = 0xFFFFFFFFU;

    /* ── 4. Disable SysTick ─────────────────────────────────────── */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* ── 5. Clear all pending interrupts ────────────────────────── */
    for (u32 i = 0; i < 8U; i++)
        NVIC->ICPR[i] = 0xFFFFFFFFU;

    /* Bootloader doesn't use DMA — no DMA cleanup needed */

    /* ── 6. Set vector table offset to App base ─────────────────── */
    SCB->VTOR = APP_BASE;

    /* ── 7. Set Main Stack Pointer from App vector table ────────── */
    __set_MSP(app_sp);

    /* ── 8. CONTROL register: privileged thread mode, use MSP ───── */
    __set_CONTROL(0);

    /* ── 9. Full synchronization barriers ───────────────────────── */
    __DSB();
    __ISB();

    /*
     * 10. Re-enable interrupts.
     *
     * At this point all NVIC IRQs are disabled (ICER) and SysTick is off
     * (CTRL=0), so no interrupt source is actually active.  But PRIMASK
     * must be 0 before the jump so the app's HAL_Delay / SysTick work.
     * SysTick is a system exception — it bypasses NVIC ICER and is gated
     * by PRIMASK alone.  If PRIMASK stays at 1, uwTick never increments
     * and HAL_Delay() hangs forever.
     */
    __enable_irq();

    /* ── 11. Jump to application Reset_Handler ──────────────────── */
    void (*app_reset_handler)(void) = (void (*)(void))(app_pc);
    app_reset_handler();

    /* Never reach here */
    while (1) {}
}

/* ═══════════════════════════════════════════════════════════════════
 *  OTA mode — run until reset
 * ═══════════════════════════════════════════════════════════════════ */

static void boot_enter_ota_mode(struct transport *t)
{
    ota_init(&ota_ctx, t);
    while (1) {
        int ret = ota_service(&ota_ctx);
        if (ret < 0 && ret != E_PROTO_TIMEOUT)
            ota_ctx.last_error = (u32)(-ret);
        HAL_Delay(1);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  Clock: HSE 8MHz → PLL ×9 → 72MHz
 * ═══════════════════════════════════════════════════════════════════ */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.HSIState       = RCC_HSI_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL     = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        Error_Handler();

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK)
        Error_Handler();
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}
