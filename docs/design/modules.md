# 模块设计文档

> **更新**: 2026-05-18 | **当前架构**: Linux主核 + FreeRTOS从核 (GD32主控移植版)
>
> 本文档详细描述各模块的设计接口和职责分工。总览见 [architecture.md](../architecture.md)。

---

## 1. FreeRTOS 模块设计

### 1.1 入口模块 (`main.c`)

**文件**: [freertos/main.c](file:///home/alientek/Phytium/freertos/main.c)

**职责**:
- 平台初始化 (GIC, UART, 时钟)
- 混沌加密表初始化 `chaos_init()`
- 主控系统初始化 `master_init()`
- 创建主控任务 `master_task_create()`
- 启动 RPMsg 回显任务 `xTaskCreate(RpmsgEchoTask)`
- 启动 FreeRTOS 调度器 `vTaskStartScheduler()`

**依赖**:
- SDK 内部 BSP (`include/board.h`)

### 1.2 RPMsg 通信核心 (`rpmsg-echo_os.c`)

**文件**: [freertos/src/rpmsg-echo_os.c](file:///home/alientek/Phytium/freertos/src/rpmsg-echo_os.c)

**职责**:
- 初始化 OpenAMP 平台层 `device_init()`
- 创建 RPMsg 端点 `rpmsg_create_ept()`
- 主循环轮询 `platform_poll()`
- 消息回调 `rpmsg_endpoint_cb()` - 根据 command 分发
- 发送主控命令到 Linux `rpmsg_send_master_cmd()`

**定义**:
```c
#define DEVICE_MASTER_DATA    0x0020U  // Linux → FreeRTOS: 注入LoRa数据
#define DEVICE_MASTER_CMD     0x0021U  // FreeRTOS → Linux: 转发主控命令
```

**依赖**:
- OpenAMP 库 (`openamp.h`, `rpmsg.h`)

### 1.3 LoRa 帧接收 (`master_recv.c`)

**文件**: [freertos/src/master_recv.c](file:///home/alientek/Phytium/freertos/src/master_recv.c)

**职责**:
- 物理LoRa数据接收 `master_recv_lora_data()` - 当前 stub 返回 0
- RPMsg数据注入 `master_recv_inject_data()` - Linux模拟数据注入入口
- 帧同步检测 `master_check_frame_sync()`
- CRC8 校验 `calc_frame_crc8()`
- 帧解析 `parse_frame()`
- 按数据类型分流处理:
  - `process_status_header()` - 处理故障/状态头
  - `process_node_raw()` - 处理节点采样数据 → 存储到状态缓冲区
  - `process_wave_header()` - 处理波形头 → 擦除波形缓冲区
  - `process_wave()` - 处理波形数据块 → 写入波形缓冲区
  - `process_fault_list()` - 处理故障列表

**任务入口**: `master_recv_task()`

**依赖**:
- `data_frame.h` - 数据结构定义
- `master_sys.h` - 存储接口

### 1.4 故障判决 (`master_judge.c`)

**文件**: [freertos/src/master_judge.c](file:///home/alientek/Phytium/freertos/src/master_judge.c)

**职责**:
- 1秒周期遍历所有节点
- 离线检测 (`now - last_recv > 15s`)
- 故障严重程度判定
- 生成回传命令 `MasterInternalCmd_t` 并发送到命令队列

**任务入口**: `master_judge_task()`

**依赖**:
- `master.h` - 全局节点状态结构
- FreeRTOS `queue.h`

### 1.5 命令处理 (`master_cmd.c`)

**文件**: [freertos/src/master_cmd.c](file:///home/alientek/Phytium/freertos/src/master_cmd.c)

**职责**:
- 从命令队列阻塞获取命令
- 根据命令类型构造 LoRa 命令帧
- 混沌加密 `chaos_encrypt_packet()`
- 调用 `rpmsg_send_master_cmd()` 发送到 Linux

**支持命令**:
- `MASTER_CMD_REQ_WAVE` - 请求波形数据
- `MASTER_CMD_REQ_FAULT_LIST` - 请求故障列表
- `MASTER_CMD_CLEAR_FLASH` - 清除Flash
- `MASTER_CMD_WAVE_COLLECT` - 启动波形采集

**任务入口**: `master_cmd_task()`

**依赖**:
- `chaos_encrypt.h` - 加密接口
- `rpmsg-echo_os.h` - RPMsg 发送接口

### 1.6 系统存储 (`master_sys.c`)

**文件**: [freertos/src/master_sys.c](file:///home/alientek/Phytium/freertos/src/master_sys.c)

**职责**:
- 节点状态初始化
- 状态数据存储 `master_flash_save_node_data()` → `g_status_buf[]`
- 波形数据擦除/存储 → `g_wave_buf[]`

**存储方案**:
- 使用共享内存模拟 Flash:
  - 状态缓冲区: `g_status_buf` (MASTER_MAX_STORAGE_NODES × 采样点)
  - 波形缓冲区: `g_wave_buf`

**依赖**:
- `master.h` - 存储参数定义

### 1.7 混沌加密 (`chaos_encrypt.c`)

**文件**: [freertos/src/chaos_encrypt.c](file:///home/alientek/Phytium/freertos/src/chaos_encrypt.c)

**职责**:
- 初始化混沌表 `chaos_init_table()`
- 混沌加密 `chaos_encrypt_packet()`
- 混沌解密 `chaos_decrypt_packet()`

**算法**: 基于 Henon 映射的混沌加密算法

---

## 2. Linux 模块设计

### 2.1 RPMsg 接收 (`master_receiver.c`)

**文件**: [src/openamp-demo/linux-master/master_receiver.c](file:///home/alientek/Phytium/src/openamp-demo/linux-master/master_receiver.c)

**职责**:
- 打开 `/dev/rpmsg_ctrl0` → 端点创建 `ioctl(CREATE_EPT)`
- 打开 `/dev/rpmsg0` 数据通道
- `read()` 阻塞等待 RPMsg 消息
- 根据 command 分发:
  - `DEVICE_MASTER_CMD` → 解析 node_id + cmd_code → 打印
  - `DEVICE_MASTER_DATA` (预留) → 打印原始LoRa帧

**预期输出**:
```
[CMD] node=0 cmd=REQ_WAVE(0x10)
[CMD] node=2 cmd=REQ_FAULT_LIST(0x11)
```

**后续扩展**: 接收到命令后通过 UART 发送到 LoRa 模块。

---

## 3. 数据结构定义

### 3.1 LoRa 帧数据结构 (`data_frame.h`)

**文件**: [freertos/inc/data_frame.h](file:///home/alientek/Phytium/freertos/inc/data_frame.h)

主要结构体:

| 结构体 | 用途 |
|--------|------|
| `NodeSample_t` | 节点单采样点 (有功功率、无功功率、电压) |
| `NodeUploadData_t` | 节点上传数据头 (节点ID、故障类型、严重等级) |
| `FaultUploadHeader_t` | 故障上传头 (故障编号、描述、时间戳) |
| `WaveChunkHeader_t` | 波形数据块头 (采样率、采样点数) |

### 3.2 全局系统状态 (`master.h`)

**文件**: [freertos/inc/master.h](file:///home/alientek/Phytium/freertos/inc/master.h)

主要宏定义:
- `MASTER_MAX_NODES` - 最大终端节点数 (默认 10)
- `MASTER_MAX_STORAGE_NODES` - 最大存储节点数
- `MASTER_WAVE_BUFFER_SIZE` - 波形缓冲区大小

主要结构:
- `NodeStatus_t` - 单节点状态 (采样数据、在线状态、故障统计)

---

## 4. 消息协议格式

### RPMsg 消息格式

```
[4B command][2B length][nB data]
```

| command | 值 | 方向 | 用途 |
|---------|-----|------|------|
| `DEVICE_MASTER_DATA` | 0x0020 | Linux→FreeRTOS | 注入LoRa原始帧 |
| `DEVICE_MASTER_CMD` | 0x0021 | FreeRTOS→Linux | 主控回传命令 |

### LoRa 帧格式

```
[0xAA][0x55][LEN][NODE_ID][TYPE][PAYLOAD][CRC8][0x55][0xAA]
```

---

## 5. 任务优先级 (FreeRTOS)

| 任务 | 优先级 | 说明 |
|------|--------|------|
| `master_judge_task` | 5 (最高) | 故障判决，需要及时响应 |
| `RpmsgEchoTask` | 4 | RPMsg 轮询 |
| `master_recv_task` | 4 | LoRa帧接收解析 |
| `master_cmd_task` | 3 (最低) | 命令发送，可以延迟 |

优先级设计: **故障判决 > 数据接收 > 命令发送**

---

## 6. 数据流框图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                                                                          │
│                          Linux 主核 (CPU0-2)                              │
│                                                                          │
│  ┌─────────────────────────┐                                              │
│  │  master_receiver        │                                              │
│  │   (user space)          │                                              │
│  │                         │  DEVICE_MASTER_DATA (注入模拟数据)           │
│  │  open /dev/rpmsg0  ←───────────────────────────────────────────────────┘
│  │  read() / write()                                              │
│  └───────────┬───────────────────────────────────────────────────┘
│              │
│           [IPI SGI 9]
│              ↓
│        /dev/rpmsg0 (rpmsg_char.ko)
│         virtio_rpmsg_bus
│          共享内存 vring0
│              │
└──────────────┼──────────────────────────────────────────────────────────────
               │
               ▼
          共享内存 0xB0100000
               │
               ▲
               │
           [IPI SGI 9]
               │
┌──────────────┼──────────────────────────────────────────────────────────────
│              │
│           FreeRTOS CPU3
│              │
│    ┌─────────▼─────────┐
│    │  RpmsgEchoTask    │
│    │  rpmsg-echo_os.c  │
│    │                   │
│    │  command = 0x0020 ├────→ master_recv_inject_data()
│    └─────────┬─────────┘
│              │
│    ┌─────────▼────────────┐
│    │  master_recv_task    │  master_recv.c
│    │  parse_frame()       │  CRC + 类型分流
│    │  ↓                   │
│    │  状态 → g_status_buf │  master_sys.c
│    │  波形 → g_wave_buf   │
│    └─────────┬────────────┘
│              │ 节点状态更新通知
│              ↓
│    ┌──────────────────────┐  1秒周期
│    │  master_judge_task    │  master_judge.c
│    │  (优先级=5, 最高)    │  离线检测 + 故障判决
│    └─────────┬────────────┘
│              │  生成内部命令
│              ↓
│    ┌──────────────────────┐  阻塞等待队列
│    │  master_cmd_task      │  master_cmd.c
│    │  (优先级=3, 最低)    │  混沌加密 + RPMsg发送
│    └─────────┬────────────┘
│              │
│              └──────────┐
│                         ↓
│                 rpmsg_send_master_cmd()
│                 command = 0x0021
│                   写入 vring1 + IPI → Linux
│                         │
└─────────────────────────┼──────────────────────────────────────────────────
                          │
                          ↓
                     Linux 主核
                     master_receiver.c
                     read() → 解析 node_id/cmd_code
                     → 打印 [CMD] node=X cmd=REQ_WAVE

                     (后续扩展: 发送 UART3 → LoRa模块 → 终端节点)
```

---

## 7. 接口依赖关系

```
main.c
  ├─> chaos_encrypt.c → 初始化表
  ├─> master_sys.c    → 全局存储初始化
  ├─> rpmsg-echo_os.c → 创建 RpmsgEchoTask
  └─> 创建 master_recv_task / master_judge_task / master_cmd_task

RpmsgEchoTask (rpmsg-echo_os.c)
  ├─> DEVICE_MASTER_DATA → master_recv_inject_data()
  └─> DEVICE_MASTER_CMD  → (无, 由 master_cmd_task 发起)

master_recv_task (master_recv.c)
  ├─> master_recv_lora_data() (stub)
  ├─> parse_frame() → CRC8
  ├─> process_node_raw() → master_flash_save_node_data()
  ├─> process_wave() → master_flash_save_wave_data()
  └─> 节点状态更新 → 唤醒 master_judge_task (通过优先级调度)

master_judge_task (master_judge.c)
  ├─> 读取 NodeStatus_t
  ├─> 判决 → 生成 MasterInternalCmd_t
  └─> xQueueSend(g_master_cmd_queue) → master_cmd_task

master_cmd_task (master_cmd.c)
  ├─> xQueueReceive()
  ├─> chaos_encrypt_packet()
  └─> rpmsg_send_master_cmd() → rpmsg_send() → Linux
```

---

## 8. 设计原则

1. **单一职责**: 每个源文件对应一个功能模块
2. **优先级分层**: 故障判决 > 数据接收 > 命令发送
3. **队列解耦**: 判决和发送通过队列解耦，不直接调用
4. **可移植**: 保持接口清晰，硬件相关代码隔离
5. **可测试**: RPMsg 注入入口支持无硬件全链路验证
