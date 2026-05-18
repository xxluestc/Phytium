#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>

#define DEVICE_MASTER_DATA 0x0020U
#define DEVICE_MASTER_CMD  0x0021U
#define DEVICE_SENSOR_DATA 0x0010U
#define DEVICE_SENSOR_BATCH 0x0011U
#define DEVICE_CORE_CHECK  0x0003U

#define MAX_DATA_LENGTH    496

typedef struct __attribute__((packed)) {
    uint32_t command;
    uint16_t length;
    char data[MAX_DATA_LENGTH];
} ProtocolData;

static int running = 1;

void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

static int send_and_recv(int fd, uint32_t cmd, const char *name)
{
    ProtocolData tx = {.command = cmd, .length = 0};
    ssize_t wret = write(fd, &tx, 6);
    if (wret < 0) {
        printf("[TX] %s FAIL: %s\n", name, strerror(errno));
        return -1;
    }
    printf("[TX] %s OK\n", name);

    usleep(300000);

    char rx_buf[600];
    memset(rx_buf, 0, sizeof(rx_buf));
    ssize_t rret = read(fd, rx_buf, sizeof(rx_buf) - 1);
    if (rret > 0) {
        ProtocolData *pkt = (ProtocolData *)rx_buf;
        printf("[RX] %s cmd=0x%04X len=%d total=%zd\n",
               name, pkt->command, pkt->length, rret);
        return 1;
    }
    if (rret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        printf("[RX] %s ERROR: %s\n", name, strerror(errno));
        return -1;
    }
    printf("[RX] %s (empty)\n", name);
    return 0;
}

int main(void)
{
    int fd;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    setbuf(stdout, NULL);

    fd = open("/dev/rpmsg0", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "[ERROR] open: %s\n", strerror(errno));
        return 1;
    }
    printf("[INFO] /dev/rpmsg0 opened\n\n");

    printf("=== TEST 1: CORE_CHECK → MASTER_DATA → CORE_CHECK ===\n");
    send_and_recv(fd, DEVICE_CORE_CHECK, "CORE_CHECK #1");
    send_and_recv(fd, DEVICE_MASTER_DATA, "MASTER_DATA");
    send_and_recv(fd, DEVICE_CORE_CHECK, "CORE_CHECK #2");
    printf("If CORE_CHECK #2 works, M33 is alive after MASTER_DATA\n\n");

    printf("=== TEST 2: More polling for MASTER_CMD from simulation ===\n");
    for (int i = 0; i < 30; i++) {
        char rx_buf[600];
        memset(rx_buf, 0, sizeof(rx_buf));
        ssize_t rret = read(fd, rx_buf, sizeof(rx_buf) - 1);
        if (rret > 0) {
            ProtocolData *pkt = (ProtocolData *)rx_buf;
            printf("[ASYNC RX] cmd=0x%04X len=%d total=%zd\n",
                   pkt->command, pkt->length, rret);
        }
        usleep(200000);
    }

    printf("[DONE]\n");
    close(fd);
    return 0;
}