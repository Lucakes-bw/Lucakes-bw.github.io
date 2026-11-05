#!/bin/bash
# 搭建实验网络：三个网络命名空间 c（客户端）— r（路由器）— s（服务器）
#   c: 10.0.1.2/24，默认路由经 10.0.1.1
#   r: 10.0.1.1/24 和 10.0.2.1/24，开启转发
#   s: 10.0.2.2/24，默认路由经 10.0.2.1
# 用法：scripts/lab.sh up | down
set -e
if [ "$1" = down ]; then
    for n in c r s; do ip netns del $n 2>/dev/null || true; done
    exit 0
fi
for n in c r s; do ip netns add $n; ip -n $n link set lo up; done
ip link add c-r type veth peer name r-c
ip link add r-s type veth peer name s-r
ip link set c-r netns c; ip link set r-c netns r
ip link set r-s netns r; ip link set s-r netns s
ip -n c addr add 10.0.1.2/24 dev c-r; ip -n c link set c-r up
ip -n r addr add 10.0.1.1/24 dev r-c; ip -n r link set r-c up
ip -n r addr add 10.0.2.1/24 dev r-s; ip -n r link set r-s up
ip -n s addr add 10.0.2.2/24 dev s-r; ip -n s link set s-r up
ip -n c route add default via 10.0.1.1
ip -n s route add default via 10.0.2.1
ip netns exec r sysctl -qw net.ipv4.ip_forward=1
# 关掉分段卸载，让抓包看到的和真实网线上的一样（《TCP 的可靠传输》3.2 节）
for p in "c c-r" "r r-c" "r r-s" "s s-r"; do
    set -- $p
    ip netns exec $1 ethtool -K $2 tso off gso off gro off >/dev/null 2>&1 || true
done
ip netns exec c ping -c 1 -W 1 10.0.2.2 >/dev/null && echo "实验网络就绪：c 10.0.1.2 → r → s 10.0.2.2"
