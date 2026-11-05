#!/bin/bash
# 第 2 篇：5 个客户端同时连上服务器，2 秒后路由器丢弃所有转发的包（相当于网线断了、对方死机）
cd "$(dirname "$0")/.."
ip netns exec s ./build/dead server 8080 & SRV=$!
sleep 0.5
run() { local name=$1; shift; ( ip netns exec c stdbuf -oL ./build/dead client 10.0.2.2 8080 "$@" | sed -u "s/^/[$name] /" ) & }
run A idle
run B idle ka=10,5,3
run C send
run D send ut=10000
run E send ka=10,5,3
sleep 2
ip netns exec r iptables -A FORWARD -j DROP
echo "[$(date +%T)] 路由器开始丢弃所有包"
for t in 30 120 300 600; do
  sleep $(( t - ${last:-0} )); last=$t
  echo "=== 断开 $t 秒：c 上的连接"; ip netns exec c ss -tion state established "( dport = :8080 )" | grep -o "timer:([^)]*)\|retrans:[^ ]*\|rto:[^ ]*\|unacked:[^ ]*\|lastsnd:[^ ]*" | paste -sd" " -
  echo "=== s 上的连接数：$(ip netns exec s ss -tn state established "( sport = :8080 )" | tail -n +2 | wc -l)"
done
sleep 400
echo "=== 1000 秒，结束"
ip netns exec r iptables -F FORWARD
kill $SRV; pkill -f "build/dead client"; wait 2>/dev/null
