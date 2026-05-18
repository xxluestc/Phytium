# FreeRTOS 从核任务流程详解

> **更新**: 2026-05-18 | **源文件**: freertos/main.c, freertos/src/*.c, freertos/inc/master.h

## 1. 任务总览

FreeRTOS 从核 (CPU3) 上运行 **4个任务**，通过 RPMsg 与 Linux 主核通信：

```
main() 启动流程:
  ├── chaos_init()          混沌加密初始化
  ├── master_init()         主控系统初始化 (节点管理, 共享内存Flash)
  ├── master_task_create()  创建3个业务任务
  │   ├── master_recv_task  (Prio=4, 512 words)
  │   ├── master_judge_task (Prio=5, 256 words)
  │   └── master_cmd_task   (Prio=3, 256 words)
  ├── rpmsg_echo_task()     创建RPMsg通信任务
  │   └── RpmsgEchoTask     (Prio=4, 8KB stack)
  └── vTaskStartScheduler() 启动调度器
```

## 2. 任务详情

### 2.1 RpmsgEchoTask — RPMsg 通信任务

| 属性 | 值 |
|------|-----|
| **优先级** | 4 |
| **栈大小** | 8KB (4096×2) |
| **源文件** | [freertos/src/rpmsg-echo_os.c](file:///home/alientek/Phytium/freertos/src/rpmsg-echo_os.c) |
| **创建位置** | [freertos/main.c](file:///home/alientek/Phytium/freertos/main.c) → `rpmsg_echo_task()` |
| **功能** | OpenAMP/RPMsg通信核心，所有跨核数据的中转站 |

**执行流程**:
```
RpmsgEchoTask()
  ├── device_init()
  │   ├── platform_create_proc()        ← 创建remoteproc实例
  │   ├── platform_setup_src_table()    ← 设置资源表
  │   ├── platform_setup_share_mems()   ← 映射共享内存
  │   └── platform_create_rpmsg_vdev()  ← 创建VirtIO RPMsg设备
  ├── init_sensor_data()                ← 初始化10组传感器模拟数据
  └── FRpmsgEchoApp()
      ├── rpmsg_create_ept()            ← 创建RPMsg端点 "rpmsg-openamp-demo-channel"
      └── while(1):
          ├── platform_poll(priv)       ← 等待消息/处理vring
          │   ├── 收到 DEVICE_MASTER_DATA (0x0020):
          │   │   → master_recv_inject_data()  注入数据到master_recv管线
          │   ├── 收到 DEVICE_SENSOR_DATA (0x0010):
          │   │   → send_all_sensor_packets()  发送传感器批量数据
          │   ├── 收到 DEVICE_CORE_CHECK (0x0003):
          │   │   → 回显相同数据 (心跳)
          │   └── 收到 DEVICE_CORE_SHUTDOWN (0x0002):
          │       → shutdown_req = 1, 退出循环
          └── rproc_get_stop_flag() 检查
```

**RPMsg端点消息处理** (`rpmsg_endpoint_cb`):

| 消息类型 | command值 | 方向 | 处理函数 |
|----------|-----------|------|----------|
| 主控数据注入 | 0x0020 | Linux→RTOS | `master_recv_inject_data()` |
| 传感器请求 | 0x0010 | Linux→RTOS | `send_all_sensor_packets()` |
| 心跳检查 | 0x0003 | 双向 | 直接回显 |
| 关闭从核 | 0x0002 | Linux→RTOS | 设置 `shutdown_req=1` |

**关键优化**:
- A2零拷贝: `g_zc_batch` 全局缓冲区，协议头+数据预分配，直接发送无memcpy
- C2边缘检测: `edge_detect_anomaly()` 直接在零拷贝缓冲区上执行阈值检测
- A3中断合并: 每50批才打印一次log

---

### 2.2 master_recv_task — 数据接收任务

| 属性 | 值 |
|------|-----|
| **优先级** | 4 |
| **栈大小** | 512 words |
| **源文件** | [freertos/src/master_recv.c](file:///home/alientek/Phytium/freertos/src/master_recv.c) |
| **功能** | LoRa帧接收、帧解析、数据分流存储 |

**数据来源**:

LoRa 模块通过 UART 连接到 **FreeRTOS CPU3 侧**。`master_recv_lora_data()` 是统一入口，通过 `USE_LORA_SIMULATION` 宏切换仿真/真实硬件：

```c
#define USE_LORA_SIMULATION  1     /* 1=仿真模式, 0=真实LoRa UART */
```

1. **数据模拟器** (当前使用, `USE_LORA_SIMULATION=1`):
   ```c
   master_recv_lora_data(buf, max_len)
     → master_sim_lora_data(buf, max_len)   ← 状态机自动生成LoRa帧
   ```
   模拟3个节点(过压/欠压/骤升)，上电自动运行，无需外部依赖。

2. **真实LoRa UART** (预留接口, `USE_LORA_SIMULATION=0`):
   ```c
   master_recv_lora_data(buf, max_len)
     → master_lora_uart_recv(buf, max_len)  ← 从UART RX环形缓冲区取帧
   ```
   待接入: UART3初始化 → RX中断/DMA → ring_buf[2048] → 帧头搜索 → 出帧。

3. **RPMsg注入** (备选, 用于调试):
   ```c
   master_recv_inject_data(data, len) // 通过RPMsg DEVICE_MASTER_DATA注入
   ```

**帧解析流程**:
```
master_recv_task()
  └── while(1):
      ├── master_recv_lora_data() 或 master_recv_inject_data()
      ├── parse_frame()             帧同步头检测 (0xAA55 + CRC8)
      └── 按数据类型分流:
          ├── DATA_TYPE_STATUS (0x01):
          │   ├── process_status_header()  解析故障头/状态头
          │   └── process_node_raw()       存储节点采样数据
          │       └── master_flash_save_node_data() → 共享内存状态区
          ├── DATA_TYPE_WAVE (0x02):
          │   ├── process_wave_header()    解析波形头
          │   └── process_flash_wave()     存储波形数据
          │       └── master_flash_save_wave_data() → 共享内存波形区
          └── DATA_TYPE_FAULT_LIST (0x06):
              └── process_fault_list()     解析故障列表
