#include "mwcc68_app.h"
#include "mwcc68_cfg.h"
#include "mwcc68_uart.h"
#include "systick.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static uint32_t usart0Timeout = 0;

uint8_t currentPacksize = LORA_PACKSIZE;
uint8_t currentWlrate = LORA_RATE;

uint8_t originalPacksize = LORA_PACKSIZE;
uint8_t originalWlrate = LORA_RATE;

uint8_t isHighSpeedMode = 0;

#define RX_BUFFER_SIZE   512
static uint8_t rxBuffer[RX_BUFFER_SIZE];
static volatile uint16_t rxWriteIndex = 0;
static volatile uint16_t rxReadIndex = 0;

#define PROTOCOL_HEADER1    0xAA    
#define PROTOCOL_HEADER2    0x55
#define CMD_PREPARE_CONFIG  0x01    //进入异常
#define CMD_BACK_TO_NORMAL  0x02    //退出异常

_LoRa_CFG LoRa_CFG = {
    .addr     = LORA_ADDR,
    .chn      = LORA_CHN,
    .netid    = LORA_NETID,
    .power    = LORA_TPOWER,
    .wlrate   = LORA_RATE,
    .wltime   = LORA_WLTIME,
    .wmode    = LORA_WMODE,
    .tmode    = LORA_TMODE,
    .packsize = LORA_PACKSIZE,
    .bps      = LORA_TTLBPS,
    .parity   = LORA_TTLPAR,
    .lbt      = LORA_LBT
};

_LORA_DEVICE_STA Lora_Device_Sta;

