#ifndef _MWCC68_CFG_H
#define _MWCC68_CFG_H

#include "gd32l23x.h"

typedef struct {
   uint16_t addr;    
   uint8_t chn;      
   uint8_t netid;    
   uint8_t power;    
   uint8_t wlrate;   
   uint8_t wltime;   
   uint8_t wmode;    
   uint8_t tmode;    
   uint8_t packsize; 
   uint8_t bps;      
   uint8_t parity;   
   uint8_t lbt;      
}_LoRa_CFG;

typedef enum {
    LORA_RATE_1K2_1=0,
    LORA_RATE_1K2_2,
    LORA_RATE_2K4,
    LORA_RATE_4K8,
    LORA_RATE_9K6,
    LORA_RATE_19K2,
    LORA_RATE_38K4,
    LORA_RATE_62K5
}_LORA_RATE;

typedef enum {
    LORA_WLTIME_1S=0,
    LORA_WLTIME_2S
}_LORA_WLTIME;

typedef enum {
    LORA_WMODE_GEN=0,
    LORA_WMODE_WAKE,
    LORA_WMODE_SAVEPOWER,
    LORA_WMODE_RSSI,
    LORA_WMODE_DEEPSLEEP,
    LORA_WMODE_RELAY
}_LORA_WMODE;

typedef enum {
    LORA_TPW_9DBM=0,
    LORA_TPW_11DBM,
    LORA_TPW_14DBM,
    LORA_TPW_17DBM,
    LORA_TPW_20DBM,
    LORA_TPW_22DBM
}_LORA_TPOWER;

typedef enum {
    LORA_TMODE_PT=0,
    LORA_TMODE_FP,
}_LORA_TMODE;

typedef enum {
    LORA_BRD_1200=0,
    LORA_BRD_2400,
    LORA_BRD_4800,
    LORA_BRD_9600,
    LORA_BRD_19200,
    LORA_BRD_38400,
    LORA_BRD_57600,
    LORA_BRD_115200
}_LORA_BRDRATE;

typedef enum {
    LORA_BRDVER_8N1=0,
    LORA_BRDVER_8E1,
    LORA_BRDVER_8O1
}_LORA_BRDVERIFT;

typedef enum {
    LORA_LBT_DISABLE=0,
    LORA_LBT_ENABLE
}_LORA_LBT;

typedef enum {
    LORA_PACKSIZE_32=0,
    LORA_PACKSIZE_64,
    LORA_PACKSIZE_128, 
    LORA_PACKSIZE_240
}_LORA_PACKSIZE;

#define LORA_ADDR      0x000B	//自己的地址
#define LORA_CHN       23		//自己的信道
#define LORA_NETID     0	//

#define DEST_ADDR      0x000A	//目标地址
#define LORA_TPOWER    LORA_TPW_20DBM
#define LORA_RATE      LORA_RATE_19K2
#define LORA_WLTIME    LORA_WLTIME_1S
#define LORA_WMODE     LORA_WMODE_GEN
#define LORA_TMODE     LORA_TMODE_FP
#define LORA_PACKSIZE  LORA_PACKSIZE_240
#define LORA_TTLBPS    LORA_BRD_115200
#define LORA_TTLPAR    LORA_BRDVER_8N1
#define LORA_LBT       LORA_LBT_DISABLE

typedef enum {
    LORA_CONFG_STA=0,   // 配置状态
    LORA_RX_STA,       // 接收状态
    LORA_TX_STA        // 发送状态
}_LORA_DEVICE_STA;

extern _LORA_DEVICE_STA Lora_Device_Sta;

typedef enum {
    LORA_INT_OFF=0,
    LORA_INT_REDGE,
    LORA_INT_FEDGE
}_LORA_INT_STA;

#endif