/*
 * Phytium Pi OpenAMP Real-time Dashboard Server
 * 嵌入式HTTP服务器 + RPMsg传感器数据采集 + Web可视化面板
 * 运行: nohup ~/dashboard_server > /tmp/dashboard.log 2>&1 &
 * 停止: pkill -9 -f dashboard_server
 * 面板: http://192.168.88.11:8080
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>

/* ═══════════════════════════════════════════════════════════════
 *  配置参数 (按需修改)
 * ═══════════════════════════════════════════════════════════════ */
#define DASHBOARD_PORT          8080
#define CHANNEL_NAME            "rpmsg-openamp-demo-channel"
#define DEVICE_SENSOR_DATA      0x0010U
#define SENSOR_PACKET_COUNT     10          /* 每批传感器数据包数 */
#define BATCH_INTERVAL_SEC      2           /* 批次间隔(秒) */
#define MAX_LOG_LINES           30          /* 面板日志行数 */
#define MAX_CSV_LINES           100         /* CSV文件最大行数(超出滚动) */
#define CSV_LOG_PATH            "/tmp/dashboard_data.csv"
#define FIRMWARE_SIZE_MB        1.2f        /* openamp_core0.elf 大小 */
#define VRING_SIZE_KB           256         /* 两个vring总大小 */
#define SHM_TOTAL_MB            409         /* 共享内存总量 */
#define RPMSG_BUFFER_KB         256         /* RPMsg缓冲区估算 */
/* ═══════════════════════════════════════════════════════════════ */

#define RPMSG_ADDR_ANY 0xFFFFFFFF
#define RPMSG_CREATE_EPT_IOCTL _IOW(0xb5, 0x1, struct rpmsg_endpoint_info)
#define RPMSG_DESTROY_EPT_IOCTL _IO(0xb5, 0x2)

struct rpmsg_endpoint_info { char name[32]; uint32_t src; uint32_t dst; };
typedef struct { uint32_t command; uint16_t length; char data[496]; } ProtocolData;
typedef struct { uint32_t sensor_id; uint32_t timestamp; float voltage; float current; float temperature; uint8_t status; } SensorPacket;

/* 全局统计 */
static volatile int g_total_batches = 0;
static volatile int g_total_packets = 0;
static volatile float g_transfer_rate = 0;    /* pkt/s */
static volatile float g_bandwidth = 0;        /* B/s   */
static volatile float g_batch_latency_ms = 0; /* 批次往返延迟(Linux请求→收完10包) */
static volatile float g_pkt_latency_us = 0;   /* 单包平均延迟(微秒) */
static volatile float g_avg_latency_ms = 0;
static SensorPacket g_last_sensors[SENSOR_PACKET_COUNT];
static volatile int g_last_count = 0;
static volatile int g_running = 1;
static struct timespec g_batch_start, g_batch_end;

void signal_handler(int sig) { g_running = 0; }

/* ─── HTTP响应工具 ─── */
static void http_ok(int fd, const char *content_type) {
    dprintf(fd, "HTTP/1.0 200 OK\r\nContent-Type: %s\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Cache-Control: no-cache\r\n\r\n", content_type);
}

