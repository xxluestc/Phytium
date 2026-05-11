# OpenAMP 异构多核通信流程详解

> **当前状态**: 设备树已配置 → Bare-metal/FreeRTOS 双版本 → A1批量合并(659×) → C2边缘异常检测 → Web面板实时监控

## 1. 架构总览

```
┌─────────────────────────────────────────────────────────────────┐
│                    Phytium PE2204 SoC                            │
│                                                                  │
│  Linux主核 (CPU0-2, SMP)          FreeRTOS/bare-metal 从核       │
│  ┌─────────────────────────┐    ┌─────────────────────────────┐ │
│  │  CPU0: FTC310           │    │   CPU3: FTC664 (独占)       │ │
│  │  CPU1: FTC310           │    │                             │ │
│  │  CPU2: FTC664           │    │  RpmsgEchoTask (FreeRTOS)   │ │
│  │                         │    │  或 main() (bare-metal)     │ │
│  │  sensor_receiver        │    │       ↑                     │ │
│  │       ↓                 │    │  RPMsg endpoint             │ │
│  │  /dev/rpmsg_ctrl0       │    │       ↑                     │ │
│  │  /dev/rpmsg0 (数据通道)  │    │  OpenAMP lib                │ │
│  │       ↓                 │    │       ↑                     │ │
│  │  rpmsg_char.ko          │    │  virtio (vring)             │ │
│  │       ↓                 │    │       ↑                     │ │
│  │  virtio_rpmsg_bus       │    │  libmetal                   │ │
│  │       ↓                 │    │       ↑                     │ │
│  │  rproc-virtio           │    │  FreeRTOS Kernel / 裸机     │ │
│  │       ↓                 │    │       ↑                     │ │
│  │  homo_remoteproc        │    │  PSCI CPU_ON                │ │
│  └──────────┬──────────────┘    └──────────────┬──────────────┘ │
│             │                                  │                │
│             │  ┌──────────────────────────┐    │                │
│             └──│   共享内存 (0xB0100000)    │────┘                │
│                │   - vring0 (TX)           │                     │
│                │   - vring1 (RX)           │                     │
│                │   - RPMsg buffers         │                     │
│                │   - 固件代码+数据          │                     │
│                └──────────────────────────┘                     │
│                                                                  │
│             ┌──────────────────────────┐                        │
│             │   IPI 中断 (GICv3 SGI 9) │                        │
│             │   Linux ←→ 从核 通知     │                        │
│             └──────────────────────────┘                        │
└─────────────────────────────────────────────────────────────────┘
```

### CPU 分配

| CPU | 核心 | MPIDR | 用途 |
|-----|------|-------|------|
| CPU0 | FTC310 | 0x200 | Linux SMP |
| CPU1 | FTC310 | 0x201 | Linux SMP |
| CPU2 | FTC664 | 0x000 | Linux SMP |
| CPU3 | FTC664 | 0x100 | **从核 (OpenAMP 独占)** |

### 从核操作系统

| 类型 | SDK | 源文件 | 入口 |
|------|-----|--------|------|
| **FreeRTOS** (当前) | phytium-free-rtos-sdk | `rpmsg-echo_os.c` | `main()` → `xTaskCreate(RpmsgEchoTask)` → `vTaskStartScheduler()` |
| Bare-metal | phytium-standalone-sdk | `slaver_00_example.c` | `main()` → `slave_init()` → `FRpmsgEchoApp()` |

## 2. 通信流程 (Linux → 从核 → Linux)

### 2.1 启动阶段

