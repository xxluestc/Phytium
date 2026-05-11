#!/bin/bash
# OpenAMP 一键停止 (在开发板上直接运行)
# 用法: ~/stop-openamp.sh

echo '=== Phytium Pi OpenAMP 停止 ==='
PASS="user"

echo '[1/4] 停止服务...'
killall dashboard_server 2>/dev/null && echo '  面板已停止'
killall lifecycle_mgr 2>/dev/null
sleep 1
killall -9 dashboard_server lifecycle_mgr 2>/dev/null

echo '[2/4] 停止从核...'
echo "$PASS" | sudo -S sh -c 'echo stop > /sys/class/remoteproc/remoteproc0/state' 2>/dev/null
sleep 1
echo "  从核: $(cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null)"

echo '[3/4] 卸载模块...'
echo "$PASS" | sudo -S rmmod rpmsg_ctrl 2>/dev/null
echo "$PASS" | sudo -S rmmod rpmsg_char 2>/dev/null

echo '[4/4] 清理...'
rm -f /tmp/openamp.running /tmp/dashboard.log /tmp/lifecycle.log
DEVCOUNT=$(ls /dev/rpmsg* 2>/dev/null | wc -l)
echo "  剩余 /dev/rpmsg: $DEVCOUNT 个"
[ "$DEVCOUNT" -gt 10 ] && echo "  [WARN] 端点残留过多, 建议重启系统"

echo '========================================'
echo '  OpenAMP 已停止'
echo '========================================'
