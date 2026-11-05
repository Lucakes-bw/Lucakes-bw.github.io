#!/bin/bash
# 第 1 篇：先 4 个进程等磁盘（D 状态），再 4 个进程占 CPU，各 150 秒，每 15 秒记一次 /proc/loadavg
# 用法：scripts/loadexp.sh 数据文件（最好在限速的磁盘上，至少几百 MB）
FILE=${1:?需要一个数据文件}
cd "$(dirname "$0")/.."
phase() {
  echo "### $1 开始 $(cat /proc/loadavg)"
  for i in 1 2 3 4; do ./build/load $2 & done
  sleep 5; ps -o pid,stat,pcpu,comm -C load; vmstat 1 5
  for t in 15 30 45 60 75 90 105 120 135 150; do sleep 15; echo "t=$t $(cat /proc/loadavg)"; done
  pkill -f "build/load"; wait 2>/dev/null
}
phase "D 状态读盘" "dread $FILE"
sleep 300; echo "### 空闲 300 秒后 $(cat /proc/loadavg)"
phase "占 CPU" "spin 1000"
