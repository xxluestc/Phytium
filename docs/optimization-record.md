# OpenAMP 异构多核通信优化记录

## 优化总览

| 阶段 | 编号 | 优化项 | 状态 | 效果 |
|------|------|--------|------|------|
| **A: 通信性能** | A1 | 批量消息合并 | **完成** | 延迟 19.79ms→0.03ms (659×) |
| | A2 | 零拷贝传输 | 待做 | 预计吞吐量提升 3-5× |
| | A3 | 中断合并 IRQ Coalescing | 待做 | 预计 CPU 开销降低 50% |
| | A4 | Vring 大小调优 | 待做 | 匹配批量传输需求 |
| **B: 生命周期管理** | B1 | 快速启动 | 待做 | 减少 rproc_start 延迟 |
| | B2 | 热重启 | 待做 | 从核崩溃自动恢复 |
| | B3 | 动态负载切换 | 待做 | bare-metal/FreeRTOS 切换 |
| **C: 应用场景** | C1 | 多节点并发模拟 | 待做 | 20+ 节点并发 |
| | C2 | 边缘异常检测 | **完成** | FreeRTOS 阈值预判过滤 |
| | C3 | 可视化面板 | **完成** | Web 实时监控面板 |

---

## A1: 批量消息合并

### 原理

```
优化前 (逐个发送): 
  Linux请求 → FreeRTOS send1→poll→send2→poll→...→send10→poll
  10次 rpmsg_send + 20次 SGI9中断 + 10次 vring往返

优化后 (批量合并):
  Linux请求 → FreeRTOS打包10包为1个消息 → 1次 rpmsg_send
  1次 rpmsg_send + 2次 SGI9中断 + 1次 vring往返
```

### 实现

FreeRTOS侧 (`rpmsg-echo_os.c`):
```c
// 批量合并: 10个SensorPacket一次性打包发送
tx_data.command = DEVICE_SENSOR_BATCH;
tx_data.length = sizeof(SensorPacket) * SENSOR_PACKET_COUNT;
memcpy(tx_data.data, sensor_packets, sizeof(SensorPacket) * SENSOR_PACKET_COUNT);
rpmsg_send(ept, &tx_data, 6 + tx_data.length);  // 1次发送
```

Linux侧 (`dashboard_server.c`):
```c
if (pkt->command == DEVICE_SENSOR_BATCH) {
    int pkt_count = pkt->length / sizeof(SensorPacket);
    memcpy(g_last_sensors, pkt->data, pkt_count * sizeof(SensorPacket)); // 1次接收
}
```

### 效果对比

| 指标 | 优化前 | 优化后 | 提升 |
|------|--------|--------|------|
| **批次往返延迟** | 19.79ms | **0.03ms** | **659× ↓** |
| **单包延迟** | 1979μs | **3.1μs** | **638× ↓** |
| **每批 rpmsg_send 次数** | 10次 | **1次** | **10× ↓** |
| **每批 SGI9 中断次数** | 20次 | **2次** | **10× ↓** |
| **vring 往返次数** | 10次 | **1次** | **10× ↓** |
| **共享内存占用** | 1.7MB | 1.7MB | 不变 |
| **有效带宽** | 154 B/s | 150 B/s | 不变 |

---

## C2: 边缘异常检测

### 原理

FreeRTOS 从核在发送数据前，先对传感器数据做阈值判断。异常数据标记状态后随批量消息上报 Linux，正常数据累计统计。体现"边缘计算"理念：从核做预判，减少主核无效处理。

### 实现

FreeRTOS侧 (`rpmsg-echo_os.c`):
```c
#define THR_VOLTAGE_MIN  210.0f  // 电压下限(WARN)
#define THR_VOLTAGE_MAX  230.0f  // 电压上限(WARN)
#define THR_CURRENT_MIN  0.5f    // 电流下限(WARN)
#define THR_CURRENT_MAX  2.5f    // 电流上限(WARN)
#define THR_TEMP_WARN    35.0f   // 温度预警
#define THR_TEMP_ERROR   50.0f   // 温度异常

int edge_detect_anomaly(SensorPacket *pkts, int count) {
    // 逐包评估电压/电流/温度，更新status字段
    // 累计 g_edge_alarm_count, g_edge_normal_count
}
```

### 效果

| 指标 | 优化前 | 优化后 |
|------|--------|--------|
| 异常检测位置 | 无 | FreeRTOS 从核 |
| 检测能力 | 无 | 电压/电流/温度三维度阈值 |
| 边缘告警数 | 0 | **21次** (70包中) |
| 正常过滤数 | 0 | **49次** (70包中) |
| Linux 侧负担 | 需逐包判断 | 直接使用 status 字段 |

---

## C3: Web 实时监控面板

### 原理

嵌入式 HTTP 服务器 (C语言, 纯 socket) + HTML/JS 前端。双线程架构：HTTP 线程服务 Web 页面和 JSON API，RPMsg 线程与 FreeRTOS 通信采集数据。浏览器每 1 秒轮询 `/stats` 接口刷新显示。

### 效果

- 实时展示异构架构、统计数据、传感器数据表
- 传输日志滚动显示
- 优化加速比可视化
- CSV 历史数据记录

---

## 性能基准数据

### 测试环境

| 项目 | 值 |
|------|-----|
| 硬件 | 飞腾派 CEK8903 (PE2204) |
| 主核 | Linux 6.6.63 (CPU0-2) |
| 从核 | FreeRTOS (CPU3, FTC664) |
| 通信 | RPMsg + VirtIO, SGI 9, 共享内存 409MB |
| 数据包 | 10 个 SensorPacket (30B/包) 每批 |
| 批次间隔 | 2 秒 |

### 通信性能基线 (优化前 bare-metal 逐个发送)

| 指标 | 值 |
|------|-----|
| 批次往返延迟 | ~19.79ms |
| 单包延迟 | ~1979μs |
| 吞吐率 | ~5.1 pkt/s |
| 有效带宽 | ~154 B/s |

### 通信性能 (A1 优化后 FreeRTOS 批量发送)

| 指标 | 值 |
|------|-----|
| 批次往返延迟 | **~0.03ms** |
| 单包延迟 | **~3.1μs** |
| 吞吐率 | ~5.0 pkt/s (受 2s 批次间隔限制) |
| 有效带宽 | ~150 B/s |
| 优化加速比 | **1298× ~ 3190×** |

---

## 停止通信程序

### 完整停止流程 (已验证)

```bash
# 步骤1: 停止面板服务器
killall -9 dashboard_server

# 步骤2: 停止从核 (FreeRTOS)
echo stop | sudo tee /sys/class/remoteproc/remoteproc0/state
# 确认: cat /sys/class/remoteproc/remoteproc0/state → offline

# 步骤3: 卸载内核模块 (注意顺序: 先 ctrl 后 char)
sudo rmmod rpmsg_ctrl
sudo rmmod rpmsg_char

# 步骤4: 验证清理完成
ls /dev/rpmsg*          # 应无输出
ls /sys/bus/rpmsg/devices/  # 应无输出或为空
```

### 快速重启

```bash
# 1. 启动从核
echo start | sudo tee /sys/class/remoteproc/remoteproc0/state

# 2. 加载模块
sudo modprobe rpmsg_char rpmsg_ctrl

# 3. 绑定通道
echo rpmsg_chrdev | sudo tee /sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override
echo virtio0.rpmsg-openamp-demo-channel.-1.0 | sudo tee /sys/bus/rpmsg/drivers/rpmsg_chrdev/bind
sudo chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0

# 4. 启动面板
nohup ~/dashboard_server > /tmp/dashboard.log 2>&1 &
```
