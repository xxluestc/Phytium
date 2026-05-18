# Phytium PE2204 LoRa 主控系统 — 项目信息汇总

> **更新**: 2026-05-18 | **状态**: GD32代码全部移植完成，LoRa模块待接入

## 一、项目基本信息

| 项目 | 内容 |
|------|------|
| 项目名称 | Phytium PE2204 LoRa 主控系统 |
| 项目路径 | `/home/alientek/Phytium` |
| 开发板 | 飞腾派 CEK8903 (Phytium Pi) |
| SoC | PE2204 (2×FTC664 + 2×FTC310) |
| 架构 | ARM64 (aarch64) |
| 系统 | Debian 12 (PIOS v3.2) |
| 内核 | 6.6.63-phytium-embedded-v3.2 |
| 开发板 IP | 192.168.88.11/24 |
| 用户 | user / root (密码: user / root) |

## 二、项目架构概览

```
Phytium PE2204 异构四核
├── Linux 主核 (CPU0-2, SMP)
│   ├── master_receiver     ← RPMsg 接收/转发 FreeRTOS 数据
│   ├── sensor_receiver     ← 传感器数据批量接收
│   ├── dashboard_server    ← Web 监控面板
│   └── lifecycle_mgr       ← 生命周期管理
│
├── FreeRTOS 从核 (CPU3, 独占)
│   ├── RpmsgEchoTask       ← OpenAMP/RPMsg 通信 (Prio=4)
│   ├── master_recv_task    ← LoRa 帧接收/解析 (Prio=4)
│   │   ├── 仿真模式: master_sim_lora_data()  ← 当前使用
│   │   └── 真实硬件: master_lora_uart_recv()  ← 预留接口
│   ├── master_judge_task   ← 故障判决 (Prio=5)
│   └── master_cmd_task     ← 命令生成/发送 (Prio=3)
│
├── OpenAMP/RPMsg ←→ 核间通信
│   ├── 共享内存: 0xB0100000 (409MB)
│   ├── 中断: GICv3 SGI 9
│   └── 通道: rpmsg-openamp-demo-channel (1条双向)
│
└── 外部接口 — LoRa模块直连FreeRTOS
    └── ATK-MWCC68D LoRa 模块 ← UART3 + GPIO2_10 → FreeRTOS CPU3
        (Linux不直接操作LoRa，只通过RPMsg接收处理后的数据)
```

> 详细架构见: [docs/architecture.md](docs/architecture.md)
> 任务流程见: [docs/freertos-task-flow.md](docs/freertos-task-flow.md)

## 三、核心代码文件

### 3.1 FreeRTOS 从核 (freertos/)

| 文件 | 功能 |
|------|------|
| [freertos/main.c](freertos/main.c) | 系统入口，初始化 + 4个任务创建 |
| [freertos/src/rpmsg-echo_os.c](freertos/src/rpmsg-echo_os.c) | ★ RPMsg通信核心，OpenAMP端点，批量发送，边缘检测 |
| [freertos/src/master_recv.c](freertos/src/master_recv.c) | LoRa帧接收/解析管线，含 `USE_LORA_SIMULATION` 宏切换仿真/真实UART |
| [freertos/src/master_judge.c](freertos/src/master_judge.c) | 故障判决，离线检测(15s超时)，波形请求生成 |
| [freertos/src/master_cmd.c](freertos/src/master_cmd.c) | 命令加密/发送，混沌加密 + RPMsg转发 |
| [freertos/src/master_sys.c](freertos/src/master_sys.c) | 节点管理，共享内存Flash模拟(状态区+波形区) |
| [freertos/src/chaos_encrypt.c](freertos/src/chaos_encrypt.c) | 混沌加解密算法 (原GD32移植) |

### 3.2 Linux 主核 (src/)

| 文件 | 功能 |
|------|------|
| [src/openamp-demo/linux-master/master_receiver.c](src/openamp-demo/linux-master/master_receiver.c) | ★ 主控数据接收，解析 RPMsg DEVICE_MASTER_CMD |
| [src/openamp-demo/linux-master/rpmsg_master.c](src/openamp-demo/linux-master/rpmsg_master.c) | RPMsg echo 基础测试 |