```

**帧格式**:
```
[0xAA][0x55][LEN][NODE_ID][TYPE][PAYLOAD...][CRC8][0x55][0xAA]
```

---

### 2.3 master_judge_task — 故障判决任务

| 属性 | 值 |
|------|-----|
| **优先级** | 5 (最高) |
| **栈大小** | 256 words |
| **源文件** | [freertos/src/master_judge.c](file:///home/alientek/Phytium/freertos/src/master_judge.c) |
| **执行周期** | 1000ms (MASTER_JUDGE_INTERVAL_MS) |

**执行流程**:
```
master_judge_task()
  └── while(1):
      ├── 遍历所有10个节点
      │   ├── 离线检测: elapsed > 15000ms → is_online=0
      │   ├── 故障判定: severity >= WARNING && fault_type != NONE
      │   └── 波形请求: 满足条件 → 构造 MasterInternalCmd
      │       └── xQueueSend(g_master_cmd_queue, cmd)
      └── vTaskDelayUntil(1000ms)
```

**判决规则**:
| 条件 | 动作 |
|------|------|
| 超过15秒无数据 | 标记节点离线 |
| severity >= WARNING 且有故障类型 | 生成波形请求命令 |

---

### 2.4 master_cmd_task — 命令发送任务

| 属性 | 值 |
|------|-----|
| **优先级** | 3 (最低) |
| **栈大小** | 256 words |
| **源文件** | [freertos/src/master_cmd.c](file:///home/alientek/Phytium/freertos/src/master_cmd.c) |

**执行流程**:
```
master_cmd_task()
  └── while(1):
      └── xQueueReceive(g_master_cmd_queue, cmd)  ← 阻塞等待命令
          └── 按命令类型分发:
              ├── MASTER_CMD_REQ_WAVE:
              │   └── send_lora_cmd(CMD_REQUEST_WAVEFORM)
              ├── MASTER_CMD_REQ_FAULT_LIST:
              │   └── send_lora_cmd(CMD_REQUEST_FAULT_LIST)
              ├── MASTER_CMD_CLEAR_FLASH:
              │   └── send_lora_cmd(CMD_CLEAR_FLASH)
              └── MASTER_CMD_WAVE_COLLECT:
                  └── send_lora_cmd(CMD_START_WAVE_COLLECT)
```

**send_lora_cmd() 命令发送路径**:
```
send_lora_cmd(node_id, cmd_code, params, param_len)
  ├── chaos_encrypt_packet()                  ← 混沌加密
  └── rpmsg_send_master_cmd()                 ← RPMsg DEVICE_MASTER_CMD (0x0021)
      └── FreeRTOS → RPMsg → Linux → master_receiver
          └── (后续待接入: Linux → LoRa模块 → 终端节点)
