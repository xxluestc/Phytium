# 调试日志

## 2026-05-11: OpenAMP 异构多核通信打通

### 问题 1: 设备树 Overlay 无法使能 OpenAMP

**现象**: 通过 `/sys/kernel/config/device-tree/overlays/` 应用 overlay 添加 `reserved-memory` 和 `homo_rproc` 节点后，驱动虽然绑定但不创建 remoteproc 设备。

**根因**: 
1. 当前内核 dtb 无 `__symbols__` 节点，overlay 编译时带 `-@` 会产生 `__symbols__`，内核检查不一致时拒绝应用 overlay
2. 即使 overlay 成功应用，`reserved-memory` 节点只在启动时被内核处理，overlay 添加的不会生效

**解决**: 不走 overlay 路线，直接修改启动用的 FIT image 中的 dtb。

### 问题 2: 平铺 DT 结构 vs 嵌套 DT 结构

**现象**: 用平铺结构 (kernel 5.10 风格) 修改 dtb 后，remoteproc 设备不创建。

```
# 平铺结构 (kernel 5.10)
homo_rproc: homo_rproc@0 {
    compatible = "homo,rproc";
    remote-processor = <3>;
    inter-processor-interrupt = <9>;
    memory-region = <&rproc>;
    firmware-name = "openamp_core0.elf";
    status = "okay";
};
```

**根因**: 内核 6.6 的 `homo_remoteproc.c` 包含 `homo_core_of_init` 函数，需要子节点 `compatible = "homo,rproc-core"`。

**解决**: 使用嵌套结构:

```
# 嵌套结构 (kernel 6.6)
homo_rproc: homo_rproc@0 {
    compatible = "homo,rproc";
    status = "okay";
    homo_core0: homo_core0@b0100000 {
        compatible = "homo,rproc-core";
        remote-processor = <3>;
        inter-processor-interrupt = <9>;
        memory-region = <&rproc>;
        firmware-name = "openamp_core0.elf";
    };
};
```

### 问题 3: FIT Image 更新

**过程**:
1. 从 `/dev/mmcblk0` 偏移 4MB 处读取当前 fitImage
2. 用 `dumpimage` 拆出内核和 dtb
3. `dtc` 解编译 dtb → 添加 OpenAMP 节点 → 重新编译
4. 用 `mkimage` + `.its` 文件重打包 fitImage
5. 用 `runtime_replace_bootloader.sh fitImage` 写回 mmc

**关键命令**:
```bash
# 提取
dumpimage -T flat_dt -p 0 -o kernel.gz fitImage
dumpimage -T flat_dt -p 1 -o board.dtb fitImage

# 修改 dtb
dtc -I dtb -O dts board.dtb > board.dts
# 编辑 dts 添加 OpenAMP 节点
dtc -I dts -O dtb board.dts > new.dtb

# 打包
mkimage -f new_fit.its new_fitImage

# 刷入
runtime_replace_bootloader.sh fitImage
```

### 问题 4: 裸机固件编译

**需要**: `aarch64-none-elf-gcc` (bare-metal, 非 Linux 版)

**下载**: ARM GNU Toolchain 13.3.Rel1 `aarch64-none-elf`

**编译**:
```bash
export AARCH64_CROSS_PATH="/path/to/toolchain"
cd phytium-standalone-sdk-master/example/system/amp/openamp_for_linux
make config_pe2204_phytiumpi_aarch64
make clean
make all
# 输出: pe2204_aarch64_phytiumpi_openamp_core0.elf
```

### 问题 5: /dev/rpmsg0 权限

**现象**: rpmsg-demo-single 报 "Permission denied" 打开 /dev/rpmsg_ctrl0

**解决**: `sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0`

### 验证成功日志

