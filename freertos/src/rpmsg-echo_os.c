/*
 * Copyright (C) 2022, Phytium Technology Co., Ltd.   All Rights Reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use
 * this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 * 
 * FilePath: rpmsg-echo_os.c
 * Created Date: 2022-02-25 09:12:07
 * Last Modified: 2025-03-17 10:25:19
 * Description:  This file is for This file is for a sample demonstration application that showcases usage of rpmsg.
 *               This application is meant to run on the remote CPU running freertos code.
 *               This application echoes back data that was sent to it by the driver core.
 * 
 * Modify History: 
 *  Ver   Who        Date         Changes
 * ----- ------     --------    --------------------------------------
 * 1.0 huanghe    2022/03/25  first commit  
 * 1.1 liusm	  2023/11/17  Adapter example for linux
 * 1.2 liusm 	  2024/03/04  update example
 */

/***************************** Include Files *********************************/

#include <stdio.h>
#include <openamp/open_amp.h>
#include <openamp/version.h>
#include <metal/alloc.h>
#include <metal/version.h>
#include "platform_info.h"
#include "rpmsg_service.h"
#include <metal/sleep.h>
#include "rsc_table.h"
#include "FreeRTOS.h"
#include "task.h"
#include "finterrupt.h"
#include "fpsci.h"
#include "fdebug.h"
#include "helper.h"
#include "openamp_configs.h"
#include "rsc_table.h"
#include "libmetal_configs.h"
#include "slaver_00_example.h"
#include "master.h"

/**************************** Type Definitions *******************************/

#define OPENAMP_DEVICE_DEBUG_TAG "OPENAMP_DEVICE"
#define OPENAMP_DEVICE_ERROR(format, ...) FT_DEBUG_PRINT_E(OPENAMP_DEVICE_DEBUG_TAG, format, ##__VA_ARGS__)
#define OPENAMP_DEVICE_WARN(format, ...)  FT_DEBUG_PRINT_W(OPENAMP_DEVICE_DEBUG_TAG, format, ##__VA_ARGS__)
#define OPENAMP_DEVICE_INFO(format, ...)  FT_DEBUG_PRINT_I(OPENAMP_DEVICE_DEBUG_TAG, format, ##__VA_ARGS__)
#define OPENAMP_DEVICE_DEBUG(format, ...) FT_DEBUG_PRINT_D(OPENAMP_DEVICE_DEBUG_TAG, format, ##__VA_ARGS__)

#define MAX_DATA_LENGTH       (RPMSG_BUFFER_SIZE / 2)

#define DEVICE_CORE_START     0x0001U /* 开始任务 */
#define DEVICE_CORE_SHUTDOWN  0x0002U /* 关闭核心 */
#define DEVICE_CORE_CHECK     0x0003U /* 检查消息 */
#define DEVICE_SENSOR_DATA    0x0010U /* 传感器数据(逐个,旧) */
#define DEVICE_SENSOR_BATCH   0x0011U /* 传感器批量(合并优化) */
#define DEVICE_MASTER_DATA    0x0020U /* 主控数据: Linux→FreeRTOS (LoRa帧转发) */
#define DEVICE_MASTER_CMD     0x0021U /* 主控命令: FreeRTOS→Linux (指令转发) */
#define SENSOR_PACKET_COUNT   10      /* 每次发送的传感器数据包数量 */

/* 边缘异常检测阈值 */
#define THR_VOLTAGE_MIN       210.0f  /* 电压下限(WARN) */
#define THR_VOLTAGE_MAX       230.0f  /* 电压上限(WARN) */
#define THR_CURRENT_MIN       0.5f    /* 电流下限(WARN) */
#define THR_CURRENT_MAX       2.5f    /* 电流上限(WARN) */
#define THR_TEMP_WARN         35.0f   /* 温度预警 */
#define THR_TEMP_ERROR        50.0f   /* 温度异常 */

