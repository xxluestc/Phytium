#ifndef __DATA_MONITOR_H
#define __DATA_MONITOR_H

#include <stdint.h>
#include "data_frame.h"

/*============================================================================
 *  GD32L233C: SRAM 32KB, Flash 256KB   LoRa: 19.2kbps
 *
 *  10节点: 2kHz连续采样, 50Hz每周期40点, 20周期环形缓冲(800点=6.4KB)
 *  滑动检测: 10ms窗口(20点), 每个采样点都滑动窗口并判断
 *  故障上传: 前后10周期(400点) → 立即切换模式 → 录波
 *  正常上传: 每5s上传10周期(400点)
 *  波形: 预警 6kHz×250ms, 紧急 12kHz×400ms, TIM5 ISR→DMA乒乓→Flash
 *  波形上传: 按指令按需上传 (node_index + fault_index)
 *
 *  RAM预算: 节点12.8KB + DMA双缓冲0.3KB + 滑动窗口0.2KB + 故障记录0.5KB ≈ 13.8KB
 *============================================================================*/

#define NODE_SAMPLE_RATE         2000       /* 节点采样率 2kHz */
#define SAMPLES_PER_CYCLE        40         /* 50Hz每周期40点 */
#define NODE_BUFFER_CYCLES       20         /* 环缓冲20周期 (800点=6.4KB) */
#define NODE_BUFFER_SIZE         (SAMPLES_PER_CYCLE * NODE_BUFFER_CYCLES)

#define MID_FREQ_WINDOW_MS       10         /* 滑动检测窗口 (每采样点滑窗一次) */
#define MID_FREQ_WINDOW_SAMPLES  (NODE_SAMPLE_RATE * MID_FREQ_WINDOW_MS / 1000)

#define FAULT_UPLOAD_CYCLES      10         /* 故障上传前后10周期 (400点) */
#define FAULT_UPLOAD_POINTS      (SAMPLES_PER_CYCLE * FAULT_UPLOAD_CYCLES)

#define WAVE_SAMPLE_RATE_WARN    6000       /* 预警波形6kHz */
#define WAVE_SAMPLE_RATE_DANGER  12000      /* 紧急波形12kHz */
#define WARNING_WAVE_MS          250        /* 预警录制时长 */
#define DANGER_WAVE_MS           400        /* 紧急录制时长 */
#define WARNING_WAVE_SAMPLES     (WAVE_SAMPLE_RATE_WARN * WARNING_WAVE_MS / 1000)
#define DANGER_WAVE_SAMPLES      (WAVE_SAMPLE_RATE_DANGER * DANGER_WAVE_MS / 1000)

#define DMA_PINGPONG_SIZE        64         /* DMA双缓冲大小 (样本数, 2×64×2B=256B) */

#define WAVE_FLASH_SIZE          (128 * 1024)     /* Flash波形存储区 */
#define WAVE_FLASH_ADDR          0x08020000
#define WAVE_FLASH_PAGE_SIZE     4096

#define MAX_FAULT_RECORDS        8          /* 最大故障记录数 */

#define VOLTAGE_NOMINAL          220
#define VOLTAGE_OVER_LIMIT       (VOLTAGE_NOMINAL * 1.05f)   /* 过压阈值 */
#define VOLTAGE_UNDER_LIMIT      (VOLTAGE_NOMINAL * 0.95f)   /* 欠压阈值 */
#define VOLTAGE_SAG_THRESHOLD    (VOLTAGE_NOMINAL * 0.90f)   /* 暂降阈值 */
#define VOLTAGE_SWELL_THRESHOLD  (VOLTAGE_NOMINAL * 1.10f)   /* 暂升阈值 */

/*============================================================================
 *  故障类型枚举: 覆盖电压暂升/暂降/过压/欠压
 *============================================================================*/
typedef enum {
    FAULT_NONE = 0,           /* 无故障 */
    FAULT_OVER_VOLTAGE,       /* 过压: Vrms > 1.10pu */
    FAULT_UNDER_VOLTAGE,      /* 欠压: Vrms < 0.90pu */
    FAULT_VOLTAGE_SAG,        /* 电压暂降: Vrms < 0.80pu */
    FAULT_VOLTAGE_SWELL,      /* 电压暂升: Vrms > 1.15pu */
    FAULT_TRANSIENT           /* 暂态突变 (预留) */
} FaultType_t;

/*============================================================================
 *  故障级别: 正常 / 预警 / 紧急
 *============================================================================*/
typedef enum {
    SEVERITY_NORMAL = 0,      /* 正常模式 */
    SEVERITY_WARNING,         /* 预警模式 (6kHz录波) */
    SEVERITY_DANGER           /* 紧急模式 (12kHz录波, 切备用电源) */
} SeverityLevel_t;

/*============================================================================
 *  DMA乒乓缓冲状态
 *============================================================================*/
typedef enum {
    DMA_BUF_0 = 0,            /* 前半缓冲 */
    DMA_BUF_1 = 1             /* 后半缓冲 */
} DmaBufIndex_t;

