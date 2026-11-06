#!/bin/bash
# 第 4 篇：手工搭建 Docker 式的容器网络
#   宿主（当前的网络命名空间）上建一个网桥 br0（10.10.0.1/24），两个「容器」ctrA、ctrB 用 veth 接到网桥上；
#   再建一个命名空间 ext 代表外部的客户端，用另一对 veth 连到宿主（192.168.50.0/24）
# 用法：scripts/bridge.sh up | down
set -e
if [ "$1" = down ]; then
    for n in ctrA ctrB ext; do ip netns del $n 2>/dev/null || true; done
    ip link del br0 2>/dev/null || true
    iptables -t nat -F; iptables -F FORWARD
    exit 0
fi
ip link add br0 type bridge
ip addr add 10.10.0.1/24 dev br0
ip link set br0 up
i=2
for c in ctrA ctrB; do
    ip netns add $c
    ip link add veth-$c type veth peer name eth0 netns $c   # 一端留在宿主，另一端放进容器，改名为 eth0
    ip link set veth-$c master br0 up
    ip -n $c addr add 10.10.0.$i/24 dev eth0
    ip -n $c link set eth0 up
    ip -n $c link set lo up
    ip -n $c route add default via 10.10.0.1
    i=$((i + 1))
done
ip netns add ext
ip link add veth-ext type veth peer name eth0 netns ext
ip addr add 192.168.50.1/24 dev veth-ext
ip link set veth-ext up
ip -n ext addr add 192.168.50.2/24 dev eth0
ip -n ext link set eth0 up
ip -n ext link set lo up
sysctl -qw net.ipv4.ip_forward=1
# 容器访问外部：源地址伪装成宿主的地址（Docker 的 MASQUERADE 规则）
iptables -t nat -A POSTROUTING -s 10.10.0.0/24 ! -o br0 -j MASQUERADE
# 端口映射：访问宿主的 8080，转给 ctrA 的 80（相当于 docker run -p 8080:80）
iptables -t nat -A PREROUTING -p tcp --dport 8080 -j DNAT --to-destination 10.10.0.2:80
echo "就绪：ctrA 10.10.0.2，ctrB 10.10.0.3，网桥 10.10.0.1；外部客户端 ext 192.168.50.2 → 宿主 192.168.50.1:8080 → ctrA:80"
