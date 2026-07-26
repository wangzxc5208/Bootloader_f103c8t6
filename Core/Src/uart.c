/*
 * uart.c - USART2 transport driver (Linux kernel OOP style)
 *
 * Implements struct transport ops using STM32F1 HAL UART API
 * over USART2 (PA2=TX, PA3=RX, 115200 8N1).
 *
 * The UART_HandleTypeDef is stored as the driver's private data.
 * Interrupt-based RX with a ring buffer for reliable reception.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#include "uart.h"
#include "boot.h"
#include "boot_errno.h"
#include "compiler.h"
#include "stm32f1xx_hal.h"
#include "main.h"

/* ── Private data ────────────────────────────────────────────────── */

#define UART_RX_BUF_SIZE  256

struct uart_priv {
    UART_HandleTypeDef  handle;
    volatile u8         rx_byte;    /* Single-byte RX buffer for HAL ISR */
    u8                  rx_buf[UART_RX_BUF_SIZE];
    volatile u32        rx_head;    /* ISR writes here */
    u32                 rx_tail;    /* Consumer reads here */
    volatile bool       rx_overflow;
};

/* The ACTIVE UART handle — shared between ISR and driver.
 * Set by uart_init before any UART operations, used by
 * USART2_IRQHandler() in stm32f1xx_it.c */
UART_HandleTypeDef *g_huart2;

/* Static pointer to transport — used by HAL_UART_RxCpltCallback */
static struct transport *g_uart2_transport;

/* ── Ring buffer helpers ─────────────────────────────────────────── */

static inline bool rx_buf_empty(struct uart_priv *p)
{
    return p->rx_head == p->rx_tail;
}

static inline u32 rx_buf_count(struct uart_priv *p)
{
    return (p->rx_head - p->rx_tail) & (UART_RX_BUF_SIZE - 1);
}

static inline u32 rx_buf_space(struct uart_priv *p)
{
    return UART_RX_BUF_SIZE - 1 - rx_buf_count(p);
}

static u8 rx_buf_get(struct uart_priv *p)
{
    u8 byte = p->rx_buf[p->rx_tail];
    p->rx_tail = (p->rx_tail + 1) & (UART_RX_BUF_SIZE - 1);
    return byte;
}

static void rx_buf_put_isr(struct uart_priv *p, u8 byte)
{
    u32 head = p->rx_head;
    u32 next = (head + 1) & (UART_RX_BUF_SIZE - 1);
    if (next == p->rx_tail) {
        p->rx_overflow = true;
        return;
    }
    p->rx_buf[head] = byte;
    p->rx_head = next;
}

/* ── ISR callback ────────────────────────────────────────────────── */

/**
 * HAL_UART_RxCpltCallback - called by HAL when the specified number
 * of bytes has been received (here: 1 byte).  We push the byte into
 * our ring buffer and re-arm the RX interrupt for the next byte.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle)
{
    if (g_uart2_transport && handle == g_huart2) {
        struct uart_priv *p = g_uart2_transport->priv;
        rx_buf_put_isr(p, p->rx_byte);
        /* Re-arm for next byte */
        HAL_UART_Receive_IT(handle, (u8 *)&p->rx_byte, 1);
    }
}

/* ── HAL MSP callbacks ──────────────────────────────────────────── */

/**
 * HAL_UART_MspInit - low-level UART hardware init (clock enable)
 *
 * Called by HAL_UART_Init().  The GPIO pins (PA2/PA3) are already
 * configured in MX_GPIO_Init().
 */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        __HAL_RCC_USART2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        /* USART2 interrupt */
        HAL_NVIC_SetPriority(USART2_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
    }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        __HAL_RCC_USART2_CLK_DISABLE();
    }
}

/* ── Transport ops ───────────────────────────────────────────────── */

static int uart_init(struct transport *t)
{
    struct uart_priv *p = t->priv;

    p->rx_head = 0;
    p->rx_tail = 0;
    p->rx_overflow = false;

    UART_HandleTypeDef *h = &p->handle;
    h->Instance        = USART2;
    h->Init.BaudRate   = BOOT_UART_BAUDRATE;
    h->Init.WordLength = BOOT_UART_DATABITS;
    h->Init.StopBits   = BOOT_UART_STOPBITS;
    h->Init.Parity     = BOOT_UART_PARITY;
    h->Init.Mode       = UART_MODE_TX_RX;
    h->Init.HwFlowCtl  = BOOT_UART_FLOWCTRL;
    h->Init.OverSampling = UART_OVERSAMPLING_16;

    /* Store transport & handle pointer for ISR access */
    g_uart2_transport = t;
    g_huart2 = h;           /* Use the SAME handle for ISR and driver */

    if (HAL_UART_Init(h) != HAL_OK)
        return E_IO;

    /* Start interrupt-based RX (one byte at a time) */
    HAL_UART_Receive_IT(h, (u8 *)&p->rx_byte, 1);

    return E_OK;
}