uint8_t hexCharToValue(char c)
{
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

uint16_t parseHexString(uint8_t *input, uint16_t inputLen, uint8_t *output, uint16_t outputMaxLen)
{
    uint16_t outIdx = 0;
    uint8_t state = 0;
    uint8_t highNibble = 0;

    for(uint16_t i = 0; i < inputLen && outIdx < outputMaxLen; i++) {
        char c = (char)input[i];

        if(c == ' ' || c == '\r' || c == '\n' || c == '\t') {
            continue;
        }

        if(isxdigit((unsigned char)c)) {
            if(state == 0) {
                highNibble = hexCharToValue(c);
                state = 1;
            } else {
                output[outIdx++] = (highNibble << 4) | hexCharToValue(c);
                state = 0;
            }
        }
    }
    return outIdx;
}

uint8_t LoRa_WaitAuxHigh(uint32_t timeout_ms)
{
    while (timeout_ms--) {
        if (LORA_AUX) {
            return 1;
        }
        cpu_delay_ms(1);
    }
    printf("[AUX DBG] pin=%d at timeout\r\n", LORA_AUX ? 1 : 0);
    return 0;
}

void LoRa_EnterConfigMode(void)
{
    LORA_MD0(1);
    printf("[INFO] Enter config mode (MD0=1)\r\n");
    cpu_delay_ms(500);
    Lora_Device_Sta = LORA_CONFG_STA;
}

void LoRa_ExitConfigMode(void)
{
    LORA_MD0(0);
    printf("[INFO] Exit config mode (MD0=0), waiting for module reboot...\r\n");
    if (!LoRa_WaitAuxHigh(5000)) {
        printf("[WARN] AUX timeout after exit config! Module may not be ready.\r\n");
    } else {
        printf("[INFO] Module ready (AUX high)\r\n");
    }
    Lora_Device_Sta = LORA_RX_STA;
}

uint8_t CheckReplyOK(void)
{
    uint8_t buf[64];
    uint32_t timeout = 500;
    while(timeout--) {
        uint16_t avail = usart1_data_available();
        if(avail > 0) {
            memset(buf, 0, sizeof(buf));
            uint16_t len = usart1_peek_data(buf, sizeof(buf) - 1);
            buf[len] = '\0';
            if(strstr((char*)buf, "OK") != NULL) {
                printf("[DEBUG] LoRa reply: %s", (char*)buf);
                usart1_clear_buffer();
                return 1;
            }
        }
        cpu_delay_ms(1);
    }
    uint16_t avail = usart1_data_available();
    if(avail > 0) {
        uint8_t errBuf[64];
        memset(errBuf, 0, sizeof(errBuf));
        uint16_t len = usart1_peek_data(errBuf, sizeof(errBuf) - 1);
        errBuf[len] = '\0';
        printf("[ERR] LoRa reply (timeout): %s\r\n", (char*)errBuf);
        usart1_clear_buffer();
    } else {
        printf("[ERR] LoRa no reply!\r\n");
    }
    return 0;
}

uint8_t LoRa_SetAddr(uint16_t addr)
{
    usart1_clear_buffer();
    char cmd[20];
    sprintf(cmd, "AT+ADDR=%02X,%02X", (addr >> 8) & 0xFF, addr & 0xFF);
    printf("[INFO] Send: %s\r\n", cmd);
    for(int i=0; cmd[i]; i++) {
        while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
        usart_data_transmit(USART1, cmd[i]);
    }
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\r');
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\n');

    if(CheckReplyOK()) {
        printf("[OK] ADDR set to 0x%04X\r\n", addr);
        return 1;
    }
    printf("[ERR] ADDR failed\r\n");
    return 0;
}

uint8_t LoRa_SetNetid(uint8_t netid)
{
    usart1_clear_buffer();
    char cmd[20];
    sprintf(cmd, "AT+NETID=%d", netid);
    printf("[INFO] Send: %s\r\n", cmd);
    for(int i=0; cmd[i]; i++) {
        while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
        usart_data_transmit(USART1, cmd[i]);
    }
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\r');
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\n');

    if(CheckReplyOK()) {
        printf("[OK] NETID set to %d\r\n", netid);
        return 1;
    }
    printf("[ERR] NETID failed\r\n");
    return 0;
}

uint8_t LoRa_SetChn(uint8_t chn)
{
    usart1_clear_buffer();
    char cmd[20];
    sprintf(cmd, "AT+CHN=%d", chn);
    printf("[INFO] Send: %s\r\n", cmd);
    for(int i=0; cmd[i]; i++) {
        while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
        usart_data_transmit(USART1, cmd[i]);
    }
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\r');
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\n');

    if(CheckReplyOK()) {
        printf("[OK] CHN set to %d\r\n", chn);
        return 1;
    }
    printf("[ERR] CHN failed\r\n");
    return 0;
}

uint8_t LoRa_SetPacksize(uint8_t packsize)
{
    usart1_clear_buffer();
    char cmd[20];
    sprintf(cmd, "AT+PACKSIZE=%d", packsize);
    printf("[INFO] Send: %s\r\n", cmd);
    for(int i=0; cmd[i]; i++) {
        while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
        usart_data_transmit(USART1, cmd[i]);
    }
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\r');
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\n');

    if(CheckReplyOK()) {
        printf("[OK] PACKSIZE set to %d\r\n", packsize);
        currentPacksize = packsize;
        return 1;
    }
    printf("[ERR] PACKSIZE failed\r\n");
    return 0;
}

uint8_t LoRa_SetWlrate(uint8_t wlrate)
{
    usart1_clear_buffer();
    char cmd[20];
    sprintf(cmd, "AT+WLRATE=%d", wlrate);
    printf("[INFO] Send: %s\r\n", cmd);
    for(int i=0; cmd[i]; i++) {
        while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
        usart_data_transmit(USART1, cmd[i]);
    }
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\r');
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\n');

    if(CheckReplyOK()) {
        printf("[OK] WLRATE set to %d\r\n", wlrate);
        currentWlrate = wlrate;
        return 1;
    }
    printf("[ERR] WLRATE failed\r\n");
    return 0;
}

uint8_t LoRa_SetTmode(uint8_t tmode)
{
    usart1_clear_buffer();
    char cmd[20];
    sprintf(cmd, "AT+TMODE=%d", tmode);
    printf("[INFO] Send: %s\r\n", cmd);
    for(int i=0; cmd[i]; i++) {
        while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
        usart_data_transmit(USART1, cmd[i]);
    }
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\r');
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\n');

    if(CheckReplyOK()) {
        printf("[OK] TMODE set to %d\r\n", tmode);
        return 1;
    }
    printf("[ERR] TMODE failed\r\n");
    return 0;
}

uint8_t LoRa_SetPower(uint8_t power)
{
    usart1_clear_buffer();
    char cmd[20];
    sprintf(cmd, "AT+POWER=%d", power);
    printf("[INFO] Send: %s\r\n", cmd);
    for(int i=0; cmd[i]; i++) {
        while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
        usart_data_transmit(USART1, cmd[i]);
    }
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\r');
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\n');

    if(CheckReplyOK()) {
        printf("[OK] POWER set to %d\r\n", power);
        return 1;
    }
    printf("[ERR] POWER failed\r\n");
    return 0;
}

uint8_t LoRa_SetBps(uint8_t bps)
{
    usart1_clear_buffer();
    char cmd[20];
    sprintf(cmd, "AT+BPS=%d", bps);
    printf("[INFO] Send: %s\r\n", cmd);
    for(int i=0; cmd[i]; i++) {
        while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
        usart_data_transmit(USART1, cmd[i]);
    }
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\r');
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, '\n');

    if(CheckReplyOK()) {
        printf("[OK] BPS set to %d\r\n", bps);
        return 1;
    }
    printf("[ERR] BPS failed\r\n");
    return 0;
}