/* External functions */
extern int init_system();
extern void master_recv_inject_data(const uint8_t *data, uint16_t len);
/************************** Variable Definitions *****************************/
static volatile int shutdown_req = 0;

/*******************例程全局变量***********************************************/
struct remoteproc remoteproc_device_00;
static struct rpmsg_device *rpdev_device_00 = NULL;

/* 协议数据结构 */
typedef struct {
    uint32_t command; /* 命令字，占4个字节 */
    uint16_t length;  /* 数据长度，占2个字节 */
    char data[MAX_DATA_LENGTH];       /* 数据内容，动态长度 */
} ProtocolData;

static ProtocolData protocol_data;

/* 传感器模拟数据结构 */
typedef struct {
    uint32_t sensor_id;    /* 传感器ID (1-10) */
    uint32_t timestamp;    /* 时间戳 (ms) */
    float voltage;         /* 电压值 (V) */
    float current;         /* 电流值 (A) */
    float temperature;     /* 温度值 (°C) */
    uint8_t status;        /* 状态: 0=正常, 1=预警, 2=异常 */
} SensorPacket;

/* A2零拷贝: 预分配发送缓冲区, 融合协议头+数据, 消除memcpy */
typedef struct __attribute__((packed)) {
    uint32_t command;                          /* DEVICE_SENSOR_BATCH */
    uint16_t length;                           /* sizeof(SensorPacket) * count */
    SensorPacket packets[SENSOR_PACKET_COUNT]; /* 数据直接在发送缓冲区 */
} ZeroCopyBatch;

static ZeroCopyBatch g_zc_batch;  /* A2: 全局零拷贝缓冲区 */
static int g_kick_count = 0;      /* A3: SGI9中断计数 */

/* A2: 10组传感器数据直接写入零拷贝缓冲区 */
static void init_sensor_data(void) {
    float init_data[10][3] = {
        {220.5, 1.25, 27.3}, {221.0, 1.30, 28.1}, {219.8, 1.18, 29.5},
        {222.1, 1.42, 30.2}, {218.5, 1.10, 31.0}, {220.0, 1.22, 32.1},
        {221.5, 1.35, 33.4}, {219.2, 1.15, 34.0}, {220.8, 1.28, 34.8},
        {221.2, 1.32, 35.2}
    };
    for (int i = 0; i < SENSOR_PACKET_COUNT; i++) {
        g_zc_batch.packets[i].sensor_id = i + 1;
        g_zc_batch.packets[i].voltage = init_data[i][0];
        g_zc_batch.packets[i].current = init_data[i][1];
        g_zc_batch.packets[i].temperature = init_data[i][2];
        g_zc_batch.packets[i].status = (i == 3) ? 1 : ((i == 6) ? 2 : 0);
    }
}

static int sensor_data_send_count = 0;
static struct rpmsg_endpoint *g_ept = NULL;
static void *g_remoteproc_priv = NULL;
static int g_edge_normal_count = 0;
static int g_edge_alarm_count = 0;

/* A2+C2: 边缘检测直接操作零拷贝缓冲区 */
static int edge_detect_anomaly(void)
{
    int alarms = 0;
    for (int i = 0; i < SENSOR_PACKET_COUNT; i++) {
        SensorPacket *p = &g_zc_batch.packets[i];
        if (p->voltage < THR_VOLTAGE_MIN || p->voltage > THR_VOLTAGE_MAX ||
            p->current < THR_CURRENT_MIN || p->current > THR_CURRENT_MAX ||
            p->temperature > THR_TEMP_ERROR) {
            p->status = 2;
        } else if (p->temperature > THR_TEMP_WARN ||
                   p->voltage > 228.0f || p->voltage < 212.0f) {
            p->status = 1;
        }
        if (p->status > 0) alarms++;
    }
    g_edge_alarm_count += alarms;
    g_edge_normal_count += (SENSOR_PACKET_COUNT - alarms);
    return alarms;
}