/*============================================================================
 *  NodeSample_t: 节点采样数据 (2kHz环缓冲, int32×10000保留四位小数)
 *
 *  active_power:   有功功率 ×10000, 例 123.4567W → 1234567
 *  reactive_power: 无功功率 ×10000
 *  voltage_angle:  电压相角 ×10000, 例 30.5678° → 305678
 *  current:        电流 ×10000,     例 5.5678A → 55678
 *============================================================================*/
typedef struct {
    int32_t active_power;     /* ×10000 */
    int32_t reactive_power;   /* ×10000 */
    int32_t voltage_angle;    /* ×10000 */
    int32_t current;          /* ×10000 */
} NodeSample_t;

typedef struct {
    NodeSample_t buffer[NODE_BUFFER_SIZE];   /* 800点环缓冲 */
    uint16_t write_index;     /* 当前写入位置 */
    uint16_t cycle_count;     /* 累计采样点计数 */
} NodeBuffer_t;

/*============================================================================
 *  BusData_t: 母线实时数据 (由 data_judge_task 周期更新)
 *============================================================================*/
typedef struct {
    float voltage_mag;        /* 母线电压幅值 (V) */
    float voltage_angle;      /* 母线电压相角 (°) */
    float frequency;          /* 电网频率 (Hz) */
    float battery_voltage;    /* 电池电压 (V) */
} BusData_t;

/*============================================================================
 *  MidFreqWindow_t: 中频故障检测滑动窗口 (10ms, 20点)
 *  每次写入一个新点, 窗口满时计算 RMS/dv_dt/Peak
 *============================================================================*/
typedef struct {
    float voltage[MID_FREQ_WINDOW_SAMPLES];   /* 电压滑动窗口 */
    float current[MID_FREQ_WINDOW_SAMPLES];   /* 电流滑动窗口 (预留) */
    uint16_t index;           /* 窗口写入索引 (0~19) */
    float rms;                /* 窗口RMS (最近一次计算) */
    float dv_dt;              /* 窗口斜率 (最近一次计算) */
    float peak;               /* 窗口峰峰值 (最近一次计算) */
    float thd;                /* THD (预留) */
} MidFreqWindow_t;

/*============================================================================
 *  WaveBuffer_t: 波形DMA乒乓缓冲 + Flash管理
 *
 *  ADC → DMA → dma_buf[2]乒乓 → 半满中断 → CPU写Flash
 *  录制完成后保存 FaultRecord_t 供后续按指令上传
 *
 *  DMA时序 (64样本):  12kHz填充 5.3ms, Flash写入 1.9ms, 余量 2.7x
 *                     6kHz填充 10.7ms, Flash写入 1.9ms, 余量 5.5x
 *============================================================================*/
typedef struct {
    int16_t dma_buf[2][DMA_PINGPONG_SIZE];  /* DMA乒乓缓冲 (2×64×2B=256B) */
    uint8_t  active_buf;     /* DMA当前写入的缓冲号 (0/1) */
    uint16_t buf_count;      /* 当前缓冲已填充点数 (0~63) */

    uint32_t flash_offset;   /* Flash写入偏移 (环形) */
    uint32_t total_recorded; /* 已录制总点数 */
    uint32_t max_samples;    /* 本次录制目标点数 (预警1200 200ms/紧急7200 500ms) */
    uint32_t record_start_time; /* 录制起始时间戳 */
    uint8_t  is_recording;   /* 录制中标志 */
    SeverityLevel_t severity;/* 当前故障级别 */
    uint16_t sample_rate;    /* 当前采样率 (6000/12000) */
} WaveBuffer_t;

/*============================================================================
 *  FaultRecord_t: 故障记录 (每次录波完成保存一条, 最多8条)
 *  记录Flash偏移和点数, 供 wave_retrieve_by_index() 按指令读取
 *============================================================================*/
typedef struct {
    uint32_t timestamp;       /* 故障发生时间 */
    FaultType_t fault_type;   /* 故障类型 */
    SeverityLevel_t severity; /* 故障级别 */
    float fault_value;        /* 故障值 (RMS) */
    uint32_t flash_offset;    /* 对应波形Flash起始偏移 */
    uint32_t sample_count;    /* 波形采样点数 */
    uint16_t sample_rate;     /* 波形采样率 (6000/12000) */
    uint8_t  valid;           /* 记录有效标志 (1=有效) */
    uint16_t fault_cycle_index; /* 故障所在周期号 */
    uint8_t  node_index;      /* 故障节点号 (0~9) */
} FaultRecord_t;

/*============================================================================
 *  FaultUploadHeader_t: 故障触发时上传的节点头
 *  后续紧跟 FAULT_UPLOAD_POINTS 个 int16 raw数据
 *============================================================================*/
typedef struct {
    uint8_t  data_type;       /* 数据类型: DATA_TYPE_STATUS */
    uint8_t  severity;        /* 故障级别 */
    uint32_t timestamp;       /* 故障发生时间 (ms) */
    FaultType_t fault_type;   /* 故障类型 */
    uint8_t  node_index;      /* 故障节点号 (0~9) */
    uint16_t total_points;    /* 后续raw数据总点数 (=400) */
    uint16_t sample_rate;     /* 采样率 (=2000Hz) */
} FaultUploadHeader_t;