void User_GpioInit(void)
{
    MD0_GPIO_CLK_ENABLE();
    AUX_GPIO_CLK_ENABLE();
    rcu_periph_clock_enable(RCU_GPIOB);
    gpio_mode_set(MD0_GPIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, MD0_GPIO_PIN);
    gpio_output_options_set(MD0_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, MD0_GPIO_PIN);
    gpio_mode_set(AUX_GPIO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, AUX_GPIO_PIN);

    /* AUX EXTI 上升沿中断: LoRa 模块每收完一包 AUX 从低变高 */
    rcu_periph_clock_enable(RCU_SYSCFG);
    syscfg_exti_line_config(EXTI_SOURCE_GPIOB, EXTI_SOURCE_PIN9);
    exti_init(EXTI_9, EXTI_INTERRUPT, EXTI_TRIG_RISING);
    exti_interrupt_enable(EXTI_9);
    exti_interrupt_flag_clear(EXTI_9);
    nvic_irq_enable(EXTI5_9_IRQn, 1);
}

uint8_t Lora_Cfgbuff[20] = {0};

void LoRa_Init(void)
{
    uint8_t retry = 0;

    rcu_periph_clock_enable(RCU_USART1);
    rcu_periph_clock_enable(RCU_GPIOA);

    gpio_af_set(GPIOA, GPIO_AF_7, GPIO_PIN_2);  // USART1_TX
    gpio_af_set(GPIOA, GPIO_AF_7, GPIO_PIN_3);  // USART1_RX
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_2 | GPIO_PIN_3);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_2);

    usart_deinit(USART1);
    usart_baudrate_set(USART1, 115200U);
    usart_word_length_set(USART1, USART_WL_8BIT);
    usart_stop_bit_set(USART1, USART_STB_1BIT);
    usart_parity_config(USART1, USART_PM_NONE);
    usart_receive_config(USART1, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART1, USART_TRANSMIT_ENABLE);
    usart_enable(USART1);

    nvic_irq_enable(USART1_IRQn, 1);    //优先级1<configMAX_SYSCALL(2)，不被FreeRTOS临界区屏蔽，ISR中禁止调用FreeRTOS API
    usart_interrupt_enable(USART1, USART_INT_RBNE);

    User_GpioInit();
    LoRa_EnterConfigMode();

    retry = 20;
    while (retry) {
        usart1_clear_buffer();
        uint8_t at_cmd[] = "AT\r\n";
        for(int i=0; i<sizeof(at_cmd)-1; i++) {
            while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
            usart_data_transmit(USART1, at_cmd[i]);
        }

        cpu_delay_ms(300);

        uint8_t atbuf[32];
        uint16_t avail = usart1_data_available();
        if(avail > 0) {
            usart1_peek_data(atbuf, sizeof(atbuf));
            if(strstr((char*)atbuf, "OK") != NULL) {
                usart1_clear_buffer();
                printf("[OK] AT OK\r\n");
                break;
            }
        } else {
            printf("[DEBUG] No reply, retrying...\r\n");
        }
        retry--;
    }

    if (!retry) {
        printf("[ERROR] LoRa init failed!\r\n");
        while (1) {
            printf("[DEBUG] Check wiring!\r\n");
            cpu_delay_ms(1000);
        }
    }

    printf("[INFO] Querying current settings...\r\n");
    {
        char qcmd[] = "AT+ADDR?\r\n";
        usart1_clear_buffer();
        for(int i=0; i<sizeof(qcmd)-1; i++) {
            while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
            usart_data_transmit(USART1, qcmd[i]);
        }
        cpu_delay_ms(500);
        uint16_t qavail = usart1_data_available();
        if(qavail > 0) {
            uint8_t qbuf[64];
            memset(qbuf, 0, sizeof(qbuf));
            uint16_t qlen = usart1_read_data(qbuf, sizeof(qbuf)-1);
            qbuf[qlen] = '\0';
            printf("[QUERY] ADDR? = %s\r\n", (char*)qbuf);
        } else {
            printf("[QUERY] ADDR? no reply\r\n");
        }
    }
    {
        char qcmd[] = "AT+CFG?\r\n";
        usart1_clear_buffer();
        for(int i=0; i<sizeof(qcmd)-1; i++) {
            while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
            usart_data_transmit(USART1, qcmd[i]);
        }
        cpu_delay_ms(500);
        uint16_t qavail = usart1_data_available();
        if(qavail > 0) {
            uint8_t qbuf[128];
            memset(qbuf, 0, sizeof(qbuf));
            uint16_t qlen = usart1_read_data(qbuf, sizeof(qbuf)-1);
            qbuf[qlen] = '\0';
            printf("[QUERY] CFG? = %s\r\n", (char*)qbuf);
        } else {
            printf("[QUERY] CFG? no reply\r\n");
        }
    }

    printf("[INFO] Configuring LoRa parameters...\r\n");
    LoRa_SetAddr(LORA_ADDR);
    cpu_delay_ms(100);
    LoRa_SetNetid(LORA_NETID);
    cpu_delay_ms(100);
    LoRa_SetChn(LORA_CHN);
    cpu_delay_ms(100);
    LoRa_SetPacksize(LORA_PACKSIZE);
    cpu_delay_ms(100);
    LoRa_SetWlrate(LORA_RATE);
    cpu_delay_ms(100);
    LoRa_SetTmode(LORA_TMODE);
    cpu_delay_ms(100);
    LoRa_SetPower(LORA_TPOWER);
    cpu_delay_ms(100);
    LoRa_SetBps(LORA_TTLBPS);

    printf("[INFO] Init OK! ADDR=%d, NETID=%d, CHN=%d, PACKSIZE=%d, WLRATE=%d, TMODE=%d, POWER=%d, BPS=%d\r\n",
           LORA_ADDR, LORA_NETID, LORA_CHN, LORA_PACKSIZE, LORA_RATE, LORA_TMODE, LORA_TPOWER, LORA_TTLBPS);
    LoRa_ExitConfigMode();
}

