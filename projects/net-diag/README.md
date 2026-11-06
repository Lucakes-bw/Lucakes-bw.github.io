# 网络排障：实验程序

《计算机网络》专栏「网络排障」一章的实验程序，C11。文章中的输出都来自这些程序：Ubuntu 24.04 容器（以 `--privileged` 运行），内核 6.8，GCC 13.3，bpftrace 0.20.2。实验网络由三个网络命名空间组成：客户端 c（10.0.1.2）— 路由器 r — 服务器 s（10.0.2.2）。

```bash
make
scripts/lab.sh up                   # 搭建实验网络（需要 root）
ip netns exec s ./build/churn server 8080 &
ip netns exec c ./build/churn client 10.0.2.2 8080 1000
scripts/lab.sh down
```

| 程序 | 文章 | 内容 |
|---|---|---|
| `drops udprecv\|udpsend\|listen\|connect` | 内核在哪里丢包 | UDP 接收缓冲区满、路由器队列满、防火墙、连接跟踪表满 |
| `dead server\|client` | 对端死了，多久才能发现 | 空闲、keepalive、重传超时、`TCP_USER_TIMEOUT` |
| `bulk sink\|send` | 缓冲区与吞吐量 | 自动调优与 `SO_RCVBUF`、`SO_SNDBUF` |
| `churn server\|client\|reuse` | 短连接与端口耗尽 | TIME_WAIT 与本地端口；长连接 |
| `bulk blob\|fetch` | MTU 黑洞 | 路径 MTU 发现、黑洞、MSS 钳制 |

`scripts/` 中：

- `lab.sh`：搭建、拆除实验网络；
- `drops.bt`：按原因和位置统计内核丢弃的包（`bpftrace scripts/drops.bt`）；
- `deadexp.sh`：第 2 篇的实验，5 个客户端同时连接后断开网络，约 17 分钟。

有的实验会修改全局的内核参数（`net.core.rmem_max`、`net.netfilter.nf_conntrack_max`），做完要改回原来的值。