```
Step 1: Linux 启动
  └── 内核解析设备树 → 发现 homo_rproc@0 节点
      └── homo_remoteproc 驱动 probe
          ├── 解析 reserved-memory (0xB0100000, 409MB)
          ├── 映射共享内存为可执行 (PAGE_KERNEL_EXEC)
          ├── 注册 SGI 9 中断处理
          ├── 注册 CPU hotplug 回调
          ├── homo_core_of_init() → 解析子节点 "homo,rproc-core"
          └── rproc_add() → /sys/class/remoteproc/remoteproc0

Step 2: 用户启动从核
  $ echo start > /sys/class/remoteproc/remoteproc0/state
  └── homo_rproc_start()
      ├── remove_cpu(3)            # 下电 CPU3
      ├── 加载 openamp_core0.elf 到 0xB0100000
      ├── 刷新 I/D-Cache
      └── PSCI CPU_ON → CPU3 从 0xB0100000 开始执行

Step 3: 从核初始化 (FreeRTOS)
  └── openamp_core0.elf 启动
      ├── main() 入口
      ├── rpmsg_echo_task() → xTaskCreate(RpmsgEchoTask, 8KB栈, 优先级4)
      ├── vTaskStartScheduler() → FreeRTOS 调度器启动
      ├── RpmsgEchoTask:
      │   ├── device_init() - 初始化 libmetal + OpenAMP
      │   ├── 创建 RPMsg 端点 "rpmsg-openamp-demo-channel"
      │   └── FRpmsgEchoApp() - 主循环 platform_poll()
      └── 等待主核消息

Step 4: RPMsg 通道建立
  └── virtio_rpmsg_bus 检测到从核端点
      └── 创建设备: /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0

Step 5: 用户绑定驱动
  $ echo rpmsg_chrdev > .../driver_override
  $ echo virtio0... > .../rpmsg_chrdev/bind
  └── /dev/rpmsg0 创建
```

### 2.2 传感器数据通信流程 (当前实现)

```
Linux (sensor_receiver)               FreeRTOS从核 (RpmsgEchoTask)
────────────────────────               ──────────────────────────
1. ioctl(CREATE_EPT, "rpmsg-openamp-demo-channel")
   在 /dev/rpmsg_ctrl0 创建端点
                                       (等待消息...)
2. write(DEVICE_SENSOR_DATA)  ────→   3. rpmsg_endpoint_cb()
   发送传感器数据请求                      收到 DEVICE_SENSOR_DATA 命令
                                           ↓
                                      4. send_all_sensor_packets()
                                         发送10组传感器数据:
                                          Packet 1: ID=1, V=220.5, A=1.25, T=27.3
                                          Packet 2: ID=2, V=221.0, A=1.30, T=28.1
                                          ...
                                          Packet 10: ID=10, V=221.2, A=1.32, T=35.2
                                          (每组间隔 platform_poll 处理vring)
5. read() ← 接收10个数据包
   逐个解析 SensorPacket
   打印: [PKT X] ID=X ts=X V=X A=X T=X [STATUS]
                                          ↓
6. 打印标志位:
   [COMPLETED] Batch N: Received 10/10
   [STATS] Total: N batches, N*10 packets
                                          ↓
7. sleep(2) → 下一批请求 ────────→     (等待下一批请求...)
   重复步骤2-7 (持续运行)
```

### 2.3 基础回显通信流程 (rpmsg-demo-single)

```
Linux 用户程序                     内核                        从核
─────────────                     ────                        ────
write(fd, CHECK命令+数据)
  └── rpmsg_char.ko
      └── virtio_rpmsg_bus
          └── 将数据写入 vring0
              └── SGI 9 → 从核
                                        ──→ rpmsg_recv()
                                            └── 回显相同数据
                                            └── rpmsg_send() → vring1
  ←── rproc_vq_interrupt()
  ←── virtio_rpmsg_bus
  ←── rpmsg_char.ko
read(fd, buf) ← "Hello World! No:X"
```

### 2.4 关键数据路径

```
发送: 用户程序 → write() → /dev/rpmsg0 → rpmsg_char → virtio_rpmsg_bus
      → vring (共享内存) → SGI 9 → 从核 → rpmsg_recv()

接收: 从核 → rpmsg_send() → vring (共享内存) → IPI → Linux
      → rproc_vq_interrupt() → virtio_rpmsg_bus → rpmsg_char
      → /dev/rpmsg0 → read() → 用户程序
```

## 3. 代码修改方法

