#ifndef __ATK_MWCC68D_H
#define __ATK_MWCC68D_H

#include "gd32l23x.h"
#include "atk_mwcc68d_uart.h"

/* AT��Ӧ�ȴ���ʱʱ�䣨���룩 */
#define ATK_MWCC68D_AT_TIMEOUT  500

/* ���Ŷ��� - GD32L233���� */
#define ATK_MWCC68D_AUX_GPIO_PORT           GPIOA
#define ATK_MWCC68D_AUX_GPIO_PIN            GPIO_PIN_4
#define ATK_MWCC68D_AUX_GPIO_CLK_ENABLE()   rcu_periph_clock_enable(RCU_GPIOA)

#define ATK_MWCC68D_MD0_GPIO_PORT           GPIOC
#define ATK_MWCC68D_MD0_GPIO_PIN            GPIO_PIN_0
#define ATK_MWCC68D_MD0_GPIO_CLK_ENABLE()   rcu_periph_clock_enable(RCU_GPIOC)

/* IO���� */
#define ATK_MWCC68D_AUX()                   gpio_input_bit_get(ATK_MWCC68D_AUX_GPIO_PORT, ATK_MWCC68D_AUX_GPIO_PIN)
#define ATK_MWCC68D_MD0(x)                  do{ \
                                                if(x) { \
                                                    gpio_bit_set(ATK_MWCC68D_MD0_GPIO_PORT, ATK_MWCC68D_MD0_GPIO_PIN); \
                                                } else { \
                                                    gpio_bit_reset(ATK_MWCC68D_MD0_GPIO_PORT, ATK_MWCC68D_MD0_GPIO_PIN); \
                                                } \
                                            }while(0)

/* ʹ��ö�� */
typedef enum
{
    ATK_MWCC68D_DISABLE             = 0x00,
    ATK_MWCC68D_ENABLE,
} atk_mwcc68d_enable_t;

/* ���书��ö�� */
typedef enum
{
    ATK_MWCC68D_TPOWER_9DBM         = 0,   /* 9dBm */
    ATK_MWCC68D_TPOWER_11DBM        = 1,   /* 11dBm */
    ATK_MWCC68D_TPOWER_14DBM        = 2,   /* 14dBm */
    ATK_MWCC68D_TPOWER_17DBM        = 3,   /* 17dBm */
    ATK_MWCC68D_TPOWER_20DBM        = 4,   /* 20dBm��Ĭ�ϣ� */
    ATK_MWCC68D_TPOWER_22DBM        = 5,   /* 22dBm */
} atk_mwcc68d_tpower_t;

/* ����ģʽö�� */
typedef enum
{
    ATK_MWCC68D_WORKMODE_NORMAL     = 0,    /* һ��ģʽ��Ĭ�ϣ� */
    ATK_MWCC68D_WORKMODE_WAKEUP     = 1,    /* ����ģʽ */
    ATK_MWCC68D_WORKMODE_LOWPOWER   = 2,    /* ʡ��ģʽ */
    ATK_MWCC68D_WORKMODE_SIGNAL     = 3,    /* �ź�ǿ��ģʽ */
    ATK_MWCC68D_WORKMODE_SLEEP      = 4,    /* ˯��ģʽ */
    ATK_MWCC68D_WORKMODE_RELAY      = 5,    /* �м�ģʽ */
} atk_mwcc68d_workmode_t;

/* ����ģʽö�� */
typedef enum
{
    ATK_MWCC68D_TMODE_TT            = 0,    /* ͸�����䣨Ĭ�ϣ� */
    ATK_MWCC68D_TMODE_DT            = 1,    /* ������ */
} atk_mwcc68d_tmode_t;

/* ��������ö�� */
typedef enum
{
    ATK_MWCC68D_WLRATE_1K2          = 1,    /* 1.2Kbps */
    ATK_MWCC68D_WLRATE_2K4          = 2,    /* 2.4Kbps */
    ATK_MWCC68D_WLRATE_4K8          = 3,    /* 4.8Kbps */
    ATK_MWCC68D_WLRATE_9K6          = 4,    /* 9.6Kbps */
    ATK_MWCC68D_WLRATE_19K2         = 5,    /* 19.2Kbps��Ĭ�ϣ� */
    ATK_MWCC68D_WLRATE_38K4         = 6,    /* 38.4Kbps */
    ATK_MWCC68D_WLRATE_62K5         = 7,    /* 62.5Kbps */
} atk_mwcc68d_wlrate_t;

/* ����ʱ��ö�� */
typedef enum
{
    ATK_MWCC68D_WLTIME_1S           = 0,    /* 1�루Ĭ�ϣ� */
    ATK_MWCC68D_WLTIME_2S           = 1,    /* 2�� */
} atk_mwcc68d_wltime_t;

/* ���ݰ���Сö�� */
typedef enum
{
    ATK_MWCC68D_PACKSIZE_32         = 0,    /* 32�ֽ� */
    ATK_MWCC68D_PACKSIZE_64         = 1,    /* 64�ֽ� */
    ATK_MWCC68D_PACKSIZE_128        = 2,    /* 128�ֽ� */
    ATK_MWCC68D_PACKSIZE_240        = 3,    /* 240�ֽ� */
} atk_mwcc68d_packsize_t;

