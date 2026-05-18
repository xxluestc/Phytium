/**
 ****************************************************************************************************
 * @file        atk_mwcc68d.c
 * @author      ALIENTEK
 * @version     V1.0
 * @date        2024-01-28
 * @brief       ATK-MWCC68Dģ���������� - GD32L233��ֲ��
 ****************************************************************************************************
 */

#include "atk_mwcc68d.h"
#include "systick.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief       ATK-MWCC68Dģ��Ӳ����ʼ��
 * @param       ��
 * @retval      ��
 */
static void atk_mwcc68d_hw_init(void)
{
    /* Enable clocks */
    ATK_MWCC68D_AUX_GPIO_CLK_ENABLE();
    ATK_MWCC68D_MD0_GPIO_CLK_ENABLE();
    
    /* AUX pin - Input mode with pull-up */
    gpio_mode_set(ATK_MWCC68D_AUX_GPIO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, ATK_MWCC68D_AUX_GPIO_PIN);
    
    /* MD0 pin - Output mode push-pull with pull-down */
    gpio_mode_set(ATK_MWCC68D_MD0_GPIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLDOWN, ATK_MWCC68D_MD0_GPIO_PIN);
    gpio_output_options_set(ATK_MWCC68D_MD0_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, ATK_MWCC68D_MD0_GPIO_PIN);
    
    ATK_MWCC68D_MD0(0);
}

/**
 * @brief       ATK-MWCC68Dģ���ʼ��
 * @param       baudrate: ATK-MWCC68Dģ��UARTͨѶ������
 * @retval      ATK_MWCC68D_EOK  : ATK-MWCC68Dģ���ʼ���ɹ�
 *              ATK_MWCC68D_ERROR: ATK-MWCC68Dģ���ʼ��ʧ��
 */