### 3.1 FreeRTOS 从核代码 (当前使用)

**位置**: `phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux/`

```
openamp_for_linux/
├── main.c              ← FreeRTOS 入口 (创建任务, 启动调度器)
├── src/
│   └── rpmsg-echo_os.c ← ★ 通信逻辑 + 传感器数据发送
├── common/             ← 共享头文件
├── configs/            ← 平台配置文件
│   └── pe2204_aarch64_phytiumpi_openamp_for_linux.config
├── Kconfig             ← Kconfig (需FREERTOS_SDK_DIR)
└── makefile            ← 构建配置
```

**修改从核逻辑** → 在 `rpmsg-echo_os.c` 中:
- `rpmsg_endpoint_cb()` — 消息处理回调 (添加新命令)
- `send_all_sensor_packets()` — 传感器数据批量发送
- `RpmsgEchoTask()` — FreeRTOS 任务入口

### 3.2 Bare-metal 从核代码

**位置**: `phytium-standalone-sdk-master/example/system/amp/openamp_for_linux/`

```
openamp_for_linux/
├── main.c              ← 从核主入口
├── src/
│   └── slaver_00_example.c  ← ★ 通信逻辑 (主要修改对象)
├── configs/            ← 平台配置文件
└── makefile
```

### 3.3 Linux 端代码

**位置**: 项目 `demo/` 目录

| 文件 | 用途 |
|------|------|
| `sensor_receiver.c` | 传感器数据批量接收 (当前) |
| `rpmsg-demo-single.c` | 基础回显测试 |

**修改 Linux 逻辑** → 使用 `/dev/rpmsg_ctrl0` 创建端点，`/dev/rpmsg0` 收发数据。

## 4. 编译方法

### 4.1 FreeRTOS 固件编译 (当前)

**前置**: standalone SDK 必须复制到 `freertos-sdk/standalone/` 目录下。

```bash
# 工具链
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"

# 进入目录
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux

# 配置和编译
make config_pe2204_phytiumpi_aarch64
make clean && make all
# 输出: pe2204_aarch64_phytiumpi_openamp_for_linux.elf
```

**构建系统修复记录** (初次搭建需要):
1. 复制 standalone SDK: `cp -r phytium-standalone-sdk-master/ freertos-sdk/standalone/`
2. `Kconfig` 第22行: `source "$(SDK_DIR)/../freertos.kconfig"` → `source "$(FREERTOS_SDK_DIR)/freertos.kconfig"`
3. `makefile` 第2行: 改为 `FREERTOS_SDK_DIR := $(abspath ...)` 并 `export`
4. `tools/freertos_comonents.mk` 第2行: 改为 `$(abspath $(SDK_DIR)/..)`

### 4.2 Bare-metal 固件编译

```bash
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"

cd /home/alientek/Phytium_syscode/phytium-standalone-sdk-master/phytium-standalone-sdk-master/example/system/amp/openamp_for_linux

make config_pe2204_phytiumpi_aarch64
make clean && make all
# 输出: pe2204_aarch64_phytiumpi_openamp_core0.elf
```

### 4.3 Linux 程序编译

```bash
export CROSS_COMPILE="/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"

${CROSS_COMPILE}gcc -Wall -O2 -std=c11 -o sensor_receiver sensor_receiver.c
```

## 5. 部署和烧写

### 5.1 从核固件部署

```bash
# 复制到开发板
scp <firmware.elf> user@192.168.88.11:/tmp/openamp_core0.elf
ssh user@192.168.88.11 "sudo cp /tmp/openamp_core0.elf /lib/firmware/"

# 方法1: 重启从核 (FreeRTOS 可能不响应stop, 推荐重启系统)
ssh user@192.168.88.11 "sudo reboot"

# 方法2: 如果从核支持stop (bare-metal)
ssh user@192.168.88.11 "
  echo stop | sudo tee /sys/class/remoteproc/remoteproc0/state
  sleep 1
  echo start | sudo tee /sys/class/remoteproc/remoteproc0/state
"
```

