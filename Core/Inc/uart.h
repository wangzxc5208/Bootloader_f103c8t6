/*
 * uart.h - UART transport interface (Linux kernel OOP style)
 *
 * Defines struct transport as an abstraction over UART/serial links.
 * This allows swapping USART1, USART2, etc. without changing the
 * protocol layer.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _UART_H
#define _UART_H

#include "types.h"
#include "list.h"

/**
 * struct transport - Serial transport operations interface
 *
 * Abstracts a byte-oriented serial link.  Frame-level protocol
 * code (proto.c) works through this interface and is transport-agnostic.
 */
struct transport {
    const char *name;

    /**
     * @init: initialize the transport hardware
     * @t: pointer to this transport instance
     * Returns 0 on success, negative error on failure.
     */
    int (*init)(struct transport *t);

    /**
     * @deinit: deinitialize the transport hardware
     * @t: pointer to this transport instance
     */
    void (*deinit)(struct transport *t);

    /**
     * @send_byte: send a single byte
     * @t: pointer to this transport instance
     * @byte: the byte to send
     * Returns 0 on success, negative error on failure.
     */
    int (*send_byte)(struct transport *t, u8 byte);

    /**
     * @recv_byte: receive a single byte (blocking with timeout)
     * @t: pointer to this transport instance
     * @byte: output — the received byte
     * @timeout_ms: maximum wait time in milliseconds
     * Returns 0 on success, -ETIMEDOUT on timeout, negative error on failure.
     */
    int (*recv_byte)(struct transport *t, u8 *byte, u32 timeout_ms);

    /**
     * @send: send a buffer of bytes
     * @t: pointer to this transport instance
     * @data: data to send
     * @len: number of bytes
     * Returns number of bytes sent or negative error.
     */
    int (*send)(struct transport *t, const u8 *data, u32 len);

    /**
     * @recv: receive a buffer of bytes (blocking with timeout)
     * @t: pointer to this transport instance
     * @buf: destination buffer
     * @len: maximum number of bytes to receive
     * @timeout_ms: maximum wait time
     * Returns number of bytes received or negative error.
     */
    int (*recv)(struct transport *t, u8 *buf, u32 len, u32 timeout_ms);

    /**
     * @flush: flush any pending RX data
     * @t: pointer to this transport instance
     */
    void (*flush)(struct transport *t);

    /**
     * @set_baudrate: change the baudrate at runtime
     * @t: pointer to this transport instance
     * @baudrate: new baud rate
     * Returns 0 on success, negative error on failure.
     */
    int (*set_baudrate)(struct transport *t, u32 baudrate);

    /* Private data — driver-specific state (USART handle, etc.) */
    void *priv;

    /* Linked into the global transports list */
    struct list_head list;
};

/* ── Global transport list ───────────────────────────────────────── */

extern struct list_head transports;

/**
 * transport_register - register a transport driver
 * @t: transport to register
 */
int transport_register(struct transport *t);

/**
 * transport_unregister - unregister a transport driver
 * @t: transport to unregister
 */
void transport_unregister(struct transport *t);

/**
 * transport_find - find a registered transport by name
 * @name: transport name
 * @return: pointer to transport or NULL
 */
struct transport *transport_find(const char *name);

/* ── USART2 transport ────────────────────────────────────────────── */

extern struct transport uart_transport_stm32;

#endif /* _UART_H */
