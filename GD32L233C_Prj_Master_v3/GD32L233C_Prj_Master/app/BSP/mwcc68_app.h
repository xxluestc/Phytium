#ifndef _MWCC68_APP_H
#define _MWCC68_APP_H

#include "gd32l23x.h"

#define MD0_GPIO_PORT                  GPIOB
#define MD0_GPIO_PIN                   GPIO_PIN_8
#define MD0_GPIO_CLK_ENABLE()          rcu_periph_clock_enable(RCU_GPIOB)

#define LORA_MD0(x)                    do{ x ? \
                                         gpio_bit_set(MD0_GPIO_PORT, MD0_GPIO_PIN) : \
                                         gpio_bit_reset(MD0_GPIO_PORT, MD0_GPIO_PIN); \
                                       }while(0)   

#define AUX_GPIO_PORT                  GPIOB
#define AUX_GPIO_PIN                   GPIO_PIN_9
#define AUX_GPIO_CLK_ENABLE()          rcu_periph_clock_enable(RCU_GPIOB)

#define LORA_AUX                       gpio_input_bit_get(AUX_GPIO_PORT, AUX_GPIO_PIN)

#define AUX_INT_IRQn                   EXTI5_9_IRQn
#define AUX_INT_IRQHandler             EXTI5_9_IRQHandler

#define ATK_MWCC68D_EOK                0    // 成功
#define ATK_MWCC68D_ETIMEOUT           1    // 超时
#define ATK_MWCC68D_EINVAL             2    // 参数无效

/*初始化函数*/
void User_GpioInit(void);                                      // GPIO初始化
void LoRa_Init(void);                                           // LoRa模块初始化

/*配置模式函数*/
void LoRa_EnterConfigMode(void);                                // 进入配置模式
void LoRa_ExitConfigMode(void);                                 // 退出配置模式

/*AT指令配置函数*/
uint8_t LoRa_SetAddr(uint16_t addr);                            // 设置地址
uint8_t LoRa_SetNetid(uint8_t netid);                           // 设置网络ID
uint8_t LoRa_SetChn(uint8_t chn);                               // 设置通道
uint8_t LoRa_SetPacksize(uint8_t packsize);                     // 设置数据包大小
uint8_t LoRa_SetWlrate(uint8_t wlrate);                         // 设置工作速率

/*发送函数*/
void LoRa_SendData(uint8_t* data, uint16_t len);                // 发送数据
void LoRa_SendData_Direct(uint8_t* data, uint16_t len,          // 直接发送数据（指定目标地址和通道）
                          uint16_t destAddr, uint8_t chn);
void LoRa_SendString(char* str);                                // 发送字符串

/*接收函数*/
uint16_t LoRa_Available(void);                                  // 接收缓冲区可用数据量
uint8_t LoRa_ReadByte(void);                                    // 读取单个字节
uint16_t LoRa_ReadBytes(uint8_t *data, uint16_t maxLen);        // 读取多个字节
void LoRa_ClearBuffer(void);                                    // 清空接收缓冲区
uint8_t LoRa_ReceiveData(uint8_t *buf, uint16_t *len, uint32_t timeout);  // 带超时的接收数据
void LoRa_ReceData(void);                                       // 轮询接收任务

/*数据处理函数*/
void Process_PC_Commands(void);                                 // 处理PC端串口命令
uint8_t CheckDataAbnormal(uint8_t* data, uint16_t len);         // 检查数据是否异常（含0xAA）
void ProcessDataAbnormal(void);                                 // 异常处理：切换到高速模式
void ProcessBackToNormal(void);                                 // 恢复正常模式

/*内部工具函数*/
uint8_t hexCharToValue(char c);                                 // 十六进制字符转数值
uint16_t parseHexString(uint8_t *input, uint16_t inputLen,      // 解析十六进制字符串
                        uint8_t *output, uint16_t outputMaxLen);
uint8_t CheckReplyOK(void);                                     // 检查AT指令回复OK

/*测试函数*/
void MWCC68_Test(void);                                         // 主测试入口

#endif
