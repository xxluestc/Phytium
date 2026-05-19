# LoRa 真实硬件接入指南

> **目标**：将 ATK-MWCC68D LoRa 模块接到飞腾派 PE2204，替换当前的仿真数据，
> 实现 FreeRTOS 从核通过 UART3 直接收发 GD32 终端节点的真实 LoRa 数据。
>
> **当前状态**：FreeRTOS 仿真模式 `USE_LORA_SIMULATION=1`，所有数据由软件生成。
> **目标状态**：`USE_LORA_SIMULATION=0`，数据从真实 LoRa 模块通过 UART3 进入 FreeRTOS。

***

## 目录

1. [架构说明](#1-架构说明)
2. [前置条件检查](#2-前置条件检查)
3. [硬件连接](#3-硬件连接)
4. [LoRa 模块 AT 配置](#4-lora-模块-at-配置)
5. [FreeRTOS 代码修改](#5-freertos-代码修改)
6. [编译与部署](#6-编译与部署)
7. [GD32 终端节点准备](#7-gd32-终端节点准备)
8. [验证流程](#8-验证流程)
9. [踩坑记录与排错](#9-踩坑记录与排错)
10. [关键文件路径速查](#10-关键文件路径速查)

***

## 1. 架构说明

### 1.1 系统拓扑

```
┌─────────────────────────────────────────────────────────────────┐
│  Phytium PE2204 开发板                                          │
│                                                                  │
│  ┌──────────────────┐         ┌───────────────────────────────┐ │
│  │ Linux (CPU0-2)   │ RPMsg   │ FreeRTOS (CPU3, 独占)          │ │
│  │                   │◄───────►│                                │ │
│  │ master_receiver   │         │ master_recv_task               │ │
│  │ (显示/记录数据)   │         │   ├── master_recv_lora_data() │ │
│  │                   │         │   │   └── 帧提取+CRC校验      │ │
│  │ /dev/rpmsg0       │         │   └── master_judge_task       │ │
│  │                   │         │       └── 故障判决            │ │
│  │                   │         │                                │ │
│  │ LoRa TX 路径:     │         │ master_cmd_task               │ │
│  │   RPMsg ← FreeRTOS│         │   └── 加密+组帧→UART3_TX      │ │
│  └──────────────────┘         │                                │ │
│                                │ UART3 (0x2800f000)             │ │
│                                │   TXD = J1 Pin8               │ │
│                                │   RXD = J1 Pin10              │ │
│                                │ GPIO2_10 = J1 Pin7 (AUX/MD0) │ │
│                                └──────────┬────────────────────┘ │
│                                           │                      │
└───────────────────────────────────────────┼──────────────────────┘
                                            │
                                    ┌───────▼───────┐
                                    │ ATK-MWCC68D   │
                                    │ LoRa 模块      │
                                    │ 地址: 0x000A  │
                                    │ 信道: 23      │
                                    └───────┬───────┘
                                            │ RF (19.2kbps)
                                            │
                                    ┌───────▼───────┐
                                    │ ATK-MWCC68D   │
                                    │ LoRa 模块      │
                                    │ 地址: 0x000B  │
                                    │ (GD32终端节点) │
                                    └───────┬───────┘
                                            │ UART
                                    ┌───────▼───────┐
                                    │ GD32L233C     │
                                    │ 终端节点       │
                                    └───────────────┘
```

### 1.2 关键设计决策

| 决策                   | 说明                                                                                  |
| -------------------- | ----------------------------------------------------------------------------------- |
| **FreeRTOS 直连 LoRa** | LoRa 模块的 UART 和 GPIO 由 FreeRTOS (CPU3) 直接控制，**不经 Linux**。Linux 只能通过 RPMsg 获取已处理的数据。 |
| **Linux 不占用 UART3**  | 设备树中**不能**使能 UART3，否则 Linux 内核会占用该外设，导致 FreeRTOS 寄存器访问冲突。                           |
| **仿真/真实切换**          | 通过 `freertos/src/master_recv.c` 中的 `USE_LORA_SIMULATION` 宏一键切换。                     |
| **两个 UART3 地址坑**     | SDK 定义 `FUART3_BASE_ADDR = 0x2800f000`，但代码注释中误写为 `0x2802D000`。**必须以 SDK 为准**。       |

***

## 2. 前置条件检查

在开始硬件接线之前，确认以下环境已就绪：

### 2.1 编译环境

```bash
# 检查工具链
ls /home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-gcc
# 应输出: .../aarch64-none-elf-gcc (可执行文件路径)

# 检查 SDK
ls /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/Makefile
# 应输出: .../Makefile
```

### 2.2 当前仿真模式确认

```bash
# 确认当前为仿真模式
grep "USE_LORA_SIMULATION" /home/alientek/Phytium/freertos/src/master_recv.c
# 应输出: #define USE_LORA_SIMULATION  1
```

### 2.3 飞腾派开发板确认

- [ ] 开发板已上电，Linux 系统正常运行
- [ ] 已通过串口或 SSH 登录到 Linux
- [ ] `ifconfig` 或 `ip addr` 确认网络正常（可选，用于文件传输）

***

## 3. 硬件连接

### 3.1 所需物料

| 物料                  | 数量  | 说明                      |
| ------------------- | --- | ----------------------- |
| ATK-MWCC68D LoRa 模块 | 2 个 | 一个接飞腾派（主控），一个接 GD32（终端） |
| 杜邦线（母对母）            | 5 根 | 连接 J1 排针                |
| USB-TTL 模块          | 1 个 | 用于首次配置 LoRa 模块（临时使用）    |
| GD32L233C 开发板       | 1 块 | 烧录终端节点程序                |

### 3.2 引脚对应关系

**飞腾派 J1 排针 → ATK-MWCC68D LoRa 模块：**

| 飞腾派 J1 排针 | 引脚功能       | PE2204 内部信号 | ATK-MWCC68D 引脚 | 线色建议 |
| :-------: | ---------- | ----------- | -------------- | :--: |
|   Pin 6   | GND        | GND         | GND            |  黑色  |
|   Pin 8   | UART3\_TXD | UART3 TX    | RXD            |  黄色  |
|   Pin 10  | UART3\_RXD | UART3 RX    | TXD            |  橙色  |
|   Pin 1   | VCC\_3.3V  | 3.3V        | VCC            |  红色  |
|   Pin 7   | GPIO2\_10  | GPIO2\_10   | AUX / MD0      |  蓝色  |

> **⚠️ 重要**：ATK-MWCC68D 模块的 AUX 引脚连接到 GPIO2\_10，用于：
>
> - 模块状态指示（模块忙时为低电平）
> - 进入 AT 命令模式（MD0 拉低时上电）
> - 唤醒模块

### 3.3 接线步骤

1. **断电操作**：先断开飞腾派电源
2. **连接 GND**：J1 Pin6 → LoRa 模块 GND
3. **连接 VCC**：J1 Pin1 → LoRa 模块 VCC（**确认是 3.3V！ATK-MWCC68D 不支持 5V**）
4. **连接 TXD**：J1 Pin8 → LoRa 模块 RXD
5. **连接 RXD**：J1 Pin10 → LoRa 模块 TXD
6. **连接 AUX**：J1 Pin7 → LoRa 模块 AUX
7. **检查**：用万用表确认无短路后再上电

### 3.4 引脚定义参考

PE2204 UART3 的寄存器基址和中断号来自 SDK：

```c
// 来源: phytium-free-rtos-sdk-master/standalone/soc/pe220x/fparameters_comm.h
#define FUART3_BASE_ADDR   0x2800f000U   // ← 正确地址！不是 0x2802D000
#define FUART3_ID          3U
#define FUART3_IRQ_NUM     (88 + 30)     // = 118 (GICv3 SPI)
#define FUART3_CLK_FREQ_HZ 100000000U    // 100MHz

// GPIO2 基址
#define FGPIO2_BASE_ADDR   0x28036000U
```

***

## 4. LoRa 模块 AT 配置

ATK-MWCC68D 模块需要预先配置地址、信道、速率等参数。
**模块的参数保存在内部 Flash 中，配置一次即可，断电不丢失**。

### 4.1 进入 AT 命令模式

ATK-MWCC68D 模块有两种方式进入 AT 模式：

**方式一：AUX 引脚拉低上电（推荐）**

将 AUX (MD0) 引脚接 GND 后给模块上电，模块进入 AT 命令模式。

**方式二：正常模式发送** **`+++`**

模块正常运行时，通过 UART 发送 `+++`（无回车），模块进入 AT 模式。

### 4.2 配置步骤（使用 USB-TTL）

1. 将 USB-TTL 模块连接到 LoRa 模块：
   - USB-TTL TXD → LoRa RXD
   - USB-TTL RXD → LoRa TXD
   - USB-TTL GND → LoRa GND
2. 将 LoRa 模块 AUX 引脚接 GND，然后上电（VCC 接 3.3V）
3. 打开串口工具（波特率 **9600**，8N1，无流控）：

```bash
# 在 PC 上使用 minicom/picocom/screen
picocom -b 9600 /dev/ttyUSB0
```

1. 发送 AT 配置命令（每条命令以 `\r\n` 结尾）：

```
AT+ADDR=000A           # 设置主控模块地址为 0x000A
AT+DESTADDR=000B       # 设置目标地址为终端节点地址 0x000B
AT+CHANNEL=23          # 设置信道为 23
AT+NETID=0             # 设置网络 ID 为 0
AT+BAUD=9600           # 设置 UART 波特率为 9600
AT+PARITY=8N1          # 设置 8N1 格式
AT+AIRRATE=6           # 设置空中速率 19.2kbps (索引6)
AT+TPOWER=20           # 设置发射功率 20dBm
AT+WLTIME=1            # 设置唤醒时间为 1 秒
AT+WMODE=0             # 设置唤醒模式为通用模式
AT+TMODE=1             # 设置传输模式为定点传输
AT+PACKSIZE=240        # 设置数据包大小为 240 字节
AT+LBT=0               # 关闭 LBT(信道监听)
AT+SAVE                # 保存配置到 Flash
AT+RESET               # 复位模块使配置生效
```

1. 验证配置：

```
AT+ADDR=?              # 查询模块地址，应返回 +ADDR=000A
AT+CHANNEL=?           # 查询信道，应返回 +CHANNEL=23
AT+BAUD=?              # 查询波特率，应返回 +BAUD=9600
```

> **⚠️ 关键坑点**：
>
> - 上电后等模块初始化完成（AUX 引脚变高）再发送 AT 命令
> - 如果 AUX 一直为低，说明模块未就绪，检查供电和接线
> - 每个命令后必须等返回 `+OK` 再发下一条
> - 配置完成后务必执行 `AT+SAVE` + `AT+RESET`

### 4.3 GD32 侧 LoRa 模块配置

对连接 GD32 终端节点的 LoRa 模块，用相同的步骤配置，参数互为镜像：

```
AT+ADDR=000B           # 终端模块地址 0x000B
AT+DESTADDR=000A       # 目标为主控地址 0x000A
AT+CHANNEL=23          # 信道与主控相同
AT+NETID=0
AT+BAUD=9600
AT+PARITY=8N1
AT+AIRRATE=6
AT+TPOWER=20
AT+WLTIME=1
AT+TMODE=1
AT+PACKSIZE=240
AT+SAVE
AT+RESET
```

***

## 5. FreeRTOS 代码修改

### 5.1 需要修改的文件清单

| 文件                           | 修改内容                                    | 优先级 |
| ---------------------------- | --------------------------------------- | :-: |
| `freertos/src/master_recv.c` | 1. 关闭仿真 2. 实现 `master_lora_uart_recv()` | ★★★ |
| `freertos/src/master_cmd.c`  | 修改 TX 路径，通过 UART3 发送指令到终端               | ★★☆ |
| `freertos/inc/data_frame.h`  | 确认帧结构定义与 GD32 一致（通常无需修改）                | ★☆☆ |

### 5.2 步骤一：关闭仿真模式

编辑 `freertos/src/master_recv.c`，将仿真开关从 `1` 改为 `0`：

**修改前：**

```c
#define USE_LORA_SIMULATION  1     /* 1=仿真模式, 0=真实LoRa UART */
```

**修改后：**

```c
#define USE_LORA_SIMULATION  0     /* 1=仿真模式, 0=真实LoRa UART */
```

### 5.3 步骤二：实现 UART3 驱动

在 `freertos/src/` 目录下新建 `lora_uart.c` 和对应的 `freertos/inc/lora_uart.h`。

#### 5.3.1 PL011 寄存器定义（在 `lora_uart.c` 中直接使用）

PE2204 的 UART 是标准 ARM PL011，以下是所需寄存器偏移和位定义：

```c
// PL011 寄存器偏移 (来源: SDK fpl011_hw.h)
#define UART_DR     0x000   // 数据寄存器
#define UART_RSR    0x004   // 接收状态/错误清除
#define UART_FR     0x018   // 标志寄存器
#define UART_IBRD   0x024   // 整数波特率除数
#define UART_FBRD   0x028   // 小数波特率除数
#define UART_LCR_H  0x02C   // 行控制寄存器
#define UART_CR     0x030   // 控制寄存器
#define UART_IFLS   0x034   // 中断FIFO级别选择
#define UART_IMSC   0x038   // 中断屏蔽设置/清除
#define UART_RIS    0x03C   // 原始中断状态
#define UART_MIS    0x040   // 屏蔽后中断状态
#define UART_ICR    0x044   // 中断清除寄存器

// FR 标志位
#define UART_FR_TXFE  0x80  // TX FIFO 空
#define UART_FR_RXFF  0x40  // RX FIFO 满
#define UART_FR_TXFF  0x20  // TX FIFO 满
#define UART_FR_RXFE  0x10  // RX FIFO 空

// CR 控制位
#define UART_CR_UARTEN  0x0001  // UART 使能
#define UART_CR_RXE     0x0200  // 接收使能
#define UART_CR_TXE     0x0100  // 发送使能

// LCR_H 行控制位
#define UART_LCRH_FEN   0x10  // FIFO 使能
#define UART_LCRH_WLEN_8 0x60 // 8 位数据

// IMSC 中断屏蔽位
#define UART_IMSC_RXIM  0x10  // 接收中断屏蔽
```

#### 5.3.2 UART3 外设基址（PE2204 专用）

```c
// ★ 正确地址，以 SDK fparameters_comm.h 为准
#define UART3_BASE     0x2800f000U
#define UART3_CLK_HZ   100000000U
#define GPIO2_BASE     0x28036000U

#define UART_REG(base, offset)  (*(volatile u32 *)((base) + (offset)))
```

#### 5.3.3 GPIO 与控制引脚

AUX/MD0 引脚（GPIO2\_10）用于检测 LoRa 模块状态：

```c
// GPIO2 寄存器偏移
#define GPIO_DATA_OFFSET    0x000   // 数据寄存器（位10 = GPIO2_10）
#define GPIO_DIR_OFFSET     0x400   // 方向寄存器

// AUX 引脚检查宏
#define LORA_AUX_IS_HIGH()  (UART_REG(GPIO2_BASE, GPIO_DATA_OFFSET) & (1U << 10))
#define LORA_AUX_IS_BUSY()  (!LORA_AUX_IS_HIGH())  // 低电平时模块忙
```

#### 5.3.4 环形缓冲区实现

```c
#define LORA_RX_BUF_SIZE  4096  // 接收环形缓冲区大小

typedef struct {
    u8 buf[LORA_RX_BUF_SIZE];
    volatile u32 head;  // 写入索引 (ISR 写入)
    volatile u32 tail;  // 读取索引 (任务读取)
} lora_rx_ring_t;

static lora_rx_ring_t g_lora_rx_ring;

// 向环形缓冲区写入一个字节（ISR 中调用）
static inline void ring_put(u8 byte) {
    u32 next = (g_lora_rx_ring.head + 1) % LORA_RX_BUF_SIZE;
    if (next != g_lora_rx_ring.tail) {
        g_lora_rx_ring.buf[g_lora_rx_ring.head] = byte;
        g_lora_rx_ring.head = next;
    }
}

// 从环形缓冲区读取一个字节（任务中调用）
static inline int ring_get(u8 *byte) {
    if (g_lora_rx_ring.tail == g_lora_rx_ring.head)
        return -1;  // 缓冲区空
    *byte = g_lora_rx_ring.buf[g_lora_rx_ring.tail];
    g_lora_rx_ring.tail = (g_lora_rx_ring.tail + 1) % LORA_RX_BUF_SIZE;
    return 0;
}

// 环形缓冲区可用字节数
static inline u32 ring_avail(void) {
    return (g_lora_rx_ring.head - g_lora_rx_ring.tail + LORA_RX_BUF_SIZE) % LORA_RX_BUF_SIZE;
}
```

#### 5.3.5 UART3 初始化

```c
int lora_uart_init(void)
{
    // 1. 禁用 UART3
    UART_REG(UART3_BASE, UART_CR) = 0;

    // 2. 配置波特率: 9600 @ 100MHz
    //    BaudDiv = 100,000,000 / (16 * 9600) = 651.0417
    //    整数部分 = 651, 小数部分 = 0.0417 * 64 = 2.67 ≈ 3
    UART_REG(UART3_BASE, UART_IBRD) = 651;
    UART_REG(UART3_BASE, UART_FBRD) = 3;

    // 3. 配置行控制: 8N1, 使能 FIFO
    UART_REG(UART3_BASE, UART_LCR_H) = UART_LCRH_WLEN_8 | UART_LCRH_FEN;

    // 4. 配置 FIFO 中断级别: RX=1/8满时触发中断
    UART_REG(UART3_BASE, UART_IFLS) = 0x00;  // RXIFSEL=0(1/8), TXIFSEL=0(1/8)

    // 5. 使能 RX 中断
    UART_REG(UART3_BASE, UART_IMSC) = UART_IMSC_RXIM;

    // 6. 使能 UART3
    UART_REG(UART3_BASE, UART_CR) = UART_CR_UARTEN | UART_CR_RXE | UART_CR_TXE;

    // 7. 配置 GPIO2_10 为输入 (AUX 状态检测)
    UART_REG(GPIO2_BASE, GPIO_DIR_OFFSET) &= ~(1U << 10);

    // 8. 注册中断处理函数 ← 需要根据 SDK 中断框架注册
    // FInterruptInstall(FUART3_IRQ_NUM, lora_uart_isr, NULL);

    return 0;
}
```

#### 5.3.6 UART3 中断服务函数

```c
static void lora_uart_isr(void *arg)
{
    (void)arg;

    u32 mis = UART_REG(UART3_BASE, UART_MIS);

    if (mis & UART_IMSC_RXIM) {
        // 读空 RX FIFO
        while (!(UART_REG(UART3_BASE, UART_FR) & UART_FR_RXFE)) {
            u8 byte = (u8)(UART_REG(UART3_BASE, UART_DR) & 0xFF);
            ring_put(byte);
        }
    }

    // 清除中断
    UART_REG(UART3_BASE, UART_ICR) = mis;
}
```

#### 5.3.7 帧提取函数

LoRa 数据帧格式（与 GD32 一致）：

```
┌──────┬──────┬───────┬───────┬──────────────────────┬──────┬──────┬──────┐
│ 0xAA │ 0x55 │LEN_H  │LEN_L  │    DATA (N 字节)     │ CRC8 │ 0x55 │ 0xAA │
│  帧头           │ 长度   │                        │ 校验 │ 帧尾         │
└─────────────────┴────────┴────────────────────────┴──────┴──────────────┘

帧总长 = 2(头) + 2(长度) + N(DATA) + 1(CRC8) + 2(尾) = 7 + N
```

帧提取逻辑：

```c
// 帧同步状态机
typedef enum { SYNC_HDR1, SYNC_HDR2, SYNC_LEN_H, SYNC_LEN_L, SYNC_DATA } lora_sync_t;

// 从环形缓冲区提取一帧，存入 buf，返回帧数据长度（不含帧头尾CRC），失败返回0
uint16_t lora_uart_extract_frame(uint8_t *buf, uint16_t max_len)
{
    static lora_sync_t sync_state = SYNC_HDR1;
    static uint16_t frame_len = 0;
    static uint16_t data_idx = 0;
    static uint8_t crc_acc = 0;
    uint8_t byte;

    while (ring_get(&byte) == 0) {
        switch (sync_state) {
        case SYNC_HDR1:
            if (byte == 0xAA) {
                sync_state = SYNC_HDR2;
            }
            break;
        case SYNC_HDR2:
            if (byte == 0x55) {
                sync_state = SYNC_LEN_H;
            } else if (byte != 0xAA) {
                sync_state = SYNC_HDR1;  // 不是帧头，重新找
            }
            break;
        case SYNC_LEN_H:
            frame_len = (uint16_t)byte << 8;
            sync_state = SYNC_LEN_L;
            break;
        case SYNC_LEN_L:
            frame_len |= byte;
            if (frame_len > max_len || frame_len == 0) {
                // 长度异常，丢弃
                sync_state = SYNC_HDR1;
                frame_len = 0;
                break;
            }
            data_idx = 0;
            crc_acc = 0;
            sync_state = SYNC_DATA;
            break;
        case SYNC_DATA:
            if (data_idx < frame_len) {
                buf[data_idx] = byte;
                crc_acc ^= byte;  // 更新累加异或 CRC（与 GD32 的 CRC8 算法相同）
                data_idx++;
            } else if (data_idx == frame_len) {
                // CRC8 校验
                if (crc_acc == byte) {
                    // 期望下一字节为 0x55（帧尾第一字节）
                    sync_state = SYNC_HDR1;  // 实际上直接等尾标记
                    // 继续读下一个字节验证 0x55 0xAA
                } else {
                    // CRC 校验失败，丢弃此帧
                    sync_state = SYNC_HDR1;
                    frame_len = 0;
                }
            }
            break;
        }
    }

    return 0;  // 还未提取到完整帧
}
```

> **⚠️ 上述帧提取代码为概念实现，需要根据实际 GD32 发送格式微调**。
> 建议先在仿真模式下用已知数据验证 parse\_frame() 能正确解析，再移植帧提取逻辑。
>
> **另一种更稳健的方案：暂不使用中断+环形缓冲区的帧提取，而是先用轮询方式验证能收到字节，再逐步完善。**

### 5.4 步骤三：实现 `master_lora_uart_recv()`

修改 `freertos/src/master_recv.c` 中的空函数：

```c
static uint16_t master_lora_uart_recv(uint8_t *buf, uint16_t max_len)
{
    // 调用帧提取函数
    return lora_uart_extract_frame(buf, max_len);
}
```

### 5.5 步骤四（可选）：修改 TX 路径

如果要让 FreeRTOS 能直接通过 LoRa 向终端节点发送指令：

编辑 `freertos/src/master_cmd.c`，在 `send_lora_cmd()` 中添加 UART3 发送逻辑：

```c
static void send_lora_cmd(uint8_t node_id, uint8_t cmd_code,
                           const uint8_t *params, uint8_t param_len)
{
    // ... 现有加密和组帧代码不变 ...

    // ★ 原代码通过 RPMsg 发送，现在通过 UART3 发送
    // 方式1: 直接用 UART FIFO 发送
    for (int i = 0; i < (5 + enc_len); i++) {
        while (UART_REG(UART3_BASE, UART_FR) & UART_FR_TXFF)
            ;  // 等待 TX FIFO 有空位
        UART_REG(UART3_BASE, UART_DR) = g_lora_pkt[i];
    }

    // 等待 AUX 变高（模块空闲）
    while (LORA_AUX_IS_BUSY())
        vTaskDelay(pdMS_TO_TICKS(1));

    // ... 后续重试逻辑不变 ...
}
```

> **建议**：先验证 RX 路径正常工作，再考虑 TX 路径。RX 能收到 GD32 数据就说明通信链路通了。

***

## 6. 编译与部署

### 6.1 FreeRTOS 编译

```bash
# 设置工具链路径
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"

# 进入 SDK 编译目录
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux

# 配置平台
make config_pe2204_phytiumpi_aarch64

# 编译
make clean && make all
```

**⚠️ 如果在 SDK 目录外新增了源文件（如** **`lora_uart.c`），需要修改 SDK 的 Makefile 将新文件加入编译。**

### 6.2 Makefile 修改

找到 `/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/Makefile`，
在 `C_SOURCES` 变量中添加新文件：

```makefile
C_SOURCES += \
src/lora_uart.c
```

### 6.3 固件部署到飞腾派

```bash
# 方法1: scp（如果有网络）
scp pe2204_aarch64_phytiumpi_openamp_for_linux.elf root@<飞腾派IP>:/lib/firmware/openamp_core0.elf

# 方法2: U盘或 SD 卡
# 将 .elf 文件复制到飞腾派的 /lib/firmware/ 目录
```

### 6.4 启动 FreeRTOS 从核

在飞腾派 Linux 上：

```bash
# 1. 确认设备树无冲突：检查 UART3 是否被 Linux 占用
ls /dev/ttyAMA*   # 如果没有 ttyAMA3 则说明 UART3 未被占用（正常）

# 2. 加载 RPMsg 模块
modprobe rpmsg_char rpmsg_ctrl

# 3. 启动 FreeRTOS 从核（CPU3）
echo start > /sys/class/remoteproc/remoteproc0/state

# 4. 等待从核启动
sleep 1

# 5. 查看启动日志
dmesg | tail -30
# 应看到:
#   remoteproc remoteproc0: powering up ......
#   remoteproc remoteproc0: Booting fw image openamp_core0.elf, size ......
#   remoteproc remoteproc0: remote processor ...... is now up

# 6. 检查从核状态
cat /sys/class/remoteproc/remoteproc0/state
# 应输出: running

# 7. 创建 RPMsg 通道设备
echo rpmsg_chrdev > /sys/bus/rpmsg/drivers/rpmsg_chrdev/driver_override 2>/dev/null || true
# 查找通道名称
ls /sys/bus/rpmsg/devices/
# 应看到: virtio0.rpmsg-openamp-demo-channel.-1.0 (或类似名称)
echo "virtio0.rpmsg-openamp-demo-channel.-1.0" > /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind 2>/dev/null || true

# 8. 设置权限
sudo chmod 666 /dev/rpmsg*
ls -la /dev/rpmsg*
```

### 6.5 启动 Linux 端 receiver

```bash
cd /home/alientek/Phytium
./build/iot-main  # 启动 master_receiver 显示收到的数据
```

***

## 7. GD32 终端节点准备

### 7.1 烧录终端程序

GD32L233C 终端节点运行 `/home/alientek/Phytium/GD32L233C_Prj_Master_v3/` 中的程序：

```bash
# 使用 J-Link 或 GD-Link 烧录
# 具体烧录命令取决于使用的调试器

# 确认以下宏在终端节点中正确定义:
# - 节点地址: DEST_ADDR = 0x000A (发送到主控)
# - 本机地址: LORA_ADDR = 0x000B
# - 信道: LORA_CHN = 23
# - 数据包大小: LORA_PACKSIZE = 240
```

### 7.2 终端节点上电

1. GD32 终端节点接上已配置好的 LoRa 模块
2. 上电，等待初始化完成
3. 终端节点应自动开始周期性上报状态数据

### 7.3 确认终端节点发送数据

在 GD32 的 UART 调试口查看是否有数据发送日志：

```
[Node] Sending status packet, len=XX
[Node] Sending waveform packet, len=XX
```

***

## 8. 验证流程

### 8.1 逐级验证策略（从底层到上层）

|  阶段 | 验证内容           | 方法                    | 成功标准              |
| :-: | -------------- | --------------------- | ----------------- |
|  1  | LoRa 模块硬件连接    | 用万用表测电压、通断            | VCC=3.3V, GND 连通  |
|  2  | UART3 寄存器可访问   | FreeRTOS 启动后写读 CR 寄存器 | 读回值与写入值一致         |
|  3  | GPIO2\_10 状态正确 | 读 GPIO 数据寄存器          | AUX 高电平(模块就绪)     |
|  4  | UART3 能收到字节    | 环形缓冲区有数据              | ring\_avail() > 0 |
|  5  | 帧提取正确          | 打印提取的原始帧内容            | 帧头和 CRC 正确        |
|  6  | 数据解析正确         | Linux 侧 receiver 显示数据 | 看到节点状态、波形数据       |
|  7  | 与实际仿真数据对比      | 对比真实数据和仿真数据的字段        | 所有字段值合理           |

### 8.2 快速验证命令（FreeRTOS 侧）

在 FreeRTOS 的 `master_recv_task` 中添加调试日志，验证 UART3 接收：

```c
// 在 master_recv_task 循环中添加:
if (!USE_LORA_SIMULATION) {
    static u32 last_print = 0;
    u32 now = xTaskGetTickCount();
    if ((now - last_print) > pdMS_TO_TICKS(5000)) {
        OPENAMP_DEVICE_INFO("UART3 ring avail: %lu bytes", ring_avail());
        last_print = now;
    }
}
```

### 8.3 Linux 侧验证

```bash
# 方式1: 通过 dmesg 看内核日志中 FreeRTOS 的输出
dmesg -w | grep -i "lora\|uart3\|master"

# 方式2: 直接通过 RPMsg 设备读数据
cat /dev/rpmsg0 | xxd | head -20

# 方式3: 运行 receiver 程序，看是否有数据显示
./build/iot-main
# 应看到类似输出:
# [MASTER_DATA] node=01 type=STATUS ...
# [MASTER_DATA] node=02 type=STATUS ...
```

### 8.4 GD32 终端侧验证

在 GD32 终端调试口查看发送确认：

```
[LoRa] TX done, len=XX
[LoRa] ACK received (如果有确认机制)
```

***

## 9. 踩坑记录与排错

以下是之前在移植、调试过程中总结的问题和解决方案。

### 9.1 UART3 基址错误

| 项目     | 值                                                                                                                                            |
| ------ | -------------------------------------------------------------------------------------------------------------------------------------------- |
| **现象** | UART 读写寄存器无响应或无数据                                                                                                                            |
| **原因** | 代码注释中误写 UART3\_BASE=0x2802D000，**正确值是 0x2800f000**                                                                                           |
| **解决** | 使用 SDK `fparameters_comm.h` 中的 `FUART3_BASE_ADDR = 0x2800f000`                                                                               |
| **参考** | [fparameters\_comm.h:L190](file:///home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/standalone/soc/pe220x/fparameters_comm.h#L190) |

### 9.2 Linux 设备树占用 UART3

| 项目     | 值                                                                                                       |
| ------ | ------------------------------------------------------------------------------------------------------- |
| **现象** | FreeRTOS 写 UART3 寄存器后数据被覆盖或异常                                                                           |
| **原因** | Linux 内核设备树使能了 UART3，内核驱动与 FreeRTOS 同时访问导致冲突                                                            |
| **解决** | 确保设备树中 UART3 的 `status = "disabled"`。**不需要加载之前创建的** **`lora-uart.dtso`**，因为 LoRa 由 FreeRTOS 控制而非 Linux。 |

当前创建的 `lora-uart.dtso` 是为 Linux 控制 UART3 准备的，但现在架构改为 FreeRTOS 直连，该文件**不需要使用**。

### 9.3 模块 AUX 引脚未连接

| 项目     | 值                                             |
| ------ | --------------------------------------------- |
| **现象** | 无法判断 LoRa 模块是否就绪，发送数据无响应                      |
| **原因** | ATK-MWCC68D 的 AUX 引脚用于指示模块状态（忙/闲），未连接则无法判断    |
| **解决** | 接上 AUX 到 GPIO2\_10（J1 Pin7），通过读 GPIO 电平判断模块状态 |

### 9.4 波特率不匹配

| 项目     | 值                                           |
| ------ | ------------------------------------------- |
| **现象** | 收到乱码或收不到数据                                  |
| **原因** | FreeRTOS 端 UART 波特率与 LoRa 模块配置的 UART 波特率不一致 |
| **解决** | 两边都配置为 **9600, 8N1**。用 AT+BAUD=? 确认模块波特率    |
| **注意** | AT 命令的波特率和数据通信的波特率是**同一个 UART 配置**          |

### 9.5 地址/信道不匹配

| 项目     | 值                                           |
| ------ | ------------------------------------------- |
| **现象** | LoRa 模块 AUX 正常但始终收不到数据                      |
| **原因** | 主控和终端的 LoRa 模块地址/信道配置不一致                    |
| **解决** | 在两端分别用 AT 命令确认 ADDR、DESTADDR、CHANNEL 是否互为镜像 |

### 9.6 帧格式不兼容

| 项目     | 值                                                                                     |
| ------ | ------------------------------------------------------------------------------------- |
| **现象** | 收到数据但解析失败（CRC 错误、长度异常）                                                                |
| **原因** | FreeRTOS 的帧提取逻辑与 GD32 发送的帧格式不完全一致                                                     |
| **解决** | 1. 先用 USB-TTL 监听 GD32 终端 LoRa 模块的 UART 输出，确认帧格式2. 调整 `lora_uart_extract_frame()` 的状态机 |

### 9.7 中断注册失败

| 项目     | 值                                                      |
| ------ | ------------------------------------------------------ |
| **现象** | UART3 有数据但 ISR 不触发                                     |
| **原因** | PE2204 使用 GICv3 中断控制器，UART3 的 IRQ 号为 118（88+30），需要正确注册 |
| **解决** | 使用 SDK 的 `FInterruptInstall()` 注册，参考 SDK 中 UART 中断示例   |

### 9.8 从核编译链接错误

| 项目     | 值                                                                                         |
| ------ | ----------------------------------------------------------------------------------------- |
| **现象** | `undefined reference to ...`                                                              |
| **原因** | 1. 新增的 `.c` 文件未加入 Makefile 的源文件列表2. 使用了 FreeRTOS API 但未包含正确的头文件3. 头文件路径未加入编译器的 include 路径 |
| **解决** | 1. 修改 Makefile 添加新源文件2. 确保 `#include` 路径正确3. 检查链接脚本是否包含所有必需的 `.o` 文件                      |

### 9.9 内核日志刷屏

| 项目     | 值                             |
| ------ | ----------------------------- |
| **现象** | dmesg 被大量 RPMsg 日志占满          |
| **原因** | 仿真模式下每收到一帧都打印日志，真实数据频率更高时容易刷屏 |
| **解决** | 降低日志频率，改为每 N 帧或每 N 秒打印一次统计信息  |

***

## 10. 关键文件路径速查

### 10.1 本项目文件

| 文件       | 路径                                                                                                                                              | 用途            |
| -------- | ----------------------------------------------------------------------------------------------------------------------------------------------- | ------------- |
| 主接收逻辑    | [master\_recv.c](file:///home/alientek/Phytium/freertos/src/master_recv.c)                                                                      | 仿真/真实切换 + 帧解析 |
| 命令发送     | [master\_cmd.c](file:///home/alientek/Phytium/freertos/src/master_cmd.c)                                                                        | 加密+组帧+发送路径    |
| 帧结构定义    | [data\_frame.h](file:///home/alientek/Phytium/freertos/inc/data_frame.h)                                                                        | LoRa 帧数据结构    |
| 主控头文件    | [master.h](file:///home/alientek/Phytium/freertos/inc/master.h)                                                                                 | 节点信息、任务声明     |
| RPMsg 通信 | [rpmsg-echo\_os.c](file:///home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/src/rpmsg-echo_os.c) | 核间通信核心        |

### 10.2 SDK 文件

| 文件          | 路径                                                                                                                                 | 用途                 |
| ----------- | ---------------------------------------------------------------------------------------------------------------------------------- | ------------------ |
| PE220x 参数   | [fparameters\_comm.h](file:///home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/standalone/soc/pe220x/fparameters_comm.h) | UART/GPIO/IRQ 基址定义 |
| PE2204 参数   | [fparameters.h](file:///home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/standalone/soc/pe220x/pe2204/fparameters.h)     | 芯片特有参数             |
| PL011 头文件   | [fpl011.h](file:///home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/standalone/drivers/serial/fpl011/fpl011.h)           | PL011 UART 驱动接口    |
| PL011 寄存器   | [fpl011\_hw.h](file:///home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/standalone/drivers/serial/fpl011/fpl011_hw.h)    | PL011 寄存器位定义       |
| 编译 Makefile | `example/system/amp/openamp_for_linux/Makefile`                                                                                    | FreeRTOS 编译配置      |

### 10.3 GD32 终端参考代码

| 文件      | 路径                                                                                          | 用途                     |
| ------- | ------------------------------------------------------------------------------------------- | ---------------------- |
| UART 驱动 | [mwcc68\_uart.c](file:///home/alientek/Phytium/GD32L233C_Prj_Master/app/BSP/mwcc68_uart.c)  | GD32 侧 LoRa UART 初始化参考 |
| LoRa 配置 | [mwcc68\_cfg.h](file:///home/alientek/Phytium/GD32L233C_Prj_Master/app/BSP/mwcc68_cfg.h)    | LoRa 地址/信道/速率配置        |
| 主控接收    | [master\_recv.c](file:///home/alientek/Phytium/GD32L233C_Prj_Master/app/task/master_recv.c) | GD32 原始接收逻辑（已移植）       |

### 10.4 配置参数速查

| 参数       | 主控模块 (飞腾派) | 终端模块 (GD32) | 定义位置                          |
| -------- | :--------: | :---------: | ----------------------------- |
| LoRa 地址  |   0x000A   |    0x000B   | mwcc68\_cfg.h LORA\_ADDR      |
| 目标地址     |   0x000B   |    0x000A   | mwcc68\_cfg.h DEST\_ADDR      |
| 信道       |     23     |      23     | mwcc68\_cfg.h LORA\_CHN       |
| 网络 ID    |      0     |      0      | mwcc68\_cfg.h LORA\_NETID     |
| UART 波特率 |  9600, 8N1 |  9600, 8N1  | AT 命令配置                       |
| 空中速率     |  19.2kbps  |   19.2kbps  | mwcc68\_cfg.h LORA\_RATE      |
| 发射功率     |    20dBm   |    20dBm    | mwcc68\_cfg.h LORA\_TPOWER    |
| 数据包大小    |   240 字节   |    240 字节   | mwcc68\_cfg.h LORA\_PACKSIZE  |
| 唤醒时间     |     1 秒    |     1 秒     | mwcc68\_cfg.h LORA\_WLTIME    |
| 传输模式     |    定点传输    |     定点传输    | mwcc68\_cfg.h LORA\_TMODE\_FP |

### 10.5 PE2204 硬件参数

| 参数             | 值               |
| -------------- | --------------- |
| UART3 基址       | 0x2800f000      |
| UART3 IRQ      | 118 (GICv3 SPI) |
| UART3 时钟       | 100MHz          |
| GPIO2 基址       | 0x28036000      |
| UART3 TXD 引脚   | J1 Pin8         |
| UART3 RXD 引脚   | J1 Pin10        |
| GPIO2\_10 引脚   | J1 Pin7         |
| GICv3 基址       | 0x30800000      |
| FreeRTOS CPU 核 | CPU3 (独占)       |

