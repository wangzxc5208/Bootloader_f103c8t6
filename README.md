# STM32F103C8T6 OTA Bootloader

基于 USART2 的固件升级 bootloader，支持 A/B 双分区、版本回滚。

## 硬件

| 项目 | 说明 |
|------|------|
| MCU | STM32F103C8T6 (Cortex-M3, 64KB Flash, 20KB RAM) |
| OTA 接口 | USART2 — **PA2 (TX)** / **PA3 (RX)** |
| 波特率 | **9600, 8N1** |
| 调试接口 | SWD (PA13/PA14) |

### 接线

```
STM32 PA2 (TX) ──── USB-TTL RXD
STM32 PA3 (RX) ──── USB-TTL TXD
STM32 GND      ──── USB-TTL GND
```

## Flash 分区

```
0x08000000 ┌──────────────────┐
           │  Bootloader      │  16 KB  (pages 0-15)
0x08004000 ├──────────────────┤
           │  App Slot A      │  20 KB  (pages 16-35)  ← 主运行区
0x08009000 ├──────────────────┤
           │  App Slot B      │  20 KB  (pages 36-55)  ← 备份/回滚
0x0800E000 ├──────────────────┤
           │  Info Block A    │   2 KB  (pages 56-57)  ← 元数据
0x0800E800 ├──────────────────┤
           │  Info Block B    │   2 KB  (pages 58-59)  ← 元数据副本
0x0800F000 ├──────────────────┤
           │  Reserved        │   4 KB  (pages 60-63)
0x08010000 └──────────────────┘
```

## 启动流程

```
上电
  │
  ├─ Bootloader 初始化 (USART2 + Flash 驱动)
  ├─ 读取 Info Block → 确定 active slot
  ├─ 校验 Slot A / Slot B (SP/PC 范围 + 魔数 0xCAFEBABE @ 0x200)
  │
  ├─ 两个 Slot 都无效 → 进入 OTA 模式(一直等待固件)
  │
  ├─ 至少一个 Slot 有效:
  │   ├─ STATUS_TRYING    → boot_attempt++, 超过上限则回滚
  │   ├─ STATUS_UPDATE_DONE → 首次启动新固件, 设 TRYING
  │   └─ STATUS_VALIDATED → 正常启动
  │
  ├─ 等 3 秒 OTA 命令(可在此期间连 GUI 工具进入 OTA)
  │
  └─ 跳转到 App
```

## 回滚机制

```
OTA 完成 → status = UPDATE_DONE → 重启
  │
  ├─ Bootloader 设 status = TRYING, boot_attempt = 1
  ├─ 跳转 App
  │
  ├─ App 调用 boot_mark_success() → status = VALIDATED ✓
  │
  └─ App 崩溃/不调 boot_mark_success():
        boot_attempt++ → 达到 max_attempts(3) → 回滚到上一个 Slot
```

## OTA 协议

帧格式 (大端):
```
┌──────┬──────┬──────┬────────┬──────────┬────────┐
│ 0xAA │ 0x55 │ CMD  │ LEN(2B)│  DATA    │ CRC16  │
└──────┴──────┴──────┴────────┴──────────┴────────┘
```

| 命令 | 代码 | 说明 |
|------|------|------|
| PING | 0x01 | 检测 bootloader 存活 |
| START_OTA | 0x02 | 开始升级 (image_size + version) |
| SEND_DATA | 0x03 | 发送固件数据块 (offset + payload) |
| VERIFY | 0x04 | 校验已写入数据的 CRC |
| ACTIVATE | 0x05 | 激活新固件并复位 |
| GET_STATUS | 0x06 | 查询升级进度 |
| GET_VERSION | 0x07 | 查询 bootloader 版本 |
| RESET | 0x08 | 软件复位 |
| ACK | 0x80 | 成功响应 |
| NACK | 0x81 | 错误响应 + 错误码 |

## 构建

```bash
# Bootloader
cd bootloader_f103
cmake --preset Debug
cmake --build build/Debug
# 产物: build/Debug/bootloader_f103.bin (ST-Link 烧录到 0x08000000)

# 测试 App
cd bootloader_test_led
cmake --preset Debug
cmake --build build/Debug
# 产物: build/Debug/bootloader_test_led.bin (OTA 升级)
```

## OTA 工具

### GUI 工具 (推荐)
```bash
pip install pyserial
python tools/ota_gui.py
```
1. 选串口 COMx，波特率 **9600**
2. 浏览固件 .bin 文件
3. 填版本号 (如 1.0.0)
4. 点「开始升级」

### 命令行工具
```bash
python tools/ota_sender.py COM3 app.bin --version 1.0.0 --baud 9600
```

## App 开发规则

用 CubeMX 新建 App 工程后，需改 3 处：

### 1. 链接脚本 (`STM32F103XX_FLASH.ld`)

```ld
/* FLASH 起始地址改为 Slot A */
MEMORY {
    FLASH (rx) : ORIGIN = 0x08004000, LENGTH = 20K
}

/* 在 .isr_vector 后面加魔数段 */
SECTIONS {
    .isr_vector : { ... } >FLASH

    /* bootloader 在此处校验 App 有效性 */
    .image_header 0x08004200 : {
        LONG(0xCAFEBABE);
        LONG(1);
    } >FLASH

    .text : { ... } >FLASH
    /* ... 其余不变 ... */
}
```

### 2. `main()` 开头

```c
int main(void)
{
    SystemCoreClockUpdate();   // ← 必须在 HAL_Init() 之前！
    HAL_Init();
    // SystemClock_Config();   // ← 注释掉！bootloader 已配 72MHz
    MX_GPIO_Init();

    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(500);
    }
}
```

### 3. 想用版本回滚的话

```c
extern void boot_mark_success(void);

int main(void) {
    // ... 初始化 ...

    boot_mark_success();  // 告诉 bootloader 启动成功

    while (1) { /* 业务逻辑 */ }
}
```

**如果不调 `boot_mark_success()`**，bootloader 会在 3 次启动失败后自动回滚到上一个版本。

## App 开发规则 (为什么)

| 规则 | 原因 |
|------|------|
| `FLASH ORIGIN = 0x08004000` | App 在 Slot A，bootloader 占 0x08000000-0x08003FFF |
| `.image_header` + `0xCAFEBABE` | bootloader 在 `slot+0x200` 处校验魔数，没有就不启动 |
| `SystemCoreClockUpdate()` 在 `HAL_Init` 前 | startup 设 `SystemCoreClock=8MHz`，但实际 HCLK 是 72MHz。不更新的话 SysTick 快 9 倍，`HAL_Delay` 不准 |
| 不调 `SystemClock_Config()` | bootloader 已配好 HSE+PLL=72MHz。重复配置 PLL 会导致切换过程中的时序问题 |

## 代码风格

Linux 内核 C 面向对象风格：

- `container_of()` — 从成员指针反查容器结构体
- `struct flash_driver` / `struct transport` — vtable 操作接口
- `struct list_head` — 双向链表 (list_for_each_entry 等)
- 返回值: `0` = 成功，负数 = 错误码 (E_FLASH_ERASE 等)

## 大小

| 组件 | Debug (-Og) | Release (-Os) | 分区 |
|------|-------------|---------------|------|
| Bootloader | 11,232 B (68.6%) | ~10,500 B | 16 KB |
| 测试 App | 6,120 B | — | 20 KB |
