/*
 * proto.h - OTA Protocol definitions
 *
 * Defines the frame format, command codes, and error codes for
 * the USART-based firmware update protocol.
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _PROTO_H
#define _PROTO_H

#include "types.h"
#include "boot.h"

/* ── Frame constants ─────────────────────────────────────────────── */

#define PROTO_SYNC0        0xAAU
#define PROTO_SYNC1        0x55U
#define PROTO_HDR_SIZE     5U    /* SYNC0 + SYNC1 + CMD + LEN_MSB + LEN_LSB */
#define PROTO_CRC_SIZE     2U
#define PROTO_MAX_PAYLOAD  1024U
#define PROTO_MAX_FRAME    (PROTO_HDR_SIZE + PROTO_MAX_PAYLOAD + PROTO_CRC_SIZE)

/* ── Command codes (Host → Bootloader) ───────────────────────────── */

enum proto_cmd {
    CMD_PING         = 0x01,   /* Check bootloader alive */
    CMD_START_OTA    = 0x02,   /* Begin firmware update */
    CMD_SEND_DATA    = 0x03,   /* Firmware data chunk */
    CMD_VERIFY       = 0x04,   /* Verify written image CRC */
    CMD_ACTIVATE     = 0x05,   /* Activate new image + reset */
    CMD_GET_STATUS   = 0x06,   /* Query bootloader status */
    CMD_GET_VERSION  = 0x07,   /* Query bootloader version */
    CMD_RESET        = 0x08,   /* Software reset MCU */
};

/* ── Response codes (Bootloader → Host) ──────────────────────────── */

enum proto_resp {
    RESP_ACK         = 0x80,   /* Success */
    RESP_NACK        = 0x81,   /* General error */
    RESP_STATUS      = 0x82,   /* Status response (CMD_GET_STATUS) */
    RESP_VERSION     = 0x83,   /* Version response (CMD_GET_VERSION) */
};

/* ── Status codes returned by CMD_GET_STATUS ─────────────────────── */

enum proto_status {
    PSTATE_IDLE       = 0x00,   /* Bootloader idle, ready */
    PSTATE_RECEIVING  = 0x01,   /* Receiving firmware data */
    PSTATE_VERIFYING  = 0x02,   /* Verifying written image */
    PSTATE_COMPLETE   = 0x03,   /* Update complete, waiting for activate */
    PSTATE_ERROR      = 0x04,   /* Error occurred */
};

/* ── Frame structure (on-wire format) ────────────────────────────── */

/**
 * struct proto_frame - OTA protocol frame
 *
 * Wire format (big-endian length):
 *   [SYNC0] [SYNC1] [CMD] [LEN_MSB] [LEN_LSB] [DATA...] [CRC_MSB] [CRC_LSB]
 *
 * The CRC covers CMD + LEN + DATA (i.e., everything after the sync bytes).
 */
struct proto_frame {
    u8  cmd;                     /* Command or response code */
    u16 len;                     /* Payload length (big-endian on wire) */
    u8  data[PROTO_MAX_PAYLOAD]; /* Payload */
};

/* ── CMD_START_OTA payload ───────────────────────────────────────── */

struct proto_start_ota {
    u32 image_size;              /* Total firmware image size (bytes) */
    u16 major;                   /* Version: major */
    u16 minor;                   /* Version: minor */
    u16 patch;                   /* Version: patch */
    u8  reserved[2];             /* Future use, set to zero */
};

/* ── CMD_SEND_DATA payload ───────────────────────────────────────── */

struct proto_data_chunk {
    u32 offset;                  /* Byte offset within image */
    u8  payload[PROTO_MAX_PAYLOAD - 4];  /* Data */
};

/* ── CMD_GET_STATUS response payload ─────────────────────────────── */

struct proto_status_resp {
    u8  state;                   /* Current state (enum proto_status) */
    u8  progress;                /* Progress percentage (0-100) */
    u32 bytes_written;           /* Bytes written so far */
    u32 total_size;              /* Expected total size */
    u32 last_error;              /* Last error code (0 if none) */
};

/* ── CMD_GET_VERSION response payload ────────────────────────────── */

struct proto_version_resp {
    u16 proto_version;           /* Protocol version (e.g., 0x0100) */
    u16 boot_major;
    u16 boot_minor;
    u16 boot_patch;
    u8  capabilities[4];         /* Bitmask of supported features */
};

/* ── Protocol version ────────────────────────────────────────────── */

#define PROTO_VERSION_MAJOR  1
#define PROTO_VERSION_MINOR  0
#define PROTO_VERSION        ((PROTO_VERSION_MAJOR << 8) | PROTO_VERSION_MINOR)

/* ── API ─────────────────────────────────────────────────────────── */

/**
 * proto_build_frame - build a protocol frame into a buffer
 * @cmd:  command/response code
 * @data: payload data (may be NULL if len == 0)
 * @len:  payload length
 * @out:  output buffer (must be at least PROTO_MAX_FRAME bytes)
 * @return: total frame size on success, negative error on failure
 */
int proto_build_frame(u8 cmd, const u8 *data, u16 len, u8 *out);

/**
 * proto_parse_frame - parse a received protocol frame
 * @buf:  raw frame buffer
 * @size: size of data in buffer
 * @frame: output — parsed frame (data pointer points into buf)
 * @return: 0 on success, negative error on failure
 */
int proto_parse_frame(const u8 *buf, u32 size, struct proto_frame *frame);

/**
 * proto_send_ack - send an ACK response
 * @t:    transport to use
 * @return: 0 on success, negative error on failure
 */
int proto_send_ack(struct transport *t);

/**
 * proto_send_nack - send a NACK response with error code
 * @t:    transport to use
 * @error: error code (s32, sent as 4-byte payload)
 * @return: 0 on success, negative error on failure
 */
int proto_send_nack(struct transport *t, s32 error);

/**
 * proto_recv_frame - receive a complete frame from transport
 * @t:      transport to use
 * @buf:    buffer for raw frame data
 * @buf_size: buffer capacity
 * @timeout_ms: receive timeout
 * @return: frame size on success, negative error on failure
 */
int proto_recv_frame(struct transport *t, u8 *buf, u32 buf_size, u32 timeout_ms);

#endif /* _PROTO_H */
