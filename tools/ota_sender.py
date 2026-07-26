#!/usr/bin/env python3
"""
OTA Firmware Sender — sends firmware images to the bootloader over USART2.

Protocol:
  Frame: 0xAA 0x55 <CMD> <LEN_MSB> <LEN_LSB> <DATA...> <CRC16_MSB> <CRC16_LSB>

Commands:
  CMD_PING         (0x01)  Check if bootloader is alive
  CMD_START_OTA    (0x02)  Begin firmware update
  CMD_SEND_DATA    (0x03)  Send a firmware chunk
  CMD_VERIFY       (0x04)  Verify written image CRC
  CMD_ACTIVATE     (0x05)  Activate new image + reset
  CMD_GET_STATUS   (0x06)  Query bootloader status
  CMD_GET_VERSION  (0x07)  Query bootloader version
  CMD_RESET        (0x08)  Software reset

Responses:
  RESP_ACK         (0x80)  Success
  RESP_NACK        (0x81)  Error
  RESP_STATUS      (0x82)  Status response
  RESP_VERSION     (0x83)  Version response

Usage:
  python ota_sender.py <serial_port> <firmware.bin> [--baud 115200] [--version 1.0.0]
"""

import argparse
import struct
import sys
import time
from typing import Optional, Tuple

try:
    import serial
except ImportError:
    print("Error: pyserial is required. Install with: pip install pyserial")
    sys.exit(1)


# ── Protocol constants ─────────────────────────────────────────────

SYNC0 = 0xAA
SYNC1 = 0x55
HDR_SIZE = 5
CRC_SIZE = 2
MAX_PAYLOAD = 1024

CMD_PING        = 0x01
CMD_START_OTA   = 0x02
CMD_SEND_DATA   = 0x03
CMD_VERIFY      = 0x04
CMD_ACTIVATE    = 0x05
CMD_GET_STATUS  = 0x06
CMD_GET_VERSION = 0x07
CMD_RESET       = 0x08

RESP_ACK     = 0x80
RESP_NACK    = 0x81
RESP_STATUS  = 0x82
RESP_VERSION = 0x83


# ── CRC-16-CCITT ───────────────────────────────────────────────────

CRC16_TABLE = [
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0,
]


def crc16(data: bytes, crc: int = 0) -> int:
    """Compute CRC-16-CCITT over data."""
    for byte in data:
        crc = ((crc << 8) ^ CRC16_TABLE[((crc >> 8) ^ byte) & 0xFF]) & 0xFFFF
    return crc


# ── Frame helpers ──────────────────────────────────────────────────

def build_frame(cmd: int, data: bytes = b"") -> bytes:
    """Build a protocol frame."""
    if len(data) > MAX_PAYLOAD:
        raise ValueError(f"Payload too large: {len(data)} > {MAX_PAYLOAD}")

    hdr = bytes([SYNC0, SYNC1, cmd,
                 (len(data) >> 8) & 0xFF, len(data) & 0xFF])
    crc = crc16(hdr[2:] + data)
    return hdr + data + bytes([(crc >> 8) & 0xFF, crc & 0xFF])


def recv_frame(ser: serial.Serial, timeout: float = 5.0) -> Optional[Tuple[int, bytes]]:
    """Receive and decode a protocol frame. Returns (cmd, payload) or None."""
    t0 = time.time()

    # Find sync
    while True:
        if time.time() - t0 > timeout:
            return None
        b = ser.read(1)
        if not b:
            continue
        if b[0] == SYNC0:
            b2 = ser.read(1)
            if b2 and b2[0] == SYNC1:
                break

    # Read header
    hdr = ser.read(3)
    if len(hdr) < 3:
        return None
    cmd = hdr[0]
    payload_len = (hdr[1] << 8) | hdr[2]

    # Read payload + CRC
    remaining = payload_len + CRC_SIZE
    payload_and_crc = ser.read(remaining)
    if len(payload_and_crc) < remaining:
        return None

    payload = payload_and_crc[:payload_len]
    crc_recv = (payload_and_crc[-2] << 8) | payload_and_crc[-1]

    # Verify CRC
    computed = crc16(bytes([cmd]) + hdr[1:3] + payload)
    if computed != crc_recv:
        print(f"  [WARN] CRC mismatch: got 0x{crc_recv:04X}, expected 0x{computed:04X}")
        return None

    return (cmd, payload)