### 3.3 RPMsg 消息端点

| 端点 ID | 值 | 方向 | 功能 |
|---------|-----|------|------|
| DEVICE_MASTER_DATA | 0x0020 | Linux → FreeRTOS | LoRa帧转发 |
| DEVICE_MASTER_CMD | 0x0021 | FreeRTOS → Linux | 主控指令转发 |
| DEVICE_SENSOR_BATCH | 0x0011 | FreeRTOS → Linux | 传感器批量数据 |

## 四、编译工具链

### 4.1 FreeRTOS 固件编译

```bash
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux
make config_pe2204_phytiumpi_aarch64
make clean && make all
```

### 4.2 Linux 程序编译

```bash
export CROSS_COMPILE="/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"

# 编译 master_receiver
cd /home/alientek/Phytium/src/openamp-demo
make master-recv
```

### 4.3 编译器路径速查

| 用途 | 路径 |
|------|------|
| FreeRTOS 编译器 | `/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/` |
| Linux 交叉编译器 | `/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/` |

## 五、关键源码目录

| 目录 | 说明 |
|------|------|
| `/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/` | FreeRTOS SDK |
| `/home/alientek/Phytium_syscode/phytium-standalone-sdk-master/` | Bare-metal SDK |
| `/home/alientek/Phytium_syscode/内核源码/` | 内核源码 (5.10.209) |
| `/home/alientek/phytium-embedded-docs-master/` | 飞腾参考手册 |

## 六、部署与运行

### 启动流程

```bash
# 1. 加载模块
sudo modprobe rpmsg_char rpmsg_ctrl

# 2. 启动从核
echo start | sudo tee /sys/class/remoteproc/remoteproc0/state

# 3. 绑定通道
echo rpmsg_chrdev | sudo tee /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override
echo virtio0.rpmsg-openamp-demo-channel.-1.0 | sudo tee /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind

# 4. 权限
sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0

# 5. 运行
./master_receiver
```

### 验证命令

```bash
cat /sys/class/remoteproc/remoteproc0/state    # running/offline
ls /sys/bus/rpmsg/devices/                      # 通道设备
ls /dev/rpmsg*                                  # rpmsg0, rpmsg_ctrl0
dmesg | grep -i rproc                           # 启动日志
```

## 七、LoRa 模块接口 (待接入)

| 飞腾派接口 | PE2204引脚 | LoRa模块引脚 | 功能 |
|-----------|-----------|-------------|------|
| J1 Pin 8 | UART3_TXD | RXD | 数据发送 |
| J1 Pin 10 | UART3_RXD | TXD | 数据接收 |
| J1 Pin 7 | GPIO2_10 | AUX/MD0 | 模式控制 |

设备树: [device-tree/lora-uart.dtso](device-tree/lora-uart.dtso)

## 八、GD32 原始工程

| 项目 | 路径 |
|------|------|
| GD32 完整工程 | `GD32L233C_Prj_Master/` |
| 移植记录 | [docs/transplant-gd32-to-phytium.md](docs/transplant-gd32-to-phytium.md) |

## 九、文档索引

| 文档 | 内容 |
|------|------|
| [docs/architecture.md](docs/architecture.md) | ★ 架构全景: 硬件布局、内存映射、数据流、文件索引 |
| [docs/freertos-task-flow.md](docs/freertos-task-flow.md) | ★ FreeRTOS 4任务: 优先级、代码、交互关系 |
| [docs/communication-flow.md](docs/communication-flow.md) | 通信流程: 启动→数据→命令 |
| [docs/knowledge-base.md](docs/knowledge-base.md) | 知识库: DT配置、驱动架构、固件编译 |
| [docs/optimization-record.md](docs/optimization-record.md) | 优化记录: A1-A4, C2-C3 |
| [docs/setup-guide.md](docs/setup-guide.md) | 部署指南 |
| [docs/debug-log.md](docs/debug-log.md) | 调试日志 |

---

**版本**: v3.0 | **基于**: GD32L233C_Prj_Master 移植