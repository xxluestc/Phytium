# Phytium PE2204 异构多核系统架构全景

> **更新**: 2026-05-18 | **状态**: 代码移植完成，LoRa模块待接入硬件验证

## 1. 系统架构总图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Phytium PE2204 SoC                                  │
│                                                                              │
│  ┌─────────────────────────────────┐   ┌─────────────────────────────────┐  │
│  │     Linux 主核 (CPU0-2, SMP)    │   │    FreeRTOS 从核 (CPU3, 独占)    │  │
│  │                                 │   │                                 │  │
│  │  CPU0: FTC310                   │   │  系统启动                       │  │
│  │  CPU1: FTC310                   │   │  main()                         │  │
│  │  CPU2: FTC664                   │   │    ├── chaos_init()             │  │
│  │                                 │   │    ├── master_init()     ★     │  │
│  │  ┌───────────────────────────┐  │   │    ├── master_task_create()     │  │
│  │  │  master_receiver          │  │   │    │   ├── master_recv_task ★  │  │
│  │  │  (src/openamp-demo/       │  │   │    │   ├── master_judge_task   │  │
│  │  │   linux-master/           │  │   │    │   └── master_cmd_task     │  │
│  │  │   master_receiver.c)      │  │   │    ├── rpmsg_echo_task()        │  │
│  │  │                           │  │   │    │   └── RpmsgEchoTask ★     │  │
│  │  │  主控数据接收程序          │  │   │    └── vTaskStartScheduler()   │  │
│  │  └────────────┬──────────────┘  │   │                                 │  │
│  │               │                 │   │  ┌───────────────────────────┐  │  │
│  │  /dev/rpmsg0  │  /dev/rpmsg_ctrl0 │   │  │  RpmsgEchoTask (Prio=4)  │  │  │
│  │  (数据通道)   │  (控制通道)   │   │  │  │  rpmsg-echo_os.c         │  │  │
│  │       ↓              ↓         │   │  │  │  ├── 零拷贝批量发送    │  │  │
│  │  ┌───────────────────────────┐  │   │  │  │  ├── 边缘异常检测(C2) │  │  │
│  │  │  rpmsg_char.ko (内核模块) │  │   │  │  │  ├── DEVICE_MASTER_DATA │  │  │
│  │  │  virtio_rpmsg_bus         │  │   │  │  │  │   接收(Linux→RTOS) │  │  │
│  │  │  rproc_virtio             │  │   │  │  │  └── DEVICE_MASTER_CMD  │  │  │
│  │  │  homo_remoteproc (phytium)│  │   │  │  │      发送(RTOS→Linux) │  │  │
│  │  └────────────┬──────────────┘  │   │  └────────────┬──────────────┘  │  │
│  │               │                 │   │               │                 │  │
│  │               │   RPMsg/VirtIO  │   │               │                 │  │
│  │               │   GICv3 SGI 9   │   │               │                 │  │
│  │               │                 │   │               │                 │  │
│  └───────────────┼─────────────────┘   └───────────────┼─────────────────┘  │
│                  │                                     │                    │
│         ┌────────┴─────────────────────────────────────┴────────┐           │
│         │              共享内存 (0xB0100000, 409MB)              │           │
│         │  ┌───────────┐  ┌───────────┐  ┌────────────────────┐  │           │
│         │  │ vring0    │  │ vring1    │  │ RPMsg 缓冲区       │  │           │
│         │  │ (TX通道)  │  │ (RX通道)  │  │ + 固件代码+数据    │  │           │
│         │  └───────────┘  └───────────┘  └────────────────────┘  │           │
│         └────────────────────────────────────────────────────────┘           │
│                                                                              │
│  ┌────────────────────────────────────────────────────────────────────────┐  │
│  │  外部接口 (待接入)                                                      │  │
│  │  ┌──────────────────────┐     ┌──────────────────────┐                  │  │
│  │  │ ATK-MWCC68D LoRa模块  │     │ 终端节点 (10个)      │                  │  │
│  │  │ UART3 + GPIO2_10     │←───→│ 通过LoRa无线通信     │                  │  │
│  │  │ (连接飞腾派J1接口)   │     │                      │                  │  │
│  │  └──────────────────────┘     └──────────────────────┘                  │  │
│  └────────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 2. 硬件资源分配

### 2.1 CPU分配