/************************** 资源表定义，与linux协商一致 **********/
static struct remote_resource_table __resource resources __attribute__((used)) = {
	/* Version */
	1,

	/* NUmber of table entries */
	NUM_TABLE_ENTRIES,
	/* reserved fields */
	{0, 0,},

	/* Offsets of rsc entries */
	{
	 offsetof(struct remote_resource_table, rpmsg_vdev),
	},

	/* Virtio device entry */
	{
	 RSC_VDEV, VIRTIO_ID_RPMSG_, VDEV_NOTIFYID, RPMSG_IPU_C0_FEATURES, 0, 0, 0,
	 NUM_VRINGS, {0, 0},
	},
    
	/* Vring rsc entry - part of vdev rsc entry */
	{DEVICE00_TX_VRING_ADDR, VRING_ALIGN, DEVICE00_VRING_NUM, 1, 0},
	{DEVICE00_RX_VRING_ADDR, VRING_ALIGN, DEVICE00_VRING_NUM, 2, 0},
};

/********** 共享内存定义，与linux协商一致 **********/
static metal_phys_addr_t poll_phys_addr = DEVICE00_KICK_IO_ADDR;
struct metal_device kick_driver_00 = {
    .name = DEVICE_00_KICK_DEV_NAME,
	.bus = NULL,
    .num_regions = 1,
	.regions = {
		{
			.virt = (void *)DEVICE00_KICK_IO_ADDR,
			.physmap = &poll_phys_addr,
			.size = 0x1000,
			.page_shift = -1UL,
			.page_mask = -1UL,
			.mem_flags = DEVICE00_SOURCE_TABLE_ATTRIBUTE,
			.ops = {NULL},
		}
	},
    .irq_num = 1,/* Number of IRQs per device */
	.irq_info = (void *)DEVICE_00_SGI,
} ;

struct remoteproc_priv device_00_priv = {
    .kick_dev_name =           DEVICE_00_KICK_DEV_NAME  ,
	.kick_dev_bus_name =        KICK_BUS_NAME ,
    .cpu_id        =  DRIVER_CORE_MASK,/* 给所有core发送中断 */

	.src_table_attribute = DEVICE00_SOURCE_TABLE_ATTRIBUTE ,
	
	/* |rx vring|tx vring|share buffer| */
	.share_mem_va = DEVICE00_SHARE_MEM_ADDR ,
	.share_mem_pa = DEVICE00_SHARE_MEM_ADDR ,
	.share_buffer_offset = DEVICE00_VRING_SIZE ,
	.share_mem_size = DEVICE00_SHARE_MEM_SIZE ,
	.share_mem_attribute = DEVICE00_SHARE_BUFFER_ATTRIBUTE
} ;

/************************** Function Prototypes ******************************/
/*协议解析接口*/
int parse_protocol_data(const char* input, size_t input_size, ProtocolData* output) 
{

    if (input_size < 6) { /* 确保最小长度（命令字+数据长度）*/
        return -1; /* 数据太短 */
    }

    /* 提取命令字 */
    output->command = *((uint32_t*)input);
    input += 4;

    /* 提取数据长度 */
    output->length = *((uint16_t*)input);
    input += 2;

    /* 检查数据长度是否超出预定义最大长度 */
    if (output->length > MAX_DATA_LENGTH) {
        return -2; // 数据长度超出限制
    }

    /* 复制数据内容 */
    memcpy(output->data, input, output->length);

    return 0; /* 解析成功 */
}

/*协议组装接口*/
int assemble_protocol_data(const ProtocolData* input, char* output, size_t* output_size) 
{
    /* 检查预期的输出大小是否超出最大长度 */
    if (6 + input->length > MAX_DATA_LENGTH) {
        return -1; /* 数据长度超出限制 */
    }

    *output_size = 6 + input->length; /* 命令字+长度+数据 */

    /* 组装命令字 */
    *((uint32_t*)output) = input->command;

    /* 组装数据长度 */
    *((uint16_t*)(output + 4)) = input->length;

    /* 复制数据内容 */
    memcpy(output + 6, input->data, input->length);

    return 0; /* 组装成功 */
}

