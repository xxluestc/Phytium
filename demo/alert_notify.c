/*
 * 电网安全预警通知模块 v2.0
 * 支持: 飞书Bot / 企业微信Bot / 邮件 / 本地日志
 *
 * 配置: 修改下方 WEBHOOK_URL 和 PLATFORM 即可
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ═══════════ 配置 ═══════════ */
#define PUSHPLUS_TOKEN  "292555a7b922440f8bd36bc79fe6e867"
#define LOG_FILE        "/tmp/grid_alerts.log"
#define MAX_LOG_LINES   500

/* ═══════════ 告警级别 ═══════════ */
typedef enum { LEVEL_NORMAL=0, LEVEL_WARN=1, LEVEL_DANGER=2 } AlertLevel;

/* ═══════════ 告警消息模板 ═══════════ */
typedef struct {
    AlertLevel level;
    char source[32];      /* 告警来源: weather/sensor/system */
    char title[128];      /* 告警标题 */
    char detail[512];     /* 详细信息 */
    char action[256];     /* 建议措施 */
    time_t timestamp;
} AlertMsg;

/* ─── 通过curl发送HTTP请求 ─── */
static int curl_post(const char *url, const char *json) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "curl -s -X POST '%s' -H 'Content-Type: application/json' "
        "-d '%s' --connect-timeout 5 2>/dev/null &", url, json);
    return system(cmd);
}

/* ─── 获取当前时间字符串 ─── */
static const char *time_str(const AlertMsg *a) {
    static char buf[32];
    strftime(buf, sizeof(buf), "%m-%d %H:%M:%S", localtime(&a->timestamp));
    return buf;
}

/* ─── PushPlus 微信推送 ─── */
static int pushplus_notify(const AlertMsg *a) {
    if (!PUSHPLUS_TOKEN[0]) return -1;

    const char *bg = a->level == 2 ? "#dc2626" : (a->level == 1 ? "#d97706" : "#059669");
    const char *icons[] = {"🟢正常", "🟡预警", "🔴危险"};

    /* HTML格式消息, 手机微信显示效果好 */
    char content[1024];
    snprintf(content, sizeof(content),
        "<div style='padding:12px;border-left:4px solid %s;background:#fafafa;'>"
        "<h2 style='color:%s;margin:0 0 8px'>%s 电网安全告警</h2>"
        "<table style='width:100%%;font-size:14px'>"
        "<tr><td style='color:#666;width:60px'>来源</td><td><b>%s</b></td></tr>"
        "<tr><td style='color:#666'>详情</td><td>%s</td></tr>"
        "<tr><td style='color:#666'>建议</td><td style='color:%s'><b>%s</b></td></tr>"
        "<tr><td style='color:#666'>时间</td><td>%s</td></tr>"
        "</table>"
        "<p style='color:#999;font-size:11px;margin:8px 0 0'>Phytium Pi 电网安全监测系统</p></div>",
        bg, bg, icons[a->level > 2 ? 0 : a->level],
        a->source, a->detail, bg, a->action, time_str(a));

    /* URL 编码 (content) */
    char encoded[2048];
    char *p = encoded;
    for (char *c = content; *c; c++) {
        if (*c == ' ') *p++ = '+';
        else if (*c == '\n') { *p++ = '%'; *p++ = '0'; *p++ = 'A'; }
        else if (*c == '#' || *c == '%' || *c == '&' || *c == '=' || *c == '?' || *c == '\'' || *c == '"') {
            snprintf(p, 4, "%%%02X", (unsigned char)*c); p += 3;
        } else *p++ = *c;
    }
    *p = 0;

    /* 直接用GET方式 (更稳定) */
    char url[3072];
    snprintf(url, sizeof(url),
        "http://www.pushplus.plus/send?token=%s&title=%s&content=%s&template=html",
        PUSHPLUS_TOKEN,
        /* title也URL编码 */
        a->level == 2 ? "%F0%9F%94%B4%E7%94%B5%E7%BD%91%E5%8D%B1%E9%99%A9" :
        "%F0%9F%9F%A1%E7%94%B5%E7%BD%91%E9%A2%84%E8%AD%A6",
        encoded);

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
        "curl -s --connect-timeout 5 '%s' 2>/dev/null &", url);
    system(cmd);
    return 0;
}

