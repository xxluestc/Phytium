# OpenAMP 异构多核通信知识库

## 1. 硬件架构

### SoC: Phytium PE2204

| CPU | 核心类型 | Device Tree | 逻辑 CPU | MPIDR | 用途 |
|-----|---------|-------------|---------|-------|------|
| cpu@0 | FTC664 (big) | cpu_b0 | cpu0 | 0x000 | Linux SMP |
| cpu@1 | FTC664 (big) | cpu_b1 | cpu1 | 0x100 | Linux SMP |
| cpu@100 | FTC310 (LITTLE) | cpu_l0 | cpu2 | 0x200 | Linux SMP |
| cpu@101 | FTC310 (LITTLE) | cpu_l1 | cpu3 | 0x201 | **OpenAMP 从核** |

### 内存布局

| 地址范围 | 大小 | 用途 |
|----------|------|------|
| 0x80000000 - 0x80010000 | 64KB | 启动保留 (/memreserve/) |
| 0x80010000 - 0xB0100000 | ~768MB | Linux 可用 |
| **0xB0100000 - 0xC9A00000** | **409MB** | **OpenAMP 共享内存 (reserved-memory, no-map)** |
| 0xC9A00000 - 0x??? | ~3GB | Linux 可用 |

## 2. 设备树配置 (Kernel 6.6)

### 嵌套结构（正确）

```dts
/memreserve/ 0x80000000 0x10000;

/ {
    reserved-memory {
        #address-cells = <2>;
        #size-cells = <2>;
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
            remote-processor = <3>;        // CPU 3 (cpu_l1, FTC310)
            inter-processor-interrupt = <9>; // SGI 9 (8-15 可用, 0-7 内核保留)
            memory-region = <&rproc>;       // 指向共享内存
            firmware-name = "openamp_core0.elf";
        };
    };
};
```

### 关键 DT 属性说明

| 属性 | 值 | 说明 |
|------|-----|------|
| `remote-processor` | `<3>` | Linux 逻辑 CPU 号 |
| `inter-processor-interrupt` | `<9>` | GICv3 SGI 中断号 (8-15 范围，0-7 被内核 SMP 保留) |
| `memory-region` | `<&rproc>` | phandle 指向 reserved-memory 节点 |
| `firmware-name` | `"openamp_core0.elf"` | 必须在 /lib/firmware/ 下 |

## 3. 内核配置

```makefile
CONFIG_REMOTEPROC=y
CONFIG_REMOTEPROC_CDEV=y
CONFIG_HOMO_REMOTEPROC=y       # Phytium 定制驱动 (内建)
CONFIG_RPMSG=y
CONFIG_RPMSG_CHAR=m            # /dev/rpmsg0 字符设备 (模块)
CONFIG_RPMSG_CTRL=m            # /dev/rpmsg_ctrl0 控制设备 (模块)
CONFIG_RPMSG_VIRTIO=y          # VirtIO RPMsg 传输
CONFIG_PHYTIUM_MBOX=y          # Phytium 邮箱驱动
```

## 4. 驱动架构

### homo_remoteproc 驱动流程

```
homo_rproc_probe()
  ├── rproc_of_parse_firmware()    → 获取 firmware-name
  ├── rproc_alloc()               → 分配 remoteproc 设备
  ├── of_property_read_u32()       → 解析 remote-processor
  ├── of_property_read_u32()       → 解析 inter-processor-interrupt
  ├── of_parse_phandle()           → 解析 memory-region
  ├── ioremap(PAGE_KERNEL_EXEC)    → 映射共享内存为可执行
  ├── request_percpu_irq()         → 注册 SGI IPI 中断
  ├── cpuhp_setup_state()          → CPU hotplug 回调
  ├── homo_core_of_init()          → 解析子节点 "homo,rproc-core"
  └── rproc_add()                  → 注册 remoteproc 设备 → /sys/class/remoteproc/remoteproc0

homo_rproc_start() (echo start > state)
  ├── remove_cpu(3)                → 下电 CPU3
  ├── arm_smccc_smc(CPU_ON, ...)   → PSCI 启动 CPU3，入口 0xB0100000
  └── rproc_virtio → virtio_rpmsg_bus → 创建 RPMsg 通道

homo_rproc_kick() (发送 IPI)
  └── gic_write_sgi1r(ipi=9, target=CPU3_MPIDR)  → GICv3 直接寄存器写入
```

## 5. 裸机固件编译

### 工具链

- **必须**: `aarch64-none-elf-gcc` (ARM bare-metal, 非 Linux 版本)
- **不建议**: `aarch64-none-linux-gnu-gcc` (仅用于 Linux 应用)