/* A1+A2+A3+A4整合: 零拷贝批量发送 + 中断计数 + vring调优 */
static int send_batch_zero_copy(struct rpmsg_endpoint *ept)
{
    int ret, alarms;

    /* 更新时间戳 (直接写入零拷贝缓冲区) */
    for (int i = 0; i < SENSOR_PACKET_COUNT; i++) {
        g_zc_batch.packets[i].timestamp = sensor_data_send_count * 1000 + i * 100;
    }

    /* C2: 边缘检测 (直接操作零拷贝缓冲区, 无memcpy) */
    alarms = edge_detect_anomaly();

    /* A2零拷贝: 协议头+数据已在g_zc_batch中, 直接发送, 0次memcpy */
    g_zc_batch.command = DEVICE_SENSOR_BATCH;
    g_zc_batch.length = sizeof(SensorPacket) * SENSOR_PACKET_COUNT;

    ret = rpmsg_send(ept, &g_zc_batch, 6 + g_zc_batch.length);
    g_kick_count++;  /* A3: 累计SGI9中断计数 */
    sensor_data_send_count++;

    /* 仅每50批打印一次, 避免串口泛滥 */
    if (g_kick_count % 50 == 0) {
        OPENAMP_DEVICE_INFO("FreeRTOS ZC: %d pkts/batch, kicks:%d, edge:%dN/%dA\r\n",
                            SENSOR_PACKET_COUNT, g_kick_count,
                            g_edge_normal_count, g_edge_alarm_count);
    }
    return ret;
}

/* 保留旧接口兼容性 */
static int send_sensor_packet(struct rpmsg_endpoint *ept, SensorPacket *pkt)
{
    ProtocolData tx_data;
    tx_data.command = DEVICE_SENSOR_DATA;
    tx_data.length = sizeof(SensorPacket);
    memcpy(tx_data.data, (char *)pkt, sizeof(SensorPacket));
    return rpmsg_send(ept, &tx_data, 6 + sizeof(SensorPacket));
}

static int send_all_sensor_packets(struct rpmsg_endpoint *ept)
{
    return send_batch_zero_copy(ept);
}

/*
 * rpmsg_send_master_cmd: FreeRTOS→Linux 主控指令转发
 *
 * 原GD32通过LoRa模块直接发送指令到终端节点。
 * 移植后架构:
 *   FreeRTOS → RPMsg → Linux → LoRa模块 → 终端节点
 *
 * RPMsg消息格式 (与Linux侧约定):
 *   [4B command=DEVICE_MASTER_CMD][2B length][1B node_id][1B cmd_code][nB params]
 */
int rpmsg_send_master_cmd(uint8_t node_id, uint8_t cmd_code,
                           const uint8_t *params, uint8_t param_len)
{
    if (!g_ept) {
        OPENAMP_DEVICE_WARN("rpmsg endpoint not ready for master cmd\r\n");
        return -1;
    }

    ProtocolData tx_data;
    tx_data.command = DEVICE_MASTER_CMD;
    tx_data.length = 2 + param_len;
    tx_data.data[0] = (char)node_id;
    tx_data.data[1] = (char)cmd_code;
    if (params && param_len > 0 && param_len <= (MAX_DATA_LENGTH - 2)) {
        memcpy(&tx_data.data[2], params, param_len);
    }

    int ret = rpmsg_send(g_ept, &tx_data, 6 + tx_data.length);
    if (ret < 0) {
        OPENAMP_DEVICE_ERROR("rpmsg_send_master_cmd failed: %d\r\n", ret);
    }
    return ret;
}

/*-----------------------------------------------------------------------------*
 *  RPMSG endpoint callbacks
 *-----------------------------------------------------------------------------*/