/* ─── 发送通知 ─── */
static int push_notify(const AlertMsg *a) {
    return pushplus_notify(a);
}

/* ─── 日志记录 ─── */
static void log_alert(const AlertMsg *a) {
    FILE *fp = fopen(LOG_FILE, "a");
    if (!fp) return;
    fprintf(fp, "[%s] %s | %s | %s | %s\n",
            ctime(&a->timestamp),
            a->level == 2 ? "DANGER" : (a->level == 1 ? "WARN" : "NORMAL"),
            a->source, a->title, a->detail);
    fclose(fp);

    /* 日志滚动 */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "tail -%d %s > %s.tmp && mv %s.tmp %s",
             MAX_LOG_LINES, LOG_FILE, LOG_FILE, LOG_FILE, LOG_FILE);
    system(cmd);
}

/* ─── 发送告警 (统一入口) ─── */
int alert_send(AlertLevel level, const char *source,
               const char *title, const char *detail, const char *action) {
    if (level == LEVEL_NORMAL) return 0; /* 正常不推送 */

    AlertMsg a = {
        .level = level, .timestamp = time(NULL)
    };
    snprintf(a.source, sizeof(a.source), "%s", source);
    snprintf(a.title, sizeof(a.title), "%s", title);
    snprintf(a.detail, sizeof(a.detail), "%s", detail);
    snprintf(a.action, sizeof(a.action), "%s", action);

    log_alert(&a);
    int ret = push_notify(&a);

    printf("[ALERT] %s | %s → %s (push:%d)\n",
           a.level == 2 ? "🔴" : "🟡", a.source, a.title, ret);
    return ret;
}

/* ─── 天气告警 (供 weather_monitor 集成调用) ─── */
int alert_weather(int level, const char *msg, const char *suggest) {
    return alert_send(level, "weather",
                      "气象电网风险", msg, suggest);
}

/* ─── 传感器告警 ─── */
int alert_sensor(int level, int node_id, const char *param, float value, float threshold) {
    char title[128], detail[256], action[128];
    snprintf(title, sizeof(title), "节点%d %s异常", node_id, param);
    snprintf(detail, sizeof(detail), "节点%d %s=%.2f, 阈值=%.2f", node_id, param, value, threshold);
    snprintf(action, sizeof(action), "检查节点%d硬件状态, 必要时切备用电源", node_id);
    return alert_send(level, "sensor", title, detail, action);
}

/* ═══════════ 测试 ═══════════ */
int main(int argc, char *argv[]) {
    printf("=== 电网预警通知模块测试 ===\n\n");

    /* 测试1: 配置检查 */
    printf("[PLATFORM] PushPlus (微信推送)\n");
    printf("[TOKEN]  %s...%s\n",
           PUSHPLUS_TOKEN[0] ? PUSHPLUS_TOKEN : "(未配置)",
           PUSHPLUS_TOKEN[0] ? "" : "");

    /* 测试2: 各级别告警 (日志记录, WeCom仅在有Webhook时发送) */
    printf("─── 天气预警 (WARN) ───\n");
    alert_weather(LEVEL_WARN, "风速35km/h, 注意线路摆动",
                  "关注频率波动, 准备降功率");

    printf("\n─── 传感器告警 (DANGER) ───\n");
    alert_sensor(LEVEL_DANGER, 3, "电压", 245.0, 230.0);

    printf("\n─── 天气危险 (DANGER) ───\n");
    alert_weather(LEVEL_DANGER, "雷暴+大风55km/h, 断线风险",
                  "分布式电源切孤岛模式, 启动应急抢修");

    printf("\n─── 正常状态 (不推送) ───\n");
    alert_weather(LEVEL_NORMAL, "天气正常", "");

    printf("\n[INFO] 告警日志: %s\n", LOG_FILE);
    printf("[INFO] 如需测试企业微信推送, 请:\n");
    printf("  1. 企业微信创建群机器人\n");
    printf("  2. 修改 WEBHOOK_URL\n");
    printf("  3. 重新编译运行\n");

    return 0;
}