```
[  361.124486] remoteproc remoteproc0: powering up homo_core0
[  361.125318] remoteproc remoteproc0: Booting fw image openamp_core0.elf, size 1249064
[  361.190610] psci: CPU3 killed (polled 0 ms)
[  361.515587] virtio_rpmsg_bus virtio0: rpmsg host is online
[  361.515619] remoteproc remoteproc0: remote processor homo_core0 is now up
[  361.632578] virtio_rpmsg_bus virtio0: creating channel rpmsg-openamp-demo-channel addr 0x0
```

### 成功运行的 demo 输出

```
received message: Hello World! No:1
received message: Hello World! No:2
...
received message: Hello World! No:100
```

## 2026-05-11 (续): FreeRTOS 切换

### 问题 6: FreeRTOS SDK 编译 — symlink 路径解析

**现象**: `$(SDK_DIR)/../freertos.kconfig` 在 symlink 下 `..` 穿越到错误目录

**根因**: FreeRTOS SDK 依赖 standalone SDK（需放在 `freertos-sdk/standalone/`）。
使用 symlink 时，`standalone/../` 的 `..` 解析到物理路径（standalone SDK 根目录）而非逻辑路径（FreeRTOS SDK 根目录）。

**解决**: 不使用 symlink，直接 `cp -r` 复制 standalone SDK 到 `freertos-sdk/standalone/`。

**修复的文件**:
1. `Kconfig`: `source "$(SDK_DIR)/../freertos.kconfig"` → `source "$(FREERTOS_SDK_DIR)/freertos.kconfig"`
2. `makefile`: `FREERTOS_SDK_DIR =` → `FREERTOS_SDK_DIR := $(abspath ...)` 并 `export`
3. `tools/freertos_comonents.mk`: `FREERTOS_SDK_DIR := $(SDK_DIR)/..` → `$(abspath $(SDK_DIR)/..)`

### 问题 7: FreeRTOS 传感器数据只发1包

**现象**: `rpmsg_send` 第2次调用失败，只收到1包

**根因**: `platform_poll()` 需要 remoteproc 结构体（`&remoteproc_device_00`），但 `send_all_sensor_packets` 从回调中获得的 `priv` 不是正确的 remoteproc 指针。

**解决**: 
1. 添加全局变量 `g_remoteproc_priv` 在 `FRpmsgEchoApp` 中保存
2. `send_all_sensor_packets` 使用 `g_remoteproc_priv` 调用 `platform_poll()`

### FreeRTOS 传感器通信验证成功

```
[SEND] Requested sensor data from slave
[RECV] Waiting for 10 sensor packets...
  [PKT  1] ID= 1 ts=    0 V=220.50V A=1.25A T=27.3C [NORMAL]
  ...
  [PKT 10] ID=10 ts=  900 V=221.20V A=1.32A T=35.2C [NORMAL]
  >> [COMPLETED] Batch 1: Received 10/10 sensor packets
```
15批次 × 10包 = 150包稳定连续收发。

## 2026-05-11 (续2): A1+C2 优化实施

### A1: 批量消息合并 — 效果验证

| 指标 | 优化前(逐个) | 优化后(批量) | 提升 |
|------|-------------|-------------|------|
| 批次延迟 | 19.79ms | 0.03ms | 659× |
| 单包延迟 | 1979μs | 3.1μs | 638× |
| rpmsg_send/批 | 10次 | 1次 | 10× |
| SGI9中断/批 | 20次 | 2次 | 10× |

**实现**: FreeRTOS 将 10 个 SensorPacket 打包为一个 `DEVICE_SENSOR_BATCH` 消息，1 次 `rpmsg_send` 完成。Linux 侧一次 `read()` 解析全部 10 包。

### C2: 边缘异常检测 — 效果验证

- 阈值：电压 210-230V, 电流 0.5-2.5A, 温度 35°C(WARN)/50°C(ERROR)
- 运行 70 包中检测到 21 次异常，49 次正常
- FreeRTOS 侧完成预判，Linux 侧直接使用 status 字段

### 面板 v4 新增指标

- `optimize_speedup`: 优化加速比 (vs 逐个发送基准)
- `edge_alarms` / `edge_normal`: 边缘检测告警数/正常数