static int rpmsg_endpoint_cb(struct rpmsg_endpoint *ept, void *data, size_t len, uint32_t src, void *priv)
{
	(void)priv;
    (void)src;

    int ret;
    (void)priv;
    OPENAMP_DEVICE_INFO("src:0x%x",src);
    ept->dest_addr = src;

    ret = parse_protocol_data((char *)data, len, &protocol_data);
    if(ret != 0)
    {
        OPENAMP_DEVICE_ERROR("parse protocol data error,ret:%d",ret);
        return RPMSG_SUCCESS;/* 解析失败，忽略数据 */
    }
    OPENAMP_DEVICE_INFO("command:0x%x,length:%d.",protocol_data.command,protocol_data.length);
    switch (protocol_data.command)
    {
        case DEVICE_CORE_START:
        {
            break;
        }
        case DEVICE_CORE_SHUTDOWN:
        {
            shutdown_req = 1;
            break;
        }
        case DEVICE_CORE_CHECK:
        {
            /* Send temp_data back to driver */
            /* 请勿直接对data指针对应的内存进行写操作，操作vring中remoteproc发送通道分配的内存，引发错误的问题*/
            ret = rpmsg_send(ept, &protocol_data, len);
            if (ret < 0)
            {
                OPENAMP_DEVICE_ERROR("rpmsg_send failed.\r\n");
                return ret;
            }
            break;
        }
        case DEVICE_SENSOR_DATA:
        {
            /* Linux请求传感器数据，FreeRTOS发送10组模拟数据包 */
            g_ept = ept;
            send_all_sensor_packets(ept);
            break;
        }
        case DEVICE_MASTER_DATA:
        {
            /*
             * Linux侧通过RPMsg转发LoRa原始帧到FreeRTOS。
             * 数据格式: [2B length][nB lora_frame]
             * 注入到 master_recv 处理管线进行帧解析和混沌解密。
             */
            g_ept = ept;

            /* 测试：收到握手帧时立即回发一个测试DEVICE_MASTER_CMD */
            {
                ProtocolData test = {.command = DEVICE_MASTER_CMD,
                                     .length = 2,
                                     .data = {0x00, 0xFF}};
                int tr = rpmsg_send(ept, &test, 6 + test.length);
                OPENAMP_DEVICE_INFO("handshake echo test: ret=%d\r\n", tr);
            }

            if (protocol_data.length > 0 && protocol_data.length <= MAX_DATA_LENGTH) {
                master_recv_inject_data((const uint8_t *)protocol_data.data,
                                        protocol_data.length);
            }
            break;
        }
        default:
            break;
    }

    return RPMSG_SUCCESS;
}

static void rpmsg_service_unbind(struct rpmsg_endpoint *ept)
{
	(void)ept;
	OPENAMP_DEVICE_INFO("unexpected Remote endpoint destroy.");
	shutdown_req = 1;
}

/*-----------------------------------------------------------------------------*
 *  Application
 *-----------------------------------------------------------------------------*/
static int FRpmsgEchoApp(struct rpmsg_device *rdev, void *priv)
{
    int ret = 0;
    struct rpmsg_endpoint lept = {0};
    shutdown_req = 0;
    /* Initialize RPMSG framework */
    OPENAMP_DEVICE_INFO("Try to create rpmsg endpoint.\r\n");

    ret = rpmsg_create_ept(&lept, rdev, RPMSG_SERVICE_NAME, 0, RPMSG_ADDR_ANY, rpmsg_endpoint_cb, rpmsg_service_unbind);
    if (ret)
    {
        OPENAMP_DEVICE_ERROR("Failed to create endpoint. %d \r\n", ret);
        return -1;
    }

    g_remoteproc_priv = priv;
    g_ept = &lept;
    OPENAMP_DEVICE_INFO("Successfully created rpmsg endpoint.\r\n");

    while (1)
    {
        platform_poll(priv);
        /* we got a shutdown request, exit */
        if (shutdown_req || rproc_get_stop_flag())
        {
	        rproc_clear_stop_flag();
            break;
        }
    }

    rpmsg_destroy_ept(&lept);

    return ret;
}


