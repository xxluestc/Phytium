#include "master.h"
#include "chaos_encrypt.h"
#include "log.h"
#include <string.h>
#include <stdio.h>

#define SLAVE_ADDR_BASE     0x0001
#define FRAME_OVERHEAD      7
#define FRAME_START_0       0xAA
#define FRAME_START_1       0x55
#define FRAME_END_0         0x55
#define FRAME_END_1         0xAA
#define MAX_FRAME_BUF       270

static uint8_t calc_frame_crc8(const uint8_t *data, uint16_t len);

/* ==========================================================================
 *  LoRa 接收接口层 — 仿真与真实硬件切换
 *
 *  LoRa 模块通过 UART 连接到 FreeRTOS CPU3 侧，Linux 不直接操作 LoRa。
 *  当前用仿真函数自驱动验证全链路；接入真实硬件后切换宏定义即可。
 * ========================================================================== */

#define USE_LORA_SIMULATION  1     /* 1=仿真模式, 0=真实LoRa UART */

static uint16_t master_sim_lora_data(uint8_t *buf, uint16_t max_len);
static uint16_t master_lora_uart_recv(uint8_t *buf, uint16_t max_len);

/*
 * master_recv_lora_data: 从LoRa获取原始帧数据的统一入口
 *
 *  仿真模式 (USE_LORA_SIMULATION=1):
 *    调用 master_sim_lora_data() — 状态机生成 LoRa帧(明文),
 *    注入 master_recv_inject_data 管线, 全链路自驱动验证。
 *
 *  真实硬件 (USE_LORA_SIMULATION=0):
 *    调用 master_lora_uart_recv() — 从UART环形缓冲区取原始字节,
 *    交给后续 parse_frame() 处理。
 */
static uint16_t master_recv_lora_data(uint8_t *buf, uint16_t max_len)
{
#if USE_LORA_SIMULATION
    return master_sim_lora_data(buf, max_len);
#else
    return master_lora_uart_recv(buf, max_len);
#endif
}

/* ==========================================================================
 *  真实 LoRa UART 接收 — 后续接入硬件时在此实现
 *
 *  接入步骤:
 *    1. 初始化 PE2204 的 UART3 (或其它UART):
 *       master_lora_uart_init(baud, parity, stop_bits)
 *       配置 GPIO 复用 (TX/RX/AUX 引脚)
 *    2. 配置 UART 接收中断 + DMA:
 *       UART RX ISR → 数据写入环形缓冲区 ring_buf[2048]
 *    3. 本函数从 ring_buf 中提取完整帧:
 *       搜索 0xAA 0x55 帧头 → 读取 LEN → 校验 CRC8 → 返回一帧
 *    4. 没有完整帧时返回 0
 *
 *  涉及的 PE2204 寄存器 (以 UART3 为参考):
 *    UART3_BASE  = 0x2802D000  (需根据硬件确认)
 *    GPIO 复用   = GPIO2_10 (TX), GPIO2_11 (RX)
 *
 *  ATK-MWCC68D LoRa模块默认参数: 9600 baud, 8N1
 * ========================================================================== */
static uint16_t master_lora_uart_recv(uint8_t *buf, uint16_t max_len)
{
    /*
     * TODO: 真实 LoRa UART 数据接收
     *
     * 预期实现框架:
     *
     *   static uint8_t  ring_buf[2048];
     *   static uint16_t ring_rd = 0;
     *   static uint16_t ring_wr = 0;
     *
     *   uint16_t avail = (ring_wr - ring_rd) & 0x07FF;
     *   if (avail < 7) return 0;   // 至少需要帧头(4B) + 帧尾(3B)
     *
     *   // 在 ring_buf 中搜索帧头 0xAA 0x55
     *   uint16_t search = ring_rd;
     *   while (search != ring_wr) {
     *       if (ring_buf[search] == 0xAA
     *           && ring_buf[(search + 1) & 0x07FF] == 0x55) break;
     *       search = (search + 1) & 0x07FF;
     *   }
     *   if (search == ring_wr) { ring_rd = search; return 0; }
     *
     *   // 读取 LEN = data_len
     *   // ...出帧逻辑与 inject_data 的 parse_frame 一致...
     *
     *   ring_rd = (search + frame_total) & 0x07FF;
     *   memcpy(buf, &ring_buf[search], frame_total);
     *   return frame_total;
     */

    (void)buf;
    (void)max_len;
    return 0;
}

