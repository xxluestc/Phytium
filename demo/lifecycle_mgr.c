/*
 * Phytium Pi OpenAMP 生命周期管理器
 * B1: 快速启动优化  B2: 热重启监控  B3: 动态固件切换
 * 编译: aarch64-none-linux-gnu-gcc -Wall -O2 -o lifecycle_mgr lifecycle_mgr.c
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>

/* ═════════════ 配置 ═════════════ */
#define REMOTEPROC_PATH   "/sys/class/remoteproc/remoteproc0/state"
#define FIRMWARE_PATH     "/lib/firmware/openamp_core0.elf"
#define FW_BAREMETAL      "/lib/firmware/openamp_core0_bm.elf"
#define FW_FREERTOS       "/lib/firmware/openamp_core0.elf"
#define CHANNEL_OVERRIDE  "/sys/bus/rpmsg/devices/virtio0.rpmsg-openamp-demo-channel.-1.0/driver_override"
#define CHANNEL_BIND      "/sys/bus/rpmsg/drivers/rpmsg_chrdev/bind"
#define CHANNEL_NAME      "virtio0.rpmsg-openamp-demo-channel.-1.0"
#define MONITOR_INTERVAL  2
#define STATS_FILE        "/tmp/lifecycle_stats.json"

static volatile int g_running = 1;
static int g_boot_count = 0;
static int g_crash_count = 0;
static int g_hot_restart_count = 0;
static double g_last_boot_ms = 0;
static double g_best_boot_ms = 9999;
static double g_total_boot_ms = 0;
static const char *g_current_fw = "freertos";

void signal_handler(int sig) { g_running = 0; }

/* 执行shell命令 */
static int run_cmd(const char *cmd) {
    return system(cmd);
}

/* 读文件内容 */
static int read_file(const char *path, char *buf, int max) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, buf, max - 1);
    close(fd);
    if (n > 0) { buf[n] = 0; if (buf[n-1] == '\n') buf[n-1] = 0; }
    return n;
}

/* 写文件 */
static int write_file(const char *path, const char *val) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    int n = write(fd, val, strlen(val));
    close(fd);
    return n;
}

/* B1: 快速启动 — 计算并记录启动时间 */
static double fast_boot(void) {
    struct timespec t0, t1;
    char buf[64];

    /* 检查当前状态 */
    if (read_file(REMOTEPROC_PATH, buf, sizeof(buf)) > 0) {
        if (strstr(buf, "running")) {
            write_file(REMOTEPROC_PATH, "stop");
            usleep(500000);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* Step 1: 启动从核 */
    write_file(REMOTEPROC_PATH, "start");

    /* 等待 running (优化: 减少轮询间隔) */
    for (int i = 0; i < 500; i++) {
        if (read_file(REMOTEPROC_PATH, buf, sizeof(buf)) > 0 && strstr(buf, "running"))
            break;
        usleep(5000); /* 5ms轮询, 比默认10ms快 */
    }

    /* Step 2: 加载模块 (如果未加载) */
    run_cmd("modprobe rpmsg_char 2>/dev/null; modprobe rpmsg_ctrl 2>/dev/null");

    /* Step 3: 绑定通道 (如果未绑定) */
    struct stat st;
    if (stat("/dev/rpmsg0", &st) != 0) {
        write_file(CHANNEL_OVERRIDE, "rpmsg_chrdev");
        write_file(CHANNEL_BIND, CHANNEL_NAME);
        usleep(50000);
    }
    run_cmd("chmod 666 /dev/rpmsg0 /dev/rpmsg_ctrl0 2>/dev/null");

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1000000.0;

    g_boot_count++;
    g_last_boot_ms = elapsed;
    g_total_boot_ms += elapsed;
    if (elapsed < g_best_boot_ms) g_best_boot_ms = elapsed;

    return elapsed;
}

/* B2: 热重启 — 监控从核状态, 崩溃自动恢复 */
static int hot_restart_check(void) {
    char buf[64];
    if (read_file(REMOTEPROC_PATH, buf, sizeof(buf)) <= 0) return -1;

    if (strstr(buf, "crashed")) {
        printf("[B2] CRASH detected! Auto-recovering...\n");
        g_crash_count++;
        /* 尝试恢复 */
        write_file(REMOTEPROC_PATH, "stop");
        usleep(500000);
        double t = fast_boot();
        if (t > 0) {
            g_hot_restart_count++;
            printf("[B2] Hot restart OK: %.1f ms\n", t);
        }
        return 1;
    }
    return 0;
}

/* B3: 动态固件切换 */
static int switch_firmware(const char *fw_type) {
    printf("[B3] Switching firmware to: %s\n", fw_type);

    /* 停止当前固件 */
    write_file(REMOTEPROC_PATH, "stop");
    usleep(500000);

    /* 复制新固件 */
    const char *src = strcmp(fw_type, "baremetal") == 0 ? FW_BAREMETAL : FW_FREERTOS;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "cp %s " FIRMWARE_PATH, src);
    run_cmd(cmd);

    /* 重新启动 */
    double t = fast_boot();
    if (t > 0) {
        g_current_fw = fw_type;
        printf("[B3] Switched to %s, boot: %.1f ms\n", fw_type, t);
    }
    return (t > 0) ? 0 : -1;
}

/* 写入统计JSON */
static void write_stats(void) {
    char buf[512];
    double avg_boot = g_boot_count > 0 ? g_total_boot_ms / g_boot_count : 0;
    snprintf(buf, sizeof(buf),
        "{\"b1_boot_count\":%d,\"b1_last_boot_ms\":%.1f,\"b1_best_boot_ms\":%.1f,\"b1_avg_boot_ms\":%.1f,"
        "\"b2_crash_count\":%d,\"b2_hot_restart\":%d,"
        "\"b3_current_fw\":\"%s\",\"baseline_boot_ms\":536}\n",
        g_boot_count, g_last_boot_ms, g_best_boot_ms, avg_boot,
        g_crash_count, g_hot_restart_count, g_current_fw);
    int fd = open(STATS_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { write(fd, buf, strlen(buf)); close(fd); }
}

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("=== Phytium Pi OpenAMP Lifecycle Manager ===\n");
    printf("B1: Fast Boot   B2: Hot Restart   B3: FW Switch\n\n");

    /* B1: 执行一次快速启动 */
    printf("[B1] Fast boot test...\n");
    double t = fast_boot();
    printf("[B1] Boot time: %.1f ms (baseline: 536ms, best: %.1f ms)\n", t, g_best_boot_ms);
    write_stats();

    /* B3: 测试固件切换 (如果命令行指定) */
    if (argc > 1) {
        switch_firmware(argv[1]);
        write_stats();
    }

    /* B2: 监控循环 */
    printf("[B2] Monitoring remoteproc state (Ctrl+C to stop)...\n");
    while (g_running) {
        hot_restart_check();
        write_stats();
        sleep(MONITOR_INTERVAL);
    }

    printf("\n=== Lifecycle Summary ===\n");
    printf("Boots: %d, Crashes: %d, HotRestarts: %d\n", g_boot_count, g_crash_count, g_hot_restart_count);
    printf("Best boot: %.1f ms, Avg: %.1f ms\n", g_best_boot_ms,
           g_boot_count > 0 ? g_total_boot_ms / g_boot_count : 0);
    printf("Current firmware: %s\n", g_current_fw);
    return 0;
}