def send_command(ser: serial.Serial, cmd: int, data: bytes = b"",
                 expect_ack: bool = True) -> bool:
    """Send a command and optionally wait for ACK."""
    frame = build_frame(cmd, data)
    ser.write(frame)
    ser.flush()

    if not expect_ack:
        return True

    result = recv_frame(ser, timeout=3.0)
    if result is None:
        print("  [ERROR] No response received")
        return False

    resp_cmd, resp_data = result
    if resp_cmd == RESP_ACK:
        return True
    elif resp_cmd == RESP_NACK:
        if len(resp_data) >= 4:
            err_code = struct.unpack('>i', resp_data[:4])[0]
            print(f"  [ERROR] NACK received: error code {err_code}")
        else:
            print("  [ERROR] NACK received")
        return False
    elif resp_cmd == RESP_STATUS:
        return True  # Status response is OK
    else:
        print(f"  [WARN] Unexpected response: 0x{resp_cmd:02X}")
        return False


# ── High-level commands ────────────────────────────────────────────

def cmd_ping(ser: serial.Serial) -> bool:
    print("Pinging bootloader...", end=" ")
    if send_command(ser, CMD_PING):
        print("OK")
        return True
    print("FAIL")
    return False


def cmd_get_version(ser: serial.Serial) -> Optional[dict]:
    print("Getting bootloader version...", end=" ")
    frame = build_frame(CMD_GET_VERSION)
    ser.write(frame)
    ser.flush()

    result = recv_frame(ser, timeout=3.0)
    if result is None:
        print("FAIL (no response)")
        return None

    cmd, payload = result
    if cmd == RESP_VERSION and len(payload) >= 12:
        proto_ver, boot_major, boot_minor, boot_patch = \
            struct.unpack('>HHHH', payload[:8])
        caps = payload[8:12]
        info = {
            'proto_version': f"{proto_ver >> 8}.{proto_ver & 0xFF}",
            'bootloader_version': f"{boot_major}.{boot_minor}.{boot_patch}",
            'ota_support': bool(caps[0] & 0x01),
            'rollback_support': bool(caps[1] & 0x01),
        }
        print(f"OK (bootloader v{info['bootloader_version']}, "
              f"proto v{info['proto_version']})")
        return info
    print("FAIL (invalid response)")
    return None


def cmd_get_status(ser: serial.Serial) -> Optional[dict]:
    frame = build_frame(CMD_GET_STATUS)
    ser.write(frame)
    ser.flush()

    result = recv_frame(ser, timeout=2.0)
    if result is None:
        return None

    cmd, payload = result
    if cmd == RESP_STATUS and len(payload) >= 12:
        state, progress, bytes_written, total_size, last_error = \
            struct.unpack('>BBIII', payload[:12])
        states = {0: 'IDLE', 1: 'RECEIVING', 2: 'VERIFYING',
                   3: 'COMPLETE', 4: 'ERROR'}
        return {
            'state': states.get(state, f'UNKNOWN({state})'),
            'progress': progress,
            'bytes_written': bytes_written,
            'total_size': total_size,
            'last_error': last_error,
        }
    return None


def cmd_start_ota(ser: serial.Serial, image_size: int,
                  version: Tuple[int, int, int]) -> bool:
    major, minor, patch = version
    payload = struct.pack('>IHHH2x', image_size, major, minor, patch)
    print(f"Sending START_OTA (size={image_size}, "
          f"v{major}.{minor}.{patch})...", end=" ")
    if send_command(ser, CMD_START_OTA, payload):
        print("OK")
        return True
    print("FAIL")
    return False


