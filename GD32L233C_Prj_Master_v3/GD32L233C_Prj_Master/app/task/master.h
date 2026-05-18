#ifndef __MASTER_H
#define __MASTER_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "data_frame.h"

/*============================================================================
 *  命令码定义 (与终端 tasks.h 公用)
 *============================================================================*/
#define CMD_REQUEST_WAVEFORM          0x10    /* 请求上传指定节点/故障的已录波形 */
#define CMD_REQUEST_FAULT_LIST        0x11    /* 请求上传故障记录列表 */
#define CMD_CLEAR_FLASH               0x12    /* 清除 Flash 波形区 */
#define CMD_START_WAVE_COLLECT        0x13    /* 预留: 指令终端启动新波形采集 */

/*============================================================================
 *  主控节点管理常量
 *============================================================================*/
#define MASTER_MAX_NODES            10      /* 最大监控终端数 */
#define MASTER_WAVE_MAX_MS          500     /* 波形采集最大时长(ms) */
#define MASTER_WAVE_RATE_6000       6000    /* 采集采样率: 6kHz */
#define MASTER_WAVE_RATE_15000      15000   /* 采集采样率: 15kHz */
#define MASTER_WAVE_MAX_SAMPLES     (MASTER_WAVE_RATE_15000 * MASTER_WAVE_MAX_MS / 1000)

#define MASTER_NODE_TIMEOUT_MS      15000   /* 节点超时: 15秒无数据视为离线 */
#define MASTER_JUDGE_INTERVAL_MS    1000    /* 健康判断周期: 1秒 */
#define MASTER_CMD_RETRY_MAX        3       /* 指令重发最大次数 */

/*============================================================================
 *  每节点10周期状态数据: 400点 × 16B = 6400B/节点, 10节点=64KB
 *
 *  SRAM放不下 → 内部Flash分区存储, 状态数据与波形数据完全分离:
 *
 *    [状态数据区] 0x08030000 ~ 0x08040000  (64KB)
 *      node_0: 0x08030000 [6400B]    node_5: 0x08037D00 [6400B]
 *      node_1: 0x08031900 [6400B]    node_6: 0x08039600 [6400B]
 *      ...
 *      每节点独占 6400B, 页对齐 → 擦除状态数据不影响波形
 *
 *    [波形数据区] 0x08040000 ~ 0x08050000  (64KB)
 *      node_0: 0x08040000 [~6400B]   node_5: 0x08047D00 [~6400B]
 *      ...同上布局
 *
 *  RAM 中仅保留下载缓冲区, 接收完毕后写入 Flash
 *============================================================================*/
#define MASTER_STATUS_FLASH_BASE    0x08030000
#define MASTER_WAVE_FLASH_BASE      0x08040000
#define MASTER_FLASH_PER_NODE       0x1900      /* 6400B/节点 (页对齐) */
#define MASTER_FLASH_AREA_SIZE      (64 * 1024)  /* 每区 64KB */

/*============================================================================
 *  节点状态快照 (RAM中常驻, 每节点 ~40B, 10节点 = 400B)
 *============================================================================*/
typedef struct {
    uint8_t      node_id;
    uint8_t      is_online;
    uint8_t      has_status_data;      /* Flash中有有效10周期数据 */
    uint8_t      has_wave_data;         /* Flash中有有效波形数据 */
    uint8_t      severity;
    FaultType_t  fault_type;
    uint32_t     last_recv_time;
    uint32_t     fault_count;

    /* 最近一次状态数据头 (10周期) */
    uint8_t      last_status_type;     /* 0=NodeUploadData_t, 1=FaultUploadHeader_t */
    uint16_t     last_total_points;
    uint16_t     last_sample_rate;
    float        last_health_score;
    uint32_t     last_status_timestamp;
    FaultType_t  last_status_fault;

    /* 最近一次波形数据头 */
    uint8_t      has_last_wave_hdr;
    uint32_t     last_wave_rate;
    uint32_t     last_wave_samples;
    SeverityLevel_t last_wave_severity;

    /* 指令状态 */
    uint8_t      wave_pending;
    uint8_t      cmd_retry;
} MasterNodeInfo_t;

