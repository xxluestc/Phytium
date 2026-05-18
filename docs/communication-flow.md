# OpenAMP 异构多核通信流程详解

> **更新**: 2026-05-18 | **当前架构**: Linux主核 + FreeRTOS从核 (GD32移植版)

## 1. 通信架构总览

```
┌──────────────────────────────────────────────────────────────────┐
│                     Phytium PE2204 SoC                            │
│                                                                   │
│  Linux 主核 (CPU0-2)               FreeRTOS 从核 (CPU3)          │
│  ┌────────────────────────┐      ┌───────────────────────────┐  │
│  │  master_receiver       │      │  RpmsgEchoTask (Prio=4)    │  │
│  │  src/openamp-demo/     │      │  rpmsg-echo_os.c           │  │
│  │  linux-master/         │      │                            │  │
│  │  master_receiver.c     │      │  ┌─ DEVICE_MASTER_DATA ←──┼──┤
│  │       ↕                │      │  └─ DEVICE_MASTER_CMD  ──→┼──┤
│  │  /dev/rpmsg0           │RPMsg │                            │  │
│  │  /dev/rpmsg_ctrl0      │←────→│  master_recv_task(Prio=4) │  │
│  │       ↕                │SGI 9 │  master_recv.c             │  │
│  │  rpmsg_char.ko         │      │                            │  │
│  │  virtio_rpmsg_bus      │      │  master_judge_task(Prio=5) │  │
│  │  homo_remoteproc       │      │  master_judge.c            │  │
│  └───────────┬────────────┘      │                            │  │
│              │                   │  master_cmd_task(Prio=3)   │  │
│              │    ┌──────────────┴──────────────────────────┐ │  │
│              └────│ 共享内存 0xB0100000 (409MB)              │ │  │
│                   │ vring0(TX) + vring1(RX) + 缓冲区 + 固件  │ │  │
│                   └─────────────────────────────────────────┘ │  │
│                                                                   │
│              ┌──────────────────────────┐                        │
│              │  IPI 中断 (GICv3 SGI 9)  │                        │
│              │  Linux ←→ FreeRTOS 通知  │                        │
│              └──────────────────────────┘                        │
└──────────────────────────────────────────────────────────────────┘
```

## 2. 完整数据流: FreeRTOS 接收到数据处理 → Linux 接收并展示

### 第1步: 数据到达 FreeRTOS 从核

**数据来源(主方案)**:

