#include "main.h"
#include "gd32l23x_it.h"
#include "systick.h"
#include "gd32l233r_eval.h"
#include "mwcc68_app.h"
#include "mwcc68_uart.h"
#include "data_frame.h"
#include "master.h"
#include "log.h"
#include "chaos_encrypt.h"
#include <stdio.h>

void task_create(void)
{
    BaseType_t ret;
    
    /*创建接收任务*/
    ret = xTaskCreate(master_recv_task,
                      "MasterRecv",
                      MASTER_RECV_STK_SIZE,
                      NULL,
                      MASTER_RECV_TASK_PRIO,
                      NULL);
    if (ret != pdPASS) log_error("Failed to create master_recv_task");

    /*创建判断任务*/
    ret = xTaskCreate(master_judge_task,
                      "MasterJudge",
                      MASTER_JUDGE_STK_SIZE,
                      NULL,
                      MASTER_JUDGE_TASK_PRIO,
                      NULL);
    if (ret != pdPASS) log_error("Failed to create master_judge_task");

    /*创建命令任务*/
    ret = xTaskCreate(master_cmd_task,
                      "MasterCmd",
                      MASTER_CMD_STK_SIZE,
                      NULL,
                      MASTER_CMD_TASK_PRIO,
                      NULL);
    if (ret != pdPASS) log_error("Failed to create master_cmd_task");
}

int main(void)
{
    gd_eval_led_init(LED1);
    gd_eval_led_init(LED2);

    usart0_init();

    printf("=== GD32L233C Master Controller ===");

    log_info("Build: %s %s", __DATE__, __TIME__);

    LoRa_Init();     // LoRa模块初始化
    log_info("LoRa module initialized");

    chaos_init(0x12345678);
    log_info("Chaos initialized");

    master_init();

    task_create();

    log_info("FreeRTOS scheduler starting...");
    vTaskStartScheduler();

    while (1);
}