### 5.2 Linux 程序部署

```bash
scp sensor_receiver user@192.168.88.11:~/
ssh user@192.168.88.11 "chmod +x ~/sensor_receiver"
```

### 5.3 设备树修改 (需要时)

设备树只有修改硬件资源（共享内存大小、中断号等）才需要更新。修改应用层代码**不需要更新设备树**。

如果需要修改设备树，流程见 `docs/setup-guide.md`。

## 6. 验证方法

### 6.1 检查从核状态

```bash
ssh user@192.168.88.11 "cat /sys/class/remoteproc/remoteproc0/state"
# running = 正常, offline = 未启动, crashed = 崩溃
```

### 6.2 检查 RPMsg 通道

```bash
ssh user@192.168.88.11 "ls /sys/bus/rpmsg/devices/"
# 应看到: virtio0.rpmsg-openamp-demo-channel.-1.0
```

### 6.3 查看内核日志

```bash
ssh user@192.168.88.11 "dmesg | grep -iE 'rproc|rpmsg|virtio' | tail -20"
```

### 6.4 运行传感器测试 (当前主测试)

```bash
ssh -tt user@192.168.88.11 "
  sudo modprobe rpmsg_char rpmsg_ctrl
  echo start | sudo tee /sys/class/remoteproc/remoteproc0/state
  sleep 2
  echo rpmsg_chrdev | sudo tee /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override
  echo virtio0.rpmsg-openamp-demo-channel.-1.0 | sudo tee /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind
  sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0
  timeout 30 ~/sensor_receiver
"
```

### 6.5 快速验证脚本

```bash
ssh user@192.168.88.11 '
echo "=== remoteproc: $(cat /sys/class/remoteproc/remoteproc0/state) ==="
echo "=== RPMsg channels ===" && ls /sys/bus/rpmsg/devices/ 2>/dev/null
echo "=== /dev/rpmsg ===" && ls -la /dev/rpmsg* 2>/dev/null
echo "=== dmesg last 5 ===" && dmesg | grep -iE "rproc|rpmsg" | tail -5
'
```

## 7. 当前通信配置

| 参数 | 值 | 说明 |
|------|-----|------|
| 通道名 | `rpmsg-openamp-demo-channel` | 主核和从核必须一致 |
| 从核 OS | FreeRTOS (RpmsgEchoTask, 8KB栈, 优先级4) | 也支持 bare-metal |
| 从核地址 | `0` | RPMsg 目的地址 |
| 主核地址 | `0xFFFFFFFF` (RPMSG_ADDR_ANY) | 自动分配 |
| 共享内存基址 | `0xB0100000` | 物理地址 |
| 共享内存大小 | `0x19900000` (409MB) | 固件代码 + vring + 数据缓冲 |
| IPI 中断 | SGI 9 | GICv3 软件生成中断 |
| 从核 CPU | CPU 3 (FTC664) | 由 Linux SMP 剥离 |
| 传感器数据包 | 10包/批, 每2秒一批 | SensorPacket 结构体 (24字节) |

## 8. 故障排查

| 现象 | 原因 | 解决 |
|------|------|------|
| `/sys/class/remoteproc/` 为空 | 设备树无 OpenAMP 节点 | 确认 dtb 包含嵌套结构 `homo,rproc` + `homo,rproc-core` |
| `OF: reserved mem:` 未打印 | 未用启动 dtb 直接修改 | 不要用 overlay，直接修改 boot dtb |
| `cpuhp setup state failed -16` | CPU hotplug 状态残留 | 重启开发板 |
| probe 成功但无设备 | 用了平铺结构 (5.10) | kernel 6.6 需嵌套 `homo,rproc-core` 子节点 |
| state = running 但无通道 | FreeRTOS 固件崩溃 | 重启开发板重新加载固件 |
| sensor只收1包 | `platform_poll` priv 指针错误 | FreeRTOS 需保存 remoteproc 结构体全局指针 |
| `Permission denied` | 设备权限 | `chmod 666 /dev/rpmsg*` |
| `remoteproc can't stop` | FreeRTOS 不响应 stop | `sudo reboot` 重启开发板 |
| 串口打印过多无法输入命令 | FreeRTOS 每批打印日志到UART1 | 已优化为每50批打印一次; 日常用SSH控制 |
| `/dev/rpmsg` 设备过多(>100) | 多次启停面板未清理端点 | `sudo reboot`; 使用 `~/stop-openamp.sh` 正确停止 |