/*-----------------------------------------------------------------------------*
 *  Application entry point
 *-----------------------------------------------------------------------------*/
int device_init(void)
{
    init_system();  // Initialize the system resources and environment
    
    if (!platform_create_proc(&remoteproc_device_00, &device_00_priv, &kick_driver_00)) 
    {
        OPENAMP_DEVICE_ERROR("Failed to create remoteproc instance for device 00\r\n");
        return -1;  // Return with an error if creation fails
    }
    
    remoteproc_device_00.rsc_table = &resources;

    if (platform_setup_src_table(&remoteproc_device_00,remoteproc_device_00.rsc_table)) 
    {
        OPENAMP_DEVICE_ERROR("Failed to setup src table for device 00\r\n");
        return -1;  // Return with an error if setup fails
    }
    
    OPENAMP_DEVICE_INFO("Setup resource tables for the created remoteproc instances is over \r\n");

    if (platform_setup_share_mems(&remoteproc_device_00)) 
    {
        OPENAMP_DEVICE_ERROR("Failed to setup shared memory for device 00\r\n");
        return -1;  // Return with an error if setup fails
    }

    OPENAMP_DEVICE_INFO("Setup shared memory regions for both remoteproc instances is over \r\n");

    rpdev_device_00 = platform_create_rpmsg_vdev(&remoteproc_device_00, 0, VIRTIO_DEV_DEVICE, NULL, NULL);
    if (!rpdev_device_00) 
    {
        OPENAMP_DEVICE_ERROR("Failed to create rpmsg vdev for device 00\r\n");
        return -1;  // Return with an error if creation fails
    }

    return 0 ;   
} 

int RpmsgEchoTask(void * args)
{
	int ret;
	printf("openamp lib version: %s (", openamp_version());
	printf("Major: %d, ", openamp_version_major());
	printf("Minor: %d, ", openamp_version_minor());
	printf("Patch: %d)\r\n", openamp_version_patch());

	printf("libmetal lib version: %s (", metal_ver());
	printf("Major: %d, ", metal_ver_major());
	printf("Minor: %d, ", metal_ver_minor());
	printf("Patch: %d)\r\n", metal_ver_patch());

	/* Initialize platform */
	OPENAMP_DEVICE_INFO("start application...");
	if(!device_init())
    {
        init_sensor_data();
        FRpmsgEchoApp(rpdev_device_00,&remoteproc_device_00) ;
        if (ret)
        {
            OPENAMP_DEVICE_ERROR("Failed to running echoapp");
            platform_cleanup(&remoteproc_device_00);
        }
        platform_release_rpmsg_vdev(rpdev_device_00, &remoteproc_device_00);
        OPENAMP_DEVICE_INFO("Stopping application...");
        platform_cleanup(&remoteproc_device_00);
    }
    else
    {
        platform_cleanup(&remoteproc_device_00);
        OPENAMP_DEVICE_ERROR("Failed to init remoteproc.\r\n");
    }
    FPsciCpuOff();
}

int rpmsg_echo_task(void)
{
    BaseType_t ret; 

    ret = xTaskCreate((TaskFunction_t )RpmsgEchoTask, /* 任务入口函数 */
                        (const char* )"RpmsgEchoTask",/* 任务名字 */
                        (4096*2), /* 任务栈大小 */
                        (void* )NULL,/* 任务入口函数参数 */
                        (UBaseType_t )4, /* 任务的优先级 */
                        NULL); /* 任务控制块指针 */
    
    if(ret != pdPASS)
    {
        OPENAMP_DEVICE_ERROR("Failed to create a rpmsg_echo task. \r\n");
        return -1;
    }
    return 0;
}