def cmd_send_firmware(ser: serial.Serial, firmware: bytes,
                      progress_callback=None) -> bool:
    """Send the complete firmware image in chunks."""
    total = len(firmware)
    offset = 0
    chunk_size = MAX_PAYLOAD - 4  # 4 bytes for offset

    print(f"Sending firmware ({total} bytes)...")

    while offset < total:
        chunk = firmware[offset:offset + chunk_size]
        payload = struct.pack('>I', offset) + chunk

        if not send_command(ser, CMD_SEND_DATA, payload):
            print(f"  FAIL at offset {offset}")
            return False

        offset += len(chunk)
        pct = int(offset * 100 / total)
        if progress_callback:
            progress_callback(pct, offset, total)
        else:
            print(f"  Progress: {pct}% ({offset}/{total} bytes)")

    print("Firmware upload complete.")
    return True


def cmd_verify(ser: serial.Serial) -> bool:
    print("Verifying firmware CRC...", end=" ")
    if send_command(ser, CMD_VERIFY):
        print("OK")
        return True
    print("FAIL")
    return False


def cmd_activate(ser: serial.Serial) -> bool:
    print("Activating new firmware...", end=" ")
    if send_command(ser, CMD_ACTIVATE, expect_ack=False):
        print("OK (device will reset)")
        return True
    print("FAIL")
    return False


# ── Main ───────────────────────────────────────────────────────────

def parse_version(v: str) -> Tuple[int, int, int]:
    parts = v.split(".")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError(f"Version must be MAJOR.MINOR.PATCH, got: {v}")
    return (int(parts[0]), int(parts[1]), int(parts[2]))


def main():
    parser = argparse.ArgumentParser(
        description="OTA Firmware Sender for STM32F103 Bootloader")
    parser.add_argument("port", help="Serial port (e.g., COM3 or /dev/ttyUSB0)")
    parser.add_argument("firmware", help="Firmware binary file (.bin)")
    parser.add_argument("--baud", type=int, default=115200,
                        help="Baud rate (default: 115200)")
    parser.add_argument("--version", type=parse_version, default=(1, 0, 0),
                        help="Firmware version as MAJOR.MINOR.PATCH (default: 1.0.0)")
    parser.add_argument("--skip-verify", action="store_true",
                        help="Skip CRC verification step")
    args = parser.parse_args()

    # Read firmware
    try:
        with open(args.firmware, "rb") as f:
            firmware = f.read()
    except FileNotFoundError:
        print(f"Error: firmware file not found: {args.firmware}")
        sys.exit(1)

    if len(firmware) == 0:
        print("Error: firmware file is empty")
        sys.exit(1)

    if len(firmware) > 20 * 1024:
        print(f"Error: firmware too large ({len(firmware)} > 20480 bytes)")
        sys.exit(1)

    print(f"Firmware: {args.firmware} ({len(firmware)} bytes)")
    print(f"Version:  {args.version[0]}.{args.version[1]}.{args.version[2]}")
    print(f"Port:     {args.port} @ {args.baud} baud")
    print()

    # Open serial port
    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as e:
        print(f"Error: cannot open serial port: {e}")
        sys.exit(1)

    print("Waiting for bootloader...")
    time.sleep(0.5)

    # Flush any garbage
    ser.reset_input_buffer()

    # Step 1: Ping
    if not cmd_ping(ser):
        print("\nBootloader not responding. Check:")
        print("  - Correct serial port")
        print("  - Baud rate matches (115200)")
        print("  - USART2 TX/RX connected correctly")
        print("  - Device is in OTA mode (reset and wait 3 seconds)")
        ser.close()
        sys.exit(1)

    # Step 2: Get version info
    cmd_get_version(ser)

    # Step 3: Start OTA
    if not cmd_start_ota(ser, len(firmware), args.version):
        ser.close()
        sys.exit(1)

    # Step 4: Send firmware
    if not cmd_send_firmware(ser, firmware):
        ser.close()
        sys.exit(1)

    # Step 5: Verify
    if not args.skip_verify:
        if not cmd_verify(ser):
            print("\nVerification failed. The firmware may be corrupted.")
            ser.close()
            sys.exit(1)

    # Step 6: Activate
    print()
    if not cmd_activate(ser):
        ser.close()
        sys.exit(1)

    print("\n" + "=" * 55)
    print("  OTA Update Complete!")
    print(f"  New firmware v{args.version[0]}.{args.version[1]}.{args.version[2]}")
    print("  The device is now rebooting into the new firmware.")
    print("=" * 55)

    ser.close()


if __name__ == "__main__":
    main()