#define SIM_NODES             3
#define SIM_SAMPLES           80        /* 总采样点数: 2周期 × 40/周期 */
#define SIM_SAMPLES_PER_FRAME 10        /* 每帧携带10个采样点 */
#define SIM_INIT_WAIT         30        /* 初始等待 ~300ms (30 × 10ms) */
#define SIM_INTER_WAIT        50        /* 节点间等待 ~500ms */

static uint16_t master_sim_lora_data(uint8_t *buf, uint16_t max_len)
{
    /* 模拟器持久状态 */
    static uint32_t s_phase        = 0;  /* 0=initwait, 1=header, 2=samples, 3=interwait */
    static uint32_t s_tick         = 0;
    static uint32_t s_node         = 0;
    static uint32_t s_sample_idx   = 0;

    uint32_t timestamp;
    uint16_t payload_len;
    uint16_t data_len;
    uint16_t frame_len;
    uint16_t i;
    uint8_t  crc;
    uint32_t off;

    s_tick++;

    /* ---- Phase 0: 系统初始化等待 ---- */
    if (s_phase == 0) {
        if (s_tick < SIM_INIT_WAIT) return 0;
        s_phase = 1;
        s_tick  = 0;
        /* fall through to phase 1 */
    }

    /* ---- Phase 1: 发送故障上报头 ---- */
    if (s_phase == 1) {
        FaultUploadHeader_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.data_type   = DATA_TYPE_STATUS;
        hdr.severity    = SEVERITY_DANGER;             /* 触发判决 */
        hdr.timestamp   = s_node * 1000;               /* 模拟时间戳 */
        hdr.fault_type  = (s_node == 0) ? FAULT_OVER_VOLTAGE
                        : (s_node == 1) ? FAULT_UNDER_VOLTAGE
                        : FAULT_VOLTAGE_SWELL;          /* 3节点不同故障 */
        hdr.node_index  = (uint8_t)s_node;
        hdr.total_points = SIM_SAMPLES;
        hdr.sample_rate  = NODE_SAMPLE_RATE;

        payload_len = (uint16_t)sizeof(FaultUploadHeader_t);
        data_len    = 10 + payload_len;          /* 10B frame overhead */
        frame_len   = 4 + data_len + 3;          /* 0xAA 0x55 LEN + data + CRC + 0x55 0xAA */

        if (frame_len > max_len) return 0;

        off = 0;
        buf[off++] = 0xAA;
        buf[off++] = 0x55;
        buf[off++] = (uint8_t)(data_len >> 8);   /* LEN_H */
        buf[off++] = (uint8_t)(data_len);        /* LEN_L */

        /* DATA section */
        timestamp = hdr.timestamp;
        buf[off++] = (uint8_t)(timestamp >> 24);  /* timestamp[0] */
        buf[off++] = (uint8_t)(timestamp >> 16);  /* timestamp[1] */
        buf[off++] = (uint8_t)(timestamp >> 8);   /* timestamp[2] */
        buf[off++] = (uint8_t)(timestamp);        /* timestamp[3] */

        buf[off++] = DATA_TYPE_STATUS;            /* frame_type */

        buf[off++] = 0x00; buf[off++] = 0x00;     /* sync_code = 0 */
        buf[off++] = 0x00; buf[off++] = 0x00;

        buf[off++] = DATA_TYPE_STATUS;            /* rx_type */

        memcpy(&buf[off], &hdr, payload_len);     /* payload: FaultUploadHeader_t */
        off += payload_len;

        /* CRC8: 计算 DATA 段的 CRC */
        crc = calc_frame_crc8(&buf[4], data_len); /* buf[4] 即 DATA 起始 */
        buf[off++] = crc;
        buf[off++] = 0x55;
        buf[off++] = 0xAA;

        s_phase        = 2;
        s_sample_idx   = 0;

        return off;
    }

    /* ---- Phase 2: 分批发送状态采样数据 ---- */
    if (s_phase == 2) {
        uint16_t samples_this_frame = SIM_SAMPLES_PER_FRAME;
        if (s_sample_idx + samples_this_frame > SIM_SAMPLES)
            samples_this_frame = SIM_SAMPLES - s_sample_idx;
        if (samples_this_frame == 0) {
            s_phase = 3;
            s_tick  = 0;
            return 0;
        }

        payload_len = (uint16_t)(samples_this_frame * sizeof(NodeSample_t));
        data_len    = 10 + payload_len;
        frame_len   = 4 + data_len + 3;

        if (frame_len > max_len) return 0;

        off = 0;
        buf[off++] = 0xAA;
        buf[off++] = 0x55;
        buf[off++] = (uint8_t)(data_len >> 8);
        buf[off++] = (uint8_t)(data_len);

        /* DATA section header */
        timestamp = s_node * 1000 + s_sample_idx;
        buf[off++] = (uint8_t)(timestamp >> 24);
        buf[off++] = (uint8_t)(timestamp >> 16);
        buf[off++] = (uint8_t)(timestamp >> 8);
        buf[off++] = (uint8_t)(timestamp);

        buf[off++] = DATA_TYPE_NODE_RAW;           /* frame_type */

        buf[off++] = 0x00; buf[off++] = 0x00;      /* sync_code = 0 */
        buf[off++] = 0x00; buf[off++] = 0x00;

        buf[off++] = DATA_TYPE_NODE_RAW;           /* rx_type */

        /* payload: NodeSample_t 数组 */
        for (i = 0; i < samples_this_frame; i++) {
            NodeSample_t samp;
            /* 模拟真实电网数据: 在不同故障类型下构造异常值 */
            if (s_node == 0) {
                /* 过压: 电压偏高 */
                samp.active_power   = 1200 + (int32_t)(s_sample_idx + i) * 3;
                samp.reactive_power = 200 + (int32_t)(s_sample_idx + i);
                samp.voltage_angle  = 0;
                samp.voltage_mag    = 245000 + (int32_t)(s_sample_idx + i) * 50; /* >230V */
            } else if (s_node == 1) {
                /* 欠压: 电压偏低 */
                samp.active_power   = 800 - (int32_t)(s_sample_idx + i) * 2;
                samp.reactive_power = 150;
                samp.voltage_angle  = 0;
                samp.voltage_mag    = 185000 - (int32_t)(s_sample_idx + i) * 30; /* <210V */
            } else {
                /* 电压骤升: 先正常后骤升 */
                samp.active_power   = 1000 + (int32_t)(s_sample_idx + i) * 2;
                samp.reactive_power = 180;
                samp.voltage_angle  = 0;
                samp.voltage_mag    = (s_sample_idx + i < 40) ? 220000
                                    : 260000 + (int32_t)(s_sample_idx + i - 40) * 100;
            }
            memcpy(&buf[off], &samp, sizeof(NodeSample_t));
            off += sizeof(NodeSample_t);
            s_sample_idx++;
        }

        /* CRC8 */
        crc = calc_frame_crc8(&buf[4], data_len);
        buf[off++] = crc;
        buf[off++] = 0x55;
        buf[off++] = 0xAA;

        /* 此帧发送完毕后检查是否本轮完成 */
        if (s_sample_idx >= SIM_SAMPLES) {
            s_phase = 3;
            s_tick  = 0;
        }

        return off;
    }

    /* ---- Phase 3: 节点间等待 (留给 judge 判决/命令队列处理) ---- */
    if (s_phase == 3) {
        if (s_tick < SIM_INTER_WAIT) return 0;
        s_node  = (s_node + 1) % SIM_NODES;
        s_phase = 1;
        s_tick  = 0;
        s_sample_idx = 0;
        return 0;
    }

    return 0;
}

