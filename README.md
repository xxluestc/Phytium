# Phytium Pi OpenAMP 异构多核通信项目

在飞腾派 CEK8903 开发板上实现 **Linux 主核** + **FreeRTOS/bare-metal 从核** 之间的 OpenAMP 异构多核通信。

## 项目状态

| 阶段 | 状态 | 说明 |
|------|------|------|
| 设备树配置 | **完成** | 嵌套结构 `homo,rproc` + `homo,rproc-core` |
| Bare-metal 通信 | **完成** | 基础回显 + 10组传感器数据 |
| FreeRTOS 通信 | **完成** | 10组传感器数据，持续批量收发 |
| Linux 接收程序 | **完成** | 批量接收→处理→打印标志→循环 |

## 项目结构

```
Phytium/
├── README.md                        # 项目说明
├── firmware/
│   ├── openamp_core0.elf            # 从核固件 (bare-metal)
│   └── openamp_core0_freertos.elf   # 从核固件 (FreeRTOS)
├── device-tree/
│   ├── openamp.dtso                 # 设备树 overlay 源文件
│   └── phytiumpi-openamp.dtb        # 已编译设备树 (含 OpenAMP 节点)
├── demo/
│   ├── rpmsg-demo-single.c          # 基础回显测试 (C源码)
│   ├── rpmsg-demo-single            # 基础回显测试 (ARM64)
│   ├── sensor_receiver.c            # 传感器数据接收程序 (C源码)
│   ├── sensor_receiver              # 传感器数据接收程序 (ARM64)
│   └── rpmsg-echo_freertos.c        # FreeRTOS从核源码 (参考)
├── config/                          # 配置文件
├── docs/
│   ├── communication-flow.md        # 通信流程详解 ★
│   ├── knowledge-base.md            # 知识库 ★
│   ├── debug-log.md                 # 调试日志 ★
│   └── setup-guide.md               # 部署指南
├── logs/                            # 运行日志
└── tools/                           # 辅助工具
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

| CPU | 核心 | 用途 |
|-----|------|------|
| CPU0 | FTC310 | Linux SMP |
| CPU1 | FTC310 | Linux SMP |
| CPU2 | FTC664 | Linux SMP |
| CPU3 | FTC664 | **从核 (OpenAMP 独占, FreeRTOS/bare-metal)** |

## 快速开始

### 启动 OpenAMP 通信

```bash
# 1. 加载模块
sudo modprobe rpmsg_char rpmsg_ctrl

# 2. 启动从核 (FreeRTOS)
echo start | sudo tee /sys/class/remoteproc/remoteproc0/state

# 3. 绑定通道
echo rpmsg_chrdev | sudo tee /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override
echo virtio0.rpmsg-openamp-demo-channel.-1.0 | sudo tee /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind

# 4. 设置权限
sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0

# 5. 运行传感器数据接收 (10包/批, 持续运行)
./demo/sensor_receiver

# 6. 或运行基础回显测试
./demo/rpmsg-demo-single

# 7. 停止
echo stop | sudo tee /sys/class/remoteproc/remoteproc0/state
```

### 预期输出 (sensor_receiver)

```
[COMPLETED] Batch 1: Received 10/10 sensor packets
  [PKT  1] ID= 1 ts=    0 V=220.50V A=1.25A T=27.3C [NORMAL]
  [PKT  7] ID= 7 ts=  600 V=221.50V A=1.35A T=33.4C [ERROR ]
  ...
  [COMPLETED] Batch N: Received 10/10 sensor packets
```

## 通信架构

```
┌──────────────────────────────────────────────────────────────┐
│                    Phytium PE2204                             │
│                                                              │
│  Linux主核 (CPU0-2, SMP)          FreeRTOS从核 (CPU3)        │
│  ┌─────────────────────┐          ┌──────────────────────┐   │
│  │  sensor_receiver    │          │  RpmsgEchoTask       │   │
│  │       ↕              │          │       ↕               │   │
│  │  /dev/rpmsg0         │  RPMsg   │  RPMsg endpoint      │   │
│  │  rpmsg_char.ko      │ ←VirtIO→ │  OpenAMP + libmetal  │   │
│  │  virtio_rpmsg_bus    │  SGI 9   │  FreeRTOS Kernel     │   │
│  │  homo_remoteproc     │          │                      │   │
│  └─────────────────────┘          └──────────────────────┘   │
│            ↕                              ↕                   │
│     ┌──────────────────────────────────────┐                 │
│     │   共享内存 0xB0100000 (409MB)         │                 │
│     │   vring + RPMsg缓冲区 + 固件代码      │                 │
│     └──────────────────────────────────────┘                 │
└──────────────────────────────────────────────────────────────┘
```

## 开发资源

| 资源 | 路径 |
|------|------|
| 裸机 SDK | `/home/alientek/Phytium_syscode/phytium-standalone-sdk-master/` |
| FreeRTOS SDK | `/home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/` |
| 裸机编译器 | `GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/` |
| Linux 编译器 | `GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/` |
| 内核源码 (5.10) | `Phytium_syscode/内核源码/` |
| 参考手册 | `phytium-embedded-docs-master/` |

## 参考链接

- 飞腾嵌入式文档: https://gitee.com/phytium_embedded/phytium-embedded-docs
- OpenAMP 手册: https://gitee.com/phytium_embedded/phytium-embedded-docs/tree/master/open-amp
- 部署教程: https://blog.csdn.net/lizongjun126com/article/details/138340633
- OpenAMP 官方: https://www.openampproject.org/

## 许可证

MIT License

---

**版本**: v2.1 | **更新**: 2026-05-11 | **状态**: FreeRTOS 传感器数据通信已打通
