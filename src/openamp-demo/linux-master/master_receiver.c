#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>

#define DEVICE_MASTER_DATA  0x0020U
#define DEVICE_MASTER_CMD   0x0021U
#define DEVICE_SENSOR_BATCH 0x0011U

#define MAX_DATA_LENGTH     496

typedef struct __attribute__((packed)) {
    uint32_t command;
    uint16_t length;
    char data[MAX_DATA_LENGTH];
} ProtocolData;

static int rpmsg_fd = -1;
static int running = 1;
static int total_cmd_rx = 0;
static int total_other_rx = 0;
static int total_reads = 0;
static int total_empty = 0;

void signal_handler(int sig)
{
    printf("\n[INFO] Signal %d, stopping... (reads:%d empty:%d cmd:%d other:%d)\n",
           sig, total_reads, total_empty, total_cmd_rx, total_other_rx);
    running = 0;
}

static void print_master_cmd(const ProtocolData *pkt)
{
    if (pkt->length < 2) return;
    uint8_t node_id = (uint8_t)pkt->data[0];
    uint8_t cmd_code = (uint8_t)pkt->data[1];
    const char *cmd_name = "UNKNOWN";

    switch (cmd_code) {
    case 0x01: cmd_name = "REQ_WAVE"; break;
    case 0x02: cmd_name = "REQ_FAULT_LIST"; break;
    case 0x03: cmd_name = "CLEAR_FLASH"; break;
    case 0x04: cmd_name = "WAVE_COLLECT"; break;
    case 0x10: cmd_name = "REQ_WAVE(ext)"; break;
    case 0x11: cmd_name = "REQ_FAULT_LIST(ext)"; break;
    case 0x12: cmd_name = "CLEAR_FLASH(ext)"; break;
    case 0x13: cmd_name = "WAVE_COLLECT(ext)"; break;
    }
    printf("[CMD  #%03d] node=%d cmd=%s(0x%02X) params=%d",
           total_cmd_rx, node_id, cmd_name, cmd_code, pkt->length - 2);
    if (pkt->length > 2) {
        printf(" [");
        for (int i = 2; i < pkt->length && i < 20; i++)
            printf("%02X ", (unsigned char)pkt->data[i]);
        printf("]");
    }
    printf("\n");
}

int main(int argc, char *argv[])
{
    char rx_buf[600];
    int ret;
    int monitor_mode = 0;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    setbuf(stdout, NULL);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--monitor") == 0) {
            monitor_mode = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("\n用法: %s [--monitor]\n\n", argv[0]);
            printf("  --monitor    仅监听FreeRTOS主控命令\n");
            printf("  --help,-h    显示帮助\n\n");
            return 0;
        }
    }

    printf("\n");
    printf("==========================================\n");
    printf(" OpenAMP Master Data Receiver\n");
    printf(" Phytium Pi PE2204 - Linux Side\n");
    printf("==========================================\n\n");
    printf("[CONFIG] Channel: rpmsg-openamp-demo-channel\n");
    printf("[CONFIG] DEVICE_MASTER_CMD: 0x%04X\n", DEVICE_MASTER_CMD);
    printf("[CONFIG] Monitor only: %s\n\n", monitor_mode ? "YES" : "NO");

    rpmsg_fd = open("/dev/rpmsg0", O_RDWR | O_NONBLOCK);
    if (rpmsg_fd < 0) {
        fprintf(stderr, "[ERROR] open /dev/rpmsg0: %s\n", strerror(errno));
        fprintf(stderr, "[HINT]  remoteproc: echo start > /sys/class/remoteproc/remoteproc0/state\n");
        return 1;
    }
    printf("[INFO] /dev/rpmsg0 opened (no ctrl_fd)\n");
    usleep(100000);

    {
        ProtocolData ping = {.command = DEVICE_MASTER_DATA, .length = 0};
        ssize_t sret = write(rpmsg_fd, &ping, 6);
        if (sret < 0) {
            fprintf(stderr, "[ERROR] handshake write: %s\n", strerror(errno));
        } else {
            printf("[INFO] Handshake sent (%zd bytes)\n", sret);
        }
        usleep(200000);
    }

    printf("[INFO] Listening for master commands...\n");
    printf("[INFO] Press Ctrl+C to stop.\n\n");

    while (running) {
        memset(rx_buf, 0, sizeof(rx_buf));
        ret = read(rpmsg_fd, rx_buf, sizeof(rx_buf) - 1);
        total_reads++;

        if (ret > 0) {
            ProtocolData *pkt = (ProtocolData *)rx_buf;
            if (ret < 6) continue;

            switch (pkt->command) {
            case DEVICE_MASTER_CMD:
                total_cmd_rx++;
                print_master_cmd(pkt);
                break;
            case DEVICE_SENSOR_BATCH:
                total_other_rx++;
                break;
            default:
                total_other_rx++;
                if (total_other_rx <= 5) {
                    printf("[INFO] RX cmd=0x%04X len=%d total=%d bytes (ignored)\n",
                           pkt->command, pkt->length, ret);
                }
                break;
            }
        } else if (ret < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                fprintf(stderr, "[ERROR] read: %s\n", strerror(errno));
                break;
            }
            total_empty++;
        }

        if (total_reads % 1000 == 0) {
            printf("[STATS] reads=%d empty=%d cmd=%d other=%d\n",
                   total_reads, total_empty, total_cmd_rx, total_other_rx);
        }

        usleep(10000);
    }

    printf("\n[SUMMARY] Reads: %d, Empty: %d, Cmds: %d, Other: %d\n",
           total_reads, total_empty, total_cmd_rx, total_other_rx);

    if (rpmsg_fd >= 0) close(rpmsg_fd);
    return 0;
}