static void process_status_header(const uint8_t *payload, uint16_t len, uint16_t src_addr,
                                   MasterDownloadBuf_t *dl, MasterNodeInfo_t *node)
{
    uint8_t node_id;

    if (len >= sizeof(FaultUploadHeader_t)) {
        FaultUploadHeader_t hdr;
        memcpy(&hdr, payload, sizeof(hdr));
        node_id = hdr.node_index;
        if (node_id >= MASTER_MAX_NODES) return;

        dl->active = 1;
        dl->node_id = node_id;
        dl->data_type = DATA_TYPE_STATUS;
        dl->expected_points = hdr.total_points;
        dl->received_points = 0;
        dl->sample_rate = hdr.sample_rate;
        dl->severity = hdr.severity;

        node->last_status_type = 1;
        node->last_total_points = hdr.total_points;
        node->last_sample_rate = hdr.sample_rate;
        node->last_status_timestamp = hdr.timestamp;
        node->severity = hdr.severity;
        node->fault_type = hdr.fault_type;
        node->last_status_fault = hdr.fault_type;
        if (hdr.fault_type != FAULT_NONE) node->fault_count++;

        log_info("Fault hdr: node%d t=%d type=%d sev=%d pts=%d",
                 node_id, hdr.timestamp, hdr.fault_type, hdr.severity, hdr.total_points);
    } else if (len >= sizeof(NodeUploadData_t)) {
        NodeUploadData_t hdr;
        memcpy(&hdr, payload, sizeof(hdr));
        node_id = hdr.node_index;
        if (node_id >= MASTER_MAX_NODES) return;

        dl->active = 1;
        dl->node_id = node_id;
        dl->data_type = DATA_TYPE_STATUS;
        dl->expected_points = hdr.total_points;
        dl->received_points = 0;
        dl->sample_rate = hdr.sample_rate;
        dl->severity = hdr.severity;

        node->last_status_type = 0;
        node->last_total_points = hdr.total_points;
        node->last_sample_rate = hdr.sample_rate;
        node->last_health_score = hdr.health_score;
        node->severity = hdr.severity;

        log_debug("Status hdr: node%d sev=%d health=%.1f pts=%d",
                  node_id, hdr.severity, hdr.health_score, hdr.total_points);
    }
}

