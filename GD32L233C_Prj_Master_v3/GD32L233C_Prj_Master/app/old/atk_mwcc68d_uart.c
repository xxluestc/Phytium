/**
 ****************************************************************************************************
 * @file        atk_mwcc68d_uart.c
 * @author      ALIENTEK
 * @version     V1.0
 * @date        2024-01-28
 * @brief       ATK-MWCC68D Module UART Interface Driver - GD32L233 Ported Version
 ****************************************************************************************************
 */

#include "atk_mwcc68d_uart.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static uint8_t g_uart_tx_buf[ATK_MWCC68D_UART_TX_BUF_SIZE]; /* ATK-MWCC68D UART transmit buffer */

/* UART receive frame structure */
typedef struct {
    uint8_t buf[ATK_MWCC68D_UART_RX_BUF_SIZE];              /* Frame data buffer */
    uint16_t len : 15;                                       /* Frame data length */
    uint16_t finsh : 1;                                      /* Frame receive complete flag */
} uart_rx_frame_t;

static uart_rx_frame_t g_uart_rx_frame = {0};               /* ATK-MWCC68D UART receive frame info structure */

/**
 * @brief       ATK-MWCC68D UART printf
 * @param       fmt: format output string
 * @retval      none
 */
void atk_mwcc68d_uart_printf(char *fmt, ...)
{
    va_list ap;
    uint16_t len;
    
    va_start(ap, fmt);
    vsprintf((char *)g_uart_tx_buf, fmt, ap);
    va_end(ap);
    
    len = strlen((const char *)g_uart_tx_buf);
    for(uint16_t i = 0; i < len; i++) {
        while(usart_flag_get(ATK_MWCC68D_UART_INTERFACE, USART_FLAG_TBE) == RESET);
        usart_data_transmit(ATK_MWCC68D_UART_INTERFACE, g_uart_tx_buf[i]);
    }
}

/**
 * @brief       ATK-MWCC68D UART restart receive data
 * @param       none
 * @retval      none
 */
void atk_mwcc68d_uart_rx_restart(void)
{
    g_uart_rx_frame.len = 0;
    g_uart_rx_frame.finsh = 0;
}

/**
 * @brief       Get a frame of data received by ATK-MWCC68D UART
 * @param       none
 * @retval      NULL: No frame received
 *              Other: Received frame data
 */
uint8_t *atk_mwcc68d_uart_rx_get_frame(void)
{
    if (g_uart_rx_frame.finsh == 1)
    {
        g_uart_rx_frame.buf[g_uart_rx_frame.len] = '\0';
        return g_uart_rx_frame.buf;
    }
    else
    {
        return NULL;
    }
}

/**
 * @brief       Get the length of a frame of data received by ATK-MWCC68D UART
 * @param       none
 * @retval      0   : No frame received
 *              Other: Length of received frame data
 */
uint16_t atk_mwcc68d_uart_rx_get_frame_len(void)
{
    if (g_uart_rx_frame.finsh == 1)
    {
        return g_uart_rx_frame.len;
    }
    else
    {
        return 0;
    }
}

/**
 * @brief       ATK-MWCC68D UART initialization
 * @param       baudrate: UART communication baud rate
 * @retval      none
 */