## 9. 通信程序操作 (一键启动/停止)

### 一键启动

在开发板上直接运行:
```bash
~/start-openamp.sh
```

### 一键停止

```bash
~/stop-openamp.sh
```

脚本自动处理: 模块加载/卸载、从核启停、通道绑定、面板启动、资源清理。

### 手动操作 (备选)



### 启动流程 (完整序列, 已测试)

主板重启后, 按以下顺序启动:

```bash
# Step 1: 加载内核模块
sudo modprobe rpmsg_char rpmsg_ctrl

# Step 2: 启动从核 (FreeRTOS)
echo start | sudo tee /sys/class/remoteproc/remoteproc0/state
sleep 2  # 等待从核启动

# Step 3: 绑定 RPMsg 通道
echo rpmsg_chrdev | sudo tee /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override
echo virtio0.rpmsg-openamp-demo-channel.-1.0 | sudo tee /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind

# Step 4: 设置设备权限
sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0

# Step 5: 启动监控面板
nohup ~/dashboard_server > /tmp/dashboard.log 2>&1 &

# Step 6 (可选): 启动生命周期管理器 (B1+B2+B3)
nohup ~/lifecycle_mgr > /tmp/lifecycle.log 2>&1 &
```

### 验证通信正常

```bash
# 检查 rpmsg 设备数 (正常应为 2-5 个)
ls /dev/rpmsg* | wc -l

# 检查面板数据 (应为非零)
curl -s http://localhost:8080/stats | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d["batches"])'
```

### 停止流程 (完整序列)

```bash
# Step 1: 停止面板和生命周期管理器
killall -9 dashboard_server lifecycle_mgr 2>/dev/null

# Step 2: 停止从核
echo stop | sudo tee /sys/class/remoteproc/remoteproc0/state

# Step 3: 卸载模块 (注意顺序)
sudo rmmod rpmsg_ctrl
sudo rmmod rpmsg_char

# Step 4: 验证清理
ls /dev/rpmsg*          # 应无输出
```

### 仅重启面板 (不重启从核)

如果只是修改了面板代码, **不需要重启从核**, 只需要:

```bash
killall -9 dashboard_server
# 复制新面板程序后:
nohup ~/dashboard_server > /tmp/dashboard.log 2>&1 &
```

### 重新部署固件后

如果修改了 FreeRTOS/bare-metal 固件, **必须重启系统**:

```bash
scp new_firmware.elf user@192.168.88.11:/tmp/openamp_core0.elf
ssh user@192.168.88.11 "sudo cp /tmp/openamp_core0.elf /lib/firmware/ && sudo reboot"
```

### 常见问题: /dev/rpmsg 设备数过多

**现象**: `ls /dev/rpmsg* | wc -l` 超过 100, 面板数据显示 0 批次。

**原因**: 多次启动 dashboard_server 或 lifecycle_mgr 而不清理, 每次创建新的 RPMsg 端点但不释放旧的。

**解决**: `sudo reboot` 重启系统。**不要在短时间内反复启停面板程序。**

## 10. 优化记录

详见 [optimization-record.md](optimization-record.md)

| 优化 | 效果 |
|------|------|
| A1 批量合并 | 延迟 19.79ms→0.03ms (659×) |
| C2 边缘检测 | FreeRTOS 阈值预判, 21次告警/70包 |
| C3 Web面板 | 实时监控面板 http://192.168.88.11:8080 |
| `remoteproc can't stop rproc: -1` | FreeRTOS 不响应 stop | `sudo reboot` 重启开发板 |