/*============================================================================
 *  上传数据结构: 三模式通用 (12B header + 400点raw数据)
 *
 *  header: data_type/severity/timestamp/node_index/sample_rate/health_score/total_points
 *  后续: int16 raw 分包 (active_power/reactive_power/voltage_angle/current × n)
 *
 *  帧头: 16B,  原始数据: 400 × 16B = 6400B
 *============================================================================*/
typedef struct {
    uint8_t  data_type;       /* 数据类型标识 */
    uint8_t  severity;        /* 严重等级: 0=正常 1=预警 2=紧急 */
    uint32_t timestamp;       /* 毫秒时间戳 */
    uint8_t  node_index;      /* 节点号 (0~9) */
    uint16_t sample_rate;     /* 采样率 (2000Hz) */
    float    health_score;    /* 健康度 (0~100) */
    uint16_t total_points;    /* 后续raw数据总点数 (400) */
} NodeUploadData_t;

/*============================================================================
 *  WaveChunkHeader_t: 波形数据包头 (按指令上传时先发此头, 再发int16数据)
 *============================================================================*/
typedef struct {
    uint8_t  data_type;       /* 数据类型: DATA_TYPE_WAVE */
    uint8_t  severity;        /* 故障级别 */
    uint32_t timestamp;       /* 时间戳 (ms) */
    uint32_t sample_index;    /* 起始采样序号 */
    uint32_t sample_rate;     /* 采样率 (6000或12000Hz) */
    uint16_t sample_count;    /* 后续数据总点数 */
} WaveChunkHeader_t;

/*============================================================================
 *                          对外接口函数
 *============================================================================*/

/*--- 初始化 ---*/
void data_monitor_init(void);                     /* 全局初始化: 缓冲区清零 + Flash擦除 */
void wave_flash_init(void);                       /* Flash波形区全擦除 */

/*--- Flash读写 ---*/
void wave_flash_write_bytes(uint32_t off, const uint8_t *data, uint16_t len);
void wave_flash_read_bytes(uint32_t off, int16_t *data, uint16_t len);

/*--- 数据输入 (由ISR/task调用) ---*/
void bus_data_update(float vmag, float vang, float freq, float batt); /* 更新母线数据 */
void node_sample_process(float p, float q, float node_ang, float cur); /* 2kHz节点采样, 内部触发故障检测 */
void dma_wave_buf_done(uint8_t buf_idx);    /* DMA半满中断回调: 将dma_buf[buf_idx]写入Flash */

/*--- 波形DMA控制 ---*/
void dma_wave_push(int16_t val);             /* ISR向DMA乒乓缓冲写入一个采样点, 半满自动写Flash */
void dma_wave_start(uint16_t rate);          /* 启动DMA波形采集: 配置ADC+DMA双缓冲 */
void dma_wave_stop(void);                    /* 停止DMA波形采集 */
void *dma_wave_buf_ptr(void);               /* 返回DMA乒乓缓冲区首地址 (ADC DMA直写) */

/*--- 故障检测与触发 ---*/
void set_active_node(uint8_t node_idx);           /* 设置当前采样的节点 (0~9) */
uint8_t get_active_node(void);                    /* 获取当前节点号 */
FaultType_t detect_mid_freq_fault(MidFreqWindow_t *w); /* 滑动窗口故障检测 */
SeverityLevel_t classify_severity(FaultType_t f, float rms); /* 判定预警/紧急 */
void trigger_fault(FaultType_t f, SeverityLevel_t s);       /* 触发故障 */
void upload_fault_data(uint16_t fault_cycle_index, FaultType_t f); /* 上传10周期(400点)节点数据 */
void upload_node_status(void);                    /* 正常:10周期 / 预警&紧急: 1点+健康度 */

/*--- 模式切换 ---*/
void switch_to_warning_mode(void);                /* 切换到预警: 6kHz×200ms录波 */
void switch_to_danger_mode(void);                 /* 切换到紧急: 12kHz×600ms录波 */
void switch_to_normal_mode(void);                 /* 恢复到正常: 停止录波 */

/*--- 故障记录查询 (按指令驱动) ---*/
uint8_t wave_retrieve_by_node_fault(uint8_t node_idx, uint8_t fault_idx); /* Flash读波形并发送 */
FaultRecord_t *get_fault_record(uint8_t index);       /* 获取故障记录 */
uint8_t get_fault_record_count(void);                 /* 获取故障记录总数 */

/*--- 辅助查询 ---*/
uint16_t get_current_sample_rate(void);           /* 当前采样率 */
uint8_t  is_wave_recording(void);                 /* 是否正在录波 */
NodeSample_t *get_node_sample(uint16_t index);     /* 获取环缓冲中的采样点 */

/*--- 底层发送 ---*/
void send_normal_data(DataType_t type, void *data, uint16_t len);   /* 非阻塞发送 (状态数据) */
void send_waveform_packet(const uint8_t *data, uint16_t len);       /* 阻塞发送 (波形数据, 保证不丢) */

#endif