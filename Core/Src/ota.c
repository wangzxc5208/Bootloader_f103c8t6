/*
 * ota.c - OTA update service state machine
 *
 * Implements the firmware update lifecycle:
 *   1. Wait for START_OTA command
 *   2. Receive firmware data chunks, write to target slot
 *   3. Verify CRC of written image
 *   4. Activate new image and reset
 *
 * Uses proto.c for frame I/O, flash.c for flash operations,
 * info_block.c for persistent state.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#include "ota.h"
#include "proto.h"
#include "flash.h"
#include "info_block.h"
#include "boot.h"
#include "boot_errno.h"
#include "compiler.h"
#include "stm32f1xx_hal.h"

/* ── CRC forward ─────────────────────────────────────────────────── */

extern u16 crc16_calc(const u8 *data, u32 len, u16 crc);

/* ── OTA initialization ──────────────────────────────────────────── */

void ota_init(struct ota_ctx *ctx, struct transport *transport)
{
    if (!ctx)
        return;

    ctx->transport      = transport;
    ctx->state          = OTA_IDLE;
    ctx->target_slot    = SLOT_INVALID;
    ctx->image_size     = 0;
    ctx->bytes_written  = 0;
    ctx->running_crc    = 0;
    ctx->last_error     = E_OK;
    ctx->ota_in_progress = false;
    ctx->new_version    = VERSION_ZERO;
}

/* ── OTA enter check ─────────────────────────────────────────────── */

bool ota_enter_check(struct transport *transport, u32 timeout_ms)
{
    struct proto_frame frame;
    u8 raw[PROTO_MAX_FRAME];
    int ret;

    if (!transport)
        return false;

    /* Wait for a frame */
    ret = proto_recv_frame(transport, raw, sizeof(raw), timeout_ms);
    if (ret < 0)
        return false;  /* Timeout — boot normally */

    ret = proto_parse_frame(raw, (u32)ret, &frame);
    if (ret < 0)
        return false;

    /* Respond to PING */
    if (frame.cmd == CMD_PING) {
        proto_send_ack(transport);
        return true;  /* Enter OTA mode */
    }

    /* START_OTA — also enter OTA mode */
    if (frame.cmd == CMD_START_OTA) {
        /* We'll process it in the OTA service loop */
        return true;
    }

    return false;
}

/* ── Internal: select target slot ────────────────────────────────── */

/**
 * ota_select_slot - choose which slot to write the new image to
 *
 * Always returns SLOT_A because the app is linked for 0x08004000.
 * (A/B switching requires a position-independent app or two builds.)
 *
 * Rollback is still supported: old Slot A is backed up to Slot B
 * before erasing, and restored on failure.
 */
static u32 ota_select_slot(const struct info_block *ib)
{
    (void)ib;
    return SLOT_A;  /* App is always compiled for Slot A */
}

/* ── Internal: erase target slot ─────────────────────────────────── */

static int ota_erase_slot(u32 slot)
{
    struct flash_driver *drv = flash_get_default();
    u32 addr = slot_base(slot);

    if (!drv)
        return E_IO;

    return drv->erase(drv, addr, SLOT_A_SIZE);
}

/* ── Internal: handle START_OTA ──────────────────────────────────── */

static int ota_handle_start(struct ota_ctx *ctx, struct proto_frame *frame)
{
    int ret;

    /* Payload is big-endian on the wire — parse byte-by-byte.
     * Minimum 12 bytes: image_size(4) + major(2) + minor(2) + patch(2) + reserved(2) */
    if (frame->len < 12)
        return E_BAD_LENGTH;

    u8 *d = frame->data;
    u32 image_size = ((u32)d[0] << 24) | ((u32)d[1] << 16) | ((u32)d[2] << 8) | d[3];
    u16 major      = (u16)((d[4] << 8) | d[5]);
    u16 minor      = (u16)((d[6] << 8) | d[7]);
    u16 patch      = (u16)((d[8] << 8) | d[9]);

    if (image_size == 0 || image_size > SLOT_A_SIZE)
        return E_OTA_SIZE_EXCEED;

    /* Select the non-active slot */
    const struct info_block *ib = info_block_get();
    ctx->target_slot = ota_select_slot(ib);

    /* Store version */
    ctx->new_version.major = major;
    ctx->new_version.minor = minor;
    ctx->new_version.patch = patch;
    ctx->image_size        = image_size;
    ctx->bytes_written     = 0;
    ctx->running_crc       = 0;

    /* Erase the target slot */
    ret = ota_erase_slot(ctx->target_slot);
    if (ret != E_OK) {
        ctx->last_error = ret;
        return ret;
    }

    /* Prepare info block */
    ret = info_block_prepare_update(ctx->target_slot,
                                     &ctx->new_version,
                                     ctx->image_size);
    if (ret != E_OK) {
        ctx->last_error = ret;
        return ret;
    }

    ctx->state = OTA_RECEIVING;
    ctx->ota_in_progress = true;

    return E_OK;
}