| CPU编号    | 核心类型             | MPIDR     | 逻辑CPU    | 操作系统         | 用途                |
| -------- | ---------------- | --------- | -------- | ------------ | ----------------- |
| CPU0     | FTC310 (LITTLE)  | 0x200     | cpu0     | Linux SMP    | 通用计算              |
| CPU1     | FTC310 (LITTLE)  | 0x201     | cpu1     | Linux SMP    | 通用计算              |
| CPU2     | FTC664 (big)     | 0x000     | cpu2     | Linux SMP    | 通用计算              |
| **CPU3** | **FTC664 (big)** | **0x100** | **cpu3** | **FreeRTOS** | **OpenAMP从核(独占)** |

### 2.2 内存布局

| 地址范围                        | 大小        | 用途                                 |
| --------------------------- | --------- | ---------------------------------- |
| 0x80000000 - 0x80010000     | 64KB      | Boot保留 (/memreserve/)              |
| 0x80010000 - 0xB0100000     | \~768MB   | Linux 可用内存                         |
| **0xB0100000 - 0xC9A00000** | **409MB** | **OpenAMP共享内存 (no-map, reserved)** |
| ├─ 0xB0100000               | -         | 从核固件代码入口 (openamp\_core0.elf)      |
| ├─ vring0 (TX)              | -         | Linux→FreeRTOS 发送队列                |
| ├─ vring1 (RX)              | -         | FreeRTOS→Linux 发送队列                |
| └─ 0xB0120000               | 128KB     | 共享内存Flash模拟 (状态/波形数据)              |
| 0xC9A00000+                 | \~3GB     | Linux 可用内存                         |

### 2.3 外设引脚（LoRa模块，待接入）

| 飞腾派接口     | PE2204引脚   | LoRa模块引脚     | 功能   |
| --------- | ---------- | ------------ | ---- |
| J1 Pin 8  | UART3\_TXD | LoRa RXD     | 数据发送 |
| J1 Pin 10 | UART3\_RXD | LoRa TXD     | 数据接收 |
| J1 Pin 7  | GPIO2\_10  | LoRa AUX/MD0 | 模式控制 |

