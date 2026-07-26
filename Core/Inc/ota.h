/*
 * ota.h - OTA update service (high-level state machine)
 *
 * Manages the firmware update lifecycle:
 *   IDLE → RECEIVING → VERIFYING → COMPLETE → (activate) → RUNNING
 *
 * Uses proto.c for frame-level protocol and info_block.c for
 * persistent state management.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _OTA_H
#define _OTA_H

#include "types.h"
#include "proto.h"
#include "uart.h"
#include "version.h"

/* ── OTA state machine states ────────────────────────────────────── */

enum ota_state {
    OTA_IDLE           = 0,
    OTA_RECEIVING      = 1,
    OTA_VERIFYING      = 2,
    OTA_COMPLETE       = 3,
    OTA_ERROR          = 4,
};

/* ── OTA context ─────────────────────────────────────────────────── */

/**
 * struct ota_ctx - OTA update context
 *
 * Tracks the state of an in-progress firmware update.
 * Allocated by the caller (typically on the stack of the main loop).
 */
struct ota_ctx {
    struct transport    *transport;     /* Bound transport (e.g., USART2) */
    enum ota_state       state;         /* Current FSM state */
    u32                  target_slot;   /* Which slot to write to */
    u32                  image_size;    /* Total expected image size */
    u32                  bytes_written; /* Bytes written so far */
    struct version       new_version;   /* Version of the new image */
    u16                  running_crc;   /* Running CRC-16 of the image */
    u32                  last_error;    /* Last error code */
    bool                 ota_in_progress;
};

/* ── OTA operations ──────────────────────────────────────────────── */

/**
 * ota_init - initialize the OTA context
 * @ctx:       OTA context to initialize
 * @transport: bound transport for communication
 */
void ota_init(struct ota_ctx *ctx, struct transport *transport);

/**
 * ota_service - run one iteration of the OTA state machine
 * @ctx: OTA context
 *
 * Called repeatedly from the main loop when in OTA mode.
 * Processes incoming commands and advances the state machine.
 *
 * @return: 0 if idle (no command received), >0 if a command was
 *          processed, negative on error
 */
int ota_service(struct ota_ctx *ctx);

/**
 * ota_enter_check - check if we should enter OTA mode
 * @transport: transport to listen on
 * @timeout_ms: how long to wait for an OTA command
 *
 * Called at boot time.  Waits for a PING or START_OTA command on
 * the transport.  Returns true if OTA was requested.
 *
 * @return: true if we should enter OTA mode, false to boot normally
 */
bool ota_enter_check(struct transport *transport, u32 timeout_ms);

/**
 * ota_get_progress - get the current progress as a percentage
 * @ctx: OTA context
 * @return: 0-100 progress percentage
 */
static inline u8 ota_get_progress(const struct ota_ctx *ctx)
{
    if (ctx->image_size == 0)
        return 0;
    return (u8)((ctx->bytes_written * 100U) / ctx->image_size);
}

#endif /* _OTA_H */