void LoRa_SendData_Direct(uint8_t* data, uint16_t len, uint16_t destAddr, uint8_t chn)
{
    printf("[SEND] %d bytes to LoRa (dest=0x%04X, chn=%d)\r\n", len, destAddr, chn);
    printf("[DATA] ");
    
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, (destAddr >> 8) & 0xFF);
    printf("%02X ", (destAddr >> 8) & 0xFF);
    
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, destAddr & 0xFF);
    printf("%02X ", destAddr & 0xFF);
    
    while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
    usart_data_transmit(USART1, chn);
    printf("%02X ", chn);
    
    for(uint16_t i=0; i<len; i++) {
        printf("%02X ", data[i]);
        while(usart_flag_get(USART1, USART_FLAG_TBE) == RESET);
        usart_data_transmit(USART1, data[i]);
    }
    printf("\r\n");
}

void LoRa_SendData(uint8_t* data, uint16_t len)
{
    LoRa_SendData_Direct(data, len, DEST_ADDR, LORA_CHN);
}

void LoRa_SendString(char* str)
{
    LoRa_SendData((uint8_t*)str, strlen(str));
}

uint8_t CheckDataAbnormal(uint8_t* data, uint16_t len)
{
    for(uint16_t i=0; i<len; i++) {
        if(data[i] == 0xAA) {
            return 1;
        }
    }
    return 0;
}