/* ─── JSON API: /stats ─── */
static void serve_json_stats(int fd) {
    float shm_used = FIRMWARE_SIZE_MB + (VRING_SIZE_KB + RPMSG_BUFFER_KB) / 1024.0f;
    char buf[4096];
    int len = snprintf(buf, sizeof(buf),
        "{"
        "\"batches\":%d,\"packets\":%d,"
        "\"rate\":%.1f,\"bandwidth\":%.1f,"
        "\"batch_latency_ms\":%.2f,\"pkt_latency_us\":%.1f,"
        "\"shm_total_mb\":%d,\"shm_used_mb\":%.1f,\"shm_used_pct\":%.1f,"
        "\"sensors\":[",
        g_total_batches, g_total_packets,
        g_transfer_rate, g_bandwidth,
        g_batch_latency_ms, g_pkt_latency_us,
        SHM_TOTAL_MB, shm_used, shm_used * 100.0f / SHM_TOTAL_MB);
    for (int i = 0; i < g_last_count && i < SENSOR_PACKET_COUNT; i++) {
        SensorPacket *s = (SensorPacket *)&g_last_sensors[i];
        len += snprintf(buf + len, sizeof(buf) - len,
            "{\"id\":%u,\"ts\":%u,\"v\":%.2f,\"a\":%.3f,\"t\":%.1f,\"s\":%u}%s",
            s->sensor_id, s->timestamp, s->voltage, s->current,
            s->temperature, s->status, (i < g_last_count - 1) ? "," : "");
    }
    len += snprintf(buf + len, sizeof(buf) - len, "]}");
    dprintf(fd, "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Cache-Control: no-cache\r\nContent-Length: %d\r\n\r\n", len);
    write(fd, buf, len);
}