对应设备树: [lora-uart.dtso](file:///home/alientek/Phytium/device-tree/lora-uart.dtso)

## 3. 软件架构分层

```
┌─────────────────────────────────────────────────────┐
│  Linux 应用层                                        │
│  ├── master_receiver     ← 主控数据接收 (当前使用)    │
│  ├── sensor_receiver     ← 传感器批量接收             │
│  ├── rpmsg-demo-single   ← 基础回显测试               │
│  └── dashboard_server    ← Web监控面板 (C3)           │
├─────────────────────────────────────────────────────┤
│  Linux 内核层                                        │
│  ├── rpmsg_char.ko       ← RPMsg字符设备驱动          │
│  ├── rpmsg_ctrl.ko       ← RPMsg控制设备驱动          │
│  ├── virtio_rpmsg_bus    ← VirtIO RPMsg总线           │
│  └── homo_remoteproc     ← 飞腾异构远程处理器驱动      │
├─────────────────────────────────────────────────────┤
│  固件层 (OpenAMP/VirtIO)                             │
│  ├── 共享内存 vring0/vring1                          │
│  ├── RPMsg 端点                                       │
│  └── IPI 中断 (GICv3 SGI 9)                          │
├─────────────────────────────────────────────────────┤
│  FreeRTOS 从核任务层                                 │
│  ├── RpmsgEchoTask       ← RPMsg通信 (Prio=4)        │
│  ├── master_recv_task    ← LoRa帧接收 (Prio=4)       │
│  ├── master_judge_task   ← 故障判决 (Prio=5)         │
│  └── master_cmd_task     ← 命令发送 (Prio=3)         │
├─────────────────────────────────────────────────────┤
│  FreeRTOS 内核 + libmetal + OpenAMP库                │
└─────────────────────────────────────────────────────┘
```

## 4. 异构核间通信

### 4.1 通信媒介

| 项目   | 说明                           |
| ---- | ---------------------------- |
| 传输协议 | RPMsg over VirtIO            |
| 物理介质 | 共享内存 (0xB0100000, 409MB)     |
| 中断通知 | GICv3 SGI 9 (软件生成中断)         |
| 通道名称 | `rpmsg-openamp-demo-channel` |
| 通道数量 | **1个** (双向，通过两个vring实现)      |

### 4.2 RPMsg 端点 (消息类型)

| 端点ID                            | 方向                   | 功能                       | 状态  |
| ------------------------------- | -------------------- | ------------------------ | --- |
| `DEVICE_MASTER_DATA` (0x0020)   | **Linux → FreeRTOS** | LoRa原始帧转发给FreeRTOS处理     | 已实现 |
| `DEVICE_MASTER_CMD` (0x0021)    | **FreeRTOS → Linux** | FreeRTOS指令经RPMsg转发到Linux | 已实现 |
| `DEVICE_SENSOR_BATCH` (0x0011)  | FreeRTOS → Linux     | 传感器批量数据 (优化后)            | 已实现 |
| `DEVICE_SENSOR_DATA` (0x0010)   | FreeRTOS → Linux     | 传感器逐个数据 (旧，保留)           | 保留  |
| `DEVICE_CORE_CHECK` (0x0003)    | 双向                   | 心跳检查                     | 已实现 |
| `DEVICE_CORE_SHUTDOWN` (0x0002) | Linux → FreeRTOS     | 关闭从核                     | 已实现 |

### 4.3 单通道说明

**是的，目前只有一个RPMsg传输通道** (`rpmsg-openamp-demo-channel`)，但这是双向通道：

- **Linux → FreeRTOS**: 通过 vring0 (TX) + SGI 9 中断通知
- **FreeRTOS → Linux**: 通过 vring1 (RX) + IPI中断通知
- 不同消息类型通过 `command` 字段 (4字节头) 区分，在同一个通道上复用

## 5. LoRa数据现状

### 关键结论：当前可以在没有LoRa模块的前提下验证整个链路

| 问题                    | 答案                                                 |
| --------------------- | -------------------------------------------------- |
| FreeRTOS侧是否有LoRa真实数据？ | **没有**，`master_recv_lora_data()` 是stub，返回0         |
| 数据从哪里来？               | Linux侧通过RPMsg `DEVICE_MASTER_DATA` 消息注入模拟数据        |
| 能否验证整个链路？             | **可以**。数据链路：Linux模拟 → RPMsg → FreeRTOS处理 → RPMsg返回 |

具体说明：

- `master_recv_lora_data()` 原GD32通过USART1(DMA+中断)接收LoRa模块数据，移植后此函数为 stub 返回0
- 通过 `master_recv_inject_data()` 函数，Linux可以将模拟的LoRa帧通过RPMsg注入到FreeRTOS的接收管线
- FreeRTOS侧的帧解析、混沌解密、故障判决等完整管线都可以被验证

详见: [通信流程详解](communication-flow.md)

## 6. 项目文件结构

```
Phytium/
├── README.md                        # 项目总览
├── PROJECT_INFO.md                  # 项目信息汇总
├── Makefile                         # 顶层构建
│
├── freertos/                        # ★ FreeRTOS从核业务代码
│   ├── main.c                       #   入口: 系统初始化, 任务创建
│   ├── src/
│   │   ├── rpmsg-echo_os.c          #   RPMsg通信核心 (OpenAMP端点)
│   │   ├── master_recv.c            #   LoRa帧接收/解析/处理管线
│   │   ├── master_judge.c           #   故障判决任务
│   │   ├── master_cmd.c             #   命令发送 (RPMsg→Linux)
│   │   ├── master_sys.c             #   主控系统/共享内存Flash模拟
│   │   ├── chaos_encrypt.c          #   混沌加解密算法
│   │   └── log.c                    #   日志系统
│   └── inc/                         #   头文件
│       ├── master.h                 #   主控数据结构/宏定义
│       ├── data_frame.h             #   LoRa帧数据结构
│       ├── chaos_encrypt.h          #   混沌加密接口
│       └── log.h                    #   日志接口
│
├── src/openamp-demo/                # Linux侧OpenAMP通信程序
│   ├── linux-master/
│   │   ├── master_receiver.c        #   ★ 主控数据接收 (当前主程序)
│   │   └── rpmsg_master.c           #   基础RPMsg echo测试
│   ├── remote-core/rpmsg_slave.c    #   从核参考源码
│   └── Makefile                     #   交叉编译
│
├── src/linux-app/                   # Linux应用 (IoT入口, 待扩展)
│   ├── main.c
│   └── Makefile
│
├── device-tree/
│   ├── openamp.dtso                 # OpenAMP设备树overlay
│   ├── lora-uart.dtso              # LoRa UART设备树overlay
│   └── phytiumpi-openamp.dtb       # 已编译设备树
│
├── demo/                            # 可部署的Linux端程序
│   ├── sensor_receiver              #   传感器批量接收
│   ├── rpmsg-demo-single            #   基础回显测试
│   ├── dashboard_server             #   Web监控面板
│   └── lifecycle_mgr                #   生命周期管理
│
├── GD32L233C_Prj_Master/            # GD32原始工程 (参考用)
├── docs/                            # 文档
│   ├── architecture.md              #   ★ 本文档: 架构全景
│   ├── freertos-task-flow.md        #   FreeRTOS任务流程
│   ├── communication-flow.md        #   通信流程详解
│   ├── knowledge-base.md            #   知识库
│   ├── setup-guide.md               #   部署指南
│   ├── debug-log.md                 #   调试日志
│   ├── optimization-record.md       #   优化记录
│   └── transplant-gd32-to-phytium.md#  GD32移植记录
│
├── scripts/                         # 部署/管理脚本
├── firmware/                        # 编译好的固件
└── logs/                            # 运行日志
```

## 7. 数据流全景 (从FreeRTOS接收到Linux)

```
┌──────────────────────────────────────────────────────────────────────────┐
│  第1步: 数据到达 FreeRTOS                                                 │
│                                                                           │
│  ┌──────────────────────┐      ┌──────────────────────────────────┐      │
│  │ 外部LoRa帧 (待接入)   │ 或   │ Linux RPMsg DEVICE_MASTER_DATA     │      │
│  │ master_recv_lora_data │      │ master_recv_inject_data()         │      │
│  │ → stub: 返回0         │      │ → 注入到接收管线                  │      │
│  └──────────┬───────────┘      └────────────────┬─────────────────┘      │
│             └──────────────┬────────────────────┘                         │
│                            ▼                                              │
│  ┌──────────────────────────────────────────────────────────────┐        │
│  │  master_recv_task (Prio=4)                                    │        │
│  │  freertos/src/master_recv.c                                   │        │
│  │  ├── 帧同步头检测 (0xAA55)                                     │        │
│  │  ├── CRC8校验                                                 │        │
│  │  ├── 帧解析 → 数据类型分流                                     │        │
│  │  │   ├── 状态数据 → process_node_raw() → 共享内存Flash        │        │
│  │  │   ├── 波形数据 → process_flash_wave() → 波形区Flash        │        │
│  │  │   └── 故障列表 → process_fault_list()                       │        │
│  │  └── 更新节点在线状态                                          │        │
│  └──────────────────────────────────────────────────────────────┘        │
│                                                                           │
│  第2步: 故障判决                                                          │
│                                                                           │
│  ┌──────────────────────────────────────────────────────────────┐        │
│  │  master_judge_task (Prio=5)                                   │        │
│  │  freertos/src/master_judge.c                                  │        │
│  │  ├── 每秒轮询所有节点                                          │        │
│  │  ├── 检测节点离线 (>15s无数据)                                 │        │
│  │  ├── 故障等级判定 → 生成波形请求                                │        │
│  │  └── 发送内部命令 → g_master_cmd_queue                         │        │
│  └──────────────────────────────────────────────────────────────┘        │
│                                                                           │
│  第3步: 命令处理                                                          │
│                                                                           │
│  ┌──────────────────────────────────────────────────────────────┐        │
│  │  master_cmd_task (Prio=3)                                     │        │
│  │  freertos/src/master_cmd.c                                    │        │
│  │  ├── 从 g_master_cmd_queue 取命令                              │        │
│  │  ├── 混沌加密 (chaos_encrypt_packet)                           │        │
│  │  └── 通过 RPMsg DEVICE_MASTER_CMD (0x0021) → Linux            │        │
│  └──────────────────────────┬───────────────────────────────────┘        │
│                             │                                             │
│  第4步: 跨核通信 (FreeRTOS → Linux) ─── RPMsg/VirtIO ───→                │
│                                                                           │
│                             ▼                                             │
│  ┌──────────────────────────────────────────────────────────────┐        │
│  │  RpmsgEchoTask (Prio=4)                                       │        │
│  │  freertos/src/rpmsg-echo_os.c                                 │        │
│  │  ├── rpmsg_endpoint_cb() → 接收Linux消息                       │        │
│  │  │   ├── DEVICE_MASTER_DATA → master_recv_inject_data()       │        │
│  │  │   └── DEVICE_SENSOR_DATA → send_all_sensor_packets()       │        │
│  │  ├── rpmsg_send_master_cmd() → 发送命令到Linux                │        │
│  │  ├── 零拷贝批量发送 (A2优化)                                   │        │
│  │  └── 边缘异常检测 (C2优化)                                     │        │
│  └──────────────────────────┬───────────────────────────────────┘        │
│                             │ vring1 + IPI中断                            │
└─────────────────────────────┼─────────────────────────────────────────────┘
                              │
┌─────────────────────────────┼─────────────────────────────────────────────┐
│  第5步: Linux 内核层        │                                             │
│                             ▼                                             │
│  ┌──────────────────────────────────────────────────────────────┐        │
│  │  rproc_vq_interrupt() → virtio_rpmsg_bus → rpmsg_char        │        │
│  │  → 数据到达 /dev/rpmsg0                                       │        │
│  └──────────────────────────┬───────────────────────────────────┘        │
│                             │                                             │
│  第6步: Linux 应用层                                                     │
│                             ▼                                             │
│  ┌──────────────────────────────────────────────────────────────┐        │
│  │  master_receiver (src/openamp-demo/linux-master/              │        │
│  │                  master_receiver.c)                            │        │
│  │  ├── open("/dev/rpmsg_ctrl0") → ioctl(CREATE_EPT)             │        │
│  │  ├── open("/dev/rpmsg0")                                      │        │
│  │  └── read() → 解析 ProtocolData                               │        │
│  │       ├── DEVICE_MASTER_CMD (0x0021) → print_master_cmd()     │        │
│  │       │   解析节点ID+命令码+参数                                │        │
│  │       └── 后续: 通过LoRa模块转发到终端节点 (待接入)             │        │
│  └──────────────────────────────────────────────────────────────┘        │
└──────────────────────────────────────────────────────────────────────────┘
```

## 8. 各环节对应的文件路径速查

| 环节         | 文件路径                                              | 核心功能                                     |
| ---------- | ------------------------------------------------- | ---------------------------------------- |
| FreeRTOS启动 | `freertos/main.c`                                 | 系统初始化，4个任务创建                             |
| LoRa帧接收    | `freertos/src/master_recv.c`                      | 帧同步检测(0xAA55)，CRC8校验，数据分流                |
| 帧数据结构      | `freertos/inc/data_frame.h`                       | DataType, FaultType, SeverityLevel 枚举    |
| 主控数据结构     | `freertos/inc/master.h`                           | MasterNodeInfo, MasterDownloadBuf, 任务参数宏 |
| 主控系统       | `freertos/src/master_sys.c`                       | 节点管理，共享内存Flash模拟 (状态区+波形区)               |
| 故障判决       | `freertos/src/master_judge.c`                     | 离线检测(15s超时)，故障判定，波形请求入队                  |
| 命令发送       | `freertos/src/master_cmd.c`                       | 混沌加密，RPMsg命令转发                           |
| 混沌加密       | `freertos/src/chaos_encrypt.c`                    | 混沌加密算法 (原GD32移植)                         |
| RPMsg通信核心  | `freertos/src/rpmsg-echo_os.c`                    | OpenAMP端点，批量发送，边缘检测                      |
| Linux接收    | `src/openamp-demo/linux-master/master_receiver.c` | RPMsg端点创建，主控命令接收/打印                      |
| LoRa设备树    | `device-tree/lora-uart.dtso`                      | UART3 + GPIO2\_10 引脚配置                   |

## 9. 关键技术特性

| 特性          | 编号 | 说明                                       | 状态 |
| ----------- | -- | ---------------------------------------- | -- |
| 批量消息合并      | A1 | 10包合并为1次RPMsg发送，延迟从19.79ms→0.03ms (659×) | ✅  |
| 零拷贝传输       | A2 | 协议头+数据预分配在发送缓冲区，消除memcpy                 | ✅  |
| 中断合并        | A3 | SGI9中断节省90%                              | ✅  |
| Vring调优     | A4 | 256desc×32KB匹配批量大小                       | ✅  |
| 边缘异常检测      | C2 | FreeRTOS侧电压/电流/温度阈值预判                    | ✅  |
| Web监控面板     | C3 | dashboard\_server提供实时Web监控               | ✅  |
| 共享内存Flash模拟 | -  | 状态数据+波形数据用共享内存代替Flash                    | ✅  |