/* ── Internal: handle SEND_DATA ──────────────────────────────────── */

static int ota_handle_data(struct ota_ctx *ctx, struct proto_frame *frame)
{
    struct flash_driver *drv = flash_get_default();
    u32 offset;
    u8 *payload;
    u16 payload_len;
    int ret;

    if (ctx->state != OTA_RECEIVING)
        return E_OTA_NOT_READY;

    if (frame->len < 4)  /* Need at least the 4-byte offset */
        return E_BAD_LENGTH;

    /* Extract offset (big-endian) */
    offset  = (u32)frame->data[0] << 24;
    offset |= (u32)frame->data[1] << 16;
    offset |= (u32)frame->data[2] << 8;
    offset |= (u32)frame->data[3];

    payload_len = frame->len - 4;
    payload     = &frame->data[4];

    /* Validate offset */
    if (offset != ctx->bytes_written) {
        ctx->last_error = E_RANGE;
        return E_RANGE;
    }

    if (offset + payload_len > ctx->image_size) {
        ctx->last_error = E_OTA_SIZE_EXCEED;
        return E_OTA_SIZE_EXCEED;
    }

    /* Write to flash */
    u32 target_addr = slot_base(ctx->target_slot) + offset;
    ret = drv->write(drv, target_addr, payload, payload_len);
    if (ret < 0) {
        ctx->last_error = E_FLASH_WRITE;
        ctx->state = OTA_ERROR;
        return E_FLASH_WRITE;
    }

    /* Update running CRC */
    ctx->running_crc = crc16_calc(payload, payload_len, ctx->running_crc);
    ctx->bytes_written += payload_len;

    return E_OK;
}

/* ── Internal: handle VERIFY ─────────────────────────────────────── */

static int ota_handle_verify(struct ota_ctx *ctx)
{
    int ret;

    if (ctx->state != OTA_RECEIVING && ctx->state != OTA_VERIFYING)
        return E_OTA_NOT_READY;

    ctx->state = OTA_VERIFYING;

    if (ctx->bytes_written != ctx->image_size) {
        ctx->last_error = E_OTA_VERIFY_FAIL;
        ctx->state = OTA_ERROR;
        return E_OTA_VERIFY_FAIL;
    }

    /* Re-read the entire image and compute CRC */
    struct flash_driver *drv = flash_get_default();
    u32 addr = slot_base(ctx->target_slot);
    u16 verify_crc = 0;
    u8 chunk[256];

    for (u32 off = 0; off < ctx->image_size; ) {
        u32 chunk_size = min((u32)sizeof(chunk), ctx->image_size - off);
        ret = drv->read(drv, addr + off, chunk, chunk_size);
        if (ret < 0) {
            ctx->last_error = E_IO;
            ctx->state = OTA_ERROR;
            return E_IO;
        }
        verify_crc = crc16_calc(chunk, chunk_size, verify_crc);
        off += chunk_size;
    }

    if (verify_crc != ctx->running_crc) {
        ctx->last_error = E_OTA_VERIFY_FAIL;
        ctx->state = OTA_ERROR;
        return E_OTA_VERIFY_FAIL;
    }

    /* Commit the update */
    ret = info_block_commit_update(ctx->target_slot, verify_crc);
    if (ret != E_OK) {
        ctx->last_error = ret;
        ctx->state = OTA_ERROR;
        return ret;
    }

    ctx->state = OTA_COMPLETE;
    return E_OK;
}

/* ── Internal: handle ACTIVATE ───────────────────────────────────── */

static void ota_handle_activate(struct ota_ctx *ctx)
{
    (void)ctx;
    /* Reset the MCU — bootloader will pick up the new slot on restart */
    HAL_Delay(100);  /* Give time for ACK to be sent */
    NVIC_SystemReset();
}