static void process_node_raw(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                              MasterNodeInfo_t *node)
{
    if (!dl->active || dl->data_type != DATA_TYPE_STATUS) return;

    uint16_t samples_in_pkt = len / sizeof(NodeSample_t);
    if (samples_in_pkt == 0)
        return;
    if (dl->received_points + samples_in_pkt > FAULT_UPLOAD_POINTS) {
        samples_in_pkt = FAULT_UPLOAD_POINTS - dl->received_points;
    }

    memcpy(&dl->node_buffer[dl->received_points], payload,
           samples_in_pkt * sizeof(NodeSample_t));
    dl->received_points += samples_in_pkt;

    if (dl->received_points >= dl->expected_points) {
        master_flash_save_node_data(dl->node_id, dl->node_buffer, dl->received_points);
        dl->active = 0;
        log_info("Node%d: 10-cycle saved (%d pts)", dl->node_id, dl->received_points);
    }
}

static void process_wave_header(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                                 MasterNodeInfo_t *node, uint16_t src_addr)
{
    if (len < sizeof(WaveChunkHeader_t)) return;

    WaveChunkHeader_t hdr;
    memcpy(&hdr, payload, sizeof(hdr));
    uint8_t node_id = (uint8_t)(src_addr - 0x0001);
    if (node_id >= MASTER_MAX_NODES) node_id = 0;

    dl->active = 1;
    dl->node_id = node_id;
    dl->data_type = DATA_TYPE_WAVE;
    dl->expected_points = hdr.sample_count;
    dl->received_points = 0;
    dl->sample_rate = hdr.sample_rate;
    dl->severity = hdr.severity;

    node->has_last_wave_hdr = 1;
    node->last_wave_rate = hdr.sample_rate;
    node->last_wave_samples = hdr.sample_count;
    node->last_wave_severity = (SeverityLevel_t)hdr.severity;

    master_flash_erase_wave(node_id);

    log_info("Wave hdr: node%d rate=%d sev=%d samp=%d",
             node_id, hdr.sample_rate, hdr.severity, hdr.sample_count);
}

