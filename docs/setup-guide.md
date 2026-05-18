# OpenAMP 异构多核通信部署指南

> **更新**: 2026-05-18 | **当前架构**: Linux主核 + FreeRTOS从核 (GD32主控移植版)

## 前置条件

- 飞腾派 CEK8903 开发板 (IP: 192.168.88.11)
- 开发机: Ubuntu/Debian (本机: /home/alientek)
- 工具: `dtc`, `mkimage`, `dumpimage`, `ssh`, `scp`

## 步骤 1: 修改设备树

### 1.1 获取当前 fitImage

```bash
# 从开发板提取
ssh user@192.168.88.11 "sudo dd if=/dev/mmcblk0 bs=1M skip=4 count=60" > current_fitImage

# 拆出内核和 dtb
dumpimage -T flat_dt -p 0 -o kernel.gz current_fitImage
dumpimage -T flat_dt -p 1 -o board.dtb current_fitImage
```

### 1.2 修改 dtb 添加 OpenAMP 和 LoRa UART 节点

```bash
dtc -I dtb -O dts board.dtb > board.dts
```

在 `board.dts` 的根节点 `/` 内，`memory@0` 之前添加:

```dts
reserved-memory {
    #address-cells = <0x02>;
    #size-cells = <0x02>;
    ranges;
    rproc: rproc@b0100000 {
        no-map;
        reg = <0x0 0xb0100000 0x0 0x19900000>;
    };
};

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

&uart3 {
    status = "okay";
    pinctrl-names = "default";
    pinctrl-0 = <&uart3_pins>;
};

&pio2 {
    uart3_pins: uart3-pins {
        pins = "GPIO2_8", "GPIO2_9", "GPIO2_10", "GPIO2_11";
        function = "uart3";
        drive-strength = <8>;
        bias-pull-up;
    };
};
```

**说明**:
- `uart3` + `GPIO2_10` 用于 LoRa 模块 (ATK-MWCC68D) 串口通信
- 预留 `0xB0100000` (409MB) 给 OpenAMP 共享内存

### 1.3 重新编译和打包

```bash
dtc -I dts -O dtb board.dts > new_board.dtb

# 创建 ITS 文件并打包
mkimage -f new_fit.its new_fitImage
```

### 1.4 刷入开发板

```bash
scp new_fitImage user@192.168.88.11:~/fitImage
ssh user@192.168.88.11 "sudo runtime_replace_bootloader.sh fitImage && sudo reboot"
```

## 步骤 2: 编译 FreeRTOS 固件 (GD32主控移植版)

### 2.1 工具链准备

- **必须**: `aarch64-none-elf-gcc` (ARM bare-metal, 非 Linux 版本)
- **位置**: `/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/`

### 2.2 编译流程

本项目的 FreeRTOS 代码位于 [/home/alientek/Phytium/freertos/](file:///home/alientek/Phytium/freertos/)，依赖飞腾官方 SDK。

```bash
# 环境变量
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"

# 进入飞腾 FreeRTOS SDK
cd /home/alientek/Phytium_syscode/phytium-free-rtos-sdk-master/example/system/amp/openamp_for_linux

# 配置 + 编译
make config_pe2204_phytiumpi_aarch64
make clean && make all
```

**输出**: `pe2204_aarch64_phytiumpi_openamp_for_linux.elf`

> **备选**: 如需编译 Bare-metal 版本，参考 [knowledge-base.md](knowledge-base.md)。

## 步骤 3: 部署固件

```bash
scp pe2204_aarch64_phytiumpi_openamp_for_linux.elf user@192.168.88.11:/tmp/openamp_core0.elf
ssh user@192.168.88.11 "sudo cp /tmp/openamp_core0.elf /lib/firmware/ && sudo chmod 644 /lib/firmware/openamp_core0.elf"
```

## 步骤 4: 编译 Linux 侧应用

```bash
# 交叉编译 master_receiver (使用 Linux 编译器)
export CROSS_COMPILE="/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
cd /home/alientek/Phytium/src/openamp-demo/linux-master
make clean && make

# 部署
scp master_receiver user@192.168.88.11:~/
```

## 步骤 5: 启动 OpenAMP

```bash
# 加载模块
ssh user@192.168.88.11 "sudo modprobe rpmsg_char rpmsg_ctrl"

# 启动远程处理器
ssh user@192.168.88.11 "echo start | sudo tee /sys/class/remoteproc/remoteproc0/state"

# 验证状态
ssh user@192.168.88.11 "cat /sys/class/remoteproc/remoteproc0/state"
# 应输出: running

# 验证 RPMsg 通道
ssh user@192.168.88.11 "ls /sys/bus/rpmsg/devices/"
# 应看到: virtio0.rpmsg-openamp-demo-channel.-1.0
```

## 步骤 6: 绑定通道并运行

```bash
# 绑定驱动
ssh user@192.168.88.11 "
sudo sh -c 'echo rpmsg_chrdev > /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override'
sudo sh -c 'echo virtio0.rpmsg-openamp-demo-channel.-1.0 > /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind'
sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0
"

# 运行 master_receiver 接收命令
ssh user@192.168.88.11 "./master_receiver"
```

### 预期输出

```
Opening rpmsg channel...
Waiting for messages...
[READY] master_receiver running, press Ctrl+C to exit.
```

此时，如果 FreeRTOS 侧有命令生成，会在这里打印出来：

```
[CMD] node=0 cmd=REQ_WAVE(0x10)
[CMD] node=2 cmd=REQ_FAULT_LIST(0x11)
```

## 完整启动脚本 (一次执行)

```bash
# 一键启动
sudo modprobe rpmsg_char rpmsg_ctrl
echo start | sudo tee /sys/class/remoteproc/remoteproc0/state
sleep 0.5
echo rpmsg_chrdev | sudo tee /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override
echo virtio0.rpmsg-openamp-demo-channel.-1.0 | sudo tee /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind
sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0
./master_receiver
```

## 步骤 7: 停止

```bash
# Ctrl+C 退出 master_receiver
echo stop | sudo tee /sys/class/remoteproc/remoteproc0/state
# 状态变为: offline
```

## 完整停止流程

```bash
# 停止 master_receiver (Ctrl+C)
# 停止从核
echo stop | sudo tee /sys/class/remoteproc/remoteproc0/state
# 确认: cat /sys/class/remoteproc/remoteproc0/state → offline

# 卸载内核模块 (顺序: 先 ctrl 后 char)
sudo rmmod rpmsg_ctrl
sudo rmmod rpmsg_char

# 验证清理完成
ls /dev/rpmsg*          # 应无输出
ls /sys/bus/rpmsg/devices/  # 应无输出或为空
```

## 快速重启流程

```bash
# 1. 启动从核
echo start | sudo tee /sys/class/remoteproc/remoteproc0/state

# 2. 加载模块 (如已卸载)
sudo modprobe rpmsg_char rpmsg_ctrl

# 3. 绑定通道 (如已解绑)
echo rpmsg_chrdev | sudo tee /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override
echo virtio0.rpmsg-openamp-demo-channel.-1.0 | sudo tee /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind
sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0

# 4. 启动接收
./master_receiver
```

## 故障排查

常见问题及解决方法参见 [debug-log.md](debug-log.md)。