void ProcessDataAbnormal(void)
{
    uint8_t cmdBuf[3] = {PROTOCOL_HEADER1, PROTOCOL_HEADER2, CMD_PREPARE_CONFIG};

    printf("[WARN] ===== DATA ABNORMAL DETECTED! =====\r\n");
    printf("[WARN] Abnormal marker (0xAA) found in data!\r\n");
    printf("[INFO] Waiting for previous data to be sent...\r\n");

    cpu_delay_ms(500);

    usart1_clear_buffer();
    usart0_clear_buffer();

    printf("[INFO] Sending config switch command to LoRa B...\r\n");
    LoRa_SendData(cmdBuf, 3);

    cpu_delay_ms(500);

    printf("[INFO] LoRa A switching to high-speed mode...\r\n");
    LoRa_EnterConfigMode();
    cpu_delay_ms(100);
    LoRa_SetPacksize(LORA_PACKSIZE_240);
    cpu_delay_ms(100);
    LoRa_SetWlrate(7);
    cpu_delay_ms(100);
    LoRa_ExitConfigMode();

    isHighSpeedMode = 1;
    cpu_delay_ms(500);
    usart1_clear_buffer();

    printf("[OK] ===== SWITCHED TO HIGH-SPEED MODE! =====\r\n");
}

void ProcessBackToNormal(void)
{
    uint8_t cmdBuf[3] = {PROTOCOL_HEADER1, PROTOCOL_HEADER2, CMD_BACK_TO_NORMAL};

    printf("[INFO] ===== BACK TO NORMAL MODE REQUESTED! =====\r\n");
    printf("[INFO] Restoring normal mode...\r\n");

    usart1_clear_buffer();
    usart0_clear_buffer();

    LoRa_SendData(cmdBuf, 3);
    cpu_delay_ms(500);

    LoRa_EnterConfigMode();
    cpu_delay_ms(200);
    
    // 重新设置所有关键参数，确保与GD3同步
    LoRa_SetAddr(LORA_ADDR);
    cpu_delay_ms(200);
    LoRa_SetNetid(LORA_NETID);
    cpu_delay_ms(200);
    LoRa_SetChn(LORA_CHN);
    cpu_delay_ms(200);
    LoRa_SetPacksize(originalPacksize);
    cpu_delay_ms(200);
    LoRa_SetWlrate(originalWlrate);
    cpu_delay_ms(200);
    
    LoRa_ExitConfigMode();

    isHighSpeedMode = 0;
    cpu_delay_ms(1500);
    usart1_clear_buffer();

    printf("[OK] ===== RESTORED TO NORMAL MODE! =====\r\n");
}

void LoRa_ReceData(void)
{
    static uint32_t checkCounter = 0;
    checkCounter++;
    if(checkCounter > 50) {
        checkCounter = 0;
        uint16_t avail = usart1_data_available();
        if(avail > 0) {
            uint8_t rxbuf[128];
            uint16_t len = usart1_read_data(rxbuf, sizeof(rxbuf));
            printf("[LoRa RX]:");
            for(int i=0; i<len; i++) {
                printf("%02X ", rxbuf[i]);
            }
            printf("\r\n");
        }
    }
}

void Process_PC_Commands(void)
{
    uint8_t rawData[128];
    uint8_t parsedData[64];
    uint16_t parsedLen = 0;

    uint16_t avail = usart0_data_available();
    if(avail > 0) {
        usart0Timeout++;
        if(usart0Timeout > 5) {
            uint16_t rawLen = usart0_read_data(rawData, sizeof(rawData));

            parsedLen = parseHexString(rawData, rawLen, parsedData, 64);

            if((parsedLen == 3 && parsedData[0] == PROTOCOL_HEADER1 &&
                parsedData[1] == PROTOCOL_HEADER2 && parsedData[2] == CMD_BACK_TO_NORMAL) ||
               (rawLen >= 3 && rawData[0] == PROTOCOL_HEADER1 &&
                rawData[1] == PROTOCOL_HEADER2 && rawData[2] == CMD_BACK_TO_NORMAL)) {
                printf("[INFO] Received BACK_TO_NORMAL command from PC!\r\n");
                ProcessBackToNormal();
                usart0Timeout = 0;
                return;
            }

            printf("[PC RX] %d bytes raw: ", rawLen);
            for(int i=0; i<rawLen; i++) {
                printf("%02X ", rawData[i]);
            }
            printf("\r\n");

            if(parsedLen > 0) {
                printf("[PC RX] %d bytes parsed: ", parsedLen);
                for(int i=0; i<parsedLen; i++) {
                    printf("%02X ", parsedData[i]);
                }
                printf("\r\n");

                if(CheckDataAbnormal(parsedData, parsedLen)) {
                    printf("[INFO] Sending abnormal data first...\r\n");
                    LoRa_SendData(parsedData, parsedLen);
                    ProcessDataAbnormal();
                } else {
                    LoRa_SendData(parsedData, parsedLen);
                }
            } else {
                printf("[INFO] Treating as raw text data\r\n");
                if(CheckDataAbnormal(rawData, rawLen)) {
                    printf("[INFO] Sending abnormal data first...\r\n");
                    LoRa_SendData(rawData, rawLen);
                    ProcessDataAbnormal();
                } else {
                    LoRa_SendData(rawData, rawLen);
                }
            }

            usart0Timeout = 0;
        }
    } else {
        usart0Timeout = 0;
    }
}

