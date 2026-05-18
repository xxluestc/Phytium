#include "master.h"
#include "log.h"

void master_judge_task(void *pvParameters)
{
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t now_ms;

    log_info("Judge task started");

    /*检查每个节点状态，判断是否超时*/
    while (1) {
        now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        for (uint8_t i = 0; i < MASTER_MAX_NODES; i++) {
            MasterNodeInfo_t *n = master_get_node_info(i);
            if (!n) continue;

            uint32_t elapsed = now_ms - n->last_recv_time;

            if (elapsed > MASTER_NODE_TIMEOUT_MS && n->is_online) {
                n->is_online = 0;
                log_warn("Node%d offline (%dms)", i, elapsed);
                continue;
            }

            if (!n->is_online) continue;

            if (n->severity >= SEVERITY_WARNING && n->fault_type != FAULT_NONE
                && !n->wave_pending) {
                MasterInternalCmd_t cmd;
                cmd.cmd_type = MASTER_CMD_REQ_WAVE;
                cmd.node_id = i;
                cmd.fault_idx = 0;
                cmd.sample_rate = MASTER_WAVE_RATE_6000;
                cmd.duration_ms = 250;
                if (xQueueSend(g_master_cmd_queue, &cmd, 0) == pdPASS) {
                    n->wave_pending = 1;
                    log_info("Judge: node%d fault, req wave", i);
                }
            }
        }

        vTaskDelayUntil(&last_wake, MASTER_JUDGE_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}