static void process_flash_wave(const uint8_t *payload, uint16_t len, MasterDownloadBuf_t *dl,
                                MasterNodeInfo_t *node)
{
    if (!dl->active || dl->data_type != DATA_TYPE_WAVE) return;

    uint16_t byte_offset = dl->received_points * sizeof(int16_t);
    master_flash_save_wave_data(dl->node_id, payload, len, byte_offset);
    dl->received_points += (len / sizeof(int16_t));

    if (dl->received_points >= dl->expected_points) {
        master_recv_wave_data(dl->node_id, dl->received_points);
        dl->active = 0;
        log_info("Wave saved: node%d %d samp@%dHz",
                 dl->node_id, dl->received_points, dl->sample_rate);
    }
}

static void process_fault_list(const uint8_t *payload, uint16_t len, uint16_t src_addr,
                                MasterNodeInfo_t *node)
{
    if (len > 8) len = 8;
    uint8_t valid_count = 0;
    for (uint8_t i = 0; i < len; i++) {
        if (payload[i]) valid_count++;
    }
    log_info("Fault list: node%d=%d valid/%d", src_addr, valid_count, len);
}

static uint8_t calc_frame_crc8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else            crc <<= 1;
        }
    }
    return crc;
}

static int parse_frame(const uint8_t *raw_data, uint16_t raw_len,
                        uint8_t *out_data, uint16_t *out_data_len,
                        uint8_t *out_type, uint32_t *out_timestamp)
{
    if (!raw_data || raw_len < (FRAME_OVERHEAD + 6)) return 0;

    uint16_t pos = 0;
    while (pos + 1 < raw_len) {
        if (raw_data[pos] == FRAME_START_0 && raw_data[pos + 1] == FRAME_START_1) break;
        pos++;
    }
    if (pos + 1 >= raw_len) return 0;

    uint16_t frame_start = pos;
    if (frame_start + 2 >= raw_len) return 0;

    uint16_t data_len = ((uint16_t)raw_data[frame_start + 2] << 8) | raw_data[frame_start + 3];
    uint16_t frame_total = FRAME_OVERHEAD + data_len;
    if (frame_start + frame_total > raw_len) return 0;

    const uint8_t *data_ptr = &raw_data[frame_start + 4];
    uint8_t crc_received = raw_data[frame_start + 4 + data_len];
    if (calc_frame_crc8(data_ptr, data_len) != crc_received) return 0;

    if (raw_data[frame_start + 4 + data_len + 1] != FRAME_END_0 ||
        raw_data[frame_start + 4 + data_len + 2] != FRAME_END_1) return 0;

    *out_timestamp  = ((uint32_t)data_ptr[0] << 24) | ((uint32_t)data_ptr[1] << 16)
                    | ((uint32_t)data_ptr[2] << 8)  |  data_ptr[3];
    *out_type       = data_ptr[4];
    *out_data_len   = data_len - 5;
    memcpy(out_data, &data_ptr[5], *out_data_len);
    return 1;
}

/*
 * master_recv_inject_data: RPMsg注入数据接口
 *
 * 当Linux侧通过RPMsg将LoRa数据转发给FreeRTOS时，
 * 调用此函数将数据注入到接收处理流程。
 * 这保持了与原GD32 master_recv_task 相同的处理逻辑。
 */
