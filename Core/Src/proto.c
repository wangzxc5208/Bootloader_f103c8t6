/*
 * proto.c - OTA protocol frame encoding/decoding
 *
 * Implements the frame-level protocol: sync detection, CRC-16
 * verification, frame building and parsing.
 *
 * Wire format (big-endian):
 *   SYNC0 | SYNC1 | CMD | LEN_MSB | LEN_LSB | DATA[0..LEN-1] | CRC_MSB | CRC_LSB
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#include "proto.h"
#include "uart.h"
#include "boot_errno.h"
#include "compiler.h"

/* ── CRC-16 forward ──────────────────────────────────────────────── */

extern u16 crc16_calc(const u8 *data, u32 len, u16 crc);

/* ── Frame building ──────────────────────────────────────────────── */

int proto_build_frame(u8 cmd, const u8 *data, u16 len, u8 *out)
{
    u16 crc;
    u32 idx = 0;

    if (len > PROTO_MAX_PAYLOAD)
        return E_BAD_LENGTH;

    /* Sync bytes */
    out[idx++] = PROTO_SYNC0;
    out[idx++] = PROTO_SYNC1;

    /* Command */
    out[idx++] = cmd;

    /* Length (big-endian) */
    out[idx++] = (u8)((len >> 8) & 0xFF);
    out[idx++] = (u8)(len & 0xFF);

    /* Payload */
    if (data && len > 0) {
        for (u16 i = 0; i < len; i++)
            out[idx++] = data[i];
    }

    /* CRC-16 over cmd + len + data (excludes sync bytes) */
    crc = crc16_calc(&out[2], (u32)(idx - 2), 0x0000);

    /* CRC appended big-endian */
    out[idx++] = (u8)((crc >> 8) & 0xFF);
    out[idx++] = (u8)(crc & 0xFF);

    return (int)idx;
}

/* ── Frame parsing ───────────────────────────────────────────────── */

int proto_parse_frame(const u8 *buf, u32 size, struct proto_frame *frame)
{
    if (size < PROTO_HDR_SIZE + PROTO_CRC_SIZE)
        return E_BAD_LENGTH;

    /* Check sync bytes */
    if (buf[0] != PROTO_SYNC0 || buf[1] != PROTO_SYNC1)
        return E_PROTO_SYNC;

    /* Extract fields */
    frame->cmd = buf[2];
    frame->len = (u16)((buf[3] << 8) | buf[4]);

    u32 total = PROTO_HDR_SIZE + frame->len + PROTO_CRC_SIZE;
    if (size < total)
        return E_BAD_LENGTH;

    if (frame->len > PROTO_MAX_PAYLOAD)
        return E_BAD_LENGTH;

    /* Copy payload */
    for (u16 i = 0; i < frame->len; i++)
        frame->data[i] = buf[PROTO_HDR_SIZE + i];

    /* Verify CRC (over cmd + len + data, big-endian CRC at end) */
    u16 computed = crc16_calc(&buf[2], (u32)(PROTO_HDR_SIZE - 2 + frame->len), 0x0000);
    u16 received = (u16)((buf[total - 2] << 8) | buf[total - 1]);

    if (computed != received)
        return E_BAD_CRC;

    return (int)total;
}

/* ── High-level send/receive ─────────────────────────────────────── */

int proto_send_ack(struct transport *t)
{
    u8 frame[PROTO_MAX_FRAME];
    int size = proto_build_frame(RESP_ACK, NULL, 0, frame);
    if (size < 0)
        return size;
    return t->send(t, frame, (u32)size);
}

int proto_send_nack(struct transport *t, s32 error)
{
    u8 error_bytes[4];
    error_bytes[0] = (u8)((error >> 24) & 0xFF);
    error_bytes[1] = (u8)((error >> 16) & 0xFF);
    error_bytes[2] = (u8)((error >> 8) & 0xFF);
    error_bytes[3] = (u8)(error & 0xFF);

    u8 frame[PROTO_MAX_FRAME];
    int size = proto_build_frame(RESP_NACK, error_bytes, 4, frame);
    if (size < 0)
        return size;
    return t->send(t, frame, (u32)size);
}

/**
 * proto_recv_frame - receive a complete frame with sync detection
 *
 * Strategy: read bytes one at a time looking for the sync pattern.
 * Once found, read the header to determine the payload length, then
 * read the payload and CRC.  Verify CRC before returning.
 */
int proto_recv_frame(struct transport *t, u8 *buf, u32 buf_size, u32 timeout_ms)
{
    u32 idx = 0;
    int ret;
    u8 byte;

    if (buf_size < PROTO_MAX_FRAME)
        return E_PROTO_OVERFLOW;

    /* Phase 1: Find sync bytes */
    while (1) {
        ret = t->recv_byte(t, &byte, timeout_ms);
        if (ret != E_OK)
            return E_PROTO_TIMEOUT;

        if (idx == 0 && byte == PROTO_SYNC0) {
            buf[idx++] = byte;
        } else if (idx == 1) {
            if (byte == PROTO_SYNC1) {
                buf[idx++] = byte;
                break;  /* Got sync */
            } else if (byte == PROTO_SYNC0) {
                /* Restart — possible repeated sync0 */
                idx = 0;
                buf[idx++] = byte;
            } else {
                idx = 0;  /* False start, reset */
            }
        } else {
            /* Shouldn't happen */
            idx = 0;
        }
    }

    /* Phase 2: Read header (cmd + len) */
    ret = t->recv(t, &buf[idx], PROTO_HDR_SIZE - idx, timeout_ms);
    if (ret < 0)
        return E_PROTO_TIMEOUT;
    if (ret < (int)(PROTO_HDR_SIZE - idx))
        return E_PROTO_TIMEOUT;
    idx += (u32)ret;

    /* Extract payload length */
    u16 payload_len = (u16)((buf[3] << 8) | buf[4]);
    if (payload_len > PROTO_MAX_PAYLOAD)
        return E_BAD_LENGTH;

    /* Phase 3: Read payload + CRC */
    u32 remaining = payload_len + PROTO_CRC_SIZE;
    if (remaining > 0) {
        ret = t->recv(t, &buf[idx], remaining, timeout_ms);
        if (ret < 0)
            return E_PROTO_TIMEOUT;
        if ((u32)ret < remaining)
            return E_PROTO_TIMEOUT;
        idx += (u32)ret;
    }

    /* Phase 4: Verify frame */
    struct proto_frame frame;
    ret = proto_parse_frame(buf, idx, &frame);
    if (ret < 0)
        return ret;

    return (int)idx;
}
