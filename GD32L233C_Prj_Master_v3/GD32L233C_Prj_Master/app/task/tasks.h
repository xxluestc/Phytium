#ifndef __TASKS_H
#define __TASKS_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "data_frame.h"

#define NODE_MAX_COUNT     10    // 最大节点数量
#define MASTER_ADDR         0x0000  // 主节点地址值
#define SLAVE_ADDR_BASE     0x0001  // 从节点地址基础值
#define LORA_BUFFER_SIZE    256     // LORA接收缓冲区大小

#endif /* __TASKS_H */