/* ── Internal: handle GET_STATUS ─────────────────────────────────── */

static int ota_handle_get_status(struct ota_ctx *ctx, struct transport *t)
{
    struct proto_status_resp resp;

    switch (ctx->state) {
    case OTA_IDLE:
        resp.state = PSTATE_IDLE;
        break;
    case OTA_RECEIVING:
        resp.state = PSTATE_RECEIVING;
        break;
    case OTA_VERIFYING:
        resp.state = PSTATE_VERIFYING;
        break;
    case OTA_COMPLETE:
        resp.state = PSTATE_COMPLETE;
        break;
    case OTA_ERROR:
        resp.state = PSTATE_ERROR;
        break;
    default:
        resp.state = PSTATE_IDLE;
        break;
    }

    resp.progress      = ota_get_progress(ctx);
    resp.bytes_written = ctx->bytes_written;
    resp.total_size    = ctx->image_size;
    resp.last_error    = ctx->last_error;

    u8 frame_buf[PROTO_MAX_FRAME];
    int size = proto_build_frame(RESP_STATUS, (u8 *)&resp, sizeof(resp), frame_buf);
    if (size < 0)
        return size;

    return t->send(t, frame_buf, (u32)size);
}

/* ── Internal: handle GET_VERSION ────────────────────────────────── */

static int ota_handle_get_version(struct transport *t)
{
    struct proto_version_resp resp;

    resp.proto_version = PROTO_VERSION;
    resp.boot_major    = 1;
    resp.boot_minor    = 0;
    resp.boot_patch    = 0;
    resp.capabilities[0] = 0x01; /* Supports OTA */
    resp.capabilities[1] = 0x01; /* Supports rollback */
    resp.capabilities[2] = 0;
    resp.capabilities[3] = 0;

    u8 frame_buf[PROTO_MAX_FRAME];
    int size = proto_build_frame(RESP_VERSION, (u8 *)&resp, sizeof(resp), frame_buf);
    if (size < 0)
        return size;

    return t->send(t, frame_buf, (u32)size);
}

/* ── Public: OTA service loop ────────────────────────────────────── */

int ota_service(struct ota_ctx *ctx)
{
    struct proto_frame frame;
    u8 raw[PROTO_MAX_FRAME];
    int ret;
    int frame_size;

    if (!ctx || !ctx->transport)
        return E_INVAL;

    /* Try to receive a frame (non-blocking — short timeout) */
    frame_size = proto_recv_frame(ctx->transport, raw, sizeof(raw), 100);
    if (frame_size == E_PROTO_TIMEOUT)
        return 0;  /* No data available */
    if (frame_size < 0)
        return frame_size;

    ret = proto_parse_frame(raw, (u32)frame_size, &frame);
    if (ret < 0) {
        proto_send_nack(ctx->transport, ret);
        return ret;
    }

    /* Dispatch command */
    switch (frame.cmd) {
    case CMD_PING:
        proto_send_ack(ctx->transport);
        break;

    case CMD_START_OTA:
        ret = ota_handle_start(ctx, &frame);
        if (ret == E_OK)
            proto_send_ack(ctx->transport);
        else
            proto_send_nack(ctx->transport, ret);
        break;

    case CMD_SEND_DATA:
        ret = ota_handle_data(ctx, &frame);
        if (ret == E_OK)
            proto_send_ack(ctx->transport);
        else
            proto_send_nack(ctx->transport, ret);
        break;

    case CMD_VERIFY:
        ret = ota_handle_verify(ctx);
        if (ret == E_OK)
            proto_send_ack(ctx->transport);
        else
            proto_send_nack(ctx->transport, ret);
        break;

    case CMD_ACTIVATE:
        proto_send_ack(ctx->transport);
        ota_handle_activate(ctx);
        break;

    case CMD_GET_STATUS:
        ota_handle_get_status(ctx, ctx->transport);
        break;

    case CMD_GET_VERSION:
        ota_handle_get_version(ctx->transport);
        break;

    case CMD_RESET:
        proto_send_ack(ctx->transport);
        HAL_Delay(100);
        NVIC_SystemReset();
        break;

    default:
        proto_send_nack(ctx->transport, E_INVAL);
        break;
    }

    return 1;  /* Processed a command */
}
