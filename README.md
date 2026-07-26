# STM32F103C8T6 USART Bootloader

基于 USART2 的固件下载 bootloader。单分区设计，简单直接。

> 原 A/B 分区 + 版本回滚完整版在 `legacy-ab` 分支。

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
           │  Application     │  48 KB  (pages 16-63)
0x08010000 └──────────────────┘
```

## 启动流程

```
上电
  │
  ├─ Bootloader 初始化 (USART2 + Flash 驱动)
  ├─ 校验 App (SP 在 RAM / PC 在 App 范围 / thumb bit / 魔数 0xCAFEBABE @ 0x200)
  │
  ├─ App 无效 → 进入 OTA 模式（一直等待固件）
  │
  └─ App 有效 → 等 3 秒 OTA 命令
       ├─ 收到 PING / START_OTA → 进入 OTA 模式
       └─ 超时 → 跳转到 App（含完整环境清理）
```

## 跳转前环境清理

跳转到 App 前执行完整的硬件复位，确保 App 从干净状态启动：

```
1. __disable_irq()          — 关全局中断
2. RCC 复位到 HSI 8MHz      — 关 PLL、关 HSE，切回默认时钟
3. NVIC->ICER[0..7] 清零    — 关所有外设中断使能
4. SysTick->CTRL = 0        — 关滴答
5. NVIC->ICPR[0..7] 清零    — 清所有挂起中断
6. SCB->VTOR = APP_BASE     — 设向量表偏移
7. __set_MSP(app_sp)        — 设主栈指针
8. __set_CONTROL(0)         — 特权线程模式 + MSP
9. __enable_irq()           — 开中断 (源已全关，安全)
10. DSB + ISB → 跳转
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
cmake --preset Debug
cmake --build build/Debug
# 产物: build/Debug/bootloader_f103.bin (ST-Link 烧录到 0x08000000)
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

用 CubeMX 新建 App 工程后，需改 2 处：

### 1. 链接脚本 (`STM32F103XX_FLASH.ld`)

```ld
/* FLASH 起始地址改为 App 区 */
MEMORY {
    FLASH (rx) : ORIGIN = 0x08004000, LENGTH = 48K
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

### 2. `main()` — 正常使用 CubeMX 生成的代码

简化版 bootloader 跳转前会把时钟切回 HSI 8MHz，所以 **App 可以正常调用 `SystemClock_Config()` 配 PLL 72MHz**，不需要特殊处理。

```c
int main(void)
{
    HAL_Init();                 // SysTick HSI 8MHz
    SystemClock_Config();       // 正常配 HSE+PLL=72MHz
    MX_GPIO_Init();

    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(500);
    }
}
```

### App 开发规则 (为什么)

| 规则 | 原因 |
|------|------|
| `FLASH ORIGIN = 0x08004000` | App 起始地址，bootloader 占 0x08000000-0x08003FFF |
| `.image_header` + `0xCAFEBABE` | bootloader 在 `app+0x200` 处校验魔数，没有就不启动 |
| `SystemClock_Config()` 正常调用 | bootloader 跳转前已切回 HSI，App 从干净状态启动 |

## 代码风格

Linux 内核 C 面向对象风格：

- `container_of()` — 从成员指针反查容器结构体
- `struct flash_driver` / `struct transport` — vtable 操作接口
- `struct list_head` — 双向链表 (list_for_each_entry 等)
- 返回值: `0` = 成功，负数 = 错误码 (E_FLASH_ERASE 等)

## 分支

| 分支 | 说明 |
|------|------|
| `main` | 简化版 (当前) — 单分区 USART 下载 |
| `legacy-ab` | 完整版 — A/B 双分区 + 版本回滚 + Info Block 持久化 |

## 大小

| 组件 | Debug (-Og) |
|------|-------------|
| Bootloader | 10,176 B (62.1% of 16 KB) |
