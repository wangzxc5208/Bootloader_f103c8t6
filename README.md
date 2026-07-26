# STM32F103C8T6 USART Bootloader

> **带魔数版本** — App 必须在 `0x08004200` 处放置 `0xCAFEBABE` 才能启动。

基于 USART2 的固件下载 bootloader。单分区设计，简单直接，带完整环境清理。

> 原 A/B 分区 + 版本回滚完整版在 `legacy-ab` 分支。
> 无魔数版本在 [`no-magic-check`](../../tree/no-magic-check) 分支。

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
           │  Bootloader      │  16 KB
0x08004000 ├──────────────────┤
           │  Application     │  48 KB
0x08010000 └──────────────────┘
```

## 启动流程

```
上电
  │
  ├─ Bootloader 初始化 (USART2 + Flash 驱动)
  ├─ 校验 App (SP / PC 范围 / thumb bit / 魔数 0xCAFEBABE @ 0x200)
  │
  ├─ App 无效 → 进入 OTA 模式
  │
  └─ App 有效 → 等 3 秒 OTA 命令
       ├─ 收到命令 → 进入 OTA 模式
       └─ 超时 → 环境清理 → 跳转 App
```

## 跳转前环境清理

```
1. __disable_irq()           关全局中断
2. RCC 复位到 HSI 8MHz       关 PLL/HSE, 切默认时钟
3. NVIC->ICER[0..7] 清零     关外设 IRQ
4. SysTick->CTRL = 0         关滴答
5. NVIC->ICPR[0..7] 清零     清 pending
6. SCB->VTOR = APP_BASE      向量表偏移
7. __set_MSP(app_sp)         主栈指针
8. __set_CONTROL(0)          特权线程 + MSP
9. __enable_irq()            开中断 (源已全关, 安全)
10. DSB + ISB → 跳转
```

## OTA 协议

帧格式 (大端), CRC-16-CCITT (poly 0x1021):

```
┌──────┬──────┬──────┬────────┬──────────┬────────┐
│ 0xAA │ 0x55 │ CMD  │ LEN(2B)│  DATA    │ CRC16  │
└──────┴──────┴──────┴────────┴──────────┴────────┘
```

| 命令 | 代码 | 说明 |
|------|------|------|
| PING | 0x01 | 检测存活 |
| START_OTA | 0x02 | 开始升级 (image_size + version) |
| SEND_DATA | 0x03 | 数据块 (offset + payload) |
| VERIFY | 0x04 | CRC 校验 |
| ACTIVATE | 0x05 | 激活并复位 |
| GET_STATUS | 0x06 | 查询进度 |
| GET_VERSION | 0x07 | 查询版本 |
| RESET | 0x08 | 软件复位 |
| ACK | 0x80 | 成功 |
| NACK | 0x81 | 错误 + 错误码 |

## 构建

CMake 链接后自动生成 `.bin`：

```bash
cmake --preset Debug
cmake --build build/Debug
# → build/Debug/bootloader_f103.bin  (烧录到 0x08000000)
# → build/Debug/bootloader_f103.elf  (调试)
```

## OTA 工具

### Web 工具 (推荐)

**零依赖**。Chrome/Edge 打开 `tools/ota_web.html` 即可：

```
双击 tools/ota_web.html
```

**使用流程：**

1. 选串口 + 9600 → 点「连接」
2. 给 MCU 上电 → **自动持续检测**，找到后显示版本号
3. 拖入 `.bin` 固件 → 自动提取版本号 → 点「开始升级」
4. 四步进度：握手 → 发送 → 校验 → 激活 → 完成自动关串口

**特性：**
- 连接后持续后台检测，MCU 随时上电都能自动连上
- 拖放固件文件，文件名自动提取版本号
- 实时进度条 + 步骤指示器
- 升级完成后自动关串口，避免干扰 MCU 重启
- 深色主题，日志分类着色

> 需要 Chrome 89+ / Edge 89+（Web Serial API）。

### Python GUI

```bash
pip install pyserial
python tools/ota_gui.py
```

### 命令行

```bash
python tools/ota_sender.py COM3 app.bin --version 1.0.0 --baud 9600
```

## App 开发规则

### 1. 链接脚本

```ld
MEMORY {
    FLASH (rx) : ORIGIN = 0x08004000, LENGTH = 48K
}
SECTIONS {
    .isr_vector : { ... } >FLASH
    .image_header 0x08004200 : {
        LONG(0xCAFEBABE);  /* bootloader 校验魔数 */
    } >FLASH
    .text : { ... } >FLASH
}
```

### 2. main() — 正常用 CubeMX 代码

```c
int main(void) {
    HAL_Init();
    SystemClock_Config();   // 正常配 PLL 72MHz
    MX_GPIO_Init();
    while (1) { /* ... */ }
}
```

bootloader 跳转前已切回 HSI 8MHz，App 从干净状态启动，`SystemClock_Config()` 正常工作。

| 规则 | 原因 |
|------|------|
| `FLASH = 0x08004000, 48K` | bootloader 占 0x0000-0x3FFF |
| `.image_header + 0xCAFEBABE` | 魔数校验，无则不启动 |
| `SystemClock_Config()` 正常调用 | 跳转前已复位 RCC，App 冷启动 |

## 分支

> **本分支 (`main`) 带魔数校验** — App 必须在 `0x08004200` 处放置 `0xCAFEBABE` 才能启动。\
> 如果不需要魔数校验，切换至 [`no-magic-check`](../../tree/no-magic-check) 分支。

| 分支 | 说明 |
|------|------|
| `main` | **简化版（带魔数）** — 单分区 + 魔数 `0xCAFEBABE` 校验 |
| `no-magic-check` | **简化版（无魔数）** — 单分区，去掉魔数，向量表合规即启动 |
| `legacy-ab` | 完整版 — A/B 双分区 + 版本回滚 + Info Block |

## 代码风格

Linux 内核 C 面向对象：

- `container_of()` 反查容器
- `struct flash_driver` / `struct transport` — vtable 接口
- `struct list_head` 双向链表
- 返回 `0` = 成功，负数 = 错误码

## 大小

| 组件 | Debug (-Og) | 分区 |
|------|-------------|------|
| Bootloader | ~10.2 KB (62%) | 16 KB |
| 测试 App | ~5 KB | 48 KB |