/* ─── HTML 仪表盘页面 ─── */
static void serve_html(int fd) {
    const char *html =
"<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"UTF-8\">"
"<title>Phytium Pi OpenAMP Dashboard</title><style>"
"*{margin:0;padding:0;box-sizing:border-box}"
"body{background:#f0f4f8;color:#1a202c;font-family:'Segoe UI',system-ui,sans-serif;height:100vh;overflow:hidden}"
".header{background:linear-gradient(135deg,#1e40af,#3b82f6);padding:10px 20px;color:#fff;"
"display:flex;justify-content:space-between;align-items:center;box-shadow:0 2px 6px rgba(0,0,0,.1)}"
".header h1{font-size:18px;color:#fff;font-weight:700}"
".header .status{font-size:12px;padding:4px 12px;border-radius:12px;background:#10b981;color:#fff;font-weight:600}"
".main{display:flex;gap:12px;padding:12px;height:calc(100vh - 56px)}"
".left{flex:1;display:flex;flex-direction:column;gap:12px;min-width:400px}"
".right{width:420px;display:flex;flex-direction:column;gap:12px}"
".card{background:#fff;border:1px solid #e2e8f0;border-radius:8px;padding:14px;box-shadow:0 1px 3px rgba(0,0,0,.05)}"
".card h2{font-size:13px;color:#1e40af;margin-bottom:8px;padding-bottom:6px;border-bottom:2px solid #e8f0fe;font-weight:700}"
".arch-flow{display:flex;justify-content:center;align-items:center;gap:16px}"
".core{background:#fff;border:2px solid #e2e8f0;border-radius:10px;padding:12px 20px;text-align:center;box-shadow:0 2px 6px rgba(0,0,0,.04)}"
".core.linux{border-color:#3b82f6;background:#eff6ff}"
".core.freertos{border-color:#f59e0b;background:#fffbeb}"
".core h3{font-size:13px;font-weight:700;margin-bottom:2px}"
".core.linux h3{color:#1e40af}"
".core.freertos h3{color:#d97706}"
".core .cpu{font-size:10px;color:#64748b}"
".arrow{font-size:22px;color:#3b82f6;animation:pulse 1s infinite;font-weight:bold}"
"@keyframes pulse{0%,100%{opacity:1}50%{opacity:0.3}}"
".data-pipe{background:linear-gradient(135deg,#fef3c7,#fef9c3);border:2px dashed #f59e0b;border-radius:8px;padding:8px 14px;text-align:center}"
".data-pipe .label{font-size:10px;color:#92400e;font-weight:600}"
".data-pipe .value{font-size:12px;color:#d97706;font-weight:700;margin-top:2px}"
".stats-grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}"
".stat{background:linear-gradient(135deg,#f8fafc,#eff6ff);border-radius:6px;padding:10px 8px;text-align:center;border:1px solid #e2e8f0}"
".stat .num{font-size:22px;font-weight:800;color:#1e40af}"
".stat .lbl{font-size:10px;color:#64748b;margin-top:2px;font-weight:500}"
".sensor-table{width:100%;border-collapse:collapse;font-size:11px}"
".sensor-table th{background:#f1f5f9;padding:6px 6px;text-align:left;border-bottom:2px solid #cbd5e1;color:#475569;font-weight:700}"
".sensor-table td{padding:5px 6px;border-bottom:1px solid #e2e8f0;color:#334155}"
".sensor-table tr:hover{background:#f8fafc}"
".s-ok{color:#059669;font-weight:700}.s-warn{color:#d97706;font-weight:700}.s-err{color:#dc2626;font-weight:700}"
".rate-bar{background:#e2e8f0;border-radius:4px;height:20px;overflow:hidden;margin:4px 0}"
".rate-bar .fill{background:linear-gradient(90deg,#3b82f6,#06b6d4,#10b981);height:100%;transition:width 1s;border-radius:4px}"
".flow-text{text-align:center;font-size:11px;color:#64748b;margin-top:6px;font-weight:500}"
".ctrl{display:flex;gap:6px;margin-top:8px}"
".ctrl button{flex:1;padding:6px;border:none;border-radius:4px;font-size:11px;font-weight:600;cursor:pointer}"
".ctrl .btn-ok{background:#10b981;color:#fff}.ctrl .btn-warn{background:#f59e0b;color:#fff}.ctrl .btn-err{background:#ef4444;color:#fff}"
".log-box{font-size:10px;color:#64748b;max-height:120px;overflow-y:auto;line-height:1.4;font-family:monospace}"
"</style></head><body>"
"<div class=\"header\">"
"<h1>Phytium Pi OpenAMP 异构多核通信面板 — Linux(CPU0-2) ↔ FreeRTOS(CPU3)</h1>"
"<div class=\"status\" id=\"status\">● RUNNING</div></div>"
"<div class=\"main\"><div class=\"left\">"
"<div class=\"card\">"
"<div class=\"arch-flow\">"
"<div class=\"core linux\"><h3>Linux 主核</h3><div class=\"cpu\">CPU0-2 · FTC664+FTC310</div></div>"
"<div class=\"arrow\">⟷</div>"
"<div class=\"data-pipe\"><div class=\"label\">RPMsg+VirtIO</div><div class=\"value\">SGI9 | 409MB SHM</div></div>"
"<div class=\"arrow\">⟷</div>"
"<div class=\"core freertos\"><h3>FreeRTOS 从核</h3><div class=\"cpu\">CPU3 · FTC664</div></div>"
"</div>"
"<div class=\"flow-text\">10组传感器模拟数据 → FreeRTOS采集 → RPMsg发送 → Linux接收处理 → 实时显示</div></div>"
"<div class=\"card\"><div class=\"stats-grid\">"
"<div class=\"stat\"><div class=\"num\" id=\"batches\">-</div><div class=\"lbl\">完成批次</div></div>"
"<div class=\"stat\"><div class=\"num\" id=\"packets\">-</div><div class=\"lbl\">总数据包</div></div>"
"<div class=\"stat\"><div class=\"num\" id=\"rate\">-</div><div class=\"lbl\">吞吐率(pkt/s)</div></div>"
"<div class=\"stat\"><div class=\"num\" id=\"bandwidth\">-</div><div class=\"lbl\">有效带宽(B/s)</div></div>"
"<div class=\"stat\"><div class=\"num\" id=\"blat\">-</div><div class=\"lbl\">批次往返延迟(ms)*</div></div>"
"<div class=\"stat\"><div class=\"num\" id=\"plat\">-</div><div class=\"lbl\">单包平均延迟(μs)</div></div>"
"</div><div style=\"font-size:10px;color:#94a3b8;margin-top:4px\">"
"*批次往返延迟 = Linux发送请求 → FreeRTOS处理 → 10包全部收完的总时间</div>"
"<div class=\"rate-bar\" style=\"margin-top:8px\"><div class=\"fill\" id=\"rateBar\" style=\"width:0%\"></div></div></div>"
"<div class=\"card\"><h2>传输日志 (最近30条)</h2>"
"<div class=\"log-box\" id=\"logBox\">等待数据...</div></div></div>"
"<div class=\"right\">"
"<div class=\"card\"><h2>传感器数据</h2>"
"<table class=\"sensor-table\"><tr><th>ID</th><th>时间戳</th><th>电压V</th><th>电流A</th><th>温度°C</th><th>状态</th></tr>"
"<tbody id=\"sensorBody\"><tr><td colspan=\"6\" style=\"text-align:center\">等待数据...</td></tr></tbody></table></div>"
"<div class=\"card\"><h2>运行控制</h2>"
"<div style=\"font-size:11px;color:#64748b;margin-bottom:8px\">"
"<b>启动:</b> <code>nohup ~/dashboard_server &gt; /tmp/dashboard.log 2&gt;&amp;1 &amp;</code><br>"
"<b>停止:</b> <code>pkill -9 -f dashboard_server</code><br>"
"<b>日志:</b> <code>tail -f /tmp/dashboard.log</code><br>"
"<b>面板:</b> <a href=\"http://192.168.88.11:8080\" style=\"color:#3b82f6\">http://192.168.88.11:8080</a></div>"
"<div class=\"ctrl\"><button class=\"btn-warn\" onclick=\"fetch('/stats').then(r=>r.json()).then(d=>alert('状态正常: '+d.batches+'批, '+d.packets+'包'))\">📊 查看状态</button>"
"<button class=\"btn-err\" onclick=\"if(confirm('确定停止面板服务器?')){alert('请在SSH中执行: pkill -9 -f dashboard_server')}\">⏹ 停止面板</button></div></div>"
"<div class=\"card\"><h2>异构通信资源消耗</h2>"
"<div class=\"stats-grid\" style=\"grid-template-columns:1fr 1fr\">"
"<div class=\"stat\"><div class=\"num\" id=\"shmUsed\" style=\"font-size:18px;color:#059669\">-</div><div class=\"lbl\">共享内存使用</div></div>"
"<div class=\"stat\"><div class=\"num\" style=\"font-size:18px;color:#d97706\">SGI 9</div><div class=\"lbl\">IPI中断号</div></div>"
"<div class=\"stat\"><div class=\"num\" id=\"totalKB\" style=\"font-size:18px;color:#3b82f6\">-</div><div class=\"lbl\">累计传输(KB)</div></div>"
"<div class=\"stat\"><div class=\"num\" style=\"font-size:18px;color:#8b5cf6\">10</div><div class=\"lbl\">包/批次</div></div>"
"</div></div></div></div>"
"<script>"
"var logLines=[];"
"(function poll(){"
"fetch('/stats').then(r=>r.json()).then(d=>{"
"document.getElementById('batches').textContent=d.batches;"
"document.getElementById('packets').textContent=d.packets;"
"document.getElementById('rate').textContent=d.rate.toFixed(1);"
"document.getElementById('bandwidth').textContent=d.bandwidth.toFixed(1);"
"document.getElementById('blat').textContent=d.batch_latency_ms.toFixed(2);"
"document.getElementById('plat').textContent=d.pkt_latency_us.toFixed(0);"
"document.getElementById('shmUsed').textContent=d.shm_used_mb.toFixed(1)+'/'+d.shm_total_mb+'MB('+d.shm_used_pct.toFixed(1)+'%)';"
"document.getElementById('totalKB').textContent=(d.packets*36/1024).toFixed(1);"
"var pct=Math.min(100,d.rate/50*100);"
"document.getElementById('rateBar').style.width=pct+'%';"
"var t=new Date().toLocaleTimeString();"
"logLines.unshift(t+' | Batch '+d.batches+' | '+d.packets+'pkts | '+d.rate.toFixed(1)+'pkt/s | '+d.latency_ms.toFixed(1)+'ms');"
"if(logLines.length>20)logLines=logLines.slice(0,20);"
"document.getElementById('logBox').innerHTML=logLines.join('<br>');"
"if(d.sensors&&d.sensors.length>0){"
"var rows='';"
"d.sensors.forEach(function(s){"
"var cls=s.s==0?'s-ok':(s.s==1?'s-warn':'s-err');"
"var st=s.s==0?'NORMAL':(s.s==1?'WARN':'ERROR');"
"rows+='<tr><td>'+s.id+'</td><td>'+s.ts+'</td><td>'+s.v.toFixed(2)+'</td>'"
"+'<td>'+s.a.toFixed(3)+'</td><td>'+s.t.toFixed(1)+'</td>'"
"+'<td class=\"'+cls+'\">'+st+'</td></tr>';});"
"document.getElementById('sensorBody').innerHTML=rows;}"
"});setTimeout(poll,1000);})();"
"</script></body></html>";
    http_ok(fd, "text/html; charset=utf-8");
    write(fd, html, strlen(html));
}