void atk_mwcc68d_uart_init(uint32_t baudrate)
{
    /* Enable clocks */
    ATK_MWCC68D_UART_TX_GPIO_CLK_ENABLE();
    ATK_MWCC68D_UART_RX_GPIO_CLK_ENABLE();
    ATK_MWCC68D_UART_CLK_ENABLE();
    ATK_MWCC68D_TIM_CLK_ENABLE();

    /* Configure TX pin as alternate function push-pull */
    gpio_mode_set(ATK_MWCC68D_UART_TX_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, ATK_MWCC68D_UART_TX_GPIO_PIN);
    gpio_output_options_set(ATK_MWCC68D_UART_TX_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, ATK_MWCC68D_UART_TX_GPIO_PIN);
    gpio_af_set(ATK_MWCC68D_UART_TX_GPIO_PORT, GPIO_AF_7, ATK_MWCC68D_UART_TX_GPIO_PIN);

    /* Configure RX pin as alternate function input floating */
    gpio_mode_set(ATK_MWCC68D_UART_RX_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, ATK_MWCC68D_UART_RX_GPIO_PIN);
    gpio_af_set(ATK_MWCC68D_UART_RX_GPIO_PORT, GPIO_AF_7, ATK_MWCC68D_UART_RX_GPIO_PIN);

    /* Configure UART */
    usart_deinit(ATK_MWCC68D_UART_INTERFACE);   // 初始化UART
    usart_baudrate_set(ATK_MWCC68D_UART_INTERFACE, baudrate);   // 设置UART波特率
    usart_word_length_set(ATK_MWCC68D_UART_INTERFACE, USART_WL_8BIT);   // 设置UART数据位为8位
    usart_stop_bit_set(ATK_MWCC68D_UART_INTERFACE, USART_STB_1BIT);   // 设置UART停止位为1位
    usart_parity_config(ATK_MWCC68D_UART_INTERFACE, USART_PM_NONE);   // 设置UART无校验位
    usart_hardware_flow_rts_config(ATK_MWCC68D_UART_INTERFACE, USART_RTS_DISABLE);   // 禁用UART软件流控制
    usart_hardware_flow_cts_config(ATK_MWCC68D_UART_INTERFACE, USART_CTS_DISABLE);   // 禁用UART软件流控制
    usart_receive_config(ATK_MWCC68D_UART_INTERFACE, USART_RECEIVE_ENABLE);   // 使能UART接收
    usart_transmit_config(ATK_MWCC68D_UART_INTERFACE, USART_TRANSMIT_ENABLE);   // 使能UART发送
    usart_enable(ATK_MWCC68D_UART_INTERFACE);   // 使能UART

    /* Configure UART interrupt */
    nvic_irq_enable(ATK_MWCC68D_UART_IRQn, 2); // 使能UART中断，优先级为2
    
       /* Enable UART receive interrupt (includes overrun error interrupt) */
    usart_interrupt_enable(ATK_MWCC68D_UART_INTERFACE, USART_INT_RBNE); // 使能UART接收中断

    /* Configure TIMER for UART receive timeout detection */
    timer_deinit(ATK_MWCC68D_TIM_INTERFACE); // 初始化TIMER
    timer_parameter_struct timer_init_struct;    // 定义TIMER参数结构体
    timer_struct_para_init(&timer_init_struct); // 初始化TIMER参数结构体
    timer_init_struct.prescaler = ATK_MWCC68D_TIM_PRESCALER - 1; // 设置TIMER预分频器
    timer_init_struct.alignedmode = TIMER_COUNTER_EDGE; // 设置TIMER为边沿计数模式
    timer_init_struct.counterdirection = TIMER_COUNTER_UP; // 设置TIMER为向上计数
    timer_init_struct.period = 100 - 1;  // 设置TIMER周期为10ms
    timer_init_struct.clockdivision = TIMER_CKDIV_DIV1;  // 设置TIMER时钟分频为1
    timer_init(ATK_MWCC68D_TIM_INTERFACE, &timer_init_struct);  // 初始化TIMER

    /* Configure TIMER interrupt */
    nvic_irq_enable(ATK_MWCC68D_TIM_IRQn, 3); // 使能TIMER中断，优先级为3
    
    /* Enable TIMER update interrupt */
    timer_interrupt_enable(ATK_MWCC68D_TIM_INTERFACE, TIMER_INT_UP); // 使能TIMER更新中断
}

/**
 * @brief       ATK-MWCC68D TIMER interrupt handler
 * @param       none
 * @retval      none
 */
void ATK_MWCC68D_TIM_IRQHandler(void)
{
    if(timer_interrupt_flag_get(ATK_MWCC68D_TIM_INTERFACE, TIMER_INT_FLAG_UP) != RESET)  // 检查TIMER更新中断标志位是否为设置
    {
        timer_interrupt_flag_clear(ATK_MWCC68D_TIM_INTERFACE, TIMER_INT_FLAG_UP);  // 清除TIMER更新中断标志位
        g_uart_rx_frame.finsh = 1;       /* Mark frame receive complete */
        timer_disable(ATK_MWCC68D_TIM_INTERFACE);  /* Stop TIMER counting */
    }
}

/**
 * @brief       ATK-MWCC68D UART interrupt handler
 * @param       none
 * @retval      none
 */
void ATK_MWCC68D_UART_IRQHandler(void)
{
    uint8_t tmp;
    
    /* Handle UART overrun error interrupt */
    if(usart_flag_get(ATK_MWCC68D_UART_INTERFACE, USART_FLAG_ORERR) != RESET)  // 检查UART溢出错误标志位是否为设置
    {
        usart_flag_clear(ATK_MWCC68D_UART_INTERFACE, USART_FLAG_ORERR);     // 清除UART溢出错误标志位
        (void)usart_data_receive(ATK_MWCC68D_UART_INTERFACE); // 读取UART溢出数据，避免数据丢失
    }
    
    /* Handle UART receive interrupt */
    if(usart_flag_get(ATK_MWCC68D_UART_INTERFACE, USART_FLAG_RBNE) != RESET)  // 检查UART接收中断标志位是否为设置
    {
        tmp = usart_data_receive(ATK_MWCC68D_UART_INTERFACE);
        
        if(g_uart_rx_frame.len < (ATK_MWCC68D_UART_RX_BUF_SIZE - 1))  //检查UART接收缓冲区是否已满
        {
            TIMER_CNT(ATK_MWCC68D_TIM_INTERFACE) = 0;  /* Reset TIMER counter */   
            if(g_uart_rx_frame.len == 0)
            {
                timer_enable(ATK_MWCC68D_TIM_INTERFACE);  /* Start TIMER counting */ 
            }
            g_uart_rx_frame.buf[g_uart_rx_frame.len] = tmp;
            g_uart_rx_frame.len++;
        }
        else
        {
            /* Buffer overflow, reset */
            g_uart_rx_frame.len = 0;
            g_uart_rx_frame.buf[g_uart_rx_frame.len] = tmp;
            g_uart_rx_frame.len++;
        }
    }
}