| 方式 | 函数 | 文件 | 状态 |
|------|------|------|------|
| **数据模拟器 (推荐)** | `master_sim_lora_data()` → `master_recv_lora_data()` | [freertos/src/master_recv.c](file:///home/alientek/Phytium/freertos/src/master_recv.c) | **已实现 (自驱动)** |
| 物理LoRa模块 | `master_recv_lora_data()` (替换) | [freertos/src/master_recv.c](file:///home/alientek/Phytium/freertos/src/master_recv.c) | 待接入硬件 |
| RPMsg注入 | `master_recv_inject_data()` | [freertos/src/master_recv.c](file:///home/alientek/Phytium/freertos/src/master_recv.c) | 可用 (备选) |

**数据模拟器路径** (当前可验证, 无需任何外部依赖):
```
master_recv_task (每10ms循环)
  → master_recv_lora_data(raw_buf, 270)
    → master_sim_lora_data(buf, max_len)  ← ★ 内部状态机
      Phase 0: 等待300ms (SIM_INIT_WAIT=30次×10ms)
      Phase 1: 构造 FaultUploadHeader_t 帧 (SEVERITY_DANGER, FAULT_OVER_VOLTAGE)
      Phase 2: 分批发送 80个 NodeSample_t (10个/帧, 共8帧)
      Phase 3: 等待500ms (留给judge判决)
      循环: 轮转 3个节点 (node 0/1/2, 不同故障类型)
  → master_recv_inject_data(raw_buf, raw_len)  ← 复用注入管线
    → parse_frame() → CRC8校验通过 → 按rx_type分流
      → process_status_header()  (记录节点故障)
      → process_node_raw()       (累积采样点)
```

**物理LoRa模块连接**:
LoRa模块(UART)逻辑上连接到 **FreeRTOS CPU3 侧**，因为主控管线(master_recv → master_judge → master_cmd)全部运行在FreeRTOS侧。物理上PE2204所有CPU核均可访问UART3寄存器，推荐FreeRTOS直接驱动以消除RPMsg转发的延迟。

**RPMsg注入路径** (备选):
```
Linux (master_receiver.c)
  → write() → /dev/rpmsg0
    → rpmsg_char.ko
      → virtio_rpmsg_bus
        → vring0 (共享内存)
          → SGI 9 中断 → FreeRTOS

FreeRTOS (rpmsg-echo_os.c)
  → platform_poll() 检测到消息
    → rpmsg_endpoint_cb()
      → case DEVICE_MASTER_DATA (0x0020):
          → master_recv_inject_data(data, len)
```

### 第2步: LoRa帧解析

**文件**: [freertos/src/master_recv.c](file:///home/alientek/Phytium/freertos/src/master_recv.c)
**任务**: `master_recv_task` (优先级4)

```
master_recv_task()
  ├── 获取原始数据
  └── parse_frame(raw_data, raw_len, out_data, out_len, out_type, out_timestamp)
      ├── 帧同步头检测: 0xAA 0x55
      ├── CRC8 校验: calc_frame_crc8()
      └── 按数据类型分流:
          ├── 故障/状态头 → process_status_header()
          │   解析 FaultUploadHeader_t / NodeUploadData_t
          │   记录节点ID、故障类型、严重等级、采样率等
          │   [数据结构定义: freertos/inc/data_frame.h]
          │
          ├── 状态采样数据 → process_node_raw()
          │   按节点累积采样点 (NodeSample_t)
          │   完整后 → master_flash_save_node_data()
          │   [存储: freertos/src/master_sys.c → g_status_buf[]]
          │
          ├── 波形头 → process_wave_header()
          │   解析 WaveChunkHeader_t (采样率、采样点数)
          │   [存储: freertos/src/master_sys.c → master_flash_erase_wave()]
          │
          ├── 波形数据 → process_flash_wave()
          │   按偏移量写入波形数据
          │   [存储: freertos/src/master_sys.c → g_wave_buf[]]
          │
          └── 故障列表 → process_fault_list()
              解析有效故障条目
```

### 第3步: 故障判决

**文件**: [freertos/src/master_judge.c](file:///home/alientek/Phytium/freertos/src/master_judge.c)
**任务**: `master_judge_task` (优先级5, 最高)
**周期**: 1000ms

```
master_judge_task()
  └── 每1秒遍历 MASTER_MAX_NODES (10) 个节点:
      ├── 检查 g_nodes[i] 状态
      ├── 离线检测: now - last_recv_time > 15000ms → is_online = 0
      └── 故障判定:
          如果 severity >= SEVERITY_WARNING && fault_type != FAULT_NONE:
            构造 MasterInternalCmd_t:
              cmd_type = MASTER_CMD_REQ_WAVE
              node_id  = i
              sample_rate = 6000
              duration_ms = 250
            → xQueueSend(g_master_cmd_queue, &cmd)
```

### 第4步: 命令生成与加密

**文件**: [freertos/src/master_cmd.c](file:///home/alientek/Phytium/freertos/src/master_cmd.c)
**任务**: `master_cmd_task` (优先级3, 最低)

```
master_cmd_task()
  └── xQueueReceive(g_master_cmd_queue, &cmd)  // 阻塞等待
      └── switch(cmd.cmd_type):
          ├── MASTER_CMD_REQ_WAVE:
          │   send_lora_cmd(node_id, CMD_REQUEST_WAVEFORM, params, 2)
          ├── MASTER_CMD_REQ_FAULT_LIST:
          │   send_lora_cmd(node_id, CMD_REQUEST_FAULT_LIST, NULL, 0)
          ├── MASTER_CMD_CLEAR_FLASH:
          │   send_lora_cmd(node_id, CMD_CLEAR_FLASH, NULL, 0)
          └── MASTER_CMD_WAVE_COLLECT:
              send_lora_cmd(node_id, CMD_START_WAVE_COLLECT, params, 2)

send_lora_cmd():
  ├── chaos_encrypt_packet()   ← 混沌加密 [freertos/src/chaos_encrypt.c]
  └── rpmsg_send_master_cmd()  ← RPMsg发送 [freertos/src/rpmsg-echo_os.c]
```

### 第5步: 跨核 RPMsg 传输 (FreeRTOS → Linux)

**发送方文件**: [freertos/src/rpmsg-echo_os.c](file:///home/alientek/Phytium/freertos/src/rpmsg-echo_os.c)
**函数**: `rpmsg_send_master_cmd(node_id, cmd_code, params, param_len)`

```
rpmsg_send_master_cmd()
  ├── 构造 ProtocolData:
  │   command = DEVICE_MASTER_CMD (0x0021)
  │   length  = 2 + param_len
  │   data[0] = node_id
  │   data[1] = cmd_code
  │   data[2..] = params
  └── rpmsg_send(g_ept, &tx_data, 6 + tx_data.length)
      ├── 写入 vring1 (共享内存 RX vring)
      └── 触发 IPI 中断 → Linux

【物理路径】
FreeRTOS CPU3:
  rpmsg_send() → virtio_queue_notify() → 写入 GICv3 SGI 9 寄存器
  → 中断路由到 Linux CPU0-2

Linux 内核:
  rproc_vq_interrupt() → virtio_rpmsg_bus → rpmsg_char.ko
  → 数据到达 /dev/rpmsg0 读缓冲区
```

### 第6步: Linux 应用接收与处理

**文件**: [src/openamp-demo/linux-master/master_receiver.c](file:///home/alientek/Phytium/src/openamp-demo/linux-master/master_receiver.c)

```
main() 启动:
  ├── open("/dev/rpmsg_ctrl0")        ← 打开控制设备
  ├── ioctl(CREATE_EPT, "rpmsg-openamp-demo-channel")  ← 创建RPMsg端点
  └── open("/dev/rpmsg0")             ← 打开数据通道

while(running):
  └── read(rpmsg_fd, rx_buf)
      └── 解析 ProtocolData:
          ├── command = DEVICE_MASTER_CMD (0x0021):
          │   print_master_cmd():
          │     node_id = pkt->data[0]
          │     cmd_code = pkt->data[1]
          │     → 打印: "[CMD] node=X cmd=REQ_WAVE(0x10)"
          └── (后续可用) DEVICE_MASTER_DATA → 打印LoRa帧内容
```

### 第7步: (后续) Linux → LoRa模块 → 终端节点

```
待实现路径:
master_receiver
  → 解析 DEVICE_MASTER_CMD
  → 获取命令参数 (node_id, cmd_code, params)
  → 通过 UART3 发送到 ATK-MWCC68D LoRa 模块
    → LoRa 模块无线发送
      → 终端节点接收并执行命令
```

## 3. 数据格式定义

### RPMsg 消息格式

所有消息遵循统一的 `ProtocolData` 格式:

```
[4B command][2B length][nB data]
```

| 字段 | 大小 | 说明 |
|------|------|------|
| command | 4字节 | 消息类型 (0x0020/0x0021/0x0010/0x0011等) |
| length | 2字节 | data字段长度 |
| data | 变长 | 消息载荷 |

定义位置: [freertos/src/rpmsg-echo_os.c](file:///home/alientek/Phytium/freertos/src/rpmsg-echo_os.c) 和 [src/openamp-demo/linux-master/master_receiver.c](file:///home/alientek/Phytium/src/openamp-demo/linux-master/master_receiver.c)

### LoRa 帧格式

```
[0xAA][0x55][LEN][NODE_ID][TYPE][PAYLOAD][CRC8][0x55][0xAA]
```

定义位置: [freertos/src/master_recv.c](file:///home/alientek/Phytium/freertos/src/master_recv.c)

### 数据结构定义

全部在 [freertos/inc/data_frame.h](file:///home/alientek/Phytium/freertos/inc/data_frame.h):

| 结构体 | 用途 |
|--------|------|
| `NodeSample_t` | 节点采样数据 (有功功率、无功功率、电压) |
| `NodeUploadData_t` | 节点上传数据头 |
| `FaultUploadHeader_t` | 故障上传数据头 |
| `WaveChunkHeader_t` | 波形数据块头 |

## 4. 各环节对应文件速查

| 环节 | 步骤 | 文件路径 | 核心函数 |
|------|------|---------|---------|
| 数据到达 | 1 | [freertos/src/master_recv.c](file:///home/alientek/Phytium/freertos/src/master_recv.c) | `master_sim_lora_data()` → `master_recv_lora_data()` (自驱动模拟) / `master_recv_inject_data()` (RPMsg注入) |
| RPMsg接收 | 1 | [freertos/src/rpmsg-echo_os.c](file:///home/alientek/Phytium/freertos/src/rpmsg-echo_os.c) | `rpmsg_endpoint_cb()` → `DEVICE_MASTER_DATA` |
| 帧解析 | 2 | [freertos/src/master_recv.c](file:///home/alientek/Phytium/freertos/src/master_recv.c) | `parse_frame()` → CRC8 + 数据分流 |
| 数据结构 | 2 | [freertos/inc/data_frame.h](file:///home/alientek/Phytium/freertos/inc/data_frame.h) | `NodeSample_t`, `FaultUploadHeader_t` 等 |
| 状态存储 | 2 | [freertos/src/master_sys.c](file:///home/alientek/Phytium/freertos/src/master_sys.c) | `master_flash_save_node_data()` |
| 波形存储 | 2 | [freertos/src/master_sys.c](file:///home/alientek/Phytium/freertos/src/master_sys.c) | `master_flash_save_wave_data()` |
| 故障判决 | 3 | [freertos/src/master_judge.c](file:///home/alientek/Phytium/freertos/src/master_judge.c) | `master_judge_task()` 1秒周期 |
| 命令入队 | 3 | [freertos/src/master_judge.c](file:///home/alientek/Phytium/freertos/src/master_judge.c) | `xQueueSend(g_master_cmd_queue)` |
| 命令加密 | 4 | [freertos/src/master_cmd.c](file:///home/alientek/Phytium/freertos/src/master_cmd.c) | `chaos_encrypt_packet()` |
| 命令发送 | 4 | [freertos/src/rpmsg-echo_os.c](file:///home/alientek/Phytium/freertos/src/rpmsg-echo_os.c) | `rpmsg_send_master_cmd()` |
| 跨核传输 | 5 | 共享内存 + SGI 9 | RPMsg over VirtIO |
| Linux接收 | 6 | [src/openamp-demo/linux-master/master_receiver.c](file:///home/alientek/Phytium/src/openamp-demo/linux-master/master_receiver.c) | `read()` → `print_master_cmd()` |
| LoRa发送 | 7 | (待实现) | Linux UART → LoRa模块 → 终端节点 |

## 5. 启动流程

```
Step 1: Linux 加载驱动
  sudo modprobe rpmsg_char rpmsg_ctrl

Step 2: 启动 FreeRTOS 从核
  echo start > /sys/class/remoteproc/remoteproc0/state
  → homo_rproc_start()
    → remove_cpu(3) / 加载固件到 0xB0100000
    → PSCI CPU_ON → CPU3 开始执行

Step 3: FreeRTOS 初始化
  main() [freertos/main.c]
    → chaos_init() → master_init() → master_task_create()
    → rpmsg_echo_task() → xTaskCreate(RpmsgEchoTask)
    → vTaskStartScheduler()

Step 4: RPMsg 通道建立
  RpmsgEchoTask → device_init()
    → platform_create_proc() / platform_setup_share_mems()
    → platform_create_rpmsg_vdev() →创建 VirtIO RPMsg 设备
    → rpmsg_create_ept("rpmsg-openamp-demo-channel")
  virtio_rpmsg_bus 检测到端点 → 创建 /sys/bus/rpmsg/devices/

Step 5: 绑定用户驱动
  echo rpmsg_chrdev > .../driver_override
  echo virtio0... > .../bind
  → /dev/rpmsg0 创建

Step 6: Linux 应用连接
  master_receiver.c
    → open("/dev/rpmsg_ctrl0")
    → ioctl(CREATE_EPT, "rpmsg-openamp-demo-channel")
    → open("/dev/rpmsg0")
```

## 6. 停止流程

```bash
# FreeRTOS从核 (rpmsg-echo_os.c)
echo stop > /sys/class/remoteproc/remoteproc0/state
  → shutdown_req = 1
  → RpmsgEchoTask 退出循环
  → rpmsg_destroy_ept()
  → platform_cleanup()
  → FPsciCpuOff()  → CPU3 下电
```

## 7. 关于 LoRa 模拟的说明

**当前状态**: LoRa模块未接入硬件，但通过 `master_sim_lora_data()` 可自驱动验证全链路。

```
【当前可验证路径】(无需LoRa模块, 无需Linux注入, 上电自动运行)
master_sim_lora_data() ← 状态机自动生成帧
  → master_recv_lora_data() → master_recv_task 获取
    → master_recv_inject_data() → 复用完整注入管线
      → parse_frame() → CRC8校验 → 帧解析
        → process_status_header()  [节点0: FAULT_OVER_VOLTAGE, DANGER]
        → process_node_raw()       [80个NodeSample_t → 共享内存Flash]
      → master_judge_task (1s) → 检测到DANGER → xQueueSend(MASTER_CMD_REQ_WAVE)
      → master_cmd_task → send_lora_cmd()
        → chaos_encrypt_packet() 加密命令
        → rpmsg_send_master_cmd() → RPMsg明文 → Linux
          → master_receiver.c: "[CMD] node=0 cmd=REQ_WAVE(0x10)"
  循环: node 0(过压) → node 1(欠压) → node 2(骤升) → node 0...

【物理LoRa完整路径】(需要LoRa模块)
终端节点 → LoRa无线 → ATK-MWCC68D → UART3 → FreeRTOS
  → master_recv → judge → cmd → chaos_encrypt → LoRa TX → 终端节点
```

## 8. 混沌加密安全边界

**加密区域** = LoRa空中无线链路 (对抗电磁监听、重放攻击)

```
                            ┌──────────────┐
                            │  密文传输     │
                            │  [sync][cipher│
                            │   text...]    │
                            └──┬────────┬──┘
                  ┌────────────▼┐      ┌▼────────────┐
                  │  主控下发    │      │  节点上报    │
                  │ LoRa TX     │      │ LoRa RX     │
                  └──────┬──────┘      └──────┬──────┘
                         │                    │
         ┌───────────────▼── FreeRTOS ────────▼───────────┐
         │ 1. 加密命令下发:                                │
         │    master_cmd_task → send_lora_cmd()             │
         │    → chaos_encrypt_packet() → LoRa TX            │
         │                                                 │
         │ 2. 解密数据接收:                                │
         │    LoRa RX → chaos_decrypt_packet(sync_code)     │
         │    → process_status_header/process_node_raw()   │
         │                                                 │
         │ 3. ★ RPMsg 明文 (不经过LoRa空间, 无需加密)     │
         │    rpmsg_send_master_data(plaintext)             │
         │    rpmsg_send_master_cmd(plaintext)              │
         └──────────────┬─ RPMsg 明文 ──────────────────────┘
                        │
         ┌──────────────▼── Linux (CPU0-2) ───────────────┐
         │  master_receiver.c:                              │
         │    → handle_master_data(raw)  // 明文payload      │
         │    → handle_master_cmd(raw)   // 明文cmd参数      │
         └─────────────────────────────────────────────────┘
```

**设计原则**: FreeRTOS侧是加密/解密的唯一入口。
- 命令**生成**在 FreeRTOS 侧 (master_judge_task 判决后自动生成，不依赖Linux)
- 命令**加密**在 FreeRTOS 侧 (send_lora_cmd → chaos_encrypt_packet)
- RPMsg 明文副本仅供 Linux 监控/日志/UI，Linux 不参与决策闭环

## 9. 异核通信实现位置

| 层 | 位置 | 说明 |
|-----|------|------|
| FreeRTOS 应用层 | [freertos/src/rpmsg-echo_os.c](file:///home/alientek/Phytium/freertos/src/rpmsg-echo_os.c) | `rpmsg_send()`, `rpmsg_endpoint_cb()`, `platform_poll()` |
| FreeRTOS OpenAMP库 | SDK内置 | `rpmsg_create_ept()`, `rpmsg_send()`, `platform_create_proc()` |
| 共享内存初始化 | [freertos/src/rpmsg-echo_os.c](file:///home/alientek/Phytium/freertos/src/rpmsg-echo_os.c) (资源表) + 设备树 | vring地址、大小定义 |
| Linux 内核驱动 | `drivers/remoteproc/homo_remoteproc.c` (飞腾定制) | `homo_rproc_kick()` 写GICv3 SGI寄存器 |
| Linux 应用层 | [src/openamp-demo/linux-master/master_receiver.c](file:///home/alientek/Phytium/src/openamp-demo/linux-master/master_receiver.c) | `/dev/rpmsg0` read/write |

**通道数量**: 1个 (`rpmsg-openamp-demo-channel`)，双向复用，通过command字段区分消息。