```

**支持的命令码**:
| 命令 | 代码 | 说明 |
|------|------|------|
| CMD_REQUEST_WAVEFORM | 0x10 | 请求节点波形数据 |
| CMD_REQUEST_FAULT_LIST | 0x11 | 请求故障列表 |
| CMD_CLEAR_FLASH | 0x12 | 清除Flash存储 |
| CMD_START_WAVE_COLLECT | 0x13 | 启动波形采集 |

## 3. 任务间通信机制

```
┌─────────────────────────────────────────────────────────────┐
│                    任务间数据流                               │
│                                                              │
│  ┌──────────────────┐                                       │
│  │ RpmsgEchoTask    │ ←── RPMsg 消息 ──→ Linux              │
│  │ (Prio=4)         │                                       │
│  └────────┬─────────┘                                       │
│           │ master_recv_inject_data()                        │
│           ▼                                                  │
│  ┌──────────────────┐                                       │
│  │ master_recv_task │ ← 帧解析, 更新节点状态                  │
│  │ (Prio=4)         │                                       │
│  └────────┬─────────┘                                       │
│           │ 更新 g_nodes[].severity, fault_type               │
│           ▼                                                  │
│  ┌──────────────────┐     ┌──────────────────┐              │
│  │ master_judge_task│────→│ g_master_cmd_queue│             │
│  │ (Prio=5)         │Queue│ (FreeRTOS Queue)  │              │
│  └──────────────────┘     └────────┬─────────┘              │
│                                    │ xQueueReceive()         │
│                                    ▼                         │
│                           ┌──────────────────┐              │
│                           │ master_cmd_task  │              │
│                           │ (Prio=3)         │              │
│                           └────────┬─────────┘              │
│                                    │ rpmsg_send_master_cmd() │
│                                    ▼                         │
│                           ┌──────────────────┐              │
│                           │ RpmsgEchoTask    │              │
│                           │ (rpmg_send)      │              │
│                           └────────┬─────────┘              │
│                                    │ RPMsg → Linux           │
└────────────────────────────────────┼─────────────────────────┘
                                     ▼
                                   Linux
                             master_receiver
```

## 4. 优先级设计原理

| 优先级 | 任务 | 设计原因 |
|--------|------|---------|
| **5 (最高)** | master_judge_task | 故障判决必须及时，优先级最高确保异常快速响应 |
| **4** | RpmsgEchoTask + master_recv_task | 通信与接收同级，通过FreeRTOS时间片轮转调度 |
| **3 (最低)** | master_cmd_task | 命令发送不紧急，在空闲时处理 |

**注意**: `RpmsgEchoTask` 和 `master_recv_task` 同为优先级4，FreeRTOS会以时间片轮转方式交替执行。

## 5. 共享资源

### 5.1 FreeRTOS Queue

| 队列名 | 长度 | 元素类型 | 生产者 | 消费者 |
|--------|------|---------|--------|--------|
| `g_master_cmd_queue` | 5 | `MasterInternalCmd_t` | master_judge_task | master_cmd_task |

### 5.2 共享内存Flash模拟

| 区域 | 大小 | 用途 | 管理函数 |
|------|------|------|----------|
| `g_status_buf[10][6400]` | 64KB | 节点状态数据 | `master_flash_save/load/erase_node_data()` |
| `g_wave_buf[10][6400]` | 64KB | 节点波形数据 | `master_flash_save/load/erase_wave_data()` |

来源: [freertos/src/master_sys.c](file:///home/alientek/Phytium/freertos/src/master_sys.c)

### 5.3 全局变量

| 变量 | 定义位置 | 用途 |
|------|---------|------|
| `g_nodes[10]` | master_sys.c | 节点信息数组 (在线状态、故障类型等) |
| `g_dl_buf` | master_sys.c | 下载缓冲区 (帧重组) |
| `g_ept` | rpmsg-echo_os.c | RPMsg端点指针 |
| `g_remoteproc_priv` | rpmsg-echo_os.c | remoteproc私有数据(用于platform_poll) |
| `g_zc_batch` | rpmsg-echo_os.c | 零拷贝批量发送缓冲区 |
| `shutdown_req` | rpmsg-echo_os.c | 关闭请求标志 |

## 6. LoRa数据模拟现状

| 函数 | 状态 | 说明 |
|------|------|------|
| `master_recv_lora_data()` | **统一入口** | 通过 `USE_LORA_SIMULATION` 宏分发到仿真或真实UART |
| `master_sim_lora_data()` | **已实现** | 状态机自动生成LoRa帧，3节点轮转，上电自驱动运行 |
| `master_lora_uart_recv()` | **预留接口** | 真实LoRa UART接收，待填入UART驱动代码 |
| `master_recv_inject_data()` | **可用** | 通过RPMsg DEVICE_MASTER_DATA注入模拟数据(调试用途) |

**切换方式**:
```c
// freertos/src/master_recv.c 第24行
#define USE_LORA_SIMULATION  1     /* 1=仿真模式, 0=真实LoRa UART */
```

**结论**: 当前在**没有LoRa模块**的情况下，通过 `master_sim_lora_data()` 自驱动验证全链路。接入真实LoRa模块时只需将宏改为0并在 `master_lora_uart_recv()` 中实现UART接收逻辑。