/*============================================================================
 *  当前正在接收的节点, 接收完毕后写入Flash
 *============================================================================*/
typedef struct {
    uint8_t  active;              /* 是否有正在进行的下载 */
    uint8_t  node_id;             /* 当前接收的节点 */
    uint8_t  data_type;           /* 当前DT: DATA_TYPE_STATUS或DATA_TYPE_WAVE */
    uint16_t expected_points;     /* 期望的总点数 */
    uint16_t received_points;     /* 已接收点数 */
    uint32_t sample_rate;
    uint8_t  severity;
    NodeSample_t node_buffer[FAULT_UPLOAD_POINTS];  /* 400点样本缓冲区 (6400B) */
} MasterDownloadBuf_t;

/*============================================================================
 *  主控内部指令类型
 *============================================================================*/
typedef enum {
    MASTER_CMD_NONE = 0,     /* 无指令 */
    MASTER_CMD_REQ_WAVE,     /* 请求波形数据 */
    MASTER_CMD_REQ_FAULT_LIST, /* 请求故障列表 */
    MASTER_CMD_CLEAR_FLASH,  /* 清除Flash */
    MASTER_CMD_WAVE_COLLECT   /* 波形采集 */
} MasterCmdType_t;

/*============================================================================
 *  波形采集速率枚举 (终端侧采集用)
 *============================================================================*/
typedef enum {
    COLLECT_RATE_6000  = 6000,
    COLLECT_RATE_15000 = 15000
} CollectRate_t;

/*============================================================================
 *  主控内部指令结构
 *============================================================================*/
typedef struct {
    MasterCmdType_t cmd_type;
    uint8_t  node_id;
    uint8_t  fault_idx;
    uint16_t sample_rate;
    uint16_t duration_ms;
} MasterInternalCmd_t;

/*============================================================================
 *  任务优先级与堆栈
 *============================================================================*/
#define MASTER_RECV_TASK_PRIO       4       /* 接收优先于指令发送, 避免饥饿 */
#define MASTER_JUDGE_TASK_PRIO      5       /* 最高: 遍历极快 */
#define MASTER_CMD_TASK_PRIO        3       /* 最低: 等ACK时让位给接收 */

#define MASTER_RECV_STK_SIZE        512     /* 256→512: g_dl_buf 6400B 不在栈上 */
#define MASTER_JUDGE_STK_SIZE       256
#define MASTER_CMD_STK_SIZE         256

/*============================================================================
 *  队列长度
 *============================================================================*/
#define MASTER_CMD_QUEUE_LEN        5

/*============================================================================
 *  外部声明
 *============================================================================*/
extern QueueHandle_t g_master_cmd_queue;

/*============================================================================
 *  初始化函数
 *============================================================================*/
void master_init(void);

/*============================================================================
 *  任务函数
 *============================================================================*/
void master_recv_task(void *pvParameters);
void master_judge_task(void *pvParameters);
void master_cmd_task(void *pvParameters);

/*============================================================================
 *  公共接口
 *============================================================================*/
MasterNodeInfo_t *master_get_node_info(uint8_t node_id);
void master_recv_wave_data(uint8_t node_id, uint16_t count);

/*============================================================================
 *  新: Flash 存储访问接口
 *============================================================================*/
void master_flash_save_node_data(uint8_t node_id, const NodeSample_t *data, uint16_t count);
uint16_t master_flash_load_node_data(uint8_t node_id, NodeSample_t *buf, uint16_t max_count);
void master_flash_erase_node(uint8_t node_id);

void master_flash_save_wave_data(uint8_t node_id, const uint8_t *data, uint16_t len,
                                  uint32_t offset);
uint16_t master_flash_load_wave_data(uint8_t node_id, uint8_t *buf, uint16_t len);
void master_flash_erase_wave(uint8_t node_id);

MasterDownloadBuf_t *master_get_download_buf(void);

/*============================================================================
 *  获取指定节点状态专用接口
 *============================================================================*/
#endif /* __MASTER_H */