### SDK: phytium-standalone-sdk

```bash
# 环境变量
export AARCH64_CROSS_PATH="/path/to/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf"
export PHYTIUM_DEV_PATH="/path/to/phytium-standalone-sdk-master"

# 配置和编译
cd example/system/amp/openamp_for_linux/
make config_pe2204_phytiumpi_aarch64   # 飞腾派 aarch64
make clean && make all
# 输出: pe2204_aarch64_phytiumpi_openamp_core0.elf
```

### 固件配置要点

- 加载地址: `0xB0100000` (CONFIG_IMAGE_LOAD_ADDRESS)
- 中断角色: slave (CONFIG_INTERRUPT_ROLE_SLAVE=y)
- 使用 IPI 中断: CONFIG_USE_OPENAMP_IPI=y
- 启用 Cache 一致性: CONFIG_USE_CACHE_COHERENCY=y

## 6. FIT Image 修改流程

### mmc 布局

| 偏移 | 大小 | 内容 |
|------|------|------|
| 0 - 4MB | 4MB | fip-all.bin (U-Boot + ATF) |
| 4MB - 64MB | 60MB | fitImage (kernel.gz + dtb) |
| 64MB+ (sector 131072) | 29.2GB | ext4 rootfs |

### 更新 fitImage

```bash
# 方法1: 工具脚本 (推荐)
sudo runtime_replace_bootloader.sh fitImage  # 需要 fitImage 在当前目录

# 方法2: 手动 dd
sudo dd if=new_fitImage of=/dev/mmcblk0 bs=1M seek=4 count=60
```

### 打包新 fitImage

```bash
# 1. 创建 .its 文件
cat > new_fit.its << EOF
/dts-v1/;
/ {
    description = "U-Boot fitImage for Phytium Phytiumpi";
    #address-cells = <1>;
    images {
        kernel { ... };
        fdt-phytium { ... };
    };
    configurations {
        default = "phytium";
        phytium { kernel = "kernel"; fdt = "fdt-phytium"; };
    };
};
EOF

# 2. 用 mkimage 打包
mkimage -f new_fit.its new_fitImage
```

## 7. RPMsg 通信操作

### 启动序列

```bash
modprobe rpmsg_char rpmsg_ctrl                          # 加载模块
echo start > /sys/class/remoteproc/remoteproc0/state    # 启动从核
echo rpmsg_chrdev > .../driver_override                  # 绑定驱动
echo virtio0.rpmsg-openamp-demo-channel.-1.0 > .../bind # 创建设备
```

### 验证检查

```bash
cat /sys/class/remoteproc/remoteproc0/state  # running/offline
ls /sys/bus/rpmsg/devices/                   # 应看到通道设备
ls /dev/rpmsg*                               # rpmsg0, rpmsg_ctrl0
dmesg | grep -i rproc                        # 启动日志
```

### 停止序列

```bash
echo stop > /sys/class/remoteproc/remoteproc0/state
modprobe -r rpmsg_char rpmsg_ctrl
```

## 8. 关键路径和文件

| 文件 | 位置 |
|------|------|
| 内核源码 (5.10) | `/home/alientek/Phytium_syscode/内核源码/` |
| 裸机 SDK | `/home/alientek/Phytium_syscode/phytium-standalone-sdk-master/` |
| 裸机编译器 | `/home/alientek/Phytium_syscode/GCC编译器/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf/` |
| Linux 编译器 | `/home/alientek/Phytium_syscode/GCC编译器/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/` |
| 手册和文档 | `/home/alientek/phytium-embedded-docs-master/` |
| PIOS 镜像 | `/home/alientek/Phytium_syscode/PIOS2.2镜像/` |

## 9. 常见问题速查

| 现象 | 原因 | 解决 |
|------|------|------|
| `/sys/class/remoteproc/` 为空 | 设备树无 OpenAMP 节点 | 确认 dtb 包含嵌套结构 |
| `OF: reserved mem:` 未打印 | 未用启动 dtb 直接修改 | 不要用 overlay，直接修改 dtb |
| `cpuhp setup state failed -16` | cpu hotplug 状态残留 | 重启开发板 |
| probe 成功但无设备 | 用了平铺结构 | 改用嵌套 `homo,rproc-core` 子节点 |
| `Permission denied` 打开 rpmsg | 设备权限 | `chmod 666 /dev/rpmsg*` |
| 固件加载后崩溃 | 固件编译架构不匹配 | 确认 `aarch64-none-elf` 编译 |
