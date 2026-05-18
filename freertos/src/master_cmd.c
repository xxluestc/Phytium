#include "master.h"
#include "chaos_encrypt.h"
#include "log.h"
#include <string.h>

static uint8_t g_enc_buf[128];
static uint8_t g_lora_pkt[256];

/*
 * master_send_cmd_to_linux: 通过RPMsg将指令发送到Linux侧
 *
 * 移植说明:
 *   原GD32通过 LoRa_SendData_Direct() 直接发送指令到终端节点。
 *   移植后架构:
 *     FreeRTOS → RPMsg → Linux → LoRa模块 → 终端节点
 *   当前LoRa模块未接，此函数为stub，仅log。
 *
 *   cmd_node_id: 目标节点ID
 *   cmd_code:    命令码 (CMD_REQUEST_WAVEFORM 等)
 *   params:      命令参数
 *   param_len:   参数长度
 *
 *   RPMsg消息格式 (与Linux侧约定):
 *     [4B command=DEVICE_MASTER_CMD][2B length][1B node_id][1B cmd_code][nB params]
 */
extern int rpmsg_send_master_cmd(uint8_t node_id, uint8_t cmd_code,
                                  const uint8_t *params, uint8_t param_len);

static void send_lora_cmd(uint8_t node_id, uint8_t cmd_code, const uint8_t *params, uint8_t param_len)
{
    uint8_t payload[32];
    payload[0] = cmd_code;
    if (params && param_len > 0 && param_len < 31)
        memcpy(&payload[1], params, param_len);
    uint8_t total = 1 + param_len;

    uint32_t sync;
    uint16_t enc_len = chaos_encrypt_packet(payload, total, g_enc_buf, &sync);

    g_lora_pkt[0] = (sync >> 24) & 0xFF;
    g_lora_pkt[1] = (sync >> 16) & 0xFF;
    g_lora_pkt[2] = (sync >> 8) & 0xFF;
    g_lora_pkt[3] = sync & 0xFF;
    g_lora_pkt[4] = 0x00;
    memcpy(&g_lora_pkt[5], g_enc_buf, enc_len);

    /*
     * 发送方式:
     *   1. RPMsg → Linux → LoRa (当LoRa接Linux侧时)
     *   2. 直接UART → LoRa (当LoRa接FreeRTOS侧时)
     *   当前使用RPMsg转发方式。
     */
    int ret = rpmsg_send_master_cmd(node_id, cmd_code, params, param_len);
    if (ret >= 0) {
        MasterNodeInfo_t *n = master_get_node_info(node_id);
        if (n) { n->wave_pending = 0; n->cmd_retry = 0; }
        log_debug("CMD node%d sent via RPMsg, code=0x%02X", node_id, cmd_code);
    } else {
        MasterNodeInfo_t *n = master_get_node_info(node_id);
        if (n) {
            n->cmd_retry++;
            if (n->cmd_retry < MASTER_CMD_RETRY_MAX) {
                n->wave_pending = 0;
            }
        }
        log_warn("CMD node%d send failed, ret=%d, retry=%d",
                 node_id, ret, n ? n->cmd_retry : 0);
    }

    (void)g_lora_pkt;
}

void master_cmd_task(void *pvParameters)
{
    MasterInternalCmd_t cmd;

    log_info("Cmd task started");

    while (1) {
        if (xQueueReceive(g_master_cmd_queue, &cmd, portMAX_DELAY) != pdPASS)
            continue;

        MasterNodeInfo_t *n = master_get_node_info(cmd.node_id);
        if (!n || !n->is_online) continue;

        switch (cmd.cmd_type) {
        case MASTER_CMD_REQ_WAVE: {
            uint8_t params[2] = { cmd.node_id, cmd.fault_idx };
            send_lora_cmd(cmd.node_id, CMD_REQUEST_WAVEFORM, params, 2);
            log_info("CMD: req wave node%d fault#%d", cmd.node_id, cmd.fault_idx);
            break;
        }
        case MASTER_CMD_REQ_FAULT_LIST: {
            send_lora_cmd(cmd.node_id, CMD_REQUEST_FAULT_LIST, NULL, 0);
            log_info("CMD: req fault list node%d", cmd.node_id);
            break;
        }
        case MASTER_CMD_CLEAR_FLASH: {
            send_lora_cmd(cmd.node_id, CMD_CLEAR_FLASH, NULL, 0);
            log_info("CMD: clear flash node%d", cmd.node_id);
            break;
        }
        case MASTER_CMD_WAVE_COLLECT: {
            uint8_t params[2] = { (uint8_t)(cmd.sample_rate & 0xFF),
                                  (uint8_t)(cmd.sample_rate >> 8) };
            send_lora_cmd(cmd.node_id, CMD_START_WAVE_COLLECT, params, 2);
            log_info("CMD: start wave collect node%d %dHz", cmd.node_id, cmd.sample_rate);
            break;
        }
        default:
            break;
        }
    }
}