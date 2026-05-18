# Phytium PE2204 LoRa 主控系统

在飞腾派 CEK8903 开发板上实现 **异构多核 LoRa 主控系统**：Linux 主核 (CPU0-2) 负责数据接收/转发，FreeRTOS 从核 (CPU3) 负责 LoRa 帧处理/故障判决/命令生成，通过 OpenAMP/RPMsg 进行核间通信。

> **当前状态**: GD32 代码已全部移植到 FreeRTOS 从核。LoRa 模块通过 UART 直连 **FreeRTOS CPU3 侧** (Linux 不直接操作 LoRa)。当前使用 `master_sim_lora_data()` 仿真器自驱动验证全链路，无需 LoRa 硬件。接入真实模块时只需切换 `USE_LORA_SIMULATION` 宏。

## 项目文档导航

| 文档 | 内容 |
|------|------|
| [docs/architecture.md](docs/architecture.md) | ★ **架构全景图** - 硬件布局、内存映射、数据流、所有关键文件 |
| [docs/freertos-task-flow.md](docs/freertos-task-flow.md) | ★ **FreeRTOS 任务流程** - 4个任务的优先级、代码、交互 |
| [docs/communication-flow.md](docs/communication-flow.md) | ★ **通信流程详解** - 启动/数据/命令各阶段流程 |
| [docs/knowledge-base.md](docs/knowledge-base.md) | 知识库 - 硬件配置、驱动架构、编译部署 |
| [docs/transplant-gd32-to-phytium.md](docs/transplant-gd32-to-phytium.md) | GD32 移植记录 |
| [docs/optimization-record.md](docs/optimization-record.md) | 性能优化记录 (A1-A4, C2-C3) |
| [docs/setup-guide.md](docs/setup-guide.md) | 部署指南 |
| [docs/debug-log.md](docs/debug-log.md) | 调试日志 |

## 快速开始

### 启动 OpenAMP 通信

```bash
# 1. 加载内核模块
sudo modprobe rpmsg_char rpmsg_ctrl

# 2. 启动 FreeRTOS 从核 (CPU3)
echo start | sudo tee /sys/class/remoteproc/remoteproc0/state

# 3. 绑定 RPMsg 通道驱动
echo rpmsg_chrdev | sudo tee /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override
echo virtio0.rpmsg-openamp-demo-channel.-1.0 | sudo tee /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind

# 4. 设置设备权限
sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0

# 5. 运行主控数据接收程序
./demo/master_receiver

# 6. 停止
echo stop | sudo tee /sys/class/remoteproc/remoteproc0/state
```

## 项目结构

```
Phytium/
├── README.md                           # 项目说明 (本文档)
├── PROJECT_INFO.md                     # 项目信息汇总
├── Makefile                            # 顶层构建
│
├── freertos/                           # ★ FreeRTOS 从核业务代码
│   ├── main.c                          #   系统启动入口, 任务创建
│   ├── src/
│   │   ├── rpmsg-echo_os.c             #   ★ RPMsg通信核心 (OpenAMP端点)
│   │   ├── master_recv.c               #   LoRa帧接收/解析管线
│   │   ├── master_judge.c              #   故障判决任务
│   │   ├── master_cmd.c                #   命令生成/发送 (RPMsg→Linux)
│   │   ├── master_sys.c               #   节点管理, 共享内存Flash模拟
│   │   ├── chaos_encrypt.c            #   混沌加解密算法
│   │   └── log.c                       #   日志系统
│   └── inc/
│       ├── master.h                    #   主控数据结构/宏定义
│       ├── data_frame.h                #   LoRa帧数据结构
│       ├── chaos_encrypt.h             #   混沌加密接口
│       └── log.h                       #   日志接口
│
├── src/openamp-demo/                   # Linux 侧 OpenAMP 通信
│   ├── linux-master/
│   │   ├── master_receiver.c           #   ★ 主控数据接收 (当前主程序)
│   │   └── rpmsg_master.c              #   RPMsg echo 测试
│   ├── remote-core/rpmsg_slave.c       #   从核参考源码
│   └── Makefile                        #   交叉编译
│
├── src/linux-app/                      # Linux IoT入口程序
├── device-tree/                        # 设备树配置文件
├── demo/                               # 编译好的Linux可执行程序
├── GD32L233C_Prj_Master/               # GD32原始工程 (参考)
├── scripts/                            # 部署管理脚本
├── docs/                               # 项目文档
└── logs/                               # 运行日志
```

