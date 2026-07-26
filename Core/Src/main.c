/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : OTA Bootloader — boot decision + firmware update over USART2
  *
  * Flash layout (STM32F103C8T6, 64KB):
  *   0x08000000  Bootloader      16 KB
  *   0x08004000  App Slot A      20 KB
  *   0x08009000  App Slot B      20 KB  (reserved for rollback)
  *   0x0800E000  Info Block       4 KB  (dual-copy, wear-leveled)
  *
  * Boot flow:
  *   1. Init USART2 + Flash driver
  *   2. Load info block, determine active slot
  *   3. Validate slot (SP/PC in range, magic at offset 0x200)
  *   4. If no valid app → OTA mode; else → rollback check → jump
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
#include "info_block.h"
#include "boot_errno.h"
#include "compiler.h"

static struct ota_ctx ota_ctx;

void SystemClock_Config(void);
static bool boot_validate_slot(u32 slot);
static void boot_jump_to_app(u32 slot) __noreturn;
static void boot_enter_ota_mode(struct transport *t);

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();

  /* ── Init transport + flash ──────────────────────────────────── */
  transport_register(&uart_transport_stm32);
  struct transport *tport = transport_find("usart2");
  if (tport) tport->init(tport);

  flash_driver_register(&flash_driver_stm32f1);

  /* ── Load persistent state ───────────────────────────────────── */
  struct info_block ib;
  if (info_block_init(&ib) != E_OK) {
      if (tport) boot_enter_ota_mode(tport);
      while (1) {}
  }

  const struct info_block *ib_ptr = info_block_get();
  u32 active_slot = ib_ptr ? ib_ptr->active_slot : SLOT_A;

  bool a_ok = boot_validate_slot(SLOT_A);
  bool b_ok = boot_validate_slot(SLOT_B);

  /* No valid app → OTA mode */
  if (!a_ok && !b_ok) {
      if (tport) boot_enter_ota_mode(tport);
      while (1) {}
  }

  /* ── Rollback / retry logic ──────────────────────────────────── */
  if (ib_ptr) {
      u32 st = ib_ptr->update_status;
      if (st == INFO_STATUS_TRYING) {
          if (ib_ptr->boot_attempt >= ib_ptr->max_attempts) {
              struct info_block mut = *ib_ptr;
              info_block_trigger_rollback(&mut);
              ib_ptr = info_block_get();
              if (ib_ptr) active_slot = ib_ptr->active_slot;
          } else {
              struct info_block mut = *ib_ptr;
              mut.boot_attempt++;
              info_block_write(&mut);
          }
      } else if (st == INFO_STATUS_UPDATE_DONE) {
          struct info_block mut = *ib_ptr;
          mut.update_status = INFO_STATUS_TRYING;
          mut.boot_attempt = 1;
          info_block_write(&mut);
      }
  }

  /* ── OTA entry window (3s) ───────────────────────────────────── */
  if (tport) {
      if (ota_enter_check(tport, OTA_ENTER_TIMEOUT_MS))
          boot_enter_ota_mode(tport);
  }

  /* ── Jump to app ─────────────────────────────────────────────── */
  boot_jump_to_app(active_slot);
}

/* ── Slot validation ─────────────────────────────────────────────── */

static bool boot_validate_slot(u32 slot)
{
    u32 base = slot_base(slot);
    u32 *v   = (u32 *)base;
    u32 sp   = v[0];
    u32 pc   = v[1];

    /* Stack pointer must target RAM */
    if (sp < 0x20000000U || sp > 0x20005000U) return false;
    /* Reset vector must be within this slot */
    if (pc < base || pc >= (base + SLOT_A_SIZE)) return false;
    /* Cortex-M3 requires thumb mode (bit 0 = 1) */
    if ((pc & 1) == 0) return false;
    /* Image header magic at offset 0x200 */
    u32 *hdr = (u32 *)(base + APP_HEADER_OFFSET);
    if (hdr[0] != APP_HEADER_MAGIC) return false;

    return true;
}

/* ── Jump to application ─────────────────────────────────────────── */

static void boot_jump_to_app(u32 slot)
{
    u32 base   = slot_base(slot);
    u32 *v     = (u32 *)base;
    u32 app_sp = v[0];
    u32 app_pc = v[1];

    __disable_irq();
    SysTick->CTRL = 0;
    SCB->VTOR = base;
    __set_MSP(app_sp);

    void (*reset)(void) = (void (*)(void))(app_pc);
    reset();
    while (1) {}
}

/* ── OTA mode ────────────────────────────────────────────────────── */

static void boot_enter_ota_mode(struct transport *t)
{
    ota_init(&ota_ctx, t);
    while (1) {
        int ret = ota_service(&ota_ctx);
        if (ret < 0 && ret != E_PROTO_TIMEOUT)
            ota_ctx.last_error = ret;
        HAL_Delay(1);
    }
}

/* ── Clock: HSE 8MHz → PLL ×9 → 72MHz ───────────────────────────── */

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
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

  clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV2;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

void Error_Handler(void) { __disable_irq(); while (1) {} }
