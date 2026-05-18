#include "mwcc68_uart.h"
#include "systick.h"
#include "string.h"
#include <stdio.h>

static uint8_t  rx_buf[LORA_RX_BUF_SIZE];
static volatile uint16_t rx_wr;    /* ISR 写入位置 */
static volatile uint16_t rx_rd;    /* 任务读取位置 */
static volatile uint16_t rx_count; /* 缓冲中字节数 */

/* 帧边界队列: 由 master_recv 任务软超时后调用 usart1_mark_frame() 填充 */
static volatile uint16_t frame_pos[LORA_FRAME_QUEUE];
static volatile uint8_t  frame_head;
static volatile uint8_t  frame_tail;

static volatile uint32_t aux_int_cnt;

static uint8_t  usart0RxBuffer[512];
static volatile uint16_t USART0_RX_STA;
static volatile uint32_t usart0IntCount;

/*----------------------------------------------------------------------------
 *  ISR: USART0 调试串口接收
 *----------------------------------------------------------------------------*/
void USART0_IRQHandler(void)
{
    usart0IntCount++;
    if (usart_interrupt_flag_get(USART0, USART_INT_FLAG_RBNE) != RESET) {
        if (USART0_RX_STA < 256) {
            uint8_t data = usart_data_receive(USART0);
            usart0RxBuffer[USART0_RX_STA++] = data;
        }
        usart_interrupt_flag_clear(USART0, USART_INT_FLAG_RBNE);
    }
}

/*----------------------------------------------------------------------------
 *  ISR: USART1 LoRa 数据接收 (环形缓冲)
 *----------------------------------------------------------------------------*/
void USART1_IRQHandler(void)
{
    if (usart_interrupt_flag_get(USART1, USART_INT_FLAG_RBNE) != RESET) {
        uint8_t data = usart_data_receive(USART1);
        if (rx_count < LORA_RX_BUF_SIZE) {
            rx_buf[rx_wr] = data;
            rx_wr = (rx_wr + 1) % LORA_RX_BUF_SIZE;
            rx_count++;
        }
    }

    if (usart_flag_get(USART1, USART_FLAG_ORERR) != RESET) {
        usart_flag_clear(USART1, USART_FLAG_ORERR);
    }
    usart_interrupt_flag_clear(USART1, USART_INT_FLAG_ERR_NERR);
}

/*----------------------------------------------------------------------------
 *  ISR: AUX 上升沿 (仅计数, 帧边界由任务软超时标记)
 *----------------------------------------------------------------------------*/
void EXTI5_9_IRQHandler(void)
{
    if (exti_interrupt_flag_get(EXTI_9) != RESET) {
        exti_interrupt_flag_clear(EXTI_9);
        aux_int_cnt++;
    }
}

/*----------------------------------------------------------------------------
 *  旧 API: 兼容层
 *----------------------------------------------------------------------------*/
uint16_t usart1_data_available(void)
{
    return rx_count;
}

uint16_t usart1_read_data(uint8_t* buf, uint16_t maxLen)
{
    uint16_t len = (rx_count < maxLen) ? rx_count : maxLen;
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = rx_buf[rx_rd];
        rx_rd = (rx_rd + 1) % LORA_RX_BUF_SIZE;
    }
    rx_count -= len;
    return len;
}

void usart1_clear_buffer(void)
{
    rx_rd = rx_wr;
    rx_count = 0;
}

uint16_t usart1_peek_data(uint8_t* buf, uint16_t maxLen)
{
    uint16_t len = (rx_count < maxLen) ? rx_count : maxLen;
    uint16_t pos = rx_rd;
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = rx_buf[pos];
        pos = (pos + 1) % LORA_RX_BUF_SIZE;
    }
    buf[len] = '\0';
    return len;
}

/*----------------------------------------------------------------------------
 *  帧边界 API: 任务层软超时 → usart1_mark_frame() → usart1_read_frame()
 *----------------------------------------------------------------------------*/
uint8_t usart1_frame_available(void)
{
    return (frame_head != frame_tail) ? 1 : 0;
}

uint16_t usart1_get_rx_count(void)
{
    return rx_count;
}

/**
 * @brief 标记当前接收缓冲区位置为一帧数据的结束
 */
void usart1_mark_frame(void)
{
    uint8_t next = (frame_head + 1) % LORA_FRAME_QUEUE;
    if (next != frame_tail) {
        frame_pos[frame_head] = rx_wr;
        frame_head = next;
    }
}

