/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Bootloader main program — boot decision + OTA service
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "gpio.h"

/* Bootloader includes */
#include "boot.h"
#include "flash.h"
#include "uart.h"
#include "proto.h"
#include "ota.h"
#include "info_block.h"
#include "version.h"
#include "boot_errno.h"
#include "compiler.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
static struct ota_ctx ota_ctx;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static bool boot_validate_slot(u32 slot);
static void boot_jump_to_app(u32 slot) __noreturn;
static void boot_enter_ota_mode(struct transport *t);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The bootloader entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/
  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();

  /* USER CODE BEGIN 2 */

  /* ── Bootloader initialization ──────────────────────────────────── */

  /* Register and initialize the USART2 transport */
  transport_register(&uart_transport_stm32);
  struct transport *tport = transport_find("usart2");
  if (tport) {
      tport->init(tport);
  }

  /* Register the flash driver */
  flash_driver_register(&flash_driver_stm32f1);

  /* Load the persistent info block */
  struct info_block ib;
  int ret = info_block_init(&ib);
  if (ret != E_OK) {
      /* Corrupted info block — enter OTA mode for recovery */
      if (tport)
          boot_enter_ota_mode(tport);
  }

  /* ── Check if any app is installed ──────────────────────────────── */

  const struct info_block *ib_ptr = info_block_get();
  u32 active_slot = SLOT_A;

  if (ib_ptr) {
      active_slot = ib_ptr->active_slot;
  }

  bool slot_a_ok = boot_validate_slot(SLOT_A);
  bool slot_b_ok = boot_validate_slot(SLOT_B);

  /* If neither slot has a valid app, enter OTA mode immediately.
   * No timeout — just wait forever for the host to send firmware. */
  if (!slot_a_ok && !slot_b_ok) {
      if (tport) {
          /* Signal to host: send a valid "BOT" ACK frame */
          u8 ready[PROTO_MAX_FRAME];
          int rlen = proto_build_frame(0x80, (const u8 *)"BOT", 3, ready);
          if (rlen > 0)
              tport->send(tport, ready, (u32)rlen);
          HAL_Delay(50);
          boot_enter_ota_mode(tport);
      }
      /* No transport — just loop */
      while (1) { HAL_Delay(1000); }
  }

  /* ── Rollback logic ─────────────────────────────────────────────── */

  if (ib_ptr) {
      if (ib_ptr->update_status == INFO_STATUS_TRYING) {
          if (ib_ptr->boot_attempt >= ib_ptr->max_attempts) {
              struct info_block ib_mut = *ib_ptr;
              info_block_trigger_rollback(&ib_mut);
              ib_ptr = info_block_get();
              if (ib_ptr)
                  active_slot = ib_ptr->active_slot;
          } else {
              struct info_block ib_mut = *ib_ptr;
              ib_mut.boot_attempt++;
              info_block_write(&ib_mut);
          }
      } else if (ib_ptr->update_status == INFO_STATUS_UPDATE_DONE) {
          struct info_block ib_mut = *ib_ptr;
          ib_mut.update_status = INFO_STATUS_TRYING;
          ib_mut.boot_attempt = 1;
          info_block_write(&ib_mut);
      }
  }

  /* ── OTA entry check (only if a valid app exists) ───────────────── */

  if (tport) {
      bool enter_ota = ota_enter_check(tport, OTA_ENTER_TIMEOUT_MS);
      if (enter_ota)
          boot_enter_ota_mode(tport);
  }

  /* ── Jump to application ────────────────────────────────────────── */

  boot_jump_to_app(active_slot);

  /* USER CODE END 2 */

  /* Infinite loop — should never reach here */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* Fallback: if we reach here (shouldn't happen), enter OTA mode */
    if (tport) {
        ota_service(&ota_ctx);
    }
  }
  /* USER CODE END 3 */
}

/* ── Boot validation & jump ──────────────────────────────────────── */

/**
 * boot_validate_slot - check whether a slot contains a valid image
 * @slot: SLOT_A or SLOT_B
 *
 * A valid image must have:
 *   1. A valid initial stack pointer (within RAM: 0x20000000-0x20005000)
 *   2. A valid reset vector (within the slot's Flash range)
 *
 * @return: true if the slot appears bootable
 */
static bool boot_validate_slot(u32 slot)
{
    u32 base = slot_base(slot);
    u32 *vector = (u32 *)base;

    u32 sp = vector[0];  /* Initial stack pointer */
    u32 pc = vector[1];  /* Reset handler address */

    /* Stack pointer must point to RAM */
    if (sp < 0x20000000U || sp > 0x20005000U)
        return false;

    /* Reset vector must be within the slot */
    if (pc < base || pc >= (base + SLOT_A_SIZE))
        return false;

    /* PC should be thumb-mode (bit 0 set) */
    if ((pc & 1) == 0)
        return false;

    /* Optionally: check image header magic at APP_HEADER_OFFSET */
    u32 *header = (u32 *)(base + APP_HEADER_OFFSET);
    if (header[0] != APP_HEADER_MAGIC)
        return false;

    return true;
}

/**
 * boot_jump_to_app - jump to the application in a slot
 * @slot: SLOT_A or SLOT_B
 *
 * This function does NOT return.  It:
 *   1. Disables interrupts
 *   2. Sets the MSP to the app's initial SP
 *   3. Sets VTOR to the app's vector table
 *   4. Jumps to the app's reset handler
 */
static void boot_jump_to_app(u32 slot)
{
    u32 base = slot_base(slot);
    u32 *vector = (u32 *)base;

    u32 app_sp = vector[0];
    u32 app_pc = vector[1];

    /* Disable global interrupts */
    __disable_irq();

    /* Disable SysTick */
    SysTick->CTRL = 0;

    /* Relocate the vector table to the application */
    SCB->VTOR = base;

    /* Set the main stack pointer */
    __set_MSP(app_sp);

    /* Jump to the application reset handler */
    /* The cast to function pointer with __attribute__ generates a BLX */
    void (*app_reset)(void) = (void (*)(void))(app_pc);
    app_reset();

    /* Never reached */
    while (1) {}
}

/* ── OTA mode entry ──────────────────────────────────────────────── */

/**
 * boot_enter_ota_mode - run the OTA service loop indefinitely
 * @t: transport to use for OTA communication
 *
 * Runs until a CMD_ACTIVATE or CMD_RESET is received (which triggers
 * a system reset), or until an unrecoverable error occurs.
 */
static void boot_enter_ota_mode(struct transport *t)
{
    /* Initialize OTA context */
    ota_init(&ota_ctx, t);

    /* Main OTA loop */
    while (1) {
        int ret = ota_service(&ota_ctx);

        if (ret < 0 && ret != E_PROTO_TIMEOUT) {
            /* Log the error and continue */
            ota_ctx.last_error = ret;
        }

        /* Small delay to prevent tight looping */
        HAL_Delay(1);
    }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