uint8_t atk_mwcc68d_init(uint32_t baudrate)
{
    uint8_t ret;
    
    atk_mwcc68d_hw_init();                          /* Hardware initialization */
    atk_mwcc68d_uart_init(baudrate);                /* UART initialization */
    atk_mwcc68d_enter_config();                     /* Enter configuration mode */
    ret = atk_mwcc68d_at_test();                    /* AT command test */
    atk_mwcc68d_exit_config();                      /* Exit configuration mode */
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ATK-MWCC68Dģ���������ģʽ
 * @param       ��
 * @retval      ��
 */
void atk_mwcc68d_enter_config(void)
{
    ATK_MWCC68D_MD0(1);
}

/**
 * @brief       ATK-MWCC68Dģ���˳�����ģʽ
 * @param       ��
 * @retval      ��
 */
void atk_mwcc68d_exit_config(void)
{
    ATK_MWCC68D_MD0(0);
}

/**
 * @brief       �ж�ATK-MWCC68Dģ���Ƿ����
 * @note        Only when ATK-MWCC68D module is free can data be sent
 * @param       ��
 * @retval      ATK_MWCC68D_EOK  : ATK-MWCC68Dģ�����
 *              ATK_MWCC68D_EBUSY: ATK-MWCC68Dģ��æ
 */
uint8_t atk_mwcc68d_free(void)
{
    if (ATK_MWCC68D_AUX() != 0)
    {
        return ATK_MWCC68D_EBUSY;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ��ATK-MWCC68Dģ�鷢��ATָ��
 * @param       cmd    : AT command to send
 * @param       ack    : Expected response
 * @param       timeout: Wait timeout in ms
 * @retval      ATK_MWCC68D_EOK     : Success
 *              ATK_MWCC68D_ETIMEOUT: Timeout
 */
uint8_t atk_mwcc68d_send_at_cmd(char *cmd, char *ack, uint32_t timeout)
{
    uint8_t *ret = NULL;
    
    atk_mwcc68d_uart_rx_restart();
    atk_mwcc68d_uart_printf("%s\r\n", cmd);
    
    if ((ack == NULL) || (timeout == 0))
    {
        return ATK_MWCC68D_EOK;
    }
    else
    {
        while (timeout > 0)
        {
            ret = atk_mwcc68d_uart_rx_get_frame();
            if (ret != NULL)
            {
                if (strstr((const char *)ret, ack) != NULL)
                {
                    return ATK_MWCC68D_EOK;
                }
                else
                {
                    atk_mwcc68d_uart_rx_restart();
                }
            }
            timeout--;
            cpu_delay_ms(1);
        }
        
        return ATK_MWCC68D_ETIMEOUT;
    }
}

/**
 * @brief       ATK-MWCC68Dģ��ATָ�����
 * @param       ��
 * @retval      ATK_MWCC68D_EOK  : ATָ����Գɹ�
 *              ATK_MWCC68D_ERROR: ATָ�����ʧ��
 */
uint8_t atk_mwcc68d_at_test(void)
{
    uint8_t ret;
    uint8_t i;
    
    for (i=0; i<10; i++)
    {
        ret = atk_mwcc68d_send_at_cmd("AT", "OK", ATK_MWCC68D_AT_TIMEOUT);
        if (ret == ATK_MWCC68D_EOK)
        {
            return ATK_MWCC68D_EOK;
        }
    }
    
    return ATK_MWCC68D_ERROR;
}

/**
 * @brief       ATK-MWCC68Dģ��ָ���������
 * @param       enable: ATK_MWCC68D_DISABLE: Disable echo
 *                      ATK_MWCC68D_ENABLE : Enable echo
 * @retval      ATK_MWCC68D_EOK   : Success
 *              ATK_MWCC68D_ERROR : Failed
 *              ATK_MWCC68D_EINVAL: Invalid parameter
 */
uint8_t atk_mwcc68d_echo_config(atk_mwcc68d_enable_t enable)
{
    uint8_t ret;
    char cmd[5] = {0};
    
    switch (enable)
    {
        case ATK_MWCC68D_ENABLE:
        {
            sprintf(cmd, "ATE1");
            break;
        }
        case ATK_MWCC68D_DISABLE:
        {
            sprintf(cmd, "ATE0");
            break;
        }
        default:
        {
            return ATK_MWCC68D_EINVAL;
        }
    }
    
    ret = atk_mwcc68d_send_at_cmd(cmd, "OK", ATK_MWCC68D_AT_TIMEOUT);
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ATK-MWCC68Dģ��������λ
 * @param       ��
 * @retval      ATK_MWCC68D_EOK  : Success
 *              ATK_MWCC68D_ERROR: Failed
 */
uint8_t atk_mwcc68d_sw_reset(void)
{
    uint8_t ret;
    
    ret = atk_mwcc68d_send_at_cmd("AT+RESET", "OK", ATK_MWCC68D_AT_TIMEOUT);
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ATK-MWCC68Dģ�������������
 * @param       enable: ATK_MWCC68D_DISABLE: Don't save
 *                      ATK_MWCC68D_ENABLE : Save parameters
 * @retval      ATK_MWCC68D_EOK   : Success
 *              ATK_MWCC68D_ERROR : Failed
 *              ATK_MWCC68D_EINVAL: Invalid parameter
 */
uint8_t atk_mwcc68d_flash_config(atk_mwcc68d_enable_t enable)
{
    uint8_t ret;
    char cmd[11] = {0};
    
    switch (enable)
    {
        case ATK_MWCC68D_DISABLE:
        {
            sprintf(cmd, "AT+FLASH=0");
            break;
        }
        case ATK_MWCC68D_ENABLE:
        {
            sprintf(cmd, "AT+FLASH=1");
            break;
        }
        default:
        {
            return ATK_MWCC68D_EINVAL;
        }
    }
    
    ret = atk_mwcc68d_send_at_cmd(cmd, "OK", ATK_MWCC68D_AT_TIMEOUT);
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ATK-MWCC68Dģ��ָ���������
 * @param       ��
 * @retval      ATK_MWCC68D_EOK   : Success
 *              ATK_MWCC68D_ERROR : Failed
 */
uint8_t atk_mwcc68d_default(void)
{
    uint8_t ret;
    
    ret = atk_mwcc68d_send_at_cmd("AT+DEFAULT", "OK", ATK_MWCC68D_AT_TIMEOUT);
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ATK-MWCC68Dģ���豸��ַ����
 * @param       addr: Device address
 * @retval      ATK_MWCC68D_EOK   : Success
 *              ATK_MWCC68D_ERROR : Failed
 */
uint8_t atk_mwcc68d_addr_config(uint16_t addr)
{
    uint8_t ret;
    char cmd[14] = {0};
    
    sprintf(cmd, "AT+ADDR=%02X,%02X", (uint8_t)(addr >> 8) & 0xFF, (uint8_t)addr & 0xFF);
    
    ret = atk_mwcc68d_send_at_cmd(cmd, "OK", ATK_MWCC68D_AT_TIMEOUT);
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ATK-MWCC68Dģ�鷢�书������
 * @param       tpower: Transmission power level
 * @retval      ATK_MWCC68D_EOK   : Success
 *              ATK_MWCC68D_ERROR : Failed
 *              ATK_MWCC68D_EINVAL: Invalid parameter
 */
uint8_t atk_mwcc68d_tpower_config(atk_mwcc68d_tpower_t tpower)
{
    uint8_t ret;
    char cmd[12] = {0};
    
    switch (tpower)
    {
        case ATK_MWCC68D_TPOWER_9DBM:
        case ATK_MWCC68D_TPOWER_11DBM:
        case ATK_MWCC68D_TPOWER_14DBM:
        case ATK_MWCC68D_TPOWER_17DBM:
        case ATK_MWCC68D_TPOWER_20DBM:
        case ATK_MWCC68D_TPOWER_22DBM:
        {
            break;
        }
        default:
        {
            return ATK_MWCC68D_EINVAL;
        }
    }
    
    sprintf(cmd, "AT+TPOWER=%d", tpower);
    
    ret = atk_mwcc68d_send_at_cmd(cmd, "OK", ATK_MWCC68D_AT_TIMEOUT);
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ATK-MWCC68Dģ�鹤��ģʽ����
 * @param       workmode: Operating mode
 * @retval      ATK_MWCC68D_EOK   : Success
 *              ATK_MWCC68D_ERROR : Failed
 *              ATK_MWCC68D_EINVAL: Invalid parameter
 */
uint8_t atk_mwcc68d_workmode_config(atk_mwcc68d_workmode_t workmode)
{
    uint8_t ret;
    char cmd[12] = {0};
    
    switch (workmode)
    {
        case ATK_MWCC68D_WORKMODE_NORMAL:
        case ATK_MWCC68D_WORKMODE_WAKEUP:
        case ATK_MWCC68D_WORKMODE_LOWPOWER:
        case ATK_MWCC68D_WORKMODE_SIGNAL:
        case ATK_MWCC68D_WORKMODE_SLEEP:
        case ATK_MWCC68D_WORKMODE_RELAY:
        {
            break;
        }
        default:
        {
            return ATK_MWCC68D_EINVAL;
        }
    }
    
    sprintf(cmd, "AT+CWMODE=%d", workmode);
    
    ret = atk_mwcc68d_send_at_cmd(cmd, "OK", ATK_MWCC68D_AT_TIMEOUT);
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ATK-MWCC68Dģ�鷢��ģʽ����
 * @param       tmode: Transmission mode
 * @retval      ATK_MWCC68D_EOK   : Success
 *              ATK_MWCC68D_ERROR : Failed
 *              ATK_MWCC68D_EINVAL: Invalid parameter
 */
uint8_t atk_mwcc68d_tmode_config(atk_mwcc68d_tmode_t tmode)
{
    uint8_t ret;
    char cmd[11] = {0};
    
    switch (tmode)
    {
        case ATK_MWCC68D_TMODE_TT:
        case ATK_MWCC68D_TMODE_DT:
        {
            break;
        }
        default:
        {
            return ATK_MWCC68D_EINVAL;
        }
    }
    
    sprintf(cmd, "AT+TMODE=%d", tmode);
    
    ret = atk_mwcc68d_send_at_cmd(cmd, "OK", ATK_MWCC68D_AT_TIMEOUT);
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ATK-MWCC68Dģ��������ʺ��ŵ�����
 * @param       wlrate : Wireless data rate
 * @param       channel: Channel number (0~83)
 * @retval      ATK_MWCC68D_EOK   : Success
 *              ATK_MWCC68D_ERROR : Failed
 *              ATK_MWCC68D_EINVAL: Invalid parameter
 */
uint8_t atk_mwcc68d_wlrate_channel_config(atk_mwcc68d_wlrate_t wlrate, uint8_t channel)
{
    uint8_t ret;
    char cmd[15] = {0};
    
    switch (wlrate)
    {
        case ATK_MWCC68D_WLRATE_1K2:
        case ATK_MWCC68D_WLRATE_2K4:
        case ATK_MWCC68D_WLRATE_4K8:
        case ATK_MWCC68D_WLRATE_9K6:
        case ATK_MWCC68D_WLRATE_19K2:
        case ATK_MWCC68D_WLRATE_38K4:
        case ATK_MWCC68D_WLRATE_62K5:
        {
            break;
        }
        default:
        {
            return ATK_MWCC68D_EINVAL;
        }
    }
    
    if (channel > 83)
    {
        return ATK_MWCC68D_EINVAL;
    }
    
    sprintf(cmd, "AT+WLRATE=%d,%d", channel, wlrate);
    
    ret = atk_mwcc68d_send_at_cmd(cmd, "OK", ATK_MWCC68D_AT_TIMEOUT);
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ATK-MWCC68Dģ�������ַ����
 * @param       netid: Network ID (0~255)
 * @retval      ATK_MWCC68D_EOK   : Success
 *              ATK_MWCC68D_ERROR : Failed
 */
uint8_t atk_mwcc68d_netid_config(uint8_t netid)
{
    uint8_t ret;
    char cmd[13] = {0};
    
    sprintf(cmd, "AT+NETID=%d", netid);
    
    ret = atk_mwcc68d_send_at_cmd(cmd, "OK", ATK_MWCC68D_AT_TIMEOUT);
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ATK-MWCC68Dģ������ʱ������
 * @param       wltime: Sleep time
 * @retval      ATK_MWCC68D_EOK   : Success
 *              ATK_MWCC68D_ERROR : Failed
 *              ATK_MWCC68D_EINVAL: Invalid parameter
 */
uint8_t atk_mwcc68d_wltime_config(atk_mwcc68d_wltime_t wltime)
{
    uint8_t ret;
    char cmd[12] = {0};
    
    switch (wltime)
    {
        case ATK_MWCC68D_WLTIME_1S:
        case ATK_MWCC68D_WLTIME_2S:
        {
            break;
        }
        default:
        {
            return ATK_MWCC68D_EINVAL;
        }
    }
    
    sprintf(cmd, "AT+WLTIME=%d", wltime);
    
    ret = atk_mwcc68d_send_at_cmd(cmd, "OK", ATK_MWCC68D_AT_TIMEOUT);
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ATK-MWCC68Dģ�����ݰ���С����
 * @param       packsize: Packet size
 * @retval      ATK_MWCC68D_EOK   : Success
 *              ATK_MWCC68D_ERROR : Failed
 *              ATK_MWCC68D_EINVAL: Invalid parameter
 */
uint8_t atk_mwcc68d_packsize_config(atk_mwcc68d_packsize_t packsize)
{
    uint8_t ret;
    char cmd[14] = {0};
    
    switch (packsize)
    {
        case ATK_MWCC68D_PACKSIZE_32:
        case ATK_MWCC68D_PACKSIZE_64:
        case ATK_MWCC68D_PACKSIZE_128:
        case ATK_MWCC68D_PACKSIZE_240:
        {
            break;
        }
        default:
        {
            return ATK_MWCC68D_EINVAL;
        }
    }
    
    sprintf(cmd, "AT+PACKSIZE=%d", packsize);
    
    ret = atk_mwcc68d_send_at_cmd(cmd, "OK", ATK_MWCC68D_AT_TIMEOUT);
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ATK-MWCC68Dģ�����ݼ�����Կ����
 * @param       datakey: Encryption key (0~0xFFFFFFFF)
 * @retval      ATK_MWCC68D_EOK   : Success
 *              ATK_MWCC68D_ERROR : Failed
 *              ATK_MWCC68D_EINVAL: Invalid parameter
 */
uint8_t atk_mwcc68d_datakey_config(uint32_t datakey)
{
    uint8_t ret;
    char cmd[20] = {0};
    
    sprintf(cmd, "AT+DATAKEY=%08X", datakey);
    
    ret = atk_mwcc68d_send_at_cmd(cmd, "OK", ATK_MWCC68D_AT_TIMEOUT);
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ATK-MWCC68Dģ�鴮������
 * @param       baudrate: UART baud rate
 * @param       parity  : Parity bit
 * @retval      ATK_MWCC68D_EOK   : Success
 *              ATK_MWCC68D_ERROR : Failed
 *              ATK_MWCC68D_EINVAL: Invalid parameter
 */
uint8_t atk_mwcc68d_uart_config(atk_mwcc68d_uartrate_t baudrate, atk_mwcc68d_uartpari_t parity)
{
    uint8_t ret;
    char cmd[12] = {0};
    
    switch (baudrate)
    {
        case ATK_MWCC68D_UARTRATE_1200BPS:
        case ATK_MWCC68D_UARTRATE_2400BPS:
        case ATK_MWCC68D_UARTRATE_4800BPS:
        case ATK_MWCC68D_UARTRATE_9600BPS:
        case ATK_MWCC68D_UARTRATE_19200BPS:
        case ATK_MWCC68D_UARTRATE_38400BPS:
        case ATK_MWCC68D_UARTRATE_57600BPS:
        case ATK_MWCC68D_UARTRATE_115200BPS:
        {
            break;
        }
        default:
        {
            return ATK_MWCC68D_EINVAL;
        }
    }
    
    switch (parity)
    {
        case ATK_MWCC68D_UARTPARI_NONE:
        case ATK_MWCC68D_UARTPARI_EVEN:
        case ATK_MWCC68D_UARTPARI_ODD:
        {
            break;
        }
        default:
        {
            return ATK_MWCC68D_EINVAL;
        }
    }
    
    sprintf(cmd, "AT+UART=%d,%d", baudrate, parity);
    
    ret = atk_mwcc68d_send_at_cmd(cmd, "OK", ATK_MWCC68D_AT_TIMEOUT);
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       ATK-MWCC68Dģ���ŵ��������
 * @param       enable: ATK_MWCC68D_DISABLE: Disable LBT
 *                      ATK_MWCC68D_ENABLE : Enable LBT
 * @retval      ATK_MWCC68D_EOK   : Success
 *              ATK_MWCC68D_ERROR : Failed
 *              ATK_MWCC68D_EINVAL: Invalid parameter
 */
uint8_t atk_mwcc68d_lbt_config(atk_mwcc68d_enable_t enable)
{
    uint8_t ret;
    char cmd[9] = {0};
    
    switch (enable)
    {
        case ATK_MWCC68D_DISABLE:
        {
            sprintf(cmd, "AT+LBT=0");
            break;
        }
        case ATK_MWCC68D_ENABLE:
        {
            sprintf(cmd, "AT+LBT=1");
            break;
        }
        default:
        {
            return ATK_MWCC68D_EINVAL;
        }
    }
    
    ret = atk_mwcc68d_send_at_cmd(cmd, "OK", ATK_MWCC68D_AT_TIMEOUT);
    if (ret != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_ERROR;
    }
    
    return ATK_MWCC68D_EOK;
}

/**
 * @brief       Send data to ATK-MWCC68D module (transmit via LoRa wireless)
 * @param       buf: Data buffer to send
 * @param       len: Length of data to send
 * @retval      ATK_MWCC68D_EOK   : Success
 *              ATK_MWCC68D_ERROR : Failed
 *              ATK_MWCC68D_EBUSY : Module busy
 *              ATK_MWCC68D_EINVAL: Invalid parameter
 */
uint8_t atk_mwcc68d_send_data(uint8_t *buf, uint16_t len)
{
    uint16_t i;
    
    /* Parameter check */
    if ((buf == NULL) || (len == 0) || (len > ATK_MWCC68D_UART_TX_BUF_SIZE))
    {
        return ATK_MWCC68D_EINVAL;
    }
    
    /* Wait for module to be free */
    if (atk_mwcc68d_free() != ATK_MWCC68D_EOK)
    {
        return ATK_MWCC68D_EBUSY;
    }
    
    /* Send data to LoRa module via UART */
    for (i = 0; i < len; i++)
    {
        while (usart_flag_get(ATK_MWCC68D_UART_INTERFACE, USART_FLAG_TBE) == RESET);
        usart_data_transmit(ATK_MWCC68D_UART_INTERFACE, buf[i]);
    }
    
    /* Wait for transmission complete */
    while (usart_flag_get(ATK_MWCC68D_UART_INTERFACE, USART_FLAG_TC) == RESET);
    
    return ATK_MWCC68D_EOK;
}