/* ─── 极简HTTP服务器 ─── */
static void *http_server_thread(void *arg) {
    (void)arg;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return NULL; }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(8080), .sin_addr.s_addr = INADDR_ANY};
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return NULL; }
    listen(server_fd, 5);
    printf("[HTTP] Dashboard server on http://192.168.88.11:8080\n");

    while (g_running) {
        struct sockaddr_in client_addr; socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &len);
        if (client_fd < 0) { if (errno == EINTR) continue; break; }

        char req[2048] = {0};
        read(client_fd, req, sizeof(req) - 1);

        if (strncmp(req, "GET /stats", 10) == 0) serve_json_stats(client_fd);
        else serve_html(client_fd);
        close(client_fd);
    }
    close(server_fd);
    return NULL;
}

/* ─── RPMsg 通信线程 ─── */
static void *rpmsg_thread(void *arg) {
    (void)arg;
    int ctrl_fd, rpmsg_fd, ret;
    struct rpmsg_endpoint_info eptinfo = {0};
    time_t start_time = time(NULL);

    while (g_running) {
        ctrl_fd = open("/dev/rpmsg_ctrl0", O_RDWR);
        if (ctrl_fd < 0) { sleep(2); continue; }

        strncpy(eptinfo.name, "rpmsg-openamp-demo-channel", sizeof(eptinfo.name) - 1);
        eptinfo.src = RPMSG_ADDR_ANY; eptinfo.dst = 0;
        ret = ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &eptinfo);
        if (ret < 0) { close(ctrl_fd); sleep(2); continue; }

        rpmsg_fd = open("/dev/rpmsg0", O_RDWR | O_NONBLOCK);
        if (rpmsg_fd < 0) { ioctl(ctrl_fd, RPMSG_DESTROY_EPT_IOCTL); close(ctrl_fd); sleep(2); continue; }

        while (g_running) {
            /* 请求传感器数据 */
            ProtocolData tx = {.command = DEVICE_SENSOR_DATA, .length = 0};
            clock_gettime(CLOCK_MONOTONIC, &g_batch_start);
            ret = write(rpmsg_fd, &tx, 6);
            if (ret < 0) break;

            /* 接收10个数据包 */
            char rx_buf[512];
            int batch_count = 0;
            struct timespec t_start, t_now;
            clock_gettime(CLOCK_MONOTONIC, &t_start);

            while (batch_count < SENSOR_PACKET_COUNT && g_running) {
                ret = read(rpmsg_fd, rx_buf, sizeof(rx_buf) - 1);
                if (ret > 0) {
                    ProtocolData *pkt = (ProtocolData *)rx_buf;
                    if (pkt->command == DEVICE_SENSOR_DATA && pkt->length == sizeof(SensorPacket)) {
                        memcpy((void *)&g_last_sensors[batch_count], pkt->data, sizeof(SensorPacket));
                        batch_count++;
                        g_total_packets++;
                    }
                }
                usleep(500);
            }

            clock_gettime(CLOCK_MONOTONIC, &g_batch_end);
            clock_gettime(CLOCK_MONOTONIC, &t_now);

            if (batch_count == SENSOR_PACKET_COUNT) {
                g_total_batches++;
                g_last_count = batch_count;

                /* 计算速率 */
                double elapsed = (g_batch_end.tv_sec - g_batch_start.tv_sec) +
                                 (g_batch_end.tv_nsec - g_batch_start.tv_nsec) / 1e9;
                g_transfer_rate = (elapsed > 0) ? (float)(SENSOR_PACKET_COUNT / elapsed) : 0;
                g_bandwidth = g_transfer_rate * (sizeof(SensorPacket) + 6);
                g_batch_latency_ms = (float)(elapsed * 1000.0);
                g_pkt_latency_us = (elapsed > 0) ? (float)(elapsed * 1000000.0 / SENSOR_PACKET_COUNT) : 0;

                double uptime = difftime(time(NULL), start_time);
                g_transfer_rate = (uptime > 0) ? (float)(g_total_packets / uptime) : 0;
                g_bandwidth = g_transfer_rate * (sizeof(SensorPacket) + 6);
            }

            /* 记录CSV (仅保留最近MAX_CSV_LINES行) */
            if (batch_count == SENSOR_PACKET_COUNT) {
                static int csv_lines = 0;
                static FILE *log_fp = NULL;
                if (!log_fp) {
                    log_fp = fopen(CSV_LOG_PATH, "w");
                    if (log_fp) fprintf(log_fp, "#timestamp,batches,packets,rate_pkt_s,bandwidth_B_s,batch_latency_ms\n");
                }
                if (log_fp) {
                    if (csv_lines >= MAX_CSV_LINES) { fclose(log_fp); log_fp = fopen(CSV_LOG_PATH, "w");
                        if (log_fp) fprintf(log_fp, "#timestamp,batches,packets,rate_pkt_s,bandwidth_B_s,batch_latency_ms\n");
                        csv_lines = 0; }
                    fprintf(log_fp, "%ld,%d,%d,%.1f,%.1f,%.2f\n",
                            time(NULL), g_total_batches, g_total_packets,
                            g_transfer_rate, g_bandwidth, g_batch_latency_ms);
                    fflush(log_fp);
                    csv_lines++;
                }
            }

            sleep(BATCH_INTERVAL_SEC);
        }

        ioctl(ctrl_fd, RPMSG_DESTROY_EPT_IOCTL);
        close(rpmsg_fd);
        close(ctrl_fd);
        sleep(2);
    }
    return NULL;
}

int main(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║  Phytium Pi OpenAMP Dashboard Server             ║\n");
    printf("║  异构多核通信实时监控面板                          ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");
    printf("[INFO] HTTP Dashboard: http://192.168.88.11:8080\n");
    printf("[INFO] JSON Stats API: http://192.168.88.11:8080/stats\n\n");

    pthread_t http_tid, rpmsg_tid;
    pthread_create(&http_tid, NULL, http_server_thread, NULL);
    sleep(1);
    pthread_create(&rpmsg_tid, NULL, rpmsg_thread, NULL);

    while (g_running) {
        printf("\r[STATS] Batches: %d | Packets: %d | Rate: %.1f pkt/s | BW: %.1f B/s | Sensors: %d",
               g_total_batches, g_total_packets, g_transfer_rate, g_bandwidth, g_last_count);
        fflush(stdout);
        sleep(2);
    }

    printf("\n[INFO] Shutting down...\n");
    pthread_join(rpmsg_tid, NULL);
    pthread_join(http_tid, NULL);
    printf("[INFO] Dashboard stopped. Total: %d batches, %d packets\n", g_total_batches, g_total_packets);
    return 0;
}