## 硬件平台

| 项目 | 详情 |
|------|------|
| 开发板 | 飞腾派 CEK8903 (Phytium Pi) |
| SoC | PE2204 (2×FTC664 + 2×FTC310) |
| 架构 | ARM64 (aarch64) |
| 系统 | Debian 12 (PIOS v3.2) |
| 内核 | 6.6.63-phytium-embedded-v3.2 |
| 开发板 IP | 192.168.88.11/24 |
| 用户 | user / root (密码: user / root) |

## CPU 分配

| CPU | 核心 | MPIDR | 用途 |
|-----|------|-------|------|
| CPU0 | FTC310 (LITTLE) | 0x200 | Linux SMP |
| CPU1 | FTC310 (LITTLE) | 0x201 | Linux SMP |
| CPU2 | FTC664 (big) | 0x000 | Linux SMP |
| **CPU3** | **FTC664 (big)** | **0x100** | **FreeRTOS 从核 (OpenAMP 独占)** |

## 通信架构

```
Linux主核 (CPU0-2)                   FreeRTOS从核 (CPU3)
┌──────────────────────┐          ┌──────────────────────────┐
│  master_receiver     │          │  RpmsgEchoTask (Prio=4)   │
│  /dev/rpmsg0         │  RPMsg   │  ├─ DEVICE_MASTER_DATA ← │
│  rpmsg_char.ko       │ ←──────→ │  └─ DEVICE_MASTER_CMD  → │
│  virtio_rpmsg_bus    │  SGI 9   │                           │
│  homo_remoteproc     │          │  master_recv_task (Prio=4)│
└──────────┬───────────┘          │  master_judge_task(Prio=5)│
           │                      │  master_cmd_task  (Prio=3)│
           │      ┌───────────────┴──────────────────────────┐│
           └──────│  共享内存 0xB0100000 (409MB)              ││
                  │  vring0 + vring1 + RPMsg缓冲区 + 固件     ││
                  └──────────────────────────────────────────┘│
```

**关键答案**:
- **异核通信**: 共享内存 + GICv3 SGI 9 中断，实现位置在 `rpmsg-echo_os.c` (FreeRTOS侧) 和内核 `homo_remoteproc` 驱动 (Linux侧)
- **通道数量**: **1个** RPMsg 通道 (`rpmsg-openamp-demo-channel`)，双向复用，通过 `command` 字段区分消息类型
- **LoRa数据**: 当前使用 `master_sim_lora_data()` 仿真器自驱动验证全链路。LoRa 模块直连 FreeRTOS CPU3 侧 UART，Linux 不直接操作 LoRa。接入真实硬件时切换 `USE_LORA_SIMULATION` 宏为 `0`，在 `master_lora_uart_recv()` 填入 UART 驱动。

## 开发资源

| 资源 | 路径 |
|------|------|
| FreeRTOS SDK | `/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/` |
| 裸机 SDK | `/home/alientek/Phytium_syscode/phytium-standalone-sdk-master/` |
| FreeRTOS 编译器 | `/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/` |
| Linux 编译器 | `/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/` |
| 内核源码 (5.10) | `/home/alientek/Phytium_syscode/内核源码/` |
| 参考手册 | `/home/alientek/phytium-embedded-docs-master/` |

## 参考链接

- 飞腾嵌入式文档: https://gitee.com/phytium_embedded/phytium-embedded-docs
- OpenAMP 手册: https://gitee.com/phytium_embedded/phytium-embedded-docs/tree/master/open-amp
- OpenAMP 官方: https://www.openampproject.org/

## 许可证

MIT License

---

**版本**: v3.0 | **更新**: 2026-05-18 | **状态**: GD32代码移植完成，LoRa仿真器自驱动运行，真实UART接口预留