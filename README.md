# Phytium Pi OpenAMP 异构多核通信项目

在飞腾派 CEK8903 开发板上实现 **Linux 主核** (Core 0-1, FTC664) 与 **裸机从核** (Core 3, FTC310) 之间的 OpenAMP 异构多核通信。

## 项目结构

```
Phytium/
├── README.md                       # 项目说明
├── firmware/
│   └── openamp_core0.elf           # 从核裸机 OpenAMP 固件
├── device-tree/
│   ├── openamp.dtso                # 设备树 overlay 源文件
│   └── phytiumpi-openamp.dtb       # 编译好的设备树二进制
├── demo/
│   ├── rpmsg-demo-single.c         # RPMsg 单通道测试程序
│   └── rpmsg-demo-single           # ARM64 可执行文件
├── config/                         # 配置文件
├── docs/
│   ├── knowledge-base.md           # 知识库
│   ├── debug-log.md                # 调试日志
│   └── setup-guide.md              # 部署指南
├── logs/                           # 运行日志
├── scripts/                        # 部署脚本
└── tools/                          # 辅助工具
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
| 默认用户 | user / root |
| 默认密码 | user / root |

## 快速开始

```bash
# 1. 加载 RPMsg 内核模块
sudo modprobe rpmsg_char rpmsg_ctrl

# 2. 启动远程处理器（从核）
echo start | sudo tee /sys/class/remoteproc/remoteproc0/state

# 3. 查看状态（应显示 running）
cat /sys/class/remoteproc/remoteproc0/state

# 4. 绑定 RPMsg 通道
echo rpmsg_chrdev | sudo tee /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override
echo virtio0.rpmsg-openamp-demo-channel.-1.0 | sudo tee /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind

# 5. 设置设备权限
sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0

# 6. 运行通信测试
./demo/rpmsg-demo-single

# 7. 停止远程处理器
echo stop | sudo tee /sys/class/remoteproc/remoteproc0/state
```

## 预期输出

```
received message: Hello World! No:1
received message: Hello World! No:2
...
received message: Hello World! No:100
```

## 通信架构

```
Linux主核 (Core 0-1, FTC664)   共享内存 0xB0100000    裸机从核 (Core 3, FTC310)
┌──────────────────────┐     ┌──────────────┐     ┌──────────────────────┐
│   rpmsg-demo-single  │ ←→  │  RPMsg/VirtIO │ ←→  │  openamp_core0.elf  │
│   /dev/rpmsg0         │     │  SGI 9 (IPI)  │     │  RPMsg endpoint     │
└──────────────────────┘     └──────────────┘     └──────────────────────┘
```

## 开发资源

- 源码目录: `/home/alientek/Phytium_syscode/`
- 内核源码: `内核源码/kernel-5.10.209-phytium-embedded-v2.2.tar.gz`
- 编译器: `GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/`
- 裸机 SDK: `phytium-standalone-sdk-master/`
- 参考手册: `phytium-embedded-docs-master/`

## 参考链接

- 飞腾嵌入式文档: https://gitee.com/phytium_embedded/phytium-embedded-docs
- OpenAMP 手册: https://gitee.com/phytium_embedded/phytium-embedded-docs/tree/master/open-amp
- 部署教程: https://blog.csdn.net/lizongjun126com/article/details/138340633
- OpenAMP 官方: https://www.openampproject.org/

## 许可证

MIT License

---

**版本**: v2.0 | **更新**: 2026-05-11
