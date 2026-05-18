#ifndef _MWCC68_UART_H
#define _MWCC68_UART_H

#include "gd32l23x.h"

#define LORA_RX_BUF_SIZE   1024
#define LORA_FRAME_QUEUE   8

void usart0_init(void);
void usart1_init(void);

uint16_t usart1_data_available(void);
uint16_t usart1_read_data(uint8_t* buf, uint16_t maxLen);
void     usart1_clear_buffer(void);
uint16_t usart1_peek_data(uint8_t* buf, uint16_t maxLen);

uint8_t  usart1_frame_available(void);
uint16_t usart1_read_frame(uint8_t* buf, uint16_t maxLen);
uint16_t usart1_get_rx_count(void);
void     usart1_mark_frame(void);

uint16_t usart0_data_available(void);
uint16_t usart0_read_data(uint8_t* buf, uint16_t maxLen);
void     usart0_clear_buffer(void);
uint16_t usart0_peek_data(uint8_t* buf, uint16_t maxLen);

uint8_t *lora_check_cmd(uint8_t *str);
uint8_t  lora_send_cmd(uint8_t *cmd, uint8_t *ack, uint16_t waittime);

#endif