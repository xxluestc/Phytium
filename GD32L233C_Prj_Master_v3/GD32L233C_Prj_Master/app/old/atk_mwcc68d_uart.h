#ifndef __ATK_MWCC68D_UART_H
#define __ATK_MWCC68D_UART_H

#include "gd32l23x.h"

/* Pin definitions - GD32L233 configuration */
/* USART1: TX=PA9, RX=PA10 (AF7) */
#define ATK_MWCC68D_UART_TX_GPIO_PORT           GPIOA
#define ATK_MWCC68D_UART_TX_GPIO_PIN            GPIO_PIN_9
#define ATK_MWCC68D_UART_TX_GPIO_CLK_ENABLE()   rcu_periph_clock_enable(RCU_GPIOA)

#define ATK_MWCC68D_UART_RX_GPIO_PORT           GPIOA
#define ATK_MWCC68D_UART_RX_GPIO_PIN            GPIO_PIN_10
#define ATK_MWCC68D_UART_RX_GPIO_CLK_ENABLE()   rcu_periph_clock_enable(RCU_GPIOA)

/* TIMER configuration - GD32L233 timers are TIMER0~5 */
#define ATK_MWCC68D_TIM_INTERFACE               TIMER2
#define ATK_MWCC68D_TIM_IRQn                    TIMER2_IRQn
#define ATK_MWCC68D_TIM_IRQHandler              TIMER2_IRQHandler
#define ATK_MWCC68D_TIM_CLK_ENABLE()            rcu_periph_clock_enable(RCU_TIMER2)
#define ATK_MWCC68D_TIM_PRESCALER               3200  // 32MHz/3200 = 10kHz

/* UART configuration - GD32L233 USART1 (USART2 does not exist) */
#define ATK_MWCC68D_UART_INTERFACE              USART1
#define ATK_MWCC68D_UART_IRQn                   USART1_IRQn
#define ATK_MWCC68D_UART_IRQHandler             USART1_IRQHandler
#define ATK_MWCC68D_UART_CLK_ENABLE()           rcu_periph_clock_enable(RCU_USART1)

/* UART�շ������С */
#define ATK_MWCC68D_UART_RX_BUF_SIZE            128
#define ATK_MWCC68D_UART_TX_BUF_SIZE            128

/* �������� */
void atk_mwcc68d_uart_printf(char *fmt, ...);       /* ATK-MWCC68D UART printf */
void atk_mwcc68d_uart_rx_restart(void);             /* ATK-MWCC68D UART���¿�ʼ�������� */
uint8_t *atk_mwcc68d_uart_rx_get_frame(void);       /* ��ȡATK-MWCC68D UART���յ���һ֡���� */
uint16_t atk_mwcc68d_uart_rx_get_frame_len(void);   /* ��ȡATK-MWCC68D UART���յ���һ֡���ݵĳ��� */
void atk_mwcc68d_uart_init(uint32_t baudrate);      /* ATK-MWCC68D UART��ʼ�� */

#endif