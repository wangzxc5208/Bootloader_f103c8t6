#!/usr/bin/env python3
"""
OTA Firmware Upgrade Tool — Visual GUI for STM32F103 Bootloader

A simple, user-friendly GUI for sending firmware images to the
STM32F103C8T6 bootloader over USART2.

Uses tkinter (built-in with Python) — no extra dependencies except pyserial.

Requirements:
  pip install pyserial

Usage:
  python ota_gui.py
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import struct
import sys
import time
import threading
from typing import Optional, Tuple

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Error: pyserial is required. Install with: pip install pyserial")
    sys.exit(1)


# ═══════════════════════════════════════════════════════════════════
# Protocol constants (must match bootloader Core/Inc/proto.h)
# ═══════════════════════════════════════════════════════════════════

SYNC0, SYNC1 = 0xAA, 0x55
HDR_SIZE, CRC_SIZE, MAX_PAYLOAD = 5, 2, 1024

CMD_PING, CMD_START_OTA, CMD_SEND_DATA = 0x01, 0x02, 0x03
CMD_VERIFY, CMD_ACTIVATE, CMD_GET_STATUS = 0x04, 0x05, 0x06
CMD_GET_VERSION, CMD_RESET = 0x07, 0x08
RESP_ACK, RESP_NACK, RESP_STATUS, RESP_VERSION = 0x80, 0x81, 0x82, 0x83

CRC16_TABLE = [
    0x0000,0x1021,0x2042,0x3063,0x4084,0x50A5,0x60C6,0x70E7,
    0x8108,0x9129,0xA14A,0xB16B,0xC18C,0xD1AD,0xE1CE,0xF1EF,
    0x1231,0x0210,0x3273,0x2252,0x52B5,0x4294,0x72F7,0x62D6,
    0x9339,0x8318,0xB37B,0xA35A,0xD3BD,0xC39C,0xF3FF,0xE3DE,
    0x2462,0x3443,0x0420,0x1401,0x64E6,0x74C7,0x44A4,0x5485,
    0xA56A,0xB54B,0x8528,0x9509,0xE5EE,0xF5CF,0xC5AC,0xD58D,
    0x3653,0x2672,0x1611,0x0630,0x76D7,0x66F6,0x5695,0x46B4,
    0xB75B,0xA77A,0x9719,0x8738,0xF7DF,0xE7FE,0xD79D,0xC7BC,
    0x48C4,0x58E5,0x6886,0x78A7,0x0840,0x1861,0x2802,0x3823,
    0xC9CC,0xD9ED,0xE98E,0xF9AF,0x8948,0x9969,0xA90A,0xB92B,
    0x5AF5,0x4AD4,0x7AB7,0x6A96,0x1A71,0x0A50,0x3A33,0x2A12,
    0xDBFD,0xCBDC,0xFBBF,0xEB9E,0x9B79,0x8B58,0xBB3B,0xAB1A,
    0x6CA6,0x7C87,0x4CE4,0x5CC5,0x2C22,0x3C03,0x0C60,0x1C41,
    0xEDAE,0xFD8F,0xCDEC,0xDDCD,0xAD2A,0xBD0B,0x8D68,0x9D49,
    0x7E97,0x6EB6,0x5ED5,0x4EF4,0x3E13,0x2E32,0x1E51,0x0E70,
    0xFF9F,0xEFBE,0xDFDD,0xCFFC,0xBF1B,0xAF3A,0x9F59,0x8F78,
    0x9188,0x81A9,0xB1CA,0xA1EB,0xD10C,0xC12D,0xF14E,0xE16F,
    0x1080,0x00A1,0x30C2,0x20E3,0x5004,0x4025,0x7046,0x6067,
    0x83B9,0x9398,0xA3FB,0xB3DA,0xC33D,0xD31C,0xE37F,0xF35E,
    0x02B1,0x1290,0x22F3,0x32D2,0x4235,0x5214,0x6277,0x7256,
    0xB5EA,0xA5CB,0x95A8,0x8589,0xF56E,0xE54F,0xD52C,0xC50D,
    0x34E2,0x24C3,0x14A0,0x0481,0x7466,0x6447,0x5424,0x4405,
    0xA7DB,0xB7FA,0x8799,0x97B8,0xE75F,0xF77E,0xC71D,0xD73C,
    0x26D3,0x36F2,0x0691,0x16B0,0x6657,0x7676,0x4615,0x5634,
    0xD94C,0xC96D,0xF90E,0xE92F,0x99C8,0x89E9,0xB98A,0xA9AB,
    0x5844,0x4865,0x7806,0x6827,0x18C0,0x08E1,0x3882,0x28A3,
    0xCB7D,0xDB5C,0xEB3F,0xFB1E,0x8BF9,0x9BD8,0xABBB,0xBB9A,
    0x4A75,0x5A54,0x6A37,0x7A16,0x0AF1,0x1AD0,0x2AB3,0x3A92,
    0xFD2E,0xED0F,0xDD6C,0xCD4D,0xBDAA,0xAD8B,0x9DE8,0x8DC9,
    0x7C26,0x6C07,0x5C64,0x4C45,0x3CA2,0x2C83,0x1CE0,0x0CC1,
    0xEF1F,0xFF3E,0xCF5D,0xDF7C,0xAF9B,0xBFBA,0x8FD9,0x9FF8,
    0x6E17,0x7E36,0x4E55,0x5E74,0x2E93,0x3EB2,0x0ED1,0x1EF0,
]


def crc16(data: bytes, crc: int = 0) -> int:
    for byte in data:
        crc = ((crc << 8) ^ CRC16_TABLE[((crc >> 8) ^ byte) & 0xFF]) & 0xFFFF
    return crc


def build_frame(cmd: int, data: bytes = b"") -> bytes:
    if len(data) > MAX_PAYLOAD:
        raise ValueError(f"Payload too large: {len(data)} > {MAX_PAYLOAD}")
    hdr = bytes([SYNC0, SYNC1, cmd,
                 (len(data) >> 8) & 0xFF, len(data) & 0xFF])
    crc = crc16(hdr[2:] + data)
    return hdr + data + bytes([(crc >> 8) & 0xFF, crc & 0xFF])


def recv_frame(ser, timeout=3.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        b = ser.read(1)
        if not b:
            continue
        if b[0] == SYNC0:
            b2 = ser.read(1)
            if b2 and b2[0] == SYNC1:
                break
    else:
        return None
    hdr = ser.read(3)
    if len(hdr) < 3:
        return None
    cmd, plen = hdr[0], (hdr[1] << 8) | hdr[2]
    remaining = plen + CRC_SIZE
    data = ser.read(remaining)
    if len(data) < remaining:
        return None
    payload, crc_rx = data[:plen], (data[-2] << 8) | data[-1]
    crc_calc = crc16(bytes([cmd]) + hdr[1:3] + payload)
    if crc_calc != crc_rx:
        return None
    return cmd, payload


# ═══════════════════════════════════════════════════════════════════
# GUI Application
# ═══════════════════════════════════════════════════════════════════

class OtaGuiApp:
    def __init__(self, root):
        self.root = root
        self.root.title("STM32F103 OTA 固件升级工具")
        self.root.geometry("680x520")
        self.root.resizable(False, False)

        self.ser: Optional[serial.Serial] = None
        self.firmware_path: str = ""
        self.firmware_data: bytes = b""
        self.running = False

        self._build_ui()
        self._refresh_ports()

    # ── UI construction ──────────────────────────────────────────

    def _build_ui(self):
        # Title
        title = ttk.Label(self.root, text="STM32F103 OTA 固件升级工具",
                          font=("Microsoft YaHei", 14, "bold"))
        title.pack(pady=(12, 8))

        # ── Connection frame ──────────────────────────────────────
        conn_frame = ttk.LabelFrame(self.root, text="串口连接", padding=8)
        conn_frame.pack(fill=tk.X, padx=12, pady=4)

        row1 = ttk.Frame(conn_frame)
        row1.pack(fill=tk.X)
        ttk.Label(row1, text="串口:").pack(side=tk.LEFT)
        self.port_var = tk.StringVar()
        self.port_cb = ttk.Combobox(row1, textvariable=self.port_var,
                                     width=18, state="readonly")
        self.port_cb.pack(side=tk.LEFT, padx=6)
        ttk.Button(row1, text="刷新", width=6,
                   command=self._refresh_ports).pack(side=tk.LEFT, padx=4)

        ttk.Label(row1, text="  波特率:").pack(side=tk.LEFT, padx=(12, 0))
        self.baud_var = tk.StringVar(value="9600")
        baud_cb = ttk.Combobox(row1, textvariable=self.baud_var, width=10,
                                values=["9600", "19200", "38400", "57600",
                                        "115200", "230400", "460800"],
                                state="readonly")
        baud_cb.pack(side=tk.LEFT, padx=6)

        self.connect_btn = ttk.Button(row1, text="连接", width=8,
                                       command=self._toggle_connect)
        self.connect_btn.pack(side=tk.RIGHT, padx=4)

        # ── Firmware frame ────────────────────────────────────────
        fw_frame = ttk.LabelFrame(self.root, text="固件信息", padding=8)
        fw_frame.pack(fill=tk.X, padx=12, pady=4)

        fw_row1 = ttk.Frame(fw_frame)
        fw_row1.pack(fill=tk.X)
        ttk.Label(fw_row1, text="文件:").pack(side=tk.LEFT)
        self.file_var = tk.StringVar()
        ttk.Entry(fw_row1, textvariable=self.file_var, width=50,
                  state="readonly").pack(side=tk.LEFT, padx=6, fill=tk.X, expand=True)
        ttk.Button(fw_row1, text="浏览...", width=8,
                   command=self._browse_file).pack(side=tk.LEFT, padx=4)

        fw_row2 = ttk.Frame(fw_frame)
        fw_row2.pack(fill=tk.X, pady=(6, 0))
        ttk.Label(fw_row2, text="版本:").pack(side=tk.LEFT)
        self.major_var = tk.StringVar(value="1")
        self.minor_var = tk.StringVar(value="0")
        self.patch_var = tk.StringVar(value="0")
        ttk.Entry(fw_row2, textvariable=self.major_var, width=5,
                  justify=tk.CENTER).pack(side=tk.LEFT, padx=2)
        ttk.Label(fw_row2, text=".").pack(side=tk.LEFT)
        ttk.Entry(fw_row2, textvariable=self.minor_var, width=5,
                  justify=tk.CENTER).pack(side=tk.LEFT, padx=2)
        ttk.Label(fw_row2, text=".").pack(side=tk.LEFT)
        ttk.Entry(fw_row2, textvariable=self.patch_var, width=5,
                  justify=tk.CENTER).pack(side=tk.LEFT, padx=2)

        ttk.Label(fw_row2, text="   大小:").pack(side=tk.LEFT, padx=(20, 0))
        self.size_var = tk.StringVar(value="—")
        ttk.Label(fw_row2, textvariable=self.size_var,
                  foreground="gray").pack(side=tk.LEFT)

        # ── Progress frame ────────────────────────────────────────
        prog_frame = ttk.LabelFrame(self.root, text="升级进度", padding=8)
        prog_frame.pack(fill=tk.X, padx=12, pady=4)

        self.progress = ttk.Progressbar(prog_frame, mode="determinate", length=620)
        self.progress.pack(fill=tk.X)
        self.progress_var = tk.StringVar(value="就绪")
        ttk.Label(prog_frame, textvariable=self.progress_var,
                  foreground="gray").pack(anchor=tk.W, pady=(2, 0))

        # ── Log frame ─────────────────────────────────────────────
        log_frame = ttk.LabelFrame(self.root, text="日志", padding=4)
        log_frame.pack(fill=tk.BOTH, expand=True, padx=12, pady=4)

        self.log_text = tk.Text(log_frame, height=8, width=76,
                                state=tk.DISABLED, font=("Consolas", 9),
                                bg="#1e1e1e", fg="#d4d4d4",
                                insertbackground="white")
        scrollbar = ttk.Scrollbar(log_frame, command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scrollbar.set)
        self.log_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        # ── Action button ─────────────────────────────────────────
        self.start_btn = ttk.Button(self.root, text="开始升级",
                                     command=self._start_update, width=20)
        self.start_btn.pack(pady=(4, 10))
        self.start_btn.configure(state=tk.DISABLED)

    # ── Logging ──────────────────────────────────────────────────

    def _log(self, msg: str, tag: str = ""):
        self.log_text.configure(state=tk.NORMAL)
        prefix = f"[{tag}] " if tag else ""
        self.log_text.insert(tk.END, f"{prefix}{msg}\n")
        self.log_text.see(tk.END)
        self.log_text.configure(state=tk.DISABLED)
        self.root.update_idletasks()

    # ── Serial port management ───────────────────────────────────

    def _refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_cb["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])
        self._log(f"发现 {len(ports)} 个串口: {', '.join(ports) if ports else '无'}")

    def _toggle_connect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
            self.ser = None
            self.connect_btn.configure(text="连接")
            self.start_btn.configure(state=tk.DISABLED)
            self._log("已断开连接")
        else:
            port = self.port_var.get()
            if not port:
                messagebox.showwarning("提示", "请先选择串口")
                return
            try:
                baud = int(self.baud_var.get())
                self.ser = serial.Serial(port, baud, timeout=0.1)
                self.ser.reset_input_buffer()
                self.connect_btn.configure(text="断开")
                self._log(f"已连接 {port} @ {baud} baud")

                # Try ping
                if self._ping():
                    self.start_btn.configure(state=tk.NORMAL)
                    self._log("✓ 检测到 Bootloader", "OK")
                else:
                    self._log("⚠ 未收到 Bootloader 响应 (请重置设备进入OTA模式)", "WARN")
                    self.start_btn.configure(state=tk.DISABLED)
            except Exception as e:
                messagebox.showerror("连接失败", str(e))
                self._log(f"连接失败: {e}", "ERROR")

    # ── Firmware file ────────────────────────────────────────────

    def _browse_file(self):
        path = filedialog.askopenfilename(
            title="选择固件文件",
            filetypes=[("Binary files", "*.bin"), ("All files", "*.*")])
        if path:
            self.firmware_path = path
            self.file_var.set(path)
            with open(path, "rb") as f:
                self.firmware_data = f.read()
            sz = len(self.firmware_data)
            self.size_var.set(f"{sz:,} 字节 ({sz/1024:.1f} KB)")
            self._log(f"加载固件: {path} ({sz} bytes)")
            if sz > 20 * 1024:
                self._log("⚠ 固件大小超过 20KB，可能无法写入", "WARN")

    # ── Protocol helpers ─────────────────────────────────────────

    def _send_cmd(self, cmd: int, data: bytes = b"", expect_ack=True) -> bool:
        if not self.ser or not self.ser.is_open:
            return False
        frame = build_frame(cmd, data)
        self.ser.write(frame)
        self.ser.flush()
        if not expect_ack:
            return True
        r = recv_frame(self.ser, timeout=3.0)
        return r is not None and r[0] == RESP_ACK

    def _ping(self) -> bool:
        if not self.ser or not self.ser.is_open:
            return False
        frame = build_frame(CMD_PING)
        self.ser.write(frame)
        self.ser.flush()
        r = recv_frame(self.ser, timeout=1.5)
        return r is not None and r[0] == RESP_ACK

    # ── Update workflow ──────────────────────────────────────────

    def _start_update(self):
        if not self.firmware_data:
            messagebox.showwarning("提示", "请先选择固件文件")
            return
        if not self.ser or not self.ser.is_open:
            messagebox.showwarning("提示", "请先连接串口")
            return
        if self.running:
            return

        self.running = True
        self.start_btn.configure(state=tk.DISABLED, text="升级中...")
        self.progress["value"] = 0
        self.progress_var.set("准备中...")
        self.log_text.configure(state=tk.NORMAL)
        self.log_text.delete("1.0", tk.END)
        self.log_text.configure(state=tk.DISABLED)

        thread = threading.Thread(target=self._update_thread, daemon=True)
        thread.start()

    def _update_thread(self):
        try:
            self._do_update()
        except Exception as e:
            self._log(f"异常: {e}", "ERROR")
        finally:
            self.running = False
            self.root.after(0, lambda: self.start_btn.configure(
                state=tk.NORMAL, text="开始升级"))

    def _do_update(self):
        fw = self.firmware_data
        major = int(self.major_var.get())
        minor = int(self.minor_var.get())
        patch = int(self.patch_var.get())

        # Step 1: Ping
        self._ui_log("→ 检测 Bootloader...")
        self._ui_progress(0, "检测中...")
        if not self._ping():
            self._ui_log("✗ Bootloader 无响应", "FAIL")
            self._ui_progress(0, "失败")
            return
        self._ui_log("✓ Bootloader 在线")

        # Step 2: Get version
        self._ui_log("→ 获取版本信息...")
        self.ser.write(build_frame(CMD_GET_VERSION))
        self.ser.flush()
        r = recv_frame(self.ser, timeout=2.0)
        if r and r[0] == RESP_VERSION and len(r[1]) >= 12:
            pv, bmaj, bmin, bpat = struct.unpack('>HHHH', r[1][:8])
            self._ui_log(f"  Bootloader v{bmaj}.{bmin}.{bpat}, 协议 v{pv>>8}.{pv&0xFF}")

        # Step 3: START_OTA
        self._ui_log(f"→ 开始 OTA: v{major}.{minor}.{patch}, {len(fw)} bytes")
        self._ui_progress(0, "开始升级...")
        payload = struct.pack('>IHHH2x', len(fw), major, minor, patch)
        if not self._send_cmd(CMD_START_OTA, payload):
            self._ui_log("✗ START_OTA 失败", "FAIL")
            self._ui_progress(0, "失败")
            return
        self._ui_log("✓ 设备已准备接收")

        # Step 4: SEND_DATA
        self._ui_log(f"→ 发送固件 ({len(fw)} bytes)...")
        offset = 0
        chunk_size = MAX_PAYLOAD - 4
        errors = 0
        while offset < len(fw):
            chunk = fw[offset:offset + chunk_size]
            pkt = struct.pack('>I', offset) + chunk
            if self._send_cmd(CMD_SEND_DATA, pkt):
                offset += len(chunk)
                pct = int(offset * 100 / len(fw))
                self._ui_progress(pct, f"发送中... {pct}% ({offset}/{len(fw)})")
                errors = 0
            else:
                errors += 1
                if errors > 3:
                    self._ui_log(f"✗ 发送失败 (offset={offset})", "FAIL")
                    self._ui_progress(0, "发送失败")
                    return
                self._ui_log(f"  重试 offset={offset}...", "RETRY")
                time.sleep(0.2)
                continue
        self._ui_log(f"✓ 固件发送完成 ({offset} bytes)")

        # Step 5: VERIFY
        self._ui_log("→ 校验固件 CRC...")
        self._ui_progress(95, "校验中...")
        if not self._send_cmd(CMD_VERIFY):
            self._ui_log("✗ CRC 校验失败", "FAIL")
            self._ui_progress(0, "校验失败")
            return
        self._ui_log("✓ CRC 校验通过")

        # Step 6: ACTIVATE
        self._ui_log("→ 激活新固件...")
        self._ui_progress(100, "激活中...")
        frame = build_frame(CMD_ACTIVATE)
        self.ser.write(frame)
        self.ser.flush()
        time.sleep(0.3)
        self._ui_log("")
        self._ui_log("═" * 50)
        self._ui_log("  ✓ 升级完成！设备正在重启...")
        self._ui_log(f"  新版本: v{major}.{minor}.{patch}")
        self._ui_log("═" * 50)
        self._ui_progress(100, "完成 ✓")

    # ── UI thread-safe helpers ───────────────────────────────────

    def _ui_log(self, msg: str, tag: str = ""):
        self.root.after(0, lambda: self._log(msg, tag))

    def _ui_progress(self, val: int, text: str = ""):
        def _update():
            self.progress["value"] = val
            if text:
                self.progress_var.set(text)
        self.root.after(0, _update)


# ═══════════════════════════════════════════════════════════════════
# Entry point
# ═══════════════════════════════════════════════════════════════════

def main():
    root = tk.Tk()
    app = OtaGuiApp(root)

    # Set dark theme colors (fallback for non-Windows)
    style = ttk.Style()
    try:
        style.theme_use("vista")
    except tk.TclError:
        pass

    root.mainloop()


if __name__ == "__main__":
    main()