static void uart_deinit(struct transport *t)
{
    struct uart_priv *p = t->priv;
    HAL_UART_DeInit(&p->handle);
}

static int uart_send_byte(struct transport *t, u8 byte)
{
    struct uart_priv *p = t->priv;
    if (HAL_UART_Transmit(&p->handle, &byte, 1, 100) != HAL_OK)
        return E_IO;
    return E_OK;
}

static int uart_recv_byte(struct transport *t, u8 *byte, u32 timeout_ms)
{
    struct uart_priv *p = t->priv;
    u32 waited = 0;

    while (rx_buf_empty(p)) {
        if (timeout_ms > 0 && waited >= timeout_ms)
            return E_TIMEDOUT;
        /* Busy-wait with 1ms granularity */
        HAL_Delay(1);
        waited++;
    }

    __disable_irq();
    if (rx_buf_empty(p)) {
        __enable_irq();
        return E_IO;
    }
    *byte = rx_buf_get(p);
    __enable_irq();
    return E_OK;
}

static int uart_send(struct transport *t, const u8 *data, u32 len)
{
    struct uart_priv *p = t->priv;
    /* HAL_UART_Transmit blocks until done; we add a per-byte timeout margin */
    u32 timeout = len * 2 + 100;  /* generous margin */
    HAL_StatusTypeDef status = HAL_UART_Transmit(&p->handle, (u8 *)data,
                                                  (u16)len, (u32)timeout);
    if (status != HAL_OK)
        return E_IO;
    return (int)len;
}

static int uart_recv(struct transport *t, u8 *buf, u32 len, u32 timeout_ms)
{
    u32 received = 0;
    u32 waited = 0;

    while (received < len) {
        int ret = uart_recv_byte(t, &buf[received], 1);
        if (ret == E_OK) {
            received++;
            waited = 0;  /* reset timeout on each byte */
        } else if (ret == E_TIMEDOUT) {
            if (received > 0 && waited < timeout_ms) {
                waited++;
                continue;
            }
            break;
        } else {
            return E_IO;
        }
    }

    return (int)received;
}

static void uart_flush(struct transport *t)
{
    struct uart_priv *p = t->priv;
    __disable_irq();
    p->rx_head = 0;
    p->rx_tail = 0;
    p->rx_overflow = false;
    __enable_irq();
}

static int uart_set_baudrate(struct transport *t, u32 baudrate)
{
    struct uart_priv *p = t->priv;
    p->handle.Init.BaudRate = baudrate;
    if (HAL_UART_Init(&p->handle) != HAL_OK)
        return E_IO;
    return E_OK;
}

/* ── Driver instance ─────────────────────────────────────────────── */

static struct uart_priv uart2_priv;

struct transport uart_transport_stm32 = {
    .name       = "usart2",
    .init       = uart_init,
    .deinit     = uart_deinit,
    .send_byte  = uart_send_byte,
    .recv_byte  = uart_recv_byte,
    .send       = uart_send,
    .recv       = uart_recv,
    .flush      = uart_flush,
    .set_baudrate = uart_set_baudrate,
    .priv       = &uart2_priv,
    .list       = LIST_HEAD_INIT(uart_transport_stm32.list),
};

/* ── Global transport list ───────────────────────────────────────── */

LIST_HEAD(transports);

int transport_register(struct transport *t)
{
    if (!t || !t->name)
        return E_INVAL;
    list_add_tail(&t->list, &transports);
    return E_OK;
}

void transport_unregister(struct transport *t)
{
    if (t)
        list_del_init(&t->list);
}

struct transport *transport_find(const char *name)
{
    struct transport *t;
    list_for_each_entry(t, &transports, list) {
        /* Simple string compare — we know the strings are literals */
        const char *a = t->name;
        const char *b = name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == '\0' && *b == '\0')
            return t;
    }
    return NULL;
}
