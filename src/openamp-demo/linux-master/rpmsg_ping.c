#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>

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

int main(void)
{
    int fd;
    int tx_count = 0, rx_count = 0;
    char rx_buf[600];

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    setbuf(stdout, NULL);

    fd = open("/dev/rpmsg0", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "[ERROR] open /dev/rpmsg0: %s\n", strerror(errno));
        return 1;
    }
    printf("[INFO] /dev/rpmsg0 opened\n");

    printf("[INFO] Starting ping: DEVICE_CORE_CHECK (0x%04X)\n", DEVICE_CORE_CHECK);

    while (running && tx_count < 10) {
        ProtocolData tx = {.command = DEVICE_CORE_CHECK, .length = 0};
        ssize_t wret = write(fd, &tx, 6);
        tx_count++;
        if (wret < 0) {
            printf("[TX #%d] FAIL: %s\n", tx_count, strerror(errno));
        } else {
            printf("[TX #%d] OK %zd bytes\n", tx_count, wret);
        }

        usleep(300000);

        memset(rx_buf, 0, sizeof(rx_buf));
        ssize_t rret = read(fd, rx_buf, sizeof(rx_buf) - 1);
        if (rret > 0) {
            ProtocolData *pkt = (ProtocolData *)rx_buf;
            rx_count++;
            printf("[RX #%d] cmd=0x%04X len=%d total=%zd\n",
                   rx_count, pkt->command, pkt->length, rret);
        } else if (rret == 0) {
            /* no data */
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            printf("[RX] ERROR: %s\n", strerror(errno));
            break;
        }
    }

    printf("\n[RESULT] TX=%d RX=%d\n", tx_count, rx_count);
    close(fd);
    return (rx_count > 0) ? 0 : 1;
}