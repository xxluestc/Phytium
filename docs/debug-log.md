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