/* ����ͨ�Ų�����ö�� */
typedef enum
{
    ATK_MWCC68D_UARTRATE_1200BPS    = 0,    /* 1200bps */
    ATK_MWCC68D_UARTRATE_2400BPS    = 1,    /* 2400bps */
    ATK_MWCC68D_UARTRATE_4800BPS    = 2,    /* 4800bps */
    ATK_MWCC68D_UARTRATE_9600BPS    = 3,    /* 9600bps */
    ATK_MWCC68D_UARTRATE_19200BPS   = 4,    /* 19200bps */
    ATK_MWCC68D_UARTRATE_38400BPS   = 5,    /* 38400bps */
    ATK_MWCC68D_UARTRATE_57600BPS   = 6,    /* 57600bps */
    ATK_MWCC68D_UARTRATE_115200BPS  = 7,    /* 115200bps��Ĭ�ϣ� */
} atk_mwcc68d_uartrate_t;

/* ����ͨѶУ��λö�� */
typedef enum
{
    ATK_MWCC68D_UARTPARI_NONE       = 0,    /* ��У�飨Ĭ�ϣ� */
    ATK_MWCC68D_UARTPARI_EVEN       = 1,    /* żУ�� */
    ATK_MWCC68D_UARTPARI_ODD        = 2,    /* ��У�� */
} atk_mwcc68d_uartpari_t;

/* ������� */
#define ATK_MWCC68D_EOK             0       /* û�д��� */
#define ATK_MWCC68D_ERROR           1       /* ͨ�ô��� */
#define ATK_MWCC68D_ETIMEOUT        2       /* ��ʱ���� */
#define ATK_MWCC68D_EINVAL          3       /* �������� */
#define ATK_MWCC68D_EBUSY           4       /* æ���� */

/* �������� */
uint8_t atk_mwcc68d_init(uint32_t baudrate);                                                        /* ATK-MWCC68D��ʼ�� */
void atk_mwcc68d_enter_config(void);                                                                /* ATK-MWCC68Dģ���������ģʽ */
void atk_mwcc68d_exit_config(void);                                                                 /* ATK-MWCC68Dģ����˳���ģʽ */
uint8_t atk_mwcc68d_free(void);                                                                     /* �ж�ATK-MWCC68Dģ���Ƿ���� */
uint8_t atk_mwcc68d_send_at_cmd(char *cmd, char *ack, uint32_t timeout);                            /* ��ATK-MWCC68Dģ�鷢��ATָ�� */
uint8_t atk_mwcc68d_at_test(void);                                                                  /* ATK-MWCC68Dģ��ATָ����� */
uint8_t atk_mwcc68d_echo_config(atk_mwcc68d_enable_t enable);                                       /* ATK-MWCC68Dģ��ָ��������� */
uint8_t atk_mwcc68d_sw_reset(void);                                                                 /* ATK-MWCC68Dģ��������λ */
uint8_t atk_mwcc68d_flash_config(atk_mwcc68d_enable_t enable);                                      /* ATK-MWCC68Dģ������������� */
uint8_t atk_mwcc68d_default(void);                                                                  /* ATK-MWCC68Dģ��ָ��������� */
uint8_t atk_mwcc68d_addr_config(uint16_t addr);                                                     /* ATK-MWCC68Dģ���豸��ַ���� */
uint8_t atk_mwcc68d_tpower_config(atk_mwcc68d_tpower_t tpower);                                     /* ATK-MWCC68Dģ�鷢�书������ */
uint8_t atk_mwcc68d_workmode_config(atk_mwcc68d_workmode_t workmode);                               /* ATK-MWCC68Dģ�鹤��ģʽ���� */
uint8_t atk_mwcc68d_tmode_config(atk_mwcc68d_tmode_t tmode);                                        /* ATK-MWCC68Dģ�鷢��ģʽ���� */
uint8_t atk_mwcc68d_wlrate_channel_config(atk_mwcc68d_wlrate_t wlrate, uint8_t channel);            /* ATK-MWCC68Dģ��������ʺ��ŵ����� */
uint8_t atk_mwcc68d_netid_config(uint8_t netid);                                                    /* ATK-MWCC68Dģ�������ַ���� */
uint8_t atk_mwcc68d_wltime_config(atk_mwcc68d_wltime_t wltime);                                     /* ATK-MWCC68Dģ������ʱ������ */
uint8_t atk_mwcc68d_packsize_config(atk_mwcc68d_packsize_t packsize);                               /* ATK-MWCC68Dģ�����ݰ���С���� */
uint8_t atk_mwcc68d_datakey_config(uint32_t datakey);                                               /* ATK-MWCC68Dģ�����ݼ�����Կ���� */
uint8_t atk_mwcc68d_uart_config(atk_mwcc68d_uartrate_t baudrate, atk_mwcc68d_uartpari_t parity);    /* ATK-MWCC68Dģ�鴮������ */
uint8_t atk_mwcc68d_lbt_config(atk_mwcc68d_enable_t enable);                                        /* ATK-MWCC68Dģ���ŵ�������� */
uint8_t atk_mwcc68d_send_data(uint8_t *buf, uint16_t len);
#endif