void master_recv_inject_data(const uint8_t *data, uint16_t len)
{
    uint8_t  inner_data[MAX_FRAME_BUF];
    uint16_t inner_len;
    uint8_t  inner_type;
    uint32_t timestamp;

    uint8_t  dec_buf[128];
    uint16_t dec_len;

    MasterDownloadBuf_t *dl = master_get_download_buf();
    MasterNodeInfo_t *node;

    if (!parse_frame(data, len, inner_data, &inner_len, &inner_type, &timestamp)) {
        return;
    }
    if (inner_len < 5) return;

    uint32_t sync_code = ((uint32_t)inner_data[0] << 24) | ((uint32_t)inner_data[1] << 16)
                       | ((uint32_t)inner_data[2] << 8)  |  (uint32_t)inner_data[3];
    uint8_t  rx_type   = inner_data[4];
    uint16_t enc_len   = inner_len - 5;

    if (sync_code != 0 && enc_len > 0 && enc_len <= MAX_ENCRYPT_DATA_LEN) {
        dec_len = chaos_decrypt_packet(&inner_data[5], enc_len, dec_buf, sync_code);
    } else {
        memcpy(dec_buf, &inner_data[5], enc_len);
        dec_len = enc_len;
    }

    uint8_t node_id = 0;
    if (dl->active) {
        node_id = dl->node_id;
    } else {
        if (rx_type == DATA_TYPE_STATUS && dec_len >= sizeof(FaultUploadHeader_t)) {
            FaultUploadHeader_t hdr;
            memcpy(&hdr, dec_buf, sizeof(hdr));
            node_id = hdr.node_index;
        } else if (rx_type == DATA_TYPE_STATUS && dec_len >= sizeof(NodeUploadData_t)) {
            NodeUploadData_t hdr;
            memcpy(&hdr, dec_buf, sizeof(hdr));
            node_id = hdr.node_index;
        }
        if (node_id >= MASTER_MAX_NODES) node_id = 0;
    }

    uint16_t src_addr = SLAVE_ADDR_BASE + node_id;
    node = master_get_node_info(node_id);
    if (!node) return;
    node->is_online = 1;
    node->last_recv_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    switch (rx_type) {
    case DATA_TYPE_STATUS:
        dl->active = 0;
        process_status_header(dec_buf, dec_len, src_addr, dl, node);
        break;
    case DATA_TYPE_WAVE:
        dl->active = 0;
        process_wave_header(dec_buf, dec_len, dl, node, src_addr);
        break;
    case DATA_TYPE_POWER:
        log_debug("Power: len=%d", dec_len);
        break;
    case DATA_TYPE_NODE_RAW:
        process_node_raw(dec_buf, dec_len, dl, node);
        break;
    case DATA_TYPE_FLASH_WAVE:
        process_flash_wave(dec_buf, dec_len, dl, node);
        break;
    case DATA_TYPE_FAULT_LIST:
        dl->active = 0;
        process_fault_list(dec_buf, dec_len, src_addr, node);
        break;
    default:
        break;
    }
}

/*
 * master_recv_task: 主接收任务
 *
 * LoRa模块直连 FreeRTOS CPU3 侧 UART。
 * 数据路径:
 *   master_recv_lora_data() → 仿真(g_sim) / 真实UART(uart_recv)
 *     → master_recv_inject_data() → parse_frame() → 分发处理
 *
 * Linux 不参与 LoRa 数据收发，只通过 RPMsg 接收处理后的状态/命令。
 */
void master_recv_task(void *pvParameters)
{
    uint8_t  raw_buf[MAX_FRAME_BUF];
    uint16_t raw_len;

    (void)pvParameters;

    log_info("Recv task started");

    while (1) {
        raw_len = master_recv_lora_data(raw_buf, sizeof(raw_buf));
        if (raw_len == 0) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        master_recv_inject_data(raw_buf, raw_len);

        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}