/**
 * @brief 从 LoRa 接收缓冲区读取一帧数据
 * @param buf 目标缓冲区
 * @param maxLen 最大读取字节数
 * @return 实际读取字节数
 */
uint16_t usart1_read_frame(uint8_t* buf, uint16_t maxLen)
{
    if (frame_head == frame_tail) return 0;

    uint16_t end   = frame_pos[frame_tail]; // 这包的结束地址
    uint16_t start = rx_rd; // 这包的开始地址

    frame_tail = (frame_tail + 1) % LORA_FRAME_QUEUE;    // 更新帧尾

    uint16_t len = (end >= start) ? (end - start) : (LORA_RX_BUF_SIZE - start + end);
    if (len == 0) return 0;
    if (len > maxLen) len = maxLen;

    for (uint16_t i = 0; i < len; i++) {
        buf[i] = rx_buf[start];
        start = (start + 1) % LORA_RX_BUF_SIZE;
    }
    rx_rd   = end;
    rx_count -= len;
    return len;
}

/*----------------------------------------------------------------------------
 *  USART0 调试串口
 *----------------------------------------------------------------------------*/
int fputc(int ch, FILE *f)
{
    usart_data_transmit(USART0, (uint8_t)ch);
    while (usart_flag_get(USART0, USART_FLAG_TBE) == RESET);
    return ch;
}

void usart0_init(void)
{
    rcu_periph_clock_enable(RCU_USART0);
    rcu_periph_clock_enable(RCU_GPIOA);

    gpio_af_set(GPIOA, GPIO_AF_7, GPIO_PIN_9);
    gpio_af_set(GPIOA, GPIO_AF_7, GPIO_PIN_10);

    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_9 | GPIO_PIN_10);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);

    usart_deinit(USART0);
    usart_baudrate_set(USART0, 115200U);
    usart_word_length_set(USART0, USART_WL_8BIT);
    usart_stop_bit_set(USART0, USART_STB_1BIT);
    usart_parity_config(USART0, USART_PM_NONE);
    usart_receive_config(USART0, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_enable(USART0);

    nvic_irq_enable(USART0_IRQn, 3);
    usart_interrupt_enable(USART0, USART_INT_RBNE);
}

/*----------------------------------------------------------------------------
 *  LoRa 命令交互
 *----------------------------------------------------------------------------*/
uint8_t *lora_check_cmd(uint8_t *str)
{
    /* 旧 API 兼容桩 */
    (void)str;
    return NULL;
}

uint8_t lora_send_cmd(uint8_t *cmd, uint8_t *ack, uint16_t waittime)
{
    uint8_t res = 0;
    rx_count = 0;

    if ((uint32_t)cmd <= 0XFF) {
        while (usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
        usart_data_transmit(USART1, (uint8_t)(uint32_t)cmd);
    } else {
        uint8_t len = strlen((char*)cmd);
        for (uint8_t i = 0; i < len; i++) {
            while (usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
            usart_data_transmit(USART1, cmd[i]);
        }
        while (usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
        usart_data_transmit(USART1, '\r');
        while (usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
        usart_data_transmit(USART1, '\n');
    }

    if (ack && waittime) {
        while (--waittime) {
            cpu_delay_ms(10);
            if (rx_count > 0) {
                /* 简单检查 ACK */
                uint8_t ok = 1;
                for (uint8_t i = 0; ack[i]; i++) {
                    if (rx_buf[(rx_rd + i) % LORA_RX_BUF_SIZE] != ack[i]) {
                        ok = 0;
                        break;
                    }
                }
                if (ok) break;
                rx_count = 0;
            }
        }
        if (waittime == 0) res = 1;
    }
    return res;
}

uint16_t usart0_data_available(void)  { return USART0_RX_STA; }
uint16_t usart0_peek_data(uint8_t* buf, uint16_t maxLen) {
    uint16_t len = USART0_RX_STA;
    if (len > maxLen - 1) len = maxLen - 1;
    for (uint16_t i = 0; i < len; i++) buf[i] = usart0RxBuffer[i];
    buf[len] = '\0';
    return len;
}
uint16_t usart0_read_data(uint8_t* buf, uint16_t maxLen) {
    uint16_t len = USART0_RX_STA;
    if (len > maxLen) len = maxLen;
    for (uint16_t i = 0; i < len; i++) buf[i] = usart0RxBuffer[i];
    USART0_RX_STA = 0;
    return len;
}
void usart0_clear_buffer(void) { USART0_RX_STA = 0; }