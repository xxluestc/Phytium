/*
 * 电网安全预警通知模块 v3.0
 * 通过 Server酱 推送到微信 (免费, 每天5条, 无需认证)
 *
 * 编译: gcc -Wall -O2 -std=c11 -o alert_notify alert_notify.c
 * 测试: ./alert_notify
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ═══════════ 配置 ═══════════ */
#define SENDKEY      "SCT349501T29odKj78y05tBloqdBhfoxSv"
#define LOG_FILE     "/tmp/grid_alerts.log"
#define MAX_LOG      500

typedef enum { LEVEL_NORMAL=0, LEVEL_WARN=1, LEVEL_DANGER=2 } AlertLevel;

typedef struct {
    AlertLevel level;
    char source[32], title[128], detail[512], action[256];
    time_t timestamp;
} AlertMsg;

/* ─── Server酱发送 ─── */
static int push_notify(const AlertMsg *a) {
    if (!SENDKEY[0]) return -1;
    if (a->level == LEVEL_NORMAL) return 0;

    const char *icons[] = {"%F0%9F%9F%A2%E6%AD%A3%E5%B8%B8",     /* 🟢 */
                           "%F0%9F%9F%A1%E9%A2%84%E8%AD%A6",     /* 🟡 */
                           "%F0%9F%94%B4%E5%8D%B1%E9%99%A9"};    /* 🔴 */

    char ts[32]; strftime(ts, sizeof(ts), "%m-%d %H:%M", localtime(&a->timestamp));

    char desp[512];
    snprintf(desp, sizeof(desp),
        "来源: %s\n详情: %s\n建议: %s\n时间: %s\nPhytium Pi 电网监测",
        a->source, a->detail, a->action, ts);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "curl -s --connect-timeout 5 "
        "'https://sctapi.ftqq.com/%s.send?title=%s&desp=%s' "
        "> /dev/null 2>&1 &",
        SENDKEY, icons[a->level <= 2 ? a->level : 0], desp);
    system(cmd);
    return 0;
}

/* ─── 日志 ─── */
static void log_alert(const AlertMsg *a) {
    FILE *fp = fopen(LOG_FILE, "a");
    if (fp) {
        fprintf(fp, "[%s] %s | %s | %s\n",
                ctime(&a->timestamp),
                a->level == 2 ? "DANGER" : (a->level == 1 ? "WARN" : "OK"),
                a->source, a->detail);
        fclose(fp);
    }
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "tail -%d %s > %s.tmp && mv %s.tmp %s 2>/dev/null",
             MAX_LOG, LOG_FILE, LOG_FILE, LOG_FILE, LOG_FILE);
    system(cmd);
}

/* ─── 统一发送接口 ─── */
int alert_send(AlertLevel level, const char *source,
               const char *title, const char *detail, const char *action) {
    if (level == LEVEL_NORMAL) return 0;
    AlertMsg a = { .level = level, .timestamp = time(NULL) };
    snprintf(a.source, sizeof(a.source), "%s", source);
    snprintf(a.title, sizeof(a.title), "%s", title);
    snprintf(a.detail, sizeof(a.detail), "%s", detail);
    snprintf(a.action, sizeof(a.action), "%s", action);
    log_alert(&a);
    push_notify(&a);
    printf("[ALERT] %s | %s → %s\n",
           level == 2 ? "🔴" : "🟡", source, title);
    return 0;
}

/* ─── 便捷接口 ─── */
int alert_weather(int level, const char *msg, const char *suggest) {
    return alert_send(level, "weather", "气象电网风险", msg, suggest);
}
int alert_sensor(int level, int node, const char *param, float val, float thr) {
    char t[128], d[256], a[128];
    snprintf(t, sizeof(t), "节点%d %s异常", node, param);
    snprintf(d, sizeof(d), "节点%d %s=%.2f 阈值=%.2f", node, param, val, thr);
    snprintf(a, sizeof(a), "检查节点%d, 必要时切备用电源", node);
    return alert_send(level, "sensor", t, d, a);
}

/* ═══════════ 测试 ═══════════ */
int main(void) {
    printf("=== 电网预警通知测试 (Server酱) ===\n\n");

    if (!SENDKEY[0]) {
        printf("[CONFIG] SENDKEY 未配置\n");
        printf("  获取: https://sct.ftqq.com 微信扫码 → 复制SendKey\n");
        return 1;
    }
    printf("[CONFIG] SendKey: %s...\n\n", SENDKEY);

    printf("─── 天气预警 ───\n");
    alert_weather(LEVEL_WARN, "风速35km/h 注意线路摆动", "关注频率波动");

    printf("\n─── 传感器危险 ───\n");
    alert_sensor(LEVEL_DANGER, 3, "电压", 245.0, 230.0);

    printf("\n─── 天气危险 ───\n");
    alert_weather(LEVEL_DANGER, "雷暴+大风55km/h 断线风险", "切孤岛模式 启动抢修");

    printf("\n─── 正常 (不推送) ───\n");
    alert_weather(LEVEL_NORMAL, "天气正常", "");

    printf("\n[INFO] 日志: %s\n", LOG_FILE);
    printf("[INFO] 请检查微信是否收到3条告警消息\n");
    return 0;
}