uint16_t LoRa_Available(void)
{
    if(rxWriteIndex >= rxReadIndex) {
        return rxWriteIndex - rxReadIndex;
    } else {
        return RX_BUFFER_SIZE - rxReadIndex + rxWriteIndex;
    }
}

uint8_t LoRa_ReadByte(void)
{
    uint8_t data = 0;
    if(rxReadIndex != rxWriteIndex) {
        data = rxBuffer[rxReadIndex];
        rxReadIndex = (rxReadIndex + 1) % RX_BUFFER_SIZE;
    }
    return data;
}

uint16_t LoRa_ReadBytes(uint8_t *data, uint16_t maxLen)
{
    uint16_t count = 0;
    while(rxReadIndex != rxWriteIndex && count < maxLen) {
        data[count++] = rxBuffer[rxReadIndex];
        rxReadIndex = (rxReadIndex + 1) % RX_BUFFER_SIZE;
    }
    return count;
}

void LoRa_ClearBuffer(void)
{
    rxReadIndex = rxWriteIndex;
}

uint8_t LoRa_ReceiveData(uint8_t *buf, uint16_t *len, uint32_t timeout)
{
    uint32_t elapsed = 0;

    if (buf == NULL || len == NULL || *len == 0) {
        return ATK_MWCC68D_EINVAL;
    }

    while (elapsed < timeout) {
        uint16_t avail = usart1_data_available();
        if (avail > 0) {
            uint8_t rawBuf[256];
            uint16_t rawLen = usart1_read_data(rawBuf, sizeof(rawBuf));

            if (rawLen >= 3) {
                uint16_t payloadLen = rawLen - 3;
                if (payloadLen > *len) payloadLen = *len;
                for (uint16_t i = 0; i < payloadLen; i++) {
                    buf[i] = rawBuf[3 + i];
                }
                *len = payloadLen;
                return ATK_MWCC68D_EOK;
            }
        }
        cpu_delay_ms(1);
        elapsed++;
    }

    return ATK_MWCC68D_ETIMEOUT;
}

void MWCC68_Test(void)
{
    uint32_t pktCount = 0;

    printf("\r\n=== GD2 Bidirectional ===\r\n\n");

    while (1) {
        uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
        printf("[TX #%lu] 01 02 03 04\r\n", pktCount);
        LoRa_SendData(data, 4);
        pktCount++;

        usart1_clear_buffer();  // 清空接收缓冲区

        printf("[INFO] Listening for reply (15s)...\r\n");
        int gotReply = 0;
        for (int t = 0; t < 1500; t++) {
            uint16_t avail = usart1_data_available();   // 检查是否有数据可读
            if (avail > 0) {
                uint8_t buf[256];
                uint16_t len = usart1_read_data(buf, sizeof(buf));
                printf("[RX %d bytes] ", len);
                for (int i = 0; i < len; i++) printf("%02X ", buf[i]);
                printf("\r\n");
                gotReply = 1;
                break;
            }
            cpu_delay_ms(10);
        }

        if (!gotReply) {
            printf("[INFO] No reply received\r\n");
        }

        printf("[INFO] Cooldown 3s before next TX...\r\n");
        for (int t = 0; t < 300; t++) {
            cpu_delay_ms(10);
        }
    }
}
