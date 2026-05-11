# OpenAMP 异构多核通信部署指南

## 前置条件

- 飞腾派 CEK8903 开发板 (IP: 192.168.88.11)
- 开发机: Ubuntu/Debian (本机)
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

### 1.2 修改 dtb 添加 OpenAMP 节点

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
```

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

## 步骤 2: 编译裸机固件

### 2.1 获取工具链

下载 ARM bare-metal 工具链: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
选择: `AArch64 bare-metal target (aarch64-none-elf)`

### 2.2 编译

```bash
export AARCH64_CROSS_PATH="/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
cd /home/alientek/Phytium_syscode/phytium-standalone-sdk-master/phytium-standalone-sdk-master/example/system/amp/openamp_for_linux
make config_pe2204_phytiumpi_aarch64
make clean && make all
```

输出: `pe2204_aarch64_phytiumpi_openamp_core0.elf`

## 步骤 3: 部署固件

```bash
scp pe2204_aarch64_phytiumpi_openamp_core0.elf user@192.168.88.11:/tmp/openamp_core0.elf
ssh user@192.168.88.11 "sudo cp /tmp/openamp_core0.elf /lib/firmware/ && sudo chmod 644 /lib/firmware/openamp_core0.elf"
```

## 步骤 4: 启动 OpenAMP

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

## 步骤 5: 运行通信测试

```bash
# 编译 demo (使用 Linux 交叉编译器)
aarch64-none-linux-gnu-gcc -Wall -O2 -o rpmsg-demo-single rpmsg-demo-single.c

# 部署
scp rpmsg-demo-single user@192.168.88.11:~/

# 绑定通道
ssh user@192.168.88.11 "
sudo sh -c 'echo rpmsg_chrdev > /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override'
sudo sh -c 'echo virtio0.rpmsg-openamp-demo-channel.-1.0 > /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind'
sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0
"

# 运行测试
ssh user@192.168.88.11 "./rpmsg-demo-single"
```

### 预期输出

```
received message: Hello World! No:1
received message: Hello World! No:2
...
received message: Hello World! No:100
```

## 步骤 6: 停止

```bash
ssh user@192.168.88.11 "echo stop | sudo tee /sys/class/remoteproc/remoteproc0/state"
# 状态变为: offline
```
